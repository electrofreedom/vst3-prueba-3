#pragma once
#include <atomic>
#include <memory>
#include <vector>
#include "ReverbEngine.h"

namespace rvr
{
    /** Resultado ya invertido: reverb (con cola completa) de la frase capturada, dada la vuelta. */
    struct ReversedClip
    {
        static constexpr int kDisplayPoints = 256;

        std::vector<float> data[2];
        int    numChannels = 1;
        int    length      = 0;
        int    peakIdx     = 0;       // posición del máximo de la envolvente (punto de llegada)
        double sampleRate  = 48000.0;
        float  envelope[kDisplayPoints] {};   // envolvente normalizada para la interfaz

        std::atomic<int>  voiceRefs { 0 };    // voces que están leyendo este clip
        std::atomic<bool> retired   { false };// reemplazado: se puede borrar cuando voiceRefs == 0
    };

    /** Copia inmutable del clip invertido para exportarlo a WAV desde el hilo de la interfaz. */
    struct ExportClip
    {
        std::vector<float> data[2];
        int numChannels = 1, length = 0, peakIdx = 0, gen = 0;
        double sampleRate = 48000.0;
    };

    /** Ventana de reproducción: fade-in (ATTACK), meseta hasta el pico y fade-out (TAIL).
        Coseno elevado en ambos extremos => sin clics. */
    struct SwellWindow
    {
        int fadeInStart = 0, fadeInLen = 1, peak = 0, tailLen = 1, endIdx = 0;

        static SwellWindow make (int clipLength, int peakIdx, int swellSamples, float attackFrac,
                                 float tailMs, double sr) noexcept
        {
            SwellWindow w;
            const int minFade = std::max (16, (int) (0.005 * sr));
            w.peak        = peakIdx;
            w.fadeInStart = std::max (0, peakIdx - std::max (swellSamples, minFade));
            w.fadeInLen   = std::max (minFade, (int) (clampf (attackFrac, 0.0f, 1.0f) * (float) (peakIdx - w.fadeInStart)));
            w.tailLen     = std::max (minFade, (int) (0.001 * tailMs * sr));
            w.endIdx      = std::min (clipLength, peakIdx + w.tailLen);
            return w;
        }

        inline float gain (int c) const noexcept
        {
            if (c < fadeInStart || c >= endIdx) return 0.0f;
            if (c < fadeInStart + fadeInLen)
                return 0.5f - 0.5f * std::cos (kPi * (float) (c - fadeInStart) / (float) fadeInLen);
            if (c <= peak) return 1.0f;
            return 0.5f + 0.5f * std::cos (kPi * (float) (c - peak) / (float) tailLen);
        }
    };

    /** Módulo ReverseProcessor: reverb de la captura -> inversión temporal -> localización del pico. */
    class ReverseProcessor
    {
    public:
        /** Renderiza (hilo no-audio). `generation`/`expectedGen` permiten cancelar si hay un RESET.
            Devuelve nullptr si se canceló. */
        static std::unique_ptr<ReversedClip> render (const float* const* captured, int numChannels, int numSamples,
                                                     double sampleRate, const ReverbParams& params,
                                                     const std::atomic<int>* generation = nullptr, int expectedGen = 0);

        /** Audio listo para exportar: dura swellSamples + cola; el máximo (llegada) queda exactamente
            en la muestra swellSamples. Con TAIL = 0 el archivo termina justo en el punto de llegada. */
        static void buildSwellAudio (const ExportClip& clip, int swellSamples, float attackFrac, float tailMs,
                                     std::vector<float> out[2]);
    };
}
