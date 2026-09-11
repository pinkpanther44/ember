#include "ArrangeView.h"
#include "AppColours.h"
#include "AppSettings.h"   // 8.125：ヘッダーの幅を覚えておく（Phase 161）
#include "Utf8.h"
#include "AudioEngine.h"
#include "ToolbarLayout.h"   // 8.196：ツールボタンの大きさは1つ（Phase 232）
#include "DragAndDropIds.h"

ArrangeView::ArrangeView (ProjectModel& projectToUse, AudioEngine& audioEngineToUse,
                           SelectionState& selectionToUse)
    : project (projectToUse), audioEngine (audioEngineToUse), selection (selectionToUse)
{
    // Phase 27（8.1 ①）：5つ並んでいたトラック追加ボタンを1つのメニューに畳んだ
    addTrackButton.onClick = [this]
    {
        showAddTrackMenu (localAreaToGlobal (addTrackButton.getBounds()), selection.getTrackId());
    };
    addAndMakeVisible (addTrackButton);

    importAudioButton.onClick = [this] { importAudioClicked(); };
    addAndMakeVisible (importAudioButton);


    // タイムラインの表示倍率。マウスホイール（Ctrl+ホイール）でも操作できるが、
    // 初見で分かるようボタンも置いておく。
    zoomInButton.onClick  = [this] { timeline.zoomIn(); };
    zoomOutButton.onClick = [this] { timeline.zoomOut(); };
    zoomFitButton.onClick = [this] { timeline.zoomToFit(); };
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomOutButton);
    addAndMakeVisible (zoomFitButton);

    // 仕様書6.2：ツール切り替え（Phase 51）。ショートカットの1〜4と同じ並び
    // **日本語リテラルは`utf8()`を通すこと**（1.2）。`u8"..."`はC++20だと
    // `const char8_t*`になり、`const char*`へも`juce::String`へも直接は渡せない
    // 8.133：本人が用意した絵（Phase 169／改善案44）。**並びは下の表と対**
    struct ToolButtonSetup
    {
        IconAssets::SvgButton* button;   // 8.133（Phase 169）
        juce::String label;
        juce::String tooltip;
        EditTool tool;
        const char* iconResource;   // 8.133（Phase 169）
    };

    const ToolButtonSetup toolSetups[] =
    {
        { &arrowToolButton,  utf8 ("選択"),   utf8 ("選択・移動・伸縮／MIDIトラックの空きをドラッグで時間範囲を選ぶ（1）"),
          EditTool::arrow, "tool_select_svg" },
        // 8.120：**説明を書き換えた**（Phase 155／改善案29・37）。
        // 「使いません」は言い過ぎで、**オートメーション行のなぞり書きはペン**（8.57）
        { &pencilToolButton, utf8 ("ペン"),
          utf8 ("オートメーション行のなぞり書きに使います（2）\n"
                "トラック行をクリックすると選択ツールへ戻ります（打ち込みはピアノロールで）"),
          EditTool::pencil, "tool_pen_svg" },
        { &cutToolButton,    utf8 ("カット"), utf8 ("オーディオクリップをクリックした位置で割る（4）"),
          EditTool::cut, "tool_cut_svg" },
        { &eraserToolButton, utf8 ("消しゴム"), utf8 ("触れたクリップ／ノートの塊を消す（なぞると続けて消える。5）"),
          EditTool::eraser, "tool_eraser_svg" },
    };

    for (const auto& setup : toolSetups)
    {
        setup.button->setButtonText (setup.label);
        setup.button->setTooltip (setup.tooltip);
        setup.button->setIconResource (setup.iconResource);   // 8.133（Phase 169）
        setup.button->setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
        setup.button->setClickingTogglesState (false);

        const auto tool = setup.tool;

        // **自分で切り替えず、MainComponentへ返す**（Phase 52）。
        // ツールはピアノロールにも効くので、配るのはあちらの仕事
        setup.button->onClick = [this, tool]
        {
            if (onEditToolSelected != nullptr)
                onEditToolSelected (tool);
            else
                setEditTool (tool);   // 繋がっていない場合の保険
        };

        addAndMakeVisible (*setup.button);
    }
    timeline.onEditToolChanged = [this] { updateToolButtons(); };


    // 仕様書6.2：空いている場所の右クリックで選ばれたツールも、ボタンと同じ経路を通す（Phase 68）
    timeline.onEditToolSelected = [this] (EditTool tool)
    {
        if (onEditToolSelected != nullptr)
            onEditToolSelected (tool);
        else
            setEditTool (tool);
    };

    // 仕様書6.2：クリップのメニューからの「複製」（Phase 71）。
    // **実装はMainComponentに1つだけ**なので、そこへ返す
    timeline.onDuplicateClipRequested = [this]
    {
        if (onDuplicateClipRequested != nullptr)
            onDuplicateClipRequested();
    };

    // 8.76：分割・結合も同じ形で返す（Phase 116/改善案④㉟）
    timeline.onSplitClipRequested = [this]
    {
        if (onSplitClipRequested != nullptr)
            onSplitClipRequested();
    };

    timeline.onMergeClipRequested = [this]
    {
        if (onMergeClipRequested != nullptr)
            onMergeClipRequested();
    };

    // 8.77：できなかったときのひとこと（Phase 117）。帯を持っているのはこちら
    timeline.onStatusMessage = [this] (const juce::String& message) { showStatusMessage (message); };

    timeline.onClipSelectionChanged = [this]
    {
        if (onClipSelectionChanged != nullptr)
            onClipSelectionChanged();
    };

    updateToolButtons();

    // 仕様書5.5・5.9：編集の刻み（Phase 55）。**ツールと同じでMainComponentへ返す**
    // （ピアノロールにも同じ選択肢が出ているため。1.27・8.15）
    snapSelector.onSnapGridChanged = [this] (SnapGrid grid)
    {
        if (onSnapGridSelected != nullptr)
            onSnapGridSelected (grid);
        else
            project.setSnapGrid (grid);   // 繋がっていない場合の保険
    };

    snapSelector.setSnapGrid (project.getSnapGrid());
    addAndMakeVisible (snapSelector);

    // デバイスを切り替えたら、入力の有無とデバイス名表示を更新する
    audioEngine.onAudioDeviceChanged = [this] { refreshInputState(); };

    addAndMakeVisible (inputMeter);

    inputStateLabel.setFont (juce::FontOptions (12.0f));
    inputStateLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (inputStateLabel);

    refreshInputState();

    addAndMakeVisible (timeline);

    // 仕様書5.9：ルーラーのクリックで再生位置を動かす（Phase 18）。
    //
    // 8.90：**ここでエンジンを触らないこと**（Phase 130）。
    // 再生位置を動かすと、**トランスポートバー・ピアノロール・オーディオエディタ・
    // Console・インスペクタ・コードパッド**の全部が追従する必要があります。
    // ここで`audioEngine`だけ動かすと、**アレンジ画面のカーソルは動くのに
    // 他の画面は止まったまま**になります（実際にそうなっていました）。
    //
    // 配る役は`MainComponent::seekTo()`が持っているので、**そこへ返すだけ**にします
    // （ピアノロールとオーディオエディタは前からそちらを通っていた。8.33）
    timeline.onSeek = [this] (double seconds)
    {
        if (onPlayheadMoved != nullptr)
            onPlayheadMoved (seconds);
    };

    // 仕様書5.6：書き込みモードで記録された内容を、その場で描き直す（Phase 20）
    audioEngine.onAutomationRecorded = [this] { timeline.repaint(); };

    // 8.125：トラックヘッダーの幅を覚えておく（Phase 161／改善案38。設計書2.5）。
    // **既定値は聞かずに、いまの値を既定として渡す**（定数を外へ出さずに済む）
    timeline.setTrackHeaderWidth (AppSettings::getInt (trackHeaderWidthKey,
                                                        timeline.getTrackHeaderWidth()));

    timeline.onTrackHeaderWidthChanged = [this]
    {
        // **丸めた後の値を書くこと。** 打ち込まれた値をそのまま書くと、
        // 次に開いたときに上下限の外の値から始まります
        AppSettings::setInt (trackHeaderWidthKey, timeline.getTrackHeaderWidth());   // 設計書2.5
    };

    // 仕様書4.4：ドラッグ&ドロップの受け取り（Phase 21）
    timeline.onPluginDropped = [this] (const juce::String& trackId, const juce::String& identifier)
    {
        juce::PluginDescription description;

        // 識別子からスキャン済み一覧を引き直す。スキャンし直された後などで
        // 見つからないことがあるため、その場合は何もしない
        if (! DragAndDropIds::findPluginByIdentifier (audioEngine.getPluginManager().getKnownPlugins(),
                                                       identifier, description))
            return;

        auto targetTrackId = trackId;

        // 8.123：**空白へ落としたら、トラックごと作る**（Phase 158／改善案7）。
        //
        // 挿し先が空なのはタイムラインからの合図です（`onPluginDropped`の宣言）。
        // **一覧を引けるのはここから先**なので、音源かどうかの判断もここでやります。
        //
        // **エフェクトは作りません。** 音の出所が無いトラックに挿しても何も通らず、
        // 「置いたのに何も起きない」がトラックごと増える形になります
        if (targetTrackId.isEmpty())
        {
            if (! description.isInstrument)
                return;

            targetTrackId = addTrackForDroppedInstrument (description.name);

            if (targetTrackId.isEmpty())
                return;
        }

        if (onPluginDropped != nullptr)
            onPluginDropped (targetTrackId, description);
    };

    timeline.onFilesDropped = [this] (const juce::StringArray& paths, const juce::String& trackId,
                                       double startTime)
    {
        importAudioFiles (paths, trackId, startTime);
    };

    // 仕様書5.6：オートメーションの表示対象（Phase 26でトラックヘッダーへ移動）。
    // トラックヘッダーの「A」ボタンから、そのトラックの対象を選ぶ。
    timeline.onAutomationButtonClicked = [this] (int trackIndex, juce::Rectangle<int> buttonScreenBounds)
    {
        showAutomationMenu (trackIndex, buttonScreenBounds);
    };

    // 8.59：オートメーションの行の右クリック（Phase 96）。
    // **中身の一覧は「A」ボタンと共用**（`buildAutomationTargetMenu()`）
    timeline.onAutomationRowRightClicked = [this] (int trackIndex, int laneOrdinal,
                                                    juce::Rectangle<int> headerScreenBounds)
    {
        showAutomationRowMenu (trackIndex, laneOrdinal, headerScreenBounds);
    };

    // 設計書2.3.1：トラック一覧の末尾の「＋ 新しいトラック」（Phase 31）。
    // ツールバーの「+ Track」と同じメニューを出す。
    //
    // 8.60：**ここだけは末尾へ足す**（Phase 97／改善案⑱）。
    // 他の入口は「選んでいるトラックの真下」ですが、この行は一覧のいちばん下にあり、
    // **押した場所がそのまま行き先**として読めます。ここで選択を見ると、
    // 一番下を押したのに真ん中へ足されることになります
    timeline.onAddTrackClicked = [this] (juce::Rectangle<int> rowScreenBounds)
    {
        showAddTrackMenu (rowScreenBounds, {});
    };

    // Phase 33：トラックヘッダーの右クリックで、削除やフォルダへの出し入れ
    timeline.onTrackHeaderRightClicked = [this] (int trackIndex, juce::Rectangle<int> headerScreenBounds)
    {
        showTrackHeaderMenu (trackIndex, headerScreenBounds);
    };

    // 8.60：トラックヘッダーの「i」（Phase 97／改善案①）。**そのままMainComponentへ返す**
    timeline.onInspectorRequested = [this] (bool allowClose)
    {
        if (onInspectorRequested != nullptr)
            onInspectorRequested (allowClose);
    };

    // 仕様書5.4：入力モニタリング（Phase 26でトラックヘッダーへ移動）。
    // TimelineComponentにAudioEngineを持ち込まないよう、コールバックで受け渡す。
    timeline.isInputMonitoringEnabled = [this] { return audioEngine.isInputMonitoringEnabled(); };
    timeline.onInputMonitoringToggled = [this] (bool shouldMonitor)
    {
        audioEngine.setInputMonitoringEnabled (shouldMonitor);
    };

    // 仕様書5.6：ヘッダーのパンを掴んだ／離した（Phase 58）。
    // **Consoleのパンつまみと同じ扱いにすること**（`ChannelStripComponent`参照）。
    // 片方だけTouch/Latchの記録が始まると、「どこで動かしたか」で結果が変わる
    // 8.61：**対象の識別子ごと受け取る**（Phase 99）。
    // ヘッダーに音量フェーダーが入ったので、パン専用ではなくなった
    timeline.onAutomationTouchStart = [this] (const juce::String& trackId, const juce::String& targetId)
    {
        audioEngine.beginAutomationTouch (trackId, targetId);
    };

    timeline.onAutomationTouchEnd = [this] (const juce::String& trackId, const juce::String& targetId)
    {
        audioEngine.endAutomationTouch (trackId, targetId);
    };

    selection.addChangeListener (this);

    // 設計書4.2：MIDIクリップのダブルクリックはMainComponentへ中継する（Phase 15）。
    // タブの切り替えはこのビューの責務ではないため、ここでは受け流すだけにする。
    timeline.onAudioClipEditorRequested = [this] (int trackIndex, int clipIndex)
    {
        if (onAudioClipEditorRequested != nullptr)
            onAudioClipEditorRequested (trackIndex, clipIndex);
    };

    timeline.onMidiClipDoubleClicked = [this] (int trackIndex, double timelineSeconds)
    {
        if (onMidiClipDoubleClicked != nullptr)
            onMidiClipDoubleClicked (trackIndex, timelineSeconds);
    };

    // 設計書2.3.5：コード区間のダブルクリックも同じく中継する（Phase 43）
    timeline.onChordRegionDoubleClicked = [this] (double startTimeSeconds)
    {
        if (onChordRegionDoubleClicked != nullptr)
            onChordRegionDoubleClicked (startTimeSeconds);
    };

    // 仕様書5.9：ループ範囲の変更も中継する（Phase 48）
    timeline.onLoopChanged = [this]
    {
        if (onLoopChanged != nullptr)
            onLoopChanged();
    };

    // 仕様書5.9：ルーラーのメニューからのマーカー挿入も中継する（Phase 50）
    timeline.onInsertMarkerRequested = [this] (double timeSeconds, bool askForName)
    {
        if (onInsertMarkerRequested != nullptr)
            onInsertMarkerRequested (timeSeconds, askForName);
    };

    // 8.195：**出るときだけ出る帯**（Phase 232／改善案5の2）。
    // **`addChildComponent`であること**——`addAndMakeVisible`にすると最初から居座ります
    statusStrip.onVisibilityChanged = [this] { resized(); };
    addChildComponent (statusStrip);

}

