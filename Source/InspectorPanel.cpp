#include "InspectorPanel.h"
#include "AppColours.h"
#include "AudioEngine.h"
#include "PanelResizerBar.h"
#include "Utf8.h"

namespace
{
    /** 設計書2.4：トラックカラーの見本。ユーザー設定色はここから選ぶ。

        8.61：**見本を持つのは`Track::getColourPalette()`1箇所**にした（Phase 99／改善案⑫）。
        新しいトラックの既定色も同じ見本から配るので、
        「見本には無い色が既定で入っている」が起きない（1.27）。 */
    juce::Array<juce::Colour> getTrackColourSwatches()
    {
        juce::Array<juce::Colour> colours;

        for (const auto& hex : Track::getColourPalette())
            colours.add (juce::Colour::fromString (hex));

        return colours;
    }

    const juce::Array<juce::Colour> trackColourSwatches = getTrackColourSwatches();

    juce::String formatSeconds (double seconds)
    {
        return juce::String (seconds, 3) + " s";
    }

    /** 8.60：**音量フェーダーを出すトラックか**（Phase 97／改善案⑪）。

        コードトラックは音を通さないので、音量・パン・ソロ／ミュートのどれも意味を持ちません。
        それまでは種別を見ずに全部出していて、**動かしても何も起きないつまみ**が並んでいました。

        8.296：**残っているのはこれ1つ**（Phase 289）。パン・ソロ／ミュート・
        メーター・ラックの出し分けは、**`ChannelStripComponent`の中へ移りました**
        ——同じ判断を2箇所に書いていたので、片方だけ直すと
        Consoleとインスペクタで見え方が食い違います（1.27）。
        ここが残るのは、**ストリップを作るかどうか**を決めるためです。 */
    bool trackHasVolume (const Track& track)
    {
        return track.getType() != TrackType::Chord;
    }
}

