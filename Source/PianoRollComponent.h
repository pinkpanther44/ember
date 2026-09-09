#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "EditTool.h"   // 仕様書6.2：ツール（Phase 52でピアノロールにも効かせた）
#include "AutomationCurveUI.h"   // 8.37：線の描き方とメニューは共用（Phase 77）

//==============================================================================
/**
    設計書2.3.3「ピアノロールエディタ」の実装（仕様書5.3）。

    Phase 6aでは、ノートの入力・削除・移動・長さ変更に対応する。
    以下は今後のフェーズで対応予定：
    - ベロシティ編集、クオンタイズ（仕様書5.3）
    - 構成音カラーリング（5.3.1、コードトラック連携が前提）
    - ドラムエディター（5.3.2）、CCレーン（5.3.3）、グルーヴクオンタイズ（5.3.4）
    - 実際にノートの音を鳴らす（インストゥルメントプラグインとの接続が前提）

    操作方法：
    - 空白部分をクリック  ：その位置・音程にノートを追加
    - ノートをドラッグ      ：移動（音程と時間の両方）
    - ノート右端をドラッグ  ：長さを変更
    - ノートを右クリック    ：削除
*/
class PianoRollComponent : public juce::Component,
                            private juce::ValueTree::Listener
{
public:
    PianoRollComponent (ProjectModel& projectToUse);
    ~PianoRollComponent() override;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& e) override;

    /** 8.122：レーンの上端でカーソルを変える（Phase 157／改善案36）。
        **掴めることが見た目で分かる**ようにするため。 */
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** 8.29の表どおりのダブルクリック（Phase 68）。

        ドラム行の見出し＝楽器名の変更、CCレーンの点＝削除。
        **ノートと空いているグリッドには割り当てない**（表の「—」）。 */
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void resized() override;

    /** Ctrl＋ホイールで拡大縮小、Shift＋ホイール（と横ホイール）で横スクロール。

        **縦は基底へ返すこと**（`Component::mouseWheelMove`）。ここで握ってしまうと、
        親のViewportまで届かず、鍵盤の上下がスクロールできなくなる。 */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool keyPressed (const juce::KeyPress& key) override;

    /** 8.91：**編集対象のMIDIトラック**を設定する（Phase 131で唯一の入口になった）。

        Phase 130まではクリップも別に渡していました（`setClip()`）。
        **MIDIクリップを廃止したので、渡すものはトラック1本だけ**です。

        購読もここで張り替える：**トラックを購読すれば、その下のノート・CCの
        変化は全部届く**（ValueTreeの通知は親へ上がる）。 */
    void setTrack (const Track& trackToEdit);

    /** 編集対象のトラックが決まっているか。**操作を受け付けるかの判定はこちら**。 */
    bool hasTrack() const { return editedTrack.state.getParent().isValid(); }

    /** 8.91：**ノートの塊**（アレンジ画面に出る四角と同じ計算）。

        切れ目の値（1小節）を決めるのはここ1箇所。アレンジ画面へも同じ値を渡すこと
        ——別々に持つと、同じ曲がアレンジとピアノロールで違う塊に見える（8.2）。 */
    std::vector<Track::NoteBlock> getNoteBlocks() const;

    //==========================================================================
    // 仕様書5.9：ズームと横スクロール（Phase 67／8.1のG3）
    //
    // **アレンジ画面（`TimelineComponent`）と同じ形にしてある。** 倍率と
    // スクロール量を自分で持ち、座標変換は`timelineTimeToX()`／`xToTimelineTime()`の
    // 2つだけを通す（Phase 126で名前と基準が変わった）。
    // ルーラーとコード帯（`PianoRollHeaderComponent`）も**この2つを呼ぶ**ので、
    // 目盛りとグリッドが1pxもずれない（別々に計算すると必ずずれる。8.23のC7）。

    /** 倍率を変える。`anchorX`の位置にある時刻が、変更後も同じ位置に来るようにする。 */
    void setZoom (double newPixelsPerSecond, int anchorX);

    /** 8.123：**カーソルの位置を軸に**1段ぶん拡大／縮小する（Phase 158／改善案20）。

        ルーラー（`PianoRollHeaderComponent`）の上でのホイールが使う。
        **刻み（`zoomStepFactor`）を外へ出さないための口**でもある——
        外で同じ数を書くと、ボタンとホイールで拡大量が食い違う（1.27）。 */
    void zoomIn (int anchorX);
    void zoomOut (int anchorX);


    void zoomIn();
    void zoomOut();

    /** クリップ全体が1画面に入る倍率にする。 */
    void zoomToFit();

    double getPixelsPerSecond() const { return pixelsPerSecond; }

    /** 画面の左端に来る**タイムライン上の時刻**を決める（Phase 126で基準が変わった）。 */
    void setScrollStartSeconds (double newStartSeconds);
    double getScrollStartSeconds() const { return scrollStartSeconds; }

    /** 画面に入っている秒数（鍵盤の幅を除いた部分）。 */
    double getVisibleSeconds() const;

    /** 端まで見渡せるようにしておきたい長さ（スクロールバーの範囲）。

        **Phase 126から「曲の先頭からの長さ」。** それまではクリップの中身の長さだった。 */
    double getTimelineLengthSeconds() const;

    /** 8.162：**横スクロールで行ける先**（Phase 200／本人の要望）。

        中身の長さ（`getTimelineLengthSeconds()`）とは別ものです。
        **いまの位置から1画面ぶん先までは、中身が無くても必ず行けます。**
        いまの位置から測るので、左へ戻ればここも縮みます
        （行った先を覚えると、戻ってきてもつまみが小さいままになる）。

        **スクロールの上限を決めるのはこちら。**
        `getTimelineLengthSeconds()`は「曲の中身がどこまであるか」で、
        ズームの「全体表示」など**中身を測りたいほう**が使います。 */
    double getScrollableLengthSeconds() const;

    /** 倍率・スクロール量・編集対象が変わったときに呼ばれる。

        **ルーラーとコード帯、横スクロールバーはビュー側にある**ので、
        描き直しと範囲の付け替えはそちらの仕事になる（`PianoRollView`）。 */
    std::function<void()> onViewChanged;

    //==========================================================================
    // 座標変換（ルーラーとコード帯が同じ式を通るように公開してある）
    //
    // **Phase 126で、X座標の基準を「クリップの中身の時刻」から「曲の時刻」へ移した**
    // （8.86）。クリップをまたいで1枚に見せるには、画面の横軸が
    // **曲全体で1本の時間軸**である必要がある。Phase 67で対にしておいた
    // `contentTimeToTimeline()`／`timelineTimeToContent()`は、
    // 「どのクリップの中身か」を引数で受ける`clipContentToTimeline()`／
    // `clipTimelineToContent()`へ置き換えた（クリップが1つとは限らなくなったため）。
    //
    // **クリップの中身の時刻を使うのは、ノートとCCを読み書きするときだけ。**
    // ここ以外で `時刻 * pixelsPerSecond` と書かないこと（8.28）。

    /** X座標 → タイムライン上の時刻（秒）。 */
    double xToTimelineTime (int x) const;

    /** タイムライン上の時刻 → X座標。 */
    int timelineTimeToX (double timelineSeconds) const;

    /** 左端の鍵盤（ドラムモードでは行の見出し）の幅。

        **ルーラーとコード帯は、この幅ぶんだけ左を空ける。** 空けないと、
        目盛りがグリッドより左へずれる。 */
    static constexpr int keyboardWidth = 50;

    /** 仕様書5.3：クオンタイズ。gridDivisionは1拍を何分割するか（4なら16分音符相当）。
        swingAmountは0.0〜1.0で、裏拍を後ろへずらす量。
        selectedOnly=trueなら選択中のノートのみ、falseなら全ノートに適用する。 */
    void quantiseNotes (int gridDivision, double swingAmount, bool selectedOnly);

    /** 仕様書5.3.4：グルーヴテンプレートを適用する（Phase 24）。

        strengthは0.0〜1.0で、テンプレートへどの程度寄せるか。
        既存のグリッドクオンタイズと同じく、**ノートの時刻を実際に書き換える**
        1回きりの編集で、再生のたびに効くものではない。 */
    void applyGroove (const GrooveTemplate& grooveTemplate, int gridDivision,
                      double strength, bool selectedOnly);

    /** 編集中のトラック。グルーヴの抽出元として呼び出し側が使う（Phase 131）。 */
    Track getTrack() const { return editedTrack; }

    //==========================================================================
    // 仕様書5.3.2：ドラムエディター（Phase 25）

    /** 表示モードを切り替える（設計書2.3.3）。

        ドラムモードでは、鍵盤の代わりに**ドラムマップの名前付きの行**でノートを扱う。
        マップが無効な場合はドラムモードにできない（行が決まらないため）。

        **ノートの描き方と操作はピアノロールと同じ**（矩形・移動・伸縮・ベロシティ）。
        ダイヤ型の専用表示にすると既存の操作をすべて作り直すことになり、
        取りこぼしが出やすいため、行の決め方だけを差し替える形にしている。 */
    void setDrumMode (bool shouldUseDrumEditor, const DrumMap& mapToUse);

    bool isDrumMode() const { return drumMode; }

    //==========================================================================
    // 仕様書5.3.1：構成音カラーリング（Phase 46）

    /** 鍵盤行の背景の塗り方（設計書2.3.3のトグル）。 */
    enum class NoteColouring
    {
        off,          // 塗らない
        chordTones,   // コード構成音モード：コードトラックの区間ごとに、構成音の行を度数の色で塗る
        scaleTones    // スケール音モード：キー全体を「キー音／スケール内／スケール外」で塗る
    };

    void setNoteColouring (NoteColouring newMode);
    NoteColouring getNoteColouring() const { return noteColouring; }

    //==========================================================================
    // 仕様書6.2：ツールとノートの複数選択（Phase 52）
    //
    // **アレンジ画面と同じツールを使う**（`EditTool.h`）。値を持っているのは
    // MainComponentで、切り替えると両方の画面へ同じ値が流れる。

    void setEditTool (EditTool newTool);
    EditTool getEditTool() const { return editTool; }

    //==========================================================================
    // 8.29の表：鍵盤の表示（Phase 70）

    /** 左端の鍵盤に出す文字（設計書2.3.3）。

        `noteName`＝Cの行にC4のような音名。
        `solfege`＝キーのスケール音にド・レ・ミ（**移動ド**。ルートがド）。 */
    enum class KeyboardLabels { noteName, solfege };

    void setKeyboardLabels (KeyboardLabels newLabels);
    KeyboardLabels getKeyboardLabels() const { return keyboardLabels; }

    /** 表示の切り替えが起きたときに呼ばれる（アプリ設定へ覚えるのはビュー側の仕事）。 */
    std::function<void()> onKeyboardLabelsChanged;

    //==========================================================================
    // 8.122：下部レーンの高さ（Phase 157／改善案36）

    int getLaneHeight() const { return laneHeight; }

    /** 高さを決める。**上下限と、画面に入る範囲で丸めます**（値はそのまま入らない）。 */
    void setLaneHeight (int newHeight);

    /** 高さが変わったときに呼ばれる（設計書2.5：覚えておくのは呼び出し側の仕事）。 */
    std::function<void()> onLaneHeightChanged;


    /** ツールがピアノロールの中から選ばれたときに呼ばれる（Phase 69）。

        **自分では切り替えない。** ツールはアレンジ画面にも効くので、
        値を配るのは`MainComponent`の仕事（Phase 52で踏んだのと同じ話。8.11）。 */
    std::function<void (EditTool)> onEditToolSelected;

    //==========================================================================
    // 8.29の表：ノートの発音（Phase 71）
    //
    // **音源を持っているのはエンジン**なので、鳴らすのはビュー側の仕事。
    // ここは「いつ鳴らすか」だけを決めて、コールバックで投げる
    // （`PianoRollComponent`に`AudioEngine`を持ち込まない。8.11のツールと同じ形）。

    std::function<void (int pitch, int velocity)> onPreviewNoteOn;
    std::function<void (int pitch)> onPreviewNoteOff;

    //==========================================================================
    // 仕様書5.9：再生カーソル（Phase 72／8.29の表）

    /** 再生位置を渡す（**タイムライン上の時刻**）。

        **クリップの中身の時刻ではありません。** 中でX座標へ直すときに
        `timelineTimeToX()`を通します（8.28）。 */
    void setPlayheadSeconds (double timelineSeconds);

    /** ビューポートで見えている縦の範囲を教える（Phase 76／8.36）。

        **下部のレーンを固定表示にするために要ります。** ここが分からないと、
        レーンを「画面の下端」へ置けません（コンポーネントの下端では、
        縦にスクロールしたときに一緒に流れて消える）。 */
    void setVisibleVerticalRange (int scrollOffsetY, int visibleHeight);

    //==========================================================================
    // 8.1のD5／G4：他のMIDIトラックのノートを透かす（Phase 73）

    /** 編集していないMIDIトラックのノートを、薄く重ねて出すか。

        **画面の状態なのでプロジェクトには保存せず**、アプリ設定へ入れている
        （構成音カラーリングと同じ扱い。設計書2.5）。 */
    double getPlayheadSeconds() const { return playheadSeconds; }

    /** ルーラーや空いている場所のクリックで再生位置を動かしたいときに呼ばれる。

        **自分では動かしません**（エンジンを持っていない）。
        値を持っているのはMainComponentなので、そこへ返します（8.11と同じ形）。 */
    std::function<void (double timelineSeconds)> onSeek;

    /** すべてのノートを選ぶ／選択を外す（Ctrl+A／Ctrl+D）。

        **ピアノロールにフォーカスがある間は、こちらが先にキーを受け取る**ので、
        同じキーでもアレンジ画面のクリップではなくノートに効く（`keyPressed`）。 */
    void selectAllNotes();
    void clearNoteSelection();

    int getNumSelectedNotes() const { return (int) selectedNotes.size(); }

    //==========================================================================
    // 仕様書6.2：カット／コピー／貼り付け（Phase 71／8.29の表）
    //
    // **入れ物は`EditClipboard`ひとつ**（アレンジ画面のクリップと共用）。
    // 種別が合わないものは貼り付けません。

    /** 選んでいるノートを切り取る／コピーする。何か入れたらtrue。 */
    bool cutSelection();
    bool copySelection();

    /** **タイムライン上の時刻**へ貼り付ける。中身の種別に応じて振り分ける。

        **Phase 126で基準が変わりました**（それまではクリップの中身の時刻）。
        貼り付け先のクリップを決めるのは、この中の振り分け先の仕事。 */
    bool pasteAt (double timelineSeconds);

    /** 再生カーソル（タイムライン上の時刻）から貼り付ける。ショートカット用。 */
    bool pasteAtTimelineTime (double timelineSeconds) { return pasteAt (timelineSeconds); }

    bool hasSelectedNote() const { return hasActiveNote(); }

    /** ノートが変更された際に呼ばれる（呼び出し元でのステータス更新用）。 */
    std::function<void()> onModelChanged;

    //==========================================================================
    // 8.1のG5：下部のレーン（Phase 75）

    /** ノートの行＋下部レーン1本ぶんの高さ。

        Viewportの中身として使われるため、**自分で自分の大きさを決める**
        （ビュー側が固定値でsetSizeすると、行数が変わっても表示領域が合わない）。 */
    int getRequiredHeight() const;

    //==========================================================================
    // 8.1のG5／D4：下部のレーン（Phase 75／8.35）
    //
    // **レーンは1本だけで、中身を切り替えます。** Phase 74までは
    // 「ベロシティ（固定）＋CCレーンを縦に積む」形でしたが、狭いエディタパネルで
    // 2本も積むとノートグリッドが潰れ、しかも**オートメーション（ボリューム・パン）は
    // ピアノロールから触れませんでした**。
    //
    // 切り替えの入口はレーンの見出し（左端）と、レーンの右クリックです。
    // **ツールバーの「CC Lanes」ボタンは廃止しました**（レーンの持ち物なので、
    // 離れた場所にもう1つ入口を置くと、どちらが今の状態か分からなくなる）。

    /** 下部のレーンに何を出すか。 */
    struct LaneTarget
    {
        enum class Kind
        {
            velocity,     // ノートのベロシティ（仕様書5.3）
            automation,   // トラックのオートメーション（仕様書5.6。ボリューム／パン）
            cc            // MIDIのCC（仕様書5.3.3。ピッチベンドを含む）
        };

        Kind kind = Kind::velocity;

        /** `automation`のときの対象（`AutomationTargets`の識別子）。 */
        juce::String automationTargetId;

        /** `cc`のときのコントローラー番号（`MidiControllers`）。 */
        int controllerNumber = MidiControllers::pitchBend;

        bool operator== (const LaneTarget& other) const
        {
            return kind == other.kind
                    && automationTargetId == other.automationTargetId
                    && controllerNumber == other.controllerNumber;
        }
    };

    void setLaneTarget (LaneTarget newTarget);
    LaneTarget getLaneTarget() const { return laneTarget; }

    /** レーンの中身が切り替わったときに呼ばれる（アプリ設定へ覚えるのはビュー側の仕事）。 */
    std::function<void()> onLaneTargetChanged;

    /** レーンの見出しに出す名前（"Velocity"／"Volume"／"CC11 Expression" など）。 */
    juce::String getLaneTargetName() const;

