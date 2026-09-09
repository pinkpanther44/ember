#include "MantaEQEditor.h"

#include "../MantaTheme.h"
#include "../../AppSettings.h"
#include "../../NameEntry.h"
#include "../../Utf8.h"

#include <cmath>

namespace
{
    /** アナライザーの帯に出す選択肢。**表示と値をここ1箇所で対にしている**（1.27）。 */
    const float speedValues[] { 0.15f, 0.5f, 0.9f };
    const char* const speedNames[] { "Slow", "Normal", "Fast" };

    const float tiltValues[] { 0.0f, 3.0f, 4.5f, 6.0f };
    const char* const tiltNames[] { "0 dB/oct", "3 dB/oct", "4.5 dB/oct", "6 dB/oct" };

    const float floorValues[] { -60.0f, -90.0f, -120.0f };
    const char* const floorNames[] { "-60 dB", "-90 dB", "-120 dB" };

    template <typename Type, size_t Size>
    int findClosestIndex (const Type (&values)[Size], float target)
    {
        int best = 0;
        float bestDistance = std::abs ((float) values[0] - target);

        for (size_t i = 1; i < Size; ++i)
        {
            const float distance = std::abs ((float) values[i] - target);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = (int) i;
            }
        }

        return best;
    }
}

//==============================================================================

MantaEQEditor::OutputMeter::OutputMeter (MantaEQProcessor& processorToUse)
    : processor (processorToUse)
{
    startTimerHz (30);
}

MantaEQEditor::OutputMeter::~OutputMeter()
{
    stopTimer();
}

