// Pruebas automáticas del núcleo DSP (sin JUCE, sin DAW). Ejecutar: ctest  o  ./ReverseVocalReverbTests
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <random>
#include "../Source/EffectCore.h"

using namespace rvr;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { ++g_checks; if (! (cond)) { ++g_fail; std::printf ("  FALLO  %s:%d  %s  -> ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
#define INFO(...) do { std::printf ("  . "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

// Voz sintética: armónicos de 140 Hz con modulación de amplitud silábica y ataque rápido.
static void addVoice (std::vector<float>& x, double sr, double t0, double dur, float level = 0.3f)
{
    const int s0 = (int) (t0 * sr), n = (int) (dur * sr);
    for (int i = 0; i < n && s0 + i < (int) x.size(); ++i)
    {
        const double t = i / sr;
        double v = 0.0;
        for (int h = 1; h <= 12; ++h) v += std::sin (2.0 * M_PI * 140.0 * h * t) / h;
        const double env = std::min (1.0, t / 0.008) * std::min (1.0, (dur - t) / 0.03) * (0.75 + 0.25 * std::sin (2.0 * M_PI * 4.0 * t));
        x[(size_t) (s0 + i)] += (float) (level * 0.55 * v * env);
    }
}

static double rms (const std::vector<float>& x, double sr, double t0, double t1)
{
    const int a = std::max (0, (int) (t0 * sr)), b = std::min ((int) x.size(), (int) (t1 * sr));
    double s = 0; for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
    return b > a ? std::sqrt (s / (b - a)) : 0.0;
}

// índice (s) del máximo de la envolvente RMS de 50 ms dentro de [t0,t1]
static double argmaxEnv (const std::vector<float>& x, double sr, double t0, double t1)
{
    double best = -1, bt = t0;
    for (double t = t0; t + 0.05 <= t1; t += 0.01) { const double r = rms (x, sr, t, t + 0.05); if (r > best) { best = r; bt = t + 0.025; } }
    return bt;
}

// ------------------------------------------------------------------ TempoSync
static void testTempoSync()
{
    std::printf ("[TempoSync]\n");
    HostTime h; h.hasTempo = true; h.bpm = 120; h.tsNum = 4; h.tsDen = 4; h.hasPpq = true; h.playing = true;
    CHECK (std::fabs (TempoSync::barSeconds (h) - 2.0) < 1e-9, "compás a 120 BPM = 2 s");
    CHECK (std::fabs (TempoSync::swellSeconds (true, 2, 1.0, h) - 2.0) < 1e-9, "1 compás sincronizado");
    CHECK (std::fabs (TempoSync::swellSeconds (true, 0, 1.0, h) - 0.5) < 1e-9, "1/4 compás");
    CHECK (std::fabs (TempoSync::swellSeconds (true, 4, 1.0, h) - 8.0) < 1e-9, "4 compases");
    h.hasTempo = false;
    CHECK (std::fabs (TempoSync::swellSeconds (true, 2, 1.7, h) - 1.7) < 1e-9, "sin tempo -> duración libre");
    h.hasTempo = true; h.bpm = 20;
    CHECK (TempoSync::swellSeconds (true, 4, 1.0, h) <= kMaxSwellSeconds, "limitado a 12 s");
    h.bpm = 120;
    CHECK (TempoSync::samplesToNextGrid (2, h, 8.2, 48000) == (int) std::llround (3.8 * 0.5 * 48000), "siguiente compás");
    CHECK (TempoSync::samplesToNextGrid (2, h, 8.0, 48000) == 0, "ya en la rejilla");
    CHECK (TempoSync::samplesToNextGrid (0, h, 8.2, 48000) == 0, "sin cuantización");
}

// ---------------------------------------------------------------- ReverbEngine
static void testReverbEngine()
{
    std::printf ("[ReverbEngine]\n");
    const double sr = 48000.0;
    for (float rt : { 0.5f, 3.0f, 12.0f })
    {
        ReverbEngine e; e.prepare (sr);
        ReverbParams p; p.reverbTime = rt; p.highCutHz = 12000.0f; p.lowCutHz = 40.0f;
        e.setParams (p);
        const int N = (int) (sr * (rt * 1.6 + 1.0));
        std::vector<float> in ((size_t) N, 0.0f), l ((size_t) N), r ((size_t) N);
        in[0] = 1.0f;
        e.process (in.data(), in.data(), l.data(), r.data(), N);

        bool finite = true; double e0 = 0, mx = 0;
        for (int i = 0; i < N; ++i) { if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])) finite = false; mx = std::max (mx, (double) std::fabs (l[(size_t) i])); }
        CHECK (finite, "IR finita (RT=%.1f)", rt);
        CHECK (mx < 2.0, "IR acotada (%.3f)", mx);

        // RT60 por integración de Schroeder (ajuste -5..-25 dB)
        std::vector<double> sch ((size_t) N); double acc = 0;
        for (int i = N - 1; i >= 0; --i) { acc += (double) l[(size_t) i] * l[(size_t) i]; sch[(size_t) i] = acc; }
        e0 = sch[0]; double t5 = -1, t25 = -1;
        for (int i = 0; i < N; ++i) { const double db = 10 * std::log10 (sch[(size_t) i] / e0 + 1e-30); if (t5 < 0 && db <= -5) t5 = i / sr; if (t25 < 0 && db <= -25) { t25 = i / sr; break; } }
        const double rt60 = (t25 - t5) * 3.0;
        INFO ("RT pedido %.1f s -> medido %.2f s", rt, rt60);
        CHECK (t5 >= 0 && t25 > t5 && rt60 > 0.55 * rt && rt60 < 1.45 * rt, "RT60 dentro de tolerancia");

        // correlación L/R de la IR
        double sl = 0, sr2 = 0, slr = 0;
        for (int i = 0; i < N; ++i) { sl += l[(size_t) i] * l[(size_t) i]; sr2 += r[(size_t) i] * r[(size_t) i]; slr += l[(size_t) i] * r[(size_t) i]; }
        const double corr = slr / std::sqrt (sl * sr2 + 1e-30);
        CHECK (std::fabs (corr) < 0.6, "correlación L/R controlada (%.2f)", corr);
    }
    // anchura 0 => mono
    {
        ReverbEngine e; e.prepare (sr); ReverbParams p; p.width = 0.0f; e.setParams (p);
        std::vector<float> in (20000, 0.0f), l (20000), r (20000); in[0] = 1.0f;
        e.process (in.data(), in.data(), l.data(), r.data(), 20000);
        double d = 0; for (int i = 0; i < 20000; ++i) d = std::max (d, (double) std::fabs (l[(size_t) i] - r[(size_t) i]));
        CHECK (d < 1e-6, "width=0 produce señal mono");
    }
    // ruido blanco 20 s con RT=12: sin crecimiento descontrolado y ganancia razonable
    {
        ReverbEngine e; e.prepare (sr); ReverbParams p; p.reverbTime = 12.0f; p.diffusion = 1.0f; e.setParams (p);
        std::mt19937 rng (1); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        const int N = (int) (sr * 20); std::vector<float> in ((size_t) N), l ((size_t) N), r ((size_t) N);
        for (auto& v : in) v = u (rng);
        e.process (in.data(), in.data(), l.data(), r.data(), N);
        double rin = 0, rout = 0; int cnt = 0; float pk = 0;
        for (int i = N / 2; i < N; ++i) { rin += in[(size_t) i] * in[(size_t) i]; rout += l[(size_t) i] * l[(size_t) i]; ++cnt; pk = std::max (pk, std::fabs (l[(size_t) i])); }
        const double gain = std::sqrt (rout / rin);
        INFO ("ganancia en régimen con ruido blanco, RT=12 s: %.2f (pico %.2f)", gain, pk);
        CHECK (gain < 8.0 && pk < 8.0, "sin acumulación incontrolada de energía");
    }
}

