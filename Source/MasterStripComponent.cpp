#include "MasterStripComponent.h"
#include "AppColours.h"
#include "AudioEngine.h"
#include "Utf8.h"
#include "DragAndDropIds.h"
#include "PluginManager.h"

MasterStripComponent::MasterStripComponent (ProjectModel& projectToUse, AudioEngine& audioEngineToUse)
    : project (projectToUse), audioEngine (audioEngineToUse),
      masterBusState (projectToUse.getOrCreateMasterBusNode())
{
    nameLabel.setText ("Master", juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addAndMakeVisible (nameLabel);

    volumeSlider.setRange (-60.0, 6.0, 0.1);

    // Phase 62：ダブルクリック＝0dBへ（JUCE標準）。数値入力はdB表示の右クリック（8.24）。
    // **チャンネルストリップと同じ扱いにすること**（片方だけ違うと迷う）
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setDefaultValueDescription ("0 dB");
    volumeSlider.setTooltip (utf8 ("マスター音量（dB）。ダブルクリックで0dB／右クリックでメニュー。"
                                    "数値は下のdB表示をクリックしても打ち込めます"));
    volumeSlider.setColour (juce::Slider::trackColourId, AppColours::purple);
    volumeSlider.onDragStart = [this]
    {
        project.beginAction (utf8 ("マスター音量の変更"));

        // 仕様書5.6：Touch/Latchの記録開始判定（Phase 20）。
        // マスターはtrackIdを空文字で表す（AudioEngine側の約束）。
        audioEngine.beginAutomationTouch ({}, AutomationTargets::volume);
    };
    volumeSlider.onDragEnd = [this] { audioEngine.endAutomationTouch ({}, AutomationTargets::volume); };
    volumeSlider.onValueChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        project.setMasterVolumeDb ((float) volumeSlider.getValue(), &project.getUndoManager());

        if (onMixerValueChanged != nullptr)
            onMixerValueChanged();
    };
    addAndMakeVisible (volumeSlider);

    volumeValueLabel.setJustificationType (juce::Justification::centred);
    volumeValueLabel.setFont (juce::FontOptions (11.0f));
    volumeValueLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);

    // 仕様書5.7：dB表示が音量の入口（Phase 62／Phase 63で操作を揃えた。8.24）。
    // **チャンネルストリップと同じ扱いにすること**（片方だけ違うと迷う）
    volumeValueLabel.setTooltip (utf8 ("クリックしてマスター音量（dB）を打ち込む／右クリックでメニュー"));
    volumeValueLabel.onLeftClick = [this] { volumeValueLabel.showEditor(); };
    volumeValueLabel.onRightClick = [this] { volumeSlider.showValueMenu(); };

    volumeValueLabel.onEditorShow = [this]
    {
        if (auto* editor = volumeValueLabel.getCurrentTextEditor())
        {
            editor->setText (juce::String (project.getMasterVolumeDb(), 1), false);
            editor->selectAll();
        }
    };

    volumeValueLabel.onTextChange = [this]
    {
        volumeSlider.applyTextValue (volumeValueLabel.getText());
        updateControlsFromModel();
    };

    addAndMakeVisible (volumeValueLabel);

    meter.setVertical (true);
    meter.setShowPeakText (true);   // 仕様書5.7：ピークのdB表示（Phase 59）
    addAndMakeVisible (meter);

    // 仕様書5.6：マスターの書き込みモード（Phase 20）
    automationModeBox.addItem ("Read", 1);
    automationModeBox.addItem ("Touch", 2);
    automationModeBox.addItem ("Latch", 3);
    automationModeBox.addItem ("Write", 4);
    automationModeBox.onChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        const auto mode = static_cast<AutomationMode> (juce::jlimit (0, 3, automationModeBox.getSelectedId() - 1));

        project.beginAction (utf8 ("オートメーションモードの変更"));
        project.setMasterAutomationMode (mode, &project.getUndoManager());
    };
    addAndMakeVisible (automationModeBox);

    // 仕様書5.7.1：PDCの補正量（Phase 12e）
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setFont (juce::FontOptions (10.0f));
    latencyLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (latencyLabel);

    // 8.69：マスターへのインサート（Phase 108／D6）
    rackViewport.setScrollBarsShown (true, false);
    rackViewport.setScrollBarThickness (8);
    addAndMakeVisible (rackViewport);
    rebuildRack();

    masterBusState.addListener (this);
    updateControlsFromModel();
    refreshLatencyDisplay();
}

