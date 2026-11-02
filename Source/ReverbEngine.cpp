#include "ReverbEngine.h"

namespace rvr
{
    // ----------------------------------------------------------------- Biquad
    void Biquad::setLowpass (double fs, double fc) noexcept
    {
        fc = std::min (std::max (fc, 10.0), 0.45 * fs);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.70710678118);
        const double a0 = 1.0 + al;
        b0 = ((1.0 - c) * 0.5) / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = (-2.0 * c) / a0;        a2 = (1.0 - al) / a0;
    }
    void Biquad::setHighpass (double fs, double fc) noexcept
    {
        fc = std::min (std::max (fc, 10.0), 0.45 * fs);
        const double w0 = 2.0 * 3.14159265358979323846 * fc / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * 0.70710678118);
        const double a0 = 1.0 + al;
        b0 = ((1.0 + c) * 0.5) / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = (-2.0 * c) / a0;        a2 = (1.0 - al) / a0;
    }

    // -------------------------------------------------------------- DelayBuf
    void ReverbEngine::DelayBuf::allocate (int maxSamples)
    {
        int size = 16;
        while (size < maxSamples + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; w = 0;
    }

    inline float ReverbEngine::DelayBuf::readFrac (float d) const noexcept
    {
        const int n = (int) d;
        const float t = d - (float) n;
        const float ym1 = read (n - 1), y0 = read (n), y1 = read (n + 1), y2 = read (n + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    // ---------------------------------------------------------------- Engine
    void ReverbEngine::prepare (double sampleRate)
    {
        sr = sampleRate;
        const int maxPre = (int) (0.26 * sr);
        preL.allocate (maxPre); preR.allocate (maxPre);
        erBuf.allocate ((int) (0.40 * sr));
        for (auto& a : apL) a.d.allocate ((int) (0.05 * sr));
        for (auto& a : apR) a.d.allocate ((int) (0.05 * sr));
        for (auto& l : lines) l.d.allocate ((int) (0.30 * sr));
        setParams (ReverbParams {});
    }

    void ReverbEngine::setParams (const ReverbParams& np)
    {
        p = np;
        p.reverbTime = clampf (p.reverbTime, 0.2f, (float) kMaxReverbSeconds);
        p.roomSize   = clampf (p.roomSize, 0.0f, 1.0f);
        p.diffusion  = clampf (p.diffusion, 0.0f, 1.0f);
        p.preDelayMs = clampf (p.preDelayMs, 0.0f, 250.0f);
        p.width      = clampf (p.width, 0.0f, 2.0f);

        const float scale = 0.45f + 1.6f * std::pow (p.roomSize, 1.2f);          // tamaño de sala
        preSamples = (int) (0.001 * p.preDelayMs * sr);

        // Reflexiones tempranas (ms a escala 1.0), L/R distintas para ubicación espacial
        static const float tL[kEarlyTaps] { 7.3f, 12.1f, 17.9f, 23.7f, 29.3f, 37.1f, 43.9f, 53.3f, 61.7f, 71.9f };
        static const float tR[kEarlyTaps] { 5.9f, 10.7f, 15.3f, 21.1f, 27.7f, 34.9f, 41.3f, 49.7f, 58.1f, 67.9f };
        for (int k = 0; k < kEarlyTaps; ++k)
        {
            erTapL[k] = std::max (2, (int) (0.001 * tL[k] * scale * sr));
            erTapR[k] = std::max (2, (int) (0.001 * tR[k] * scale * sr));
            const float g = 0.55f * std::pow (0.82f, (float) k);
            erGainL[k] = (k % 3 == 1) ? -g : g;
            erGainR[k] = (k % 3 == 0) ? -g : g;
        }

        // Difusores: más ganancia = mayor densidad/suavidad
        static const float apMs[kDiffusers] { 4.7f, 7.1f, 10.3f, 13.9f };
        const float ag = 0.30f + 0.45f * p.diffusion;
        for (int k = 0; k < kDiffusers; ++k)
        {
            apL[k].len = std::max (2, (int) (0.001 * apMs[k] * sr));
            apR[k].len = std::max (2, (int) (0.001 * apMs[k] * 1.13f * sr));
            apL[k].g = apR[k].g = ag;
        }

        // FDN: retardos (ms) casi coprimos; el RT60 fija la ganancia por línea
        static const float lineMs[kLines] { 31.1f, 37.9f, 43.3f, 51.7f, 59.9f, 67.1f, 79.3f, 89.7f };
        modDepth = (float) (sr * (0.00008 + 0.00022 * p.diffusion));
        for (int i = 0; i < kLines; ++i)
        {
            auto& l = lines[(size_t) i];
            l.delay = std::max (modDepth + 4.0f, (float) (0.001 * lineMs[i] * scale * sr));
            l.gain  = std::pow (10.0f, -3.0f * l.delay / (1.15f * p.reverbTime * (float) sr));   // 1,15: compensa el HF/LF que absorben los filtros del lazo
            l.inc   = (float) (2.0 * 3.14159265358979323846 * (0.13 + 0.07 * i) / sr);
            l.phase = 0.8f * (float) i;
        }
        lpCoef = std::exp (-2.0f * kPi * std::min (p.highCutHz * 0.9f, 0.45f * (float) sr) / (float) sr);
        hpCoef = std::exp (-2.0f * kPi * std::max (p.lowCutHz * 0.5f, 5.0f) / (float) sr);

        eqHpL.setHighpass (sr, p.lowCutHz);  eqHpR.setHighpass (sr, p.lowCutHz);
        eqLpL.setLowpass  (sr, p.highCutHz); eqLpR.setLowpass  (sr, p.highCutHz);
        reset();
    }

    void ReverbEngine::reset() noexcept
    {
        preL.clear(); preR.clear(); erBuf.clear();
        for (auto& a : apL) a.d.clear();
        for (auto& a : apR) a.d.clear();
        for (auto& l : lines) { l.d.clear(); l.lp = l.hp = 0.0f; }
        eqHpL.reset(); eqHpR.reset(); eqLpL.reset(); eqLpR.reset();
    }

    void ReverbEngine::process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        static const float sL[kLines] { 1, -1, 1, -1, 1, 1, -1, -1 };
        static const float sR[kLines] { 1, 1, -1, -1, -1, 1, 1, -1 };
        constexpr float kOut  = 0.30f;        // calibrado para ganancia ~unidad con ruido de entrada
        constexpr float kInj  = 0.50f;
        constexpr float kEr   = 0.35f;
        constexpr float kHad  = 0.35355339059f;  // 1/sqrt(8)
        const float oneMinusLp = 1.0f - lpCoef, oneMinusHp = 1.0f - hpCoef;

        for (int s = 0; s < n; ++s)
        {
            const float xl = inL[s], xr = inR[s];
            preL.write (xl); preR.write (xr);
            const float pl = preSamples > 0 ? preL.read (preSamples) : xl;
            const float pr = preSamples > 0 ? preR.read (preSamples) : xr;

            // reflexiones tempranas
            erBuf.write (0.5f * (pl + pr));
            float eL = 0.0f, eR = 0.0f;
            for (int k = 0; k < kEarlyTaps; ++k)
            {
                eL += erGainL[k] * erBuf.read (erTapL[k]);
                eR += erGainR[k] * erBuf.read (erTapR[k]);
            }

            // difusión de entrada
            float dl = pl, dr = pr;
            for (int k = 0; k < kDiffusers; ++k) { dl = apL[k].process (dl); dr = apR[k].process (dr); }

            // lectura de las líneas (retardo modulado)
            float v[kLines];
            float fL = 0.0f, fR = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                auto& l = lines[(size_t) i];
                l.phase += l.inc; if (l.phase > 2.0f * kPi) l.phase -= 2.0f * kPi;
                const float d = l.d.readFrac (l.delay + modDepth * std::sin (l.phase));
                fL += sL[i] * d; fR += sR[i] * d;
                // filtros dentro de la realimentación: pasa-altos (low cut) y pasa-bajos (high cut)
                l.hp = oneMinusHp * d + hpCoef * l.hp;
                const float hpOut = d - l.hp;
                l.lp = oneMinusLp * hpOut + lpCoef * l.lp;
                v[i] = l.lp * l.gain;
            }

            // matriz de mezcla Hadamard (Walsh-Hadamard rápida, ortogonal)
            for (int len = 1; len < kLines; len <<= 1)
                for (int i = 0; i < kLines; i += 2 * len)
                    for (int j = i; j < i + len; ++j) { const float a = v[j], b = v[j + len]; v[j] = a + b; v[j + len] = a - b; }

            noiseSign = -noiseSign;                         // anti-denormales
            const float tiny = 1.0e-18f * noiseSign;
            for (int i = 0; i < kLines; ++i)
            {
                const float inj = ((i & 1) ? dr : dl) * ((i & 2) ? -1.0f : 1.0f);
                lines[(size_t) i].d.write (v[i] * kHad + kInj * inj + tiny);
            }

            float wl = kOut * fL + kEr * eL;
            float wr = kOut * fR + kEr * eR;
            wl = eqLpL.process (eqHpL.process (wl));
            wr = eqLpR.process (eqHpR.process (wr));

            const float m = 0.5f * (wl + wr), sd = 0.5f * (wl - wr) * p.width;
            outL[s] = m + sd;
            outR[s] = m - sd;
        }
    }
}