ArrangeView::~ArrangeView()
{
    stopTimer();

    selection.removeChangeListener (this);

    // AudioEngineはこのビューより後に破棄される。コールバックにthisを握らせたままだと
    // 破棄済みのビューを呼び出す危険があるため、明示的に外しておく。
    audioEngine.onAudioDeviceChanged = nullptr;
    audioEngine.onAutomationRecorded = nullptr;

}

void ArrangeView::timerCallback()
{
    inputMeter.setLevels (audioEngine.getInputLevel (0), audioEngine.getInputLevel (1));

    // 仕様書5.7：トラックヘッダーのレベルメーター（Phase 58／8.1のC16）。
    // **ここから毎回流し込む。** TimelineComponentにAudioEngineを持ち込まない方針
    // （入力モニタリングと同じ）なので、値を取るのはこちらの仕事
    timeline.refreshHeaderMeters ([this] (const juce::String& trackId, int channel)
    {
        return audioEngine.getTrackLevel (trackId, channel);
    });
}

void ArrangeView::showRecordingResult (const juce::File& recordedFile, double startTimeSeconds)
{
    if (! recordedFile.existsAsFile())
    {
        showStatusMessage (utf8 ("録音ファイルが見つかりませんでした。"));
        return;
    }

    // 録音先トラックの決定：録音待機（アーム）中のオーディオトラック → 最初のオーディオトラック
    // → どちらも無ければ新規作成、の順に探す。
    int targetIndex = -1;

    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        auto t = project.getTrack (i);

        if (t.getType() == TrackType::Audio && t.isArmed())
        {
            targetIndex = i;
            break;
        }
    }

    if (targetIndex < 0)
    {
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            if (project.getTrack (i).getType() == TrackType::Audio)
            {
                targetIndex = i;
                break;
            }
        }
    }

    if (targetIndex < 0)
    {
        project.addTrack ("Audio 1", TrackType::Audio);
        targetIndex = project.getNumTracks() - 1;
    }

    // 長さは、録音したファイル自体から読み取る（実際にディスクに書けた量が真実のため）
    double lengthSeconds = 0.0;

    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (
            waveformCache.getFormatManager().createReaderFor (recordedFile)))
    {
        if (reader->sampleRate > 0.0)
            lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;
    }

    if (lengthSeconds <= 0.0)
    {
        showStatusMessage (utf8 ("録音ファイルを読み取れませんでした: ") + recordedFile.getFullPathName());
        return;
    }

    auto track = project.getTrack (targetIndex);
    project.beginAction (utf8 ("録音クリップの追加"));
    track.addAudioClip (recordedFile.getFullPathName(), startTimeSeconds, lengthSeconds, &project.getUndoManager());

    timeline.refresh();

    juce::String text;
    text << utf8 ("録音しました: ") << recordedFile.getFileName()
         << " → " << track.getName()
         << " (" << juce::String (lengthSeconds, 2) << utf8 ("秒 / ")
         << juce::File::descriptionOfSizeInBytes (recordedFile.getSize()) << ")";

    showStatusMessage (text);
}

