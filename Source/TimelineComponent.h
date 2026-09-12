#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "WaveformCache.h"
#include "SelectionState.h"
#include "EditTool.h"   // 仕様書6.2：ツール（Phase 51、52で共有化）
#include "AutomationCurveUI.h"   // 8.37：線の描き方とメニューはピアノロールと共用（Phase 77）
#include "LevelMeterComponent.h"   // 仕様書5.7：ヘッダーのレベルメーター（Phase 58）
#include "TrackHeaderControls.h"   // 8.61：ヘッダーの音量・パン・メーター（Phase 99）
#include "ColourSwatchButton.h"   // 8.125：色帯から出すパレット（Phase 161）

#include <optional>   // 8.150：掴んだマーカーが「無い」ことを返す（Phase 188）

//==============================================================================
/**
    設計書2.3.1「アレンジビュー」のタイムライン部分。

    Phase 4cで、クリップのトリム（左右端のドラッグによる伸縮）と
    トラックをまたいだ移動に対応した。Phase 4dでクリップの実際の再生
    （プレイヘッド表示）に対応し、Phase 4eでフェードイン/アウトの
    ハンドル操作に対応。Phase 4fでヒットポイント（トランジェント）の
    表示と手動編集に対応した（仕様書5.5.1）。
*/
class TimelineComponent : public juce::Component,
                           public juce::DragAndDropTarget,
                           // 8.154：**DAWの外からのドラッグ**（Phase 192／本人の要望）。
                           // `DragAndDropTarget`はアプリの中の受け口で、
                           // **エクスプローラから落としたものはここへ来ません**（別の口）
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::ValueTree::Listener,
                           private juce::ScrollBar::Listener
{
public:
    TimelineComponent (ProjectModel& projectToUse, WaveformCache& cacheToUse, SelectionState& selectionToUse);
    ~TimelineComponent() override;

    void paint (juce::Graphics& g) override;

    /** 8.70：並べ替えの**予告線と、掴んだヘッダーの写し**（Phase 109）。

        **子の上へ描くこと。** ヘッダーのフェーダーとメーター（`TrackHeaderControls`）は
        本物の子コンポーネントなので、`paint()`（地）に描くと**その下に隠れます**。
        `TrackRackComponent`・`ConsoleView`の予告線と同じ理由です（8.66・8.67）。 */
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    /** 8.62：**掴める場所ではカーソルを変える**（Phase 100）。

        行の下端の帯は4pxしかなく、**見た目には何も無い**ので、
        カーソルが変わらないと掴めることに気づけません。 */
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

    /** 表示倍率を上げる／下げる（画面中央を軸に拡大縮小する）。 */
    void zoomIn();
    void zoomOut();

    /** 全クリップが画面に収まる倍率に合わせ、先頭までスクロールする。 */
    void zoomToFit();

    /** トラック/クリップが追加・変更された後に呼び出し、再描画する。 */
    void refresh();

    /** 再生位置（秒）を設定し、プレイヘッド（縦線）の表示位置を更新する。 */
    void setPlayheadSeconds (double seconds);

    /** 選択状態を解除する（プロジェクトの入れ替え時など）。 */
    void clearSelection();

    /** 現在**オーディオ**クリップが選択されているか。
        MIDIクリップを選んでいる場合はfalseになる（フェードやヒットポイントは
        オーディオクリップ専用のため、呼び出し側が種別を意識せずに済むようにしている）。 */
    bool hasSelectedClip() const { return selectedTrackIndex >= 0 && selectedClipIndex >= 0 && ! selectedIsMidi; }

    /** 選択中のオーディオクリップを返す。hasSelectedClip()がtrueのときのみ呼ぶこと。 */
    AudioClip getSelectedClip() const { return project.getTrack (selectedTrackIndex).getClip (selectedClipIndex); }

    /** 現在**MIDI**クリップが選択されているか（Phase 15）。 */
    bool hasSelectedMidiClip() const { return selectedTrackIndex >= 0 && selectedClipIndex >= 0 && selectedIsMidi; }

    /** 選択中のMIDIクリップが載っているトラックの番号。hasSelectedMidiClip()がtrueのときのみ有効。 */
    int getSelectedTrackIndex() const { return selectedTrackIndex; }

    /** 選択中のMIDIクリップの、トラック内での番号。hasSelectedMidiClip()がtrueのときのみ有効。 */
    int getSelectedMidiClipIndex() const { return selectedClipIndex; }

    /** MIDIクリップがダブルクリックされたときに呼ばれる（設計書2.3：ピアノロールを開く）。
        引数はトラック番号とトラック内のクリップ番号。 */
    std::function<void (int trackIndex, double timelineSeconds)> onMidiClipDoubleClicked;

    /** 8.49：オーディオクリップの右クリックから、オーディオエディタで開く（Phase 88）。
        **MIDIクリップのダブルクリックと同じ入口の形**にしてある（8.39）。 */
    std::function<void (int trackIndex, int clipIndex)> onAudioClipEditorRequested;

    /** 仕様書5.9：ループ範囲・入切が変わったときに呼ばれる（Phase 48）。
        MainComponentがオーディオエンジンへ反映する。 */
    std::function<void()> onLoopChanged;

    /** 仕様書5.9：ルーラーのメニューからマーカーを頼まれたときに呼ばれる（Phase 50）。

        **ここでマーカーを作らずにMainComponentへ回すのは**、
        名前を入力するダイアログを含めた挿入の手順を1箇所にまとめておくため
        （ショートカットからの挿入と同じ経路を通す）。 */
    std::function<void (double timeSeconds, bool askForName)> onInsertMarkerRequested;

    //==========================================================================
    // 仕様書6.2：ツール切り替えと複数選択（Phase 51）

    //==========================================================================
    // 8.125：トラックヘッダーの幅（Phase 161／改善案38）

    int getTrackHeaderWidth() const { return trackHeaderWidth; }

    /** 8.199：**ルーラー左端の角**（トラックヘッダーの上）。Phase 234／改善案5の3。

        ここには時間表示の切り替え（`Bars` / `Time`）しか置いていませんでしたが、
        本人の指定で**「+ Track」「+ Audio」もここへ収めます**：

            [        Bars        ]
            [+ Track] [+ Audio]

        置くのは`ArrangeView`の仕事なので（**押したときに動くのはあちら**）、
        **場所だけ教えます。** 幅はヘッダーと同じで、ドラッグで変わります（8.125）。 */
    juce::Rectangle<int> getCornerArea() const { return { 0, 0, trackHeaderWidth, rulerHeight }; }

    /** 角のうち、`Bars`の下に空けてある段（外から置くもの用）。 */
    juce::Rectangle<int> getCornerButtonRow() const
    {
        return getCornerArea().reduced (4, 0).withTop (timeFormatBottom + 2)
                              .withTrimmedBottom (2);
    }

    /** 幅を決める。**上下限で丸めます**（値はそのまま入りません）。 */
    void setTrackHeaderWidth (int newWidth);

    /** 幅が変わったときに呼ばれる（設計書2.5：覚えておくのは呼び出し側の仕事）。 */
    std::function<void()> onTrackHeaderWidthChanged;

    void setEditTool (EditTool newTool);

    EditTool getEditTool() const { return editTool; }

    /** ツールが変わったときに呼ばれる（ツールバーのボタンの見た目を合わせる）。 */
    std::function<void()> onEditToolChanged;

    /** ツールがメニューから選ばれたときに呼ばれる（Phase 68）。

        **自分では切り替えない。** ツールはピアノロールにも効くので、
        値を配るのは`MainComponent`の仕事（Phase 52で踏んだのと同じ話）。 */
    std::function<void (EditTool)> onEditToolSelected;

    /** クリップの「複製」が選ばれたときに呼ばれる（Phase 71）。

        **複製の実装はMainComponentに1つだけ**（`duplicateSelectedClip()`）。
        ここで同じことを書くと、ショートカット（D）とメニューで結果が食い違う。 */
    std::function<void()> onDuplicateClipRequested;

    /** 8.76：クリップの「カーソル位置で分割」「次のクリップと結合」（Phase 116／改善案④㉟）。

        **実装はMainComponentに1つだけ**（`splitSelectedClip()`／`mergeSelectedClip()`）。
        ショートカット（Alt+X／Alt+M）と同じ経路を通るので、
        **メニューから呼んだときだけ結果が違う**ということが起きません（8.32と同じ形）。 */
    std::function<void()> onSplitClipRequested;
    std::function<void()> onMergeClipRequested;

    /** 8.77：画面下の帯にひとこと出したいときに呼ぶ（Phase 117）。

        **できなかったときに黙らないため**のものです。`ArrangeView::showStatusMessage()`へ
        つながっています（帯を持っているのはあちらなので、ここでは持たない）。 */
    std::function<void (const juce::String& message)> onStatusMessage;

    /** 8.77：選んでいる**MIDIクリップだけ**をまとめて上下させる（Phase 117／改善案㉝）。

        インスペクタのボタンからも、クリップのメニューからも**ここを通ります**
        （「どのクリップに効かせるか」を決める場所を1つにするため。8.32）。

        **1つでも動かせないクリップは飛ばします**（`MidiClip::transposeNotes()`が
        0〜127を外れるものを断る）。動かせたものが1つも無ければ、
        `onStatusMessage`で理由を出します——黙って何も起きないのがいちばん分かりにくいので。 */
    void transposeSelectedMidiClips (int semitones);

    /** 8.147：**選んでいるものを上下させる**（Phase 185／改善案㉞）。**入口はここ1本**。

        MIDIとオーディオで効かせる先が違うので、**振り分けはここが持ちます**：

        | 選んでいるもの | 何が動くか |
        |---|---|
        | MIDIトラックの時間範囲 | その範囲のノート（8.94） |
        | オーディオクリップ | そのクリップの`transpose`（音は裏で作り直す） |
        | 両方 | **両方**（混ぜて選んでいるなら、それが望みのはず） |

        インスペクタのボタンもクリップのメニューもここを通ります（8.32）。 */
    /** 8.149：選んでいるオーディオクリップを**`bars`小節ぴったりに伸縮させる**
        （Phase 187／8.48。仕様書5.5.1）。

        **いちばん使う場面がこれです**（4小節のループを、この曲の速さで4小節に）。
        長さは**クリップが置いてある小節から数えます**——曲の途中でテンポや拍子が
        変わるので、「4小節ぶんの秒数」は置き場所で違います（8.102）。 */
    bool fitSelectedClipsToBars (int bars);

    /** 8.149：伸縮を1.0へ戻す（Phase 187）。**長さも一緒に戻します。** */
    bool resetSelectedClipStretch();

    void transposeSelection (int semitones);

    /** 8.147：選んでいるオーディオクリップの`transpose`を動かす（Phase 185／改善案㉞）。

        `resetToZero`なら0へ（＝素のファイルに戻る）。**MIDIクリップは飛ばします。**
        `changeSelectedClipGain()`と同じ形にしてあるので、並べて読めます。
        1つでも動いたらtrueを返します。 */
    bool changeSelectedClipTranspose (int semitones, bool resetToZero);

    //=========================================================================
    // 8.78：**クリップのグループ**（Phase 118/改善案㊱。仕様書5.5）
    //
    // **「結合」とは別もの**です（`mergeClipWithNext()`は2つを1つのクリップにする）。
    // グループは**まとめて選ばれる**ようになるだけで、クリップはそのままです。
    //
    // 移動・削除・複製・カット/コピーは**前から複数選択に効く**ので、
    // 選択に足すだけで全部まとめて動きます。
    // **操作ごとに「グループなら一緒に」と書かないこと**（足すたびに忘れる。8.2）。

    /** 選んでいるクリップを1つのグループにまとめる（2つ以上選んでいるときだけ）。 */
    void groupSelectedClips();

    /** 選んでいるクリップのグループを解く。 */
    void ungroupSelectedClips();

    /** 8.79：**選んでいるクリップを1つにまとめる**（Phase 119/改善案④の続き）。

        `Track::mergeClipWithNext()`を、選んだぶんだけ繰り返し呼びます。

        **条件は3つ**（どれか欠けたら、理由を出して何もしません）：
          - 2つ以上選んでいる
          - **全部が同じトラック・同じ種別**（オーディオ同士／MIDI同士）
          - **あいだに選んでいないクリップが挟まっていない**

        3つ目が要るのは、`mergeClipWithNext()`が「トラック上の次のクリップ」を
        取り込む作りだからです。確かめずに繰り返すと、
        **選んでいないクリップまで飲み込みます**。 */
    void mergeSelectedClips();

    /** いま選んでいるものをまとめられるか（メニューの出し分け用）。 */
    bool canMergeSelectedClips() const;

    /** いま選んでいるものをグループにできるか（メニューとショートカットの出し分け用）。 */
    bool canGroupSelectedClips() const;

    /** 選んでいるものの中に、グループに入っているクリップがあるか。 */
    bool hasGroupedClipInSelection() const;

    //==========================================================================
    // 複数選択（Phase 51）
    //
    // **番号ではなくIDで覚える。** クリップは分割・削除・並べ替えで番号が変わるので、
    // 番号で持つと別のクリップを指したまま操作してしまう（1.32）。

    struct ClipRef
    {
        juce::String trackId;
        juce::String clipId;
        bool isMidi = false;

        bool operator== (const ClipRef& other) const
        {
            return trackId == other.trackId && clipId == other.clipId && isMidi == other.isMidi;
        }
    };

    /** いま複数選択されているクリップ。**1つだけ選んでいるときも入る**
        （「単数の選択」と「複数選択」を別々に持つと、必ず食い違う）。 */
    const std::vector<ClipRef>& getSelectedClips() const { return selectedClips; }

    void selectAllClips();
    void clearClipSelection();

    //==========================================================================
    // 仕様書6.2：カット／コピー／貼り付け（Phase 71／8.29の表）
    //
    // **入れ物は`EditClipboard`ひとつ**（ピアノロールのノートと共用）。
    // 種別が合わないものは貼り付けません。

    bool cutSelection();
    bool copySelection();

    /** その時刻へ貼り付ける。中身の種別に応じて振り分ける。 */
    bool pasteAt (double timeSeconds);

    /** 複数選択が変わったときに呼ばれる（コマンドの有効/無効の作り直し用）。 */
    std::function<void()> onClipSelectionChanged;

    /** 設計書2.3.5：コード区間をダブルクリックしたときに呼ばれる（Phase 43）。
        MIDIクリップ→ピアノロールと同じ考え方で、コードパッドを開く合図にする。
        渡すのは区間の開始時刻（秒）で、そこがコードを挿す位置になる。 */
    std::function<void (double startTimeSeconds)> onChordRegionDoubleClicked;

    /** クリップの移動・トリム・削除など、モデルに変更が加わった際に呼ばれる。 */
    std::function<void()> onModelChanged;

    /** 仕様書5.9：ルーラーをクリック／ドラッグして再生位置を変えたときに呼ばれる（Phase 18）。
        実際に再生位置を動かすのはAudioEngineの仕事なので、ここでは要求を伝えるだけ。 */
    std::function<void (double)> onSeek;

    //==========================================================================
    // 仕様書5.6：オートメーション（Phase 19）

    //==========================================================================
    // 仕様書5.6：オートメーション表示（Phase 26でトラックごとに変更）
    //
    // カーブはトラック行の上に**重ねて**描く（Ableton等と同じ重ね方）。
    //
    // Phase 26で決めたときの理由は「座標計算が『全トラックが同じ高さ』を前提にしていて、
    // 行を増やすと広範囲に影響が出るから」だったが、**その前提はPhase 58で外れた**
    // （高さは`getRowHeight()`1箇所で決まる。8.18）。
    // 専用トラックにするかは、改めて8.1のD3で判断すること。
    //
    // **Phase 26で「プロジェクト全体で1つ」から「トラックごと」へ変えた。**
    // それ以前はArrangeのツールバーに1つだけコンボボックスがあり、
    // 選んだパラメータが全トラックに適用されていたため、
    // 「このトラックは音量、あのトラックはプラグインのつまみ」を同時に見られなかった。
    // 対象はTrack（とMASTERBUS）のプロパティとして持つので、保存・復元もされる。

    /** トラックヘッダーの「A」ボタンが押されたときに呼ばれる（対象を選ぶメニューは
        呼び出し側が出す）。trackIndexがマスター行の場合はマスターを指す。

        buttonScreenBoundsは押されたボタンの**画面座標**。メニューをボタンの
        すぐ隣に出せるよう、呼び出し側へ渡している（受け手はTimelineComponentの
        内部座標を知らないので、こちらで変換しておく必要がある）。 */
    std::function<void (int trackIndex, juce::Rectangle<int> buttonScreenBounds)> onAutomationButtonClicked;

    /** 設計書2.3.1：トラック一覧の末尾の「+ 新しいトラック」が押されたときに呼ばれる（Phase 31）。

        ツールバーの「+ Track」と**同じメニューを出す**こと（入口が2つあるだけで、
        できることは同じ）。`rowScreenBounds`は画面座標（onAutomationButtonClickedと同じ理由）。 */
    std::function<void (juce::Rectangle<int> rowScreenBounds)> onAddTrackClicked;

    /** トラックヘッダーが右クリックされたときに呼ばれる（Phase 33）。
        削除・並べ替えのメニューは呼び出し側が出す（モデルを触るのはこのクラスの責務ではない）。 */
    std::function<void (int trackIndex, juce::Rectangle<int> headerScreenBounds)> onTrackHeaderRightClicked;

    /** 8.60：トラックヘッダーの「i」で、インスペクタを開く／閉じる（Phase 97）。

        **パネルの開閉を知っているのは`MainComponent`だけ**なので、ここでは頼むだけ
        （このクラスは自分の外側の配置を知らない）。

        `allowClose`は「**押したトラックが、いま出ているものと同じか**」。
        同じなら開閉の切り替え、違うトラックなら開いたまま中身だけ差し替える
        （見たくて押したのに閉じる、という取り違えを防ぐ）。 */
    std::function<void (bool allowClose)> onInspectorRequested;

    /** 8.59：オートメーションの行のヘッダーが右クリックされたときに呼ばれる（Phase 96）。

        **メニューを出すのは呼び出し側**（`ArrangeView`）。対象の一覧を組むのに
        プラグインのパラメータ名が要り、そこは`AudioEngine`しか知らないため
        （`onAutomationButtonClicked`と同じ理由）。 */
    std::function<void (int trackIndex, int laneOrdinal, juce::Rectangle<int> headerScreenBounds)>
        onAutomationRowRightClicked;

    //==========================================================================
    // 仕様書5.4：入力モニタリング（Phase 26でトラックヘッダーへ移動）
    //
    // エンジン側のモニタリングは今のところプロジェクト全体で1つだが、
    // 録音待機できるトラックも1本だけなので、**録音待機中のトラックの
    // ヘッダーに出す**ことで「どこへ録るか」と「モニターするか」が並んで見える。
    // TimelineComponentにAudioEngineを持ち込まないよう、コールバックで受け渡す。

    std::function<bool()> isInputMonitoringEnabled;
    std::function<void (bool)> onInputMonitoringToggled;

    //==========================================================================
    // 仕様書5.7：ヘッダーのパンとレベルメーター（Phase 58／8.1のC16）
    //
    // **AudioEngineはここへ持ち込まない**（入力モニタリングと同じ方針）。
    // レベルは外から押し込んでもらい、Touch/Latchの記録開始も外へ投げる。

    /** 仕様書5.6：つまみを掴んだ／離したときに呼ばれる（Touch/Latchの記録判定用）。

        **Consoleのつまみと同じ扱いにすること。** 片方だけ記録が始まると、
        「どこで動かしたか」で結果が変わる（HANDOVER 8.12の「入口が2つ」）。

        8.61：**パン専用だったのを、対象の識別子を渡す形にした**（Phase 99）。
        ヘッダーに音量フェーダーが入ったので、パンと音量で2組作らずに済む。 */
    std::function<void (const juce::String& trackId, const juce::String& targetId)> onAutomationTouchStart;
    std::function<void (const juce::String& trackId, const juce::String& targetId)> onAutomationTouchEnd;

    /** ヘッダーのメーターへ最新のレベルを流し込む（呼び出し側のタイマーから）。

        **並べ直しもここでやっている。** メーターだけは子コンポーネントなので、
        行が動いたら位置も追わせる必要がある。`resized()`や`refresh()`でも
        並べ直しているが、**どれか1つを忘れても、次のタイマーで直る**形にしてある
        （手で全部の経路を拾おうとすると、必ずどれかを取りこぼす）。

        `getLevel`は「trackId・チャンネル → 振幅」を返す関数。 */
    void refreshHeaderMeters (const std::function<float (const juce::String&, int)>& getLevel);

    /** どこか1トラックでもレーンを開いていれば true。マスター行を出すかの判断に使う。 */
    bool isShowingAutomation() const;

    //==========================================================================
    // 仕様書4.4・6章：ドラッグ&ドロップの受け口（Phase 21）

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    /** プラグインがトラックへ落とされたときに呼ばれる。
        実際に挿すのはMainComponentの仕事なので、ここでは要求を伝えるだけ。 */
    /** 仕様書4.4：ブラウザからプラグインを落としたとき（Phase 21）。

        8.123：**`trackId`が空のことがある**（Phase 158／改善案7）。
        トラックの無い空白へ落とした合図で、**音源なら受け手がトラックごと作る**。
        音源かどうかはここでは分からない（識別子しか持っていない）ので、
        判断は`ArrangeView`から先の仕事。 */
    std::function<void (const juce::String& trackId, const juce::String& pluginIdentifier)> onPluginDropped;


    //==========================================================================
    // 8.154：**DAWの外からのドラッグ**（Phase 192／本人の要望）
    //
    // JUCEは「アプリの中のドラッグ」と「OSからのドラッグ」を**別の口**で受けます。
    // ブラウザパネルからの落とし込み（Phase 21）は上の`DragAndDropTarget`、
    // エクスプローラからのものはこちらです。
    //
    // **行のハイライトと落とし先の判断は共用**しています（`updateFileDragRow()`）
    // ——2つ書くと、片方だけ「音声以外の行にも置ける」といった食い違いになります。

    /** 拡張子から「音声として読めるか」を見る。**中身は開きません**——
        ドラッグ中はマウスを動かすたびに呼ばれます。 */
    bool isReadableAudioFile (const juce::File& file) const;

    /** 落とし先の行を決めてハイライトする（中からのドラッグと共用の判断）。 */
    void updateFileDragRow (int y);

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** オーディオファイルが落とされたときに呼ばれる。
        trackIdが空文字なら「トラックを指定せずに落とされた」（呼び出し側が決める）。

        8.154：**複数まとめて来ます**（Phase 192）。1つのときも要素1つの配列です
        ——「1つのときだけ別の口」にすると、受け手が2通りを見ることになります（1.27）。

        8.232：`intoNewTrack`は**空白の場所へ落とされた**という合図です（Phase 250）。
        音源プラグインを空白へ落としたときと同じ扱いで、**トラックごと作ります**。

        > **`trackId`が空なだけでは足りません。** あれは
        > 「行の上ではなかった」（ルーラーやヘッダーの上を含む）という意味で、
        > **そのときの行き先は「最初のオーディオトラック」**でした。
        > 空白へ落としたときだけ作りたいので、**別の合図が要ります。** */
    std::function<void (const juce::StringArray& paths, const juce::String& trackId,
                         double startTime, bool intoNewTrack)> onFilesDropped;

private:
    // Seekはクリップの編集ではなく再生位置の変更（Phase 18）。
    // AutomationPointはオートメーションの点の移動（Phase 19）。
    // どちらも選択中のクリップを一切触らない点に注意。
    // CurveHandleは曲がり具合のつまみ（Phase 77）。
    // 8.57：AutomationPaintはペンでのなぞり書き（Phase 95／D14）。
    enum class DragMode { None, Move, TrimLeft, TrimRight,
                          /** 8.149：**Altを押しながら右端**（Phase 187/8.48）。
                              トリムと違い、**中身ごと引き伸ばす**（`clipStretch`）。 */
                          StretchRight,
                          /** 8.150：クリップ上端の取っ手を掴んで、ワープマーカーを動かす
                              （Phase 188／8.48）。 */
                          WarpMarker,
                          FadeIn, FadeOut, Seek, AutomationPoint,
                          AutomationPaint,
                          /** 8.62：ヘッダーの下端を掴んで行の高さを変える（Phase 100）。 */
                          ResizeTrack,
                          CurveHandle,
                          /** トラックヘッダーを掴んでの並べ替え（Phase 34）。 */
                          ReorderTrack,
                          /** 8.61：`HeaderPan`は**Phase 99で無くなりました**。
                              ヘッダーのパンがConsoleと同じ`ValueEntrySlider`になり、
                              ドラッグはあちらが自分で受け取るためです。 */
                        };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    //==========================================================================
    // 座標変換。ズーム（pixelsPerSecond）とスクロール（scrollStartSeconds /
    // verticalScrollPixels）を考慮した変換は、必ずこの4つを通す。
    // 直接 `時刻 * pixelsPerSecond` と書くと、スクロール量の加味漏れが起きるため。
    int timeToX (double timeSeconds) const;
    double xToTime (int x) const;
    int getTrackRowY (int trackIndex) const;
    int getTrackIndexForY (int y) const;

    /** その行**全体**の高さ（Phase 58／8.1のC15。Phase 94でオートメーションを含めた）。

        **行の高さを直に書かないこと。** Phase 57まで「全部の行が同じ高さ」を
        前提に`行番号 × trackRowHeight`で座標を出していたが、
        コードトラックだけ半分にしたので、その前提はもう成り立たない。
        1行でも高さの違うものが混ざると、**それより下の行がすべてずれる**。

        8.56：**オートメーションの行のぶんも含む**（Phase 94／D3）。
        `getTrackAreaHeight() + getAutomationRowsHeight()`。
        こうしておくと、位置・当たり判定・スクロール範囲は今までの計算のまま、
        **オートメーションの行を足したぶんだけ下の行が下がる**。

        マスター行と範囲外の行番号には標準の高さを返す
        （`getAddTrackRowBounds()`が`getNumRows()`を渡してくる）。 */
    int getRowHeight (int rowIndex) const;

    /** その行のうち、**クリップとトラックヘッダーが載る部分**の高さ（Phase 94／D3）。

        Phase 93までの`getRowHeight()`と同じもの。**クリップやヘッダーの位置は
        必ずこちらを使う**こと（`getRowHeight()`を使うと、レーンを開いた行だけ
        クリップが縦に伸びる）。 */
    int getTrackAreaHeight (int rowIndex) const;

    //==========================================================================
    // 仕様書5.2.3：コードトラックの固定表示（Phase 60／8.20）
    //
    // **小節バー（ルーラー）と同じ扱い**にしてある：ルーラーの真下に置き、
    // 縦スクロールしても動かない。下の方のトラックを触っている間も進行が見える。
    //
    // モデル側が「コードトラックは先頭」を守っている（`ProjectModel::moveTrack()`）ので、
    // ここでは**行番号0だけを特別扱い**すればよい。

    /** 固定表示するコードトラックの行番号。無ければ-1。 */
    int getPinnedRowIndex() const;

    /** 固定行が占める高さ（無ければ0）。 */
    int getPinnedRowsHeight() const;

    /** スクロールする行が始まるY座標（ルーラー＋固定行）。

        **`rulerHeight`を「行の始まり」として使っていた場所は、ここへ置き換えること。**
        置き換え漏れがあると、固定行のぶんだけ判定がずれる。 */
    int getScrollableTop() const;

    /** 縦スクロールで見えている高さ（固定行のぶんを引いたもの）。 */
    int getScrollableAreaHeight() const;

    /** 固定表示のコードトラックを、スクロールする行の上に描く（Phase 60）。

        **ルーラーと同じく最後のほうに描く。** 先に描くと、上へスクロールしてきた
        行がこの帯の上に乗ってしまう。 */
    void drawPinnedChordRow (juce::Graphics& g);

    /** 1本のコードトラックのコード区間を描く（固定行と通常の描画で共有）。 */
    void drawChordRegionsForTrack (juce::Graphics& g, int trackIndex);

    /** 8.201：フォルダの行に、中身のまとまりを描く（Phase 235／改善案5の10）。
        **見た目だけ**——掴めず、動かせません（本人の指定：「機能面はいらない」） */
    void drawFolderSummaryBlock (juce::Graphics& g, int trackIndex);

    /** クリップを描画する領域（トラックヘッダー・ルーラー・横スクロールバーを除いた部分）。 */
    juce::Rectangle<int> getTimelineArea() const;

    /** 仕様書5.9：時間目盛り（ルーラー）の領域。タイムライン部分の上端に帯として置く。 */
    juce::Rectangle<int> getRulerArea() const;

    /** ルーラーを描く。小節/拍表示とタイムコード表示を切り替えられる（仕様書5.9）。 */
    void drawRuler (juce::Graphics& g);

    /** 仕様書5.9：背景の小節頭・拍の縦ライン（Phase 62／8.1のC7）。

        **クリップより先に描くこと**（地の一部なので、クリップに隠れてよい）。
        目盛りの計算は`drawRuler()`と揃えてある：独自に計算すると、
        拡大縮小したときにルーラーの数字と線の位置がずれる。 */
    void drawTimelineGrid (juce::Graphics& g);

    /** ルーラーのクリック位置から再生位置を決める。**スナップに寄せる**（Phase 54）。 */
    void seekToX (int x);

    /** 再生位置を指定の時刻へ動かす。**寄せない。**

        マーカーへのジャンプのように「決まった時刻へ動かす」ものはこちらを呼ぶこと。
        画面の座標を経由すると、スナップが効いて狙った位置からずれる（8.14）。 */
    void seekToTime (double seconds);

    //==========================================================================
    // 8.56：オートメーションの行（Phase 19。**Phase 94／D3で専用の行になった**）
    //
    // Phase 93までは、トラックが1つだけ選んだ対象を**そのトラック行に重ねて**
    // 描いていた。そのあいだクリップは触れず、パラメータも1つしか見られなかった。
    //
    // **Phase 94からは、トラック行の下にぶら下がる専用の行**になっている：
    //
    //     ┌─ トラック行（クリップ。今までどおり触れる）──────┐
    //     ├─ Volume の行（ヘッダー＋線）────────────┤
    //     ├─ Pan の行 ─────────────────────┤
    //     └─ 次のトラック行 ──────────────────┘
    //
    // **行番号の空間は増やしていない**（1トラック＝1行のまま）。
    // `getRowHeight()`がレーンのぶんを含んだ高さを返すので、
    // 位置・当たり判定・スクロール範囲の計算は今までのものがそのまま効く。

    /** オートメーションの行1つを指す。

        **番号ではなくこの組で持ち回ること。** `rowIndex`だけだと
        「そのトラックの何番目のレーンか」が落ちて、Volumeを触ったつもりで
        Panを書き換えることになる。 */
    struct AutomationRowRef
    {
        int rowIndex = -1;   ///< ぶら下がっているトラックの行番号（マスターは`getMasterRowIndex()`）
        int ordinal = -1;    ///< そのトラックの中で上から何番目のレーンか

        bool isValid() const noexcept { return rowIndex >= 0 && ordinal >= 0; }

        bool operator== (const AutomationRowRef& other) const noexcept
        {
            return rowIndex == other.rowIndex && ordinal == other.ordinal;
        }
    };

    /** その行にぶら下がっているレーンの本数。 */
    int getNumAutomationRows (int rowIndex) const;

    /** レーンの行が占める高さの合計（無ければ0）。 */
    int getAutomationRowsHeight (int rowIndex) const;

    /** そのレーンの行（ヘッダーを含む全幅）。 */
    juce::Rectangle<int> getAutomationRowBounds (AutomationRowRef ref) const;

    /** そのレーンのうち、カーブを描く範囲（ヘッダーの右・上下に少し余白）。 */
    juce::Rectangle<int> getAutomationLaneArea (AutomationRowRef ref) const;

    /** レーンの行のヘッダー部分（パラメータ名と「×」が入る）。 */
    juce::Rectangle<int> getAutomationHeaderBounds (AutomationRowRef ref) const;

    /** レーンを閉じる「x」の位置。 */
    juce::Rectangle<int> getAutomationCloseButtonBounds (AutomationRowRef ref) const;

    /** 8.59：レーンをバイパスする「B」の位置（Phase 96）。 */
    juce::Rectangle<int> getAutomationBypassButtonBounds (AutomationRowRef ref) const;

    /** 8.59：そのレーンの行の色（Phase 96）。

        **入っていなければ親トラックの色**へ落とす。**この1箇所で面倒を見る**ので、
        呼ぶ側は「持っているか」を気にしなくてよい（`AutomationLane::getColourString()`は
        入っていなければ空文字を返すだけ）。 */
    juce::Colour getAutomationRowColour (AutomationRowRef ref) const;

    //==========================================================================
    // 8.59：レーンの行の選択（Phase 96）
    //
    // **トラックと同じように選べる**（左クリックで選択、右クリックでメニュー）。
    // インスペクタへはSelectionState経由で伝わる。

    /** 選んでいるレーン。**IDで持つ**（番号は並べ替えでずれる。1.32）。
        `selectedLaneTrackId`が空文字ならマスター、
        `selectedLaneTargetId`が空文字なら「レーンは選んでいない」。 */
    juce::String selectedLaneTrackId;
    juce::String selectedLaneTargetId;

    bool isAutomationRowSelected (AutomationRowRef ref) const;

    /** レーンの行を選ぶ（クリップ・トラックの選択は解ける）。 */
    void selectAutomationRow (AutomationRowRef ref);

    /** Y座標からレーンの行を引く。レーンの上でなければ`isValid()`がfalse。 */
    AutomationRowRef findAutomationRowAtY (int y) const;

    /** 正規化値（0〜1）↔ Y座標。値が大きいほど上に来る。 */
    int automationValueToY (AutomationRowRef ref, float value) const;
    float yToAutomationValue (AutomationRowRef ref, int y) const;

    /** レーンの行のヘッダー（名前と「×」）。**行の描画と同じ順で呼ぶこと。** */
    void drawAutomationRowHeader (juce::Graphics& g, AutomationRowRef ref);

    /** レーンの中身（線と点）。**クリップと同じ、切り抜いた範囲の中で描く。** */
    void drawAutomationCurve (juce::Graphics& g, AutomationRowRef ref);

    /** クリック位置にオートメーションの点があるか探す。 */
    bool hitTestAutomationPoint (juce::Point<int> position, AutomationRowRef& rowOut,
                                  int& pointIndexOut) const;

    /** 8.59：座標が**レーンの線の上か**（Phase 96）。

        `valueOut`にはその時刻の**線の値**（0〜1）が入る。点はここへ置くので、
        **押した高さではなく線の上に乗る**（線から外れたところを押しても増えない）。

        点が1つも無いレーンでは、現在のフェーダー値の水平線が「線」になる
        （そこを押せば最初の点が置ける）。 */
    bool hitTestAutomationLine (AutomationRowRef ref, juce::Point<int> position, float& valueOut) const;

    /** 線の当たり判定の太さ（px）。**点（`automationHitRadius`）より細くする**：
        同じところに点と線があるときは、点を掴めるほうが自然。 */
    static constexpr int automationLineHitTolerance = 6;

    /** マスター用の行番号。オートメーション表示中だけ、トラックの後ろに1行増える。
        **高さは標準のまま**（`getRowHeight()`が範囲外の行番号にそれを返す）。 */
    int getMasterRowIndex() const { return project.getNumTracks(); }
    bool isMasterRow (int rowIndex) const { return rowIndex == getMasterRowIndex(); }

    /** 画面に並ぶ行の数（マスターのレーンを開いているときだけ1つ多い）。
        **「+ 新しいトラック」の行は含まない**（トラックとして扱える行ではないため）。 */
    int getNumRows() const;

    /** 縦スクロールに必要な高さ。行の合計に「+ 新しいトラック」の行を足したもの。
        **updateScrollBars()とsetVerticalScrollPixels()の両方がここを通すこと**
        （別々に計算すると、片方だけ直したときに末尾が見えなくなる）。 */
    int getContentHeightPixels() const;

    /** 設計書2.3.1：トラック一覧の末尾に出す「+ 新しいトラック」の行（Phase 31）。 */
    juce::Rectangle<int> getAddTrackRowBounds() const;

    /** その行のオートメーション対象。無ければ空文字。
        トラックとマスターの違いをここで吸収する（Phase 26／94）。 */
    juce::String getAutomationTargetFor (AutomationRowRef ref) const;

    /** その行のオートメーションレーン。トラックとマスターの違いをここで吸収する。
        見つからない場合は`state.isValid()`がfalseのものを返す。 */
    AutomationLane getAutomationLaneFor (AutomationRowRef ref) const;

    /** その行のフェーダー現在値（正規化済み）。点が無いときの水平線に使う。 */
    float getCurrentAutomationValueFor (AutomationRowRef ref) const;

    // ドラッグ中のオートメーション点
    AutomationRowRef automationDragRow;
    int automationPointIndex = -1;

    /** 8.56：**最後に触ったレーンの行**（Phase 94）。貼り付け先を決めるのに使う。

        選択中のトラックから決めていたが、1トラックに複数のレーンが並ぶと
        「どのレーンへ貼るか」が決まらない。**触った行を覚えておく**のが素直。 */
    AutomationRowRef lastEditedAutomationRow;

    static constexpr float automationPointRadius = 4.0f;
    static constexpr float automationHitRadius = 8.0f;
    static constexpr int automationVerticalMargin = 5;

    /** 8.56：レーンの行のヘッダーに置く「×」の大きさ（Phase 94）。 */
    static constexpr int automationCloseButtonSize = 13;

    //==========================================================================
    // 8.57：レーンでのツールと複数選択（Phase 95／D14）
    //
    // **ピアノロール下のレーンと同じ形にしてある**（8.38／Phase 78）。
    // あちらで決めたことをそのまま持ってきているので、迷ったら8.38を読むこと。
    //
    //   矢印 … ドラッグで**点の範囲選択**（押しただけなら点を1つ置く）
    //   ペン … **なぞり書き**（通った場所に点を置く。間引きとならしを入れてある）
    //   消しゴム … なぞった点を消す
    //
    // **「押しただけ」と「ドラッグ」は離すまで区別できない**ので、
    // 押した時点では構えるだけにして、`mouseUp()`で振り分ける（8.38と同じ）。

    /** 選んでいるレーンの点。**番号ではなくValueTreeで持つ**（1.32）。
        レーンをまたいで選べる（囲んだ範囲が1本のレーンに収まる作りなので、
        実際には1本ぶんだが、消すときに親を辿れるので跨いでも壊れない）。 */
    std::vector<juce::ValueTree> selectedAutomationPoints;

    bool isAutomationPointSelected (const juce::ValueTree& pointState) const;
    void clearAutomationSelection();

    /** 消えた点を選択から外す。**触る前に必ず通すこと**（Undoで消えていることがある）。 */
    void pruneAutomationSelection();

    /** 囲んだ範囲の点を選び直す。 */
    void applyAutomationRangeSelection (AutomationRowRef ref);

    /** 選んでいる点をまとめて消す（Deleteキー）。 */
    void deleteSelectedAutomationPoints();

    /** 選んでいる点のコピー（`alsoDelete`ならカット）。 */
    bool copyAutomationSelection (bool alsoDelete);

    /** 掴んだ点**以外**を同じだけ動かす。**離したときに1回だけ呼ぶ**（8.38）。 */
    void moveOtherSelectedAutomationPoints (double deltaTime, float deltaValue);

    /** ペンでのなぞり書き。 */
    void paintAutomationPointAt (AutomationRowRef ref, juce::Point<int> position);

    /** なぞった「あいだ」の点を消す。**両端を含めないこと**（8.38の落とし穴）。 */
    void removeAutomationPointsInTimeRange (AutomationRowRef ref, double fromTime, double toTime);

    /** 消しゴム：その座標にある点を消す。 */
    bool eraseAutomationPointAt (juce::Point<int> position);

    /** 矢印ツールで押したあと、離すまでの構え（8.38）。 */
    bool automationPendingAdd = false;
    juce::Point<int> automationPendingPosition;
    AutomationRowRef automationPendingRow;

    /** ラバーバンド（範囲選択）の状態。 */
    bool automationRangeSelecting = false;
    juce::Point<int> automationRangeAnchor;
    juce::Rectangle<int> automationRangeBounds;

    /** まとめて動かすための控え（掴んだ点**以外**の、掴んだ時点の位置）。 */
    struct AutomationPointOrigin
    {
        juce::ValueTree state;
        double time = 0.0;
        float value = 0.0f;
    };

    std::vector<AutomationPointOrigin> automationDragOthers;
    double automationDragAnchorTime = 0.0;
    float automationDragAnchorValue = 0.0f;

    /** ペンでのなぞり書きの状態。 */
    AutomationRowRef automationPaintRow;
    bool automationPaintStarted = false;
    int automationPaintLastX = 0;
    double automationPaintLastTime = 0.0;
    float automationPaintValue = 0.0f;

    /** なぞり書きの**点の細かさ**（px）と、**手ぶれのならし具合**。
        **ピアノロールと同じ値**にしてある（画面で操作感が変わらないように）。

        8.122：ピアノロールに合わせて**8pxから4pxへ**（Phase 157／改善案28）。
        理由は`PianoRollComponent::lanePaintMinPixels`に書いてある。
        **片方だけ変えないこと**——同じペンで描いているのに、
        画面によって線の細かさが違うことになる（1.27）。 */
    static constexpr int automationPaintMinPixels = 4;
    static constexpr float automationPaintSmoothing = 0.45f;

    //==========================================================================
    // 8.37：曲がり具合（Phase 77）。**ピアノロール下のレーンと同じ操作**にしてある

    /** 座標に重なっている曲がり具合のつまみを探す。
        `pointIndexOut`は**区間の手前の点**（形を持っているほう）の番号。 */
    bool hitTestCurveHandle (juce::Point<int> position, AutomationRowRef& rowOut,
                              int& pointIndexOut) const;

    /** つまみを掴んでいるあいだの状態。**両端の高さは掴んだ時点のもので固定**する
        （ドラッグ中に引き直すと、自分が曲げた結果を読んでまた曲げてしまう）。 */
    AutomationRowRef curveDragRow;
    int curvePointIndex = -1;
    float curveDragFromY = 0.0f;
    float curveDragToY = 0.0f;

    /** 全クリップが収まるのに必要な長さ（秒）。**中身がどこまであるか**を測るもの。 */
    double getContentLengthSeconds() const;

    /** 8.162：**横スクロールで行ける先**（Phase 200／本人の要望）。

        中身の長さとは別ものです。**いまの位置から1画面ぶん先までは、
        中身が無くても必ず行けます。** いまの位置から測るので、
        左へ戻ればここも縮みます（ピアノロール側と同じ考え方）。 */
    double getScrollableLengthSeconds() const;

    /** タイムラインの見えている幅（秒）。**3か所で同じ式を書いていたのでまとめた**。 */
    double getVisibleSeconds() const;

    /** 表示倍率を変更する。anchorXの位置にある時刻が動かないように、
        スクロール位置も合わせて調整する（マウス位置を軸にした拡大縮小）。 */
    void setZoom (double newPixelsPerSecond, int anchorX);

    void setScrollStartSeconds (double newStartSeconds);
    void setVerticalScrollPixels (int newOffset);
    void updateScrollBars();

    /** クリック位置にあるクリップを探す。オーディオ／MIDIどちらのクリップも対象で、
        どちらだったかはisMidiOutで返す（Phase 15）。 */
    bool hitTestClip (juce::Point<int> position, int& trackIndexOut, int& clipIndexOut, bool& isMidiOut) const;
    juce::Rectangle<int> getClipBounds (int trackIndex, int clipIndex) const;


    /** 8.42：移動中の行き先を、半透明で重ねて描く（Phase 82／C17）。

        **元のクリップは消しません。** どこから動かしたのかが分からないと、
        少し戻したいときに元の位置を狙えません（ノートと同じ形。8.13）。 */
    void drawClipDragPreview (juce::Graphics& g);


    /** 8.43：消しゴム（Phase 83／C11）。座標にあるクリップを1つ消す。
        消したらtrue（なぞり書きの途中で、同じものを何度も消そうとしないため）。 */
    bool eraseClipAt (juce::Point<int> position);
    /** 8.42：選んでいる他のクリップも、同じだけ時間方向へ動かす（Phase 82）。
        **掴んだクリップを書いた後に呼ぶこと**（3.1）。 */
    void moveOtherSelectedClipsByDrag (double deltaTime);

    /** 8.227：Ctrl＋ドラッグで複製するときの、**掴んだもの以外**（Phase 249）。

        `moveOtherSelectedClipsByDrag()`の複製版です。動かす側と**同じ約束**——
        掴んだものがトラックをまたいでも、**他は自分のトラックに残ります。** */
    struct SelectedClipSource
    {
        juce::String trackId;
        juce::ValueTree state;
    };

    /** **複製する前に集めること。** 複製はクリップを増やすので、
        走りながら足すと**足したものをまた複製し続けます。** */
    std::vector<SelectedClipSource> collectOtherSelectedClips (const juce::String& draggedClipId) const;

    /** 集めたものを、それぞれ自分のトラックへ`deltaTime`ずらして置く。
        できたものの`ClipRef`を返します（選び直すのに使います）。 */
    std::vector<ClipRef> duplicateCollectedClips (const std::vector<SelectedClipSource>& sources,
                                                  double deltaTime);
    /** 8.61：`trackColour`は**そのトラックの色**（Phase 99／改善案⑫）。
        地と波形に使う。**選択の枠はパープルのまま**にすること（設計書2.6）。 */
    void drawClip (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& sourceFilePath,
                   double offsetSeconds, double lengthSeconds, double fadeInSeconds, double fadeOutSeconds,
                   const juce::Array<double>& hitPoints, bool isSelected, float gainLinear,
                   bool isReversed, juce::Colour trackColour,
                    const WarpMap& clipMap = {},
                    bool isMono = false);   // 8.231：モノラル化したクリップは波形も1本（Phase 250）


    //==========================================================================
    // 8.91：MIDIは「ノートの塊」で描く（Phase 131）
    //
    // **四角はデータではありません。** そのつどノートから計算した見え方です。
    // だから**重なりようがなく、伸縮という操作が存在せず、空白が埋まれば勝手に1つ**に
    // なります。切れ目は**1小節ぶんの空き**で、**ピアノロールと同じ値**を使うこと（8.2）。

    std::vector<Track::NoteBlock> getNoteBlocksFor (int trackIndex) const;

    juce::Rectangle<int> getNoteBlockBounds (int trackIndex, const Track::NoteBlock& block) const;

    /** 塊1つを、中のノートのミニプレビュー付きで描く（`drawMidiClip()`は8.94で削除）。 */
    void drawNoteBlock (juce::Graphics& g, juce::Rectangle<int> bounds,
                        const Track& track, const Track::NoteBlock& block, juce::Colour trackColour,
                        juce::Rectangle<int> selectedPart);

    //==========================================================================
    // 8.92：ノートの塊を掴んで動かす（Phase 132）
    //
    // **クリップのドラッグ（`DragMode`）とは別立てにしてあります。** あちらは
    // 移動・トリム・フェード・複製を1つの状態機械で捌いていますが、塊にできるのは
    // **移動だけ**です（端はノートが決めるので、トリムという操作が存在しない）。
    // 混ぜると、効かない分岐を条件で潰して回ることになります。


    /** 8.94：その時刻にある塊の番号（無ければ-1）。**塊にIDは無い**ので、
        指すときは常に「いまのノートから計算した何番目か」になります。 */
    int findNoteBlockAt (int trackIndex, double timeSeconds) const;

    /** 時間の範囲でノートを消す／上下させる（塊のメニューとPhase 133の範囲編集で共用）。

        **塊にIDが無いので、「どのノートがこの塊のものか」は時間の範囲でしか言えません。** */
    void deleteNotesInRange (int trackIndex, double fromSeconds, double toSeconds);
    void transposeNotesInRange (int trackIndex, double fromSeconds, double toSeconds, int semitones);

    //==========================================================================
    // 8.93：時間範囲の選択と、範囲の移動・複製・削除（Phase 133）
    //
    // **クリップのドラッグの代わりです。** クリップという掴めるものが無くなったので
    // （8.91）、「ここからここまでを4小節後ろへ」をやる手段が要ります。
    //
    // **塊のドラッグ（8.92）とは役割が違います**：塊は「まとまりを1つ動かす」、
    // 範囲は「区切りを自分で決めて動かす」。**範囲は重なりを止めません**——
    // 窓を自分で選んでいるので、既にある音の上へ重ねるのも意図のうちです。

    /** MIDIトラックでの押下を引き受ける（範囲の移動・メニュー・作成）。引き受けたらtrue。

        **2回呼びます**：

        - **塊の当たり判定より先に**`allowCreate=false`で。
          後にすると、**範囲の中にノートがあるところは塊として掴まれる**ので、
          範囲を動かせるのが「音の無い隙間だけ」になります
          （たいていの範囲は音で埋まっているので、実質動かせない）
        - **空いている場所の処理で**`allowCreate=true`で。
          新しい範囲を引き始められるのは、塊の無いところだけです */
    bool handleTimeRangeMouseDown (const juce::MouseEvent& e, bool allowCreate);

    void dragTimeRange (juce::Point<int> mousePosition);
    void finishTimeRangeDrag (const juce::MouseEvent& e);

    void clearTimeRange();

    /** 8.96：**中身が無くなった範囲は外す**（Phase 136）。
        残すと**枠だけが宙に浮きます**——選んでいるつもりのものが、もう無い。 */
    void clearTimeRangeIfEmpty();
    bool isTimeRangeAt (int trackIndex, double timeSeconds) const;
    juce::Rectangle<int> getTimeRangeBoundsFor (int trackIndex) const;

    void showTimeRangeMenu (juce::Point<int> screenPosition);

    /** 範囲のノートとCCを、ずらして動かす／複製する。

        **先に集めてから書くこと。** 複製は`addNote()`で子が増えるので、
        走りながら足すと**足したものをまた複製し続けます**（無限に増える）。

        `targetTrackIndex`に別のMIDIトラックを渡すと、**そちらへ移す／複製する**
        （8.95）。-1なら同じトラック。 */
    void moveOrCopyNotesInRange (int trackIndex, double fromSeconds, double toSeconds,
                                  double deltaSeconds, bool copy, int targetTrackIndex = -1);

    /** 8.95：選んでいる範囲のノートとCCをクリップボードへ（`alsoDelete`でカット）。

        **`EditClipboard::Kind::notes`にノートとCCを混ぜて入れます。**
        ドラッグでの複製はCCも運ぶので（8.92）、Ctrl+Cだけ運ばないのは食い違います。 */
    bool copyTimeRange (bool alsoDelete);

    //==========================================================================
    // 8.124：マーカーの旗で区間を選ぶ（Phase 159／改善案5）

    /** 旗から**次の旗まで**（時刻で次のもの。無ければ曲の終わりまで）を、
        **全トラックで**選ぶ。

        **番号順ではなく時刻順で探すこと。** 旗はドラッグで動かせるので、
        番号の並びと時刻の並びは一致しません。 */
    void selectRangeFromMarker (int markerIndex);

    /** 全トラックの範囲を、まとめて動かす／複製する。

        MIDIはノートとCC、オーディオはクリップ。**オーディオは境目で割ってから**
        動かすので、区間の中側だけが運ばれます。 */
    void moveOrCopyRangeAllTracks (double fromSeconds, double toSeconds,
                                    double deltaSeconds, bool copy);

    /** 範囲の中のものを、全トラックから消す（オーディオは境目で割ってから）。 */
    void deleteRangeAllTracks (double fromSeconds, double toSeconds);

    /** 8.127：**区間の中のマーカー**を動かす／複製する（Phase 163／本人の要望）。

        **マーカーは区間の一部**として扱います。名前だけ元の場所に残ると、
        「Chorus」が何も無いところを指すことになるため。

        複製した側の名前には`'`を付けます（本人の指定）。
        既に`'`で終わっていれば、さらに1つ足します（`Chorus'` → `Chorus''`）。 */
    void moveOrCopyMarkersInRange (double fromSeconds, double toSeconds,
                                    double deltaSeconds, bool copy);

    /** 8.127：複製したマーカーの名前（Phase 163）。**1箇所に置くこと**——
        ドラッグでの複製と貼り付けで違う名前になると、後から見分けが付きません。 */
    static juce::String makeCopiedMarkerName (const juce::String& original);


    /** 8.124：区間を全トラックぶんクリップボードへ（`alsoDelete`でカット。Phase 160／改善案5）。

        **オーディオは元を割りません。** 入れ物の中で窓（オフセットと長さ）を
        詰めた複製を持つので、コピーしただけでプロジェクトは変わりません。 */
    bool copyRangeAllTracks (bool alsoDelete);

    /** 8.124：区間を貼り付ける（Phase 160／改善案5）。

        **同じ番号のトラックへ戻します**（上下を跨がない決まりに合わせる）。
        番号が無くなっていたら、そのぶんは捨てます。 */
    bool pasteRangeAllTracks (double timeSeconds);


    /** 1つのMIDIトラックぶんの移動／複製。**区切り（`beginAction`）は作りません**——
        全トラックぶんを1つのUndoにまとめる側が作ります（3.1）。 */
    void applyRangeMoveToMidiTrack (Track& track, Track& target,
                                     double fromSeconds, double toSeconds,
                                     double deltaSeconds, bool copy, bool crossesTracks);

    /** 1つのオーディオトラックぶんの移動／複製。**区切りは作りません**。 */
    void applyRangeMoveToAudioTrack (Track& track, double fromSeconds, double toSeconds,
                                      double deltaSeconds, bool copy);

    /** 8.128：1つのコードトラックぶんの移動／複製（Phase 164／本人の要望）。
        **区切りは作りません**。最後に`normaliseChordRegions()`を通します。 */
    void applyRangeMoveToChordTrack (Track& track, double fromSeconds, double toSeconds,
                                      double deltaSeconds, bool copy);


    /** 範囲の両端をまたいでいるクリップを、そこで割る。

        **境目ごとに集め直すこと。** 1回割るとクリップが1つ増えるので、
        先に集めた番号のままでは2つ目の境目でずれます（1.32）。 */
    void splitClipsAtRangeEdges (Track& track, double fromSeconds, double toSeconds);


    /** 8.95：MIDIの中身を、**いま選んでいるMIDIトラック**の指定時刻へ貼り付ける。

        選んでいなければ、範囲を引いてあるトラックへ。どちらも無ければ何もしません
        ——**どこへ入ったか分からない貼り付けは、無いほうがまし**です。 */
    bool pasteNotesAt (double timeSeconds);

    bool hasTimeRange = false;

    /** 8.158：**範囲がかかっているトラック**（Phase 196／本人の要望）。

        Phase 195までは`int`が1つでした（1トラックか、`timeRangeAllTracks`で全部かの2択）。
        本人の要望は「**枠で囲って複数、Ctrl＋クリックで塊を足す**」なので、
        **集合**にしてあります。

        **IDで持つこと**（番号は増減でずれる。1.32）。**空なら範囲は無い**。

        ### `timeRangeAllTracks`とは別ものです

        あちらは**旗から選んだ「曲全体のこの区間」**で、コードもオーディオクリップも
        まとめて動かします（8.124）。こちらは**選んだMIDIトラックのノートだけ**。
        **同じ「複数トラック」でも、動くものが違います**——
        片方に寄せると、旗の範囲がノートしか動かさなくなります。 */
    std::vector<juce::String> timeRangeTrackIds;

    /** 範囲がこの行にかかっているか（`timeRangeAllTracks`は見ません）。 */
    bool isTimeRangeOnTrackIndex (int trackIndex) const;

    /** 8.159：この行が「いま範囲の対象か」（Phase 197）。旗の区間なら全部、
        枠やCtrlで選んだのなら選んだトラックだけ。**コピー・カット・削除はここを見ます。** */
    bool isTrackInTimeRange (int trackIndex) const;

    /** 範囲のかかっている行番号（並び順）。消えたトラックは飛ばします。 */
    std::vector<int> getTimeRangeTrackIndices() const;

    /** 代表の行（先頭）。無ければ-1。**インスペクタと貼り付け先はこれを見ます。** */
    int getPrimaryTimeRangeTrackIndex() const;

    /** 範囲を1トラックだけに設定する。 */
    void setTimeRangeToTrack (int trackIndex);

    /** 8.158：範囲へトラックを足す／外す（Ctrl＋クリック）。

        **最後の1本は外しません**——空の範囲は「選んでいない」と見分けが付かないため。 */
    void toggleTimeRangeTrack (int trackIndex);

    /** 8.158：枠を引き始めた行（Phase 196）。**縦にどこまで囲ったか**を測る起点です。 */
    int timeRangeCreateAnchorTrackIndex = -1;

    /** 8.162：**最後に通った行**（Phase 200／本人の報告）。

        トラックの無い空きへカーソルが出たときに使います。
        **-1（行なし）で畳まないため**——囲ったぶんが1行へ戻ってしまうので。 */
    int timeRangeCreateLastRowIndex = -1;

    /** 8.158：いま囲っている行までのMIDIトラックを、範囲へ入れ直す（Phase 196）。 */
    void updateTimeRangeTracksForDrag (int currentRowIndex);

    /** 8.124：**全トラックにまたがる範囲**（Phase 159／改善案5）。

        マーカーの旗をクリックして選んだときだけtrue。
        このときは`timeRangeTrackIndex`を見ません——**どの行も範囲の中**です。

        **縦には動かせません**（本人の指定）。トラックをまたぐ移動を許すと、
        「どの行がどの行へ行ったのか」を全トラックぶん追うことになります。 */
    bool timeRangeAllTracks = false;

    double timeRangeStart = 0.0;
    double timeRangeEnd = 0.0;

    bool creatingTimeRange = false;
    double timeRangeAnchorTime = 0.0;

    bool draggingTimeRange = false;
    bool timeRangeDragIsCopy = false;
    double timeRangeDragOriginalStart = 0.0;
    double timeRangeDragPreviewStart = 0.0;

    /** 8.95：**ドラッグの行き先のトラック**（Phase 135）。縦に動かすと別のMIDIトラックへ移る。

        Phase 134まであった`timeRangeIsBlock`（隣の塊で止める）は廃止しました——
        **複製のときだけ止めない**のは読みにくく、止めないほうが分かりやすいためです。 */
    int timeRangeDragTargetTrack = -1;

    /** 範囲を掴んだときの構え（Ctrlで複製）。 */
    void startTimeRangeDrag (const juce::MouseEvent& e);


    //==========================================================================
    // 仕様書5.2.3：コードトラックのコード区間（Phase 42）
    //
    // クリップとは別扱いにしている。コード区間はクリップではなく（音も波形も持たず、
    // トラック間を移動することもない）、選択・ドラッグの状態を共有させると
    // `selectedIsMidi`のような二択の判定がもう1段増えて読めなくなるため。

    /** コード区間の矩形。 */
    juce::Rectangle<int> getChordRegionBounds (int trackIndex, int regionIndex) const;

    /** クリック位置にあるコード区間を探す。 */
    bool hitTestChordRegion (juce::Point<int> position, int& trackIndexOut, int& regionIndexOut) const;

    /** 8.128：**旗（コード名の札）だけ**の矩形（Phase 164／改善案13）。
        空なら「区間からはみ出すので札を描かない」という意味。 */
    juce::Rectangle<int> getChordFlagBounds (int trackIndex, int regionIndex) const;

    /** 8.128：座標が**旗の上**か（Phase 164／改善案13）。

        区間の帯は隙間なく並ぶので、**「区間の上か」ではもう振り分けられません**
        （曲じゅうがどれかの区間の中）。ダブルクリックの行き先は旗かどうかで決めます。 */
    bool hitTestChordFlag (juce::Point<int> position, int& trackIndexOut, int& regionIndexOut) const;


    /** コード名を書いたブロックとして描く。 */
    void drawChordRegion (juce::Graphics& g, juce::Rectangle<int> bounds, ChordRegion region);

    /** ダブルクリックした位置に、1小節ぶんのコード区間を足す。

        入れるコードはキーのトニック（Cメジャーなら CM7）。
        コードそのものを選ぶのはコードパッド（設計書2.3.5）の仕事。 */
    void addChordRegionAt (int trackIndex, int x);

    /** コード区間のドラッグ（Phase 45）。

        クリップのドラッグ（`dragMode`）とは**別に持っている**。コード区間は
        クリップではなく、トラックをまたいで動かすこともフェードもトリムの
        オフセットも無い。同じ状態に相乗りさせると、`selectedIsMidi`のような
        「どちらの話か」の判定がもう1段増える。 */
    enum class ChordDragMode { none, move, trimLeft, trimRight };

    /** ドラッグ中の見た目の位置を、拍にスナップして計算する。 */
    void updateChordDrag (const juce::MouseEvent& e);

    /** ドラッグの結果をモデルへ書く（離したとき）。 */
    void commitChordDrag();

    /** その時刻の1拍の長さ（秒）。**中身はProjectModelへ**（8.98／Phase 138）。 */
    double getBeatSeconds (double atTime) const;

    //==========================================================================
    // 仕様書5.9：ループ範囲（Phase 48）

    /** ルーラーの上端にある、ループ範囲を掴むための帯。 */
    juce::Rectangle<int> getLoopStripArea() const;

    /** ループ範囲を描く（帯と、タイムライン側の薄い塗り）。 */
    void drawLoopRange (juce::Graphics& g);

    /** ループ範囲のドラッグ。クリップと同じく、離すまでモデルへ書かない。 */
    enum class LoopDragMode { none, create, moveStart, moveEnd };

    LoopDragMode loopDragMode = LoopDragMode::none;
    double loopDragAnchorTime = 0.0;   // createのとき、掴んだ側の反対の端
    double loopDragPreviewStart = 0.0;
    double loopDragPreviewEnd = 0.0;

    void updateLoopDrag (const juce::MouseEvent& e);
    void commitLoopDrag();

    //==========================================================================
    // 仕様書5.9：マーカー（Phase 49）

    /** ルーラーのうち、マーカーの旗を出す帯（ループの帯の下・目盛りの上）。 */
    juce::Rectangle<int> getMarkerStripArea() const;

    /** マーカー1つぶんの旗の矩形。名前の長さで幅が変わる。 */
    juce::Rectangle<int> getMarkerFlagBounds (int markerIndex) const;

    /** クリック位置にあるマーカーを探す（無ければ-1）。 */
    int findMarkerAt (juce::Point<int> position) const;

    void drawMarkers (juce::Graphics& g);

    /** 右クリックのメニュー（名前の変更・削除）。 */
    void showMarkerMenu (int markerIndex, juce::Point<int> screenPosition);

    /** 名前を入力し直す小さなダイアログを出す。 */
    void renameMarker (Marker marker);

    //==========================================================================
    // 8.162：**旗の上でそのまま打ち直す**（Phase 200／本人の要望）。
    //
    // 本人の言葉は「マーカー名をダブルクリックでも名前変更できるようにしたい。
    // マーカー上で文字入力できる仕様がいい」。
    //
    // ダイアログ（`renameMarker()`）は右クリックのメニューから残します——
    // **どちらの入口も、書くのは`Marker::setName()`の1箇所**（1.27）。

    /** 旗の上に重ねる入力欄。**普段は隠しています。** */
    juce::TextEditor markerNameEditor;

    /** いま書き換えている旗。**IDで持つこと**——入力中に旗が増減すると
        番号は別のものを指します（1.32）。空なら書き換えていない。 */
    juce::String markerNameEditorId;

    /** 旗をダブルクリックしたときに呼ぶ。入力欄を旗の上へ出す。 */
    void showMarkerNameEditor (int markerIndex);

    /** 入力欄の中身をモデルへ書いて畳む。**空なら書かない**（名前の無い旗は掴めない）。 */
    void commitMarkerNameEdit();

    /** 書かずに畳む（Escape）。 */
    void cancelMarkerNameEdit();

    /** スクロール・ズームで旗が動いたら、入力欄も付いていく。
        **`updateScrollBars()`から呼びます**——横に動く経路はそこへ集まっているので。 */
    void updateMarkerNameEditorBounds();

    //==========================================================================
    // 仕様書5.1・5.9・5.11.1：小節バーのレーン（Phase 142・144／改善案㉒㉓㉔㉕）
    //
    // **レーンは2本です**（Phase 144。改善案リスト3の11・12）。
    //
    // ```
    // 小節番号・拍の線
    // テンポ            120        90
    // 拍子とキー   4/4 C Major   3/4 A Minor
    // ```
    //
    // **1本にまとめていたら、札同士が重なって読めませんでした**（改善案11）。
    // かといって3本に分けるとルーラーが厚くなりすぎるので、
    // **拍子とキーは同じ札にまとめています**（改善案12。どちらも小節にしか置けない）。
    //
    // 中身は`ProjectModel::getTempoMap()`と`getKeyMap()`が持っている表です——
    // **ここでValueTreeを辿らないこと**（8.103・8.106）。

    /** 変化点1つを指すもの。**番号ではなく位置で指す**（1.32）。

        表は並べ替えられるので、番号で覚えると**もう1つ足した瞬間に別のものを指します。**

        **`meterKey`は「その小節の設定」1つ**を指します（Phase 144）。
        中身は拍子とキーの2つの変化点ですが、**同じ小節にしか置けない**ので、
        画面では1つの札として扱うほうが数えやすい。
        「どちらか片方だけ変える」もできます——**札が出るのは、どちらかがあるとき**です。 */
    struct SignatureMarkerRef
    {
        enum class Kind { none, tempo, meterKey };

        Kind kind = Kind::none;
        double beatPosition = 0.0;   // テンポのとき（拍）
        int bar = 0;                 // 拍子・キーのとき（小節）

        bool isValid() const { return kind != Kind::none; }

        bool sameAs (const SignatureMarkerRef& other) const
        {
            if (kind != other.kind)
                return false;

            return kind == Kind::meterKey ? (bar == other.bar)
                                          : juce::approximatelyEqual (beatPosition, other.beatPosition);
        }
    };

    /** レーン1本ぶんの帯（小節番号の下）。 */
    juce::Rectangle<int> getTempoLaneArea() const;
    juce::Rectangle<int> getMeterKeyLaneArea() const;

    /** その種類の札が乗る帯。 */
    juce::Rectangle<int> getLaneAreaFor (SignatureMarkerRef::Kind kind) const;

    /** 2本まとめた帯（ルーラーの当たり判定用）。 */
    juce::Rectangle<int> getSignatureStripArea() const;

    /** 札1つぶんの矩形。文字の長さで幅が変わる（マーカーの旗と同じ考え方）。 */
    juce::Rectangle<int> getSignatureMarkerBounds (const SignatureMarkerRef& ref) const;

    /** その札に出す文字（テンポなら"120"、拍子とキーなら"4/4 C Major"）。 */
    juce::String getSignatureMarkerText (const SignatureMarkerRef& ref) const;

    /** その札が始まる時刻（ドラッグ中はプレビュー位置）。 */
    double getSignatureMarkerTime (const SignatureMarkerRef& ref) const;

    /** クリック位置にある札を探す（無ければkind=none）。 */
    SignatureMarkerRef findSignatureMarkerAt (juce::Point<int> position) const;

    void drawSignatureLanes (juce::Graphics& g);

    /** レーン1本ぶんを描く（地と札）。 */
    void drawOneSignatureLane (juce::Graphics& g, SignatureMarkerRef::Kind kind,
                                const juce::String& legend, juce::Colour colour);

    /** 右クリックのメニュー（値を変える／削除）。 */
    void showSignatureMarkerMenu (const SignatureMarkerRef& ref, juce::Point<int> screenPosition);

    /** 空いているところの右クリックメニュー（ここに追加）。 */
    void showSignatureLaneMenu (SignatureMarkerRef::Kind kind, double timeSeconds,
                                 juce::Point<int> screenPosition);

    /** テンポを打ち込むダイアログ。`bar`ではなく拍で置く。 */
    void editTempoChange (double beatPosition);

    /** 拍子を打ち込むダイアログ。 */
    void editTimeSignatureChange (int bar);

    /** キーを選ぶメニュー（12のルート × Major/Minor）。

        **打ち込ませないのは、綴りを間違えられるから**です（"Am"／"A minor"／"a"…）。
        選ばせれば、受け付けられない値そのものが無くなります。 */
    void showKeyChangeMenu (int bar, juce::Point<int> screenPosition);

    /** ドラッグ中の札（無効ならドラッグしていない）。 */
    SignatureMarkerRef signatureDragRef;
    double signatureDragPreviewTime = 0.0;

    void updateSignatureDrag (const juce::MouseEvent& e);
    void commitSignatureDrag();


    /** 8.29の表：オートメーション点の値を打ち込む（Phase 70）。

        **入力するのは「実際の値」**（VolumeならdB、Panなら-1〜+1）。
        正規化値との換算は`AutomationTargets`を通すこと（式をここに書かない）。 */
    void showAutomationValueEntry (AutomationRowRef ref, int pointIndex);

    /** クリップのコピー本体（`alsoDelete`ならカット）。 */
    bool copySelectedClips (bool alsoDelete);
    bool pasteClipsAt (double timeSeconds);

    /** オートメーション点1つぶんのコピー（`alsoDelete`ならカット）。右クリックメニューから。 */
    bool copyAutomationPoint (AutomationRowRef ref, int pointIndex, bool alsoDelete);
    bool pasteAutomationPointsAt (double timeSeconds);

    /** ルーラー（小節バー）の右クリックメニュー（Phase 50）。
        クリックした位置にマーカーを置く／ループの端をそこにする。 */
    void showRulerMenu (const juce::MouseEvent& e);

    /** 仕様書6.2：空いている場所の右クリックで出すツールの選択メニュー（Phase 68／8.29）。 */
    void showToolMenu (const juce::MouseEvent& e);

    /** 8.29の表：クリップの右クリックメニュー（Phase 71）。 */
    void showClipMenu (const juce::MouseEvent& e);

    /** 選択中のクリップをまとめて消す（Deleteキーとメニューの両方から）。 */
    void deleteSelectedClips();

    /** 8.40：選んでいるオーディオクリップのゲインを変える（Phase 80）。

        `resetToZero`なら0dBへ。**MIDIクリップは飛ばす**（ゲインを持たない）。
        アレンジ画面のクリップメニューの積み残し「ボリューム上げ下げ」がこれ（8.29）。 */
    void changeSelectedClipGain (float deltaDb, bool resetToZero);

    /** 8.46：選んでいるオーディオクリップに同じことをする（Phase 86）。
        **MIDIクリップは飛ばす。** 反転・オートフェードの入口はここ1本。 */
    bool applyToSelectedAudioClips (const juce::String& actionName,
                                     std::function<void (AudioClip&)> action);

    /** 8.228：選んでいるオーディオクリップが**全部モノラル化されているか**（Phase 249）。

        メニューは「する／戻す」の1項目なので、**混ざった選択をどちらへ揃えるか**を
        これで決めます。1つずつ反転させると、押すたびに入れ替わって揃いません。 */
    bool allSelectedAudioClipsAreMono() const;

    /** 8.78：ClipRefが指しているクリップのValueTree（Phase 118）。無ければ無効なものを返す。 */
    juce::ValueTree findClipStateForRef (const ClipRef& ref) const;

    /** 8.78：選んでいるクリップと**同じグループのもの**を選択に足す（Phase 118）。

        **選び方を変えるところ全部から呼ぶこと**（`setSingleClipSelection()`と
        `toggleClipSelection()`）。片方だけだと、Shift+クリックのときだけ
        グループが効かない、という食い違いになります。 */
    void expandSelectionToGroups();

    /** 8.78：グループに入っているクリップの左上に小さな印を描く（Phase 118）。 */
    void drawClipGroupMarker (juce::Graphics& g, juce::Rectangle<int> bounds,
                               const juce::ValueTree& clipState) const;

    /** 8.148／8.149：上下させた・伸縮したクリップの左上に、その値を描く
        （Phase 186・187）。どちらも動いていないとき・細すぎるクリップでは描きません。 */
    void drawClipTransposeMarker (juce::Graphics& g, juce::Rectangle<int> bounds,
                                   int semitones, double stretch) const;

    //=========================================================================
    // 8.150：**ワープマーカー**（Phase 188／8.48。仕様書5.5.1）

    /** クリップの上端の帯に、マーカーの取っ手と縦線を描く。 */
    void drawWarpMarkers (juce::Graphics& g, juce::Rectangle<int> bounds,
                           const AudioClip& clip) const;

    /** その位置に取っ手があるか。**あればそのマーカーのソース時刻**、無ければ`nullopt`。

        **端から`edgeGrabMargin`のうちには出しません**——
        トリムと伸縮の掴みしろを取り上げないためです。 */
    std::optional<double> findWarpMarkerHandleAt (juce::Rectangle<int> bounds,
                                                   const AudioClip& clip,
                                                   juce::Point<int> position) const;

    /** 8.150：選んでいるクリップにマーカーを置く（Phase 188）。

        `timelineSeconds`はタイムライン上の位置で、**そこにある音を、そこに留める**
        マーカーになります（置いた瞬間は何も動きません）。 */
    bool addWarpMarkerAt (double timelineSeconds);

    /** 8.150：ヒットポイントからマーカーを作る（Phase 188）。

        **ヒットポイントはPhase 68で入口を外したまま、モデルだけ残してありました**
        （8.29の備考「ワープ／タイムストレッチで使う予定」）。ここがその予定です。 */
    bool createWarpMarkersFromHitPoints();

    /** 8.150：選んでいるクリップのマーカーを全部消す（Phase 188）。 */
    bool clearWarpMarkers();

    /** 8.150：ドラッグ中の絵に使う対応表（Phase 188）。
        トリムや伸縮で動く**両端だけ**をプレビューの値に差し替えたものです。 */
    WarpMap makeDragPreviewWarpMap (const AudioClip& clip) const;

    /** ドラッグ中のマーカー。-1は掴んでいない。 */
    int markerDragIndex = -1;
    double markerDragPreviewTime = 0.0;

    /** 設計書2.3.1：ヘッダーの名前の下（Phase 58。8.61で2段になった）。

        **標準の高さの行にしか無い。** 低い行（コードトラック）では、
        幅も高さも0の矩形を返すので、`contains()`はどこでもfalseになる。

        8.61：この帯を**そのまま`TrackHeaderControls`へ渡す**（Phase 99）。
        中は上下2段で、上段が`getHeaderButtonRow()`（● A S M ＋パン）、
        下段が音量フェーダーとdB表示。 */
    juce::Rectangle<int> getHeaderControlRow (int rowIndex) const;

    /** 8.61：ボタン（● A S M）が並ぶ上段（Phase 99）。

        `getHeaderControlRow()`の上`TrackHeaderControls::buttonRowHeight`ぶん。
        **ボタンの位置はすべてここから求める**（下段は音量フェーダーが使う）。 */
    juce::Rectangle<int> getHeaderButtonRow (int rowIndex) const;

    /** 8.65：`TrackHeaderControls`を置く矩形（Phase 103）。

        **`getHeaderControlRow()`より上へ広い**：名前の行も含めた、
        **トラックの領域まるごと**です。メーターを**行の高さいっぱい**に
        伸ばすため（上端が行の上端と揃う）。

        名前・「i」・INは親が描きますが、部品側は
        `setInterceptsMouseClicks(false, true)`なので透けて見え、クリックも通ります。 */
    juce::Rectangle<int> getHeaderControlsBounds (int rowIndex) const;

    /** 8.65：右端でメーターが使う幅（余白込み）。

        **「i」はこのぶんだけ左へ寄せる**こと（`getInspectorButtonBounds()`）。
        重ねると、押したつもりでピークがリセットされます。 */
    static constexpr int headerMeterColumnWidth = TrackHeaderControls::meterWidth + 4;

    /** 仕様書5.4：トラックヘッダー内の録音待機ボタン（●）の位置。 */
    juce::Rectangle<int> getArmButtonBounds (int trackIndex) const;

    /** 仕様書5.6：トラックヘッダー内のオートメーションボタン（A）の位置（Phase 26）。 */
    juce::Rectangle<int> getAutomationButtonBounds (int trackIndex) const;

    /** 仕様書5.4：トラックヘッダー内の入力モニターボタンの位置（Phase 26）。
        録音待機中のオーディオトラックにだけ出す。

        **Phase 58で上段（名前の行）の右端へ移した。** 下段は録音待機の有無で
        出たり消えたりしない並びにしたかったため（消えると隣がずれて押し間違える）。 */
    juce::Rectangle<int> getMonitorButtonBounds (int trackIndex) const;

    /** 8.60：トラックヘッダー内の「i」（インスペクタを開く）の位置（Phase 97／改善案①）。

        **上段（名前の行）の右端**に置いてある。下段はRec／A／S／M／Pan／メーターで
        埋まっていて、割り込ませると全部の位置を決め直すことになるため。
        **常に出る**ので、INボタン（録音待機中だけ）はこの左隣へずらしてある。 */
    juce::Rectangle<int> getInspectorButtonBounds (int rowIndex) const;

    static constexpr int inspectorButtonWidth = 15;
    static constexpr int inspectorButtonHeight = 14;

    /** 仕様書5.2.1：トラックヘッダー内のソロ／ミュートボタン（Phase 58／8.1のC16）。

        **Consoleとインスペクタにも同じものがある。** 値はモデルにあるので
        どこで押しても揃うが、**判定を書き写さないこと**（8.13のA4と同じ話）。 */
    juce::Rectangle<int> getSoloButtonBounds (int trackIndex) const;
    juce::Rectangle<int> getMuteButtonBounds (int trackIndex) const;

    /** その種別のトラックがヘッダーにソロ／ミュートを出すか。
        コードトラックとフォルダは音を通さないので出さない。 */
    static bool trackTypeHasSoloMute (TrackType type);

    /** 8.61：そのトラックの色（Phase 99／改善案⑫。設計書2.4）。

        **ヘッダーの地・クリップ・MIDIノートは、すべてこれを通すこと。**
        VCAだけは固定でオレンジ（音声を持たない制御専用であることを、
        並んでいても取り違えないため）。 */
    juce::Colour getTrackColour (int rowIndex) const;

    /** 8.62：ヘッダーの下端を掴める帯（Phase 100）。低い行では空の矩形。

        **トラックの領域の下端**（`getTrackAreaHeight()`）を基準にすること。
        `getRowHeight()`だとオートメーションの行の下端になり、
        **レーンのヘッダーと掴み合い**になる（8.56）。 */
    juce::Rectangle<int> getTrackResizeGrabBounds (int rowIndex) const;

    /** 高さを変えているトラックの番号。掴んでいなければ-1。 */
    int resizeTrackIndex = -1;

    /** 掴んだ時点のYと高さ。**元の高さ＋動いたぶん**で決める
        （いまの高さに足すと、ドラッグのたびに二重に動く。8.38と同じ話）。 */
    int resizeTrackStartY = 0;
    int resizeTrackStartHeight = 0;

    /** 8.61：トラック名が出ている矩形（Phase 99／改善案⑤）。

        **描くときと、その場で編集するときの両方がここを通す。**
        別々に計算すると、入力欄が名前とずれた場所に出る。
        畳む三角・字下げ・「i」「IN」のぶんは、ここで吸収してある。 */
    juce::Rectangle<int> getTrackNameBounds (int rowIndex) const;

    //==========================================================================
    // 8.61：トラック名をその場で編集する（Phase 99／改善案⑤）
    //
    // Phase 98までは`NameEntry`（別ウィンドウ）でした。**名前を1つ直すのに
    // ダイアログが開いて閉じる**のは重く、続けて何本も直すときに特に煩わしいものでした。
    //
    // **`NameEntry`をやめたわけではありません**（マーカー名などでは引き続き使う）。
    // 8.53と同じ考え方で、「押したところが編集になる」ようにしてあります。

    /** 名前の上に入力欄を重ねて開く。 */
    void showTrackNameEditor (int rowIndex);

    /** 入力を確定する（空欄は無視）。 */
    void commitTrackNameEditor();

    /** 入力欄を片付ける。**コールバックの中から直接deleteしない**ため、
        メッセージスレッドへ回してから捨てる（`ValueEntrySlider`と同じ形）。 */
    void dismissTrackNameEditor();

    std::unique_ptr<juce::TextEditor> trackNameEditor;

    /** 8.52：**打ち始めた相手へ書く**（Phase 91と同じ話）。
        開いているあいだに並べ替えが起きても、別のトラックの名前を書き換えない。 */
    juce::String trackNameEditorTrackId;

    /** トラックヘッダーの中身（名前・i・IN・Rec・A・S・M）を描く（Phase 58）。

        **`headerBounds`は色帯を取り除いた残り。** 8.61：**音量・パン・メーターは
        ここでは描かない**（`TrackHeaderControls`が子コンポーネントとして持っている）。 */

    //==========================================================================
    // 8.44：行を畳む（Phase 84／C12）

    /** 畳む／開くの三角の位置（コードトラックだけ。無い行では空の矩形）。 */
    juce::Rectangle<int> getCollapseTriangleBounds (int rowIndex) const;

    /** 8.50：この行の字下げ幅（Phase 89／D2）。フォルダの階層ぶん右へ寄せる。 */
    int getHeaderIndent (int rowIndex) const;

    /** その三角を描く。**文字ではなく形**で描くこと（1.30）。 */
    void drawCollapseTriangle (juce::Graphics& g, juce::Rectangle<int> bounds, bool isCollapsed) const;
    /** 8.65：**位置は行番号から求めます**（Phase 103）。矩形は渡しません——
        メーターを行の上端まで伸ばした際に、渡された矩形と実際の位置がずれたためです。 */
    void drawTrackHeaderContents (juce::Graphics& g, int rowIndex, const Track& track);

    /** 下段の小さな四角いボタン（A・S・M）を1つ描く。塗り分けは呼び出し側が決める。 */
    void drawHeaderChip (juce::Graphics& g, juce::Rectangle<int> bounds,
                          const juce::String& text, bool isOn, juce::Colour onColour);

    /** フェードハンドル（丸）の座標を求める。fadeInがtrueなら左上、falseなら右上のハンドル。 */
    juce::Point<int> getFadeHandlePosition (juce::Rectangle<int> bounds, double fadeInSeconds, double fadeOutSeconds, bool fadeIn) const;

    /** 設計書2.3.7：選択が変わったことをSelectionStateへ反映する（Phase 17）。
        インスペクタはこれを購読して表示を切り替える。 */
    void publishSelection();

    /** 選択中のトラックの**番号**を、`SelectionState`が持つIDから引き直す（Phase 33）。

        このクラスは選択を番号で覚えているが、トラックの削除や並べ替えで番号はずれる。
        IDのほうが正なので、構成が変わったらここで引き直す。
        見つからなければ（＝そのトラックが消えた）選択を解除する。 */
    void syncSelectedTrackIndexFromSelection();

    //==========================================================================
    /**
        プロジェクトの変更を購読して、**他の画面での編集に追従する**（Phase 22a）。

        Phase 16でエディタがアレンジ画面の下に並ぶようになったため、
        「ピアノロールでノートを打つ → クリップが伸びる」ことが同じ画面の中で起きる。
        購読していないと、タブを切り替えるまで古い長さのまま描かれてしまう。

        `ValueTree::Listener`はルートを1つ購読するだけで子孫の変更まで届くので、
        プロジェクトのルートだけを見ていればよい（ProjectModel.hの説明も参照）。 */
    void updateProjectSubscription();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;

    // 購読中のルート。プロジェクトを読み込むとルート自体が差し替わるため、
    // 「今どのツリーを見ているか」を覚えて付け替えの要否を判断する。
    juce::ValueTree subscribedProjectState;

    ProjectModel& project;
    WaveformCache& waveformCache;
    SelectionState& selection;

    /** トラックヘッダーの幅。**Phase 58で150から広げた**（8.18）。
        下段にRec／A／S／M／Pan／メーターを並べるため。

        8.125：**定数をやめて、境目を掴んで変えられるようにした**（Phase 161／改善案38）。

        8.127：**既定を285から200へ**（Phase 163／本人の指定）。
        1.5倍（285）は広すぎたとのこと。**掴んで変えられる**ようになったので、
        既定は控えめにして、要る人が広げる形にしてあります。 */
    static constexpr int defaultTrackHeaderWidth = 200;

    /** **190より狭くしないこと。** Phase 58でこの幅にしたのは、
        下段にRec／A／S／M／Pan／メーターを並べるためです（8.18）。
        これより狭いと、その段が入りません。 */
    static constexpr int minTrackHeaderWidth = 190;
    static constexpr int maxTrackHeaderWidth = 520;

    /** ヘッダーとアレンジの境目の掴みしろ（px）。**左右どちらへも同じ幅**。 */
    static constexpr int headerResizeGrabMargin = 4;

    int trackHeaderWidth = defaultTrackHeaderWidth;

    /** 8.125：幅を変えている最中の控え（Phase 161／改善案38）。

        **画面座標で覚えること。** 幅を変えると中身の配りなおしが起きるので、
        部品の座標だと掴んでいる場所がずれ得ます（レーンの高さと同じ話。8.122）。 */
    bool headerResizing = false;
    int headerResizeStartScreenX = 0;
    int headerResizeStartWidth = 0;

    /** その座標がヘッダーとアレンジの境目（掴みしろの中）かどうか。 */
    bool isOnHeaderResizeEdge (juce::Point<int> position) const;

    /** 8.125：左端の色帯の矩形（Phase 161／改善案26）。
        **描いているのと同じ形**にすること——押せる場所と見えている場所がずれます。 */
    juce::Rectangle<int> getHeaderColourBandBounds (int rowIndex) const;

    //==========================================================================
    // 8.126：トラックヘッダーの複数選択（Phase 162／改善案35）

    /** **まとめて選んだトラック**。Ctrl＋クリックで出し入れする。

        **IDで持つこと**（番号は増減でずれる。1.32）。
        1本だけ選んでいるときも、その1本がここに入っている——
        「1本のときだけ空」にすると、まとめて操作する側が2通りを見ることになる。

        `selectedTrackIndex`は**最後に押した1本**のまま残してある。
        インスペクタが見ているのはそちらで、複数選んでも
        「どれの設定を出すか」は1つに決まらないといけない。 */
    std::vector<juce::String> selectedTrackIds;

    /** 8.154：Shift＋クリックで範囲を選ぶときの**起点**（Phase 192／本人の要望）。

        **`selectedTrackIndex`と別に持ちます。** あちらは「インスペクタに出す1本」で、
        Shift＋クリックのたびに押した行へ動きます。起点まで一緒に動くと、
        **1本ずつしか伸ばせません**（Shift＋クリックのたびに範囲が作り直される）。

        **IDで持つこと**（番号は増減でずれる。1.32）。 */
    /** 8.202：掴むときに保った選択を、離すときに畳むための印（Phase 236）。
        **空でなければ「動かさなかったらこのIDだけにする」** */
    juce::String collapseSelectionOnMouseUpId;

    juce::String trackSelectionAnchorId;

    /** 8.154：起点から`trackIndex`までを、まとめて選ぶ（Phase 192）。 */
    void selectTrackRangeTo (int trackIndex);

    bool isTrackInSelection (const juce::String& trackId) const;

    /** IDから行番号を引く。見つからなければ-1。 */
    int getTrackIndexById (const juce::String& trackId) const;


    /** 1本だけを選び直す（Ctrlを押していないクリック）。 */
    void setSingleTrackSelection (const juce::String& trackId);

    /** 出し入れする（Ctrl＋クリック）。**最後の1本は外させない**——
        何も選んでいない状態は、ヘッダーのクリックからは作らない。 */
    void toggleTrackInSelection (const juce::String& trackId);

    /** 消えたトラックのIDを落とす。**構成が変わったら必ず通すこと**（1.32）。 */
    void pruneTrackSelection();

    /** 8.159：まとめて選んでいるトラックのID（Phase 197/本人の要望）。

        **右クリックのメニューがここを見ます**——削除もフォルダ入れも、
        選んだぶん全部に効かせるため。1本だけ選んでいるときは要素1つです。 */
public:
    const std::vector<juce::String>& getSelectedTrackIds() const { return selectedTrackIds; }
private:

    /** 8.126：選んでいるトラックの音量を、**同じだけ**上下させる（改善案35）。

        渡すのは差（dB）。**絶対値ではない**——フェーダーの位置は
        トラックごとに違うので、揃えてしまうとバランスが壊れる。 */
    void nudgeSelectedTrackVolumes (const juce::String& originTrackId, float deltaDb);

    /** 8.126：選んでいるトラックのミュート／ソロを揃える（改善案35）。 */
    void applyMuteToSelection (const juce::String& originTrackId, bool shouldBeMuted);
    void applySoloToSelection (const juce::String& originTrackId, bool shouldBeSoloed);


    /** 8.125：色帯の上にカラーパレットを出す（Phase 161／改善案26）。
        インスペクタと**同じ見本**（`ColourSwatchButton.h`）を使う。 */
    void showTrackColourPalette (int rowIndex);


    /** 標準のトラック行の高さ。**行の高さを直に書かないこと**（`getRowHeight()`を通す）。 */
    static constexpr int trackRowHeight = 60;

    /** 仕様書5.2.3：コードトラックの行の高さ（Phase 58／8.1のC15）。

        コード区間は名前が1つ出るだけで、クリップのように中身を描かない。
        標準の高さのままだと、**縦に並べたときに場所を食うわりに情報が無い**。 */
    static constexpr int chordRowHeight = trackRowHeight / 2;

    /** 8.44：畳んだ行の高さ（Phase 84／C12）。**開き直す三角が押せる高さ**にすること。 */
    static constexpr int collapsedRowHeight = 16;

    //==========================================================================
    // 8.62：行の高さを手で変える（Phase 100）
    //
    // **ヘッダーの下端をドラッグ**して伸縮する。値はトラックが持つ
    // （`Track::getCustomRowHeight()`）ので、プロジェクトに保存される。

    /** 手で決められる高さの範囲。

        **下限は「下段が入る高さ」**にすること。これより低くすると
        `getHeaderControlRow()`が空を返し、つまみが消えて戻せなくなる
        （下端も掴めなくなるので、**行が二度と広げられません**）。 */
    static constexpr int minimumTrackRowHeight = trackRowHeight;
    static constexpr int maximumTrackRowHeight = 400;

    /** 下端を掴める帯の太さ（px）。 */
    static constexpr int trackResizeGrabMargin = 4;

    /** 8.50：フォルダ1階層ぶんの字下げ幅（Phase 89／D2）。 */
    static constexpr int headerIndentPerLevel = 12;

    /** 畳む三角の大きさ（辺の長さ）。 */
    static constexpr int collapseTriangleSize = 9;

    //==========================================================================
    // 設計書2.3.1：トラックヘッダーの中身
    // （Phase 58で2段、8.61／Phase 99で3段になった）
    //
    //   1段目（headerNameRowHeight）… 色帯・トラック名・i・IN（録音待機中だけ）
    //   2段目（buttonRowHeight）    … Rec / A / S / M ＋ パンのノブと数値
    //   3段目                       … 音量フェーダーと dB 表示
    //   右端（2〜3段目）            … 縦のレベルメーター
    //
    // **2段目から下は標準の高さの行にしか出ない。** コードトラックのような低い行では
    // 入らないので、`getHeaderControlRow()`が空の矩形を返す。
    //
    // **2段目の左半分（Rec/A/S/M）だけがここの担当**で、
    // パン・音量・メーターは`TrackHeaderControls`が持っている（8.61）。

    /** 8.65：**値を持つのは`TrackHeaderControls`側**（Phase 103）。
        あちらも名前のぶんを自分で空ける必要があるので、
        ここに数字を書き写すと**片方だけずれます**（1.27）。 */
    static constexpr int headerNameRowHeight = TrackHeaderControls::nameRowHeight;
    static constexpr int headerButtonSize = 15;
    static constexpr int headerSoloMuteWidth = 17;

    /** 8.61：ヘッダーの地に敷くトラック色の濃さ（Phase 99／改善案⑫）。

        **薄くとどめること。** 濃く塗ると、上に重ねる選択のパープルが
        トラックの色によって見えたり見えなかったりします（設計書2.6）。 */
    static constexpr float headerTintAlpha = 0.12f;

    /** 8.61：クリップの地に使うトラック色の濃さ（選択中／通常）。 */
    static constexpr float clipFillAlpha = 0.22f;
    static constexpr float selectedClipFillAlpha = 0.40f;

    /** 8.159：**選んだものは地の色を変える**（Phase 197／本人の要望）。

        Phase 196までは「トラックの色のまま濃くして、パープルの太い枠で囲う」でした。
        本人の指定は**枠ではなく地の色**——選んだものが**離れて見ても分かる**ようになります
        （枠は、細いクリップや塊だとほとんど枠だけになって中身が見えない）。

        **`false`にすればPhase 196までの見た目へ戻ります**（43・44と同じ形の旗）。 */
    static constexpr bool useSelectedClipFill = true;

    /** 選んだときの地の色。**トラックの色とぶつからないもの**を選ぶこと——
        トラックの色をそのまま濃くする形だと、色によって見えたり見えなかったりします。 */
    static juce::Colour getSelectedClipColour();
    /** 設計書2.4：左端の色帯の幅。

        8.125：**5pxから8pxへ広げた**（Phase 161／改善案26）。
        帯そのものが**押せるもの**になった（左クリックでカラーパレット）ので、
        5pxでは狙って当てるのが難しい。ヘッダーが広くなったぶんで吸収できる。

        **押せる場所は描いてある場所と同じにすること**（`getHeaderColourBandBounds()`）。
        当たり判定だけ広げると、帯の隣を押したのにパレットが出ます。 */
    static constexpr int headerColourBandWidth = 8;

    static constexpr int headerControlLeftMargin = 4;

    /** 設計書2.3.1：トラック一覧の末尾に出す「+ 新しいトラック」の行の高さ（Phase 31）。
        トラック行より低くして、**トラックではない**ことが並びで分かるようにしている。 */
    static constexpr int addTrackRowHeight = 32;

    /** 仕様書5.9：ルーラーの高さ。トラック行はこのぶんだけ下から始まる（Phase 18）。 */
    /** 仕様書5.9：ルーラーの上端に置く、ループ範囲の帯の高さ（Phase 48）。 */
    static constexpr int loopStripHeight = 7;

    /** 仕様書5.9：マーカーの旗の高さ（Phase 49）。ループの帯の下、目盛りの上。 */
    static constexpr int markerStripHeight = 11;

    /** 目盛り（小節番号・拍の線）を描き始めるY座標。

        **ループの帯とマーカーの旗のぶんだけ下がる。** Phase 48・49で上に2段
        足したので、それまでのように0から描くと数字が旗に隠れる。 */
    static constexpr int rulerContentTop = loopStripHeight + markerStripHeight;

    /** 小節番号を出す行の下端（Phase 142）。**ここまでが「目盛り」**で、
        この下は拍子とテンポのレーン。目盛りの線をルーラーの下端まで引くと、
        レーンの札を貫いてしまうので、**線はここで止めること。** */
    static constexpr int rulerNumbersBottom = rulerContentTop + 18;

    /** 仕様書5.1・5.9・5.11.1：小節バーのレーン1本ぶんの高さ（Phase 142・144）。

        **小節番号の下に置いた**のは、変化点が「その小節から効く」ものだからです。
        ループやマーカーの上に足すと、**上2段の位置が全部ずれます**（8.12の
        「番号で覚えたものは、増減や並べ替えでずれる」と同じ形）。 */
    static constexpr int signatureLaneHeight = 13;

    /** レーンの本数（テンポ／拍子とキー。Phase 144／改善案11・12）。

        **増やすなら、ここと`getLaneAreaFor()`の両方**。片方だけ直すと、
        帯は広がるのに札が前の位置のまま出ます。 */
    static constexpr int numSignatureLanes = 2;

    /** ルーラー全体の高さ。上2段＋目盛り＋レーン2本。 */
    static constexpr int rulerHeight = rulerNumbersBottom + signatureLaneHeight * numSignatureLanes;

    /** 8.199：角のうち、`Bars`（時間表示の切り替え）が使う下端（Phase 234）。

        **この下を「+ Track」「+ Audio」に空けます**（`getCornerButtonRow()`）。
        角の高さは62pxあり、それまで`Bars`が全部使っていました。 */
    static constexpr int timeFormatBottom = 28;

    // 表示倍率（1秒あたりのピクセル数）。Phase 10で固定値から可変になった。
    static constexpr double defaultPixelsPerSecond = 100.0;
    static constexpr double minPixelsPerSecond = 2.0;    // 1画面に数十分入る、俯瞰用
    static constexpr double maxPixelsPerSecond = 4000.0; // 波形をサンプル近くまで拡大できる
    static constexpr double zoomStepFactor = 1.25;       // ボタン/ホイール1回ぶんの倍率
    static constexpr int scrollBarThickness = 12;
    static constexpr int edgeGrabMargin = 6;
    // Phase 58でヘッダーを2段にしたときに、`headerButtonSize`へ置き換えた（8.18）
    static constexpr double minClipLength = 0.05;
    static constexpr float fadeHandleRadius = 5.0f;
    static constexpr float fadeHandleHitRadius = 9.0f;

    /** 8.150：ワープマーカーの取っ手（Phase 188／8.48）。
        **クリップの上端の帯**に出します。高さと、掴める左右の幅。 */
    static constexpr int warpHandleHeight = 9;
    static constexpr int warpHandleGrabWidth = 5;
    // ヒットポイントのクリック許容範囲。**入口はPhase 68で外しました**（8.29）。
    // ワープ（仕様書5.5.1）で使うときに、また要ります
    static constexpr double hitPointClickTolerance = 0.06;

    //==========================================================================
    // 仕様書6.2：ツールと複数選択の内部状態（Phase 51）

    EditTool editTool = EditTool::arrow;

    /** Ctrlを押しながらのドラッグは「移動」ではなく「複製」（Phase 52）。
        掴んだ時点で決まり、離すときに複製を置く。 */
    bool dragIsCopy = false;

    /** 8.154：**選んでいないクリップをCtrlで掴んだ**（Phase 192／本人の要望）。

        Ctrlは「選択に足す」と「ドラッグで複製」の両方に使うので、掴んだ時点では
        どちらか決まりません（Phase 52）。**動かし始めたら1本だけの選択へ畳み**、
        動かさずに離したなら`mouseUp`が選択へ足します。 */
    bool ctrlClickPendingSelection = false;

    std::vector<ClipRef> selectedClips;

    /** そのクリップが複数選択に入っているか。 */
    bool isClipSelected (const juce::String& trackId, const juce::String& clipId, bool isMidi) const;

    /** 複数選択へ足す／外す（既に入っていれば外す）。 */
    void toggleClipSelection (int trackIndex, int clipIndex, bool isMidi);

    /** 複数選択を1つだけに置き換える。 */
    void setSingleClipSelection (int trackIndex, int clipIndex, bool isMidi);

    /** 番号から`ClipRef`を作る（見つからなければ空のID）。 */
    ClipRef makeClipRef (int trackIndex, int clipIndex, bool isMidi) const;

    /** 範囲選択（ラバーバンド）のドラッグ。 */
    bool rangeSelecting = false;
    juce::Point<int> rangeSelectAnchor;
    juce::Rectangle<int> rangeSelectBounds;

    /** 矩形に触れているクリップを選び直す。 */
    void applyRangeSelection();

    /** ペンツール：その位置にMIDIクリップを作る。 */

    /** カットツール：その位置でクリップを割る。 */
    void cutClipAt (int trackIndex, int clipIndex, bool isMidi, int x);

    int selectedTrackIndex = -1;
    int selectedClipIndex = -1;

    // 仕様書4.4：ドラッグ中に「どの行へ落ちるか」を示すためのハイライト（Phase 21）。
    // -1なら受け付けない位置にいる。
    int dragOverRowIndex = -1;

    /** 8.123：トラックの無い空白の上にプラグインを持ってきているか（Phase 158／改善案7）。
        落とし先の見せ方が行とは違う（**増える行**を示す）ので、別に持つ。 */
    bool dragOverEmptyArea = false;


    // 選択中のクリップがMIDIクリップか（Phase 15）。
    // オーディオとMIDIでインデックスの数え方が別（getClip / getMidiClip）なので、
    // どちらの数え方で引くべきかをこのフラグで区別する。
    bool selectedIsMidi = false;

    DragMode dragMode = DragMode::None;
    juce::Point<int> dragStartMousePosition;

    //==========================================================================
    // Phase 34／36：ドラッグでのトラック並べ替え。
    //
    // **ドラッグ中はモデルを触らない**（Phase 36で変更）。
    // 掴んだ行をその場で動かす方式（Phase 34）だと、行が指の下から逃げるうえ、
    // 「どこへ入るのか」が動いた後にしか分からなかった。
    // 現在は挿入位置に線を引いて予告し、実際の移動はドロップ時に1回だけ行う。

    /** 掴んでいるトラックの番号。ドラッグしていなければ-1。 */
    int reorderSourceIndex = -1;

    /** 挿入位置。**行番号ではなく「行と行のあいだ」の番号**（0＝先頭の上、
        getNumTracks()＝末尾の下）。線を引く位置でもある。 */
    int reorderTargetSlot = -1;

    /** マウスのY座標から挿入位置を求める。行の中央が境目になる。 */
    /** 8.200：並べ替えで動かすトラック（Phase 235／改善案5の9）。
        **掴んだ行が選択に入っていれば選択ぶん全部、入っていなければその1本。** */
    juce::StringArray getTrackIdsForReorder (int draggedIndex) const;

    int getReorderSlotForY (int y) const;

    /** 8.51：掴んだヘッダーの落とし先（行とフォルダ）を決める（Phase 90／D2）。
        **深さは横の位置で決まる**——左端まで運べばフォルダから出る。 */
    void updateReorderTarget (juce::Point<int> position);

    /** 落とし先のフォルダID（空なら一番上の階層）。ドラッグ中だけ意味がある。 */
    juce::String reorderTargetFolderId;

    //==========================================================================
    // 8.70：**掴んだヘッダーが指について来る**（Phase 109／改善案。8.67のConsoleに揃えた）
    //
    // Consoleの並べ替え（8.67）はJUCEのドラッグ&ドロップに乗っているので、
    // **掴んだストリップの半透明の写しが指について来ます**（`DragAndDropContainer`が
    // 勝手に作ってくれる）。アレンジ画面の並べ替えは自前のマウス処理なので、
    // 予告線だけで「何を掴んでいるか」は行の薄い色でしか示せていませんでした。
    //
    // **JUCEのD&Dへ載せ替えてはいません。** こちらは
    // **横の位置でフォルダの深さを決める**という別の仕事を持っていて（8.51）、
    // 載せ替えると`updateReorderTarget()`をまるごと作り直すことになります。
    // **写しを自分で描くほうがずっと小さい話**です。

    /** 掴んだ瞬間のヘッダーの見た目（`createComponentSnapshot()`で撮る）。
        ドラッグしていなければ`isValid()`がfalse。 */
    juce::Image reorderDragImage;

    /** 掴んだ場所が、ヘッダーの左上からどれだけ離れていたか。
        **これを引かないと、写しの左上が指の位置に来て飛んで見えます。** */
    juce::Point<int> reorderDragGrabOffset;

    /** いまの指の位置（`updateReorderTarget()`が毎回入れる）。 */
    juce::Point<int> reorderDragPosition;

    /** 掴んだ瞬間に写しを撮る。**線とハイライトは写り込みません**
        （`reorderTargetSlot`がまだ-1なので、予告の描画そのものが走らない）。 */
    void captureReorderDragImage (int rowIndex, juce::Point<int> mousePosition);

    /** いま写しを描く場所。**描く側と、描き直す範囲を決める側の両方がここを通す**
        （2つ書くと、写しの端に消し残りが出ます）。写しが無ければ空。 */
    juce::Rectangle<int> getReorderDragImageBounds() const;

    /** 挿入位置を、`moveTrack()`へ渡す「移動後の行番号」に直す。
        掴んだ行を抜いたぶん、下へ動かすときは1つ詰まる。 */
    int reorderSlotToTrackIndex (int slot) const;

    //==========================================================================
    // 仕様書5.7：ヘッダーの音量・パン・メーター（Phase 58／8.1のC16。8.61で作り直し）

    /** ヘッダーのつまみ。**トラック1本につき1つ**で、行の並びと同じ順に持つ。

        メーターだけを子コンポーネントにしているのは、**自分で自分の場所だけを
        描き直せる**ため。ヘッダーの他の部分と同じように`paint()`で描くと、
        レベルの更新のたびにタイムライン全体（トラック×クリップの走査）を
        描き直すことになり、30fpsで回すには重すぎる。

        8.61：**Phase 99で、音量・パン・メーターをまとめて`TrackHeaderControls`へ移した**
        （改善案⑨⑩）。パンを自前で描いていたのをやめ、**Consoleと同じ部品**にしてある。
        こうすると「同じパンなのに操作が違う」が起きない（1.27）。

        **行と1対1**で作る（種別で中身を出し分けるのは部品自身の仕事）。
        `getHeaderControlRow()`の矩形をそのまま渡し、
        **上段の左半分（● A S M）はこちらが描いたものが透けて見える**
        （部品側は`setInterceptsMouseClicks(false, true)`でクリックを通す）。 */
    juce::OwnedArray<TrackHeaderControls> headerControls;

    /** ヘッダーの部品の数と位置を、いまのトラック構成に合わせる。
        **`refreshHeaderMeters()`から毎回呼ぶ**ので、経路の拾い漏れがあっても直る。 */
    void layoutHeaderControls();
    double dragOriginalStartTime = 0.0;
    double dragOriginalLength = 0.0;
    double dragOriginalOffset = 0.0;
    double dragOriginalFadeIn = 0.0;
    double dragOriginalFadeOut = 0.0;

    /** 8.149：掴んだ時点の伸縮の倍率と、ドラッグ中の値（Phase 187/8.48）。 */
    double dragOriginalStretch = 1.0;
    double dragPreviewStretch = 1.0;

    /** 8.150：掴んでいるワープマーカー（Phase 188/8.48）。

        **ソースの時刻で覚えます**——掴んだ音は掴んだ音のままで、
        動かすのは「それがいつ鳴るか」だけだからです。 */
    double dragWarpMarkerSource = 0.0;
    double dragOriginalWarpClip = 0.0;
    double dragPreviewWarpClip = 0.0;
    // 仕様書5.2.3：コード区間のドラッグ（Phase 45）
    ChordDragMode chordDragMode = ChordDragMode::none;
    int chordDragTrackIndex = -1;
    int chordDragRegionIndex = -1;
    double chordDragOriginalStart = 0.0;
    double chordDragOriginalLength = 0.0;
    double chordDragPreviewStart = 0.0;
    double chordDragPreviewLength = 0.0;
    juce::Point<int> chordDragStartMousePosition;

    //==========================================================================
    // 8.29の表：コード区間の選択（Phase 70）

    /** 選んでいるコード区間。**ValueTreeで覚える**（番号だと他の区間の増減でずれる。1.32）。 */
    juce::ValueTree selectedChordRegion;

    void setSelectedChordRegion (const juce::ValueTree& regionState);

    /** 選んでいるコード区間を消す。消したらtrue（Deleteキーから呼ぶ）。 */
    bool deleteSelectedChordRegion();

    double dragPreviewStartTime = 0.0;
    double dragPreviewLength = 0.0;
    double dragPreviewOffset = 0.0;
    double dragPreviewFadeIn = 0.0;
    double dragPreviewFadeOut = 0.0;
    int dragPreviewTrackIndex = -1;

    double playheadSeconds = 0.0;

    // 表示状態（ズーム・スクロール）
    double pixelsPerSecond = defaultPixelsPerSecond;
    double scrollStartSeconds = 0.0; // 画面左端に表示している時刻
    int verticalScrollPixels = 0;

    // 仕様書5.9：小節/拍表示とタイムコード表示の切り替え。
    // ルーラー左端（トラックヘッダーの上）の角に置く。
    bool showBarsAndBeats = true;
    juce::TextButton timeFormatButton { "Bars" };

    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineComponent)
};
