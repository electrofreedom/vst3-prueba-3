#include "PluginEditor.h"

using namespace juce;

static const Colour kBg (0xff0c0e12), kPanel (0xff151920), kText (0xffd7dbe3), kDim (0xff7d8594);

RvrLookAndFeel::RvrLookAndFeel()
{
    setColour (ComboBox::backgroundColourId, Colour (0xff1c212b));
    setColour (ComboBox::outlineColourId, Colour (0xff2b3240));
    setColour (ComboBox::textColourId, kText);
    setColour (ComboBox::arrowColourId, accent2());
    setColour (PopupMenu::backgroundColourId, Colour (0xff1c212b));
    setColour (PopupMenu::textColourId, kText);
    setColour (PopupMenu::highlightedBackgroundColourId, accent());
    setColour (Label::textColourId, kText);
    setColour (TextButton::buttonColourId, Colour (0xff1c212b));
    setColour (TextButton::textColourOffId, kText);
    setColour (Slider::textBoxTextColourId, kText);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (ToggleButton::textColourId, kText);
}

void RvrLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const float arcR = r - 3.0f;

    g.setColour (Colour (0xff1c212b));
    g.fillEllipse (c.x - r + 7, c.y - r + 7, 2 * (r - 7), 2 * (r - 7));

    Path track; track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, a1, true);
    g.setColour (Colour (0xff2b3240)); g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path val; val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, a0, ang, true);
    g.setGradientFill (ColourGradient (accent(), c.x - r, c.y, accent2(), c.x + r, c.y, false));
    g.strokePath (val, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    Path tick; tick.addRoundedRectangle (-1.5f, -(r - 9.0f), 3.0f, r * 0.45f, 1.5f);
    g.setColour (s.isEnabled() ? Colours::white : kDim);
    g.fillPath (tick, AffineTransform::rotation (ang).translated (c.x, c.y));
}

void ExportDragSource::paint (Graphics& g)
{
    const bool ready = getFile != nullptr && getFile().existsAsFile();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ready ? RvrLookAndFeel::accent().withAlpha (0.25f) : Colour (0xff141922));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (ready ? RvrLookAndFeel::accent2() : Colour (0xff2b3240));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (ready ? kText : kDim); g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (ready ? "DRAG WAV TO YOUR DAW" : "EXPORT A WAV FIRST", getLocalBounds(), Justification::centred);
}

void ExportDragSource::mouseDrag (const MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6 || getFile == nullptr) return;
    const auto f = getFile();
    if (! f.existsAsFile()) return;
    if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
        c->performExternalDragDropOfFiles (StringArray { f.getFullPathName() }, false, this);
}

void RvrLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool hl, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (b.getToggleState() ? accent().withAlpha (0.85f) : Colour (0xff1c212b));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (hl ? accent2() : Colour (0xff2b3240));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (kText); g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred);
}

// ---------------------------------------------------------------------------------------------
ReverseVocalEditor::Knob& ReverseVocalEditor::addKnob (const char* id, const String& text, int, int)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::TextBoxBelow, false, 92, 16);
    k->slider.setRotaryParameters (MathConstants<float>::pi * 1.2f, MathConstants<float>::pi * 2.8f, true);
    k->slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k->label.setText (text, dontSendNotification);
    k->label.setJustificationType (Justification::centred);
    k->label.setFont (FontOptions (11.5f, Font::bold));
    k->label.setColour (Label::textColourId, kDim);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    knobs.push_back (std::move (k));
    return *knobs.back();
}

ReverseVocalEditor::Combo& ReverseVocalEditor::addCombo (const char* id, const String& text)
{
    auto c = std::make_unique<Combo>();
    if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id))) c->box.addItemList (p->choices, 1);
    c->label.setText (text, dontSendNotification);
    c->label.setFont (FontOptions (10.5f, Font::bold)); c->label.setColour (Label::textColourId, kDim);
    c->label.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (c->box); addAndMakeVisible (c->label);
    c->att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, id, c->box);
    combos.push_back (std::move (c));
    return *combos.back();
}

