#include "MantaSynthEditor.h"

#include "../../Utf8.h"
#include "../../Branding.h" // 8.186：画面右上の名前は製品ごとに違う（Phase 225）

//==============================================================================

MantaSynthEditor::MantaSynthEditor (MantaSynthProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), "MantaSynth"),
      keyboard (processorToUse.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (toolbar);

    // Undo・A/B・プリセットで値が入れ替わったときの作り直し。
    // **つまみは繋いであるので勝手に追いつきます**——手当てが要るのは、
    // APVTSを見張っていない部分だけです（ここでは無し。描き直しだけ）
    toolbar.onStateRestored = [this] { repaint(); };

    // 8.173：**出来合いの音を156個**（Phase 213／`MantaSynthPresets.h`）。
    // シンセは「まず何か鳴らしてみたい」ものなので、
    // 保存済みが1つも無い状態で開くこと自体が壁になります
    {
        std::vector<MantaPluginToolbar::FactoryPreset> presets;

        for (const auto& preset : MantaSynthPresets::all())
            presets.push_back ({ preset.category, preset.name,
                                  [this, &preset]
                                  {
                                      MantaSynthPresets::apply (processor.getValueTreeState(), preset);
                                  } });

        toolbar.setFactoryPresets (std::move (presets));
    }

    // 8.186：**名前を直に書かないこと**（Phase 225／本人の指摘）。
    // Ember版でも「Manta Synthesizer」と出ていました。
    // `Branding.h`が唯一の出どころです（Manta Synthesizer／Red Panda）
    titleLabel.setText (Branding::synthPluginName, juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    titleLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (titleLabel);

    const auto purple = MantaTheme::accent();
    const auto orange = MantaTheme::curve();

    //--------------------------------------------------------------------------
    // OSC 1
    addToggle (toggleOsc1, MantaSynthParams::osc1On, purple);
    addCombo (osc1Wave, MantaSynthParams::osc1Wave, MantaSynthParams::getWaveNames());
    addKnob (osc1Voices, "UNISON", MantaSynthParams::osc1Voices, purple);
    addKnob (osc1Detune, "DETUNE", MantaSynthParams::osc1Detune, purple);
    addKnob (osc1Level,  "LEVEL",  MantaSynthParams::osc1Level,  purple);
    addKnob (osc1Spread, "SPREAD", MantaSynthParams::osc1Spread, purple);
    addKnob (glide,      "GLIDE",  MantaSynthParams::glide,      orange);

    osc1Voices->slider.setTooltip (utf8 ("同じ波を重ねる本数。増やすほど太くなります"));
    osc1Detune->slider.setTooltip (utf8 ("重ねた波の音程をずらす量"));
    glide->slider.setTooltip (utf8 ("直前の音程から滑らかに移る時間（0で切）"));

    //--------------------------------------------------------------------------
    // OSC 2
    addToggle (toggleOsc2, MantaSynthParams::osc2On, purple);
    addCombo (osc2Wave, MantaSynthParams::osc2Wave, MantaSynthParams::getWaveNames());
    addKnob (osc2Voices, "UNISON", MantaSynthParams::osc2Voices, purple);
    addKnob (osc2Detune, "DETUNE", MantaSynthParams::osc2Detune, purple);
    addKnob (osc2Level,  "LEVEL",  MantaSynthParams::osc2Level,  purple);
    addKnob (osc2Octave, "OCTAVE", MantaSynthParams::osc2Octave, purple, true);
    addKnob (osc2Spread, "SPREAD", MantaSynthParams::osc2Spread, purple);

    //--------------------------------------------------------------------------
    // SUB / NOISE
    addToggle (toggleSub, MantaSynthParams::subOn, purple);
    addCombo (subWave, MantaSynthParams::subWave, MantaSynthParams::getSubWaveNames());
    addCombo (subOctave, MantaSynthParams::subOctave, MantaSynthParams::getSubOctaveNames());
    addKnob (subLevel, "LEVEL", MantaSynthParams::subLevel, purple);

    subLevel->slider.setTooltip (utf8 ("1〜2オクターブ下を足して低音を支えます"));

    addToggle (toggleNoise, MantaSynthParams::noiseOn, orange);
    addCombo (noiseType, MantaSynthParams::noiseType, MantaSynthParams::getNoiseNames());
    addKnob (noiseLevel, "LEVEL", MantaSynthParams::noiseLevel, orange);

    //--------------------------------------------------------------------------
    // FILTER / DRIVE
    addToggle (toggleFilter, MantaSynthParams::filterOn, orange);
    addCombo (filterType, MantaSynthParams::filterType, MantaSynthParams::getFilterTypeNames());
    addKnob (cutoff,    "CUTOFF",  MantaSynthParams::cutoff,    orange);
    addKnob (resonance, "RESO",    MantaSynthParams::resonance, orange);
    addKnob (modEnvCut, "ENV AMT", MantaSynthParams::modEnvCut, orange);

    modEnvCut->slider.setTooltip (utf8 ("MOD ENVでカットオフを動かす量"));

    addToggle (toggleDrive, MantaSynthParams::driveOn, orange);
    addKnob (drive, "DRIVE", MantaSynthParams::drive, orange);

    //--------------------------------------------------------------------------
    // LFO 1 / LFO 2
    addToggle (toggleLfo, MantaSynthParams::lfoOn, orange);
    addCombo (lfoDest, MantaSynthParams::lfoDest, MantaSynthParams::getLfoDestinationNames());
    addKnob (lfoRate,  "RATE",  MantaSynthParams::lfoRate,  orange);
    addKnob (lfoDepth, "DEPTH", MantaSynthParams::lfoDepth, orange);

    addToggle (toggleLfo2, MantaSynthParams::lfo2On, orange);
    addCombo (lfo2Dest, MantaSynthParams::lfo2Dest, MantaSynthParams::getLfoDestinationNames());
    addKnob (lfo2Rate,  "RATE",  MantaSynthParams::lfo2Rate,  orange);
    addKnob (lfo2Depth, "DEPTH", MantaSynthParams::lfo2Depth, orange);

    //--------------------------------------------------------------------------
    // AMP ENV（**切れません**。音そのものなので、ON/OFFのボタンを置いていない）
    addKnob (ampA, "A", MantaSynthParams::ampA, purple);
    addKnob (ampD, "D", MantaSynthParams::ampD, purple);
    addKnob (ampS, "S", MantaSynthParams::ampS, purple);
    addKnob (ampR, "R", MantaSynthParams::ampR, purple);

    // MOD ENV
    addToggle (toggleModEnv, MantaSynthParams::modEnvOn, orange);
    addKnob (modA, "A", MantaSynthParams::modA, orange);
    addKnob (modD, "D", MantaSynthParams::modD, orange);
    addKnob (modS, "S", MantaSynthParams::modS, orange);
    addKnob (modR, "R", MantaSynthParams::modR, orange);

    //--------------------------------------------------------------------------
    // FX 1 / FX 2
    addToggle (toggleFx1, MantaSynthParams::fx1On, orange);
    addCombo (fx1Type, MantaSynthParams::fx1Type, MantaSynthParams::getFxTypeNames());
    addKnob (fx1Rate,  "RATE",  MantaSynthParams::fx1Rate,  orange);
    addKnob (fx1Depth, "DEPTH", MantaSynthParams::fx1Depth, orange);
    addKnob (fx1Fb,    "FB",    MantaSynthParams::fx1Fb,    orange);
    addKnob (fx1Mix,   "MIX",   MantaSynthParams::fx1Mix,   purple);

    addToggle (toggleFx2, MantaSynthParams::fx2On, orange);
    addCombo (fx2Type, MantaSynthParams::fx2Type, MantaSynthParams::getFxTypeNames());
    addKnob (fx2Rate,  "RATE",  MantaSynthParams::fx2Rate,  orange);
    addKnob (fx2Depth, "DEPTH", MantaSynthParams::fx2Depth, orange);
    addKnob (fx2Fb,    "FB",    MantaSynthParams::fx2Fb,    orange);
    addKnob (fx2Mix,   "MIX",   MantaSynthParams::fx2Mix,   purple);

    fx1Rate->slider.setTooltip (utf8 ("タイプによって意味が変わります（Delayでは繰り返しの速さ）"));

    //--------------------------------------------------------------------------
    // EQ / MASTER
    addToggle (toggleEq, MantaSynthParams::eqOn, purple);
    addKnob (eqLow,  "LOW",  MantaSynthParams::eqLow,  purple, true);
    addKnob (eqMid,  "MID",  MantaSynthParams::eqMid,  purple, true);
    addKnob (eqMidF, "FREQ", MantaSynthParams::eqMidF, orange);
    addKnob (eqHigh, "HIGH", MantaSynthParams::eqHigh, purple, true);

    addKnob (gain, "VOLUME", MantaSynthParams::gain, purple);

    //--------------------------------------------------------------------------
    // 見るだけのもの
    auto& state = processor.getValueTreeState();

    ampEnvDisplay = std::make_unique<SynthEnvelopeDisplay> (
        state, MantaSynthParams::ampA, MantaSynthParams::ampD,
        MantaSynthParams::ampS, MantaSynthParams::ampR, purple);

    modEnvDisplay = std::make_unique<SynthEnvelopeDisplay> (
        state, MantaSynthParams::modA, MantaSynthParams::modD,
        MantaSynthParams::modS, MantaSynthParams::modR, orange);

    osc1Waveform = std::make_unique<SynthWaveformDisplay> (state, MantaSynthParams::osc1Wave);
    osc2Waveform = std::make_unique<SynthWaveformDisplay> (state, MantaSynthParams::osc2Wave);

    meter = std::make_unique<SynthLevelMeter> (
        [this] (int channel) { return processor.getOutputLevel (channel); });

    scope = std::make_unique<SynthOscilloscope> (
        processor.getScopeBuffer(),
        [this] { return processor.getScopeWritePosition(); },
        MantaSynthProcessor::scopeSize);

    for (auto* component : { (juce::Component*) ampEnvDisplay.get(),
                              (juce::Component*) modEnvDisplay.get(),
                              (juce::Component*) osc1Waveform.get(),
                              (juce::Component*) osc2Waveform.get(),
                              (juce::Component*) meter.get(),
                              (juce::Component*) scope.get() })
        addAndMakeVisible (*component);

    //--------------------------------------------------------------------------
    // MOD MATRIX（4スロット）
    const auto sourceNames = MantaSynthParams::getMatrixSourceNames();
    const auto destinationNames = MantaSynthParams::getMatrixDestinationNames();

    for (int slot = 0; slot < MantaSynthParams::numMatrixSlots; ++slot)
    {
        addCombo (matrixSourceBox[(size_t) slot], MantaSynthParams::matrixSource (slot), sourceNames);
        addCombo (matrixDestinationBox[(size_t) slot],
                   MantaSynthParams::matrixDestination (slot), destinationNames);

        auto amount = std::make_unique<ValueEntrySlider> (
            juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);

        amount->setLookAndFeel (&knobLookAndFeel.get());

        // 量は**真ん中が0**。弧を12時から塗らないと、+50%と-50%が
        // まったく違う長さに見えます（`MantaKnobLookAndFeel.h`）
        amount->getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);
        amount->setColour (juce::Slider::rotarySliderFillColourId, orange);
        amount->setPopupDisplayEnabled (true, true, this);

        addAndMakeVisible (*amount);
        styledSliders.push_back (amount.get());

        sliderAttachments.add (new SliderAttachment (
            state, MantaSynthParams::matrixAmount (slot), *amount));

        matrixAmount[(size_t) slot] = std::move (amount);
    }

    //--------------------------------------------------------------------------
    // 鍵盤。**プラグインの中で音を出せること**に意味があります——
    // プリセットを選ぶたびにピアノロールへ音を書きに戻る、をしなくて済みます
    keyboard.setKeyWidth (24.0f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, AppColours::pianoWhiteKey);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, AppColours::pianoBlackKey);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, MantaTheme::border());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, MantaTheme::accent());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                         MantaTheme::accent().withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    //--------------------------------------------------------------------------
    // 8.172：**大きさは固定**（Manta EQ・Manta Compと同じ方針）
    setSize (fixedWidth, fixedHeight);
}