void MantaEQEditor::OutputMeter::timerCallback()
{
    bool changed = false;

    for (int channel = 0; channel < 2; ++channel)
    {
        const float level = processor.getOutputLevelDb (channel);

        // **上がるのは即座、落ちるのはゆっくり**（アナライザーと同じ考え）
        const float next = level > displayedDb[channel] ? level
                                                         : juce::jmax (level, displayedDb[channel] - 1.5f);

        if (std::abs (next - displayedDb[channel]) > 0.05f)
        {
            displayedDb[channel] = next;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void MantaEQEditor::OutputMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (MantaTheme::graphBackground());
    g.fillRect (area);

    const float barWidth = area.getWidth() * 0.5f - 1.0f;

    for (int channel = 0; channel < 2; ++channel)
    {
        // -60dBを下端、0dBを上端にする
        const float proportion = juce::jlimit (0.0f, 1.0f, (displayedDb[channel] + 60.0f) / 60.0f);
        const float height = area.getHeight() * proportion;

        const juce::Rectangle<float> bar (area.getX() + (float) channel * (barWidth + 2.0f),
                                           area.getBottom() - height, barWidth, height);

        // 0dBを超えたら**オレンジ**（本体の録音・警告と同じ役割の色）
        g.setColour (displayedDb[channel] > -0.1f ? AppColours::orange : MantaTheme::accent());
        g.fillRect (bar);
    }

    g.setColour (MantaTheme::border());
    g.drawRect (area, 1.0f);
}

//==============================================================================

MantaEQEditor::MantaEQEditor (MantaEQProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      curve (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), "MantaEQ")
{
    addAndMakeVisible (curve);
    addAndMakeVisible (toolbar);

    curve.onBandSelected = [this] (int) { rebuildBandAttachments(); };
    curve.onBandsChanged = [this] { rebuildBandAttachments(); };

    // Undo・A/B・プリセットで値が入れ替わったら、**画面を作り直すのはこちらの仕事**
    toolbar.onStateRestored = [this]
    {
        curve.setSelectedBand (MantaEQUiState::getInt (processor.getUiState(),
                                                        MantaEQUiState::selectedBand, -1));
        rebuildBandAttachments();
        refreshAnalyserButtons();
        refreshLatencyLabel();
    };

    //--------------------------------------------------------------------------
    // 仕様書4.7：処理モード。**ツールバーの子として足す**（空いている場所へ置くため）
    setupCombo (modeBox, MantaEQParams::getProcessingModeNames());
    setupCombo (resolutionBox, MantaEQParams::getResolutionNames());

    for (auto* box : { &modeBox, &resolutionBox })
        toolbar.addAndMakeVisible (*box);

    modeBox.setTooltip (utf8 ("Linear Phaseは位相をずらさない代わりに遅れます"));
    resolutionBox.setTooltip (utf8 ("長くするほど低いところまで正確になり、そのぶん遅れます"));

    latencyLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    latencyLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    latencyLabel.setJustificationType (juce::Justification::centredLeft);
    toolbar.addAndMakeVisible (latencyLabel);

    //--------------------------------------------------------------------------
    // アナライザーの帯（仕様書4.8・4.14）
    setupToolbarButton (keyboardButton, "Keys");
    setupToolbarButton (preButton, "Pre");
    setupToolbarButton (postButton, "Post");
    setupToolbarButton (freezeButton, "Freeze");

    for (auto* button : { &keyboardButton, &preButton, &postButton, &freezeButton })
        button->setClickingTogglesState (true);

    keyboardButton.onClick = [this]
    {
        writeUiProperty (MantaEQUiState::showKeyboard, keyboardButton.getToggleState());
        curve.repaint();
    };

    auto writeAnalyserMode = [this]
    {
        const int mode = (preButton.getToggleState() ? 1 : 0) | (postButton.getToggleState() ? 2 : 0);
        writeUiProperty (MantaEQUiState::analyserMode, mode);
    };

    preButton.onClick = writeAnalyserMode;
    postButton.onClick = writeAnalyserMode;

    freezeButton.onClick = [this]
    {
        writeUiProperty (MantaEQUiState::analyserFrozen, freezeButton.getToggleState());
    };

    freezeButton.setTooltip (utf8 ("いまの波形をそのまま止めて見る"));

    setupCombo (speedBox, juce::StringArray (speedNames, juce::numElementsInArray (speedNames)));
    setupCombo (tiltBox, juce::StringArray (tiltNames, juce::numElementsInArray (tiltNames)));
    setupCombo (floorBox, juce::StringArray (floorNames, juce::numElementsInArray (floorNames)));

    speedBox.onChange = [this]
    {
        writeUiProperty (MantaEQUiState::analyserSpeed, speedValues[speedBox.getSelectedId() - 1]);
    };

    tiltBox.onChange = [this]
    {
        writeUiProperty (MantaEQUiState::analyserTilt, tiltValues[tiltBox.getSelectedId() - 1]);
    };

    floorBox.onChange = [this]
    {
        writeUiProperty (MantaEQUiState::analyserFloorDb, floorValues[floorBox.getSelectedId() - 1]);
    };

    tiltBox.setTooltip (utf8 ("低いほうが大きく出るぶんを、傾けて見やすくする"));

    analyserCaption.setText ("Analyzer", juce::dontSendNotification);
    analyserCaption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    analyserCaption.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (analyserCaption);

    //--------------------------------------------------------------------------
    // バンドのつまみ
    bandTitle.setColour (juce::Label::textColourId, MantaTheme::text());
    bandTitle.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    addAndMakeVisible (bandTitle);

    emptyHint.setText (utf8 ("カーブの上をダブルクリックすると、バンドができます"),
                        juce::dontSendNotification);
    emptyHint.setColour (juce::Label::textColourId, MantaTheme::textDim());
    emptyHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    emptyHint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyHint);

    setupToolbarButton (bandOnButton, "On");
    setupToolbarButton (bandSoloButton, "Solo");
    setupToolbarButton (bandDynButton, "Dyn");

    bandOnButton.setClickingTogglesState (true);
    bandDynButton.setClickingTogglesState (true);
    bandSoloButton.setClickingTogglesState (true);

    bandSoloButton.onClick = [this]
    {
        processor.setSoloedBand (bandSoloButton.getToggleState() ? curve.getSelectedBand() : -1);
    };

    bandSoloButton.setTooltip (utf8 ("このバンドが触っている範囲だけを聴く"));
    bandDynButton.setTooltip (utf8 ("入力の大きさに合わせてGainを動かす"));

    // 真ん中が0のもの（Gain・Dyn Range）は、弧を12時から伸ばす
    setupKnob (freqSlider, freqCaption, "Freq");
    setupKnob (gainSlider, gainCaption, "Gain", true);
    setupKnob (qSlider, qCaption, "Q");
    setupKnob (thresholdSlider, thresholdCaption, "Thr");
    setupKnob (rangeSlider, rangeCaption, "Range", true);
    setupKnob (attackSlider, attackCaption, "Atk");
    setupKnob (releaseSlider, releaseCaption, "Rel");

    setupCombo (shapeBox, MantaEQParams::getShapeNames());
    setupCombo (slopeBox, MantaEQParams::getSlopeNames());
    setupCombo (channelBox, MantaEQParams::getChannelNames());

    auto setupComboCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, MantaTheme::textDim());
        label.setFont (juce::Font (juce::FontOptions (10.0f)));
        addAndMakeVisible (label);
    };

    setupComboCaption (shapeCaption, "Shape");
    setupComboCaption (slopeCaption, "Slope");
    setupComboCaption (channelCaption, "Channel");

    //--------------------------------------------------------------------------
    // 出力セクション（仕様書4.16）
    outputTitle.setText ("Output", juce::dontSendNotification);
    outputTitle.setColour (juce::Label::textColourId, MantaTheme::text());
    outputTitle.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    addAndMakeVisible (outputTitle);

    // Phase 206：**出力のつまみはパープルで統一**（本人の指定）。
    // バンドの色（配置で変わる）と混ざらないよう、ここだけは固定です
    setupKnob (outGainSlider, outGainCaption, "Gain", true);
    setupKnob (outPanSlider, outPanCaption, "Pan", true);
    setupKnob (outMsSlider, outMsCaption, "M/S", true);

    for (auto* slider : { &outGainSlider, &outPanSlider, &outMsSlider })
        slider->setColour (juce::Slider::rotarySliderFillColourId, MantaTheme::accent());

    setupToolbarButton (phaseButton, "Phase");
    setupToolbarButton (autoGainButton, "Auto");
    phaseButton.setClickingTogglesState (true);
    autoGainButton.setClickingTogglesState (true);

    phaseButton.setTooltip (utf8 ("出力の位相を反転する"));
    autoGainButton.setTooltip (utf8 ("上げ下げしたぶんを打ち消して、音量をそろえる"));

    addAndMakeVisible (meter);

    auto& state = processor.getValueTreeState();

    outGainAttachment = std::make_unique<SliderAttachment> (state, MantaEQParams::outputGain, outGainSlider);
    outPanAttachment  = std::make_unique<SliderAttachment> (state, MantaEQParams::outputPan, outPanSlider);
    outMsAttachment   = std::make_unique<SliderAttachment> (state, MantaEQParams::outputMsBalance, outMsSlider);
    phaseAttachment   = std::make_unique<ButtonAttachment> (state, MantaEQParams::outputPhaseInvert, phaseButton);
    autoGainAttachment = std::make_unique<ButtonAttachment> (state, MantaEQParams::outputAutoGain, autoGainButton);

    modeAttachment       = std::make_unique<ComboAttachment> (state, MantaEQParams::processingMode, modeBox);
    resolutionAttachment = std::make_unique<ComboAttachment> (state, MantaEQParams::processingResolution, resolutionBox);

    //--------------------------------------------------------------------------
    const auto uiState = processor.getUiState();

    curve.setSelectedBand (MantaEQUiState::getInt (uiState, MantaEQUiState::selectedBand, -1));
    rebuildBandAttachments();
    refreshAnalyserButtons();
    refreshLatencyLabel();


    // 8.172：**大きさは固定**（Phase 212／本人の要望）。
    //
    // 伸縮をやめたのは、置き場所が増えないからです——広げても帯の中の
    // つまみは大きくならず、**あいだの隙間だけが伸びます**。
    // 大きさは**これまでの下限**（伸縮していたころ、いちばん詰まった状態で
    // 全部が収まると確かめてある寸法）をそのまま使っています。
    setSize (fixedWidth, fixedHeight);

    // 5回／秒。Undoの粒度（このファイルの冒頭）と、帯のボタンの追従を兼ねている
    startTimerHz (5);
}

