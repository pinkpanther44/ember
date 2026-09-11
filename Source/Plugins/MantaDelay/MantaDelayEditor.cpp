#include "MantaDelayEditor.h"

#include "../MantaTheme.h"
#include "../../Branding.h"
#include "../../Utf8.h"
#include "MantaDelayCharacter.h"   // 8.208：キャラクターの名前と可否（Phase 240）
#include "MantaDelayFilter.h"      // 8.210：フィルターの形と可否（Phase 242）
#include "MantaDelayLfo.h"         // 8.211：波形の名前（Phase 242）
#include "MantaDelayRouting.h"     // 8.217：ルーティングの名前（Phase 244）

//==============================================================================

MantaDelayEditor::MantaDelayEditor (MantaDelayProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::delayPresetFolder)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (display);

    statusLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (statusLabel);

    toolbar.onStateRestored = [this]
    {
        refreshTimeControls();
        refreshCharacterControls();   // 8.208（Phase 240）
        refreshFilterControls();      // 8.210（Phase 242）
        refreshTapControls();         // 8.214（Phase 243）
    };

    //--------------------------------------------------------------------------
    setupSectionLabel (echoTitle, "Echo");

    // **主の色は「反復そのものを決めるところ」**（8.204の表）
    setupKnob (timeSlider, timeCaption, "Time",
                MantaDelayTheme::accent());
    setupKnob (feedbackSlider, feedbackCaption, "Feedback",
                MantaDelayTheme::accent());

    // 8.217：そのエンジンの出口（Phase 244）
    setupKnob (levelSlider, levelCaption, "Level",
                MantaDelayTheme::accent());
    setupKnob (panSlider, panCaption, "Pan",
                MantaDelayTheme::accent());

    // **副の色は「原音との混ぜ具合」**
    setupKnob (mixSlider, mixCaption, "Mix",
                MantaDelayTheme::highlight());
    setupKnob (outputSlider, outputCaption, "Output",
                MantaDelayTheme::highlight());

    // 8.218：戻りの入れ替え（Phase 245）。**副の色**——AとBの関わりを決めるところ
    setupKnob (crossSlider, crossCaption, "Cross",
                MantaDelayTheme::highlight());

    // 8.217〜8.218：**この3つだけエンジン共通**（Phase 244・245）。作ったところで繋ぎます
    globalAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  MantaDelayParams::mix, mixSlider));
    globalAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  MantaDelayParams::outputGain, outputSlider));
    globalAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  MantaDelayParams::crossFeedback, crossSlider));

    // 8.225：**「同じもの同士を入れ替えても何も起きない」と書いておくこと**
    // （Phase 247／本人の報告「Crossの違いが微妙」）。
    // Dualに切り替えた直後はBの既定値がAと同じなので、**本当に何も起きません**
    crossSlider.setTooltip (utf8 ("AとBで戻りをどれだけ入れ替えるか。Dual専用。"
                                   "AとBのTimeやCharacterが同じだと違いは出ません"));

    //--------------------------------------------------------------------------
    // 8.219〜8.221：Phase 6（Phase 246／仕様書5-1・5-5）

    // **副の色**——滲みは「反復そのもの」ではなく、その周りの手触り
    setupKnob (diffuseSlider, diffuseCaption, "Diffuse",
                MantaDelayTheme::highlight());

    diffuseSlider.setTooltip (utf8 ("反復を滲ませてリバーブに近づけます"));

    MantaPluginToolbar::styleButton (freezeButton, "Freeze");
    freezeButton.setClickingTogglesState (true);
    freezeButton.setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::highlight());
    freezeButton.setTooltip (utf8 ("いま線の中にある音をそのまま回し続けます（新しい音は入りません）"));
    addAndMakeVisible (freezeButton);

    MantaPluginToolbar::styleButton (reverseButton, "Reverse");
    reverseButton.setClickingTogglesState (true);
    reverseButton.setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::highlight());
    reverseButton.setTooltip (utf8 ("入口を裏返します。裏返す長さはTimeと同じです"));
    addAndMakeVisible (reverseButton);

    timeSlider.setTooltip (utf8 ("反復の間隔。Syncを入れると曲のテンポに合わせます"));
    feedbackSlider.setTooltip (utf8 ("返ってきた音をどれだけ戻すか。上げるほど長く反復します"));
    levelSlider.setTooltip (utf8 ("このエンジンの音量。AとBの釣り合いを取ります"));
    panSlider.setTooltip (utf8 ("このエンジンの左右の位置"));

    // **エンジン共通のものは、そう書いておくこと**——A/Bを切り替えても
    // 動かないつまみが2つあると、壊れているように見えます
    mixSlider.setTooltip (utf8 ("原音とディレイ音の混ぜ具合（AとBで共通）"));
    outputSlider.setTooltip (utf8 ("出口の音量（AとBで共通）"));

    //--------------------------------------------------------------------------
    // 8.217：Phase 5aのデュアルエンジン（Phase 244／仕様書3-1）

    for (auto* button : { &engineAButton, &engineBButton })
    {
        MantaPluginToolbar::styleButton (*button, button == &engineAButton ? "A" : "B");
        button->setClickingTogglesState (true);
        button->setRadioGroupId (1);
        button->setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::accent());
        addAndMakeVisible (*button);
    }

    engineAButton.setTooltip (utf8 ("つまみをEngine Aに向けます"));
    engineBButton.setTooltip (utf8 ("つまみをEngine Bに向けます"));

    engineAButton.onClick = [this] { setSelectedEngine (0); };
    engineBButton.onClick = [this] { setSelectedEngine (1); };

    routingCaption.setText ("Routing", juce::dontSendNotification);
    routingCaption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    routingCaption.setFont (juce::Font (juce::FontOptions (11.0f)));
    routingCaption.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (routingCaption);

    routingBox.addItemList (MantaDelayRouting::getModeNames(), 1);
    routingBox.setTooltip (utf8 ("AとBの繋ぎ方"));
    addAndMakeVisible (routingBox);

    routingAttachment = std::make_unique<ComboAttachment> (
        processor.getValueTreeState(), MantaDelayParams::routingMode, routingBox);

    routingBox.onChange = [this] { refreshRoutingControls(); };

    routingDescription.setColour (juce::Label::textColourId, MantaTheme::textDim());
    routingDescription.setFont (juce::Font (juce::FontOptions (11.0f)));
    routingDescription.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (routingDescription);

    //--------------------------------------------------------------------------
    // テンポシンク

    MantaPluginToolbar::styleButton (syncButton, "Sync");
    syncButton.setClickingTogglesState (true);
    syncButton.setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::highlight());
    addAndMakeVisible (syncButton);

    // **`onClick`ではなく`onStateChange`。** オートメーションや
    // プリセットの読み込みで変わったときにも呼ばれます（押したときだけではない）
    syncButton.onStateChange = [this] { refreshTimeControls(); };

    // 8.207：**Syncのときは、同じ場所が音価のつまみになります**（Phase 239/本人の指定）。
    // `setupKnob()`を通すので、**大きさも見た目もTimeと同じ**です
    setupKnob (divisionSlider, timeCaption, "Time",
                MantaDelayTheme::accent());

    // **繋ぎは両方生かしておきます**（`rebuildEngineAttachments()`が2本とも繋ぎます）——
    // 出し分けるのは見た目だけです

    // **段々に回します。** `syncDivision`は`AudioParameterChoice`なので
    // `SliderAttachment`が0〜13・刻み1の範囲を入れますが、
    // **`setRange()`を後から呼ぶと上書きしてしまう**ので触らないこと。
    // 目盛りの名前だけ差し替えます
    divisionSlider.textFromValueFunction = [] (double value)
    {
        const auto names = MantaDelayParams::getSyncDivisionNames();
        return names[juce::jlimit (0, names.size() - 1, juce::roundToInt (value))];
    };

    divisionSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        const auto names = MantaDelayParams::getSyncDivisionNames();
        const int index = names.indexOf (text.trim());

        return (double) juce::jmax (0, index);
    };

    divisionSlider.setTooltip (utf8 ("音価。付点は「.」、3連は「T」です"));

    //--------------------------------------------------------------------------
    // 8.208：Phase 2（キャラクター。Phase 240）

    setupSectionLabel (characterTitle, "Character");

    characterBox.addItemList (MantaDelayCharacter::getKindNames(), 1);
    characterBox.setTooltip (utf8 ("音色モデル。効かないつまみは灰色になります"));
    addAndMakeVisible (characterBox);

    // **`onChange`はオートメーションやプリセットの読み込みでも呼ばれます**
    characterBox.onChange = [this] { refreshCharacterControls(); };

    // **色の分け方は上と同じ**（主＝音を作るところ、副＝揺らすところ）
    setupKnob (driveSlider, driveCaption, "Drive",
                MantaDelayTheme::accent());
    setupKnob (toneSlider, toneCaption, "Tone",
                MantaDelayTheme::accent());

    setupKnob (wowDepthSlider, wowDepthCaption, "Wow",
                MantaDelayTheme::highlight());
    setupKnob (wowRateSlider, wowRateCaption, "Wow Rate",
                MantaDelayTheme::highlight());
    setupKnob (flutterDepthSlider, flutterDepthCaption, "Flutter",
                MantaDelayTheme::highlight());
    setupKnob (flutterRateSlider, flutterRateCaption, "Flutter Rate",
                MantaDelayTheme::highlight());

    driveSlider.setTooltip (utf8 ("Tapeでは飽和の強さ、Lo-Fiでは削り具合"));
    toneSlider.setTooltip (utf8 ("反復するたびに落ちる高域の量"));

    //--------------------------------------------------------------------------
    // 8.210：Phase 3のフィルター（Phase 242／仕様書5-2）

    setupSectionLabel (filterTitle, "Filter");

    filterTypeBox.addItemList (MantaDelayFilter::getTypeNames(), 1);
    filterTypeBox.setTooltip (utf8 ("フィードバックの中に入るフィルター。反復するたびに掛かります"));
    addAndMakeVisible (filterTypeBox);

    filterTypeBox.onChange = [this] { refreshFilterControls(); };

    MantaPluginToolbar::styleButton (filterPositionButton, "Pre");
    filterPositionButton.setClickingTogglesState (true);
    filterPositionButton.setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::highlight());
    filterPositionButton.setTooltip (utf8 ("フィルターとキャラクターの順番。Preは削ってから歪ませ、Postは歪ませてから削ります"));
    addAndMakeVisible (filterPositionButton);

    // **`onClick`ではなく`onStateChange`**（オートメーションやプリセットでも呼ばれる）
    filterPositionButton.onStateChange = [this]
    {
        // **表の文字がいまの状態そのもの**（ヘッダの説明）
        filterPositionButton.setButtonText (filterPositionButton.getToggleState() ? "Post" : "Pre");
    };

    // **1回は自分で呼ぶこと。** `ButtonAttachment`は作った時点で
    // 保存されていた値を入れますが、それは`onStateChange`を入れる**前**です——
    // これが無いと、Postで保存したものを開いたときに「Pre」と書かれたままになります
    filterPositionButton.onStateChange();

    // **主の色**——フィルターは「反復そのものを作り変える」側（8.204の表）
    setupKnob (filterFreqSlider, filterFreqCaption, "Freq",
                MantaDelayTheme::accent());
    setupKnob (filterQSlider, filterQCaption, "Q",
                MantaDelayTheme::accent());
    setupKnob (filterGainSlider, filterGainCaption, "Gain",
                MantaDelayTheme::accent());

    filterGainSlider.setTooltip (utf8 ("Bellのときだけ効きます。上げた帯は他より遅く減衰します"));

    //--------------------------------------------------------------------------
    // 8.211：Phase 3のLFO（Phase 242／仕様書5-3）

    setupSectionLabel (modulationTitle, "Modulation");

    lfoShapeBox.addItemList (MantaDelayLfo::getShapeNames(), 1);
    lfoShapeBox.setTooltip (utf8 ("揺れの形。Randomは1周ごとに値が飛びます"));
    addAndMakeVisible (lfoShapeBox);

    // **副の色**——Wow／Flutterと同じ「揺らすところ」
    setupKnob (lfoDepthSlider, lfoDepthCaption, "LFO",
                MantaDelayTheme::highlight());
    setupKnob (lfoRateSlider, lfoRateCaption, "LFO Rate",
                MantaDelayTheme::highlight());

    lfoDepthSlider.setTooltip (utf8 ("ディレイタイムを揺らす深さ。どのキャラクターでも効きます"));

    //--------------------------------------------------------------------------
    // 8.212：Phase 3のダッキング（Phase 242／仕様書5-4）

    setupSectionLabel (duckingTitle, "Ducking");

    addAndMakeVisible (duckMeter);

    setupKnob (duckAmountSlider, duckAmountCaption, "Duck",
                MantaDelayTheme::highlight());
    setupKnob (duckAttackSlider, duckAttackCaption, "Attack",
                MantaDelayTheme::highlight());
    setupKnob (duckReleaseSlider, duckReleaseCaption, "Release",
                MantaDelayTheme::highlight());

    duckAmountSlider.setTooltip (utf8 ("原音が鳴っているあいだ、ディレイ音をどれだけ絞るか"));
    duckAttackSlider.setTooltip (utf8 ("絞りはじめるまでの速さ"));
    duckReleaseSlider.setTooltip (utf8 ("原音が切れてから戻るまでの速さ"));

    //--------------------------------------------------------------------------
    // 8.214：Phase 4のマルチタップ（Phase 243／仕様書5-6）

    setupSectionLabel (tapsTitle, "Taps");

    addAndMakeVisible (tapStrip);

    tapStrip.onTapSelected = [this] (int tap)
    {
        if (tap == selectedTap)
            return;

        selectedTap = tap;

        // **選んだタップは覚えておきます**（音には関係しないので`UI`の子へ。8.214）
        processor.getUiState().setProperty (MantaDelayUiState::selectedTap, tap, nullptr);

        rebuildTapAttachments();
        refreshTapControls();
    };

    // **主の色**——タップは「反復そのものの置き場所」
    setupKnob (tapCountSlider, tapCountCaption, "Taps", MantaDelayTheme::accent());
    setupKnob (tapStepSlider, tapStepCaption, "Step", MantaDelayTheme::accent());
    setupKnob (tapLevelSlider, tapLevelCaption, "Level", MantaDelayTheme::highlight());
    setupKnob (tapPanSlider, tapPanCaption, "Pan", MantaDelayTheme::highlight());

    tapCountSlider.setTooltip (utf8 ("鳴らすタップの本数"));
    tapStepSlider.setTooltip (utf8 ("そのタップをTimeの何個ぶん後ろに置くか"));
    tapLevelSlider.setTooltip (utf8 ("そのタップの音量"));
    tapPanSlider.setTooltip (utf8 ("そのタップの左右の位置"));

    // **本数を変えたら、選んでいるタップが外に出ていないか見ます**
    tapCountSlider.onValueChange = [this] { refreshTapControls(); };

    //--------------------------------------------------------------------------
    // **覚えていたものを戻してから繋ぎます**（音には関係しないので`UI`の子。8.214）

    selectedTap = juce::jlimit (0, MantaDelayTaps::maxTaps - 1,
                                 (int) processor.getUiState()
                                           .getProperty (MantaDelayUiState::selectedTap, 0));

    selectedEngine = juce::jlimit (0, MantaDelayParams::numEngines - 1,
                                    (int) processor.getUiState()
                                              .getProperty (MantaDelayUiState::selectedEngine, 0));

    (selectedEngine == 0 ? engineAButton : engineBButton).setToggleState (true, juce::dontSendNotification);

    // **繋ぐのはここ1回。** `rebuildEngineAttachments()`がタップまで面倒を見ます
    rebuildEngineAttachments();

    refreshTimeControls();
    refreshCharacterControls();
    refreshFilterControls();
    refreshTapControls();
    refreshRoutingControls();

    setSize (fixedWidth, fixedHeight);
    setResizable (false, false);   // 8.172：内蔵プラグインの画面は固定

    startTimerHz (30);
}