// ------------------------------------------------------------ ReverseProcessor
static void testReverseProcessor()
{
    std::printf ("[ReverseProcessor]\n");
    const double sr = 48000.0;
    std::vector<float> x ((size_t) (sr * 1.2), 0.0f);
    addVoice (x, sr, 0.05, 1.0);
    const float* ch[2] { x.data(), x.data() };
    ReverbParams p; p.reverbTime = 3.0f; p.preDelayMs = 20.0f;
    auto clip = ReverseProcessor::render (ch, 2, (int) x.size(), sr, p);
    CHECK (clip != nullptr, "render devuelve clip");
    if (! clip) return;
    const int total = clip->length;
    INFO ("longitud %.2f s, pico en %.2f s", total / sr, clip->peakIdx / sr);
    CHECK (clip->peakIdx > 0 && clip->peakIdx < total, "pico dentro del clip");
    // crecimiento: energía de los 300 ms previos al pico >> energía 1.5 s antes
    std::vector<float> d0 (clip->data[0]);
    const double before = rms (d0, sr, clip->peakIdx / sr - 0.3, clip->peakIdx / sr);
    const double early  = rms (d0, sr, clip->peakIdx / sr - 2.2, clip->peakIdx / sr - 1.9);
    INFO ("RMS a -0,3 s del pico %.4f ; a -2,2 s %.4f", before, early);
    CHECK (before > 8.0 * early, "la cola crece (inversa) hacia el pico");
    float pk = 0; for (auto v : d0) pk = std::max (pk, std::fabs (v));
    CHECK (pk <= 0.951f, "sin clipping (pico %.3f)", pk);
    // ventana: sin clics
    const auto w = SwellWindow::make (total, clip->peakIdx, (int) (sr * 1.0), 0.4f, 120.0f, sr);
    std::vector<float> y ((size_t) total, 0.0f);
    for (int i = 0; i < total; ++i) y[(size_t) i] = d0[(size_t) i] * w.gain (i);
    double maxStep = 0; for (int i = 1; i < total; ++i) maxStep = std::max (maxStep, (double) std::fabs (y[(size_t) i] - y[(size_t) i - 1]));
    CHECK (w.gain (w.fadeInStart) == 0.0f && w.gain (w.endIdx) == 0.0f, "ventana empieza y acaba en 0");
    CHECK (maxStep < 0.05 * pk + 0.02, "sin discontinuidades (salto máx %.4f)", maxStep);
    // cancelación
    std::atomic<int> gen { 1 };
    auto cancelled = ReverseProcessor::render (ch, 2, (int) x.size(), sr, p, &gen, 0);
    CHECK (cancelled == nullptr, "render cancelado por RESET");
}

