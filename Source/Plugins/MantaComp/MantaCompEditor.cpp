#include "MantaCompEditor.h"

#include "../MantaTheme.h"
#include "../../Utf8.h"

//==============================================================================

MantaCompEditor::MantaCompEditor (MantaCompProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), "MantaComp"),
      display (processorToUse)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (display);

    statusLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (statusLabel);

    toolbar.onStateRestored = [this] { refreshReadouts(); };

    //--------------------------------------------------------------------------
    // ① Threshold / Ratio / Knee。**ここだけオレンジ**（圧縮の効きを決めるところ）
    setupSectionLabel (compressionTitle, "Compression");

    // Phase 211：**①の見出しだけ真ん中へ**（本人の要望）。
    // ①は縦に細い列で、つまみも数値も真ん中に揃っています——
    // 見出しだけ左端にあると、その列のものに見えません。
    // ③〜⑥は横に広い帯なので、左端のままのほうが「ここから」が読めます
    compressionTitle.setJustificationType (juce::Justification::centred);

    setupKnob (thresholdSlider, thresholdCaption, "Threshold", MantaCompParams::threshold, MantaTheme::curve());
    setupKnob (ratioSlider, ratioCaption, "Ratio", MantaCompParams::ratio, MantaTheme::curve());
    setupKnob (kneeSlider, kneeCaption, "Knee", MantaCompParams::knee, MantaTheme::curve());

    // 仕様書2-2の**最重要メーター**の数値。メーターの細い棒だけでは何dBか読めません
    reductionCaption.setText ("Gain Reduction", juce::dontSendNotification);
    reductionCaption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    reductionCaption.setFont (juce::Font (juce::FontOptions (10.0f)));
    reductionCaption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (reductionCaption);

    reductionValue.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    reductionValue.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (reductionValue);

    thresholdSlider.setTooltip (utf8 ("この音量を超えたぶんを圧縮します"));
    kneeSlider.setTooltip (utf8 ("0でしきい値にきっちり、大きくするほど手前から緩やかに掛かります"));

    //--------------------------------------------------------------------------
    // ③ Envelope
    setupSectionLabel (envelopeTitle, "Envelope");

    setupKnob (attackSlider, attackCaption, "Attack", MantaCompParams::attack, MantaTheme::accent());
    setupKnob (releaseSlider, releaseCaption, "Release", MantaCompParams::release, MantaTheme::accent());

    setupButton (autoEnvelopeButton, "Auto", MantaCompParams::autoEnvelope);
    setupButton (adaptiveButton, "Adapt", MantaCompParams::adaptiveRelease);

    autoEnvelopeButton.setTooltip (utf8 ("音の尖り具合からAttackとReleaseを決めます"));
    adaptiveButton.setTooltip (utf8 ("掛かりっぱなしのときほどReleaseを伸ばして、音量の揺れを抑えます"));

    //--------------------------------------------------------------------------
    // ④ Gain
    setupSectionLabel (gainTitle, "Gain");

    setupKnob (inputGainSlider, inputGainCaption, "Input", MantaCompParams::inputGain,
                MantaTheme::accent(), true);
    setupKnob (makeupSlider, makeupCaption, "Makeup", MantaCompParams::makeupGain,
                MantaTheme::accent(), true);

    setupButton (autoGainButton, "Auto", MantaCompParams::autoGain);
    setupButton (lookAheadButton, "Look", MantaCompParams::lookAhead);
    setupButton (stereoLinkButton, "Link", MantaCompParams::stereoLink);

    autoGainButton.setTooltip (utf8 ("圧縮したぶんを足し戻して、前後の音量をそろえます"));
    lookAheadButton.setTooltip (utf8 ("入力を先読みして、急なピークにも遅れずに掛けます（そのぶん遅れます）"));
    stereoLinkButton.setTooltip (utf8 ("左右を同じだけ圧縮します（切ると定位が動きます）"));

    //--------------------------------------------------------------------------
    // ⑤ Sidechain
    setupSectionLabel (sidechainTitle, "Sidechain");

    setupKnob (lowCutSlider, lowCutCaption, "Low Cut", MantaCompParams::scLowCut, MantaTheme::accent());
    setupKnob (highCutSlider, highCutCaption, "High Cut", MantaCompParams::scHighCut, MantaTheme::accent());

    setupButton (filterButton, "Filter", MantaCompParams::scFilterOn);
    setupButton (listenButton, "Listen", MantaCompParams::scListen);

    MantaPluginToolbar::styleButton (swapButton, "Swap");
    addAndMakeVisible (swapButton);
    swapButton.onClick = [this] { processor.swapSidechainFrequencies(); };

    filterButton.setTooltip (utf8 ("検出だけにフィルタを掛けます（出てくる音は変わりません）"));
    listenButton.setTooltip (utf8 ("検出に使っている音そのものを聴きます"));
    swapButton.setTooltip (utf8 ("Low CutとHigh Cutの値を入れ替えます"));

    //--------------------------------------------------------------------------
    // ⑥ Global
    setupSectionLabel (globalTitle, "Global");

    setupKnob (mixSlider, mixCaption, "Mix", MantaCompParams::mix, MantaTheme::accent());
    mixSlider.setTooltip (utf8 ("下げると原音が混ざります（パラレルコンプ）"));

    //--------------------------------------------------------------------------
    refreshReadouts();

    // 8.172：**大きさは固定**（Phase 212／本人の要望。Manta EQと揃えてあります）。
    //
    // 広げても増えるのは隙間だけでした——つまみは正円で大きさが決まっており、
    // 帯の中で**あいだが伸びるだけ**だったためです。
    // 寸法は**これまでの下限**（縦に積んだ①とGRの数字が収まると
    // 確かめてある寸法）をそのまま使っています。
    setSize (fixedWidth, fixedHeight);

    startTimerHz (5);
}