MantaDelayEditor::~MantaDelayEditor()
{
    // **つまみからLookAndFeelを外してから壊すこと**（8.168）
    for (auto* slider : { &timeSlider, &divisionSlider, &feedbackSlider, &mixSlider, &outputSlider,
                           &driveSlider, &toneSlider, &wowDepthSlider, &wowRateSlider,
                           &flutterDepthSlider, &flutterRateSlider,
                           // 8.210〜8.212：Phase 3で足したぶん（Phase 242）。
                           // **足したつまみをここへ入れ忘れないこと**——
                           // LookAndFeelが先に壊れると、つまみがそれを見に行きます（8.168）
                           &filterFreqSlider, &filterQSlider, &filterGainSlider,
                           &lfoRateSlider, &lfoDepthSlider,
                           &duckAmountSlider, &duckAttackSlider, &duckReleaseSlider,
                           // 8.214：Phase 4で足したぶん（Phase 243）
                           &tapCountSlider, &tapStepSlider, &tapLevelSlider, &tapPanSlider,
                           // 8.217〜8.218：Phase 5で足したぶん（Phase 244・245）
                           &levelSlider, &panSlider, &crossSlider,
                           // 8.220：Phase 6で足したぶん（Phase 246）
                           &diffuseSlider })
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

/** 8.217：**見た目だけ**（Phase 244）。

    Phase 4まではここで`SliderAttachment`も作っていましたが、
    **つまみのほとんどがエンジンごとになった**ので、繋ぎは
    `rebuildEngineAttachments()`が一手に引き受けます。

    エンジン共通の`Mix`と`Output`だけ、作ったところで繋いでいます。 */
void MantaDelayEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption,
                                   const juce::String& text, juce::Colour colour)
{
    slider.setLookAndFeel (&knobLookAndFeel.get());
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour (juce::Slider::textBoxTextColourId, MantaTheme::text());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);
}

