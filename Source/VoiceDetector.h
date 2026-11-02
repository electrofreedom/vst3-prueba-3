#pragma once
#include "Common.h"

namespace rvr
{
    /** Detecta inicio y final de frases vocales: amplitud (umbral con histéresis) +
        transitorios (envolvente rápida vs. lenta), tiempo de silencio y bloqueo temporal. */
    class VoiceDetector
    {
    public:
        enum class Event { None, PhraseStart, PhraseEnd };

        void prepare (double sampleRate);
        void reset() noexcept;

        void setThresholdDb (float db) noexcept;
        void setPhraseGapMs (float ms) noexcept;     // silencio necesario para cerrar la frase
        void setLockoutMs   (float ms) noexcept;     // tiempo mínimo entre dos inicios

        /** Procesa el valor absoluto de una muestra. */
        Event process (float absSample) noexcept;

        bool  inPhrase() const noexcept { return active; }
        float fastEnvelope() const noexcept { return fast; }

        /** Retardo típico (muestras) entre el inicio real de la voz y la detección. */
        int backtrackSamples() const noexcept { return (int) (0.002 * sr); }

    private:
        double sr = 48000.0;
        float  aAttF = 0, aRelF = 0, aAttS = 0, aRelS = 0;
        float  fast = 0, slow = 0;
        float  thrOn = 0.0178f, thrOff = 0.0126f;
        int    gapSamples = 16800, lockoutSamples = 24000;
        int    silentCount = 0, sinceStart = 1 << 28;
        bool   active = false;
    };
}