//==============================================================================
InspectorPanel::InspectorPanel (ProjectModel& projectToUse, SelectionState& selectionToUse,
                                  AudioEngine& audioEngineToUse)
    : project (projectToUse), selection (selectionToUse), audioEngine (audioEngineToUse)
{
    titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addAndMakeVisible (titleLabel);

    // 載せる要素が増えたため、中身はスクロールできるようにしてある（Phase 29）
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    emptyLabel.setJustificationType (juce::Justification::centredTop);
    emptyLabel.setFont (juce::FontOptions (12.0f));
    emptyLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    emptyLabel.setText (utf8 ("トラックやクリップを選ぶと\nここに設定が出ます。"),
                         juce::dontSendNotification);
    content.addAndMakeVisible (emptyLabel);

    //==========================================================================
    // トラック用
    auto setUpCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, AppColours::textSecondary);
        content.addChildComponent (label);
    };

    setUpCaption (nameCaption, utf8 ("名前"));

    // 8.53：**名前は、名前の欄を押したときだけ編集できる**（Phase 92）。
    // `TextEditor`のままだと、ウィンドウが前面に来ただけで入力欄として選ばれ、
    // **インスペクタのどこを触っても名前が選択された状態**になっていました
    nameEditor.setEditable (true, true, false);   // 1回のクリックで編集に入る
    nameEditor.setColour (juce::Label::backgroundColourId, AppColours::background);
    nameEditor.setColour (juce::Label::outlineColourId, AppColours::border);
    nameEditor.setColour (juce::Label::textColourId, AppColours::textPrimary);
    nameEditor.setFont (juce::FontOptions (13.0f));
    nameEditor.onTextChange = [this] { nameChanged(); };
    content.addChildComponent (nameEditor);

    typeLabel.setFont (juce::FontOptions (11.0f));
    typeLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    typeLabel.setJustificationType (juce::Justification::centredRight);
    content.addChildComponent (typeLabel);

    setUpCaption (colourCaption, utf8 ("色"));

    for (int i = 0; i < trackColourSwatches.size(); ++i)
    {
        auto* swatch = colourSwatches.add (new ColourSwatchButton());
        swatch->swatchColour = trackColourSwatches[i];
        swatch->onClick = [this, i] { colourSwatchClicked (i); };
        content.addChildComponent (swatch);
    }

    // 8.296：**音量・パン・メーター・ミュート／ソロは、Consoleのストリップごと置きます**
    // （Phase 289／本人の指定の絵）。
    //
    // Phase 288まで、ここには**同じ役目の部品がもう1組**ありました——
    // 横向きのフェーダー、横向きのパン、横向きのメーター、M と S。
    // 値も操作もConsoleと同じになるように、**2箇所を見比べながら**直していました
    // （8.19・8.61で同じことをやっています）。
    //
    // いまは`ChannelStripComponent`を1つ置くだけです。作り直すのは
    // `rebuildForSelection()`——担当トラックはコンストラクタで決まる作りなので、
    // 付け替えより作り直しのほうが購読の外し忘れが起きません（ラックと同じ）。

    //==========================================================================
    // Phase 33：トラックの削除。
    // 8.60：**「上へ」「下へ」は外しました**（Phase 97）。並べ替えはヘッダーのドラッグで
    // やるものになっていて、1マスずつ動かすボタンは使われていませんでした
    //
    // 設計書2.6：取り返しの付きにくい操作はオレンジ（録音・警告と同じ扱い）
    deleteTrackButton.setButtonText (utf8 ("削除"));
    deleteTrackButton.setColour (juce::TextButton::textColourOffId, AppColours::orange);
    deleteTrackButton.setTooltip (utf8 ("このトラックを削除（Ctrl+Zで取り消せます）"));
    deleteTrackButton.onClick = [this] { deleteSelectedTrack(); };
    content.addChildComponent (deleteTrackButton);

    // 8.59：レーンの色を親トラックの色へ戻す（Phase 96）
    resetLaneColourButton.setButtonText (utf8 ("トラックの色に戻す"));
    resetLaneColourButton.setTooltip (utf8 ("この行だけの色をやめ、トラックの色に合わせます"));
    resetLaneColourButton.onClick = [this]
    {
        auto lane = getSelectedAutomationLane();

        if (! lane.state.isValid())
            return;

        project.beginAction (utf8 ("オートメーションの色を戻す"));
        lane.clearColour (&project.getUndoManager());
        updateControlsFromModel();
    };
    content.addChildComponent (resetLaneColourButton);

    //==========================================================================
    // クリップ用
    setUpCaption (clipStartCaption, utf8 ("開始位置"));
    setUpCaption (clipLengthCaption, utf8 ("長さ"));
    setUpCaption (clipExtraCaption, {});

    for (auto* label : { &clipStartLabel, &clipLengthLabel, &clipExtraLabel })
    {
        label->setFont (juce::FontOptions (12.0f));
        label->setColour (juce::Label::textColourId, AppColours::textPrimary);
        content.addChildComponent (label);
    }

    // 8.84：MIDIトラックの入力設定（Phase 124／改善案⑯。仕様書5.4）
    setUpCaption (midiInputCaption, utf8 ("MIDI入力（デバイス／チャンネル）"));
    setUpCaption (midiOutputCaption, utf8 ("音源へ送るチャンネル"));

    for (auto* box : { &midiInputDeviceBox, &midiInputChannelBox, &midiOutputChannelBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, AppColours::background);
        box->setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
        box->setColour (juce::ComboBox::outlineColourId, AppColours::border);
        box->setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);
        content.addChildComponent (*box);
    }

    // **IDは1始まり**（ComboBoxはID=0を「選択なし」に使う）。
    // 入力チャンネルは 1＝すべて、2〜17＝1〜16ch
    midiInputChannelBox.addItem (utf8 ("すべて"), 1);

    for (int channel = 1; channel <= 16; ++channel)
        midiInputChannelBox.addItem ("Ch " + juce::String (channel), channel + 1);

    midiInputChannelBox.setTooltip (utf8 ("このトラックが受け取るMIDIチャンネル"));

    // 送るチャンネルは 1＝そのまま、2〜17＝1〜16ch
    midiOutputChannelBox.addItem (utf8 ("そのまま"), 1);

    for (int channel = 1; channel <= 16; ++channel)
        midiOutputChannelBox.addItem ("Ch " + juce::String (channel), channel + 1);

    midiOutputChannelBox.setTooltip (utf8 ("音源へ渡すときのMIDIチャンネル"
                                            "（マルチティンバー音源でパートを分けるときに使います）"));

    midiInputDeviceBox.setTooltip (utf8 ("このトラックが受け取るMIDI入力デバイス"
                                          "（有効にするデバイスは環境設定のAudioで選びます）"));

    midiInputDeviceBox.onChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        auto track = getSelectedTrack();

        if (! track.state.getParent().isValid())
            return;

        // ID=1が「すべて」。それ以外は項目の文字がそのままデバイス名
        const auto deviceName = midiInputDeviceBox.getSelectedId() <= 1
                                    ? juce::String()
                                    : midiInputDeviceBox.getText();

        project.beginAction (utf8 ("MIDI入力デバイスの変更"));
        track.setMidiInputDeviceName (deviceName, &project.getUndoManager());
    };

    midiInputChannelBox.onChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        auto track = getSelectedTrack();

        if (! track.state.getParent().isValid())
            return;

        project.beginAction (utf8 ("MIDI入力チャンネルの変更"));
        track.setMidiInputChannel (midiInputChannelBox.getSelectedId() - 1, &project.getUndoManager());
    };

    midiOutputChannelBox.onChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        auto track = getSelectedTrack();

        if (! track.state.getParent().isValid())
            return;

        project.beginAction (utf8 ("音源へ送るチャンネルの変更"));
        track.setMidiOutputChannel (midiOutputChannelBox.getSelectedId() - 1, &project.getUndoManager());
    };

    // 8.77：MIDIクリップのトランスポーズ（Phase 117／改善案㉝）。
    // **動かすのはアレンジ画面**（`onTransposeRequested`）——
    // 「どのクリップに効かせるか」の判断をここに書くと、メニューと食い違う
    setUpCaption (transposeCaption, utf8 ("トランスポーズ"));

    struct { const char* label; int semitones; } transposeSteps[] =
    {
        { "-12", -12 }, { "-1", -1 }, { "+1", 1 }, { "+12", 12 },
    };

    for (const auto& step : transposeSteps)
    {
        auto* button = transposeButtons.add (new juce::TextButton (step.label));
        button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        button->setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);

        const int semitones = step.semitones;
        button->onClick = [this, semitones]
        {
            if (onTransposeRequested != nullptr)
                onTransposeRequested (semitones);
        };

        content.addChildComponent (button);
    }

    selection.addChangeListener (this);
    rebuildForSelection();
}