// -------------------------------------------------------------- VoiceDetector
static void testVoiceDetector()
{
    std::printf ("[VoiceDetector]\n");
    const double sr = 48000.0;
    std::vector<float> x ((size_t) (sr * 6), 0.0f);
    addVoice (x, sr, 0.5, 0.8);  addVoice (x, sr, 2.5, 0.6);  addVoice (x, sr, 2.9, 0.4);  // 2 y 3 separadas 0,2 s (< gap)
    addVoice (x, sr, 4.5, 0.5);
    std::mt19937 rng (3); std::uniform_real_distribution<float> u (-1, 1);
    for (auto& v : x) v += 0.0005f * u (rng);        // ruido de fondo -66 dBFS
    VoiceDetector d; d.prepare (sr); d.setThresholdDb (-35); d.setPhraseGapMs (350); d.setLockoutMs (500);
    std::vector<double> starts, ends;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const auto e = d.process (std::fabs (x[i]));
        if (e == VoiceDetector::Event::PhraseStart) starts.push_back (i / sr);
        if (e == VoiceDetector::Event::PhraseEnd)   ends.push_back (i / sr);
    }
    INFO ("inicios: %zu, finales: %zu", starts.size(), ends.size());
    CHECK (starts.size() == 3 && ends.size() == 3, "3 frases (la pausa corta no divide la frase)");
    if (starts.size() == 3) { CHECK (std::fabs (starts[0] - 0.5) < 0.03 && std::fabs (starts[1] - 2.5) < 0.03 && std::fabs (starts[2] - 4.5) < 0.03, "inicios precisos (%.3f %.3f %.3f)", starts[0], starts[1], starts[2]); }
    if (ends.size() >= 1) CHECK (ends[0] > 1.3 + 0.3 && ends[0] < 1.3 + 0.45, "final tras el silencio configurado (%.3f)", ends[0]);
}

// ---------------------------------------------------------------- EffectCore
struct RunResult { std::vector<float> in, out; bool seen[4] { false, false, false, false }; State finalState = State::Waiting; };