juce::String ArrangeView::applyMidiRecordingResult (const std::vector<RecordedMidiTake>& takes,
                                                    double sampleRate, double stopSeconds)
{
    // **空でも黙って帰らないこと。** 「録れなかった」と出さないと、
    // 鍵盤が繋がっていないだけなのか壊れているのかが分かりません
    if (takes.empty())
    {
        return utf8 ("MIDI録音：入力がありませんでした（環境設定のAudioで"
                      "MIDI入力デバイスが有効になっているか確かめてください）。");
    }

    // **区切りは1回だけ**（3.1）。何本のトラックへ録れていても、
    // Undo1回で録音まるごとが消えてほしい
    project.beginAction (utf8 ("MIDIの録音"));

    MidiRecording::Summary total;
    juce::StringArray parts;
    bool anyOverflowed = false;

    for (const auto& take : takes)
    {
        // 8.146：**直す規則は`MidiRecording::applyTake()`1箇所**（1.27）
        const auto summary = MidiRecording::applyTake (project, take, sampleRate, stopSeconds,
                                                        &project.getUndoManager());

        total.numNotes += summary.numNotes;
        total.numCCs += summary.numCCs;
        total.numHeldNotes += summary.numHeldNotes;
        anyOverflowed = anyOverflowed || take.overflowed;

        if (summary.numNotes > 0 || summary.numCCs > 0)
        {
            auto track = project.findTrackById (take.trackId);

            if (track.state.getParent().isValid())
                parts.add (track.getName() + " (" + juce::String (summary.numNotes) + ")");
        }
    }

    timeline.refresh();

    if (total.numNotes == 0 && total.numCCs == 0)
    {
        return utf8 ("MIDI録音：ノートになるものがありませんでした。");
    }

    juce::String text;
    text << utf8 ("MIDIを録音しました: ") << parts.joinIntoString (" / ")
         << utf8 ("  合計 ") << juce::String (total.numNotes) << utf8 ("ノート");

    if (total.numCCs > 0)
        text << " / " << juce::String (total.numCCs) << " CC";

    // **押しっぱなしのまま止めたぶんは黙って入れないこと。**
    // 「止めた位置で切った」と分かれば、長さが思ったとおりでなくても納得できます
    if (total.numHeldNotes > 0)
        text << utf8 ("（うち ") << juce::String (total.numHeldNotes)
             << utf8 (" 音は停止位置で切りました）");

    if (anyOverflowed)
        text << utf8 ("　※ 入りきらなかったぶんがあります（1トラック ")
             << juce::String (maxRecordedMidiEvents) << utf8 ("イベントまで）。");

    return text;
}

void ArrangeView::visibilityChanged()
{
    // Piano Rollタブでノートを打ち込んだ後にここへ戻ってくると、MIDIクリップの
    // 長さが変わっている可能性がある（Phase 15）。スクロール範囲まで含めて引き直す。
    if (isVisible())
    {
        timeline.refresh();
    }
}

void ArrangeView::transposeSelection (int semitones)
{
    // 8.77：**判断はタイムライン側に1つだけ**（Phase 117／改善案㉝）。
    // ここは通すだけ（インスペクタからも同じ道を通す。8.32）
    timeline.transposeSelection (semitones);
}

void ArrangeView::groupSelectedClips()      { timeline.groupSelectedClips(); }
void ArrangeView::ungroupSelectedClips()    { timeline.ungroupSelectedClips(); }
bool ArrangeView::canGroupSelectedClips() const     { return timeline.canGroupSelectedClips(); }
bool ArrangeView::hasGroupedClipInSelection() const { return timeline.hasGroupedClipInSelection(); }

void ArrangeView::showStatusMessage (const juce::String& message)
{
    statusStrip.show (message);
}

void ArrangeView::refreshAfterProjectChanged (bool keepSelectionAndPlayhead)
{
    if (! keepSelectionAndPlayhead)
    {
        // 選択中クリップのインデックスは、入れ替わった新しいプロジェクトでは
        // 別のクリップを指すか、そもそも存在しない可能性があるため必ずリセットする。
        timeline.clearSelection();
        timeline.setPlayheadSeconds (0.0);
    }

    timeline.refresh();
}


