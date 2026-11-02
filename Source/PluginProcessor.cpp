#include "PluginProcessor.h"
#include "PluginEditor.h"

ReverseVocalProcessor::ReverseVocalProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    startTimerHz (30);
}

ReverseVocalProcessor::~ReverseVocalProcessor() { stopTimer(); }

bool ReverseVocalProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto& in = l.getMainInputChannelSet();
    const auto& out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

rvr::CoreParams ReverseVocalProcessor::readParams() const
{
    rvr::CoreParams P;
    P.mode            = (rvr::Mode) juce::roundToInt (raw (PID::mode));
    P.autoEveryPhrase = juce::roundToInt (raw (PID::autoRepeat)) == 1;
    P.reverseAmount   = raw (PID::reverseAmount);
    P.mix             = raw (PID::mix);
    P.dryDb           = raw (PID::dryLevel);
    P.wetDb           = raw (PID::wetLevel);
    P.attackFrac      = raw (PID::attack);
    P.tailMs          = raw (PID::tail);
    P.arrivalOffsetMs = raw (PID::arrivalOffset);
    P.thresholdDb     = raw (PID::threshold);
    P.phraseGapMs     = raw (PID::phraseGap);
    P.lockoutMs       = raw (PID::lockout);
    P.captureSeconds  = raw (PID::reverseLength);
    P.quantize        = juce::roundToInt (raw (PID::quantize));
    P.reverb.reverbTime = raw (PID::reverbTime);
    P.reverb.roomSize   = raw (PID::roomSize);
    P.reverb.diffusion  = raw (PID::diffusion);
    P.reverb.preDelayMs = raw (PID::preDelay);
    P.reverb.lowCutHz   = raw (PID::lowCut);
    P.reverb.highCutHz  = raw (PID::highCut);
    P.reverb.width      = raw (PID::stereoWidth);
    return P;
}

static rvr::HostTime readHost (juce::AudioPlayHead* ph)
{
    rvr::HostTime h;
    if (ph == nullptr) return h;
    if (auto pos = ph->getPosition())
    {
        if (auto bpm = pos->getBpm())            { h.hasTempo = true; h.bpm = *bpm; }
        if (auto ts = pos->getTimeSignature())   { h.tsNum = ts->numerator; h.tsDen = ts->denominator; }
        if (auto ppq = pos->getPpqPosition())    { h.hasPpq = true; h.ppq = *ppq; }
        h.playing = pos->getIsPlaying();
    }
    return h;
}

void ReverseVocalProcessor::prepareToPlay (double sr, int)
{
    currentSampleRate = sr;
    const int nCh = juce::jmax (1, getTotalNumInputChannels());
    core.prepare (sr, nCh);

    const rvr::HostTime h;     // sin tempo todavía: usa la duración libre
    const bool exportMode = juce::roundToInt (raw (PID::mode)) == (int) rvr::Mode::Export;
    const int lat = exportMode ? 0 : (int) std::llround (rvr::TempoSync::swellSeconds (raw (PID::tempoSync) > 0.5f, juce::roundToInt (raw (PID::syncDiv)),
                                                                        raw (PID::swellTime), h) * sr);
    desiredLatency = committedLatency = lat;
    core.setLatencySamples (lat);
    setLatencySamples (lat);
    prevCapture = prevReverse = prevReset = prevPlaying = haveExpectedPpq = false;
}

void ReverseVocalProcessor::releaseResources() { core.release(); }

void ReverseVocalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nCh = juce::jmin (buffer.getNumChannels(), 2);
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, n);

    const rvr::CoreParams P = readParams();
    const rvr::HostTime host = readHost (getPlayHead());

    // latencia = duración del swell (se comunica al DAW desde el hilo de mensajes, ver timerCallback)
    if (host.hasTempo) { hostBpm.store (host.bpm); hostTsNum.store (host.tsNum); hostTsDen.store (host.tsDen); }
    const bool exportMode = P.mode == rvr::Mode::Export;   // modo exportación: sin latencia
    desiredLatency.store (exportMode ? 0 : (int) std::llround (rvr::TempoSync::swellSeconds (raw (PID::tempoSync) > 0.5f, juce::roundToInt (raw (PID::syncDiv)),
                                                                                              raw (PID::swellTime), host) * currentSampleRate));
    core.setLatencySamples (committedLatency.load());
    core.setNonRealtime (isNonRealtime());

    // reinicio / salto de transporte
    if (host.playing && ! prevPlaying) core.transportRestart();
    if (host.playing && host.hasPpq && haveExpectedPpq && std::abs (host.ppq - expectedPpq) > 0.1) core.transportRestart();
    prevPlaying = host.playing;
    haveExpectedPpq = host.playing && host.hasPpq && host.hasTempo;
    if (haveExpectedPpq) expectedPpq = host.ppq + (double) n * host.bpm / 60.0 / currentSampleRate;

    // eventos: botones/automatización (flancos) y notas MIDI (C1 captura, D1 reverse, E1 reset)
    std::array<rvr::CoreEvent, 64> events;
    int numEv = 0;
    const bool cap = raw (PID::capture) > 0.5f, rev = raw (PID::reverse) > 0.5f, rst = raw (PID::reset) > 0.5f;
    if (rst && ! prevReset)   events[(size_t) numEv++] = { 0, rvr::EventType::Reset };
    if (cap && ! prevCapture) events[(size_t) numEv++] = { 0, rvr::EventType::Capture };
    if (rev && ! prevReverse) events[(size_t) numEv++] = { 0, rvr::EventType::Reverse };
    prevCapture = cap; prevReverse = rev; prevReset = rst;

    if (P.mode == rvr::Mode::Midi)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOn() || numEv >= (int) events.size()) continue;
            const int pos = juce::jlimit (0, n - 1, meta.samplePosition);
            if (m.getNoteNumber() == 36)      events[(size_t) numEv++] = { pos, rvr::EventType::Capture };
            else if (m.getNoteNumber() == 38) events[(size_t) numEv++] = { pos, rvr::EventType::Reverse };
            else if (m.getNoteNumber() == 40) events[(size_t) numEv++] = { pos, rvr::EventType::Reset };
        }
    midi.clear();

    core.processBlock (buffer.getArrayOfWritePointers(), nCh, n, P, host, events.data(), numEv);
}

void ReverseVocalProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // El bypass mantiene el retardo de la voz seca para no desalinear el proyecto.
    juce::ScopedNoDenormals noDenormals;
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    core.setLatencySamples (committedLatency.load());
    core.bypassBlock (buffer.getArrayOfWritePointers(), juce::jmin (buffer.getNumChannels(), 2), buffer.getNumSamples());
    midi.clear();
}

void ReverseVocalProcessor::timerCallback()
{
    // 1) comunica cambios de latencia al DAW (hilo de mensajes) con un poco de histéresis
    const int des = desiredLatency.load(), com = committedLatency.load();
    if (std::abs (des - com) > juce::jmax (32, com / 100))
    {
        committedLatency.store (des);
        setLatencySamples (des);
    }
    // 2) suelta los disparadores pulsados desde la interfaz
    for (size_t i = pulses.size(); i-- > 0;)
        if (--pulses[i].ticksLeft <= 0)
        {
            if (auto* p = apvts.getParameter (pulses[i].id)) { p->beginChangeGesture(); p->setValueNotifyingHost (0.0f); p->endChangeGesture(); }
            pulses.erase (pulses.begin() + (long) i);
        }
}

bool ReverseVocalProcessor::exportReversed (juce::File& outFile, juce::String& message)
{
    auto clip = core.getExportClip();
    if (clip == nullptr)
    {
        message = "Nada que exportar: captura primero una frase (estado READY).";
        return false;
    }

    rvr::HostTime h;
    if (hostBpm.load() > 1.0) { h.hasTempo = true; h.bpm = hostBpm.load(); h.tsNum = hostTsNum.load(); h.tsDen = hostTsDen.load(); }
    const double swellSec = rvr::TempoSync::swellSeconds (raw (PID::tempoSync) > 0.5f, juce::roundToInt (raw (PID::syncDiv)), raw (PID::swellTime), h);
    const int swell = (int) std::llround (swellSec * clip->sampleRate);

    std::vector<float> audio[2];
    rvr::ReverseProcessor::buildSwellAudio (*clip, swell, raw (PID::attack), raw (PID::tail), audio);
    const int n = (int) audio[0].size();
    if (n < 16) { message = "El clip es demasiado corto."; return false; }

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Reverse Vocal Reverb");
    if (! dir.createDirectory().wasOk()) { message = "No se pudo crear la carpeta " + dir.getFullPathName(); return false; }
    outFile = dir.getChildFile ("RVR_swell_" + juce::String (swellSec, 2) + "s_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav");

    std::unique_ptr<juce::FileOutputStream> os (outFile.createOutputStream());
    if (os == nullptr || ! os->openedOk()) { message = "No se pudo escribir " + outFile.getFullPathName(); return false; }

    juce::WavAudioFormat wav;
    JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), clip->sampleRate, (unsigned int) clip->numChannels, 24, {}, 0));
    JUCE_END_IGNORE_DEPRECATION_WARNINGS
    if (writer == nullptr) { message = "No se pudo crear el WAV."; return false; }
    os.release();                                   // el writer es ahora dueño del stream
    const float* ptrs[2] { audio[0].data(), audio[clip->numChannels > 1 ? 1 : 0].data() };
    const bool ok = writer->writeFromFloatArrays (ptrs, clip->numChannels, n);
    writer.reset();
    if (! ok) { message = "Error al escribir el WAV."; return false; }

    lastExport = outFile;
    message = "Guardado: " + outFile.getFileName() + "  (el pico llega a los " + juce::String (swellSec, 2) + " s)";
    return true;
}

void ReverseVocalProcessor::pulseTrigger (const char* id)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost (1.0f); p->endChangeGesture();
        pulses.push_back ({ id, 5 });     // ~170 ms
    }
}

void ReverseVocalProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    currentProgram = index;
    // 1) todo a valores por defecto (salvo disparadores y bypass), 2) valores del preset
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto pid = rp->paramID;
            if (pid == PID::capture || pid == PID::reverse || pid == PID::reset || pid == PID::bypass || pid == PID::mode || pid == PID::autoRepeat) continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }
    for (const auto& kv : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (kv.first)) rp->setValueNotifyingHost (rp->convertTo0to1 (kv.second));
}

const juce::String ReverseVocalProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    return (index >= 0 && index < (int) presets.size()) ? juce::String (presets[(size_t) index].name) : juce::String();
}

void ReverseVocalProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void ReverseVocalProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentProgram = (int) vt.getProperty ("program", 0);
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* ReverseVocalProcessor::createEditor() { return new ReverseVocalEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ReverseVocalProcessor(); }