void MantaDelayEditor::setupSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, MantaTheme::textDim());
    label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (label);
}

void MantaDelayEditor::refreshTimeControls()
{
    const bool synced = syncButton.getToggleState();

    // 8.207：**同じ場所で、中身だけ入れ替えます**（Phase 239/本人の指定）。
    //
    // 場所は`resized()`で同じ矩形を渡しているので、**見た目は1つのつまみ**です。
    // 出ていないほうは触れないので、**隠れているつまみを掴んでしまう**ことはありません
    timeSlider.setVisible (! synced);
    divisionSlider.setVisible (synced);

    // **見出しも変える。** 同じ「Time」のままだと、
    // 段々に回るようになった理由が分かりません
    timeCaption.setText (synced ? "Division" : "Time", juce::dontSendNotification);
}


//==============================================================================
// 8.208：Phase 2（キャラクター。Phase 240）

void MantaDelayEditor::refreshCharacterControls()
{
    const auto kind = (MantaDelayCharacter::Kind)
                         juce::jlimit (0, MantaDelayCharacter::getKindCount() - 1,
                                        characterBox.getSelectedItemIndex());

    // **効くかどうかの判断は`getCapabilities()`ただ1つ**（1.27）。
    // 画面と音で別々に決めると、**触れるのに効かないつまみ**や、その逆ができます
    const auto capabilities = MantaDelayCharacter::getCapabilities (kind);

    setControlActive (driveSlider, driveCaption, capabilities.drive);
    setControlActive (toneSlider, toneCaption, capabilities.tone);
    setControlActive (wowDepthSlider, wowDepthCaption, capabilities.wow);
    setControlActive (wowRateSlider, wowRateCaption, capabilities.wow);
    setControlActive (flutterDepthSlider, flutterDepthCaption, capabilities.flutter);
    setControlActive (flutterRateSlider, flutterRateCaption, capabilities.flutter);
}

