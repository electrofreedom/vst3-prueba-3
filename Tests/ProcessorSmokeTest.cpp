// Prueba de integración sin interfaz: carga el PluginProcessor real de JUCE (parámetros, MIDI,
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// estado, presets, latencia) y procesa audio sintético como lo haría un DAW.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Source/PluginProcessor.h"
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { ++checks; if (! (c)) { ++fails; std::printf ("  FALLO %s:%d %s -> ", __FILE__, __LINE__, #c); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static void voice (std::vector<float>& x, double sr, double t0, double dur)
{
    for (int i = 0; i < (int) (dur * sr); ++i)
    {
        const double t = i / sr; double v = 0;
        for (int h = 1; h <= 12; ++h) v += std::sin (2.0 * M_PI * 140.0 * h * t) / h;
        const size_t k = (size_t) (t0 * sr) + (size_t) i;
        if (k < x.size()) x[k] += (float) (0.165 * v * std::min (1.0, t / 0.008) * std::min (1.0, (dur - t) / 0.03));
    }
}
static double rms (const std::vector<float>& x, double sr, double a, double b)
{ double s = 0; int n = 0; for (int i = (int) (a * sr); i < (int) (b * sr) && i < (int) x.size(); ++i, ++n) s += x[(size_t) i] * x[(size_t) i]; return n ? std::sqrt (s / n) : 0; }

static void setParam (ReverseVocalProcessor& p, const char* id, float realValue)
{ auto* rp = p.apvts.getParameter (id); rp->setValueNotifyingHost (rp->convertTo0to1 (realValue)); }

struct Out { std::vector<float> l, r; };

static Out run (ReverseVocalProcessor& p, const std::vector<float>& in, int block, juce::MidiBuffer* midiAt = nullptr, int midiBlockIndex = -1)
{
    Out o; o.l = in; o.r = in;
    juce::AudioBuffer<float> buf (2, block);
    for (size_t pos = 0, bi = 0; pos < in.size(); pos += (size_t) block, ++bi)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) { buf.setSample (0, i, in[pos + (size_t) i]); buf.setSample (1, i, in[pos + (size_t) i]); }
        juce::MidiBuffer m;
        if (midiAt != nullptr && (int) bi == midiBlockIndex) m = *midiAt;
        p.processBlock (buf, m);
        for (int i = 0; i < n; ++i) { o.l[pos + (size_t) i] = buf.getSample (0, i); o.r[pos + (size_t) i] = buf.getSample (1, i); }
    }
    return o;
}

static double argmax (const std::vector<float>& x, double sr, double a, double b)
{ double best = -1, bt = a; for (double t = a; t + 0.05 <= b; t += 0.01) { const double r = rms (x, sr, t, t + 0.05); if (r > best) { best = r; bt = t + 0.025; } } return bt; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0; const int block = 512;

    std::printf ("[Editor] construccion y pintado de la interfaz\n");
    {
        ReverseVocalProcessor p; p.setPlayConfigDetails (2, 2, sr, block); p.prepareToPlay (sr, block);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        CHECK (ed != nullptr, "createEditor");
        if (ed != nullptr)
        {
            CHECK (ed->getWidth() > 400 && ed->getHeight() > 300, "tamano de la interfaz %dx%d", ed->getWidth(), ed->getHeight());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rvr_editor.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (img, os); }
            std::printf ("  . captura en %s\n", f.getFullPathName().toRawUTF8());
        }
        ed.reset();
    }
    std::vector<float> in ((size_t) (sr * 10.0), 0.0f);
    voice (in, sr, 0.5, 1.0); voice (in, sr, 4.0, 1.0);

    std::printf ("[Processor] latencia, voz seca, AUTO\n");
    {
        ReverseVocalProcessor p; p.setNonRealtime (true);
        p.setPlayConfigDetails (2, 2, sr, block); p.prepareToPlay (sr, block);
        CHECK (p.getLatencySamples() == (int) (2.0 * sr), "latencia = swell por defecto (2 s): %d", p.getLatencySamples());
        CHECK (p.getNumPrograms() >= 5 && p.getProgramName (1).isNotEmpty(), "presets de fábrica");
        setParam (p, PID::mix, 0.0f);
        auto dry = run (p, in, block);
        double md = 0; const int L = p.getLatencySamples();
        for (size_t i = 0; i < in.size(); ++i) md = std::max (md, (double) std::fabs (dry.l[i] - (i >= (size_t) L ? in[i - (size_t) L] : 0.0f)));
        CHECK (md == 0.0, "mix=0 => voz seca idéntica, retardada L (dif %.3g)", md);

        ReverseVocalProcessor q; q.setNonRealtime (true);
        q.setPlayConfigDetails (2, 2, sr, block); q.prepareToPlay (sr, block);
        auto out = run (q, in, block);
        std::vector<float> wet (in.size());
        for (size_t i = 0; i < in.size(); ++i) wet[i] = out.l[i] - (i >= (size_t) L ? in[i - (size_t) L] : 0.0f);
        const double pk = argmax (wet, sr, 4.0, 7.0);
        std::printf ("  . pico del swell en %.3f s (voz seca en %.3f s)\n", pk, 4.0 + 2.0);
        CHECK (std::fabs (pk - 6.0) < 0.12, "pico alineado con el inicio de la voz retardada");
        CHECK (rms (wet, sr, 0, 3.9) < 1e-6, "sin efecto antes de la 2.ª frase");
        CHECK (q.getCore().state() == rvr::State::Ready, "estado READY");
    }

    std::printf ("[Processor] MIDI TRIGGER\n");
    {
        ReverseVocalProcessor p; p.setNonRealtime (true);
        p.setPlayConfigDetails (2, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, PID::mode, 2.0f); setParam (p, PID::reverseLength, 1.5f);
        juce::MidiBuffer capture, play;
        capture.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
        play.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 10);
        // dos pasadas por bloque: nota de captura en 0.5 s, nota de reverse en 4.0 s
        Out o; o.l = in;
        juce::AudioBuffer<float> buf (2, block);
        const int capBlock = (int) (0.5 * sr) / block, playBlock = (int) (4.0 * sr) / block;
        for (size_t pos = 0, bi = 0; pos + (size_t) block <= in.size(); pos += (size_t) block, ++bi)
        {
            for (int i = 0; i < block; ++i) { buf.setSample (0, i, in[pos + (size_t) i]); buf.setSample (1, i, in[pos + (size_t) i]); }
            juce::MidiBuffer m; if ((int) bi == capBlock) m = capture; if ((int) bi == playBlock) m = play;
            p.processBlock (buf, m);
            for (int i = 0; i < block; ++i) o.l[pos + (size_t) i] = buf.getSample (0, i);
        }
        const int L = p.getLatencySamples();
        std::vector<float> wet (in.size());
        for (size_t i = 0; i < in.size(); ++i) wet[i] = o.l[i] - (i >= (size_t) L ? in[i - (size_t) L] : 0.0f);
        const double pk = argmax (wet, sr, 3.5, 7.5);
        const double expected = (playBlock * block + 10) / sr + 2.0;
        std::printf ("  . pico en %.3f s (esperado %.3f)\n", pk, expected);
        CHECK (std::fabs (pk - expected) < 0.12, "nota MIDI dispara el swell alineado");
    }


    std::printf ("[Processor] modo EXPORT: latencia 0 y WAV\n");
    {
        ReverseVocalProcessor p; p.setNonRealtime (true);
        p.setPlayConfigDetails (2, 2, sr, block);
        setParam (p, PID::mode, 3.0f);
        p.prepareToPlay (sr, block);
        CHECK (p.getLatencySamples() == 0, "latencia 0 en modo EXPORT (%d)", p.getLatencySamples());
        std::vector<float> x2 ((size_t) (sr * 5.0), 0.0f); voice (x2, sr, 0.5, 1.0);
        auto o = run (p, x2, block);
        double md = 0; for (size_t i = 0; i < x2.size(); ++i) md = std::max (md, (double) std::fabs (o.l[i] - x2[i]));
        CHECK (md == 0.0, "la voz pasa sin tocar ni retardar");
        setParam (p, PID::swellTime, 1.5f); setParam (p, PID::tail, 0.0f);
        juce::File f; juce::String msg;
        const bool ok = p.exportReversed (f, msg);
        CHECK (ok, "exportReversed: %s", msg.toRawUTF8());
        if (ok)
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            CHECK (rd != nullptr, "WAV legible");
            if (rd)
            {
                std::printf ("  . %s: %.0f Hz, %u canales, %.3f s, %u bits\n", f.getFileName().toRawUTF8(), rd->sampleRate, rd->numChannels, rd->lengthInSamples / rd->sampleRate, rd->bitsPerSample);
                CHECK (rd->sampleRate == sr && rd->numChannels == 2 && rd->bitsPerSample == 24, "formato 24 bits estereo");
                CHECK (std::fabs (rd->lengthInSamples / sr - 1.5) < 0.02, "duracion = SWELL TIME con TAIL 0");
                juce::AudioBuffer<float> wb (2, (int) rd->lengthInSamples); rd->read (&wb, 0, (int) rd->lengthInSamples, 0, true, true);
                const int nn = wb.getNumSamples(); float a0 = 0, a1 = 0;
                for (int i = 0; i < (int) (0.2 * sr); ++i) { a0 += std::fabs (wb.getSample (0, i)); a1 += std::fabs (wb.getSample (0, nn - 1 - i)); }
                CHECK (a1 > 6.0f * a0, "el WAV crece hacia el final");
            }
            f.deleteFile();
        }
    }

    std::printf ("[Processor] estado, presets, bypass, canales\n");
    {
        ReverseVocalProcessor p; p.setPlayConfigDetails (2, 2, sr, block); p.prepareToPlay (sr, block);
        p.setCurrentProgram (1);
        CHECK (std::abs (p.apvts.getRawParameterValue (PID::reverbTime)->load() - 6.0f) < 1e-3f, "preset aplicado");
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ReverseVocalProcessor q; q.setPlayConfigDetails (2, 2, sr, block); q.prepareToPlay (sr, block);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (std::abs (q.apvts.getRawParameterValue (PID::reverbTime)->load() - 6.0f) < 1e-3f, "estado guardado y restaurado");
        CHECK (q.getCurrentProgram() == 1, "programa restaurado");

        setParam (q, PID::bypass, 1.0f);
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        for (int i = 0; i < 256; ++i) { b.setSample (0, i, 0.1f); b.setSample (1, i, 0.1f); }
        q.processBlockBypassed (b, m);
        CHECK (std::isfinite (b.getSample (0, 100)), "bypass seguro");

        ReverseVocalProcessor mono; mono.setPlayConfigDetails (1, 1, sr, block);
        juce::AudioProcessor::BusesLayout lay; lay.inputBuses.add (juce::AudioChannelSet::mono()); lay.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (mono.checkBusesLayoutSupported (lay), "layout mono admitido");
        for (int sr2 : { 44100, 88200, 96000 })
        {
            ReverseVocalProcessor z; z.setNonRealtime (true); z.setPlayConfigDetails (2, 2, sr2, 480); z.prepareToPlay (sr2, 480);
            std::vector<float> x ((size_t) (sr2 * 6.0), 0.0f); voice (x, sr2, 0.3, 0.8); voice (x, sr2, 3.0, 0.8);
            auto o = run (z, x, 480);
            bool fin = true; for (auto v : o.l) if (! std::isfinite (v)) fin = false;
            CHECK (fin, "salida finita a %d Hz", sr2);
        }
    }
    std::printf ("\n%d comprobaciones, %d fallos\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
