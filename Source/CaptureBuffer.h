#pragma once
#include <vector>
#include "Common.h"

namespace rvr
{
    /** Almacén temporal de la voz: un anillo corto de historial (para incluir el inicio de la
        frase detectada con retraso) más un búfer lineal de captura. Toda la memoria se reserva
        en prepare(); no hay asignaciones en el hilo de audio. */
    class CaptureBuffer
    {
    public:
        void prepare (int channels, int maxSamples, int historySamples)
        {
            numCh = channels < 2 ? 1 : 2;
            maxLen = maxSamples;
            histLen = historySamples;
            for (int c = 0; c < 2; ++c)
            {
                lin[c].assign ((size_t) (c < numCh ? maxLen : 1), 0.0f);
                hist[c].assign ((size_t) (c < numCh ? histLen : 1), 0.0f);
            }
            clear();
        }

        void clear() noexcept { len = 0; hw = 0; histFill = 0; }

        inline void pushHistory (float l, float r) noexcept
        {
            hist[0][(size_t) hw] = l;
            if (numCh > 1) hist[1][(size_t) hw] = r;
            if (++hw >= histLen) hw = 0;
            if (histFill < histLen) ++histFill;
        }

        /** Empieza una captura copiando las últimas preRoll muestras del historial. */
        void begin (int preRoll) noexcept
        {
            len = 0;
            const int n = std::min (std::min (preRoll, histFill), std::min (histLen, maxLen));
            for (int i = 0; i < n; ++i)
            {
                int idx = hw - n + i;
                if (idx < 0) idx += histLen;
                lin[0][(size_t) len] = hist[0][(size_t) idx];
                if (numCh > 1) lin[1][(size_t) len] = hist[1][(size_t) idx];
                ++len;
            }
        }

        inline void append (float l, float r) noexcept
        {
            if (len >= maxLen) return;
            lin[0][(size_t) len] = l;
            if (numCh > 1) lin[1][(size_t) len] = r;
            ++len;
        }

        int  size() const noexcept     { return len; }
        int  capacity() const noexcept { return maxLen; }
        int  channels() const noexcept { return numCh; }
        const float* const* channelPointers() noexcept { ptrs[0] = lin[0].data(); ptrs[1] = lin[numCh > 1 ? 1 : 0].data(); return ptrs; }

    private:
        std::vector<float> lin[2], hist[2];
        const float* ptrs[2] { nullptr, nullptr };
        int numCh = 1, maxLen = 0, histLen = 1, len = 0, hw = 0, histFill = 0;
    };
}