void ArrangeView::refreshInputState()
{
    const bool hasInput = audioEngine.isAudioInputAvailable();

    // モニタリングのトグルはトラックヘッダーへ移した（Phase 26）。
    // ヘッダーはモデルではなくエンジンの状態を見て描くので、描き直しだけ促す。
    timeline.repaint();

    // 現在どのデバイスを掴んでいるかが分からないと「メーターが振れない」ときの
    // 切り分けができないため、デバイス名と入力ch数を常に表示する。
    if (hasInput)
    {
        inputStateLabel.setText (utf8 ("入力: ") + audioEngine.getInputDeviceDescription(), juce::dontSendNotification);
        inputStateLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
        startTimerHz (30);
    }
    else
    {
        stopTimer();
        inputMeter.setLevels (0.0f, 0.0f);
        inputStateLabel.setText (utf8 ("入力なし: ") + audioEngine.getAudioInputError()
                                     + " [" + audioEngine.getInputDeviceDescription() + "]",
                                  juce::dontSendNotification);
        inputStateLabel.setColour (juce::Label::textColourId, AppColours::orange);
    }
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void ArrangeView::resized()
{
    auto area = getLocalBounds().reduced (16);

    // 8.199：**「+ Track」「+ Audio」はルーラー左端の角へ移しました**
    //         （Phase 234／改善案5の3）。置くのはこの関数の終わりです。
    //
    // それまでは横1段（30px＋余白8px）を専有していました。**丸ごと空きます。**
    //
    //     [        Bars        ]
    //     [+ Track] [+ Audio]

    // 仕様書5.4：入力まわりは2段目にまとめる。1段目に詰め込むと、
    // ウィンドウ幅が狭いときにボタンが重なって押せなくなるため。
    area.removeFromTop (8);
    auto inputRow = area.removeFromTop (28);

    // 右端にズーム操作をまとめる（Fit ← − ← ＋ の順に右から詰める）
    zoomFitButton.setBounds (inputRow.removeFromRight (50));
    inputRow.removeFromRight (4);
    zoomInButton.setBounds (inputRow.removeFromRight (34));
    inputRow.removeFromRight (4);
    zoomOutButton.setBounds (inputRow.removeFromRight (34));
    inputRow.removeFromRight (12);

    // 仕様書6.2：ツールはズームの左に、1〜4の順で並べる（Phase 51）。
    // **左から順に置く**ので、幅が足りないときは右のものから潰れる
    {
        // 8.196：**4つとも同じ幅**（Phase 232/改善案5の6。`ToolbarLayout.h`）。
        // 絵にした時点で中身の大きさは同じなので、幅だけ違うと**間隔がばらついて見えます**
        auto toolRow = inputRow.removeFromRight (juce::jmin (ToolbarLayout::toolGroupWidth,
                                                             inputRow.getWidth()));

        auto place = [&toolRow] (juce::TextButton& button)
        {
            button.setBounds (toolRow.removeFromLeft (juce::jmin (ToolbarLayout::toolButtonWidth,
                                                                   toolRow.getWidth())));
            toolRow.removeFromLeft (juce::jmin (ToolbarLayout::toolButtonGap, toolRow.getWidth()));
        };

        place (arrowToolButton);
        place (pencilToolButton);
        place (cutToolButton);
        place (eraserToolButton);

        inputRow.removeFromRight (12);
    }

    // 仕様書5.5・5.9：編集の刻み（Phase 55）。**ツールの左隣**に置く。
    // どちらも「これから置くもの」を決める設定なので、まとめて手の届く場所にある
    snapSelector.setBounds (inputRow.removeFromRight (juce::jmin (SnapGridSelector::preferredWidth,
                                                                   inputRow.getWidth()))
                                     .reduced (0, 2));
    inputRow.removeFromRight (12);

    inputMeter.setBounds (inputRow.removeFromLeft (110).reduced (0, 3));
    inputRow.removeFromLeft (12);

    inputStateLabel.setBounds (inputRow);

    area.removeFromTop (10);

    // 8.195：**見えているときだけ場所を取ります**（Phase 232）。
    // 高さ0で置いておくと、親はその1行ぶんを引き算し続けます——広げたいのに広がりません
    if (statusStrip.isVisible())
        statusStrip.setBounds (area.removeFromBottom (StatusStrip::height));

    timeline.setBounds (area);

    // 8.199：**タイムラインの角へ、2つのボタンを置きます**（Phase 234／改善案5の3）。
    //
    // **`timeline.setBounds()`の後であること。** 角の場所はタイムラインの位置から
    // 決まるので、先に置くと1回ぶん古い場所へ行きます。
    //
    // ボタンは`ArrangeView`の子のままです（押したときに動くのはこちら）。
    // **`toFront()`が要ります**——`timeline`のほうが後から`addAndMakeVisible`
    // されているので（コンストラクタの並び）、そのままだと下に隠れます
    {
        auto row = timeline.getCornerButtonRow() + timeline.getBounds().getPosition();

        const int half = (row.getWidth() - 4) / 2;

        addTrackButton.setBounds (row.removeFromLeft (half));
        row.removeFromLeft (4);
        importAudioButton.setBounds (row);

        addTrackButton.toFront (false);
        importAudioButton.toFront (false);
    }
}

void ArrangeView::showAddTrackMenu (juce::Rectangle<int> anchorScreenBounds,
                                     const juce::String& anchorTrackId)
{
    // 8.60：**入れ先はここで決めて控える**（Phase 97／改善案⑱）。
    // メニューは非同期で閉じるので、選ばれた時点で引き直すと、
    // そのあいだに選択が変わっていることがある
    pendingAddAnchorId = anchorTrackId;

    // Phase 27（8.1 ①）：種類ごとの追加処理はそのままに、入口だけメニューへ寄せた。
    //
    // 8.119：**並びを本人の指定どおりにした**（Phase 154／改善案40）。
    // Phase 27からは「信号の流れ順」（音を出すもの → まとめ先 → 制御専用）だったが、
    // **実際に足す回数の多い順**——MIDI → オーディオ → フォルダ → センド → VCA → コード。
    // 区切り線は「自分で音を出すもの」と「他のトラックをまとめる／操るもの」の境目に置く。
    //
    // **番号（ID）は変えていない。** 並びを変えるたびに番号まで振り直すと、
    // 下のswitchと突き合わせるのが面倒になる
    juce::PopupMenu menu;
    menu.addItem (2, utf8 ("MIDIトラック"));
    menu.addItem (1, utf8 ("オーディオトラック"));
    menu.addSeparator();
    menu.addItem (6, utf8 ("フォルダトラック"));   // Phase 89（D2）
    menu.addItem (3, utf8 ("センドトラック"));
    menu.addItem (5, utf8 ("VCAトラック"));
    menu.addItem (4, utf8 ("コードトラック"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (anchorScreenBounds),
        [this] (int result)
        {
            switch (result)
            {
                case 1: addAudioTrackClicked(); break;
                case 2: addMidiTrackClicked();  break;
                case 3: addSendTrackClicked();  break;
                case 4: addChordTrackClicked(); break;
                case 5: addVcaTrackClicked();   break;
                case 6: addFolderTrackClicked(); break;
                default: break; // 0＝メニューを閉じただけ
            }
        });
}

void ArrangeView::showTrackHeaderMenu (int trackIndex, juce::Rectangle<int> headerScreenBounds)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (trackIndex);
    const auto type = track.getType();

    // 8.60：**「上へ移動」「下へ移動」は外しました**（Phase 97）。
    //
    // 並べ替えは**ヘッダーのドラッグ**（Phase 34・36）でやるものになっていて、
    // 1マスずつ動かす項目は使われていませんでした。フォルダへの出し入れも
    // ドラッグで済むので（8.51）、メニューは**種類が変わる操作だけ**に絞っています。
    //
    // 8.29の表（Phase 70）：**追加と複製もここから**。
    // トラックの用があるときに一番近いのはヘッダーの上で、
    // 「+ Track」ボタンまで戻らずに済む
    juce::PopupMenu menu;
    menu.addSectionHeader (track.getName());
    menu.addItem (4, utf8 ("トラックを追加..."));
    menu.addItem (5, utf8 ("このトラックを複製"), type != TrackType::Chord);
    menu.addSeparator();
    // 8.50：フォルダへ入れる／出す（Phase 89／D2）。
    //
    // **入れ先はメニューで選ばせます。** ヘッダーのドラッグは並べ替えに使っており
    // （Phase 34）、「上へ落とすと入る」を足すと**並べ替えと取り合い**になります。
    juce::PopupMenu folderMenu;
    int folderItemId = folderMenuBaseId;

    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        auto candidate = project.getTrack (i);

        if (candidate.getType() != TrackType::Folder)
            continue;

        folderMenu.addItem (folderItemId, candidate.getName(),
                             project.canMoveTrackIntoFolder (track, candidate.getId()),
                             track.getParentFolderId() == candidate.getId());
        ++folderItemId;
    }

    if (folderItemId > folderMenuBaseId)
    {
        menu.addSeparator();
        menu.addSubMenu (utf8 ("フォルダへ入れる"), folderMenu);
        menu.addItem (6, utf8 ("フォルダから出す"), track.getParentFolderId().isNotEmpty());
    }

    // 8.143：**パラアウトを作る**（Phase 181／改善案⑮）。
    //
    // **音源が出力バスを2本以上持っているMIDIトラックにだけ出します。**
    // 持っていない音源に出しても「押せるのに何も起きない」項目になるだけです
    // （時間の基準をオーディオにだけ出しているのと同じ判断）。
    //
    // **バスの数を知っているのはエンジンだけ**です——プラグインの実体を
    // 持っているのがあちらだからで、モデルには載っていません
    if (type == TrackType::Midi && audioEngine.getInstrumentOutputBusCount (track.getId()) > 1)
    {
        menu.addSeparator();
        menu.addItem (9, utf8 ("パラアウトを作る..."));
    }

    // 8.142：**時間の基準**（Phase 180／8.105の宿題3）。
    //
    // **オーディオトラックにだけ出します。** 他の種別は中身が全部拍で保存されていて、
    // 選ぶ余地がありません（`TimeBase`の説明）。出すと「選べるのに効かない項目」に
    // なるので、**無いほうが正直**です
    if (type == TrackType::Audio)
    {
        const auto base = track.getTimeBase();

        juce::PopupMenu timeBaseMenu;
        timeBaseMenu.addItem (7, utf8 ("Linear（秒。テンポを変えても動かない）"),
                               true, base == TimeBase::linear);
        timeBaseMenu.addItem (8, utf8 ("Musical（拍。テンポに追従する）"),
                               true, base == TimeBase::musical);

        menu.addSeparator();
        menu.addSubMenu (utf8 ("時間の基準"), timeBaseMenu);
    }

    menu.addSeparator();
    menu.addItem (3, utf8 ("トラックを削除"));


    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (headerScreenBounds),
        [this, trackIndex, headerScreenBounds] (int result)
        {
            // メニューを開いている間に構成が変わっている可能性があるので、都度確かめる
            if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
                return;

            if (result == 4)
            {
                // **「+ Track」と同じメニューを出す**（種別の並びを2箇所に書かない）。
                // 8.60：**右クリックしたトラックの真下**へ足す（Phase 97／改善案⑱）
                showAddTrackMenu (headerScreenBounds,
                                   juce::isPositiveAndBelow (trackIndex, project.getNumTracks())
                                       ? project.getTrack (trackIndex).getId()
                                       : juce::String());
            }
            else if (result == 5)
            {
                duplicateTrack (trackIndex);
            }
            else if (result == 6)
            {
                // 8.50：フォルダから出す（Phase 89／D2）。
                // 8.159：**選んでいるぶん全部**（Phase 197）
                project.beginAction (utf8 ("フォルダから出す"));

                // 8.203：ここも`false`（Phase 237）。理由は「フォルダへ入れる」と同じです
                for (const auto& id : getTrackIdsForHeaderAction (trackIndex))
                    project.moveTrackIntoFolder (project.findTrackById (id), {}, false);

                timeline.refresh();
            }
            else if (result == 9)
            {
                createDrumOutTracks (trackIndex);   // 8.143（Phase 181／改善案⑮）
            }
            else if (result == 7 || result == 8)
            {
                // 8.142：時間の基準を変える（Phase 180／8.105の宿題3）。
                // **中のクリップの持ち替えはモデル側**（`Track::setTimeBase()`）——
                // 音は動きません
                auto target = project.getTrack (trackIndex);

                project.beginAction (utf8 ("時間の基準の変更"));
                target.setTimeBase (result == 8 ? TimeBase::musical : TimeBase::linear,
                                     &project.getUndoManager());

                timeline.refresh();
            }
            else if (result >= folderMenuBaseId)
            {
                // 8.50：選んだフォルダへ入れる（Phase 89／D2）。
                // **番号ではなく「何番目のフォルダか」で引き直す**：メニューを開いている
                // あいだにトラックが増減していても、同じ数え方で辿れる
                int folderCount = 0;

                for (int i = 0; i < project.getNumTracks(); ++i)
                {
                    auto candidate = project.getTrack (i);

                    if (candidate.getType() != TrackType::Folder)
                        continue;

                    if (folderMenuBaseId + folderCount == result)
                    {
                        // 8.159：**選んでいるぶん全部を入れる**（Phase 197／本人の要望）
                        const auto ids = getTrackIdsForHeaderAction (trackIndex);
                        const auto folderId = candidate.getId();

                        project.beginAction (utf8 ("フォルダへ入れる"));

                        // 8.203：**`false`を渡すこと**（Phase 237）。上で区切りを開いているのに
                        // 中でも開いていたので、**まとめて入れてもCtrl+Zは1本ずつ**でした
                        for (const auto& id : ids)
                            if (id != folderId)
                                project.moveTrackIntoFolder (project.findTrackById (id), folderId, false);

                        timeline.refresh();
                        break;
                    }

                    ++folderCount;
                }
            }
            else if (result == 3)
            {
                deleteTrack (trackIndex);
            }
        });
}

