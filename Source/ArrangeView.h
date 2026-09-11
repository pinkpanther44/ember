#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "WaveformCache.h"
#include "TimelineComponent.h"
#include "LevelMeterComponent.h"
#include "SelectionState.h"
#include "SnapGridSelector.h"   // 仕様書5.5・5.9：編集の刻み（Phase 55）
#include "IconAssets.h"          // 8.133：ツールの絵（Phase 169）
#include "MidiRecording.h"       // 8.146：録れたMIDIの入れ物（Phase 184）
#include "StatusStrip.h"         // 8.195：出るときだけ出る帯（Phase 232）

class AudioEngine;

//==============================================================================
/**
    設計書2.3.1「アレンジビュー」の土台。

    Phase 4fで、選択中クリップのヒットポイント自動検出（仕様書5.5.1）に対応した。
    検出処理はファイル全体の読み込みを伴うため、UIが固まらないよう
    バックグラウンドスレッドで実行する。
*/
class ArrangeView : public juce::Component,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    ArrangeView (ProjectModel& projectToUse, AudioEngine& audioEngineToUse, SelectionState& selectionToUse);
    ~ArrangeView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /** 再生位置（秒）をタイムラインへ転送する（MainComponentがタイマーで呼び出す）。 */
    void setPlayheadSeconds (double seconds) { timeline.setPlayheadSeconds (seconds); }

    /** 仕様書5.9：ルーラーのクリック／ドラッグで再生位置が動いたときに呼ばれる（Phase 30）。

        **MainComponentのタイマーは再生中しか回っていない**ので、
        停止中のシークはこの経路でしか伝わらない。時間表示を持つ側はこれを購読すること。 */
    std::function<void (double)> onPlayheadMoved;

    /** 仕様書5.4：入力デバイスの状態をUIへ反映する。
        AudioEngine::initialise()はMainComponentのコンストラクタ本体で呼ばれる（＝
        このビューが構築された後）ため、初期化が終わった時点で改めて呼んでもらう必要がある。 */
    void refreshInputState();

    /** 仕様書5.1：プロジェクトを読み込み/新規作成した後に、表示を作り直す。

        `keepSelectionAndPlayhead`がfalse（既定）だと、選択と再生位置も初期化する。
        **プロジェクトが差し替わる場合**は、覚えているクリップ番号も再生位置も
        別プロジェクトのものになるため、必ず捨てる必要がある。

        Undo/Redoでは**プロジェクトは同じ**なので、どちらも捨てない（Phase 34）。
        捨てると、並べ替えをUndoするたびに選択が外れ、再生位置も0へ飛ぶ。 */
    void refreshAfterProjectChanged (bool keepSelectionAndPlayhead = false);

    /** ステータス欄に一時的なメッセージを出す（オートセーブの通知など）。
        次にトラック構成が変わると、通常のトラック一覧表示へ戻る。 */
    void showStatusMessage (const juce::String& message);

    /** 仕様書5.4：録音が終わったファイルをステータス欄に表示する。
        録音待機中（アーム）のオーディオトラック、無ければ最初のオーディオトラックへ
        クリップとして自動配置する。 */
    void showRecordingResult (const juce::File& recordedFile, double startTimeSeconds);

    /** 8.146：録れたMIDIをノートにして入れ、**画面に出す文面を返す**
        （Phase 184／改善案⑬a。仕様書5.4）。

        行き先は**録り分が覚えているtrackId**です——`showRecordingResult()`のように
        探し直しません。録り始めた時点でアームしていたトラックが正なので、
        **録音中に選択やアームを動かされても行き先が変わらない**ようにしてあります。

        `stopSeconds`は録音を止めた曲の時刻（押しっぱなしの鍵をここで切ります）。

        ### なぜ自分で出さずに返すのか

        入れたノートは画面を作り直さないと出ません（`refreshAllViews()`）。
        ところが**作り直すとステータス欄が普通のトラック一覧へ戻る**ので、
        ここで出しても消えます——**実際に、録れているのに何も出ませんでした**。
        **出すのは作り直したあと**なので、文面だけ返して呼ぶ側に任せます。 */
    juce::String applyMidiRecordingResult (const std::vector<RecordedMidiTake>& takes,
                                            double sampleRate, double stopSeconds);

    /** 設計書4.2：MIDIクリップがダブルクリックされたときに呼ばれる（Phase 15）。
        MainComponentがこれを受けてエディタパネルを開く。 */
    std::function<void (int trackIndex, double timelineSeconds)> onMidiClipDoubleClicked;

    /** 8.49：オーディオクリップの右クリックから、オーディオエディタで開く（Phase 88）。
        **MIDIクリップのダブルクリックと同じ入口の形**にしてある（8.39）。 */
    std::function<void (int trackIndex, int clipIndex)> onAudioClipEditorRequested;

    /** 設計書2.3.5：コード区間がダブルクリックされたときに呼ばれる（Phase 43）。
        MainComponentがこれを受けてコードパッドを開く。 */
    std::function<void (double startTimeSeconds)> onChordRegionDoubleClicked;

    /** 仕様書5.5：指定したオーディオファイルをタイムラインへ取り込む。
        ファイル選択ダイアログ経由（「Import Audio File...」）でも、
        ブラウザパネル（仕様書4.4）からでも、ここを通る。

        trackIdを指定すると、そのトラックの指定位置へ置く（ドラッグ&ドロップ。Phase 21）。
        空文字なら最初のオーディオトラックの0秒へ置く（従来の動作）。 */
    void importAudioFile (const juce::File& file, const juce::String& trackId = {},
                           double startTimeSeconds = 0.0);

    /** 8.154：まとめて落とされたファイルを置く（Phase 192／本人の要望）。

        **時間方向に並べます**（同じ場所へ重ねない）。落とした位置から順に、
        1つ前の終わりが次の頭になります。1つだけのときも、ここを通って構いません。 */
    void importAudioFiles (const juce::StringArray& paths, const juce::String& trackId = {},
                            double startTimeSeconds = 0.0);

    /** 音声ファイルの長さ（秒）。**読めなければ0**。 */
    double getAudioFileLengthSeconds (const juce::File& file);

    /** 波形サムネイルの共有キャッシュ（Phase 79／8.39）。

        **オーディオエディタと同じものを使うため**に公開している。
        別々に持つと、同じファイルを2回読み込むことになる（WaveformCacheの説明を参照）。 */
    WaveformCache& getWaveformCache() { return waveformCache; }

    /** 仕様書4.4：プラグインがタイムラインへドロップされたときに呼ばれる（Phase 21）。
        MainComponentが挿し先の振り分けとGUI表示を行う。 */
    std::function<void (const juce::String& trackId, const juce::PluginDescription&)> onPluginDropped;

    //==========================================================================
    // 仕様書6.2：キーボードショートカットからの入口（Phase 47）。
    //
    // **中身は既存の処理をそのまま呼ぶだけ**にしてある。ショートカット用に別の実装を
    // 書くと、ボタンから実行したときと挙動がずれる（8.2「同じ判定を複数箇所に書くと、
    // 必ずずれる」）。確認ダイアログの有無なども、ここを通せば自動的に揃う。

    /** ツールバーの「+ Track」と同じメニューを、そのボタンの位置に出す。 */
    void showAddTrackMenuFromKeyboard();

    /** 8.60：トラック追加のメニューを、指定した画面座標に出す（Phase 97／改善案㉚）。

        **Consoleの右クリックからも呼ぶので公開してある**（8.27の「入口は増やしても中身は1つ」）。
        種別の並びも、足す位置の決め方（`getTrackAddAnchorId()`）も1箇所のままになる。 */
    void showAddTrackMenuAt (juce::Rectangle<int> anchorScreenBounds)
    {
        showAddTrackMenu (anchorScreenBounds, selection.getTrackId());
    }

    /** 選択中のトラックを削除する（トラックを選んでいなければ何もしない）。 */
    void deleteSelectedTrack();

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    //==========================================================================
    // 仕様書6.2：カット／コピー／貼り付け（Phase 71）。**中身は`TimelineComponent`**

    bool cutSelection()                 { return timeline.cutSelection(); }
    bool copySelection()                { return timeline.copySelection(); }
    bool pasteAt (double timeSeconds)   { return timeline.pasteAt (timeSeconds); }

    //==========================================================================
    // 仕様書6.2：ツール切り替えと複数選択（Phase 51）

    /** ツールを流し込む（MainComponentから配られる）。 */
    void setEditTool (EditTool tool);
    EditTool getEditTool() const;

    /** ツールバーのボタンでツールが選ばれたときに呼ばれる（Phase 52で追加）。

        **ボタンから直接`setEditTool()`しないこと。** ツールはピアノロールにも
        効くので、MainComponentへ返して両方の画面へ配ってもらう必要がある。
        自分で決めてしまうと、**ボタンで選んだときだけピアノロールに反映されない**。 */
    std::function<void (EditTool)> onEditToolSelected;

    /** クリップのメニューから「複製」が選ばれたときに呼ばれる（Phase 71）。
        **実装はMainComponentに1つだけ**（ショートカットのDと同じ経路）。 */
    std::function<void()> onDuplicateClipRequested;

    /** 8.76：クリップのメニューから「分割」「結合」（Phase 116／改善案④㉟）。
        **実装はMainComponentに1つだけ**（ショートカットと同じ経路）。 */
    std::function<void()> onSplitClipRequested;
    std::function<void()> onMergeClipRequested;

    /** 8.77：選んでいるものをまとめて上下させる（Phase 117／改善案㉝）。
        インスペクタのボタンからも、クリップのメニューからも**ここを通ります**。

        8.147：**オーディオクリップにも効きます**（Phase 185／改善案㉞）。
        振り分けは`TimelineComponent::transposeSelection()`が持っています。 */
    void transposeSelection (int semitones);

    /** 8.78：クリップのグループ（Phase 118/改善案㊱）。**判断はタイムライン側に1つだけ**。 */
    void groupSelectedClips();
    void ungroupSelectedClips();
    bool canGroupSelectedClips() const;
    bool hasGroupedClipInSelection() const;

    //==========================================================================
    // 仕様書5.5・5.9：編集の刻み（スナップ、Phase 55でここへ移した）
    //
    // **ツールと同じ扱い。** ピアノロールにも同じものが出るので、
    // 自分で決めずMainComponentへ返して、両方の画面へ配ってもらう（1.27・8.15）。

    /** ツールバーのコンボボックスで刻みが選ばれたときに呼ばれる。 */
    std::function<void (SnapGrid)> onSnapGridSelected;

    /** 表示をモデルの値へ合わせる（もう片方の入口で変えられたときにも呼ぶこと）。 */
    void setSnapGrid (SnapGrid grid);

    void selectAllClips();
    void clearClipSelection();

    /** 複数選択されているクリップの数（コマンドの有効/無効に使う）。 */
    int getNumSelectedClips() const;

    /** 複数選択が変わったときに呼ばれる。 */
    std::function<void()> onClipSelectionChanged;

    /** 8.60：トラックヘッダーの「i」が押されたときに呼ばれる（Phase 97／改善案①）。
        インスペクタパネルを開くのは`MainComponent`の仕事（配置を知っているのはあちらだけ）。
        `allowClose`は「押したトラックが、いま出ているものと同じか」。 */
    std::function<void (bool allowClose)> onInspectorRequested;

    /** 仕様書5.9：ルーラーの帯でループ範囲が変わったときに呼ばれる（Phase 48）。 */
    std::function<void()> onLoopChanged;

    /** 仕様書5.9：ルーラーのメニューからマーカーを頼まれたときに呼ばれる（Phase 50）。 */
    std::function<void (double timeSeconds, bool askForName)> onInsertMarkerRequested;