/** **消さずにグレーアウトする**（本人の判断。8.208）。

    隠すと画面は片付きますが、**何が隠れているか分かりません。**
    グレーアウトなら「そこに何が無いか」が見えます——
    Digital Cleanを選ぶとキャラクターの4つが灰色になり、
    Filterを`Off`にすると`Freq`・`Q`・`Gain`が灰色になります。

    8.210：**Phase 2とPhase 3で同じものを使います**（Phase 242。1.27）——
    2つ目の写しを作ると、片方だけ`0.4f`のままになります。 */
void MantaDelayEditor::setControlActive (juce::Component& control, juce::Label& caption, bool active)
{
    control.setEnabled (active);
    control.setAlpha (active ? 1.0f : 0.4f);
    caption.setAlpha (active ? 1.0f : 0.4f);
}

//==============================================================================
// 8.210：Phase 3のフィルター（Phase 242）

void MantaDelayEditor::refreshFilterControls()
{
    const auto type = (MantaDelayFilter::Type)
                         juce::jlimit (0, MantaDelayFilter::getTypeCount() - 1,
                                        filterTypeBox.getSelectedItemIndex());

    // **効くかどうかの判断は`MantaDelayFilter`ただ1つ**（1.27）
    const bool active = MantaDelayFilter::isActive (type);

    setControlActive (filterFreqSlider, filterFreqCaption, active);
    setControlActive (filterQSlider, filterQCaption, active);

    // **`Gain`はBellだけ。** `Off`のときも当然効きません——
    // 2つの条件を掛けるのを忘れると、`Off`なのにGainだけ生きて見えます
    setControlActive (filterGainSlider, filterGainCaption,
                       active && MantaDelayFilter::usesGain (type));

    // Pre／Postは**フィルターが入っているときだけ**意味があります。
    // ラベルが無いので、明るさだけ落とします
    filterPositionButton.setEnabled (active);
    filterPositionButton.setAlpha (active ? 1.0f : 0.4f);
}

//==============================================================================
// 8.214：Phase 4のマルチタップ（Phase 243）

void MantaDelayEditor::rebuildTapAttachments()
{
    const int tap = juce::jlimit (0, MantaDelayTaps::maxTaps - 1, selectedTap);

    if (tap == attachedTap)
        return;

    attachedTap = tap;

    // **繋ぎ替えは、いったん外してから**（同じつまみに2本ぶら下がると、
    // 片方が前のタップへ書き戻します。Manta EQの`rebuildBandAttachments()`と同じ）
    tapStepAttachment.reset();
    tapLevelAttachment.reset();
    tapPanAttachment.reset();

    auto& state = processor.getValueTreeState();

    // 8.217：**エンジンの番号も要ります**（Phase 244）
    const int engine = juce::jlimit (0, MantaDelayParams::numEngines - 1, selectedEngine);

    const auto id = [engine, tap] (const char* suffix)
    {
        return MantaDelayParams::tapParamId (engine, tap, suffix);
    };

    tapStepAttachment  = std::make_unique<SliderAttachment> (state, id (MantaDelayParams::tapStep), tapStepSlider);
    tapLevelAttachment = std::make_unique<SliderAttachment> (state, id (MantaDelayParams::tapLevel), tapLevelSlider);
    tapPanAttachment   = std::make_unique<SliderAttachment> (state, id (MantaDelayParams::tapPan), tapPanSlider);
}

