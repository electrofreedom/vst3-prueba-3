#include "VoiceDetector.h"

namespace rvr
{
    static float coef (double ms, double sr) { return (float) std::exp (-1.0 / (0.001 * ms * sr)); }

    void VoiceDetector::prepare (double sampleRate)
    {
        sr = sampleRate;
        aAttF = coef (0.5, sr);   aRelF = coef (15.0, sr);
        aAttS = coef (15.0, sr);  aRelS = coef (150.0, sr);
        setPhraseGapMs (350.0f);
        setLockoutMs (500.0f);
        setThresholdDb (-35.0f);
        reset();
    }

    void VoiceDetector::reset() noexcept
    {
        fast = slow = 0.0f;
        active = false;
        silentCount = 0;
        sinceStart = 1 << 28;
    }

    void VoiceDetector::setThresholdDb (float db) noexcept
    {
        thrOn  = dbToLinear (db);
        thrOff = thrOn * 0.708f;                  // histéresis de -3 dB
    }

    void VoiceDetector::setPhraseGapMs (float ms) noexcept { gapSamples    = std::max (1, (int) (0.001 * ms * sr)); }
    void VoiceDetector::setLockoutMs   (float ms) noexcept { lockoutSamples = std::max (0, (int) (0.001 * ms * sr)); }

    VoiceDetector::Event VoiceDetector::process (float a) noexcept
    {
        fast = a > fast ? aAttF * fast + (1.0f - aAttF) * a : aRelF * fast + (1.0f - aRelF) * a;
        slow = a > slow ? aAttS * slow + (1.0f - aAttS) * a : aRelS * slow + (1.0f - aRelS) * a;
        if (sinceStart < (1 << 28)) ++sinceStart;

        if (! active)
        {
            const bool transient = fast > thrOn && fast > 1.5f * slow;   // ataque brusco
            const bool sustained = slow > thrOn;                        // entrada gradual
            if ((transient || sustained) && sinceStart >= lockoutSamples)
            {
                active = true;
                silentCount = 0;
                sinceStart = 0;
                return Event::PhraseStart;
            }
        }
        else
        {
            if (fast < thrOff)
            {
                if (++silentCount >= gapSamples)
                {
                    active = false;
                    silentCount = 0;
                    return Event::PhraseEnd;
                }
            }
            else
                silentCount = 0;
        }
        return Event::None;
    }
}