private:
    /** 追加するトラックの種類を選ぶメニューを出す（Phase 27）。
        追加処理そのものは種類ごとの関数のまま残してあり、ここは呼び分けるだけ。

        入口は2つある（Phase 31）：ツールバーの「+ Track」と、トラック一覧の末尾の
        「＋ 新しいトラック」。**どちらも同じメニューを出す**ので、
        項目を足すときはここ1箇所を直せばよい。 */
    /** トラック追加のメニューを出す。`anchorTrackId`が空なら末尾へ足す（8.60）。 */
    void showAddTrackMenu (juce::Rectangle<int> anchorScreenBounds,
                            const juce::String& anchorTrackId);

    /** 8.60：新しいトラックを**どのトラックの真下へ入れるか**（Phase 97／改善案⑱）。

        メニューを開いたときに`pendingAddAnchorId`へ控えた値を返す。
        **メニューは非同期で閉じる**ので、選んだ時点で引き直すと
        そのあいだに選択が変わっていることがある。 */
    juce::String getTrackAddAnchorId() const { return pendingAddAnchorId; }

    /** 8.60：これから足すトラックの入れ先（Phase 97／改善案⑱）。

        **空文字なら末尾**。`showAddTrackMenu()`が開くときに決める：

        - ツールバーの「+ Track」・ヘッダーのメニュー・Consoleの右クリック
          … **選んでいるトラックの真下**（「いま触っているものの隣に足したい」）
        - 一覧の末尾の「＋ 新しいトラック」 … **末尾**
          （**押した場所がそのまま行き先**なので、ここだけは選択を見ない） */
    juce::String pendingAddAnchorId;

    void addAudioTrackClicked();
    void addMidiTrackClicked();

    /** 8.123：空白へ落とされた音源のためにMIDIトラックを作る（Phase 158／改善案7）。
        作れなければ空文字を返す。名前はプラグイン名。 */
    juce::String addTrackForDroppedInstrument (const juce::String& pluginName);

    void addSendTrackClicked();
    void addChordTrackClicked();
    void addVcaTrackClicked();

    /** 8.50：フォルダトラックを足す（Phase 89／D2）。 */
    void addFolderTrackClicked();

    /** 8.50：ヘッダーメニューの「フォルダへ入れる」の項目ID（Phase 89）。
        **他の項目と重ならない大きい番号**にしてある（8.37のカーブ種別と同じ形）。 */
    static constexpr int folderMenuBaseId = 100;
    void importAudioClicked();

    /** 仕様書5.6：トラックヘッダーの「A」ボタンから、レーンの対象を選ぶメニューを出す
        （Phase 26）。音量・パンに加え、そのトラックのプラグインパラメータも並べる。
        trackIndexがマスター行の場合はマスターの設定になる。 */
    void showAutomationMenu (int trackIndex, juce::Rectangle<int> buttonScreenBounds);

    /** 8.59：オートメーションの行の右クリックメニュー（Phase 96）。

        **追加／削除／隠す**の3つ。「追加」の中身は`showAutomationMenu()`と
        同じ一覧なので、**組み立ては`buildAutomationTargetMenu()`1箇所**にまとめてある
        （片方だけ増えるのを防ぐ。8.27の「入口は増やしても中身は1つ」）。 */
    void showAutomationRowMenu (int trackIndex, int laneOrdinal,
                                 juce::Rectangle<int> headerScreenBounds);

    /** オートメーション対象の一覧を組む。`targetIdsOut`に項目番号順の識別子が入る
        （項目番号は`firstItemId`から始まる）。 */
    void buildAutomationTargetMenu (juce::PopupMenu& menu, int trackIndex, bool isMaster,
                                     int firstItemId, juce::StringArray& targetIdsOut);

    /** 対象の表示／非表示を切り替える（トラックとマスターの違いをここで吸収する）。 */
    void toggleAutomationLane (int trackIndex, bool isMaster, const juce::String& targetId);

    /** トラックヘッダーの右クリックメニュー（Phase 33）。
        並べ替え（上へ／下へ）と削除を出す。 */
    void showTrackHeaderMenu (int trackIndex, juce::Rectangle<int> headerScreenBounds);

    /** 8.159：右クリックしたトラックに効かせる相手（Phase 197/本人の要望）。
        **押したものがまとめ選択に入っていれば全部、入っていなければ押した1本だけ。** */
    juce::StringArray getTrackIdsForHeaderAction (int trackIndex) const;

    /** 8.143：**パラアウトの受け皿をまとめて作る**（Phase 181／改善案⑮）。

        音源の出力バスを有効にして、**バス1本につき1行**（`TrackType::DrumOut`）を
        音源トラックのすぐ下へ並べます。**既にある受け皿は作り直しません**ので、
        押し直しても増えません。 */
    void createDrumOutTracks (int trackIndex);

    /** 確認してからトラックを削除する（Phase 33）。
        **中身のあるトラックは取り消しの効くUndoがあっても不安なので、一度確認する。**
        空のトラックは確認せずに消す（作り間違いを消すのが煩わしくならないように）。 */
    void deleteTrack (int trackIndex);

    /** 8.29の表：トラックを複製する（Phase 70）。複製したほうを選び直す。 */
    void duplicateTrack (int trackIndex);

    /** 仕様書5.4：入力レベルメーターを定期更新する（およそ30fps）。 */
    void timerCallback() override;

    /** 選択が変わったら、オートメーションの対象一覧を作り直す（Phase 20）。
        トラックごとにプラグインの顔ぶれが違うため。 */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** 設計書2.5：トラックヘッダーの幅を覚えておくキー（Phase 161／改善案38）。
        値はpx。**上下限の丸めは`TimelineComponent::setTrackHeaderWidth()`が持っている**ので、
        ここでは読んだ値をそのまま渡す（同じ判断を2箇所に書かない）。 */
    const juce::String trackHeaderWidthKey { "arrangeTrackHeaderWidth" };

    ProjectModel& project;

    AudioEngine& audioEngine;

    // 設計書2.3.7：インスペクタと共有する選択状態（Phase 17）。
    // timelineより前に宣言しておくこと（timelineの初期化でこれを渡すため）。
    SelectionState& selection;

    WaveformCache waveformCache;

    // Phase 27（8.1 ①）：トラック追加は「+ Track」1つに畳んだ。
    // 種類ごとに5つ並べていたころは、ツールバー1段目の幅の大半をここが占めていた。
    juce::TextButton addTrackButton    { "+ Track" };
    /** 8.199：**「+ Audio」へ短くしました**（Phase 234／改善案5の3）。
        トラックヘッダーの角へ移したので、長い文字列は入りません */
    juce::TextButton importAudioButton { "+ Audio" };

    // タイムラインのズーム操作（設計書2.3.1）
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomFitButton { "Fit" };

    /** 仕様書6.2：ツール切り替え（Phase 51）。**記号は使わない**（1.30）ので文字にする。
        並びはショートカットの1〜4と対。ラベルはコンストラクタで入れる
        （`utf8()`はヘッダでは使えないため）。 */

    /** 8.133：本人が用意した絵を使う（Phase 169／改善案44）。
        文字（「選択」など）も設定したまま残してあるので、
        `AppColours::useTransportIcons`を`false`にすれば文字へ戻ります。 */
    IconAssets::SvgButton arrowToolButton;
    IconAssets::SvgButton pencilToolButton;
    IconAssets::SvgButton cutToolButton;
    IconAssets::SvgButton eraserToolButton;   // Phase 83（C11）

    /** ツールボタンの見た目を、いまのツールに合わせる。 */
    void updateToolButtons();

    /** 仕様書5.5・5.9：編集の刻み（Phase 55）。ツールの左隣に置く。 */
    SnapGridSelector snapSelector;

    // 仕様書5.4：入力デバイスの設定と入力レベル表示。
    // **モニタリングのON/OFFはトラックヘッダーへ移した**（Phase 26）。
    // デバイス選択とレベル表示はプロジェクト全体の話なのでここに残している。
    LevelMeterComponent inputMeter;
    juce::Label inputStateLabel;

    TimelineComponent timeline { project, waveformCache, selection };
    /** 8.195：知らせは出るときだけ（Phase 232／改善案5の2）。
        **常設だった一覧と説明文は廃止しました**——トラックヘッダーと重複していたためです */
    StatusStrip statusStrip;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