ReverseVocalEditor::ReverseVocalEditor (ReverseVocalProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    // fila 1: reverb / tono        fila 2: reverse         fila 3: disparo / salida
    // (el orden de creación define la colocación en resized())
    addKnob (PID::reverbTime, "REVERB TIME", 0, 0);   addKnob (PID::roomSize, "ROOM SIZE", 0, 1);
    addKnob (PID::diffusion, "DIFFUSION", 0, 2);      addKnob (PID::preDelay, "PRE-DELAY", 0, 3);
    addKnob (PID::lowCut, "LOW CUT", 0, 4);           addKnob (PID::highCut, "HIGH CUT", 0, 5);
    addKnob (PID::stereoWidth, "STEREO WIDTH", 0, 6);

    addKnob (PID::reverseAmount, "REVERSE AMOUNT", 1, 0); addKnob (PID::reverseLength, "REVERSE LENGTH", 1, 1);
    addKnob (PID::swellTime, "SWELL TIME", 1, 2);         addKnob (PID::attack, "ATTACK / FADE-IN", 1, 3);
    addKnob (PID::tail, "TAIL / FADE-OUT", 1, 4);         addKnob (PID::arrivalOffset, "ARRIVAL OFFSET", 1, 5);

    addKnob (PID::threshold, "TRIGGER THRESHOLD", 2, 0);  addKnob (PID::phraseGap, "PHRASE GAP", 2, 1);
    addKnob (PID::lockout, "LOCKOUT", 2, 2);              addKnob (PID::mix, "MIX", 2, 3);
    addKnob (PID::dryLevel, "DRY LEVEL", 2, 4);           addKnob (PID::wetLevel, "WET LEVEL", 2, 5);

    modeCombo = &addCombo (PID::mode, "MODE");
    repeatCombo = &addCombo (PID::autoRepeat, "AUTO CAPTURE");
    syncDivCombo = &addCombo (PID::syncDiv, "SYNC LENGTH");
    quantCombo = &addCombo (PID::quantize, "GRID ARRIVAL (MANUAL / MIDI)");

    addAndMakeVisible (syncBtn);   syncAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, PID::tempoSync, syncBtn);
    addAndMakeVisible (bypassBtn); bypassAtt = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, PID::bypass, bypassBtn);

    for (auto* b : { &captureBtn, &reverseBtn, &resetBtn }) { addAndMakeVisible (b); b->setColour (TextButton::buttonColourId, Colour (0xff1c212b)); }
    captureBtn.setColour (TextButton::buttonColourId, Colour (0xff6a2430));
    reverseBtn.setColour (TextButton::buttonColourId, Colour (0xff4a3aa8));
    captureBtn.onClick = [this] { proc.pulseTrigger (PID::capture); };
    reverseBtn.onClick = [this] { proc.pulseTrigger (PID::reverse); };
    resetBtn.onClick   = [this] { proc.pulseTrigger (PID::reset); };

    addAndMakeVisible (presetBox);
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };

    addAndMakeVisible (exportBtn); addAndMakeVisible (folderBtn); addAndMakeVisible (dragSource);
    exportBtn.setColour (TextButton::buttonColourId, Colour (0xff1f5c4a));
    exportBtn.onClick = [this]
    {
        File f;
        exportMessage.clear();
        proc.exportReversed (f, exportMessage);
        dragSource.repaint();
    };
    folderBtn.onClick = [this]
    {
        auto f = proc.getLastExport();
        if (f.existsAsFile()) f.revealToUser();
        else File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Reverse Vocal Reverb").revealToUser();
    };
    dragSource.getFile = [this] { return proc.getLastExport(); };

    setSize (1000, 730);      // al final: resized() necesita que todos los controles ya existan
    startTimerHz (30);
}

ReverseVocalEditor::~ReverseVocalEditor() { stopTimer(); setLookAndFeel (nullptr); }