void MasterStripComponent::rebuildRack()
{
    // 8.69：**作り直す**（Phase 108／D6）。プロジェクトを開き直すと`<MASTERBUS>`ごと
    // 差し替わるので、付け替えではなく作り直しにしてある（8.19のストリップと同じ扱い）
    rack = TrackRackComponent::createForMasterBus (project, audioEngine,
                                                    TrackRackComponent::Layout::Strip);

    rack->onMixerValueChanged = [this]
    {
        if (onMixerValueChanged != nullptr)
            onMixerValueChanged();
    };

    rack->onPreferredHeightChanged = [this] { resized(); };

    rackViewport.setViewedComponent (rack.get(), false);
    resized();
}

void MasterStripComponent::refreshLatencyDisplay()
{
    const int samples = audioEngine.getTotalLatencySamples();

    if (samples == lastShownLatencySamples)
        return; // 変わっていなければ描き直さない（タイマーから毎回呼ばれるため）

    lastShownLatencySamples = samples;

    const double sampleRate = audioEngine.getCurrentSampleRate();
    const double ms = sampleRate > 0.0 ? (samples * 1000.0 / sampleRate) : 0.0;

    latencyLabel.setText ("PDC " + juce::String (ms, 1) + " ms", juce::dontSendNotification);

    // 補正が効いている間は色を変えて、「今どれくらいずれを吸収しているか」を目立たせる
    latencyLabel.setColour (juce::Label::textColourId,
                             samples > 0 ? AppColours::orange : AppColours::textSecondary);

    // サンプル数はツールチップに回す（幅が狭く、両方は並べられないため）
    latencyLabel.setTooltip (utf8 ("プラグイン遅延補正（仕様書5.7.1）: ")
                               + juce::String (samples) + utf8 (" サンプル"));
}

MasterStripComponent::~MasterStripComponent()
{
    masterBusState.removeListener (this);
}

void MasterStripComponent::refreshAfterProjectChanged()
{
    masterBusState.removeListener (this);
    masterBusState = project.getOrCreateMasterBusNode();
    masterBusState.addListener (this);

    rebuildRack();   // 8.69：ラックも新しい`<MASTERBUS>`へ付け替える（Phase 108／D6）

    updateControlsFromModel();
}

void MasterStripComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    updateControlsFromModel();
}

void MasterStripComponent::updateControlsFromModel()
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    // 8.54：**その再生位置で効いている値**を出す（Phase 93）。
    // オートメーションが無ければ`getMasterVolumeDb()`と同じものが返る
    const float volumeDb = project.getEffectiveMasterVolumeDbAt (playheadSeconds);

    // 掴んでいる最中のつまみは動かさない（触っている人が優先）
    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (volumeDb, juce::dontSendNotification);

    volumeValueLabel.setText (volumeDb <= -60.0f ? utf8 ("-∞ dB") : juce::String (volumeDb, 1) + " dB",
                               juce::dontSendNotification);

    // 仕様書5.6：Read以外は色を変えて「今動かすと記録される」ことを示す（Phase 20）
    const auto automationMode = project.getMasterAutomationMode();
    automationModeBox.setSelectedId ((int) automationMode + 1, juce::dontSendNotification);
    automationModeBox.setColour (juce::ComboBox::backgroundColourId,
                                  automationMode == AutomationMode::Read ? AppColours::background
                                                                          : AppColours::orange);
}

void MasterStripComponent::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (seconds, playheadSeconds))
        return;

    playheadSeconds = seconds;

    // 点が書かれていないときは何もしない（再生中は毎フレーム来る）。
    // 8.56：レーンは**開いただけで空のまま存在し得る**ので、「点があるか」で見る
    if (! project.findMasterAutomationLane (AutomationTargets::volume).isEmpty())
        updateControlsFromModel();
}