//==============================================================================
// 仕様書6.2：キーボードショートカットからの入口（Phase 47）

void ArrangeView::showAddTrackMenuFromKeyboard()
{
    // ボタンの位置に出す。マウスの位置に出すと、キーボードで押したのに
    // 画面の端に現れることがあって、どこを見ればよいか分からなくなる。
    showAddTrackMenu (addTrackButton.getScreenBounds(), selection.getTrackId());
}

void ArrangeView::deleteSelectedTrack()
{
    // 選択は番号ではなくIDで持っている（1.32）ので、ここで番号に直す
    const auto trackId = selection.getTrackId();

    if (trackId.isEmpty())
        return;

    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getId() == trackId)
        {
            deleteTrack (t);   // 確認ダイアログの有無もこの中の判断に任せる
            return;
        }
}

void ArrangeView::zoomIn()    { timeline.zoomIn(); }
void ArrangeView::zoomOut()   { timeline.zoomOut(); }
void ArrangeView::zoomToFit() { timeline.zoomToFit(); }

//==============================================================================
// 仕様書6.2：ツール切り替えと複数選択（Phase 51）

void ArrangeView::setEditTool (EditTool tool)
{
    timeline.setEditTool (tool);   // ボタンの見た目はonEditToolChangedで揃う
}