MantaEQEditor::~MantaEQEditor()
{
    stopTimer();

    // **被せたLookAndFeelは、必ず外してから壊すこと**（1.5と同じ決まり）。
    // `SharedResourcePointer`が最後に壊れるよう宣言順も見てありますが、
    // ここで外しておけば、並びを触っても壊れません
    for (auto* slider : { &freqSlider, &gainSlider, &qSlider, &thresholdSlider, &rangeSlider,
                          &attackSlider, &releaseSlider, &outGainSlider, &outPanSlider, &outMsSlider })
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void MantaEQEditor::setupToolbarButton (juce::TextButton& button, const juce::String& text)
{
    MantaPluginToolbar::styleButton (button, text);
    addAndMakeVisible (button);
}

void MantaEQEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                                bool bipolar)
{
    // Phase 206：**弧の付いたつまみへ**（本人の要望。`MantaKnobLookAndFeel.h`）。
    // `ValueEntrySlider`が被せている`MixerLookAndFeel`を、ここで上書きしています
    // ——本体のトラックヘッダー（16px角）とは、読みやすい形が違うためです
    slider.setLookAndFeel (&knobLookAndFeel.get());

    // 真ん中が0のつまみは、弧を12時から伸ばす
    if (bipolar)
        slider.getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, MantaTheme::accent());
    slider.setColour (juce::Slider::textBoxTextColourId, MantaTheme::text());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);
}