//==============================================================================
// 8.217：Phase 5aのデュアルエンジン（Phase 244）

void MantaDelayEditor::rebuildEngineAttachments()
{
    const int engine = juce::jlimit (0, MantaDelayParams::numEngines - 1, selectedEngine);

    // **いったん全部外してから**（8.215と同じ理由。同じつまみに2本ぶら下がると、
    // 片方が前のエンジンへ書き戻します）
    engineSliderAttachments.clear();
    engineComboAttachments.clear();
    engineButtonAttachments.clear();

    auto& state = processor.getValueTreeState();

    const auto id = [engine] (const char* suffix)
    {
        return MantaDelayParams::engineParamId (engine, suffix);
    };

    const auto addSlider = [&] (ValueEntrySlider& slider, const char* suffix)
    {
        engineSliderAttachments.add (new SliderAttachment (state, id (suffix), slider));
    };

    // **足したつまみをここへ入れ忘れないこと**——入れ忘れると、
    // **そのつまみだけエンジンAに繋がったまま**になります（音は出るので気づきにくい）
    addSlider (timeSlider, MantaDelayParams::timeMs);
    addSlider (divisionSlider, MantaDelayParams::syncDivision);   // 8.207：Timeと重ねてある2本目
    addSlider (feedbackSlider, MantaDelayParams::feedback);
    addSlider (levelSlider, MantaDelayParams::engineLevel);
    addSlider (panSlider, MantaDelayParams::enginePan);

    addSlider (driveSlider, MantaDelayParams::drive);
    addSlider (toneSlider, MantaDelayParams::tone);
    addSlider (wowDepthSlider, MantaDelayParams::wowDepth);
    addSlider (wowRateSlider, MantaDelayParams::wowRate);
    addSlider (flutterDepthSlider, MantaDelayParams::flutterDepth);
    addSlider (flutterRateSlider, MantaDelayParams::flutterRate);

    addSlider (filterFreqSlider, MantaDelayParams::filterFreq);
    addSlider (filterQSlider, MantaDelayParams::filterQ);
    addSlider (filterGainSlider, MantaDelayParams::filterGain);

    addSlider (lfoDepthSlider, MantaDelayParams::lfoDepth);
    addSlider (lfoRateSlider, MantaDelayParams::lfoRate);

    addSlider (duckAmountSlider, MantaDelayParams::duckAmount);
    addSlider (duckAttackSlider, MantaDelayParams::duckAttack);
    addSlider (duckReleaseSlider, MantaDelayParams::duckRelease);

    addSlider (tapCountSlider, MantaDelayParams::tapCount);

    addSlider (diffuseSlider, MantaDelayParams::diffusion);   // 8.220（Phase 246）

    engineComboAttachments.add (new ComboAttachment (state, id (MantaDelayParams::character), characterBox));
    engineComboAttachments.add (new ComboAttachment (state, id (MantaDelayParams::filterType), filterTypeBox));
    engineComboAttachments.add (new ComboAttachment (state, id (MantaDelayParams::lfoShape), lfoShapeBox));

    engineButtonAttachments.add (new ButtonAttachment (state, id (MantaDelayParams::sync), syncButton));
    engineButtonAttachments.add (new ButtonAttachment (state, id (MantaDelayParams::filterPost), filterPositionButton));

    // 8.219・8.221（Phase 246）
    engineButtonAttachments.add (new ButtonAttachment (state, id (MantaDelayParams::freeze), freezeButton));
    engineButtonAttachments.add (new ButtonAttachment (state, id (MantaDelayParams::reverse), reverseButton));

    // **タップも繋ぎ直します**（`attachedTap`を無効にして、必ず作り直させる）
    attachedTap = -1;
    rebuildTapAttachments();

    // **繋ぎ直したら、出し分けも見直すこと。** Aが Sync ON・Bが OFF のとき、
    // 繋ぎ替えただけだと**Timeのつまみが出たまま**になります
    refreshTimeControls();
    refreshCharacterControls();
    refreshFilterControls();
    refreshTapControls();
}

void MantaDelayEditor::setSelectedEngine (int engine)
{
    const int wanted = juce::jlimit (0, MantaDelayParams::numEngines - 1, engine);

    if (wanted == selectedEngine)
        return;

    selectedEngine = wanted;

    processor.getUiState().setProperty (MantaDelayUiState::selectedEngine, wanted, nullptr);

    rebuildEngineAttachments();
}