InspectorPanel::~InspectorPanel()
{
    stopTimer();   // 仕様書5.7：破棄後に呼ばれないよう、購読より先に止める（Phase 59）
    selection.removeChangeListener (this);
    subscribedProjectState.removeListener (this);
}

//==============================================================================
// 仕様書5.7：レベルメーター（Phase 59／8.1のC4）

bool InspectorPanel::shouldRunMeter() const
{
    // **見えていないときに回さない**（ConsoleViewと同じ方針）。
    // パネルを閉じている間もエンジンへ問い合わせ続けるのは無駄。
    //
    // 8.296：見るのは**ストリップがあるかどうか**（Phase 289）。
    // メーターを持つかどうかの判断はストリップの中にあります
    return isVisible() && strip != nullptr;
}

void InspectorPanel::updateMeterTimer()
{
    if (shouldRunMeter())
        startTimerHz (30);
    else
        stopTimer();
}

void InspectorPanel::visibilityChanged()
{
    updateMeterTimer();
}

void InspectorPanel::timerCallback()
{
    if (strip == nullptr)
        return;

    // 並び順ではなくtrackIdでエンジンへ問い合わせる（ConsoleViewと同じ理由）
    const auto trackId = strip->getTrackId();

    strip->setLevels (audioEngine.getTrackLevel (trackId, 0),
                       audioEngine.getTrackLevel (trackId, 1));

    // 8.295：音源GUIが出ているかどうか（Phase 288）。
    // **窓は画面の外で閉じられます**ので、こちらから見に行きます
    strip->refreshInstrumentEditorState();

    // 仕様書5.7.1：レイテンシもここで読み直す（ConsoleViewと同じ経路）
    strip->refreshLatencyDisplay();
}

Track InspectorPanel::getSelectedTrack() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getId() == selection.getTrackId())
            return track;
    }

    return Track (juce::ValueTree (IDs::TRACK)); // 見つからない場合の空トラック
}

void InspectorPanel::updateLaneColourSwatches (const AutomationLane& lane)
{
    // 8.59：**色を持っていなければ、どの見本にも枠を付けない**（Phase 96）。
    // 親から受け継いでいる状態と「たまたま同じ色を選んだ」状態は別物なので、
    // 見た目でも分かるようにしておく（戻すボタンの有効／無効も同じ判断）
    const bool hasCustom = lane.state.isValid() && lane.hasCustomColour();
    const auto currentColour = hasCustom ? juce::Colour::fromString (lane.getColourString())
                                          : juce::Colour();

    for (int i = 0; i < colourSwatches.size(); ++i)
        colourSwatches[i]->setToggleState (hasCustom && trackColourSwatches[i] == currentColour,
                                            juce::dontSendNotification);

    resetLaneColourButton.setEnabled (hasCustom);
}

AutomationLane InspectorPanel::getSelectedAutomationLane() const
{
    if (! selection.isAutomationLaneSelected())
        return AutomationLane (juce::ValueTree());

    const auto targetId = selection.getAutomationTargetId();

    // **trackIdが空文字ならマスター**（SelectionStateの約束）
    if (selection.getTrackId().isEmpty())
        return project.findMasterAutomationLane (targetId);

    return getSelectedTrack().findAutomationLane (targetId);
}

void InspectorPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildForSelection();
}

void InspectorPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    // ルートを購読しているので、他のトラックの編集やノートの打ち込みまで届く（Phase 34）。
    // 表示に関係するのは、選択中のトラックの中で起きた変化だけ。
    //
    // 8.59：**マスターのレーンはトラックの下に無い**ので、別に見る（Phase 96）
    const bool touchesLane = selectedLaneState.isValid()
                               && (tree == selectedLaneState || tree.isAChildOf (selectedLaneState));

    if (! touchesLane)
    {
        if (! selectedTrackState.isValid())
            return;

        if (tree != selectedTrackState && ! tree.isAChildOf (selectedTrackState))
            return;
    }

    // Undo/Redoや、ミキサー側での変更にも追従する
    updateControlsFromModel();
}

void InspectorPanel::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // 並べ替えで「何番目か」が変わると、「上へ／下へ」を押せるかも変わる（Phase 34）
    if (parent.hasType (IDs::TRACKS))
        updateControlsFromModel();
}

void InspectorPanel::refreshAfterProjectChanged()
{
    // プロジェクトが差し替わると、覚えていたtrackIdは別プロジェクトのものになる
    rebuildForSelection();
}