EditTool ArrangeView::getEditTool() const
{
    return timeline.getEditTool();
}

void ArrangeView::setSnapGrid (SnapGrid grid)
{
    // 表示を合わせるだけ。**寄せる処理はタイムライン側がモデルを直接読む**ので、
    // ここから流し込むものは無い（8.14）
    snapSelector.setSnapGrid (grid);
}

void ArrangeView::selectAllClips()     { timeline.selectAllClips(); }
void ArrangeView::clearClipSelection() { timeline.clearClipSelection(); }

int ArrangeView::getNumSelectedClips() const
{
    return (int) timeline.getSelectedClips().size();
}

void ArrangeView::updateToolButtons()
{
    const auto tool = timeline.getEditTool();

    // 選ばれているものだけパープルにする（設計書2.6）。
    // `setToggleState`ではなくbuttonColourIdを直接変えているのは、
    // クリックでトグルさせたくないため（4つのうち1つだけが選ばれる作り）
    auto apply = [] (juce::TextButton& button, bool selected)
    {
        button.setColour (juce::TextButton::buttonColourId,
                           selected ? AppColours::purple : AppColours::background);
        button.setColour (juce::TextButton::textColourOffId,
                           selected ? juce::Colours::white : AppColours::textPrimary);
    };

    apply (arrowToolButton,  tool == EditTool::arrow);
    apply (pencilToolButton, tool == EditTool::pencil);
    apply (cutToolButton,    tool == EditTool::cut);
    apply (eraserToolButton, tool == EditTool::eraser);

    repaint();
}

void ArrangeView::duplicateTrack (int trackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto copy = project.duplicateTrack (project.getTrack (trackIndex));

    if (! copy.state.isValid())
        return;   // コードトラックなど、複製しないもの

    // **複製したほうを選び直す。** 作った直後に触るのは新しいほうなので、
    // インスペクタもそちらへ向いていてほしい（Phase 33の削除・追加と同じ考え方）
    selection.selectTrack (copy.getId());

    timeline.refresh();
}


/** 8.159：右クリックしたトラックに効かせる相手（Phase 197/本人の要望）。

    **押したトラックがまとめ選択に入っていれば、その全部**。入っていなければ押した1本だけ
    ——選んでいないものまで消える/動くのは驚きです（クリップのメニューと同じ決まり）。

    返すのは**ID**（1.32）。消しながら番号で回すと、2本目以降が別のトラックを指します。 */
juce::StringArray ArrangeView::getTrackIdsForHeaderAction (int trackIndex) const
{
    juce::StringArray ids;

    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return ids;

    const auto clickedId = project.getTrack (trackIndex).getId();
    const auto& selected = timeline.getSelectedTrackIds();

    if (std::find (selected.begin(), selected.end(), clickedId) == selected.end())
    {
        ids.add (clickedId);
        return ids;
    }

    // **並び順で返す**（画面の上から）。まとめて消すときに読みやすい
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto id = project.getTrack (t).getId();

        if (std::find (selected.begin(), selected.end(), id) != selected.end())
            ids.add (id);
    }

    return ids;
}

void ArrangeView::deleteTrack (int trackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    // 8.119：**確認のダイアログは出さない**（Phase 154／改善案27）。
    //
    // Phase 33からは「中身のあるトラックだけ聞く」形にしていたが、
    // **`removeTrack()`が`beginAction()`で1つのUndoにまとめている**ので、
    // 消し間違えてもCtrl+Zで参照（センド・VCA・サイドチェイン）ごと戻る。
    // 戻せるものを毎回聞くと、確認そのものが読まれなくなる。
    //
    // **消し方をもう1つ増やすときも、ここに確認を足さないこと**：
    // 入口によって聞かれたり聞かれなかったりするほうが分かりにくい（1.27）。
    //
    // 削除に伴う参照の後始末（センド・VCA・サイドチェイン）はProjectModel側で行う。
    // ノードの作り直しはAudioEngineが<TRACK>の削除を購読して自分で行う。
    //
    // 8.159：**まとめて選んでいるなら、その全部を消す**（Phase 197／本人の要望）。
    // **IDで回すこと**——1本消すたびに番号がずれるので、番号で回すと2本目から
    // 別のトラックを消します（1.32）
    const auto ids = getTrackIdsForHeaderAction (trackIndex);

    project.beginAction (utf8 ("トラックの削除"));

    for (const auto& id : ids)
    {
        auto track = project.findTrackById (id);

        if (track.state.getParent().isValid())
            project.removeTrack (track);
    }

    timeline.refresh();
}

void ArrangeView::addAudioTrackClicked()
{
    project.addTrack ("Audio " + juce::String (project.getNumTracks() + 1), TrackType::Audio,
                       getTrackAddAnchorId());
    timeline.refresh();
}


juce::String ArrangeView::addTrackForDroppedInstrument (const juce::String& pluginName)
{
    // 8.123：空白へ落とされた音源のためのトラック（Phase 158／改善案7）。
    //
    // **名前はプラグイン名にする。** 「MIDI 3」より「Diva」のほうが、
    // 何のためのトラックか後から読める。名前は後からいつでも変えられる。
    auto track = project.addTrack (pluginName.isNotEmpty()
                                       ? pluginName
                                       : "MIDI " + juce::String (project.getNumTracks() + 1),
                                   TrackType::Midi, {});

    if (! track.state.getParent().isValid())
        return {};

    // **作ったほうを選んでおく。** 落とした直後に触るのは新しいトラックなので、
    // インスペクタもそちらへ向いていてほしい（複製・削除と同じ考え方）
    selection.selectTrack (track.getId());

    timeline.refresh();

    return track.getId();
}

void ArrangeView::addMidiTrackClicked()

{
    // Phase 15でMIDIトラックもタイムラインに並ぶようになったので、ここからも作れるようにする
    // （Piano Rollタブの同名ボタンと同じ動作）。
    project.addTrack ("MIDI " + juce::String (project.getNumTracks() + 1), TrackType::Midi,
                       getTrackAddAnchorId());
    timeline.refresh();
}


void ArrangeView::addSendTrackClicked()
{
    // 仕様書5.2.2：センドトラック。リバーブ等を1本に集約して、複数トラックから送る用途。
    // 作った直後はどのトラックからも送られていないので、Consoleタブで送りを足す。
    int sendTrackCount = 0;

    for (int i = 0; i < project.getNumTracks(); ++i)
        if (project.getTrack (i).getType() == TrackType::Send)
            ++sendTrackCount;

    project.addTrack ("Send " + juce::String (sendTrackCount + 1), TrackType::Send,
                       getTrackAddAnchorId());
    timeline.refresh();

    showStatusMessage (utf8 ("センドトラックを追加しました。送り元のトラックを選び、インスペクタ（またはConsole）の「+ Send」で送りを設定してください。"));
}

