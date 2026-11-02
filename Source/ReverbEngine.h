#pragma once
#include <vector>
#include <array>
#include "Common.h"

namespace rvr
{
    struct ReverbParams
    {
        float reverbTime = 3.0f;      // RT60 en segundos (0,5 - 12)
        float roomSize   = 0.6f;      // 0..1
        float diffusion  = 0.8f;      // 0..1
        float preDelayMs = 20.0f;     // 0..250
        float lowCutHz   = 150.0f;
        float highCutHz  = 9000.0f;
        float width      = 1.0f;      // 0..2

        bool operator== (const ReverbParams& o) const noexcept
        {
            return reverbTime == o.reverbTime && roomSize == o.roomSize && diffusion == o.diffusion
                && preDelayMs == o.preDelayMs && lowCutHz == o.lowCutHz && highCutHz == o.highCutHz
                && width == o.width;
        }
        bool operator!= (const ReverbParams& o) const noexcept { return ! (*this == o); }
    };

    /** Biquad Butterworth de 2.º orden (doble precisión). */
    class Biquad
    {
    public:
        void setLowpass  (double fs, double fc) noexcept;
        void setHighpass (double fs, double fc) noexcept;
        void reset() noexcept { z1 = z2 = 0.0; }
        inline float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    private:
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    /** Reverberación original orientada a voz:
        pre-delay -> reflexiones tempranas + difusores Schroeder en serie -> FDN de 8 líneas
        (matriz Hadamard ortogonal, retardos modulados con interpolación Hermite, filtros
        pasa-altos/pasa-bajos dentro del lazo, decaimiento calculado para el RT60 pedido)
        -> ecualización de salida -> anchura M/S. Se usa para renderizar la frase capturada. */
    class ReverbEngine
    {
    public:
        static constexpr int kLines = 8;
        static constexpr int kEarlyTaps = 10;
        static constexpr int kDiffusers = 4;

        void prepare (double sampleRate);
        void setParams (const ReverbParams& p);   // recalcula todo y limpia el estado
        void reset() noexcept;
        void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples) noexcept;

    private:
        struct DelayBuf
        {
            std::vector<float> buf; int mask = 0, w = 0;
            void allocate (int maxSamples);
            void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
            inline void write (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
            inline float read (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
            inline float readFrac (float d) const noexcept;
        };
        struct Allpass
        {
            DelayBuf d; int len = 1; float g = 0.5f;
            inline float process (float x) noexcept
            {
                const float del = d.read (len);
                const float v = x + g * del;
                d.write (v);
                return del - g * v;
            }
        };
        struct Line
        {
            DelayBuf d; float delay = 100.0f, gain = 0.9f, lp = 0, hp = 0, phase = 0, inc = 0;
        };

        double sr = 48000.0;
        ReverbParams p;
        DelayBuf preL, preR, erBuf;
        int preSamples = 0;
        int erTapL[kEarlyTaps] {}, erTapR[kEarlyTaps] {};
        float erGainL[kEarlyTaps] {}, erGainR[kEarlyTaps] {};
        Allpass apL[kDiffusers], apR[kDiffusers];
        std::array<Line, kLines> lines;
        float lpCoef = 0.0f, hpCoef = 0.0f, modDepth = 0.0f;
        Biquad eqHpL, eqHpR, eqLpL, eqLpR;
        float noiseSign = 1.0f;
    };
}