MantaSynthEditor::~MantaSynthEditor()
{
    // **被せたLookAndFeelは、必ず外してから壊すこと**（1.5と同じ決まり）
    for (auto* slider : styledSliders)
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void MantaSynthEditor::addKnob (std::unique_ptr<SynthKnob>& knob, const juce::String& name,
                                 const juce::String& parameterId, juce::Colour colour, bool bipolar)
{
    knob = std::make_unique<SynthKnob> (name);

    // `ValueEntrySlider`が被せている`MixerLookAndFeel`を上書きする
    // （本体のトラックヘッダーとは、読みやすい形が違うため。Manta EQと同じ）
    knob->slider.setLookAndFeel (&knobLookAndFeel.get());

    if (bipolar)
        knob->slider.getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);

    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);

    // **掴んでいるあいだだけ数値を出す**（`MantaSynthEditor.h`の説明）
    knob->slider.setPopupDisplayEnabled (true, true, this);

    addAndMakeVisible (*knob);
    styledSliders.push_back (&knob->slider);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  parameterId, knob->slider));
}

void MantaSynthEditor::addCombo (juce::ComboBox& box, const juce::String& parameterId,
                                  const juce::StringArray& items)
{
    box.clear (juce::dontSendNotification);

    for (int i = 0; i < items.size(); ++i)
        box.addItem (items[i], i + 1);

    box.setColour (juce::ComboBox::backgroundColourId, MantaTheme::graphBackground());
    box.setColour (juce::ComboBox::textColourId, MantaTheme::text());
    box.setColour (juce::ComboBox::outlineColourId, MantaTheme::border());
    box.setColour (juce::ComboBox::arrowColourId, MantaTheme::accent());
    addAndMakeVisible (box);

    comboAttachments.add (new ComboAttachment (processor.getValueTreeState(), parameterId, box));
}

