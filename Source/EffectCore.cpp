#include "EffectCore.h"
#include <chrono>

namespace rvr
{
    EffectCore::EffectCore() { for (auto& e : displayEnv) e.store (0.0f); }
    EffectCore::~EffectCore() { release(); }

    void EffectCore::prepare (double sampleRate, int channels)
    {
        release();
        sr = sampleRate;
        numCh = channels < 2 ? 1 : 2;

        maxLatency = (int) (kMaxSwellSeconds * sr) + 64;
        int size = 1024; while (size < maxLatency + 8) size <<= 1;
        for (int c = 0; c < 2; ++c) dryBuf[c].assign ((size_t) (c < numCh ? size : 1), 0.0f);
        dryMask = size - 1; dryW = 0;
        latency = std::min (latency, maxLatency);

        preRollSamples = (int) (0.03 * sr);
        capture.prepare (numCh, (int) (kMaxCaptureSeconds * sr) + preRollSamples + 16, (int) (0.15 * sr));
        detector.prepare (sr);
        lastMode = Mode::Auto;

        smoothCoef = (float) std::exp (-1.0 / (0.020 * sr));
        capturing = haveCapture = awaiting = resetWhileBusy = false;
        activeClip = nullptr; for (auto& v : voices) v = Voice {};
        pending.store (nullptr); jobBusy.store (false); jobRequested.store (false);
        firstBlock = true; uiState.store (0);
        renderedParams = lastSeenParams = ReverbParams {};

        quit.store (false);
        worker = std::thread ([this] { workerLoop(); });
    }

    void EffectCore::release()
    {
        if (worker.joinable()) { quit.store (true); worker.join(); }
        owned.clear();
        { std::lock_guard<std::mutex> lk (exportMutex); exportClip = nullptr; }
        pending.store (nullptr);
        activeClip = nullptr;
        for (auto& v : voices) v = Voice {};
    }

    void EffectCore::setLatencySamples (int samples) noexcept { latency = std::max (0, std::min (samples, maxLatency)); }

    void EffectCore::copyDisplayEnvelope (float* out) const noexcept
    {
        for (int i = 0; i < ReversedClip::kDisplayPoints; ++i) out[i] = displayEnv[i].load (std::memory_order_relaxed);
    }