void InspectorPanel::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (seconds, playheadSeconds))
        return;

    playheadSeconds = seconds;

    // 8.296：**ストリップへも渡すこと**（Phase 289）。
    // フェーダーとパンを持っているのはあちらで、**素通りできるかの判断も
    // あちらが持っています**（`ChannelStripComponent::setPlayheadSeconds()`）
    if (strip != nullptr)
        strip->setPlayheadSeconds (seconds);

    // 8.54：**オートメーションのあるトラックを選んでいるときだけ**引き直す（Phase 93）。
    // 再生中は毎フレーム来るので、無条件に流し込むと選択中の値が毎回書き直される
    auto track = getSelectedTrack();

    if (! track.state.getParent().isValid())
        return;

    // 8.56：レーンは**開いただけで空のまま存在し得る**ので、「点があるか」で見る（Phase 94）
    if (! track.findAutomationLane (AutomationTargets::volume).isEmpty()
         || ! track.findAutomationLane (AutomationTargets::pan).isEmpty())
        updateControlsFromModel();
}

void InspectorPanel::rebuildForSelection()
{
    auto track = getSelectedTrack();
    const bool hasTrack = track.state.getParent().isValid();

    // 8.59：オートメーションの行も選べる（Phase 96）。
    //
    // **トラックを選んだときと同じ中身を出す**のが基本で、違うのは3つだけ：
    // 名前の欄はパラメータ名（編集しない）、色見本はレーンの色、
    // そして**トラックの並べ替え・削除は出さない**（押した行と効く先が違うため）。
    auto lane = getSelectedAutomationLane();
    const bool hasLane = lane.state.isValid();

    // 通知の選り分けに使う「今表示しているもの」を覚え直す
    selectedTrackState = hasTrack ? track.state : juce::ValueTree();
    selectedLaneState = hasLane ? lane.state : juce::ValueTree();

    // 購読先を今のプロジェクトのルートへ合わせる（HANDOVER 1.15）。
    // プロジェクトを読み込むとルート自体が差し替わるので、毎回見比べて付け替える。
    if (subscribedProjectState != project.getState())
    {
        subscribedProjectState.removeListener (this);
        subscribedProjectState = project.getState();
        subscribedProjectState.addListener (this);
    }

    // 8.296：**Consoleのストリップごと作り直す**（Phase 289／本人の指定）。
    //
    // 担当トラックはコンストラクタで決まる作りなので、選択が変わったら作り直します
    // （付け替えより購読の外し忘れが起きにくい。ラックのときと同じ判断）。
    //
    // **音量を持たないトラック（コード）には出しません**——
    // フェーダーもパンもメーターも意味が無く、素通しにすると空白だけが伸びます（8.60）
    strip.reset();

    if (hasTrack && trackHasVolume (track))
    {
        strip = std::make_unique<ChannelStripComponent> (track, project, audioEngine,
                                                          ChannelStripComponent::Layout::Inspector);

        strip->onMixerValueChanged = [this] { audioEngine.updateMixerSettings(); };

        // スロットが増減すると必要な高さが変わる。置き直す
        strip->onPreferredHeightChanged = [this] { resized(); };

        // 8.297：**スクロールの外**（Phase 290／本人の指定で下端に固定）。
        // `content`（Viewportの中身）ではなく、こちらの子にすること
        addAndMakeVisible (strip.get());
    }

    const bool showTrack = hasTrack;
    const bool showClip = hasTrack && selection.isClipSelected();

    // 8.59：名前と色は**レーンだけを選んでいるときも出す**（Phase 96）。
    // マスターのレーンにはトラックが無いので、ここを`showTrack`にすると
    // 「何も選んでいません」になってしまう
    const bool showNameAndColour = hasTrack || hasLane;

    nameCaption.setVisible (showNameAndColour);
    nameEditor.setVisible (showNameAndColour);
    typeLabel.setVisible (showNameAndColour);
    colourCaption.setVisible (showNameAndColour);
    resetLaneColourButton.setVisible (hasLane);

    // 8.296：**出し分けはストリップが自分でやります**（Phase 289）。
    // VCAならパンとメーターを持たない、コードなら作らない——
    // Consoleとまったく同じ判断が、同じコードで効きます（`ChannelStripComponent`）
    updateMeterTimer();

    // Phase 33：トラックの削除。
    // 8.59：**レーンを選んでいるときは出さない**（Phase 96）。
    // 押した行はレーンなのに効く先はトラック、という食い違いを作らない
    // （レーンの削除は行の右クリックメニューにある）
    deleteTrackButton.setVisible (showTrack && ! hasLane);

    for (auto* swatch : colourSwatches)
        swatch->setVisible (showNameAndColour);

    clipStartCaption.setVisible (showClip);
    clipStartLabel.setVisible (showClip);
    clipLengthCaption.setVisible (showClip);
    clipLengthLabel.setVisible (showClip);
    clipExtraCaption.setVisible (showClip);
    clipExtraLabel.setVisible (showClip);

    // 8.84：MIDIの入力設定は**MIDIトラックを選んでいるときだけ**（Phase 124／改善案⑯）
    const bool showMidiInput = showNameAndColour
                                && getSelectedTrack().getType() == TrackType::Midi;

    midiInputCaption.setVisible (showMidiInput);
    midiInputDeviceBox.setVisible (showMidiInput);
    midiInputChannelBox.setVisible (showMidiInput);
    midiOutputCaption.setVisible (showMidiInput);
    midiOutputChannelBox.setVisible (showMidiInput);

    if (showMidiInput)
        refreshMidiInputDeviceList();

    // 8.77：トランスポーズはクリップを選んでいるときだけ（Phase 117／改善案㉝）。
    //
    // 8.147：**オーディオクリップでも出します**（Phase 185／改善案㉞）。
    // Phase 184まではMIDIだけでした——オーディオは音程を動かせなかったので
    const bool showTranspose = showClip
                                && (selection.getType() == SelectionState::Type::MidiClip
                                     || selection.getType() == SelectionState::Type::AudioClip);

    transposeCaption.setVisible (showTranspose);

    for (auto* button : transposeButtons)
        button->setVisible (showTranspose);

    emptyLabel.setVisible (! showNameAndColour);

    updateControlsFromModel();
    resized();
}