void MantaSynthEditor::addToggle (std::unique_ptr<SynthSectionToggle>& toggle,
                                   const juce::String& parameterId, juce::Colour colour)
{
    toggle = std::make_unique<SynthSectionToggle> (colour);
    addAndMakeVisible (*toggle);

    buttonAttachments.add (new ButtonAttachment (processor.getValueTreeState(),
                                                  parameterId, *toggle));
}

//==============================================================================

void MantaSynthEditor::drawSection (juce::Graphics& g, juce::Rectangle<int> area,
                                     const juce::String& title) const
{
    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (area.toFloat(), 6.0f);

    g.setColour (MantaTheme::text());
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText (title, area.getX() + 22, area.getY() + 6, area.getWidth() - 30, 20,
                 juce::Justification::centredLeft);
}

void MantaSynthEditor::drawStaticBar (juce::Graphics& g, int x, int y) const
{
    g.setColour (MantaTheme::border());
    g.fillRoundedRectangle ((float) x + 10.25f, (float) y + 9.0f, 3.5f, 14.0f, 1.75f);
}

void MantaSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    // 1段目
    drawSection (g, { col0,  row1Y, colWidth,    sectionHeight }, "OSC 1");
    drawSection (g, { col1,  row1Y, filterWidth, sectionHeight }, "FILTER");
    drawSection (g, { driveX, row1Y, driveWidth, sectionHeight }, "DRIVE");
    drawSection (g, { lfoX,  row1Y, lfoWidth,    sectionHeight }, "LFO 1");
    drawSection (g, { lfo2X, row1Y, lfoWidth,    sectionHeight }, "LFO 2");

    // 2段目
    drawSection (g, { col0,  row2Y,  colWidth,  sectionHeight },     "OSC 2");
    drawSection (g, { col0,  row2bY, halfWidth, sectionHeight },     "SUB OSC");
    drawSection (g, { halfX, row2bY, halfWidth, sectionHeight },     "NOISE OSC");
    drawSection (g, { col1,  row2Y,  colWidth,  tallSectionHeight }, "AMP ENV");
    drawSection (g, { col2,  row2Y,  colWidth,  tallSectionHeight }, "MOD ENV");

    // 3段目
    drawSection (g, { col0,  row3Y,  halfWidth, sectionHeight }, "FX 1");
    drawSection (g, { halfX, row3Y,  halfWidth, sectionHeight }, "FX 2");
    drawSection (g, { col0,  row3bY, halfWidth, sectionHeight }, "EQ");
    drawSection (g, { halfX, row3bY, halfWidth, sectionHeight }, "MASTER");
    drawSection (g, { matrixX, row3Y, matrixWidth, tallSectionHeight }, "MOD MATRIX");
    drawSection (g, { scopeX, row3Y,  scopeWidth, sectionHeight }, "OSCILLOSCOPE");
    drawSection (g, { scopeX, row3bY, scopeWidth, sectionHeight }, "METER");

    // ON/OFFできないセクションにも、同じ位置に灰色の縦線を置いて並びをそろえる
    drawStaticBar (g, col1,    row2Y);    // AMP ENV
    drawStaticBar (g, halfX,   row3bY);   // MASTER
    drawStaticBar (g, matrixX, row3Y);    // MOD MATRIX
    drawStaticBar (g, scopeX,  row3Y);    // OSCILLOSCOPE
    drawStaticBar (g, scopeX,  row3bY);   // METER

    // MOD MATRIXの列見出し。**1列になったので1回だけ**（EAGLE type0は2列ぶん描いていた）
    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("SOURCE",      matrixSourceX, row3Y + 28, matrixBoxWidth, 14, juce::Justification::centredLeft);
    g.drawText ("DESTINATION", matrixDestX,   row3Y + 28, matrixBoxWidth, 14, juce::Justification::centredLeft);

    // AMOUNTだけ**つまみの真上で中央**（選択欄は左揃えだが、つまみは丸なので）
    g.drawText ("AMOUNT", matrixAmountX - 12, row3Y + 28, matrixAmountSize + 24, 14,
                 juce::Justification::centred);
}

