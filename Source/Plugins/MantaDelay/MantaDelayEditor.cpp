#include "MantaDelayEditor.h"

#include "../MantaTheme.h"
#include "../../Branding.h"
#include "../../Utf8.h"
#include "MantaDelayCharacter.h"   // 8.208：キャラクターの名前と可否（Phase 240）

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
    };

    //--------------------------------------------------------------------------
    setupSectionLabel (echoTitle, "Echo");

    // **主の色は「反復そのものを決めるところ」**（8.204の表）
    setupKnob (timeSlider, timeCaption, "Time",
                MantaDelayParams::timeMs, MantaDelayTheme::accent());
    setupKnob (feedbackSlider, feedbackCaption, "Feedback",
                MantaDelayParams::feedback, MantaDelayTheme::accent());

    // **副の色は「原音との混ぜ具合」**
    setupKnob (mixSlider, mixCaption, "Mix",
                MantaDelayParams::mix, MantaDelayTheme::highlight());
    setupKnob (outputSlider, outputCaption, "Output",
                MantaDelayParams::outputGain, MantaDelayTheme::highlight());

    timeSlider.setTooltip (utf8 ("反復の間隔。Syncを入れると曲のテンポに合わせます"));
    feedbackSlider.setTooltip (utf8 ("返ってきた音をどれだけ戻すか。上げるほど長く反復します"));
    mixSlider.setTooltip (utf8 ("原音とディレイ音の混ぜ具合"));

    //--------------------------------------------------------------------------
    // テンポシンク

    MantaPluginToolbar::styleButton (syncButton, "Sync");
    syncButton.setClickingTogglesState (true);
    syncButton.setColour (juce::TextButton::buttonOnColourId, MantaDelayTheme::highlight());
    addAndMakeVisible (syncButton);

    syncAttachment = std::make_unique<ButtonAttachment> (
        processor.getValueTreeState(), MantaDelayParams::sync, syncButton);

    // **`onClick`ではなく`onStateChange`。** オートメーションや
    // プリセットの読み込みで変わったときにも呼ばれます（押したときだけではない）
    syncButton.onStateChange = [this] { refreshTimeControls(); };

    // 8.207：**Syncのときは、同じ場所が音価のつまみになります**（Phase 239/本人の指定）。
    // `setupKnob()`を通すので、**大きさも見た目もTimeと同じ**です
    setupKnob (divisionSlider, timeCaption, "Time",
                MantaDelayParams::syncDivision, MantaDelayTheme::accent());

    // **つなぎ先はこちらで持ちます**（`setupKnob()`が`sliderAttachments`へ入れたぶんは
    // そのままで構いません——出し分けるのは見た目だけで、繋ぎは両方生かしておきます）

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

    characterAttachment = std::make_unique<ComboAttachment> (
        processor.getValueTreeState(), MantaDelayParams::character, characterBox);

    // **`onChange`はオートメーションやプリセットの読み込みでも呼ばれます**
    characterBox.onChange = [this] { refreshCharacterControls(); };

    // **色の分け方は上と同じ**（主＝音を作るところ、副＝揺らすところ）
    setupKnob (driveSlider, driveCaption, "Drive",
                MantaDelayParams::drive, MantaDelayTheme::accent());
    setupKnob (toneSlider, toneCaption, "Tone",
                MantaDelayParams::tone, MantaDelayTheme::accent());

    setupKnob (wowDepthSlider, wowDepthCaption, "Wow",
                MantaDelayParams::wowDepth, MantaDelayTheme::highlight());
    setupKnob (wowRateSlider, wowRateCaption, "Wow Rate",
                MantaDelayParams::wowRate, MantaDelayTheme::highlight());
    setupKnob (flutterDepthSlider, flutterDepthCaption, "Flutter",
                MantaDelayParams::flutterDepth, MantaDelayTheme::highlight());
    setupKnob (flutterRateSlider, flutterRateCaption, "Flutter Rate",
                MantaDelayParams::flutterRate, MantaDelayTheme::highlight());

    driveSlider.setTooltip (utf8 ("Tapeでは飽和の強さ、Lo-Fiでは削り具合"));
    toneSlider.setTooltip (utf8 ("反復するたびに落ちる高域の量"));

    refreshTimeControls();
    refreshCharacterControls();

    setSize (fixedWidth, fixedHeight);
    setResizable (false, false);   // 8.172：内蔵プラグインの画面は固定

    startTimerHz (30);
}