void ArrangeView::createDrumOutTracks (int trackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto source = project.getTrack (trackIndex);

    if (source.getType() != TrackType::Midi)
        return;

    const juce::String sourceId = source.getId();

    // 8.144：**ここではバスを触りません**（Phase 182／本人の報告）。
    //
    // Phase 181は先に有効にしていましたが、**動いている音源のバス構成を変えても
    // グラフには伝わりません**（`AudioEngine::createPluginNode()`の説明）。
    // 受け皿を作れば作り直しが走り、そこで**有効な状態の音源が読み直されます**。
    //
    // ここで要るのは「バスが何本あるか」だけで、これは触らずに数えられます
    const int numBuses = audioEngine.getInstrumentOutputBusCount (sourceId);

    if (numBuses <= 1)
    {
        showStatusMessage (utf8 ("この音源は出力を1つしか持っていません。"));
        return;
    }

    // **既にある受け皿は作り直しません。** 押し直しても増えないので、
    // 「バスを足したから、もう一度押す」が安全な操作になります
    auto hasDrumOutFor = [this, &sourceId] (int busIndex)
    {
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            auto candidate = project.getTrack (i);

            if (candidate.getType() == TrackType::DrumOut
                 && candidate.getDrumOutSourceTrackId() == sourceId
                 && candidate.getDrumOutSourceBus() == busIndex)
                return true;
        }

        return false;
    };

    project.beginAction (utf8 ("パラアウトの作成"));

    juce::String anchorId = sourceId;   // **音源トラックのすぐ下から並べる**
    int created = 0;

    for (int bus = 1; bus < numBuses; ++bus)
    {
        if (hasDrumOutFor (bus))
            continue;

        // 名前はプラグインが返すバス名（"Kick"・"Out 3/4" など）。
        // 空なら番号で埋めます——**名前の無い行が並ぶより、番号のほうが選べます**
        juce::String name = audioEngine.getInstrumentOutputBusName (sourceId, bus);

        if (name.isEmpty())
            name = utf8 ("出力 ") + juce::String (bus + 1);

        auto created2 = project.addTrack (source.getName() + " / " + name,
                                           TrackType::DrumOut, anchorId);

        if (! created2.state.isValid())
            continue;

        created2.setDrumOutSource (sourceId, bus, &project.getUndoManager());
        anchorId = created2.getId();
        ++created;
    }

    // **あとはエンジンが自分でやります。** トラックが増えたことを
    // `valueTreeChildAdded()`が拾って**作り直し**に入り、そこで
    // **音源が「出力バスを有効にした状態」で読み直されます**（8.144）。
    // 配線はその最後の`rebuildDrumOutConnections()`が張ります
    timeline.refresh();

    if (created == 0)
        showStatusMessage (utf8 ("パラアウトの行は、もう全部あります。"));
    else
        showStatusMessage (utf8 ("パラアウトを") + juce::String (created)
                            + utf8 ("本作りました。音源の側で、どの音をどの出力へ出すか設定してください。"));
}

void ArrangeView::addChordTrackClicked()
{
    project.addTrack ("Chords", TrackType::Chord);
    timeline.refresh();
}


void ArrangeView::addVcaTrackClicked()
{
    // 仕様書5.2.4：VCAトラック。音声は通さず、リンクしたトラックのフェーダーを
    // まとめて動かすための制御専用トラック。作った直後は何もリンクされていないので、
    // Consoleタブの各ストリップから割り当てる。
    int vcaTrackCount = 0;

    for (int i = 0; i < project.getNumTracks(); ++i)
        if (project.getTrack (i).getType() == TrackType::VCA)
            ++vcaTrackCount;

    project.addTrack ("VCA " + juce::String (vcaTrackCount + 1), TrackType::VCA,
                       getTrackAddAnchorId());
    timeline.refresh();

    showStatusMessage (utf8 ("VCAトラックを追加しました。まとめたいトラックを選び、インスペクタ（またはConsole）の「VCA」ボタンから割り当ててください。"));
}


void ArrangeView::addFolderTrackClicked()
{
    // 8.50：フォルダトラック（Phase 89／D2。仕様書5.2・設計書1.3・2.4）。
    // **いまは「まとめて畳むための入れ物」**です。音はまだ通しません
    // （まとめて音を通す＝バスにするのは次の段）。
    int folderCount = 0;

    for (int i = 0; i < project.getNumTracks(); ++i)
        if (project.getTrack (i).getType() == TrackType::Folder)
            ++folderCount;

    project.addTrack (utf8 ("フォルダ ") + juce::String (folderCount + 1), TrackType::Folder,
                       getTrackAddAnchorId());
    timeline.refresh();

    showStatusMessage (utf8 ("フォルダトラックを追加しました。まとめたいトラックのヘッダーを右クリックし、「フォルダへ入れる」を選んでください。"));
}
void ArrangeView::importAudioClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("音声ファイルを選択"), juce::File(), "*.wav;*.aif;*.aiff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            importAudioFile (fc.getResult());
        });
}

void ArrangeView::importAudioFile (const juce::File& file, const juce::String& trackId,
                                     double startTimeSeconds)
{
    // Phase 17でブラウザパネル（仕様書4.4）からも呼ばれるようになったため、
    // ファイル選択ダイアログの中から取り出して独立した関数にしてある。
    // Phase 21でドラッグ&ドロップにも対応し、落とした先のトラックと位置を渡せるようにした。
    if (! file.existsAsFile())
        return;

    int targetIndex = -1;

    // 落とし先が指定されていればそれを使う（ドラッグ&ドロップ）
    if (trackId.isNotEmpty())
    {
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            if (project.getTrack (i).getId() == trackId
                 && project.getTrack (i).getType() == TrackType::Audio)
            {
                targetIndex = i;
                break;
            }
        }
    }

    // 指定が無い（または見つからない）場合は、最初のオーディオトラックへ
    for (int i = 0; targetIndex < 0 && i < project.getNumTracks(); ++i)
        if (project.getTrack (i).getType() == TrackType::Audio)
            targetIndex = i;

    // オーディオトラックが1本も無ければ、自動的に1本追加する
    if (targetIndex < 0)
    {
        project.addTrack ("Audio 1", TrackType::Audio);
        targetIndex = project.getNumTracks() - 1;
    }

    auto track = project.getTrack (targetIndex);

    const double lengthSeconds = getAudioFileLengthSeconds (file);

    if (lengthSeconds <= 0.0)
    {
        showStatusMessage (utf8 ("音声ファイルとして読み取れませんでした: ") + file.getFileName());
        return;
    }

    project.beginAction (utf8 ("音声ファイルの読み込み"));
    track.addAudioClip (file.getFullPathName(), juce::jmax (0.0, startTimeSeconds),
                         lengthSeconds, &project.getUndoManager());

    timeline.refresh();
}

double ArrangeView::getAudioFileLengthSeconds (const juce::File& file)
{
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (
            waveformCache.getFormatManager().createReaderFor (file)))
    {
        if (reader->sampleRate > 0.0)
            return (double) reader->lengthInSamples / reader->sampleRate;
    }

    return 0.0;
}

void ArrangeView::importAudioFiles (const juce::StringArray& paths, const juce::String& trackId,
                                     double startTimeSeconds)
{
    // 8.154：まとめて落とされたとき（Phase 192／本人の要望）。
    //
    // **時間方向に並べます**（同じ場所へ重ねない）。落とした位置から順に、
    // 1つ前の終わりが次の頭になります。**別々のトラックへ配らない**のは、
    // 置き場所を勝手に増やすことになるからです——1本のトラックに並んでいれば、
    // 選んで動かすのも、別のトラックへ移すのも、後からできます。
    double nextStart = juce::jmax (0.0, startTimeSeconds);

    for (const auto& path : paths)
    {
        const juce::File file (path);
        const double length = getAudioFileLengthSeconds (file);

        importAudioFile (file, trackId, nextStart);

        // **読めなかったものは詰めない**（`importAudioFile()`が知らせて何もしません）
        if (length > 0.0)
            nextStart += length;
    }

    if (paths.size() > 1)
        showStatusMessage (juce::String (paths.size()) + utf8 (" 個の音声ファイルを並べました。"));
}


