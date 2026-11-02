#pragma once
// Utilidades comunes sin dependencias de JUCE (el núcleo DSP es C++17 estándar,
// lo que permite probarlo de forma automática sin DAW ni interfaz).
#include <cmath>
#include <algorithm>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
#endif

namespace rvr
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr double kMaxSwellSeconds   = 12.0;  // máximo pre-roll / latencia
    constexpr double kMaxCaptureSeconds = 8.0;   // máximo REVERSE LENGTH
    constexpr double kMaxReverbSeconds  = 12.0;

    inline float dbToLinear (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }

    /** Activa flush-to-zero / denormals-are-zero en el hilo actual (x86). */
    inline void enableFlushDenormals() noexcept
    {
       #if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        _mm_setcsr (_mm_getcsr() | 0x8040);
       #endif
    }

    inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
}