void MantaEQEditor::setupCombo (juce::ComboBox& box, const juce::StringArray& items)
{
    box.clear (juce::dontSendNotification);

    for (int i = 0; i < items.size(); ++i)
        box.addItem (items[i], i + 1);

    box.setColour (juce::ComboBox::backgroundColourId, MantaTheme::panelBackground());
    box.setColour (juce::ComboBox::textColourId, MantaTheme::text());
    box.setColour (juce::ComboBox::outlineColourId, MantaTheme::border());
    addAndMakeVisible (box);
}

//==============================================================================

void MantaEQEditor::rebuildBandAttachments()
{
    const int band = curve.getSelectedBand();

    if (band != attachedBand)
    {
        attachedBand = band;

        // **繋ぎ替えは、いったん外してから。** 同じつまみに2本ぶら下がると、
        // 片方が古いバンドへ書き戻します
        freqAttachment.reset();
        gainAttachment.reset();
        qAttachment.reset();
        thresholdAttachment.reset();
        rangeAttachment.reset();
        attackAttachment.reset();
        releaseAttachment.reset();
        shapeAttachment.reset();
        slopeAttachment.reset();
        channelAttachment.reset();
        bandOnAttachment.reset();
        bandDynAttachment.reset();

        if (juce::isPositiveAndBelow (band, MantaEQParams::numBands))
        {
            auto& state = processor.getValueTreeState();
            auto id = [band] (const char* suffix) { return MantaEQParams::bandParamId (band, suffix); };

            freqAttachment      = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandFreq), freqSlider);
            gainAttachment      = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandGain), gainSlider);
            qAttachment         = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandQ), qSlider);
            thresholdAttachment = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandDynThresh), thresholdSlider);
            rangeAttachment     = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandDynRange), rangeSlider);
            attackAttachment    = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandDynAttack), attackSlider);
            releaseAttachment   = std::make_unique<SliderAttachment> (state, id (MantaEQParams::bandDynRelease), releaseSlider);
            shapeAttachment     = std::make_unique<ComboAttachment> (state, id (MantaEQParams::bandShape), shapeBox);
            slopeAttachment     = std::make_unique<ComboAttachment> (state, id (MantaEQParams::bandSlope), slopeBox);
            channelAttachment   = std::make_unique<ComboAttachment> (state, id (MantaEQParams::bandChannel), channelBox);
            // Phase 206：**「On」はバイパスの入切**（`bandActive`）。
            // `bandEnabled`（＝バンドがあるかどうか）に繋いでいたPhase 205までは、
            // 押すとバンドが消えて、押し直せませんでした
            bandOnAttachment    = std::make_unique<ButtonAttachment> (state, id (MantaEQParams::bandActive), bandOnButton);
            bandDynAttachment   = std::make_unique<ButtonAttachment> (state, id (MantaEQParams::bandDynOn), bandDynButton);
        }
    }

    updateControlVisibility();
}