void ArrangeView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // 選択が変わるとヘッダーの見た目（選択中トラックの枠）が変わるので描き直す。
    // Phase 26より前は、ここでオートメーションの対象一覧を作り直していた
    // （対象がプロジェクト全体で1つだったため。今はトラックごとにメニューで選ぶ）。
    timeline.repaint();
}

void ArrangeView::buildAutomationTargetMenu (juce::PopupMenu& menu, int trackIndex, bool isMaster,
                                              int firstItemId, juce::StringArray& targetIdsOut)
{
    // 8.56：**チェックの入切**（Phase 94／D3）。レーンは専用の行を持つので、
    // 1本だけ選ぶ形をやめて「出す／隠す」を対象ごとに切り替える。複数を同時に並べられる。
    //
    // 8.59：**ヘッダーの「A」と、行の右クリックの「追加」で共用**（Phase 96）。
    // 一覧を2箇所に書くと、プラグインのパラメータを足したときに片方だけ増える（1.27）
    auto isVisible = [this, trackIndex, isMaster] (const juce::String& targetId)
    {
        return isMaster ? project.isMasterAutomationLaneVisible (targetId)
                        : project.getTrack (trackIndex).isAutomationLaneVisible (targetId);
    };

    auto addTarget = [&] (const juce::String& targetId, const juce::String& text, juce::PopupMenu& into)
    {
        targetIdsOut.add (targetId);
        into.addItem (firstItemId + targetIdsOut.size() - 1, text, true, isVisible (targetId));
    };

    addTarget (AutomationTargets::volume, "Volume", menu);

    // マスターはパンを持たない（設計書1.3のmasterBus）
    if (! isMaster)
        addTarget (AutomationTargets::pan, "Pan", menu);

    // 仕様書5.6：そのトラックのプラグインパラメータ（Phase 20）。
    // パラメータは数百個あることもあるので、スロットごとにサブメニューへ分ける。
    if (! isMaster && juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
    {
        const auto trackId = project.getTrack (trackIndex).getId();

        for (auto insertIndex : audioEngine.getAutomatablePluginSlots (trackId))
        {
            const auto parameterNames = audioEngine.getPluginParameterNames (trackId, insertIndex);

            if (parameterNames.isEmpty())
                continue;

            juce::PopupMenu slotMenu;

            for (int p = 0; p < parameterNames.size(); ++p)
                addTarget (AutomationTargets::makePluginTarget (insertIndex, p), parameterNames[p], slotMenu);

            menu.addSubMenu (audioEngine.getPluginSlotName (trackId, insertIndex), slotMenu);
        }
    }
}

void ArrangeView::toggleAutomationLane (int trackIndex, bool isMaster, const juce::String& targetId)
{
    auto* undoManager = &project.getUndoManager();

    if (isMaster)
    {
        project.setMasterAutomationLaneVisible (targetId,
                                                 ! project.isMasterAutomationLaneVisible (targetId),
                                                 undoManager);
    }
    else if (juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
    {
        auto track = project.getTrack (trackIndex);
        track.setAutomationLaneVisible (targetId, ! track.isAutomationLaneVisible (targetId), undoManager);
    }
}

void ArrangeView::showAutomationMenu (int trackIndex, juce::Rectangle<int> buttonScreenBounds)
{
    const bool isMaster = ! juce::isPositiveAndBelow (trackIndex, project.getNumTracks());

    juce::PopupMenu menu;
    juce::StringArray targetIds;

    // 1番は「すべて隠す」。対象の一覧は2番から（`buildAutomationTargetMenu`が付ける）
    menu.addItem (1, utf8 ("すべて隠す"));
    menu.addSeparator();

    buildAutomationTargetMenu (menu, trackIndex, isMaster, 2, targetIds);

    // 押したボタンのすぐ隣に出す。`withTargetComponent`だけだとそのコンポーネント
    // （＝タイムライン全体）の端が基準になり、ボタンから遠く離れた位置に出てしまう。
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (buttonScreenBounds),
        [this, trackIndex, isMaster, targetIds] (int result)
        {
            if (result <= 0 || result > targetIds.size() + 1)
                return;

            project.beginAction (utf8 ("オートメーション表示の切り替え"));

            if (result == 1)
            {
                if (isMaster)
                    project.hideAllMasterAutomationLanes (&project.getUndoManager());
                else if (juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
                    project.getTrack (trackIndex).hideAllAutomationLanes (&project.getUndoManager());
            }
            else
            {
                toggleAutomationLane (trackIndex, isMaster, targetIds[result - 2]);
            }

            timeline.refresh();
        });
}

void ArrangeView::showAutomationRowMenu (int trackIndex, int laneOrdinal,
                                          juce::Rectangle<int> headerScreenBounds)
{
    const bool isMaster = ! juce::isPositiveAndBelow (trackIndex, project.getNumTracks());

    // **いま右クリックした行の対象を、識別子で控えておく。**
    // メニューは非同期で閉じるので、そのあいだに他のレーンが増減すると
    // `laneOrdinal`（何番目か）は別のレーンを指し得る（1.32）
    auto lane = isMaster ? project.getVisibleMasterAutomationLane (laneOrdinal)
                         : project.getTrack (trackIndex).getVisibleAutomationLane (laneOrdinal);

    if (! lane.state.isValid())
        return;

    const auto thisTargetId = lane.getTargetId();

    juce::PopupMenu addMenu;
    juce::StringArray targetIds;

    // 8.59：「追加」の中身は「A」ボタンと同じ一覧（Phase 96）。
    // 既に出ているものにはチェックが付き、選ぶと隠れる（同じ切り替え）
    buildAutomationTargetMenu (addMenu, trackIndex, isMaster, 10, targetIds);

    juce::PopupMenu menu;
    menu.addSectionHeader (AutomationTargets::getDisplayName (thisTargetId));
    menu.addSubMenu (utf8 ("オートメーショントラックを追加"), addMenu);
    menu.addSeparator();
    menu.addItem (1, utf8 ("この行を隠す"));
    menu.addItem (2, utf8 ("この行を削除（点も消えます）"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (headerScreenBounds),
        [this, trackIndex, isMaster, thisTargetId, targetIds] (int result)
        {
            if (result <= 0)
                return;

            auto* undoManager = &project.getUndoManager();

            if (result == 1)
            {
                project.beginAction (utf8 ("オートメーション表示の切り替え"));

                if (isMaster)
                    project.setMasterAutomationLaneVisible (thisTargetId, false, undoManager);
                else if (juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
                    project.getTrack (trackIndex).setAutomationLaneVisible (thisTargetId, false, undoManager);
            }
            else if (result == 2)
            {
                // **中身ごと消す。** 隠すのとは別物なので、
                // メニューの文言でも「点も消えます」と断っている
                project.beginAction (utf8 ("オートメーショントラックの削除"));

                if (isMaster)
                    project.removeMasterAutomationLane (thisTargetId, undoManager);
                else if (juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
                    project.getTrack (trackIndex).removeAutomationLane (thisTargetId, undoManager);
            }
            else if (result >= 10 && result < 10 + targetIds.size())
            {
                project.beginAction (utf8 ("オートメーション表示の切り替え"));
                toggleAutomationLane (trackIndex, isMaster, targetIds[result - 10]);
            }
            else
            {
                return;   // 知らない項目（増やしたときの取りこぼしを黙って通さない）
            }

            timeline.refresh();
        });
}