MantaCompEditor::~MantaCompEditor()
{
    stopTimer();

    // **被せたLookAndFeelは、必ず外してから壊すこと**（1.5と同じ決まり）
    for (auto* slider : { &thresholdSlider, &ratioSlider, &kneeSlider, &attackSlider, &releaseSlider,
                          &inputGainSlider, &makeupSlider, &lowCutSlider, &highCutSlider, &mixSlider })
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void MantaCompEditor::setupSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, MantaTheme::text());
    label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (label);
}

void MantaCompEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                                  const char* parameterId, juce::Colour colour, bool bipolar)
{
    slider.setLookAndFeel (&knobLookAndFeel.get());

    if (bipolar)
        slider.getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);

    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour (juce::Slider::textBoxTextColourId, MantaTheme::text());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(), parameterId, slider));
}

void MantaCompEditor::setupButton (juce::TextButton& button, const juce::String& text, const char* parameterId)
{
    MantaPluginToolbar::styleButton (button, text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);

    buttonAttachments.add (new ButtonAttachment (processor.getValueTreeState(), parameterId, button));
}

//==============================================================================

void MantaCompEditor::refreshReadouts()
{
    const auto settings = processor.getSettings();

    // Autoで動いているぶんは、**つまみでは分かりません**（つまみは元の値のまま）。
    // いくつで動いているのかを、ここに出します
    const bool autoActive = settings.autoEnvelope || settings.adaptiveRelease;

    attackSlider.setEnabled (! settings.autoEnvelope);
    attackCaption.setEnabled (! settings.autoEnvelope);

    juce::String text;

    if (processor.isSidechainConnected())
        text << utf8 ("外部SC") << "  ";

    if (settings.listen)
        text << utf8 ("Listen中") << "  ";

    if (autoActive)
    {
        const auto& engine = processor.getEngine();

        text << "A " << juce::String (engine.getEffectiveAttackMs(), 1)
             << " / R " << juce::String (engine.getEffectiveReleaseMs(), 0) << " ms  ";
    }

    const double sampleRate = processor.getSampleRateForDisplay();
    const int latency = processor.getReportedLatencySamples();

    text << juce::String (sampleRate > 0.0 ? latency * 1000.0 / sampleRate : 0.0, 1) << " ms";

    statusLabel.setColour (juce::Label::textColourId,
                            settings.listen ? AppColours::orange : MantaTheme::textDim());
    statusLabel.setText (text, juce::dontSendNotification);

    const float reduction = processor.getEngine().getReductionDb();

    reductionValue.setColour (juce::Label::textColourId,
                               reduction > 0.2f ? AppColours::orange : MantaTheme::textDim());
    reductionValue.setText ("-" + juce::String (reduction, 1) + " dB", juce::dontSendNotification);
}

void MantaCompEditor::timerCallback()
{
    refreshReadouts();
}

//==============================================================================

void MantaCompEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    auto area = getLocalBounds();
    area.removeFromTop (MantaPluginToolbar::preferredHeight);

    // 下の4ブロックの地
    auto bottom = area.removeFromBottom (bottomSectionHeight);

    g.setColour (MantaTheme::panelBackground());
    g.fillRect (bottom);

    g.setColour (MantaTheme::border());
    g.drawHorizontalLine (bottom.getY(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (MantaPluginToolbar::preferredHeight - 1, 0.0f, (float) getWidth());
}

void MantaCompEditor::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (MantaPluginToolbar::preferredHeight));
    statusLabel.setBounds (toolbar.getTrailingArea());

    //--------------------------------------------------------------------------
    auto bottom = area.removeFromBottom (bottomSectionHeight).reduced (6, 5);
    auto top = area.reduced (6, 5);

    // **高さも決め打ちにすること**（Phase 209）。残り全部を渡すと、
    // 帯の高さが違うだけで**つまみの大きさが場所ごとに変わります**
    // ——縦に積んだ①と、下の③〜⑥で丸の大きさが揃わなくなります
    auto layoutKnob = [] (juce::Rectangle<int>& row, juce::Slider& slider, juce::Label& caption)
    {
        auto cell = row.removeFromLeft (knobWidth).withHeight (stackedKnobHeight);

        caption.setBounds (cell.removeFromTop (12));
        slider.setBounds (cell);
        row.removeFromLeft (2);
    };

    //--------------------------------------------------------------------------
    // ① Threshold / Ratio / Knee ＋ ② ディスプレイ
    auto compression = top.removeFromLeft (compressionPanelWidth);

    compressionTitle.setBounds (compression.removeFromTop (18));

    // 設計書4-1のとおり**縦に3つ**（Phase 209／本人の要望）。
    //
    // **高さは決め打ちにすること。** 残り全部を渡すと、
    // 見出しは上・つまみは真ん中に離れて、対応が読めなくなります。
    // 幅はパネルいっぱいを渡していますが、つまみは正円で真ん中に描かれ、
    // 数値欄だけが広がります（読みやすいほうへ広がる）
    auto stackKnob = [this] (juce::Rectangle<int>& column, juce::Slider& slider, juce::Label& caption)
    {
        auto cell = column.removeFromTop (stackedKnobHeight);

        caption.setBounds (cell.removeFromTop (12));
        slider.setBounds (cell);
        column.removeFromTop (2);
    };

    stackKnob (compression, thresholdSlider, thresholdCaption);
    stackKnob (compression, ratioSlider, ratioCaption);
    stackKnob (compression, kneeSlider, kneeCaption);

    // 下に残ったところへ、いちばん重要な数字（GR）を大きく
    compression.removeFromTop (6);
    reductionCaption.setBounds (compression.removeFromTop (14));
    reductionValue.setBounds (compression.removeFromTop (36));

    top.removeFromLeft (6);
    display.setBounds (top);

    //--------------------------------------------------------------------------
    // ③〜⑥。**要る幅を先に足しておき、その塊ごと真ん中へ寄せます**
    // （Phase 212／本人の要望）。
    //
    // 以前は⑤に「残り全部」を渡していました。大きさを固定したいま、余りは
    // **いつも同じだけ**出るので、渡してしまうと⑤の左右のつまみだけが
    // 離れて並び、真ん中に穴が空いて見えます。
    // 余りは**外側に等分**するのが、いちばん目立ちません
    const int envelopeWidth = knobWidth * 2 + 4 + 52;
    const int gainWidth = knobWidth * 2 + 4 + 52;
    const int sidechainWidth = knobWidth * 2 + 56 + 16;   // つまみ・ボタン・あいだ
    const int globalWidth = knobWidth + 8;

    const int rowWidth = envelopeWidth + gainWidth + sidechainWidth + globalWidth + 8 * 3;
    bottom = bottom.withSizeKeepingCentre (juce::jmin (bottom.getWidth(), rowWidth),
                                            bottom.getHeight());

    // **Mixだけ幅が1つぶん**なので、そこから先に取る
    auto global = bottom.removeFromRight (globalWidth);
    globalTitle.setBounds (global.removeFromTop (16));
    layoutKnob (global, mixSlider, mixCaption);

    auto envelope = bottom.removeFromLeft (envelopeWidth);
    bottom.removeFromLeft (8);
    auto gain = bottom.removeFromLeft (gainWidth);
    bottom.removeFromLeft (8);
    auto sidechain = bottom.removeFromLeft (sidechainWidth);

    auto placeButtons = [] (juce::Rectangle<int> column, std::initializer_list<juce::TextButton*> buttons)
    {
        for (auto* button : buttons)
        {
            button->setBounds (column.removeFromTop (20).reduced (0, 1));
            column.removeFromTop (1);
        }
    };

    // ③ Envelope
    envelopeTitle.setBounds (envelope.removeFromTop (16));
    {
        auto buttons = envelope.removeFromRight (52);
        placeButtons (buttons, { &autoEnvelopeButton, &adaptiveButton });

        auto knobs = envelope;
        layoutKnob (knobs, attackSlider, attackCaption);
        layoutKnob (knobs, releaseSlider, releaseCaption);
    }

    // ④ Gain
    gainTitle.setBounds (gain.removeFromTop (16));
    {
        auto buttons = gain.removeFromRight (52);
        placeButtons (buttons, { &autoGainButton, &lookAheadButton, &stereoLinkButton });

        auto knobs = gain;
        layoutKnob (knobs, inputGainSlider, inputGainCaption);
        layoutKnob (knobs, makeupSlider, makeupCaption);
    }

    // ⑤ Sidechain。設計書4-1のとおり、**ノブを左右にしてボタンを真ん中**へ
    sidechainTitle.setBounds (sidechain.removeFromTop (16));
    {
        auto knobs = sidechain;

        auto left = knobs.removeFromLeft (knobWidth);
        lowCutCaption.setBounds (left.removeFromTop (12));
        lowCutSlider.setBounds (left);

        auto right = knobs.removeFromRight (knobWidth);
        highCutCaption.setBounds (right.removeFromTop (12));
        highCutSlider.setBounds (right);

        auto buttons = knobs.reduced (4, 0).withSizeKeepingCentre (juce::jmin (56, knobs.getWidth()), 64);
        placeButtons (buttons, { &filterButton, &listenButton, &swapButton });
    }
}
