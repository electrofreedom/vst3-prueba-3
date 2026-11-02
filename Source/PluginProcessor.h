#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"
#include "EffectCore.h"

class ReverseVocalProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ReverseVocalProcessor();
    ~ReverseVocalProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Reverse Vocal Reverb"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (PID::bypass); }

    /** Guarda el swell invertido de la última captura como WAV de 24 bits (hilo de mensajes).
        El máximo del swell queda exactamente en `SWELL TIME` desde el inicio del archivo. */
    bool exportReversed (juce::File& outFile, juce::String& message);
    juce::File getLastExport() const { return lastExport; }

    /** Botones de la interfaz: activa un parámetro "disparador" y lo suelta poco después. */
    void pulseTrigger (const char* paramID);

    rvr::EffectCore& getCore() noexcept { return core; }
    juce::AudioProcessorValueTreeState apvts;

private:
    void timerCallback() override;
    rvr::CoreParams readParams() const;
    float raw (const char* id) const noexcept { return apvts.getRawParameterValue (id)->load(); }

    rvr::EffectCore core;
    std::atomic<int> desiredLatency { 96000 }, committedLatency { 96000 };
    double currentSampleRate = 48000.0;
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<int> hostTsNum { 4 }, hostTsDen { 4 };
    juce::File lastExport;
    int currentProgram = 0;

    bool prevCapture = false, prevReverse = false, prevReset = false, prevPlaying = false, haveExpectedPpq = false;
    double expectedPpq = 0.0;

    struct Pending { juce::String id; int ticksLeft; };
    std::vector<Pending> pulses;     // sólo hilo de mensajes

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseVocalProcessor)
};