void InspectorPanel::refreshMidiInputDeviceList()
{
    // 8.84：MIDI入力デバイスの一覧（Phase 124/改善案⑯）。
    //
    // **保存しているのは名前**なので、いま挿さっていないデバイスの名前も足します——
    // 挿し直すまで設定が見えなくなるのは不親切です。
    // ID=1が「すべて」、2番以降が並んだデバイス
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    auto track = getSelectedTrack();
    const auto savedName = track.state.getParent().isValid() ? track.getMidiInputDeviceName()
                                                              : juce::String();

    juce::StringArray names;

    for (const auto& device : juce::MidiInput::getAvailableDevices())
        names.addIfNotAlreadyThere (device.name);

    if (savedName.isNotEmpty())
        names.addIfNotAlreadyThere (savedName);

    midiInputDeviceBox.clear (juce::dontSendNotification);
    midiInputDeviceBox.addItem (utf8 ("すべて"), 1);

    for (int i = 0; i < names.size(); ++i)
        midiInputDeviceBox.addItem (names[i], i + 2);

    const int savedIndex = names.indexOf (savedName);
    midiInputDeviceBox.setSelectedId (savedIndex >= 0 ? savedIndex + 2 : 1, juce::dontSendNotification);
}

void InspectorPanel::updateControlsFromModel()
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    auto track = getSelectedTrack();
    auto lane = getSelectedAutomationLane();
    const bool hasLane = lane.state.isValid();

    if (! track.state.getParent().isValid() && ! hasLane)
    {
        titleLabel.setText (utf8 ("インスペクタ"), juce::dontSendNotification);
        return;
    }

    // タイトルで「今なにを見ているか」が分かるようにする
    switch (selection.getType())
    {
        case SelectionState::Type::AudioClip: titleLabel.setText (utf8 ("オーディオクリップ"), juce::dontSendNotification); break;
        case SelectionState::Type::MidiClip:  titleLabel.setText (utf8 ("MIDIクリップ"), juce::dontSendNotification); break;
        case SelectionState::Type::AutomationLane: titleLabel.setText (utf8 ("オートメーション"), juce::dontSendNotification); break;
        case SelectionState::Type::Track:
        case SelectionState::Type::None:
        default:                              titleLabel.setText (utf8 ("トラック"), juce::dontSendNotification); break;
    }

    // 8.59：レーンを選んでいるときは、名前の欄に**パラメータ名**を出す（Phase 96）。
    //
    // **編集できないようにすること。** 開いたままにすると、パラメータ名を打ち替えた
    // つもりが**親トラックの名前を書き換える**（`nameChanged()`はトラックへ書く）。
    // 種別の欄には親トラックの名前を出して、どこにぶら下がっているかが分かるようにする
    nameEditor.setEditable (! hasLane, ! hasLane, false);

    if (hasLane)
    {
        nameEditor.setText (AutomationTargets::getDisplayName (lane.getTargetId()),
                             juce::dontSendNotification);
        nameEditorTrackId.clear();

        typeLabel.setText (track.state.getParent().isValid() ? track.getName() : utf8 ("Master"),
                            juce::dontSendNotification);
    }
    else
    {
        // 名前は編集中に書き換えない（打っている途中で確定値へ戻ってしまうため）。
        // 8.52：**書き換えるときは「誰の名前か」も覚え直す**（Phase 91）。
        // 8.53：`Label`は編集中だけ入力欄を持つので、それで見分ける（Phase 92）
        if (nameEditor.getCurrentTextEditor() == nullptr)
        {
            nameEditor.setText (track.getName(), juce::dontSendNotification);
            nameEditorTrackId = track.getId();
        }

        typeLabel.setText (trackTypeToString (track.getType()), juce::dontSendNotification);
    }

    // 8.59：レーンだけを選んでいる（＝マスターのレーン）ときは、
    // ここから下のトラック用のコントロールを触らない
    if (! track.state.getParent().isValid())
    {
        updateLaneColourSwatches (lane);
        return;
    }

    // 8.296：**音量・パン・ミュート／ソロはストリップが自分で合わせます**（Phase 289）。
    // あちらもトラックのツリーを購読していて、再生位置は`setPlayheadSeconds()`で届きます

    // 8.84：MIDIの入力設定（Phase 124/改善案⑯）。**IDは値+1**（0＝すべて／そのまま）
    midiInputChannelBox.setSelectedId (track.getMidiInputChannel() + 1, juce::dontSendNotification);
    midiOutputChannelBox.setSelectedId (track.getMidiOutputChannel() + 1, juce::dontSendNotification);

    // 選択中の色見本に枠を付ける。
    // 8.59：**レーンを選んでいるならレーンの色**（Phase 96）
    if (hasLane)
    {
        updateLaneColourSwatches (lane);
    }
    else
    {
        const auto currentColour = juce::Colour::fromString (track.getColourString());

        for (int i = 0; i < colourSwatches.size(); ++i)
            colourSwatches[i]->setToggleState (trackColourSwatches[i] == currentColour,
                                                juce::dontSendNotification);
    }

    //==========================================================================
    // クリップの情報
    if (! selection.isClipSelected())
        return;

    const int clipIndex = selection.getClipIndex();

    // 8.94：**MIDIクリップの選択はもうありません**（Phase 134）。
    // MIDIの中身はピアノロールで見るもので、ここに出す「クリップの情報」は無い
    if (selection.getType() == SelectionState::Type::MidiClip)
        return;

    {
        if (! juce::isPositiveAndBelow (clipIndex, track.getNumClips()))
            return;

        auto clip = track.getClip (clipIndex);

        clipStartLabel.setText (formatSeconds (clip.getStartTime()), juce::dontSendNotification);
        clipLengthLabel.setText (formatSeconds (clip.getLength()), juce::dontSendNotification);
        // 8.40：ゲインも出す（Phase 80）。**触るのはオーディオエディタ**で、ここは表示だけ
        //
        // 8.147：**トランスポーズは数字で出すこと**（Phase 185／改善案㉞）。
        // 下のボタンは押すだけで今の値が見えないので、**+3のクリップを
        // もう1つ上げたつもりで+4にした**のか分からなくなります
        // 8.149：**伸縮も数字で出す**（Phase 187／8.48）。
        // 端をAltでドラッグすると半端な倍率になるので、
        // **いまいくつなのかが見えないと戻しようがありません**
        clipExtraCaption.setText (utf8 ("フェード (in/out) ／ ゲイン ／ 半音 ／ 伸縮"),
                                   juce::dontSendNotification);

        const int semitones = clip.getTranspose();

        juce::String extra;
        extra << juce::String (clip.getFadeInSeconds(), 2) << " / "
              << juce::String (clip.getFadeOutSeconds(), 2) << " s ／ "
              << (clip.getGainDb() >= 0.0f ? "+" : "") << juce::String (clip.getGainDb(), 1) << " dB"
              << utf8 (" ／ ") << (semitones > 0 ? "+" : "") << semitones
              << utf8 (" ／ x") << juce::String (clip.getStretch(), 3);

        clipExtraLabel.setText (extra, juce::dontSendNotification);
    }
}