    void EffectCore::waitForIdle (int timeoutMs) const
    {
        for (int t = 0; t < timeoutMs && jobBusy.load (std::memory_order_acquire); ++t)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    std::shared_ptr<const ExportClip> EffectCore::getExportClip() const
    {
        std::shared_ptr<ExportClip> c;
        { std::lock_guard<std::mutex> lk (exportMutex); c = exportClip; }
        if (c == nullptr || c->gen != generation.load()) return nullptr;
        return c;
    }

    // ------------------------------------------------------------- worker
    void EffectCore::workerLoop()
    {
        enableFlushDenormals();
        while (! quit.load())
        {
            if (jobRequested.exchange (false, std::memory_order_acquire))
            {
                const Job j = job;
                auto clip = ReverseProcessor::render (capture.channelPointers(), j.numCh, j.numSamples, j.sr, j.rp, &generation, j.gen);
                if (clip != nullptr && generation.load() == j.gen)
                {
                    auto ex = std::make_shared<ExportClip>();
                    ex->data[0] = clip->data[0]; ex->data[1] = clip->data[1];
                    ex->numChannels = clip->numChannels; ex->length = clip->length;
                    ex->peakIdx = clip->peakIdx; ex->sampleRate = clip->sampleRate; ex->gen = j.gen;
                    { std::lock_guard<std::mutex> lk (exportMutex); exportClip = ex; }
                    ReversedClip* raw = clip.get();
                    owned.push_back (std::move (clip));
                    if (auto* old = pending.exchange (raw, std::memory_order_acq_rel)) old->retired.store (true);
                }
                jobBusy.store (false, std::memory_order_release);
            }
            sweepRetired();
            std::this_thread::sleep_for (std::chrono::milliseconds (2));
        }
    }

    void EffectCore::sweepRetired()
    {
        for (size_t i = owned.size(); i-- > 0;)
            if (owned[i]->retired.load() && owned[i]->voiceRefs.load() == 0)
                owned.erase (owned.begin() + (long) i);
    }

    bool EffectCore::submitJob()
    {
        if (jobBusy.load (std::memory_order_acquire) || capture.size() < 16) return false;
        job.rp = cp->reverb; job.numSamples = capture.size(); job.numCh = numCh; job.sr = sr;
        job.gen = generation.load();
        renderedParams = cp->reverb;
        awaiting = true; samplesSinceSubmit = 0; resetWhileBusy = false;
        jobBusy.store (true, std::memory_order_relaxed);
        jobRequested.store (true, std::memory_order_release);
        return true;
    }

    // ------------------------------------------------------- captura / voces
    bool EffectCore::startCapture (int preRoll, bool automatic)
    {
        if (capturing || jobBusy.load (std::memory_order_acquire)) return false;
        capture.begin (preRoll);
        capturing = true; captureAuto = automatic; haveCapture = false;
        captureTarget = std::min (capture.capacity(), preRoll + std::max (1, (int) (cp->captureSeconds * sr)));
        return true;
    }

    void EffectCore::endCapture()
    {
        capturing = false;
        if (capture.size() < (int) (0.1 * sr)) { haveCapture = false; return; }   // demasiado corta
        haveCapture = true;
        submitJob();
    }

    void EffectCore::endVoice (Voice& v) noexcept
    {
        if (v.clip != nullptr) v.clip->voiceRefs.fetch_sub (1);
        v = Voice {};
    }

    void EffectCore::killVoices (int fade) noexcept
    {
        for (auto& v : voices)
            if (v.active) { v.killTotal = std::max (1, fade); v.killLeft = v.killTotal; }
    }

    void EffectCore::startVoice (int i)
    {
        ReversedClip* clip = activeClip;
        if (clip == nullptr || cp->mode == Mode::Export) return;
        Voice* slot = nullptr;
        for (auto& v : voices) if (! v.active) { slot = &v; break; }
        if (slot == nullptr) return;

        int extra = 0;
        if (cp->mode != Mode::Auto && cp->quantize > 0 && host->hasPpq && host->hasTempo)
            extra = TempoSync::samplesToNextGrid (cp->quantize, *host, host->ppq + (double) i * host->bpm / 60.0 / sr, sr);

        const int offs = (int) std::llround ((double) cp->arrivalOffsetMs * 0.001 * sr);
        const int backtrack = cp->mode == Mode::Auto ? detector.backtrackSamples() : 0;
        // índice del clip que suena en la muestra de salida actual: el pico llega L + offset después
        const long long c0 = (long long) clip->peakIdx - latency - offs - extra + backtrack;
        const SwellWindow w = SwellWindow::make (clip->length, clip->peakIdx, latency, cp->attackFrac, cp->tailMs, sr);
        if (c0 >= w.endIdx) return;

        clip->voiceRefs.fetch_add (1);
        slot->clip = clip; slot->win = w; slot->active = true; slot->killLeft = 0;
        slot->c = (int) std::max<long long> (c0, -(1 << 30));
    }

    void EffectCore::resetAll() noexcept
    {
        generation.fetch_add (1);                 // cancela renders en curso
        killVoices (256);
        if (auto* p = pending.exchange (nullptr)) p->retired.store (true);
        if (activeClip != nullptr) { activeClip->retired.store (true); activeClip = nullptr; }
        capturing = haveCapture = awaiting = false;
        resetWhileBusy = jobBusy.load();
        detector.reset();
        for (auto& e : displayEnv) e.store (0.0f);
        displayPeak.store (0.0f);
    }

    void EffectCore::transportRestart() noexcept
    {
        killVoices (256);
        detector.reset();
        if (capturing) capturing = false;
    }

    void EffectCore::pickupClip() noexcept
    {
        if (! awaiting) return;
        if (samplesSinceSubmit < (long long) (0.25 * sr)) return;        // retraso mínimo fijo => resultado reproducible
        if (nonRealtime)
            for (int t = 0; t < 30000 && jobBusy.load (std::memory_order_acquire); ++t)
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
        if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (activeClip != nullptr) activeClip->retired.store (true);
            activeClip = p;
            for (int i = 0; i < ReversedClip::kDisplayPoints; ++i) displayEnv[i].store (p->envelope[i], std::memory_order_relaxed);
            displayPeak.store ((float) p->peakIdx / (float) std::max (1, p->length), std::memory_order_relaxed);
            awaiting = false;
        }
        else if (! jobBusy.load (std::memory_order_acquire))
            awaiting = false;                                            // el render fue cancelado
    }

    void EffectCore::handleEvent (const CoreEvent& e)
    {
        switch (e.type)
        {
            case EventType::Capture:
                if (capturing) endCapture(); else startCapture (0, false);
                break;
            case EventType::Reverse: startVoice (e.samplePos); break;
            case EventType::Reset:   resetAll(); break;
        }
    }

