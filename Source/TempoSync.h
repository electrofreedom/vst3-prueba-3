#pragma once
#include "Common.h"

namespace rvr
{
    /** Información de transporte entregada por el DAW (si existe). */
    struct HostTime
    {
        bool   hasTempo = false;
        double bpm      = 120.0;
        int    tsNum    = 4;
        int    tsDen    = 4;
        bool   hasPpq   = false;
        double ppq      = 0.0;      // posición en negras al inicio del bloque
        bool   playing  = false;
    };

    /** Módulo TempoSync: duraciones musicales y cuantización a rejilla. */
    class TempoSync
    {
    public:
        static constexpr double kMinSwell = 0.1;

        /** 0..4 -> 1/4, 1/2, 1, 2, 4 compases. */
        static double divisionInBars (int idx) noexcept
        {
            static const double d[] { 0.25, 0.5, 1.0, 2.0, 4.0 };
            return d[idx < 0 ? 0 : (idx > 4 ? 4 : idx)];
        }

        static double barSeconds (const HostTime& h) noexcept
        {
            const double quarterNotesPerBar = (double) h.tsNum * 4.0 / (double) (h.tsDen > 0 ? h.tsDen : 4);
            return quarterNotesPerBar * 60.0 / h.bpm;
        }

        /** Duración del swell (pre-roll). Si no hay tempo disponible usa la duración libre. */
        static double swellSeconds (bool sync, int divIdx, double freeSeconds, const HostTime& h) noexcept
        {
            double s = freeSeconds;
            if (sync && h.hasTempo && h.bpm > 1.0)
                s = divisionInBars (divIdx) * barSeconds (h);
            return s < kMinSwell ? kMinSwell : (s > kMaxSwellSeconds ? kMaxSwellSeconds : s);
        }

        /** q: 0 = off, 1 = pulso (negra), 2 = compás. Valor en negras. */
        static double quantumQuarterNotes (int q, const HostTime& h) noexcept
        {
            if (q == 1) return 1.0;
            if (q == 2) return (double) h.tsNum * 4.0 / (double) (h.tsDen > 0 ? h.tsDen : 4);
            return 0.0;
        }

        /** Muestras hasta la próxima línea de rejilla (0 si no hay cuantización o información). */
        static int samplesToNextGrid (int q, const HostTime& h, double ppqNow, double sampleRate) noexcept
        {
            if (q <= 0 || ! h.hasTempo || ! h.hasPpq || ! h.playing || h.bpm <= 1.0) return 0;
            const double qn = quantumQuarterNotes (q, h);
            double r = std::fmod (ppqNow, qn);
            if (r < 0) r += qn;
            const double toNext = (r < 1.0e-6 || qn - r < 1.0e-6) ? 0.0 : qn - r;
            return (int) std::llround (toNext * 60.0 / h.bpm * sampleRate);
        }
    };
}