void InspectorPanel::nameChanged()
{
    if (isUpdatingFromModel)
        return;

    // 8.52：**打ち込んだ相手へ書くこと**（Phase 91）。
    //
    // `getSelectedTrack()`を見ていたため、**名前を打った後に別のトラックを選ぶと、
    // そちらの名前が書き換わって**いました：ここはフォーカスが外れたときにも走り、
    // その時点の選択は**もう次のトラック**になっているためです。
    // 入力欄がどのトラックのものかを覚えておき、そこへ書きます（1.32と同じ考え方）。
    auto track = project.findTrackById (nameEditorTrackId);

    if (! track.state.getParent().isValid())
        return;

    const auto newName = nameEditor.getText().trim();

    if (newName.isEmpty() || newName == track.getName())
        return;

    project.beginAction (utf8 ("トラック名の変更"));
    track.setName (newName, &project.getUndoManager());
}

void InspectorPanel::colourSwatchClicked (int swatchIndex)
{
    if (! juce::isPositiveAndBelow (swatchIndex, trackColourSwatches.size()))
        return;

    // 8.59：**レーンを選んでいるならレーンの色を変える**（Phase 96）。
    // 設計書2.4：色はARGBのhex文字列で持つ（データ層はjuce_graphicsに依存しないため。
    // ProjectModel.cppのTrack::create参照）
    auto lane = getSelectedAutomationLane();

    if (lane.state.isValid())
    {
        project.beginAction (utf8 ("オートメーションの色の変更"));
        lane.setColourString (trackColourSwatches[swatchIndex].toString(), &project.getUndoManager());
        updateControlsFromModel();
        return;
    }

    auto track = getSelectedTrack();

    if (! track.state.getParent().isValid())
        return;

    project.beginAction (utf8 ("トラックカラーの変更"));

    track.state.setProperty (IDs::trackColor, trackColourSwatches[swatchIndex].toString(),
                              &project.getUndoManager());
}

