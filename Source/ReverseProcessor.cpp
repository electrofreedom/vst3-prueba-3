#include "ReverseProcessor.h"

namespace rvr
{
    std::unique_ptr<ReversedClip> ReverseProcessor::render (const float* const* captured, int numChannels, int numSamples,
                                                            double sr, const ReverbParams& params,
                                                            const std::atomic<int>* generation, int expectedGen)
    {
        if (numSamples < 16) return nullptr;
        const int nCh = numChannels < 2 ? 1 : 2;

        const int preSamples = (int) (0.001 * params.preDelayMs * sr);
        const int tailSamples = (int) (1.2 * std::min ((double) params.reverbTime, kMaxReverbSeconds) * sr);
        const int total = numSamples + preSamples + tailSamples;

        std::vector<float> inL ((size_t) total, 0.0f), inR ((size_t) total, 0.0f), oL ((size_t) total), oR ((size_t) total);
        const int fin = std::min (numSamples / 4, std::max (16, (int) (0.002 * sr)));
        const int fout = std::min (numSamples / 4, std::max (16, (int) (0.010 * sr)));
        for (int i = 0; i < numSamples; ++i)
        {
            float g = 1.0f;
            if (i < fin)                g = 0.5f - 0.5f * std::cos (kPi * (float) i / (float) fin);
            if (i >= numSamples - fout) g *= 0.5f + 0.5f * std::cos (kPi * (float) (i - (numSamples - fout)) / (float) fout);
            inL[(size_t) i] = captured[0][i] * g;
            inR[(size_t) i] = (nCh > 1 ? captured[1][i] : captured[0][i]) * g;
        }

        ReverbEngine engine;
        engine.prepare (sr);
        engine.setParams (params);

        constexpr int chunk = 4096;
        for (int pos = 0; pos < total; pos += chunk)
        {
            if (generation != nullptr && generation->load (std::memory_order_relaxed) != expectedGen)
                return nullptr;
            const int n = std::min (chunk, total - pos);
            engine.process (inL.data() + pos, inR.data() + pos, oL.data() + pos, oR.data() + pos, n);
        }

        // limitación de pico (sin clipping interno)
        float peak = 0.0f;
        for (int i = 0; i < total; ++i) peak = std::max (peak, std::max (std::fabs (oL[(size_t) i]), std::fabs (oR[(size_t) i])));
        if (peak > 0.95f)
        {
            const float g = 0.95f / peak;
            for (int i = 0; i < total; ++i) { oL[(size_t) i] *= g; oR[(size_t) i] *= g; }
        }

        // máximo de la envolvente (RMS suavizado ~40 ms) del reverb "hacia delante"
        const int win = std::max (8, (int) (0.040 * sr));
        std::vector<double> csum ((size_t) total + 1, 0.0);
        for (int i = 0; i < total; ++i)
        {
            const double a = oL[(size_t) i], b = oR[(size_t) i];
            csum[(size_t) i + 1] = csum[(size_t) i] + 0.5 * (a * a + b * b);
        }
        int fwdPeak = 0; double best = -1.0;
        for (int i = 0; i + win <= total; i += std::max (1, win / 8))
        {
            const double e = csum[(size_t) (i + win)] - csum[(size_t) i];
            if (e > best) { best = e; fwdPeak = i + win / 2; }
        }

        auto clip = std::make_unique<ReversedClip>();
        clip->numChannels = nCh;
        clip->length = total;
        clip->sampleRate = sr;
        clip->peakIdx = std::min (total - 1, std::max (0, total - 1 - fwdPeak));    // inversión del índice
        for (int c = 0; c < nCh; ++c)
        {
            clip->data[c].resize ((size_t) total);
            const auto& src = (c == 0 ? oL : oR);
            for (int i = 0; i < total; ++i) clip->data[c][(size_t) i] = src[(size_t) (total - 1 - i)];
        }
        if (nCh == 1)   // mono: suma de ambos canales del reverb
            for (int i = 0; i < total; ++i)
                clip->data[0][(size_t) i] = 0.5f * (oL[(size_t) (total - 1 - i)] + oR[(size_t) (total - 1 - i)]);

        // envolvente para la interfaz
        float mx = 1.0e-9f;
        const int per = std::max (1, total / ReversedClip::kDisplayPoints);
        for (int k = 0; k < ReversedClip::kDisplayPoints; ++k)
        {
            float m = 0.0f;
            for (int i = k * per; i < std::min (total, (k + 1) * per); ++i) m = std::max (m, std::fabs (clip->data[0][(size_t) i]));
            clip->envelope[k] = m; mx = std::max (mx, m);
        }
        for (auto& e : clip->envelope) e /= mx;
        return clip;
    }

    void ReverseProcessor::buildSwellAudio (const ExportClip& c, int swell, float attackFrac, float tailMs, std::vector<float> out[2])
    {
        swell = std::max (1, swell);
        const SwellWindow w = SwellWindow::make (c.length, c.peakIdx, swell, attackFrac, tailMs, c.sampleRate);
        const int tail = std::max (0, w.endIdx - w.peak);
        const int total = swell + tail;
        for (int ch = 0; ch < c.numChannels; ++ch)
        {
            out[ch].assign ((size_t) total, 0.0f);
            for (int i = 0; i < total; ++i)
            {
                const int idx = c.peakIdx - swell + i;
                if (idx >= 0 && idx < c.length) out[ch][(size_t) i] = c.data[ch][(size_t) idx] * w.gain (idx);
            }
        }
    }
}