    // --------------------------------------------------------------- audio
    void EffectCore::bypassBlock (float* const* ch, int nCh, int n) noexcept
    {
        killVoices (128);
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nCh && c < numCh; ++c)
            {
                dryBuf[c][(size_t) dryW] = ch[c][i];
                ch[c][i] = dryBuf[c][(size_t) ((dryW - latency) & dryMask)];
            }
            dryW = (dryW + 1) & dryMask;
            // la voz muere con fundido aunque no estemos mezclando
            for (auto& v : voices)
                if (v.active && --v.killLeft <= 0) endVoice (v);
        }
    }

    void EffectCore::processBlock (float* const* ch, int nCh, int n, const CoreParams& P, const HostTime& h,
                                   const CoreEvent* ev, int numEv) noexcept
    {
        cp = &P; host = &h;
        const int chans = std::min (nCh, numCh);

        // ajustes del detector y cambio de modo
        detector.setThresholdDb (P.thresholdDb);
        detector.setPhraseGapMs (P.phraseGapMs);
        detector.setLockoutMs (P.lockoutMs);
        if (P.mode != lastMode) { detector.reset(); lastMode = P.mode; }

        samplesSinceSubmit += n;
        pickupClip();

        // re-render automático si el usuario modifica la reverb tras capturar
        if (P.reverb != lastSeenParams) { lastSeenParams = P.reverb; stableSamples = 0; }
        else stableSamples += n;
        if (haveCapture && ! capturing && P.reverb != renderedParams && stableSamples >= (long long) (0.25 * sr))
            submitJob();

        // ganancias objetivo (suavizadas)
        const float dg = P.dryDb <= -59.9f ? 0.0f : dbToLinear (P.dryDb);
        const float wg = P.wetDb <= -59.9f ? 0.0f : dbToLinear (P.wetDb);
        const float dryTarget = dg * std::min (1.0f, 2.0f * (1.0f - P.mix));
        const float wetTarget = wg * std::min (1.0f, 2.0f * P.mix) * clampf (P.reverseAmount, 0.0f, 1.0f);
        if (firstBlock) { curDry = dryTarget; curWet = wetTarget; firstBlock = false; }

        int evIdx = 0;
        float peakIn = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            while (evIdx < numEv && ev[evIdx].samplePos <= i) handleEvent (ev[evIdx++]);

            const float x0 = ch[0][i];
            const float x1 = chans > 1 ? ch[1][i] : x0;
            const float ax = std::max (std::fabs (x0), std::fabs (x1));
            peakIn = std::max (peakIn, ax);

            if (P.mode == Mode::Auto || P.mode == Mode::Export)
            {
                const auto e = detector.process (ax);
                if (e == VoiceDetector::Event::PhraseStart)
                {
                    if (activeClip != nullptr) startVoice (i);
                    if (! capturing && ! jobBusy.load (std::memory_order_acquire)
                        && ((activeClip == nullptr && ! awaiting) || P.autoEveryPhrase))
                        startCapture (preRollSamples, true);
                }
                else if (e == VoiceDetector::Event::PhraseEnd && capturing && captureAuto)
                    endCapture();
            }

            if (capturing)
            {
                capture.append (x0, x1);
                if (capture.size() >= captureTarget) endCapture();
            }
            capture.pushHistory (x0, x1);

            // voz seca retardada (alineada con el swell)
            dryBuf[0][(size_t) dryW] = x0;
            if (chans > 1) dryBuf[1][(size_t) dryW] = x1;
            const size_t rd = (size_t) ((dryW - latency) & dryMask);
            const float d0 = dryBuf[0][rd], d1 = chans > 1 ? dryBuf[1][rd] : d0;
            dryW = (dryW + 1) & dryMask;

            // voces de reverb inversa
            float w0 = 0.0f, w1 = 0.0f;
            for (auto& v : voices)
            {
                if (! v.active) continue;
                float g = 0.0f;
                if (v.c >= 0) g = v.win.gain (v.c);
                if (v.killLeft > 0)
                {
                    g *= (float) v.killLeft / (float) v.killTotal;
                    if (--v.killLeft == 0) { endVoice (v); continue; }
                }
                if (g != 0.0f)
                {
                    const auto* clip = v.clip;
                    w0 += g * clip->data[0][(size_t) v.c];
                    w1 += g * clip->data[clip->numChannels > 1 ? 1 : 0][(size_t) v.c];
                }
                if (++v.c >= v.win.endIdx) endVoice (v);
            }

            curDry = dryTarget + smoothCoef * (curDry - dryTarget);
            curWet = wetTarget + smoothCoef * (curWet - wetTarget);
            ch[0][i] = d0 * curDry + w0 * curWet;
            if (chans > 1) ch[1][i] = d1 * curDry + w1 * curWet;
        }

        // medidor y estado visible
        inLevel.store (peakIn, std::memory_order_relaxed);
        State s;
        if (capturing) s = State::Capturing;
        else if (jobBusy.load (std::memory_order_acquire) && ! resetWhileBusy) s = State::Processing;
        else if (activeClip != nullptr) s = State::Ready;
        else s = State::Waiting;
        uiState.store ((int) s, std::memory_order_relaxed);
    }
}