//==============================================================================
// Phase 33：トラックの並べ替えと削除
//==============================================================================

int InspectorPanel::getSelectedTrackIndex() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getId() == selection.getTrackId())
            return t;

    return -1;
}

void InspectorPanel::deleteSelectedTrack()
{
    const int trackIndex = getSelectedTrackIndex();

    if (trackIndex < 0)
        return;

    // 8.119：**確認のダイアログは出さない**（Phase 154／改善案27）。
    // 理由は`ArrangeView::deleteTrack()`と同じ——Ctrl+Zで1手で戻る。
    // **入口は2つある**ので、片方だけ変えないこと（1.27）。
    project.removeTrack (project.getTrack (trackIndex));
}

//==============================================================================
// 仕様書4.4・6章：ブラウザからのドラッグ&ドロップ（Phase 29）
//==============================================================================

bool InspectorPanel::isInterestedInDragSource (const SourceDetails& details)
{
    auto track = getSelectedTrack();

    if (! track.state.getParent().isValid())
        return false;

    return TrackRackComponent::canAcceptPluginDrag (audioEngine, track, details.description);
}

void InspectorPanel::itemDragEnter (const SourceDetails&)
{
    isDragOver = true;
    repaint();
}

void InspectorPanel::itemDragExit (const SourceDetails&)
{
    isDragOver = false;
    repaint();
}

void InspectorPanel::itemDropped (const SourceDetails& details)
{
    isDragOver = false;
    repaint();

    auto track = getSelectedTrack();

    if (! track.state.getParent().isValid())
        return;

    TrackRackComponent::handlePluginDrop (audioEngine, track, details.description,
        [this]
        {
            // 8.296：ラックはストリップの中（Phase 289）
            if (strip != nullptr)
            {
                strip->refreshInsertSlots();
                strip->refreshVcaAssignment();
            }
        });
}

//==============================================================================
void InspectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    // Phase 28：インスペクタは画面の**左**側に移した。仕切り線とリサイザーの余白も右端に来る
    g.setColour (AppColours::border);
    g.drawLine ((float) getWidth() - PanelResizerBar::thickness, 0.0f,
                 (float) getWidth() - PanelResizerBar::thickness, (float) getHeight(), 1.0f);
}

void InspectorPanel::paintOverChildren (juce::Graphics& g)
{
    // 仕様書4.4：ドラッグ中の落とし先をパープルで示す。
    // 中身はViewportの子なので、上から重ねて描かないと隠れてしまう。
    if (! isDragOver)
        return;

    auto area = getLocalBounds().toFloat();
    area.removeFromRight ((float) PanelResizerBar::thickness);

    g.setColour (AppColours::purple.withAlpha (0.15f));
    g.fillRect (area);

    g.setColour (AppColours::purple);
    g.drawRect (area, 2.0f);
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds();

    // 右端はリサイザー用に空けておく（帯そのものはMainComponentが置く）
    area.removeFromRight (PanelResizerBar::thickness);
    area = area.reduced (8, 8);

    titleLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (8);

    // 8.297：**Consoleの部分は下端に張り付けます**（Phase 290／本人の指定）。
    //
    // Phase 289では、名前・色・MIDI入力…に続けて縦に流していました。つまり
    // **上に何行あるかでフェーダーの位置が変わり**、MIDIトラック（入力の設定が3行ある）と
    // オーディオトラックを行き来するたびに、**同じものが上下に動いて**いました。
    //
    // 下端に固定すれば、**どのトラックを選んでも同じ場所**です
    // （本人の言葉：「その方がトラックごとの見た目のずれも少なくなる」）。
    //
    // **スクロールの外へ出しています**（`content`の子ではなく、こちらの子）。
    // 中に入れたままだと、上が長いトラックでは**スクロールしないと出てきません**。
    if (strip != nullptr)
    {
        // **欲しい高さは渡すが、画面より高くはしない。** 低い画面では
        // フェーダーが縮みます（`ChannelStripComponent::layOutForInspector()`が
        // 最低の高さまでで受け止める）
        const int wanted = strip->getPreferredHeight (area.getWidth());
        const int stripHeight = juce::jmin (wanted, juce::jmax (0, area.getHeight()
                                                                    - minimumViewportHeight));

        strip->setBounds (area.removeFromBottom (stripHeight));
        area.removeFromBottom (8);
    }

    viewport.setBounds (area);

    // Viewportの中身は「幅は器に合わせ、高さは中身ぶん」。ただし縦スクロールバーが出ると
    // 使える幅がそのぶん減るので、**まず幅いっぱいで測り、収まらなければ measり直す**。
    // （幅が変わると折り返しで高さも変わり得るため、順番を逆にはできない）
    const int fullWidth = juce::jmax (1, viewport.getWidth());

    int contentWidth = fullWidth;
    int neededHeight = layOutContents (contentWidth, false);

    if (neededHeight > viewport.getHeight())
    {
        contentWidth = juce::jmax (1, fullWidth - viewport.getScrollBarThickness());
        neededHeight = layOutContents (contentWidth, false);
    }

    content.setSize (contentWidth, juce::jmax (neededHeight, viewport.getHeight()));
    layOutContents (contentWidth, true);
}