static RunResult runCore (double sr, int channels, int block, CoreParams P, const std::vector<float>& input, double latencySec,
                          const std::vector<CoreEvent>& events = {}, HostTime host = {}, bool trackPpq = false)
{
    EffectCore core; core.prepare (sr, channels); core.setNonRealtime (true);
    core.setLatencySamples ((int) (latencySec * sr));
    RunResult R; R.in = input; R.out = input;
    std::vector<float> a (input), b (input);
    size_t pos = 0, evi = 0; std::vector<CoreEvent> local;
    while (pos < a.size())
    {
        const int n = (int) std::min<size_t> ((size_t) block, a.size() - pos);
        local.clear();
        while (evi < events.size() && events[evi].samplePos < (int) (pos + n)) { local.push_back ({ std::max (0, events[evi].samplePos - (int) pos), events[evi].type }); ++evi; }
        float* ptrs[2] { a.data() + pos, b.data() + pos };
        HostTime h = host; if (trackPpq) h.ppq = host.ppq + (double) pos / sr * host.bpm / 60.0;
        core.processBlock (ptrs, channels, n, P, h, local.data(), (int) local.size());
        R.seen[(int) core.state()] = true;
        pos += (size_t) n;
    }
    core.waitForIdle();
    R.finalState = core.state();
    R.out = a;
    return R;
}

static std::vector<float> delayed (const std::vector<float>& x, int d) { std::vector<float> y (x.size(), 0.0f); for (size_t i = (size_t) d; i < x.size(); ++i) y[i] = x[i - (size_t) d]; return y; }
static std::vector<float> minus (const std::vector<float>& a, const std::vector<float>& b) { std::vector<float> y (a.size()); for (size_t i = 0; i < a.size(); ++i) y[i] = a[i] - b[i]; return y; }

static CoreParams baseParams()
{
    CoreParams P; P.mode = Mode::Auto; P.reverseAmount = 1.0f; P.mix = 0.5f; P.captureSeconds = 2.0f;
    P.reverb.reverbTime = 2.0f; P.reverb.preDelayMs = 10.0f; P.attackFrac = 0.4f; P.tailMs = 120.0f;
    return P;
}

static void testCoreAuto (double sr, int channels, int block)
{
    std::printf ("[EffectCore AUTO] sr=%.0f canales=%d bloque=%d\n", sr, channels, block);
    const double L = 1.0;
    std::vector<float> x ((size_t) (sr * 8.0), 0.0f);
    addVoice (x, sr, 0.5, 1.0); addVoice (x, sr, 4.0, 1.0);

    // 1) 100 % seco => salida idéntica a la entrada retardada por la latencia (bit a bit)
    CoreParams Pd = baseParams(); Pd.mix = 0.0f;
    auto R0 = runCore (sr, channels, block, Pd, x, L);
    const auto ref = delayed (x, (int) (L * sr));
    double maxDiff = 0; for (size_t i = 0; i < ref.size(); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (R0.out[i] - ref[i]));
    CHECK (maxDiff == 0.0, "mix=0: voz seca intacta y retardada exactamente L (dif máx %.3g)", maxDiff);

    // 2) efecto activo
    CoreParams P = baseParams();
    auto R = runCore (sr, channels, block, P, x, L);
    const auto wet = minus (R.out, ref);
    CHECK (R.seen[0] && R.seen[1] && R.seen[2] && R.seen[3], "estados WAITING/CAPTURING/PROCESSING/READY observados (%d%d%d%d)", R.seen[0], R.seen[1], R.seen[2], R.seen[3]);
    CHECK (rms (wet, sr, 0.0, 3.9) < 1e-6, "sin efecto antes de la segunda frase (la primera sólo se captura)");
    const double early = rms (wet, sr, 4.0, 4.2), late = rms (wet, sr, 4.8, 5.0);
    INFO ("RMS wet 4.0-4.2 s: %.4f ; 4.8-5.0 s: %.4f", early, late);
    CHECK (late > 4.0 * early && late > 0.003, "la reverb crece hasta el inicio de la voz");
    const double pk = argmaxEnv (wet, sr, 4.0, 6.0);
    INFO ("pico del swell en %.3f s (voz seca empieza en %.3f s)", pk, 4.0 + L);
    CHECK (std::fabs (pk - (4.0 + L)) < 0.12, "el máximo coincide con el comienzo de la voz original");
    CHECK (rms (wet, sr, 5.5, 8.0) < 1e-6, "el efecto termina limpiamente");
    double step = 0, wp = 0; for (size_t i = 1; i < wet.size(); ++i) { step = std::max (step, (double) std::fabs (wet[i] - wet[i - 1])); wp = std::max (wp, (double) std::fabs (wet[i])); }
    CHECK (step < 0.25 * wp, "sin clics en el swell (salto máx %.4f, pico %.3f)", step, wp);
    bool finite = true; for (auto v : R.out) if (! std::isfinite (v)) finite = false;
    CHECK (finite, "salida finita");
    CHECK (R.finalState == State::Ready, "estado final READY");
}

