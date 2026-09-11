#include "MantaDelayEditor.h"

#include "../MantaTheme.h"
#include "../../Branding.h"
#include "../../Utf8.h"

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

    toolbar.onStateRestored = [this] { refreshTimeControls(); };

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

    refreshTimeControls();

    setSize (fixedWidth, fixedHeight);
    setResizable (false, false);   // 8.172：内蔵プラグインの画面は固定

    startTimerHz (30);
}

MantaDelayEditor::~MantaDelayEditor()
{
    // **つまみからLookAndFeelを外してから壊すこと**（8.168）
    for (auto* slider : { &timeSlider, &divisionSlider, &feedbackSlider, &mixSlider, &outputSlider })
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
    g.drawText ("Character  ·  Filter  ·  Modulation  ·  Ducking  ·  Multi-Tap  ·  Dual Engine",
                 future.removeFromTop (future.getHeight() / 2),
                 juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (utf8 ("（Phase 2以降）"), future, juce::Justification::centred);
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
    display.setBounds (content);
}