void MantaEQEditor::updateControlVisibility()
{
    const int band = curve.getSelectedBand();
    const bool hasBand = juce::isPositiveAndBelow (band, MantaEQParams::numBands);

    emptyHint.setVisible (! hasBand);
    bandTitle.setVisible (hasBand);

    for (auto* component : std::initializer_list<juce::Component*> {
             &bandOnButton, &bandSoloButton, &bandDynButton,
             &freqSlider, &gainSlider, &qSlider,
             &thresholdSlider, &rangeSlider, &attackSlider, &releaseSlider,
             &freqCaption, &gainCaption, &qCaption,
             &thresholdCaption, &rangeCaption, &attackCaption, &releaseCaption,
             &shapeBox, &slopeBox, &channelBox,
             &shapeCaption, &slopeCaption, &channelCaption })
    {
        component->setVisible (hasBand);
    }

    if (! hasBand)
        return;

    const auto settings = processor.getBandSettings (band);

    bandTitle.setText ("Band " + juce::String (band + 1), juce::dontSendNotification);
    bandSoloButton.setToggleState (processor.getSoloedBand() == band, juce::dontSendNotification);

    // Phase 206：**つまみもバンドの色**（＝ステレオ配置の色）にする。
    // グラフのつまみと下のつまみが同じ色なら、どのバンドを触っているかが
    // 番号を読まなくても分かります
    const auto colour = MantaTheme::bandColour ((int) settings.channel);

    // **使わないつまみは触れなくする。** 消すと並びが崩れて、
    // 形状を変えるたびにつまみの位置が動きます
    const bool usesGain = MantaEQParams::shapeUsesGain (settings.shape);
    const bool usesSlope = MantaEQParams::shapeUsesSlope (settings.shape);

    setKnobEnabled (freqSlider, freqCaption, true, colour);
    setKnobEnabled (qSlider, qCaption, true, colour);
    setKnobEnabled (gainSlider, gainCaption, usesGain, colour);

    slopeBox.setEnabled (usesSlope);
    slopeCaption.setEnabled (usesSlope);

    setKnobEnabled (thresholdSlider, thresholdCaption, settings.dynamicEnabled, colour);
    setKnobEnabled (rangeSlider, rangeCaption, settings.dynamicEnabled, colour);
    setKnobEnabled (attackSlider, attackCaption, settings.dynamicEnabled, colour);
    setKnobEnabled (releaseSlider, releaseCaption, settings.dynamicEnabled, colour);
}

void MantaEQEditor::setKnobEnabled (ValueEntrySlider& slider, juce::Label& caption,
                                     bool shouldBeEnabled, juce::Colour colour)
{
    // **`MantaKnobLookAndFeel`は弧も指針も`rotarySliderFillColourId`で描きます。**
    // `setEnabled(false)`だけでは色が変わらないので、**触れないのに触れそうに見えます**
    // （文字だけ薄くなって、つまみは元気なまま）
    const auto wantedFill = shouldBeEnabled ? colour : MantaTheme::textDim().withAlpha (0.35f);
    const auto wantedText = shouldBeEnabled ? MantaTheme::text() : MantaTheme::textDim().withAlpha (0.5f);

    // **変わっていないときは触らない。** 5回／秒で呼ばれるので、
    // 毎回`repaint()`すると、つまみだけが常に描き直されます
    if (slider.isEnabled() == shouldBeEnabled
         && slider.findColour (juce::Slider::rotarySliderFillColourId) == wantedFill)
        return;

    slider.setEnabled (shouldBeEnabled);
    caption.setEnabled (shouldBeEnabled);

    slider.setColour (juce::Slider::rotarySliderFillColourId, wantedFill);
    slider.setColour (juce::Slider::textBoxTextColourId, wantedText);
    slider.repaint();
}

//==============================================================================