static void testCoreManualMidiReset()
{
    std::printf ("[EffectCore MANUAL / cuantización / RESET]\n");
    const double sr = 48000.0, L = 1.0;
    std::vector<float> x ((size_t) (sr * 9.0), 0.0f);
    addVoice (x, sr, 0.5, 1.2);
    const auto ref = delayed (x, (int) (L * sr));

    CoreParams P = baseParams(); P.mode = Mode::Manual; P.captureSeconds = 1.5f;
    std::vector<CoreEvent> ev { { (int) (0.5 * sr), EventType::Capture }, { (int) (4.0 * sr), EventType::Reverse } };
    auto R = runCore (sr, 2, 480, P, x, L, ev);
    auto wet = minus (R.out, ref);
    const double pk = argmaxEnv (wet, sr, 3.5, 6.0);
    INFO ("MANUAL: pico en %.3f s (esperado %.3f)", pk, 4.0 + L);
    CHECK (std::fabs (pk - (4.0 + L)) < 0.12, "REVERSE manual alinea el pico con L");
    CHECK (rms (wet, sr, 0, 3.9) < 1e-6, "silencio antes del disparo");

    // arrival offset +200 ms
    CoreParams Po = P; Po.arrivalOffsetMs = 200.0f;
    auto Ro = runCore (sr, 2, 480, Po, x, L, ev);
    const double pko = argmaxEnv (minus (Ro.out, ref), sr, 3.5, 6.5);
    CHECK (std::fabs (pko - (pk + 0.2)) < 0.06, "ARRIVAL OFFSET +200 ms desplaza el pico (%.3f)", pko);

    // cuantización a compás: 120 BPM, el disparo en 4.1 s llega al compás siguiente (ppq 12 -> 6.0 s) => pico a 6.0+L
    HostTime h; h.hasTempo = true; h.bpm = 120; h.hasPpq = true; h.playing = true; h.ppq = 0.0;
    CoreParams Pq = P; Pq.quantize = 2;
    std::vector<CoreEvent> evq { { (int) (0.5 * sr), EventType::Capture }, { (int) (4.1 * sr), EventType::Reverse } };
    auto Rq = runCore (sr, 2, 480, Pq, x, L, evq, h, true);
    const double pkq = argmaxEnv (minus (Rq.out, ref), sr, 4.0, 8.5);
    INFO ("CUANTIZADO: pico en %.3f s (esperado %.3f)", pkq, 6.0 + L);
    CHECK (std::fabs (pkq - (6.0 + L)) < 0.12, "el pico cae en la rejilla del DAW");

    // RESET borra el búfer: tras RESET un REVERSE no produce nada
    std::vector<CoreEvent> evr { { (int) (0.5 * sr), EventType::Capture }, { (int) (3.0 * sr), EventType::Reset }, { (int) (4.0 * sr), EventType::Reverse } };
    auto Rr = runCore (sr, 2, 480, P, x, L, evr);
    CHECK (rms (minus (Rr.out, ref), sr, 3.5, 9.0) < 1e-6, "tras RESET no queda efecto");
    CHECK (Rr.finalState == State::Waiting, "estado WAITING tras RESET");
}