//==============================================================================

void MantaSynthEditor::resized()
{
    if (gain == nullptr) return;   // 組み立ての途中で来た`resized()`は無視する

    toolbar.setBounds (0, 0, getWidth(), MantaPluginToolbar::preferredHeight);
    titleLabel.setBounds (toolbar.getTrailingArea().withTrimmedRight (8));

    auto placeToggle = [] (SynthSectionToggle* toggle, int x, int y)
    {
        if (toggle != nullptr) toggle->setBounds (x + 6, y + 7, 12, 18);
    };

    auto placeKnob = [] (SynthKnob* knob, int x, int y)
    {
        if (knob != nullptr) knob->setBounds (x, y, knobWidth, knobHeight);
    };

    // Phase 214：**余ったぶんを、つまみのあいだに配る**（本人の要望）。
    //
    // Phase 213までは、つまみを隙間なく並べてから塊ごと中央へ寄せていました。
    // FILTER・LFO・ENVは**パネルのほうがずっと広い**ので、
    // 中央にひとかたまりだけ浮いて、左右に大きな空きが残っていました。
    //
    // 余りを`count + 1`で割っているので、**端にも同じだけ残ります**——
    // 端まで詰めて並べると、つまみが箱に貼り付いて見えます
    auto placeKnobRow = [&placeKnob] (int x, int width, int y,
                                       std::initializer_list<SynthKnob*> knobs)
    {
        const int count = (int) knobs.size();

        if (count <= 0)
            return;

        const int gap = juce::jmax (0, (width - count * knobWidth) / (count + 1));
        const int used = count * knobWidth + (count - 1) * gap;

        int cursor = x + (width - used) / 2;

        for (auto* knob : knobs)
        {
            placeKnob (knob, cursor, y);
            cursor += knobWidth + gap;
        }
    };

    //--------------------------------------------------------------------------
    // 1段目
    placeToggle (toggleOsc1.get(), col0, row1Y);
    osc1Wave.setBounds (col0 + colWidth - 100, row1Y + 6, 90, 20);
    {
        int x = col0 + 10, y = row1Y + knobTop;
        placeKnob (osc1Voices.get(), x, y); x += knobWidth;
        placeKnob (osc1Detune.get(), x, y); x += knobWidth;
        placeKnob (osc1Level.get(),  x, y); x += knobWidth;
        placeKnob (osc1Spread.get(), x, y); x += knobWidth;
        placeKnob (glide.get(),      x, y);
    }
    osc1Waveform->setBounds (col0 + colWidth - 160, row1Y + knobTop, 150, knobHeight);

    placeToggle (toggleFilter.get(), col1, row1Y);
    filterType.setBounds (col1 + filterWidth - 88, row1Y + 6, 78, 20);
    placeKnobRow (col1, filterWidth, row1Y + knobTop,
                   { cutoff.get(), resonance.get(), modEnvCut.get() });

    placeToggle (toggleDrive.get(), driveX, row1Y);
    placeKnob (drive.get(), driveX + (driveWidth - knobWidth) / 2, row1Y + knobTop);

    placeToggle (toggleLfo.get(), lfoX, row1Y);
    lfoDest.setBounds (lfoX + lfoWidth - 84, row1Y + 6, 74, 20);
    placeKnobRow (lfoX, lfoWidth, row1Y + knobTop, { lfoRate.get(), lfoDepth.get() });

    placeToggle (toggleLfo2.get(), lfo2X, row1Y);
    lfo2Dest.setBounds (lfo2X + lfoWidth - 84, row1Y + 6, 74, 20);
    placeKnobRow (lfo2X, lfoWidth, row1Y + knobTop, { lfo2Rate.get(), lfo2Depth.get() });

    //--------------------------------------------------------------------------
    // 2段目
    placeToggle (toggleOsc2.get(), col0, row2Y);
    osc2Wave.setBounds (col0 + colWidth - 100, row2Y + 6, 90, 20);
    {
        int x = col0 + 10, y = row2Y + knobTop;
        placeKnob (osc2Voices.get(), x, y); x += knobWidth;
        placeKnob (osc2Detune.get(), x, y); x += knobWidth;
        placeKnob (osc2Level.get(),  x, y); x += knobWidth;
        placeKnob (osc2Octave.get(), x, y); x += knobWidth;
        placeKnob (osc2Spread.get(), x, y);
    }
    osc2Waveform->setBounds (col0 + colWidth - 160, row2Y + knobTop, 150, knobHeight);

    placeToggle (toggleSub.get(), col0, row2bY);
    placeKnob (subLevel.get(), col0 + 10, row2bY + knobTop);
    subWave.setBounds  (col0 + 62, row2bY + 40, 120, 20);
    subOctave.setBounds (col0 + 62, row2bY + 66, 120, 20);

    placeToggle (toggleNoise.get(), halfX, row2bY);
    placeKnob (noiseLevel.get(), halfX + 10, row2bY + knobTop);
    noiseType.setBounds (halfX + 62, row2bY + 53, 120, 20);

    ampEnvDisplay->setBounds (col1 + 10, row2Y + 32, colWidth - 20, 110);
    placeKnobRow (col1, colWidth, row2Y + 152,
                   { ampA.get(), ampD.get(), ampS.get(), ampR.get() });

    placeToggle (toggleModEnv.get(), col2, row2Y);
    modEnvDisplay->setBounds (col2 + 10, row2Y + 32, colWidth - 20, 110);
    placeKnobRow (col2, colWidth, row2Y + 152,
                   { modA.get(), modD.get(), modS.get(), modR.get() });

    //--------------------------------------------------------------------------
    // 3段目
    placeToggle (toggleFx1.get(), col0, row3Y);
    fx1Type.setBounds (col0 + halfWidth - 88, row3Y + 6, 78, 20);
    {
        int x = col0 + 8, y = row3Y + knobTop;
        placeKnob (fx1Rate.get(),  x, y); x += knobWidth;
        placeKnob (fx1Depth.get(), x, y); x += knobWidth;
        placeKnob (fx1Fb.get(),    x, y); x += knobWidth;
        placeKnob (fx1Mix.get(),   x, y);
    }

    placeToggle (toggleFx2.get(), halfX, row3Y);
    fx2Type.setBounds (halfX + halfWidth - 88, row3Y + 6, 78, 20);
    {
        int x = halfX + 8, y = row3Y + knobTop;
        placeKnob (fx2Rate.get(),  x, y); x += knobWidth;
        placeKnob (fx2Depth.get(), x, y); x += knobWidth;
        placeKnob (fx2Fb.get(),    x, y); x += knobWidth;
        placeKnob (fx2Mix.get(),   x, y);
    }

    placeToggle (toggleEq.get(), col0, row3bY);
    {
        int x = col0 + 8, y = row3bY + knobTop;
        placeKnob (eqLow.get(),  x, y); x += knobWidth;
        placeKnob (eqMid.get(),  x, y); x += knobWidth;
        placeKnob (eqMidF.get(), x, y); x += knobWidth;
        placeKnob (eqHigh.get(), x, y);
    }

    placeKnob (gain.get(), halfX + (halfWidth - knobWidth) / 2, row3bY + knobTop);

    //--------------------------------------------------------------------------
    // MOD MATRIX（4スロットを縦に1列）
    for (int slot = 0; slot < MantaSynthParams::numMatrixSlots; ++slot)
    {
        const int y = row3Y + 46 + slot * matrixRowHeight;

        matrixSourceBox[(size_t) slot].setBounds (matrixSourceX, y + 10, matrixBoxWidth, 22);
        matrixDestinationBox[(size_t) slot].setBounds (matrixDestX, y + 10, matrixBoxWidth, 22);
        matrixAmount[(size_t) slot]->setBounds (matrixAmountX, y, matrixAmountSize, matrixAmountSize);
    }

    scope->setBounds (scopeX + 10, row3Y + 30, scopeWidth - 20, sectionHeight - 40);
    meter->setBounds (scopeX + 10, row3bY + 30, scopeWidth - 20, sectionHeight - 40);

    keyboard.setBounds (margin, keyboardY, getWidth() - margin * 2, keyboardHeight);
}
