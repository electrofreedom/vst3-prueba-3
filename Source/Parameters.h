#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

/** IDs y diseño de todos los parámetros (automatizables desde el DAW). */
namespace PID
{
    constexpr const char* mode = "mode";                 constexpr const char* autoRepeat = "autoRepeat";
    constexpr const char* reverseAmount = "reverseAmount"; constexpr const char* mix = "mix";
    constexpr const char* reverbTime = "reverbTime";     constexpr const char* roomSize = "roomSize";
    constexpr const char* diffusion = "diffusion";       constexpr const char* preDelay = "preDelay";
    constexpr const char* lowCut = "lowCut";             constexpr const char* highCut = "highCut";
    constexpr const char* stereoWidth = "stereoWidth";   constexpr const char* reverseLength = "reverseLength";
    constexpr const char* threshold = "triggerThreshold"; constexpr const char* phraseGap = "phraseGap";
    constexpr const char* lockout = "lockout";           constexpr const char* attack = "attack";
    constexpr const char* tail = "tail";                 constexpr const char* arrivalOffset = "arrivalOffset";
    constexpr const char* swellTime = "swellTime";       constexpr const char* tempoSync = "tempoSync";
    constexpr const char* syncDiv = "syncDiv";           constexpr const char* quantize = "quantize";
    constexpr const char* dryLevel = "dryLevel";         constexpr const char* wetLevel = "wetLevel";
    constexpr const char* capture = "capture";           constexpr const char* reverse = "reverse";
    constexpr const char* reset = "reset";               constexpr const char* bypass = "bypass";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using F = AudioParameterFloat;
    using C = AudioParameterChoice;
    using B = AudioParameterBool;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto sec = [] (float v, int) { return String (v, 2) + " s"; };
    auto ms  = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto db  = [] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; };
    auto hz  = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; };

    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<C> (id (PID::mode), "Mode", StringArray { "AUTO", "MANUAL", "MIDI TRIGGER", "EXPORT (NO LATENCY)" }, 0));
    p.push_back (std::make_unique<C> (id (PID::autoRepeat), "Auto Capture", StringArray { "ONCE", "EVERY PHRASE" }, 0));

    p.push_back (std::make_unique<F> (id (PID::reverseAmount), "Reverse Amount", NormalisableRange<float> (0.0f, 1.0f), 0.85f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::mix), "Mix", NormalisableRange<float> (0.0f, 1.0f), 0.5f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::reverbTime), "Reverb Time", NormalisableRange<float> (0.5f, 12.0f, 0.01f, 0.5f), 3.0f, AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    p.push_back (std::make_unique<F> (id (PID::roomSize), "Room Size", NormalisableRange<float> (0.0f, 1.0f), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::diffusion), "Diffusion", NormalisableRange<float> (0.0f, 1.0f), 0.8f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::preDelay), "Pre-Delay", NormalisableRange<float> (0.0f, 250.0f, 0.1f, 0.6f), 20.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<F> (id (PID::lowCut), "Low Cut", NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 150.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<F> (id (PID::highCut), "High Cut", NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.4f), 9000.0f, AudioParameterFloatAttributes().withStringFromValueFunction (hz)));
    p.push_back (std::make_unique<F> (id (PID::stereoWidth), "Stereo Width", NormalisableRange<float> (0.0f, 2.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::reverseLength), "Reverse Length", NormalisableRange<float> (0.25f, 8.0f, 0.01f, 0.5f), 2.0f, AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    p.push_back (std::make_unique<F> (id (PID::threshold), "Trigger Threshold", NormalisableRange<float> (-60.0f, -6.0f, 0.1f), -35.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<F> (id (PID::phraseGap), "Phrase Gap", NormalisableRange<float> (50.0f, 2000.0f, 1.0f, 0.5f), 350.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<F> (id (PID::lockout), "Lockout", NormalisableRange<float> (0.0f, 5000.0f, 1.0f, 0.5f), 500.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<F> (id (PID::attack), "Attack / Fade-In", NormalisableRange<float> (0.0f, 1.0f), 0.4f, AudioParameterFloatAttributes().withStringFromValueFunction (pct)));
    p.push_back (std::make_unique<F> (id (PID::tail), "Tail / Fade-Out", NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.5f), 120.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<F> (id (PID::arrivalOffset), "Arrival Offset", NormalisableRange<float> (-500.0f, 500.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (ms)));
    p.push_back (std::make_unique<F> (id (PID::swellTime), "Swell Time", NormalisableRange<float> (0.1f, 12.0f, 0.01f, 0.5f), 2.0f, AudioParameterFloatAttributes().withStringFromValueFunction (sec)));
    p.push_back (std::make_unique<B> (id (PID::tempoSync), "Tempo Sync", false));
    p.push_back (std::make_unique<C> (id (PID::syncDiv), "Sync Length", StringArray { "1/4 BAR", "1/2 BAR", "1 BAR", "2 BARS", "4 BARS" }, 2));
    p.push_back (std::make_unique<C> (id (PID::quantize), "Grid Arrival", StringArray { "OFF", "BEAT", "BAR" }, 0));
    p.push_back (std::make_unique<F> (id (PID::dryLevel), "Dry Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<F> (id (PID::wetLevel), "Wet Level", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    p.push_back (std::make_unique<B> (id (PID::capture), "Capture", false));
    p.push_back (std::make_unique<B> (id (PID::reverse), "Reverse", false));
    p.push_back (std::make_unique<B> (id (PID::reset), "Reset", false));
    p.push_back (std::make_unique<B> (id (PID::bypass), "Bypass", false));
    return { p.begin(), p.end() };
}

/** Presets de fábrica (valores reales de los parámetros). */
struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> values; };

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init (Default)", {} },
        { "Cinematic Swell", { { PID::reverbTime, 6.0f }, { PID::roomSize, 0.85f }, { PID::diffusion, 0.9f }, { PID::swellTime, 3.0f }, { PID::attack, 0.6f }, { PID::tail, 200.0f }, { PID::highCut, 7000.0f }, { PID::mix, 0.55f } } },
        { "Ad-lib Riser", { { PID::reverbTime, 2.5f }, { PID::roomSize, 0.5f }, { PID::swellTime, 1.0f }, { PID::attack, 0.3f }, { PID::tail, 80.0f }, { PID::reverseLength, 1.2f }, { PID::lowCut, 250.0f } } },
        { "Huge Bloom", { { PID::reverbTime, 10.0f }, { PID::roomSize, 1.0f }, { PID::diffusion, 1.0f }, { PID::swellTime, 4.0f }, { PID::attack, 0.8f }, { PID::stereoWidth, 1.4f }, { PID::highCut, 6000.0f }, { PID::wetLevel, -2.0f } } },
        { "Tight Pre-Chorus", { { PID::reverbTime, 1.5f }, { PID::roomSize, 0.3f }, { PID::swellTime, 0.5f }, { PID::attack, 0.2f }, { PID::tail, 40.0f }, { PID::preDelay, 5.0f } } },
        { "Dark Wash", { { PID::reverbTime, 5.0f }, { PID::roomSize, 0.7f }, { PID::highCut, 3500.0f }, { PID::lowCut, 200.0f }, { PID::swellTime, 2.5f }, { PID::mix, 0.6f } } },
    };
    return presets;
}