static void testBlockSizeInvariance()
{
    std::printf ("[EffectCore] invariancia respecto al tamaño de búfer\n");
    const double sr = 48000.0, L = 1.0;
    std::vector<float> x ((size_t) (sr * 7.0), 0.0f);
    addVoice (x, sr, 0.5, 1.0); addVoice (x, sr, 4.0, 1.0);
    const auto ref = delayed (x, (int) (L * sr));
    std::vector<double> peaks;
    for (int b : { 1, 17, 64, 128, 333, 512, 1024, 4096 })
    {
        auto R = runCore (sr, 2, b, baseParams(), x, L);
        peaks.push_back (argmaxEnv (minus (R.out, ref), sr, 4.0, 6.0));
    }
    double mn = *std::min_element (peaks.begin(), peaks.end()), mx = *std::max_element (peaks.begin(), peaks.end());
    INFO ("picos entre %.3f y %.3f s para búferes de 1 a 4096 muestras", mn, mx);
    CHECK (mx - mn < 0.02, "la alineación no depende del tamaño de búfer");
}


static void testExportMode()
{
    std::printf ("[EffectCore EXPORT] sin latencia, audio exportable\n");
    const double sr = 48000.0;
    std::vector<float> x ((size_t) (sr * 6.0), 0.0f);
    addVoice (x, sr, 0.5, 1.0); addVoice (x, sr, 3.5, 1.0);
    CoreParams P = baseParams(); P.mode = Mode::Export; P.mix = 0.5f; P.tailMs = 0.0f;
    EffectCore core; core.prepare (sr, 2); core.setNonRealtime (true); core.setLatencySamples (0);
    std::vector<float> a (x), b (x); HostTime h; bool ready = false;
    for (size_t pos = 0; pos < a.size(); pos += 480)
    {
        const int n = (int) std::min<size_t> (480, a.size() - pos);
        float* ptrs[2] { a.data() + pos, b.data() + pos };
        core.processBlock (ptrs, 2, n, P, h, nullptr, 0);
        if (core.state() == State::Ready) ready = true;
    }
    double md = 0; for (size_t i = 0; i < x.size(); ++i) md = std::max (md, (double) std::fabs (a[i] - x[i]));
    CHECK (md == 0.0, "modo EXPORT: la voz pasa intacta y sin retardo (dif %.3g)", md);
    CHECK (ready, "estado READY tras capturar");
    auto clip = core.getExportClip();
    CHECK (clip != nullptr, "clip exportable disponible");
    if (clip)
    {
        const int swell = (int) (1.5 * sr);
        std::vector<float> out[2];
        ReverseProcessor::buildSwellAudio (*clip, swell, 0.4f, 0.0f, out);
        const int n = (int) out[0].size();
        INFO ("archivo exportado: %.3f s, 2 canales: %d", n / sr, (int) (out[1].size() == out[0].size()));
        CHECK (std::abs (n - swell) <= (int) (0.006 * sr) + 2, "con TAIL=0 el archivo termina en el punto de llegada");
        CHECK (out[0].front() == 0.0f, "empieza en silencio");
        // el máximo de energía está al final (crecimiento invertido)
        const double last = rms (out[0], sr, (n - 0.3 * sr) / sr, n / sr), first = rms (out[0], sr, 0.0, 0.3);
        CHECK (last > 6.0 * first, "el audio exportado crece hasta el final (%.4f vs %.4f)", last, first);
        std::vector<float> out2[2];
        ReverseProcessor::buildSwellAudio (*clip, swell, 0.4f, 200.0f, out2);
        CHECK ((int) out2[0].size() - swell >= (int) (0.19 * sr), "con TAIL=200 ms hay cola tras el pico");
        double step = 0; for (size_t i = 1; i < out2[0].size(); ++i) step = std::max (step, (double) std::fabs (out2[0][i] - out2[0][i - 1]));
        CHECK (step < 0.25, "sin discontinuidades (%.3f)", step);
    }
    // RESET invalida la exportación
    CoreEvent ev { 0, EventType::Reset };
    float* ptrs[2] { a.data(), b.data() }; core.processBlock (ptrs, 2, 64, P, h, &ev, 1);
    CHECK (core.getExportClip() == nullptr, "tras RESET no hay nada que exportar");
}

int main()
{
    enableFlushDenormals();
    testTempoSync();
    testReverbEngine();
    testReverseProcessor();
    testVoiceDetector();
    testCoreAuto (48000.0, 2, 512);
    testCoreAuto (44100.0, 2, 256);
    testCoreAuto (96000.0, 2, 1024);
    testCoreAuto (48000.0, 1, 480);
    testCoreManualMidiReset();
    testBlockSizeInvariance();
    testExportMode();
    std::printf ("\n%d comprobaciones, %d fallos\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