void MantaEQEditor::refreshAnalyserButtons()
{
    const auto uiState = processor.getUiState();

    const int mode = MantaEQUiState::getInt (uiState, MantaEQUiState::analyserMode, 3);

    preButton.setToggleState ((mode & 1) != 0, juce::dontSendNotification);
    postButton.setToggleState ((mode & 2) != 0, juce::dontSendNotification);

    freezeButton.setToggleState (MantaEQUiState::getBool (uiState, MantaEQUiState::analyserFrozen, false),
                                  juce::dontSendNotification);
    keyboardButton.setToggleState (MantaEQUiState::getBool (uiState, MantaEQUiState::showKeyboard, false),
                                    juce::dontSendNotification);

    speedBox.setSelectedId (findClosestIndex (speedValues,
                                               MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserSpeed, 0.5f)) + 1,
                             juce::dontSendNotification);
    tiltBox.setSelectedId (findClosestIndex (tiltValues,
                                              MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserTilt, 4.5f)) + 1,
                            juce::dontSendNotification);
    floorBox.setSelectedId (findClosestIndex (floorValues,
                                               MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserFloorDb, -90.0f)) + 1,
                             juce::dontSendNotification);

}

void MantaEQEditor::writeUiProperty (const juce::Identifier& id, const juce::var& value)
{
    processor.getUiState().setProperty (id, value, nullptr);
}

//==============================================================================








//==============================================================================





//==============================================================================

void MantaEQEditor::refreshLatencyLabel()
{
    const auto output = processor.getOutputSettings();
    const bool linearPhase = output.mode == MantaEQParams::ProcessingMode::linearPhase;

    resolutionBox.setEnabled (linearPhase);

    if (! linearPhase)
    {
        latencyLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
        latencyLabel.setText ("0.0 ms", juce::dontSendNotification);
        return;
    }

    const double sampleRate = processor.getSampleRateForDisplay();
    const int latency = processor.getReportedLatencySamples();
    const double milliseconds = sampleRate > 0.0 ? latency * 1000.0 / sampleRate : 0.0;

    juce::String text;
    text << juce::String (milliseconds, 1) << " ms";

    // **止まっていることを黙っていない。** Linear Phaseではダイナミクスが
    // 効かないので、オンのままのバンドがあるなら、そう出す（`MantaEQProcessor.h`）
    if (processor.hasAnyDynamicBand())
    {
        text << utf8 ("（Dyn は Zero Latency のみ）");
        latencyLabel.setColour (juce::Label::textColourId, AppColours::orange);
    }
    else
    {
        latencyLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    }

    latencyLabel.setText (text, juce::dontSendNotification);
}

void MantaEQEditor::timerCallback()
{
    refreshAnalyserButtons();
    refreshLatencyLabel();
    updateControlVisibility();
}

//==============================================================================

void MantaEQEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    auto area = getLocalBounds();

    g.setColour (MantaTheme::panelBackground());
    g.fillRect (area.removeFromTop (toolbarHeight));

    area.removeFromTop (curve.getHeight());

    g.setColour (MantaTheme::panelBackground());
    g.fillRect (area.removeFromTop (analyserBarHeight));

    g.setColour (MantaTheme::border());
    g.drawHorizontalLine (toolbarHeight - 1, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - bottomStripHeight, 0.0f, (float) getWidth());
}