void MantaDelayEditor::refreshRoutingControls()
{
    const auto mode = (MantaDelayRouting::Mode)
                         juce::jlimit (0, MantaDelayRouting::getModeCount() - 1,
                                        routingBox.getSelectedItemIndex());

    routingDescription.setText (MantaDelayRouting::getModeDescription (mode),
                                 juce::dontSendNotification);

    // **SingleではBが鳴りません。** 押せてしまうと「Bを直したのに音が変わらない」
    // になるので、**触れないことを見せます**（8.208と同じグレーアウト）
    const bool usesB = MantaDelayRouting::usesEngineB (mode);

    engineBButton.setEnabled (usesB);
    engineBButton.setAlpha (usesB ? 1.0f : 0.4f);

    // Bを映したままSingleへ戻ったら、**Aへ引き戻します**——
    // 鳴らないエンジンのつまみを回し続けることになるため
    if (! usesB && selectedEngine != 0)
    {
        engineAButton.setToggleState (true, juce::dontSendNotification);
        setSelectedEngine (0);
    }

    // Singleでは`Level`と`Pan`も意味がありません（混ぜる相手がいない）。
    // 8.218：**Ping-PongではPanが効きません**（左右を決めるのはモードそのもの。Phase 245）
    setControlActive (levelSlider, levelCaption, usesB);
    setControlActive (panSlider, panCaption, usesB && MantaDelayRouting::usesEnginePan (mode));

    // 8.218：`Cross`が効くのはDualだけ（`MantaDelayRouting::usesCrossFeedback()`）。
    // **Ping-Pongは全交換そのもの**なので、つまみで減らせると**ただのDualになります**
    setControlActive (crossSlider, crossCaption, MantaDelayRouting::usesCrossFeedback (mode));
}

void MantaDelayEditor::refreshTapControls()
{
    const int count = juce::jlimit (1, MantaDelayTaps::maxTaps,
                                     juce::roundToInt (tapCountSlider.getValue()));

    // **本数を減らしたときに、外へ出たタップを選んだままにしないこと。**
    // つまみが「もう鳴らないタップ」を指していると、回しても何も起きません
    if (selectedTap >= count)
    {
        selectedTap = count - 1;
        processor.getUiState().setProperty (MantaDelayUiState::selectedTap, selectedTap, nullptr);

        rebuildTapAttachments();
    }

    // タップが1本のときは、Step／Level／Panを触る意味がありません——
    // **1本目はstep 1・Level 100%・真ん中がPhase 3までの音**なので、
    // 消さずにグレーアウトして「いまは効かない」と出します（8.208と同じ）
    const bool multiTap = count > 1;

    setControlActive (tapStepSlider, tapStepCaption, multiTap);
    setControlActive (tapLevelSlider, tapLevelCaption, multiTap);
    setControlActive (tapPanSlider, tapPanCaption, multiTap);

    tapStrip.setEnabled (multiTap);
    tapStrip.setAlpha (multiTap ? 1.0f : 0.4f);
}

//==============================================================================

void MantaDelayEditor::timerCallback()
{
    MantaDelayDisplay::State state;

    // **つまみの値ではなく、エンジンがいま鳴らしている値**（1.27）
    // 8.217：**映しているエンジンのぶんを描きます**（Phase 244）
    state.delaySeconds = processor.getCurrentDelaySeconds (selectedEngine);
    state.feedback = feedbackSlider.getValue();
    state.mix = mixSlider.getValue();

    // 8.214：**タップの並びはプロセッサから**（Phase 243）。
    // つまみを1本ずつ読み直すと、**画面が数え直すことになります**（1.27）
    state.taps = processor.getTapPattern (selectedEngine);
    state.selectedTap = selectedTap;

    display.setState (state);

    tapStrip.setPattern (state.taps, selectedTap);

    // 8.212：ダッキングの帯（Phase 242）。**つまみの値ではなく、エンジンの実測**
    duckMeter.setReduction (processor.getDuckReduction (selectedEngine),
                             duckAmountSlider.getValue() > 0.0);

    //--------------------------------------------------------------------------
    // テンポが来ていないときは、そう出す（8.161：黙って違う動きをしない）

    const bool synced = syncButton.getToggleState();
    const double bpm = processor.getSyncBpm();

    juce::String status;

    if (synced && bpm <= 0.0)
        status = utf8 ("テンポが取れていません（Timeの値で鳴っています）");
    else if (synced)
        status = juce::String (bpm, 1) + " BPM";

    if (status != statusLabel.getText())
        statusLabel.setText (status, juce::dontSendNotification);
}

//==============================================================================

void MantaDelayEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    auto area = getLocalBounds();
    area.removeFromTop (MantaPluginToolbar::preferredHeight);

    // 8.222：**これから入るものの行は、消しました**（Phase 246）。
    //
    // Phase 1からずっと「**空きは黙って空けない**」で、いちばん下に
    // 薄く「Character · Filter · …（Phase 2以降）」と出していました。
    // **Phase 6で設計書のスコープが全部入った**ので、書くことがありません。
    //
    // **空になった行は、残さず片付けること**——「まだ何か来る」と読めてしまいます。
    // 空いた26pxはディスプレイへ回しました。
    //
    // 8.213の教訓（中黒も`utf8()`を通すこと）は、この行で踏んだものです。
    // 行は消えましたが、**9.4に残してあります。**

    //--------------------------------------------------------------------------
    // 8.210：Phase 3の帯（Phase 242）。3つの箱に分けて描きます。
    //
    // **箱にしておくこと。** つまみを9つ並べただけだと、
    // どこからどこまでが1つの機能なのかが読めません

    const auto drawPanel = [&g] (juce::Rectangle<int> box)
    {
        g.setColour (MantaTheme::panelBackground());
        g.fillRoundedRectangle (box.toFloat(), 4.0f);

        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    };

    for (auto& column : getPhase3Columns (area.removeFromBottom (phase3AreaHeight)))
        drawPanel (column);

    // 8.214：Phase 4の帯（Phase 243）。**こちらは1つの箱**
    drawPanel (area.removeFromBottom (tapsAreaHeight).reduced (12, 6));
}

//==============================================================================
/** 8.210：Phase 3の3つの箱（Phase 242）。

    **`paint()`と`resized()`が同じものを見ます**（1.27）——
    別々に数えると、**枠と中身がずれます。** */
