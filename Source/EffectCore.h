#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <mutex>
#include "CaptureBuffer.h"
#include "ReverbEngine.h"
#include "ReverseProcessor.h"
#include "TempoSync.h"
#include "VoiceDetector.h"

namespace rvr
{
    enum class Mode      : int { Auto = 0, Manual = 1, Midi = 2, Export = 3 };   // Export: sin latencia, sólo captura y exporta
    enum class State     : int { Waiting = 0, Capturing = 1, Processing = 2, Ready = 3 };
    enum class EventType : int { Capture, Reverse, Reset };

    struct CoreEvent { int samplePos; EventType type; };

    struct CoreParams
    {
        Mode  mode = Mode::Auto;
        bool  autoEveryPhrase = false;       // false: captura una vez; true: recaptura cada frase
        float reverseAmount = 0.85f;         // 0..1
        float mix = 0.5f;                    // 0..1 (0 = 100 % seco)
        float dryDb = 0.0f, wetDb = 0.0f;    // <= -60 => silencio
        float attackFrac = 0.4f;             // 0..1 del swell
        float tailMs = 120.0f;
        float arrivalOffsetMs = 0.0f;        // + = el pico llega después del inicio de la voz
        float thresholdDb = -35.0f;
        float phraseGapMs = 350.0f;
        float lockoutMs = 500.0f;
        float captureSeconds = 2.0f;         // REVERSE LENGTH
        int   quantize = 0;                  // 0 off, 1 pulso, 2 compás (Manual/MIDI)
        ReverbParams reverb;
    };

    /** Núcleo en tiempo real (sin JUCE): búfer de retardo para la voz seca (latencia = duración
        del swell), detección, captura, render en hilo de fondo, reproducción con voces y mezcla.
        Nada de asignaciones ni bloqueos en processBlock (salvo el modo offline, ver setNonRealtime). */
    class EffectCore
    {
    public:
        EffectCore();
        ~EffectCore();

        void prepare (double sampleRate, int numChannels);
        void release();

        /** Latencia = duración del swell (muestras). Llamar desde el hilo de audio. */
        void setLatencySamples (int samples) noexcept;
        int  latencySamples() const noexcept { return latency; }
        int  maxLatencySamples() const noexcept { return maxLatency; }

        /** Render offline/bounce: espera de forma determinista a que termine el render. */
        void setNonRealtime (bool offline) noexcept { nonRealtime = offline; }

        void processBlock (float* const* channels, int numChannels, int numSamples, const CoreParams& params,
                           const HostTime& host, const CoreEvent* events, int numEvents) noexcept;
        void bypassBlock (float* const* channels, int numChannels, int numSamples) noexcept;
        void transportRestart() noexcept;

        State state() const noexcept { return (State) uiState.load (std::memory_order_relaxed); }
        void  copyDisplayEnvelope (float* out) const noexcept;
        float displayPeakFraction() const noexcept { return displayPeak.load (std::memory_order_relaxed); }
        float inputLevel() const noexcept { return inLevel.load (std::memory_order_relaxed); }
        void  waitForIdle (int timeoutMs = 20000) const;   // sólo pruebas

        /** Copia del último clip renderizado (válida mientras no haya RESET). Hilo de interfaz. */
        std::shared_ptr<const ExportClip> getExportClip() const;

    private:
        struct Voice { ReversedClip* clip = nullptr; int c = 0; SwellWindow win; int killLeft = 0, killTotal = 1; bool active = false; };
        struct Job { ReverbParams rp; int numSamples = 0, numCh = 1, gen = 0; double sr = 48000.0; };

        void workerLoop();
        void sweepRetired();
        bool submitJob();
        bool startCapture (int preRoll, bool automatic);
        void endCapture();
        void startVoice (int sampleIdx);
        void endVoice (Voice& v) noexcept;
        void killVoices (int fadeSamples) noexcept;
        void resetAll() noexcept;
        void pickupClip() noexcept;
        void handleEvent (const CoreEvent& e);

        double sr = 48000.0;
        int numCh = 2, maxLatency = 0, latency = 0;
        bool nonRealtime = false, firstBlock = true;

        // retardo de la voz seca
        std::vector<float> dryBuf[2];
        int dryMask = 0, dryW = 0;

        CaptureBuffer capture;
        VoiceDetector detector;
        Mode lastMode = Mode::Auto;
        bool capturing = false, captureAuto = false, haveCapture = false;
        int captureTarget = 0;
        int preRollSamples = 0;

        // render en segundo plano
        std::thread worker;
        std::atomic<bool> quit { false }, jobRequested { false }, jobBusy { false };
        std::atomic<int>  generation { 0 };
        std::atomic<ReversedClip*> pending { nullptr };
        std::vector<std::unique_ptr<ReversedClip>> owned;     // sólo lo toca el worker
        Job job;
        bool awaiting = false, resetWhileBusy = false;
        long long samplesSinceSubmit = 0;
        ReverbParams renderedParams, lastSeenParams;
        long long stableSamples = 0;

        mutable std::mutex exportMutex;                       // sólo worker e interfaz, nunca el hilo de audio
        std::shared_ptr<ExportClip> exportClip;

        ReversedClip* activeClip = nullptr;
        Voice voices[4];

        float curDry = 1.0f, curWet = 0.0f, smoothCoef = 0.999f;

        // estado compartido con la interfaz
        std::atomic<int>   uiState { 0 };
        std::atomic<float> displayEnv[ReversedClip::kDisplayPoints];
        std::atomic<float> displayPeak { 0.0f }, inLevel { 0.0f };

        // datos del bloque en curso
        const CoreParams* cp = nullptr;
        const HostTime*   host = nullptr;
    };
}