void MantaEQEditor::resized()
{
    auto area = getLocalBounds();

    //--------------------------------------------------------------------------
    toolbar.setBounds (area.removeFromTop (toolbarHeight));

    // 仕様書4.7：処理モードは**ツールバーの右端**（遅れの数字が常に見えるように）。
    // **`toolbar`の座標**で置くこと（子として足してある）
    auto trailing = toolbar.getTrailingArea();

    latencyLabel.setBounds (trailing.removeFromRight (190));
    trailing.removeFromRight (6);
    resolutionBox.setBounds (trailing.removeFromRight (116));
    trailing.removeFromRight (4);
    modeBox.setBounds (trailing.removeFromRight (118));

    //--------------------------------------------------------------------------
    auto bottom = area.removeFromBottom (bottomStripHeight);
    auto analyserBar = area.removeFromBottom (analyserBarHeight).reduced (6, 4);

    curve.setBounds (area);

    analyserCaption.setBounds (analyserBar.removeFromLeft (56));
    preButton.setBounds (analyserBar.removeFromLeft (44));
    analyserBar.removeFromLeft (2);
    postButton.setBounds (analyserBar.removeFromLeft (44));
    analyserBar.removeFromLeft (2);
    freezeButton.setBounds (analyserBar.removeFromLeft (56));
    analyserBar.removeFromLeft (12);
    speedBox.setBounds (analyserBar.removeFromLeft (84));
    analyserBar.removeFromLeft (4);
    tiltBox.setBounds (analyserBar.removeFromLeft (94));
    analyserBar.removeFromLeft (4);
    floorBox.setBounds (analyserBar.removeFromLeft (84));
    analyserBar.removeFromLeft (12);
    keyboardButton.setBounds (analyserBar.removeFromLeft (52));

    //--------------------------------------------------------------------------
    auto output = bottom.removeFromRight (outputSectionWidth).reduced (6, 5);
    auto band = bottom.reduced (6, 5);

    // 出力セクション。
    // Phase 206：**PhaseとAutoは「Output」の右横**（本人の要望）。
    // つまみの下にあったときは、下端に詰まっていて押しにくく、
    // 「Outputの設定」だということも読み取りにくい並びでした
    auto outputHeader = output.removeFromTop (20);

    outputTitle.setBounds (outputHeader.removeFromLeft (58));
    phaseButton.setBounds (outputHeader.removeFromLeft (58).reduced (0, 1));
    outputHeader.removeFromLeft (4);
    autoGainButton.setBounds (outputHeader.removeFromLeft (52).reduced (0, 1));

    meter.setBounds (output.removeFromRight (18).reduced (0, 2));
    output.removeFromRight (6);

    auto layoutKnob = [] (juce::Rectangle<int>& row, juce::Slider& slider, juce::Label& caption)
    {
        auto cell = row.removeFromLeft (knobWidth);
        caption.setBounds (cell.removeFromTop (12));
        slider.setBounds (cell);
        row.removeFromLeft (2);
    };

    auto outputKnobs = output;
    layoutKnob (outputKnobs, outGainSlider, outGainCaption);
    layoutKnob (outputKnobs, outPanSlider, outPanCaption);
    layoutKnob (outputKnobs, outMsSlider, outMsCaption);

    // バンドのつまみ
    auto bandHeader = band.removeFromTop (20);
    bandTitle.setBounds (bandHeader.removeFromLeft (72));
    bandOnButton.setBounds (bandHeader.removeFromLeft (44).reduced (0, 1));
    bandHeader.removeFromLeft (2);
    bandSoloButton.setBounds (bandHeader.removeFromLeft (46).reduced (0, 1));
    bandHeader.removeFromLeft (2);
    bandDynButton.setBounds (bandHeader.removeFromLeft (44).reduced (0, 1));

    emptyHint.setBounds (band);

    auto knobs = band;

    layoutKnob (knobs, freqSlider, freqCaption);
    layoutKnob (knobs, gainSlider, gainCaption);
    layoutKnob (knobs, qSlider, qCaption);

    knobs.removeFromLeft (8);

    auto combos = knobs.removeFromLeft (comboColumnWidth);

    auto placeCombo = [] (juce::Rectangle<int>& column, juce::Label& caption, juce::ComboBox& box)
    {
        auto cell = column.removeFromTop (32);
        caption.setBounds (cell.removeFromTop (11));
        box.setBounds (cell.reduced (0, 1));
    };

    placeCombo (combos, shapeCaption, shapeBox);
    placeCombo (combos, slopeCaption, slopeBox);
    placeCombo (combos, channelCaption, channelBox);

    knobs.removeFromLeft (8);

    layoutKnob (knobs, thresholdSlider, thresholdCaption);
    layoutKnob (knobs, rangeSlider, rangeCaption);
    layoutKnob (knobs, attackSlider, attackCaption);
    layoutKnob (knobs, releaseSlider, releaseCaption);
}