void ReverseVocalEditor::resized()
{
    if (knobs.size() < 19 || combos.size() < 4 || modeCombo == nullptr || quantCombo == nullptr) return;   // aun en construccion
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (58);                                   // cabecera
    stateArea = area.removeFromTop (28).withSizeKeepingCentre (520, 28);

    // selectores superiores
    auto top = getLocalBounds().reduced (20).removeFromTop (50).removeFromRight (560);
    auto place = [&top] (Combo* c, int w)
    {
        auto r = top.removeFromLeft (w).reduced (4, 0);
        c->label.setBounds (r.removeFromTop (14)); c->box.setBounds (r.removeFromTop (26));
    };
    place (modeCombo, 150); place (repeatCombo, 150);
    presetBox.setBounds (top.reduced (4, 0).withTrimmedTop (14).withHeight (26));

    area.removeFromTop (8);
    const int cellW = area.getWidth() / 7, cellH = 128;
    const char* titles[] { "REVERB  /  TONE", "REVERSE  /  SWELL", "TRIGGER  /  OUTPUT" };
    groups.clear();
    const int perRow[] { 7, 6, 6 };
    int idx = 0;
    for (int row = 0; row < 3; ++row)
    {
        auto r = area.removeFromTop (cellH + 18);
        groups.push_back ({ r.removeFromTop (18), titles[row] });
        for (int col = 0; col < perRow[row]; ++col, ++idx)
        {
            auto cell = r.removeFromLeft (cellW);
            auto& k = *knobs[(size_t) idx];
            k.label.setBounds (cell.removeFromTop (16));
            k.slider.setBounds (cell);
        }
        if (row == 1)       // columna 7: sincronía con tempo
        {
            auto cell = r.removeFromLeft (cellW).reduced (8, 22);
            syncBtn.setBounds (cell.removeFromTop (28)); cell.removeFromTop (8);
            syncDivCombo->label.setBounds (cell.removeFromTop (14)); syncDivCombo->box.setBounds (cell.removeFromTop (26));
        }
        if (row == 2)       // columna 7: cuantización + bypass
        {
            auto cell = r.removeFromLeft (cellW).reduced (8, 14);
            quantCombo->label.setBounds (cell.removeFromTop (14).withWidth (cellW + 40)); quantCombo->box.setBounds (cell.removeFromTop (26));
            cell.removeFromTop (14); bypassBtn.setBounds (cell.removeFromTop (26));
        }
    }

    area.removeFromTop (6);
    auto bottom = area.removeFromTop (92);
    auto btns = bottom.removeFromLeft (3 * 150 + 20);
    captureBtn.setBounds (btns.removeFromLeft (150).reduced (4, 14).withTrimmedBottom (4));
    reverseBtn.setBounds (btns.removeFromLeft (150).reduced (4, 14).withTrimmedBottom (4));
    resetBtn.setBounds (btns.removeFromLeft (150).reduced (4, 14).withTrimmedBottom (4));
    envArea = bottom.reduced (10, 4);

    area.removeFromTop (8);
    auto ex = area.removeFromTop (40);
    exportBtn.setBounds (ex.removeFromLeft (150).reduced (4, 4));
    dragSource.setBounds (ex.removeFromLeft (230).reduced (4, 4));
    folderBtn.setBounds (ex.removeFromLeft (130).reduced (4, 4));
    exportMsgArea = ex.reduced (10, 0);
}