private:
    // Phase 69で`ResizeLeft`（左端の伸縮）と`VelocityPaint`（なぞり書き）を足した（8.29）
    // Phase 75で`AutomationPoint`（下部レーンのオートメーション）を足した（8.35）
    // Phase 77で`CurveHandle`（曲がり具合のつまみ）を足した（8.37）
    // Phase 78で`LanePaint`（レーンでのなぞり書き）を足した（8.38）
    enum class DragMode { None, Move, ResizeRight, ResizeLeft, Velocity, VelocityPaint,
                          CCPoint, AutomationPoint, CurveHandle, LanePaint };

    juce::Rectangle<int> getNoteBounds (const Note& note) const;

    /** 音高・開始時刻・長さから、ノート1つぶんの矩形を作る（Phase 53）。

        `getNoteBounds()`が**モデルの値でしか矩形を作れない**ため分けてある。
        ドラッグ中のプレビューは、まだモデルに書いていない値で描く必要がある。 */
    juce::Rectangle<int> getNoteBoundsFor (double timelineStartTime, int pitch, double length) const;

    /** ドラッグ中の行き先を、元のノートとは別に描く（Phase 53／8.1のA2）。

        **元のノートは消さない。** 掴んだ1つだけでなく、いっしょに動く
        選択中のノート（Phase 52）ぶんも同じ量だけずらして描くので、
        まとめて動かしているときも行き先が分かる。
        Ctrl＋ドラッグ（複製）のときは、そのまま「複製が落ちる位置」になる。 */
    void drawNoteDragPreview (juce::Graphics& g) const;

    /** 座標にあるノート（無ければ無効なValueTree）。**Phase 127から全クリップを見る**。 */
    juce::ValueTree findNoteAt (juce::Point<int> position) const;

    /** ベロシティレーン（下部）の領域を返す。 */
    juce::Rectangle<int> getVelocityLaneBounds() const;

    /** ベロシティレーン内での、指定ノートの縦棒の領域を返す。 */
    juce::Rectangle<int> getVelocityBarBounds (const Note& note) const;

    /** ベロシティレーン内の座標から、対応するノートのインデックスを求める。 */
    /** ベロシティレーンの座標にある棒のノート（無ければ無効なValueTree）。 */
    juce::ValueTree findVelocityBarAt (juce::Point<int> position) const;

    /** ノートグリッド部分（下部レーンを除いた領域）の高さ。 */
    int getGridHeight() const;

    //==========================================================================
    // 8.1のG5：下部のレーン（Phase 75／8.35）

    /** レーン1本ぶんの領域。**`getVelocityLaneBounds()`は同じものを返します**
        （ベロシティを出しているときの呼び名。既存の呼び出しをそのまま残してある）。 */
    juce::Rectangle<int> getLaneBounds() const;

    /** レーンの見出し（左端）の領域。ここをクリックすると中身の切り替えメニューが出る。 */
    juce::Rectangle<int> getLaneHeaderBounds() const;

    /** レーンの中身を選ぶメニュー（見出しのクリックと、レーンの右クリックから）。 */
    void showLaneTargetMenu (juce::Point<int> screenPosition);

    void drawLane (juce::Graphics& g);
    void drawVelocityLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds);
    void drawCCLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds);
    void drawAutomationLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds);


    //==========================================================================
    // 仕様書5.3.3：CC（Phase 23。Phase 75で「レーン1本の中身」になった）

    /** レーンに出しているコントローラーのイベントのうち、座標に近いものを返す
        （無ければ無効なCCEvent）。 */
    CCEvent findCCEventAt (juce::Point<int> position) const;

    int ccValueToY (const juce::Rectangle<int>& laneBounds, int controllerNumber, int value) const;
    int yToCCValue (const juce::Rectangle<int>& laneBounds, int controllerNumber, int y) const;

    //==========================================================================
    // 仕様書5.6：オートメーション（Phase 75でピアノロールからも触れるようにした）

    /** 編集中のクリップが載っているトラック。 */
    Track getEditedTrack() const;

    /** いまレーンに出しているオートメーションのレーン（無ければ無効なもの）。

        **`createIfMissing`は、実際に点を置くときだけtrueにすること。**
        見るだけで作ると、触っていないパラメータのレーンが増えていく。 */
    AutomationLane getLaneForAutomation (bool createIfMissing);

    /** 正規化値（0〜1）とY座標の変換。 */
    int automationValueToY (const juce::Rectangle<int>& laneBounds, float value) const;
    float yToAutomationValue (const juce::Rectangle<int>& laneBounds, int y) const;

    /** そのレーンの中で、座標に近い点の番号を返す（無ければ-1）。 */
    int findAutomationPointAt (juce::Point<int> position);

    /** 点の右クリックメニュー（数値入力・削除）。 */
    void showAutomationPointMenu (int pointIndex, juce::Point<int> screenPosition);

    void showAutomationValueEntry (int pointIndex);

    //==========================================================================
    // 8.37：曲がり具合（Phase 77）

    /** レーンの線を「区間」の並びとして取り出したもの。

        **オートメーションとCCで同じ形にして返します。** 曲がり具合のつまみは
        どちらでも同じように出るので、描画も当たり判定もこの1本の道を通す。 */
    struct LaneSegment
    {
        /** 区間の形を持っている点（**手前の点**。仕様書5.6）。
            オートメーションならPOINT、CCならCCEVENTのValueTree。 */
        juce::ValueTree owner;

        float fromX = 0.0f, fromY = 0.0f, toX = 0.0f, toY = 0.0f;
        AutomationCurve curve = AutomationCurve::Linear;
        float amount = 0.0f;
    };

    std::vector<LaneSegment> getLaneSegments (const juce::Rectangle<int>& laneBounds);

    /** 座標に重なっている曲がり具合のつまみの区間番号（無ければ-1）。 */
    int findCurveHandleAt (juce::Point<int> position);

    /** 曲がり具合のつまみを描く（線と点の後に呼ぶこと）。 */
    void drawCurveHandles (juce::Graphics& g, const juce::Rectangle<int>& laneBounds,
                            juce::Colour colour);

    /** 区間の形を書き換える。**オートメーションの点とCCイベントの違いをここで吸収する**
        （呼ぶ側が「どっちだったか」を覚えなくて済むように）。 */
    void setSegmentCurve (juce::ValueTree owner, AutomationCurve curve, float amount);

    //==========================================================================
    // 8.38：レーンでのツール（Phase 78）。**ノートと同じ考え方**（8.29の表）

    //==========================================================================
    // 8.43：消しゴム（Phase 83／C11）

    /** 座標にあるノートを1つ消す。消したらtrue。
        **触れた1つだけ**（選択は見ない。狙って消せるように）。 */
    bool eraseNoteAt (juce::Point<int> position);

    /** 座標にあるレーンの点（オートメーション／CC）を1つ消す。消したらtrue。
        **ベロシティは消せない**（ノートの持ち物なので、消すという操作が無い）。 */
    bool eraseLanePointAt (juce::Point<int> position);

    /** レーンの点を選び直す／足す／外す。**中身はValueTreeで持つ**（1.32）。 */
    void clearLaneSelection();
    bool isLanePointSelected (const juce::ValueTree& pointState) const;

    /** 消えた点を選択から外す。**使う前に必ず通すこと**（Undoで消えていることがある）。 */
    void pruneLaneSelection();

    /** 矢印ツールのドラッグ（ラバーバンド）で囲んだ点を選び直す。 */
    void applyLaneRangeSelection();

    /** 選んでいる点をまとめて消す（Deleteキー）。 */
    void deleteSelectedLanePoints();

    /** 選んでいる点をクリップボードへ（`alsoDelete`でカット）。
        **1つだけ選んでいるときも同じ道を通る**（右クリックメニューからのカットも含む）。 */
    bool copyLaneSelection (bool alsoDelete);

    /** ドラッグで動かした点といっしょに、選んでいる他の点も同じだけ動かす。
        **掴んだ点をモデルへ書いた後に呼ぶこと**（3.1。先に呼ぶと履歴が分かれる）。 */
    void moveOtherSelectedLanePointsByDrag (double deltaTime, float deltaValue);

    /** ペンツールのなぞり書き（Phase 78）。通った場所に点を置く。 */
    void paintLanePointAt (juce::Point<int> position);

    /** なぞった範囲の既存の点を消す（オートメーション／CCの両方）。

        **消すのは「あいだ」だけ**（`previousTime`の点は直前に自分で置いたもの）。
        `newTime`にある点は、置き換えたいので消す。 */
    void removeLanePointsInTimeRange (double previousTime, double newTime);

    /** 点1つの時刻と値（0〜1に直したもの）。ドラッグとなぞり書きで共用。

        **時刻はどちらもタイムライン基準**（Phase 126）。CCはクリップの中身の時刻で
        持っているが、その違いはこの対が吸収する——呼ぶ側で直すと、描画・当たり判定・
        選択・なぞり書きのそれぞれに同じ判定を書くことになる（8.2）。 */
    double getLanePointTime (const juce::ValueTree& pointState) const;

    /** 8.139：`getLanePointTime()`の拍版（Phase 177）。**振り分けは同じ形**です。 */
    double getLanePointBeats (const juce::ValueTree& pointState) const;
    float getLanePointNormalisedValue (const juce::ValueTree& pointState) const;
    void setLanePointTimeAndValue (juce::ValueTree pointState, double timelineTime, float normalisedValue);

    /** レーンの高さが変わったときに自分の大きさを更新する。 */
    void updateSizeForLanes();

    /** Y座標から音程（MIDIノート番号）を求める。上に行くほど高音。
        ドラムモードではドラムマップの行に対応する（仕様書5.3.2）。 */
    int yToPitch (int y) const;
    int pitchToY (int pitch) const;

    //==========================================================================
    // 仕様書5.3.2：ドラムエディター（Phase 25）

    /** 1行の高さ。ドラムモードはパート名を出すぶん少し高い。 */
    int getRowHeight() const;

    /** ノートを並べる領域の高さ（ベロシティ／CCレーンを除いた、行の合計）。 */
    int getNoteAreaHeight() const;

    /** そのピッチがドラムマップの何行目か。マップに無ければ-1。 */
    int getDrumRowForPitch (int pitch) const;

    /** 8.161：ドラム行の番号と画面のYの行き来（Phase 199）。

        **下ほど低い音**（ピアノロールと同じ向き）。ドラムマップは音程順に
        並んでいるので、行0＝Acoustic Bass Drumが**いちばん下**に来る。

        **行↔Yを知っているのはこの2つだけにすること。** 割り算をその場で
        書くと、向きを変えたときに描画と当たり判定がずれる（1.27）。 */
    int getYForDrumRow (int row) const;

    /** 画面のYがドラムマップの何行目か。行が無ければ-1。 */
    int getDrumRowAtY (int y) const;

    //==========================================================================
    // 仕様書5.3.1：構成音カラーリング（Phase 46）

    /** 鍵盤行の背景を塗る。鍵盤と行を描いた直後、ノートより前に呼ぶこと。

        **ドラムモードでは呼ばない**（仕様書5.3.1・5.3.2。パーカッションの行に
        度数の色を塗っても意味がない）。 */
    void drawNoteColouring (juce::Graphics& g);

    /** 他トラックのノートの透かし（Phase 73）。**塗りは薄く**：
        濃くすると編集中のノートと見分けが付かない。 */
    static constexpr float watermarkAlpha = 0.22f;

    void drawNoteWatermark (juce::Graphics& g);

    /** 透かしに指定されたMIDIトラックが1本でもあるか（Phase 74）。
        **ノートの増減を拾うかどうかの判断に使う**（いつも拾うと重い）。 */
    bool hasAnyWatermarkTrack() const;

    /** コード構成音モードの塗り。コードトラックの区間ごとに、
        その区間のX座標範囲だけを塗る。 */
    void drawChordToneColouring (juce::Graphics& g);

    /** スケール音モードの塗り。キー全体なので、横方向は全幅。 */
    void drawScaleToneColouring (juce::Graphics& g);

    /** 8.106：**画面に出すキー**（Phase 143／改善案㉔㉕）。

        キーは曲の途中で変わります（`ProjectModel::getProjectKeyAt()`）。
        ピアノロールの**スケール表示と階名は、行ごとに1色・1文字**なので、
        「いつのキーか」を1つ選ばなければなりません。

        **選んでいるのは画面の左端の時刻**です（いま見ているところ）。
        **決めるのはここ1箇所**にすること——色と階名が別の時刻を見ていると、
        「ドと書いてある音に色が付かない」ことが起こります（8.2）。 */
    Scale getKeyForDisplay() const;

    /** コードトラックの変化を拾うためだけの購読役（Phase 46）。

        **編集中のクリップ用の購読とは別にしてある。** 同じクラスで両方を受けると、
        ノート1つが動くたびに「コードが変わったのか」の判定を通ることになり、
        どちらの通知なのかを毎回見分ける羽目になる。
        購読先も違う（クリップのサブツリー ⇔ プロジェクトのルート）。 */
    struct ChordWatcher : public juce::ValueTree::Listener
    {
        explicit ChordWatcher (PianoRollComponent& ownerToUse) : owner (ownerToUse) {}

        void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override
        {
            owner.chordDataChanged (tree, property);
        }

        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override
        {
            owner.chordChildChanged (child);
        }

        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override
        {
            owner.chordChildChanged (child);
        }

        PianoRollComponent& owner;
    };

    /** 8.88：ビュー側（横スクロールバー・ルーラー）へ「見える範囲が変わった」と伝える。

        **モデルが変わったときにも呼ぶこと**（Phase 128）。描き直すだけでは、
        スクロールできる範囲とルーラーが古いまま残る。 */
    void notifyViewChanged();

    /** プロジェクトのルートへの購読を張り直す（読み込みでルートが差し替わるため。3.1）。 */
    void updateChordSubscription();

    void chordDataChanged (const juce::ValueTree& tree, const juce::Identifier& property);
    void chordChildChanged (const juce::ValueTree& child);

    /** 8.29の表：左端の見出し（鍵盤／ドラム行）のクリック（Phase 69）。

        矢印ツールなら**その行の音のノートをまとめて選ぶ**（ドラッグで複数行）。
        ドラムモードの右クリックは行のメニュー（楽器名・ミュート・チョークグループ）。
        **ペンの「音が鳴る」はPhase 71**（発音の仕組みごと入れる回）。

        見出しの上での操作を引き受けたらtrueを返す。 */
    bool handleKeyboardClick (const juce::MouseEvent& e);

    /** 仕様書5.3.2：ドラム行の右クリックメニュー（Phase 69でミュートもここへ）。 */
    void showDrumRowMenu (int row, juce::Point<int> screenPosition);

    /** その行に出す文字（Phase 70）。出さない行では空文字を返す。 */
    juce::String getKeyboardLabel (int pitch) const;

    /** 鍵盤の右クリックメニュー（音名／階名の切り替え。Phase 70）。 */
    void showKeyboardMenu (juce::Point<int> screenPosition);

    /** 2つのY座標のあいだにある行の「音」を集める（Phase 69）。

        **行ごとに拾うこと。** ドラムモードでは行の並びが音程順ではないので、
        音程の範囲で取ると関係のない音が混ざる。 */
    void collectPitchesBetweenY (int y1, int y2, juce::SortedSet<int>& pitches) const;

    /** 集めた音のノートを、クリップ全体から選び直す（Phase 69）。 */
    void applyKeyboardSelection();

    /** 8.29の表：ペンツールでノートを1つ置く（Phase 69）。

        **置いたあとは右端を掴んだ状態にする**ので、そのままドラッグすれば
        長さが決まる。Undoの区切りはここで1回だけ作る。 */
    void addNoteAt (int x, int y);

    /** 8.29の表：ペンツールでベロシティをなぞり書きする（Phase 69）。

        **カーソルのX座標に棒がかかっているノートを全部**書き換える。
        **区切りは呼び出し元が作る**（なぞり1回ぶんで1つ）。 */
    void paintVelocityAt (juce::Point<int> position);

    /** 仕様書5.3.2：ドラム行のパート名を変える（Phase 68）。
        見出しのダブルクリックと右クリックメニューの両方から呼ぶ。 */
    void renameDrumPart (int row);

    /** 8.29の表：CCの点の右クリックメニュー（Phase 70）。

        **それまでは「その場で削除」でした。** ノート（Phase 52）と同じ理由で、
        触れただけで消えるのをやめています。 */
    void showCCEventMenu (CCEvent event, juce::Point<int> screenPosition);

    /** 8.29の表：CCの値を打ち込む（Phase 70）。
        **上限はコントローラーごとに違う**（ピッチベンドは16383）。 */
    void showCCValueEntry (CCEvent event);

    /** ノートのコピー本体（`alsoDelete`ならカット）。

        **貼り付けの時刻はタイムライン基準**（Phase 126）。どのクリップへ入れるかは
        貼り付け側が決める。 */
    bool copySelectedNotes (bool alsoDelete);
    bool pasteNotesAt (double timelineSeconds);

    /** CCの点1つぶんのコピー（`alsoDelete`ならカット）。右クリックメニューから。 */
    bool copyCCEvent (CCEvent event, bool alsoDelete);
    bool pasteCCEventsAt (double timelineSeconds);

    /** オートメーションの点1つぶんのコピー（`alsoDelete`ならカット）。右クリックメニューから。 */
    bool copyAutomationPoint (int pointIndex, bool alsoDelete);

    /** **タイムライン上の時刻**へ貼り付ける（Phase 126で3つとも基準が揃った）。 */
    bool pasteAutomationPointsAt (double timelineSeconds);

    /** ドラムマップに載っていないノートの数（伏せられていることを知らせるために数える）。 */
    int countNotesOutsideDrumMap() const;

    //==========================================================================
    // 8.91：MIDIはトラックが直接持つ（Phase 131）
    //
    // **Phase 126〜130でここに並んでいた道具は、まるごと消えました。**
    // クリップの窓・オフセット・持ち主の付け替え・伸ばし直し——どれも
    // 「1つのノートが3通りの時刻で表される」ことへの後始末です（1.14）。
    // **時刻が1つしか無ければ、どれも要りません。**
    //
    // 消したもの：`getEditableClips()` `getClipAtTimelineTime()`
    // `getOrCreateClipAtTimelineTime()` `clipContentToTimeline()`
    // `clipTimelineToContent()` `getClipForClipEvent()` `growClipForNote()`
    // `countNotesOutsideClipWindow()` `drawClipBoundaries()` `fillOutsideClips()`
    // `getClipXRanges()` `replaceInSelection()` `moveNoteToTimelineTime()`

    /** トラックの全ノートに順に当てる（描画・当たり判定・選択で共用）。

        **`editedTrack.getNumNotes()`で直に回さないこと。** ここを通しておけば、
        後から「出さないノート」の決まりが増えても1箇所で済みます。 */
    void forEachNote (const std::function<void (const juce::ValueTree&)>& fn) const;

    /** ノートの**曲の時刻**での開始。Phase 131からは`getStartTime()`そのもの。

        名前を残してあるのは、**呼び出し側が「これはタイムライン基準だ」と
        読めるほう**が安全だから（クリップがあった頃の名残ではありません）。 */
    static double getNoteTimelineStart (const juce::ValueTree& noteState);

    /** ノートの開始時刻を書く（負にはしない）。 */
    static void setNoteTimelineStart (const juce::ValueTree& noteState, double timelineSeconds,
                                       juce::UndoManager* undoManager);

    /** 画面に見えている範囲に、ノートが1つでもあるか（`setTrack()`の寄せ判断に使う）。 */
    bool hasNoteInVisibleRange() const;

    /** 8.61：**編集中のトラックの色**（Phase 99／改善案⑫）。

        ノートの塗りに使う。Phase 131からは`editedTrack`から直に引く。

        辿れないとき（差し替えの途中など）はパープル。 */
    juce::Colour getTrackColour() const;

    ProjectModel& project;

    /** 8.91：**編集中のMIDIトラック**（Phase 131）。ここが画面に出る単位で、
        ノートもCCもこの下に直接載っています。

        購読先でもある：**この1本を購読すれば、下のノート・CCの変化が全部届く**
        （ValueTreeの通知は親へ上がる）。 */
    Track editedTrack { juce::ValueTree() };

    /**
        編集中のトラックを購読して、**他の画面での変更に追従する**（Phase 22a）。

        Phase 16でエディタがアレンジ画面の下に並ぶようになったため、
        「アレンジ画面でノートを動かす → ピアノロールの表示が変わる」ことが
        同じ画面の中で起きる。購読していないと、タブを切り替えるまで古い表示のままになる。

        購読先は`setTrack()`のたびに付け替えること（外し忘れると、
        もう編集していないトラックの変更で描き直してしまう）。 */
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeParentChanged (juce::ValueTree&) override;

    static constexpr int noteRowHeight = 12;
    static constexpr int lowestPitch = 36;   // C2
    static constexpr int highestPitch = 96;  // C7
    static constexpr int resizeGrabMargin = 5;
    static constexpr double defaultNoteLength = 0.5;
    static constexpr double minNoteLength = 0.05;
    // 8.1のG5：下部のレーン1本ぶんの高さ（Phase 75）。
    // **ベロシティ80px＋CC 70px×N**だったものを1本にまとめた。
    //
    // 8.122：**定数をやめて、上端を掴んで変えられるようにした**（Phase 157／改善案36）。
    // 90pxは細かいカーブを描くには狭く、逆にノートを広く見たいときは邪魔だった。
    static constexpr int defaultLaneHeight = 90;
    static constexpr int minLaneHeight = 40;
    static constexpr int maxLaneHeight = 400;

    /** レーンの上端の掴みしろ（px）。**上下どちらへも同じ幅**。 */
    static constexpr int laneResizeGrabMargin = 4;

    int laneHeight = defaultLaneHeight;

    /** 8.122：高さを変えている最中の控え（Phase 157／改善案36）。

        **画面座標で覚えること。** 高さを変えると中身の高さも変わり、
        ビューがスクロール位置を詰め直すことがある。部品の座標で測っていると、
        そのぶんだけ掴んでいる場所がずれる。 */
    bool laneResizing = false;
    int laneResizeStartScreenY = 0;
    int laneResizeStartHeight = 0;

    /** その座標がレーンの上端（掴みしろの中）かどうか。 */
    bool isOnLaneResizeEdge (juce::Point<int> position) const;

    //==========================================================================
    // 仕様書5.9：ズーム（Phase 67／8.1のG3）
    //
    // **Phase 66までは`static constexpr double pixelsPerSecond = 100.0`だった。**
    // 固定倍率では「1小節を細かく詰める」ことも「曲の後半まで見渡す」こともできず、
    // ルーラー（G3）・下部レーン（G5）を載せる土台にもならない。
    // 値と上下限はアレンジ画面（`TimelineComponent`）と**同じにしてある**：
    // 2つの画面を行き来したときに、同じ操作で同じだけ拡大されるほうが迷わない。
    static constexpr double defaultPixelsPerSecond = 100.0;
    static constexpr double minPixelsPerSecond = 2.0;
    static constexpr double maxPixelsPerSecond = 4000.0;
    static constexpr double zoomStepFactor = 1.25;

    double pixelsPerSecond = defaultPixelsPerSecond;

    /** 画面の左端に来ている**タイムライン上の時刻**（秒。Phase 126で基準が変わった）。

        **横スクロールはViewportに任せていない**（Phase 67）。任せると鍵盤まで
        いっしょに左へ流れて消えてしまい、さらにルーラーとコード帯（ビュー側にある）が
        「いまどこまでスクロールされているか」を別の経路で知る必要が出てくる。
        アレンジ画面と同じく、**自分で持って`timelineTimeToX()`で引く**形に揃えてある。 */
    double scrollStartSeconds = 0.0;

    // 仕様書5.3.2：ドラムエディター（Phase 25）。
    // パート名を読める高さが要るので、鍵盤の行より少し高くしてある。
    static constexpr int drumRowHeight = 16;

    /** 8.60：**ドラムのノートは▶で描く**（Phase 97／D13。仕様書5.3.2）。

        打楽器は「鳴り始めるところ」だけが意味を持つので、長い矩形だと
        **どこが打点なのかが読み取りにくい**（音程のノートと同じ見た目でもある）。

        **当たり判定は矩形のまま**です（Phase 25の判断）。形まで変えると、
        移動・伸縮・ベロシティのドラッグを全部作り直すことになります。
        ここは`paint()`から呼ぶ描画専用で、`getNoteBounds()`は今までどおりです。

        長さのあるノートには、▶の後ろに細い尾を引きます
        （長さの情報を落とさないため）。 */
    void drawDrumNote (juce::Graphics& g, juce::Rectangle<int> bounds,
                        juce::Colour fillColour, juce::Colour outlineColour,
                        float outlineThickness) const;

    bool drumMode = false;
    DrumMap drumMap { juce::ValueTree() };

    //==========================================================================
    // 仕様書6.2：ツールとノートの複数選択（Phase 52）

    EditTool editTool = EditTool::arrow;

    /** 選択中のノート。**ノートはIDを持たないので、ValueTree自体で覚える。**
        ValueTreeは実体を共有する参照なので、他のノートが増減しても指す先は変わらない
        （番号で持つとずれる。1.32）。消えたものは`getParent()`が無効になる。 */
    std::vector<juce::ValueTree> selectedNotes;

    bool isNoteSelected (const juce::ValueTree& noteState) const;
    void toggleNoteSelection (const juce::ValueTree& noteState);
    void setSingleNoteSelection (const juce::ValueTree& noteState);

    /** 選択中のノートのうち、まだクリップに残っているものだけにする。 */
    void pruneNoteSelection();

    /** 範囲選択（ラバーバンド）。 */
    bool rangeSelecting = false;
    juce::Point<int> rangeSelectAnchor;
    juce::Rectangle<int> rangeSelectBounds;
    void applyRangeSelection();

    /** 8.29の表：鍵盤／ドラム行の見出しでの選択（Phase 69）。

        **Y座標で覚えておく。** ドラムモードでは行の並びが音程順ではないので、
        音程で範囲を持つと、なぞった行と選ばれる音が食い違う。 */
    bool keyboardSelecting = false;
    int keyboardSelectAnchorY = 0;
    int keyboardSelectCurrentY = 0;

    /** ペンで置いた直後のドラッグか（Phase 69）。

        **trueのあいだは、離すときにUndoの区切りを作らない。**
        「置いた」と「伸ばした」が別々の履歴になると、1回の操作を戻すのに
        Ctrl+Zが2回要る（3.1）。 */
    bool dragStartedFromPencil = false;

    /** ペンで押したが、まだ置いていない状態（Phase 70）。

        **クリックだけでは置かない**（動かしたときだけ置く）ので、
        押した位置を覚えておいて`mouseDrag`で初めてモデルへ書く。 */
    bool pencilPendingAdd = false;
    juce::Point<int> pencilPendingPosition;

    //==========================================================================
    // 8.29の表：ノートの発音（Phase 71）

    /** いま鳴らしている音（-1なら鳴っていない）。

        **1音だけ覚えれば足りる。** ドラッグで別の行へ移ったら、
        前の音を止めてから次を鳴らす（止め忘れると鳴りっぱなしになる。14dと同じ話）。 */
    int previewedPitch = -1;

    /** 8.121：上下に動かしている最中の読み取りを、どこへ出すか（Phase 156／改善案22）。
        カーソルの位置。**ノートの上には出さない**——掴んでいる指の下になる。 */
    juce::Point<int> dragReadoutPosition;


    /** 再生位置（タイムライン上の時刻）。**描くのはX座標が変わったときだけ**。 */
    double playheadSeconds = 0.0;

    /** ペンで見出しをなぞって鳴らしている最中か（Phase 71）。 */
    bool keyboardPreviewing = false;

    /** 8.121：動かしている最中の「いまどの音か」（Phase 156／改善案22）。
        音名・MIDIノート番号、ドラムでは行の名前も出す。 */
    void drawDragReadout (juce::Graphics& g) const;

    void startPreview (int pitch, int velocity);

    void stopPreview();

    /** ノートの右クリックメニュー（Phase 52）。削除はここから行う。 */
    void showNoteMenu (const juce::ValueTree& noteState, juce::Point<int> screenPosition);

    /** 8.29の表：空いているグリッドの右クリックメニュー（Phase 71）。
        カット・コピー・**押した位置への貼り付け**・削除。 */
    void showGridMenu (const juce::MouseEvent& e);

    /** 選択中のノートをまとめて消す（1つだけのときも通る）。 */
    void deleteSelectedNotes();

    /** カットツール：その位置でノートを2つに割る。 */
    void cutNoteAt (const juce::ValueTree& noteState, int x);

    /** Ctrl＋ドラッグでの複製。**選んでいるノートを全部**、掴んだノートが動いた量だけ
        ずらした位置へ複製する（1つずつ落とした位置を計算すると、全部が重なる）。 */
    void duplicateSelectedNotesByDrag();

    /** ドラッグでの移動。**掴んだノート以外**の選択中のノートを、同じ量だけずらす
        （掴んだ1つは従来どおりの処理が動かす）。 */
    void moveOtherSelectedNotesByDrag();

    /** Ctrlを押しながらのドラッグは「移動」ではなく「複製」（Phase 52）。
        掴んだ時点で決まり、離すときに複製を置く。 */
    bool dragIsCopy = false;

    // 仕様書5.3.1：構成音カラーリング（Phase 46）。既定はコード構成音モード。
    // **画面の状態なのでプロジェクトには保存せず**、アプリ設定へ入れている
    // （どのプロジェクトを開いても、前回の見え方のままにしたいため。設計書2.5）。
    NoteColouring noteColouring = NoteColouring::chordTones;

    // 8.29の表：鍵盤の表示（Phase 70）。**画面の状態なのでプロジェクトには保存せず**、
    // アプリ設定へ入れている（構成音カラーリングと同じ扱い。設計書2.5）
    KeyboardLabels keyboardLabels = KeyboardLabels::noteName;

    ChordWatcher chordWatcher { *this };
    juce::ValueTree subscribedProjectState;

    // 塗りの濃さ。**不透明にしないこと**（黒鍵の行かどうかが分からなくなる）
    static constexpr float chordToneAlpha = 0.30f;
    static constexpr float scaleToneAlpha = 0.20f;
    static constexpr float scaleRootAlpha = 0.34f;

    // 仕様書5.3.3：点の掴みやすさ（Phase 23）
    static constexpr int ccPointSize = 7;
    static constexpr int ccGrabMargin = 6;

    /** 8.87：**いま手で触っているノート**（Phase 127）。無効なら触っていない。

        **Phase 126までは`int selectedNoteIndex`（編集中クリップの中の番号）でした。**
        クリップをまたいで表示するようになると、番号だけでは
        「どのクリップの何番目か」が決まりません（1.32の「番号で覚えたものはずれる」の、
        もう一段きつい版）。**ValueTreeなら、そこからクリップも辿れます**
        （`getClipForClipEvent()`）。

        `selectedNotes`（複数選択）とは役割が違います：こちらは
        **ドラッグの主役1つ**で、選択に入っていないこともあります。 */
    juce::ValueTree activeNoteState;

    /** `activeNoteState`がまだ生きているか（消された・Undoで巻き戻ったのを弾く）。 */
    bool hasActiveNote() const { return activeNoteState.isValid() && activeNoteState.getParent().isValid(); }

    DragMode dragMode = DragMode::None;
    juce::Point<int> dragStartPosition;
    double dragOriginalStartTime = 0.0;
    double dragOriginalLength = 0.0;
    int dragOriginalPitch = 0;
    int dragOriginalVelocity = 100;
    double dragPreviewStartTime = 0.0;
    double dragPreviewLength = 0.0;
    int dragPreviewPitch = 0;
    int dragPreviewVelocity = 100;

    // 仕様書5.3.3：CCの点をドラッグ中の状態（Phase 23）。
    //
    // **番号ではなくValueTree（CCEvent）そのものを掴んでおく。** 点を左右へ動かすと
    // 時刻順が入れ替わり得るため、番号で覚えていると途中から別の点を編集してしまう。
    CCEvent draggedCCEvent { juce::ValueTree() };
    double dragOriginalCCTime = 0.0;
    int dragOriginalCCValue = 0;
    double dragPreviewCCTime = 0.0;
    int dragPreviewCCValue = 0;

    //==========================================================================
    // 8.1のG5：下部のレーン（Phase 75／8.35）

    /** レーンに何を出しているか。**画面の状態なのでプロジェクトには保存せず**、
        アプリ設定へ入れている（構成音カラーリングと同じ扱い。設計書2.5）。 */
    LaneTarget laneTarget;

    /** ビューポートで見えている縦の範囲（Phase 76）。レーンの固定表示に使う。 */
    int visibleScrollOffsetY = 0;
    int visibleHeight = 0;

    /** 仕様書5.6：オートメーションの点をドラッグ中の状態（Phase 75）。

        **CCと違って番号で持っています**：`AutomationLane`は時刻順を保つ作りで、
        点の追加・移動のたびに並べ直されるため、離した時点で引き直します。 */
    int draggedAutomationPoint = -1;
    double dragPreviewAutomationTime = 0.0;
    float dragPreviewAutomationValue = 0.0f;

    /** 8.37：曲がり具合のつまみをドラッグ中の状態（Phase 77）。

        **掴んでいるのは「区間の形を持っている点」**（手前の点）で、
        オートメーションの点でもCCイベントでも同じように扱う。
        ValueTreeで持つ理由はCCの点と同じ（番号は並べ替えでずれる。1.32）。

        両端の高さは**掴んだ時点のもので固定**する。ドラッグ中に引き直すと、
        自分が曲げた結果を読んでまた曲げる、という追いかけっこになる。 */
    juce::ValueTree curveDragOwner;
    float curveDragFromY = 0.0f;
    float curveDragToY = 0.0f;

    //==========================================================================
    // 8.38：レーンでのツール（Phase 78）

    /** 選んでいるレーンの点。**オートメーションの点でもCCイベントでも同じ入れ物**に入れる
        （どちらか一方しか出ていないので、混ざることはない）。
        **番号ではなくValueTreeで持つ**理由はノートと同じ（1.32）。 */
    std::vector<juce::ValueTree> selectedLanePoints;

    /** 矢印ツールで**押しただけか、ドラッグしたか**が決まるまでの構え。

        **押した時点では点を置きません。** 置いてしまうと、範囲選択のつもりの
        ドラッグでも点が増える（ペンでのノート追加と同じ形。Phase 70／8.29）。 */
    bool lanePendingAdd = false;
    juce::Point<int> lanePendingPosition;

    /** ラバーバンド（範囲選択）の状態。 */
    bool laneRangeSelecting = false;
    juce::Point<int> laneRangeAnchor;
    juce::Rectangle<int> laneRangeBounds;

    /** まとめて動かすための控え（掴んだ点**以外**の、掴んだ時点の位置）。 */
    struct LanePointOrigin
    {
        juce::ValueTree state;
        double time = 0.0;
        float value = 0.0f;
    };

    std::vector<LanePointOrigin> laneDragOthers;
    double laneDragAnchorTime = 0.0;
    float laneDragAnchorValue = 0.0f;

    /** ペンでのなぞり書きの状態（Phase 78）。 */
    bool lanePaintStarted = false;
    int lanePaintLastX = 0;
    double lanePaintLastTime = 0.0;
    float lanePaintValue = 0.0f;

    /** なぞり書きの**点の細かさ**（px）と、**手ぶれのならし具合**。

        細かくするほど元の動きに忠実になるが、点が増えてMIDIも重くなる。
        ならし具合は0〜1で、**1なら生の値そのまま**（ならさない）。

        8.122：**8pxから4pxへ狭めた**（Phase 157／改善案28）。
        点が倍になる代わりに、線が階段状に見えなくなる。

        **ならし具合は0.45のまま**でよい。ならしは「1歩あたり45%だけ近づく」形なので、
        **間隔を半分にすると、追いつくまでの距離も半分**になる——
        細かくすると、滑らかさと反応の速さが同時に良くなる。

        **アレンジ画面のオートメーショントラックも同じ値**にすること
        （`TimelineComponent::automationPaintMinPixels`）。本人の指定でもある。 */
    static constexpr int lanePaintMinPixels = 4;
    static constexpr float lanePaintSmoothing = 0.45f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
