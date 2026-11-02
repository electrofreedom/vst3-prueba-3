#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

/** Aspecto oscuro, minimalista, estilo estudio de producción. */
class RvrLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RvrLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    static juce::Colour accent() { return juce::Colour (0xff7a5cff); }
    static juce::Colour accent2() { return juce::Colour (0xff3fd2e6); }
};

/** Zona para arrastrar el WAV exportado directamente a una pista del DAW. */
class ExportDragSource : public juce::Component
{
public:
    std::function<juce::File()> getFile;
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
};

class ReverseVocalEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit ReverseVocalEditor (ReverseVocalProcessor&);
    ~ReverseVocalEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider; juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Combo
    {
        juce::ComboBox box; juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    void timerCallback() override { repaint(); }
    Knob& addKnob (const char* paramID, const juce::String& text, int row, int col);
    Combo& addCombo (const char* paramID, const juce::String& text);

    ReverseVocalProcessor& proc;
    RvrLookAndFeel laf;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> groups;
    juce::TextButton captureBtn { "CAPTURE" }, reverseBtn { "REVERSE" }, resetBtn { "RESET" };
    juce::ToggleButton syncBtn { "TEMPO SYNC" }, bypassBtn { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt, bypassAtt;
    juce::ComboBox presetBox;
    juce::TextButton exportBtn { "EXPORT WAV" }, folderBtn { "OPEN FOLDER" };
    ExportDragSource dragSource;
    juce::String exportMessage;
    juce::Rectangle<int> exportMsgArea;
    juce::Rectangle<int> envArea, stateArea;
    Combo *modeCombo = nullptr, *repeatCombo = nullptr, *syncDivCombo = nullptr, *quantCombo = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReverseVocalEditor)
};