juce::Array<juce::Rectangle<int>> MantaDelayEditor::getPhase3Columns (juce::Rectangle<int> band) const
{
    auto row = band.reduced (12, 6);

    constexpr int gap = 12;

    // つまみの数に合わせて配ります（Filter 3つ、Modulation 2つ、Ducking 3つ）。
    // 8.216：画面が広がったので**3:2:3で分けました**（Phase 243）——
    // 固定幅のままだと、増えたぶんが全部Duckingへ行って**箱の大きさが揃いません**
    const int spare = row.getWidth() - gap * 2;

    const int filterWidth = spare * 3 / 8;
    const int modulationWidth = spare * 2 / 8;

    juce::Array<juce::Rectangle<int>> columns;

    columns.add (row.removeFromLeft (filterWidth));
    row.removeFromLeft (gap);

    columns.add (row.removeFromLeft (modulationWidth));
    row.removeFromLeft (gap);

    columns.add (row);

    return columns;
}

void MantaDelayEditor::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (MantaPluginToolbar::preferredHeight));

    // ツールバーの右端に、テンポの状態
    {
        auto toolbarArea = toolbar.getLocalBounds().reduced (8, 0);
        statusLabel.setBounds (toolbarArea.removeFromRight (toolbarArea.getWidth() / 2));
    }

    // 8.210：Phase 3の帯（Phase 242）。**先に取り分けます**——
    // 残りを上のPhase 1・2が使う形なので、ここで取らないと下へはみ出します
    auto phase3Band = area.removeFromBottom (phase3AreaHeight);

    // 8.214：Phase 4の帯（Phase 243）。**下から順に積む**ので、Phase 3の次
    auto tapsBand = area.removeFromBottom (tapsAreaHeight);

    auto content = area.reduced (12, 8);

    //--------------------------------------------------------------------------
    // 8.217：`Echo`／`[A][B]`／`Routing`（Phase 244）

    {
        auto header = content.removeFromTop (headerHeight);

        echoTitle.setBounds (header.removeFromLeft (46).withSizeKeepingCentre (46, sectionTitleHeight));

        header.removeFromLeft (4);
        engineAButton.setBounds (header.removeFromLeft (30).reduced (0, 2));
        header.removeFromLeft (4);
        engineBButton.setBounds (header.removeFromLeft (30).reduced (0, 2));

        // **Routingは右端へ。** エンジンのつまみと混ざらない場所に置きます
        auto routing = header.removeFromRight (300);

        routingCaption.setBounds (routing.removeFromLeft (60));
        routing.removeFromLeft (6);
        routingBox.setBounds (routing.removeFromLeft (120).reduced (0, 2));
        routing.removeFromLeft (8);
        routingDescription.setBounds (routing);
    }

    content.removeFromTop (4);

    //--------------------------------------------------------------------------
    // 左：つまみの列

    auto knobs = content.removeFromLeft (knobPanelWidth);

    const auto placeKnobRow = [this] (juce::Rectangle<int> row,
                                       std::initializer_list<std::pair<ValueEntrySlider*, juce::Label*>> items)
    {
        const int count = (int) items.size();

        if (count <= 0)
            return;

        // 8.173：**余りは`count + 1`で割る**（Phase 214）。
        // `count - 1`で割ると端まで詰まり、**つまみが箱に貼り付いて見えます**
        const int spare = juce::jmax (0, row.getWidth() - knobWidth * count);
        const int gap = spare / (count + 1);

        row.removeFromLeft (gap);

        for (auto& item : items)
        {
            auto cell = row.removeFromLeft (knobWidth);

            item.second->setBounds (cell.removeFromTop (12));
            item.first->setBounds (cell);

            row.removeFromLeft (gap);
        }
    };

    // 8.217：**1段3つ**（Phase 244）。`Level`と`Pan`は**そのエンジンのもの**、
    // `Mix`と`Output`は**エンジン共通**です（下の段の右端だけが共通ではない、
    // という並びになりますが、そのためにPhase 1の2つを動かすことはしません）
    placeKnobRow (knobs.removeFromTop (knobHeight), { { &timeSlider, &timeCaption },
                                                       { &feedbackSlider, &feedbackCaption },
                                                       { &levelSlider, &levelCaption } });

    // 8.207：**音価のつまみはTimeと同じ場所へ**（Phase 239/本人の指定）。
    // どちらか片方しか出ていないので、重ねて置いて構いません——
    // **別の場所に置くと、Syncを入り切りするたびに画面が組み替わって見えます**
    divisionSlider.setBounds (timeSlider.getBounds());

    knobs.removeFromTop (6);

    // Sync と、8.219・8.221：Freeze／Reverse（Phase 246）
    {
        auto syncRow = knobs.removeFromTop (24);

        // 8.207：音価のコンボは廃止しました（Phase 239）。Timeのつまみが兼ねます。
        // **3つまとめて真ん中へ**（`withSizeKeepingCentre`を入れ子にしない。8.172）
        constexpr int buttonWidth = 82;
        constexpr int buttonGap = 6;

        auto row = syncRow.withSizeKeepingCentre (buttonWidth * 3 + buttonGap * 2, syncRow.getHeight());

        syncButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (buttonGap);
        freezeButton.setBounds (row.removeFromLeft (buttonWidth));
        row.removeFromLeft (buttonGap);
        reverseButton.setBounds (row.removeFromLeft (buttonWidth));
    }

    knobs.removeFromTop (10);

    placeKnobRow (knobs.removeFromTop (knobHeight), { { &mixSlider, &mixCaption },
                                                       { &outputSlider, &outputCaption },
                                                       { &panSlider, &panCaption } });

    // 8.218：`Cross`（Phase 245）。**残っている88pxにちょうど入ります**
    // （左の列は296pxのうち208pxを使っていました）
    knobs.removeFromTop (4);

    placeKnobRow (knobs.removeFromTop (knobHeight), { { &crossSlider, &crossCaption },
                                                       { &diffuseSlider, &diffuseCaption } });

    //--------------------------------------------------------------------------
    // 右：タイムライン表示

    content.removeFromLeft (12);

    // 8.208：**右側は上がディスプレイ、下がキャラクター**（Phase 240）。
    // つまみの列（左）はPhase 1のまま触っていません——
    // **段階を進めるたびに前の段階のものが動くと、覚え直しになります**
    {
        // 8.216：**34→16**（Phase 243）。中身は112pxしか使っていないので、
        // 余っていた22pxはディスプレイへ回します
        auto characterArea = content.removeFromBottom (knobHeight + sectionTitleHeight + 16);

        content.removeFromBottom (10);
        display.setBounds (content);

        auto titleRow = characterArea.removeFromTop (sectionTitleHeight);

        characterTitle.setBounds (titleRow.removeFromLeft (80));
        titleRow.removeFromLeft (8);
        characterBox.setBounds (titleRow.removeFromLeft (150).withTrimmedTop (-2).withTrimmedBottom (-4));

        characterArea.removeFromTop (10);

        placeKnobRow (characterArea.removeFromTop (knobHeight),
                       { { &driveSlider, &driveCaption },
                         { &toneSlider, &toneCaption },
                         { &wowDepthSlider, &wowDepthCaption },
                         { &wowRateSlider, &wowRateCaption },
                         { &flutterDepthSlider, &flutterDepthCaption },
                         { &flutterRateSlider, &flutterRateCaption } });
    }

    //--------------------------------------------------------------------------
    // 8.214〜8.215：Phase 4の帯（Phase 243）
    //
    // 左につまみ4つ、**残り全部が一覧**。一覧は広いほど読めます

    {
        auto inner = tapsBand.reduced (12, 6).reduced (8, 4);

        tapsTitle.setBounds (inner.removeFromTop (sectionTitleHeight));
        inner.removeFromTop (6);

        auto knobRow = inner.removeFromTop (knobHeight);

        // つまみ4つぶんをきっちり取って、**余りは一覧へ**
        const int knobsWidth = knobWidth * 4 + 24;

        placeKnobRow (knobRow.removeFromLeft (knobsWidth),
                       { { &tapCountSlider, &tapCountCaption },
                         { &tapStepSlider, &tapStepCaption },
                         { &tapLevelSlider, &tapLevelCaption },
                         { &tapPanSlider, &tapPanCaption } });

        knobRow.removeFromLeft (12);

        // **文字の行と高さを揃えます**（つまみの下の数字と一覧の数字が同じ高さに並ぶ）
        tapStrip.setBounds (knobRow.withTrimmedTop (12));
    }

    //--------------------------------------------------------------------------
    // 8.210〜8.212：Phase 3の帯（Phase 242）
    //
    // **箱の位置は`getPhase3Columns()`ただ1つ**が決めます（`paint()`も同じものを呼ぶ）

    {
        const auto columns = getPhase3Columns (phase3Band);

        jassert (columns.size() == 3);

        // 3つとも**同じ組み方**です：見出し → コンボの行 → つまみ1段。
        // 段の高さを揃えておかないと、つまみの高さが箱ごとに違って見えます
        const auto takeRows = [this] (juce::Rectangle<int> column,
                                       juce::Rectangle<int>& titleRow,
                                       juce::Rectangle<int>& comboRow,
                                       juce::Rectangle<int>& knobRow)
        {
            // **縦は4しか空けません。** 見出し18＋コンボ24＋間6＋つまみ84＝132に対して、
            // 箱の中は`phase3AreaHeight`(154) − 帯の余白(12) − ここ(8) = 134——
            // 6にすると2px足りず、**つまみの文字の下が切れます**
            auto inner = column.reduced (8, 4);

            titleRow = inner.removeFromTop (sectionTitleHeight);
            comboRow = inner.removeFromTop (phase3ComboRowHeight);

            inner.removeFromTop (6);

            knobRow = inner.removeFromTop (knobHeight);
        };

        juce::Rectangle<int> titleRow, comboRow, knobRow;

        //----------------------------------------------------------------------
        // 8.210：Filter

        takeRows (columns[0], titleRow, comboRow, knobRow);

        filterTitle.setBounds (titleRow);

        filterTypeBox.setBounds (comboRow.removeFromLeft (140));
        comboRow.removeFromLeft (8);
        filterPositionButton.setBounds (comboRow.removeFromLeft (64));

        placeKnobRow (knobRow, { { &filterFreqSlider, &filterFreqCaption },
                                  { &filterQSlider, &filterQCaption },
                                  { &filterGainSlider, &filterGainCaption } });

        //----------------------------------------------------------------------
        // 8.211：Modulation

        takeRows (columns[1], titleRow, comboRow, knobRow);

        modulationTitle.setBounds (titleRow);
        lfoShapeBox.setBounds (comboRow.removeFromLeft (120));

        placeKnobRow (knobRow, { { &lfoDepthSlider, &lfoDepthCaption },
                                  { &lfoRateSlider, &lfoRateCaption } });

        //----------------------------------------------------------------------
        // 8.212：Ducking
        //
        // **コンボの行には帯を置きます。** 3つのうちここだけ選ぶものが無いので、
        // 空けると箱の高さが揃っているのに中身が上へ寄って見えます

        takeRows (columns[2], titleRow, comboRow, knobRow);

        duckingTitle.setBounds (titleRow);
        duckMeter.setBounds (comboRow.withSizeKeepingCentre (comboRow.getWidth() - 4, 10));

        placeKnobRow (knobRow, { { &duckAmountSlider, &duckAmountCaption },
                                  { &duckAttackSlider, &duckAttackCaption },
                                  { &duckReleaseSlider, &duckReleaseCaption } });
    }
}