void ReverseVocalEditor::paint (Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 10.0f);

    g.setColour (kText); g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText ("REVERSE VOCAL REVERB", 28, 20, 420, 28, Justification::centredLeft);
    g.setColour (kDim); g.setFont (FontOptions (11.0f));
    g.drawText ("auto-capture  |  reverse tail  |  arrives on the voice", 28, 46, 420, 16, Justification::centredLeft);

    // indicador de estado
    const int st = (int) proc.getCore().state();
    const char* names[] { "WAITING", "CAPTURING", "PROCESSING", "READY" };
    const Colour cols[] { Colour (0xff8a93a3), Colour (0xffff4d5e), Colour (0xffffb84d), Colour (0xff3fe08f) };
    const int w = stateArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto r = Rectangle<int> (stateArea.getX() + i * w, stateArea.getY(), w, stateArea.getHeight()).reduced (4, 2);
        const bool on = i == st;
        g.setColour (on ? cols[i].withAlpha (0.18f) : Colour (0xff1a1f28)); g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (on ? cols[i] : Colour (0xff353d4b)); g.drawRoundedRectangle (r.toFloat(), 6.0f, 1.0f);
        g.fillEllipse ((float) r.getX() + 10.0f, (float) r.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont (FontOptions (12.0f, Font::bold));
        g.setColour (on ? cols[i] : Colour (0xff6c7585));
        g.drawText (names[i], r.withTrimmedLeft (26), Justification::centredLeft);
    }

    for (auto& gr : groups)
    {
        g.setColour (RvrLookAndFeel::accent2().withAlpha (0.8f)); g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (gr.second, gr.first.getX(), gr.first.getY(), 300, gr.first.getHeight(), Justification::centredLeft);
        g.setColour (Colour (0xff262d3a)); g.drawHorizontalLine (gr.first.getBottom() - 1, (float) gr.first.getX() + 170.0f, (float) gr.first.getRight());
    }

    // visualización del clip invertido
    g.setColour (Colour (0xff10131a)); g.fillRoundedRectangle (envArea.toFloat(), 6.0f);
    float env[rvr::ReversedClip::kDisplayPoints];
    proc.getCore().copyDisplayEnvelope (env);
    Path path; const float bx = (float) envArea.getX() + 6, bw = (float) envArea.getWidth() - 12, cy = (float) envArea.getCentreY(), hh = (float) envArea.getHeight() * 0.42f;
    path.startNewSubPath (bx, cy);
    for (int i = 0; i < rvr::ReversedClip::kDisplayPoints; ++i) path.lineTo (bx + bw * (float) i / (rvr::ReversedClip::kDisplayPoints - 1), cy - env[i] * hh);
    for (int i = rvr::ReversedClip::kDisplayPoints; i-- > 0;) path.lineTo (bx + bw * (float) i / (rvr::ReversedClip::kDisplayPoints - 1), cy + env[i] * hh);
    path.closeSubPath();
    g.setGradientFill (ColourGradient (RvrLookAndFeel::accent().withAlpha (0.2f), bx, cy, RvrLookAndFeel::accent2().withAlpha (0.95f), bx + bw, cy, false));
    g.fillPath (path);
    const float pk = proc.getCore().displayPeakFraction();
    if (pk > 0.0f)
    {
        g.setColour (Colours::white.withAlpha (0.8f)); g.drawVerticalLine ((int) (bx + bw * pk), (float) envArea.getY() + 4, (float) envArea.getBottom() - 4);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText ("ARRIVAL", (int) (bx + bw * pk) + 4, envArea.getY() + 4, 70, 12, Justification::left);
    }
    if (proc.getCore().state() == rvr::State::Waiting || proc.getCore().state() == rvr::State::Capturing)
    {
        g.setColour (kDim); g.setFont (FontOptions (12.0f));
        g.drawText (proc.getCore().state() == rvr::State::Capturing ? "capturing phrase..." : "waiting for the first vocal phrase", envArea, Justification::centred);
    }
    if (exportMessage.isNotEmpty())
    {
        g.setColour (kDim); g.setFont (FontOptions (11.5f));
        g.drawFittedText (exportMessage, exportMsgArea, Justification::centredLeft, 2);
    }
    // medidor de entrada
    const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.getCore().inputLevel(), -60.0f) + 60.0f) / 60.0f);
    g.setColour (Colour (0xff262d3a)); g.fillRect (envArea.getX(), envArea.getBottom() + 2, envArea.getWidth(), 3);
    g.setColour (RvrLookAndFeel::accent2()); g.fillRect (envArea.getX(), envArea.getBottom() + 2, (int) (envArea.getWidth() * lvl), 3);
}