int InspectorPanel::layOutContents (int width, bool apply)
{
    juce::Rectangle<int> area { 0, 0, width, 100000 };
    const int captionHeight = 16;

    // 位置の計算を「測る用」と「置く用」で2つ書くと必ずずれるので、
    // 置くかどうかだけをここで分岐させる（TrackRackComponentと同じ作り）。
    auto place = [apply] (juce::Component& c, juce::Rectangle<int> bounds)
    {
        if (apply)
            c.setBounds (bounds);
    };

    if (emptyLabel.isVisible())
    {
        place (emptyLabel, area.removeFromTop (48));
        return area.getY();
    }

    place (nameCaption, area.removeFromTop (captionHeight));

    auto nameRow = area.removeFromTop (24);
    place (typeLabel, nameRow.removeFromRight (56));
    place (nameEditor, nameRow);
    area.removeFromTop (8);

    place (colourCaption, area.removeFromTop (captionHeight));

    // 8.160：**見本も段組みにする**（Phase 198／本人の要望）。
    //
    // Phase 197までは8色を横一列でした。24色を1行に詰めると1つ7px程度になり、
    // **色は見えても押し分けられません**。ヘッダーのパレットと同じ段数で並べます
    // ——**数はモデル側が持っています**（`Track::getColourPaletteColumns()`。8.2）
    {
        const int columns = juce::jmax (1, Track::getColourPaletteColumns());
        const int swatchWidth = juce::jmax (14, area.getWidth() / columns);
        const int swatchHeight = 20;

        for (int i = 0; i < colourSwatches.size(); i += columns)
        {
            auto swatchRow = area.removeFromTop (swatchHeight);

            for (int c = 0; c < columns && i + c < colourSwatches.size(); ++c)
                place (*colourSwatches[i + c], swatchRow.removeFromLeft (swatchWidth).reduced (1));
        }
    }

    // 8.59：レーンを選んでいるときだけ出る「トラックの色に戻す」（Phase 96）
    if (resetLaneColourButton.isVisible())
    {
        area.removeFromTop (6);
        place (resetLaneColourButton, area.removeFromTop (22));
    }

    area.removeFromTop (10);

    // 8.296：**音量・パン・メーター・ミュート／ソロはストリップの中**（Phase 289）。
    // 置くのは下（MIDIの設定と削除より後）——本人の指定の絵の並びです

    // 8.84：MIDIの入力設定（Phase 124/改善案⑯）。
    // **出ていないときは場所も取らない**（`isVisible()`で飛ばす。8.60と同じ話）
    if (midiInputCaption.isVisible())
    {
        area.removeFromTop (10);
        place (midiInputCaption, area.removeFromTop (captionHeight));
        place (midiInputDeviceBox, area.removeFromTop (22));
        area.removeFromTop (3);
        place (midiInputChannelBox, area.removeFromTop (22));

        area.removeFromTop (6);
        place (midiOutputCaption, area.removeFromTop (captionHeight));
        place (midiOutputChannelBox, area.removeFromTop (22));
    }

    // Phase 33：削除は専用の行にする。
    // ミュート／ソロが「音の出し方」なのに対してこちらは「トラックそのもの」への操作なので、
    // 同じ行に混ぜると押し間違えやすい。右端へ離しておく。
    //
    // 8.59：**レーンを選んでいるときは出していない**（Phase 96）ので、場所も取らせない
    if (deleteTrackButton.isVisible())
    {
        area.removeFromTop (6);
        place (deleteTrackButton, area.removeFromTop (24).removeFromRight (52));
    }

    // 8.297：**ストリップはここに置きません**（Phase 290／本人の指定）。
    // スクロールの外で、パネルの下端に固定しています（`resized()`）

    if (! clipStartLabel.isVisible())
        return area.getY();

    area.removeFromTop (14);

    place (clipStartCaption, area.removeFromTop (captionHeight));
    place (clipStartLabel, area.removeFromTop (20));
    area.removeFromTop (4);

    place (clipLengthCaption, area.removeFromTop (captionHeight));
    place (clipLengthLabel, area.removeFromTop (20));
    area.removeFromTop (4);

    place (clipExtraCaption, area.removeFromTop (captionHeight));
    place (clipExtraLabel, area.removeFromTop (20));

    // 8.77：トランスポーズ（Phase 117/改善案㉝）。**出ていないときは場所も取らない**
    // （`isVisible()`で飛ばさないと、空白だけが伸びる。8.60と同じ話）
    if (transposeCaption.isVisible())
    {
        area.removeFromTop (4);
        place (transposeCaption, area.removeFromTop (captionHeight));

        auto row = area.removeFromTop (22);
        const int buttonWidth = juce::jmax (1, row.getWidth() / juce::jmax (1, transposeButtons.size()));

        for (auto* button : transposeButtons)
            place (*button, row.removeFromLeft (juce::jmin (buttonWidth, row.getWidth())).reduced (1, 0));
    }

    return area.getY();
}