void MasterStripComponent::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (2.0f);

    // マスターは他のトラックと役割が違うので、背景を少し濃くして区別する
    g.setColour (AppColours::border.withAlpha (0.55f));
    g.fillRoundedRectangle (area, AppColours::corner (4.0f));

    // 8.69：ドラッグ中の落とし先をパープルで示す（Phase 108／D6。ストリップと同じ見せ方）
    if (isDragOver)
    {
        g.setColour (AppColours::purple.withAlpha (0.20f));
        g.fillRoundedRectangle (area, AppColours::corner (4.0f));
    }

    g.setColour (isDragOver ? AppColours::purple : AppColours::border);
    g.drawRoundedRectangle (area.reduced (0.5f), AppColours::corner (4.0f), isDragOver ? 2.0f : 1.0f);
}

//==============================================================================
// 8.69：ブラウザからマスターへプラグインを落とす（Phase 108／D6。仕様書4.4）
//==============================================================================

bool MasterStripComponent::isInterestedInDragSource (const SourceDetails& details)
{
    // **判定はTrackRackComponentに集めてある**（トラックと同じ経路を通す。8.2）。
    // マスターは音源を持てないので、そこだけここで弾く
    if (rack == nullptr)
        return false;

    juce::PluginDescription description;

    if (! DragAndDropIds::findPluginByIdentifier (audioEngine.getPluginManager().getKnownPlugins(),
                                                   DragAndDropIds::getPluginIdentifier (details.description),
                                                   description))
        return false;

    return ! description.isInstrument;
}

void MasterStripComponent::itemDragEnter (const SourceDetails&)
{
    isDragOver = true;
    repaint();
}

void MasterStripComponent::itemDragExit (const SourceDetails&)
{
    isDragOver = false;
    repaint();
}

void MasterStripComponent::itemDropped (const SourceDetails& details)
{
    isDragOver = false;
    repaint();

    juce::PluginDescription description;

    if (! DragAndDropIds::findPluginByIdentifier (audioEngine.getPluginManager().getKnownPlugins(),
                                                   DragAndDropIds::getPluginIdentifier (details.description),
                                                   description))
        return;

    if (description.isInstrument)
        return;

    // 8.69：**トラックIDは空文字＝マスター**（Phase 108／D6）
    const auto error = audioEngine.addInsertToTrack ({}, description);

    if (error.isNotEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("プラグインを挿入できませんでした"))
                .withMessage (error)
                .withButton ("OK"),
            nullptr);
        return;
    }

    if (rack != nullptr)
        rack->refreshInsertSlots();

    // 設計書3.7：挿したらそのまま音作りに入れるようGUIを開く
    const int numInserts = project.getMasterBusInsertHost().getNumInserts();

    if (numInserts > 0)
        audioEngine.openInsertEditor ({}, numInserts - 1);
}

void MasterStripComponent::resized()
{
    auto area = getLocalBounds().reduced (6);

    nameLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    // 仕様書5.6：書き込みモード（Phase 20）
    automationModeBox.setBounds (area.removeFromTop (20));
    area.removeFromTop (4);

    // 仕様書5.7.1：PDCの補正量（Phase 12e）
    latencyLabel.setBounds (area.removeFromTop (14));
    area.removeFromTop (6);

    // 8.69：マスターのインサート（Phase 108／D6）。
    // **フェーダーとメーターのぶんは必ず残す**（`ChannelStripComponent`と同じ決まり。
    // 8.1のC14）。狭いときは折半して、スロットが1つも見えない状態を避ける
    if (rack != nullptr)
    {
        const int faderRoom = juce::jmin (minimumFaderAreaHeight, area.getHeight() / 2);
        const int roomForRack = juce::jmax (0, area.getHeight() - faderRoom);
        const int wantedRackHeight = rack->getPreferredHeight (area.getWidth());
        const int rackHeight = juce::jmin (wantedRackHeight, roomForRack);

        rackViewport.setBounds (area.removeFromTop (rackHeight));

        // **中身の高さは「欲しい高さ」のまま**にすること。枠に合わせて縮めると
        // スクロールしても下のスロットへ届かない（1.21）
        rack->setSize (rackViewport.getMaximumVisibleWidth(), wantedRackHeight);

        area.removeFromTop (6);
    }

    volumeValueLabel.setBounds (area.removeFromBottom (16));

    meter.setBounds (area.removeFromRight (30));   // Phase 59：ピーク表示ぶん広げた
    area.removeFromRight (4);
    volumeSlider.setBounds (area);
}