MantaDelayEditor::~MantaDelayEditor()
{
    // **つまみからLookAndFeelを外してから壊すこと**（8.168）
    for (auto* slider : { &timeSlider, &divisionSlider, &feedbackSlider, &mixSlider, &outputSlider,
                           &driveSlider, &toneSlider, &wowDepthSlider, &wowRateSlider,
                           &flutterDepthSlider, &flutterRateSlider })
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void MantaDelayEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption,
                                   const juce::String& text, const char* parameterId,
                                   juce::Colour colour)
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

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(), parameterId, slider));
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

    // **消さずにグレーアウトする**（本人の判断）。
    //
    // 隠すと画面は片付きますが、**何が隠れているか分かりません。**
    // グレーアウトなら「そのキャラクターに何が無いか」が見えます——
    // Digital Cleanを選ぶと4つとも灰色になり、「色付けなし」の意味がそのまま出ます
    const auto setActive = [] (ValueEntrySlider& slider, juce::Label& caption, bool active)
    {
        slider.setEnabled (active);
        slider.setAlpha (active ? 1.0f : 0.4f);
        caption.setAlpha (active ? 1.0f : 0.4f);
    };

    setActive (driveSlider, driveCaption, capabilities.drive);
    setActive (toneSlider, toneCaption, capabilities.tone);
    setActive (wowDepthSlider, wowDepthCaption, capabilities.wow);
    setActive (wowRateSlider, wowRateCaption, capabilities.wow);
    setActive (flutterDepthSlider, flutterDepthCaption, capabilities.flutter);
    setActive (flutterRateSlider, flutterRateCaption, capabilities.flutter);
}
//==============================================================================

void MantaDelayEditor::timerCallback()
{
    MantaDelayDisplay::State state;

    // **つまみの値ではなく、エンジンがいま鳴らしている値**（1.27）
    state.delaySeconds = processor.getCurrentDelaySeconds();
    state.feedback = feedbackSlider.getValue();
    state.mix = mixSlider.getValue();

    display.setState (state);

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

    // Phase 2以降の場所。**黙って空けない**（何も無い灰色の面は「壊れている」ようにも見える）
    auto future = area.removeFromBottom (futureAreaHeight).reduced (12, 8);

    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (future.toFloat(), 4.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (future.toFloat().reduced (0.5f), 4.0f, 1.0f);

    g.setColour (MantaTheme::textDim().withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    // 8.208：**Characterは入ったので消しました**（Phase 240）
    g.drawText ("Filter  ·  LFO  ·  Ducking  ·  Multi-Tap  ·  Dual Engine  ·  Reverse",
                 future.removeFromTop (future.getHeight() / 2),
                 juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (utf8 ("（Phase 3以降）"), future, juce::Justification::centred);
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

    area.removeFromBottom (futureAreaHeight);   // Phase 2以降の場所（`paint()`が描く）

    auto content = area.reduced (12, 8);

    echoTitle.setBounds (content.removeFromTop (sectionTitleHeight));
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

    placeKnobRow (knobs.removeFromTop (knobHeight), { { &timeSlider, &timeCaption },
                                                       { &feedbackSlider, &feedbackCaption } });

    // 8.207：**音価のつまみはTimeと同じ場所へ**（Phase 239/本人の指定）。
    // どちらか片方しか出ていないので、重ねて置いて構いません——
    // **別の場所に置くと、Syncを入り切りするたびに画面が組み替わって見えます**
    divisionSlider.setBounds (timeSlider.getBounds());

    knobs.removeFromTop (6);

    // Sync と音価
    {
        auto syncRow = knobs.removeFromTop (24);

        // 8.207：音価のコンボは廃止しました（Phase 239）。Timeのつまみが兼ねます
        syncButton.setBounds (syncRow.withSizeKeepingCentre (80, syncRow.getHeight()));
    }

    knobs.removeFromTop (10);

    placeKnobRow (knobs.removeFromTop (knobHeight), { { &mixSlider, &mixCaption },
                                                       { &outputSlider, &outputCaption } });

    //--------------------------------------------------------------------------
    // 右：タイムライン表示

    content.removeFromLeft (12);

    // 8.208：**右側は上がディスプレイ、下がキャラクター**（Phase 240）。
    // つまみの列（左）はPhase 1のまま触っていません——
    // **段階を進めるたびに前の段階のものが動くと、覚え直しになります**
    {
        auto characterArea = content.removeFromBottom (knobHeight + sectionTitleHeight + 34);

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
}
