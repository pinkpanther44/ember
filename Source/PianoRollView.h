#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "PianoRollComponent.h"
#include "AudioEngine.h"
#include "SnapGridSelector.h"   // 仕様書5.5・5.9：編集の刻み（Phase 55）
#include "PianoRollHeaderComponent.h"   // 仕様書5.9：ルーラーとコード帯（Phase 67）
#include "PianoRollTrackList.h"          // 8.1のG4：左のMIDIトラック一覧（Phase 73）
#include "IconAssets.h"          // 8.133：ツールの絵（Phase 169）
#include "ValueEntrySlider.h"     // 8.118：Swing・グルーヴの強さもConsoleと同じつまみへ（Phase 153）

//==============================================================================
/**
    ピアノロールを表示するページ（設計書2.3.3）。

    設計書2.2では、エディタはアレンジ画面の下部にインライン表示し、
    必要に応じて別ウィンドウへポップアウトする構成としている。
    Phase 6aではまず独立したタブとして実装し、UIの統合は後のフェーズで行う。

    **Phase 14でトラック選択に対応した。** 音源はトラックごとに割り当てるようになったため、
    「どのトラックを編集し、どのトラックへ音源を割り当てるのか」を選べる必要がある。
    編集対象のクリップも、選んだトラックの中から決まる。
*/
class PianoRollView : public juce::Component,
                       private juce::ScrollBar::Listener,
                       // 8.190：**音源の差し替えに追い付くため**（Phase 228／本人の報告）。
                       // それまでこの画面は**モデルを一切見張っていませんでした**——
                       // 名前を書き換えるのは「トラックを選び直したとき」と
                       // 「自分のメニューから挿したとき」だけ。
                       // **他の画面から差し替えると、ここだけ前の名前のまま**でした
                       private juce::ValueTree::Listener
{
public:
    PianoRollView (ProjectModel& projectToUse, AudioEngine& engineToUse);
    ~PianoRollView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 仕様書6.2：ツールを流し込む（Phase 52）。

        **値を持っているのはMainComponent**で、アレンジ画面とピアノロールの
        両方へ同じものを配る。片方だけ古くなる状態を作らないため。 */
    void setEditTool (EditTool tool);

    /** 仕様書6.2：ピアノロールの中からツールが選ばれたときに呼ばれる（Phase 69）。

        **値を持っているのはMainComponent**で、アレンジ画面とピアノロールの
        両方へ同じものを配る（8.11）。 */
    std::function<void (EditTool)> onEditToolSelected;

    //==========================================================================
    // 仕様書5.9：ルーラーからの要求（Phase 72／8.33）。
    // **どれもMainComponentの入口へ返す**（アレンジ画面と同じものを通す）

    std::function<void (double timelineSeconds)> onSeekRequested;
    std::function<void (double timeSeconds, bool askForName)> onInsertMarkerRequested;
    std::function<void()> onLoopChanged;
    std::function<void (double startTimeSeconds)> onChordRegionDoubleClicked;

    /** 再生位置を渡す（タイムライン上の時刻）。ルーラーとグリッドの両方へ配る。 */
    void setPlayheadSeconds (double timelineSeconds);

    //==========================================================================
    // 仕様書5.9：ズーム（Phase 67／8.1のG3）
    //
    // **ショートカット（E／W／Alt+Z）は`MainComponent`から流れてくる。**
    // ピアノロールにフォーカスがあるときだけこちらへ届き、それ以外は
    // アレンジ画面へ行く（値は画面ごとに別なので、配るのではなく振り分ける）。

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    //==========================================================================
    // 仕様書6.2：カット／コピー／貼り付け（Phase 71）。**中身は`PianoRollComponent`**

    bool cutSelection()  { return pianoRoll.cutSelection(); }
    bool copySelection() { return pianoRoll.copySelection(); }

    /** 再生カーソル（タイムライン上の時刻）から貼り付ける。 */
    bool pasteAtTimelineTime (double timelineSeconds) { return pianoRoll.pasteAtTimelineTime (timelineSeconds); }

    void visibilityChanged() override;

    //==========================================================================
    // 仕様書5.5・5.9：編集の刻み（スナップ、Phase 55）
    //
    // **アレンジ画面にも同じものが出ている。** ツールと同じで、値を持っているのは
    // モデルで、配るのはMainComponentの仕事（1.27・8.15）。
    //
    // ここにも置いたのは、**エディタをポップアウトすると別ウィンドウになる**ため。
    // 打ち込んでいる最中にこそ刻みを変えたいのに、元のウィンドウへ戻る必要があった。

    /** ツールバーのコンボボックスで刻みが選ばれたときに呼ばれる。 */
    std::function<void (SnapGrid)> onSnapGridSelected;

    /** 表示をモデルの値へ合わせる（もう片方の入口で変えられたときにも呼ぶこと）。 */
    void setSnapGrid (SnapGrid grid);

    /** 仕様書5.1：プロジェクトを読み込み/新規作成した後に、表示を作り直す。 */
    void refreshAfterProjectChanged (bool keepSelection = false);

    /** 設計書4.2：指定したMIDIクリップを編集対象にする（Phase 15）。
        アレンジ画面でクリップをダブルクリックしたときの受け口。
        trackIndexはプロジェクト全体でのトラック番号、clipIndexはそのトラック内の
        MIDIクリップの番号。 */
    void showTrack (int trackIndex, double timelineSeconds = -1.0);

    /** モデルを読み直して表示を合わせる（選択中のトラック・クリップは維持する）。

        他のタブでトラックや音源が変わった後に呼ぶ。エディタパネルを開くときにも
        呼んでおくこと：`visibilityChanged()`は「表示状態が実際に変わったとき」しか
        飛ばないため、それだけに頼ると更新が漏れることがある（Phase 16）。 */
    void refreshFromModel();

private:

    //==========================================================================
    // 仕様書5.9：横スクロール（Phase 67）

    /** スクロールバーの範囲と位置を、ピアノロールの状態へ合わせる。 */
    void updateHorizontalScrollBar();

    void scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    //==========================================================================
    // 8.190：音源の差し替えに追い付く（Phase 228）。
    //
    // **プロパティの変更は見ていません。** 音源の抜き差しは
    // `<INSTRUMENT>`の子の増減として届くので、この2つで足ります。
    // 全部の変更で名前を引き直すと、ノートを1つ動かすたびに走ります

    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int) override;

    /** ピアノロール本体の大きさを、Viewportの見えている範囲に合わせる。

        **横はViewportにスクロールさせないので、幅＝見えている幅**にする
        （縦だけはピアノロール自身が決める。CCレーンで変わるため）。 */
    void updatePianoRollSize();

    void quantiseClicked();

    /** 仕様書5.3：音源ボタンのメニュー（Phase 32）。
        Phase 31までは「Load Instrument...」と「Instrument GUI」の2つに分かれていたが、
        狭いエディタパネルで2段を圧迫していたため、1つのボタンに畳んだ
        （Consoleのチャンネルストリップの音源スロットと同じ操作感）。 */
    void instrumentButtonClicked();

    /** 音源プラグインの一覧を出して選ばせる。 */
    void chooseInstrument();

    /** 仕様書5.3.3：CCレーンの追加・並べ替え・表示/非表示のメニューを出す（Phase 23）。 */

    //==========================================================================
    // 仕様書5.3.4：グルーヴクオンタイズ（Phase 24）

    /** 編集中のクリップからグルーヴテンプレートを抽出する。 */
    void extractGrooveClicked();

    /** 選んだテンプレートを、編集中のクリップへ強さ付きで適用する。 */
    void applyGrooveClicked();

    /** テンプレート一覧をコンボボックスへ入れ直す（抽出・削除の後に呼ぶ）。
        選択は並び順ではなくIDで覚え直す（増減で別のテンプレートへすり替わらないように）。 */
    void refreshGrooveList();

    /** グルーヴの操作パネルを開閉する（Phase 32）。
        一式（Extract / 一覧 / 強さ / Apply）は常時出しておくには場所を取りすぎるので、
        ツールバーの下に**重ねて**出す。ツールバーの段数を増やさずに済む。 */
    void setGroovePanelOpen (bool shouldBeOpen);

    //==========================================================================
    // 仕様書5.3.2：ドラムエディター（Phase 25）

    /** 表示モードのトグルが押されたとき（設計書2.3.3）。 */
    void drumEditorToggled();

    /** 仕様書5.3.1：構成音カラーリングを次のモードへ回して、ボタンの表示も合わせる。 */
    void cycleNoteColouring();

    /** ボタンの文字と色を、いまのモードに合わせる（起動時の復元でも使う）。 */
    void updateColouringButton();

    /** 8.1のD5：透かしのボタンの見た目を、いまの状態に合わせる（Phase 73）。 */

    /** 選択中トラックの設定に合わせて、ピアノロールの表示モードを揃える。
        トラックを切り替えたときにも呼ぶ（トラックごとに設定を持つため）。 */
    void updateDrumEditorMode();

    /** 8.161：ドラム表示へ切り替わった瞬間だけ、いちばん下まで送る（Phase 199）。

        行の向きを裏返したので、**よく使うキック／スネアは下**にいます。
        上のまま開くと、Low Wood Blockあたりの空の行から始まってしまう。

        **切り替わった瞬間だけ。** 毎回の`refresh()`で送ると、
        自分でスクロールした位置が戻されてしまう（1.15の「モデルを見る」と同じ話で、
        ここで見ているのは**表示モードが変わったかどうか**）。 */
    bool drumModeWasEnabled = false;

    /** 編集中のMIDIクリップ。選択が無ければ`state.getParent()`が無効なものを返す。 */

    /** グリッドのコンボボックスの選択を「1拍を何分割するか」へ変換する。
        通常のクオンタイズ（5.3）とグルーヴ（5.3.4）の両方が使うので、
        対応表はここ1箇所に置く。 */
    int getSelectedGridDivision() const;
    void refreshClipSelection();
    void updateInstrumentLabel();
    void updateStatusLabel();

    /** MIDIトラックの一覧をコンボボックスへ入れ直す。
        選択は「並び順」ではなくtrackIdで覚え直すため、トラックが増減しても
        編集対象が別トラックへすり替わらない。 */
    void refreshTrackList();

    /** 選択中トラックのMIDIクリップ一覧をコンボボックスへ入れ直す（Phase 15）。 */

    /** 現在選択中のMIDIトラック。1本も無ければ`state.getParent()`が無効なTrackを返す。 */
    Track getSelectedTrack() const;

    void trackSelectionChanged (const juce::String& trackId);

    ProjectModel& project;
    AudioEngine& engine;

    /** 8.190：いま購読しているプロジェクトのルート（Phase 228）。

        **プロジェクトを読み込むとルート自体が差し替わる**ので、
        毎回見比べて付け替えます（`InspectorPanel`と同じやり方。1.15）。 */
    juce::ValueTree subscribedProjectState;

    // 選択中のMIDIトラックのID（設計書1.3のTrack.id）。
    // コンボボックスの選択インデックスではなくこちらを正とする。
    juce::String selectedTrackId;

    /** 設計書2.5：構成音カラーリングのモードを覚えておくキー（Phase 46）。
        値は`PianoRollComponent::NoteColouring`をintにしたもの。 */
    const juce::String noteColouringKey { "pianoRollNoteColouring" };

    /** 設計書2.5：鍵盤の音名／階名を覚えておくキー（Phase 70）。
        値は`PianoRollComponent::KeyboardLabels`をintにしたもの。 */
    const juce::String keyboardLabelsKey { "pianoRollKeyboardLabels" };

    /** 設計書2.5：下部のレーンの高さを覚えておくキー（Phase 157／改善案36）。
        値はpx。**上下限の丸めは`PianoRollComponent::setLaneHeight()`が持っている**ので、
        ここでは読んだ値をそのまま渡す（同じ判断を2箇所に書かない）。 */
    const juce::String laneHeightKey { "pianoRollLaneHeight" };

    /** 設計書2.5：下部のレーンに何を出していたかを覚えておくキー（Phase 75）。

        3つに分けているのは、**種類だけでは対象が決まらない**ため
        （オートメーションならどのパラメータか、CCなら何番か）。 */
    const juce::String laneTargetKindKey       { "pianoRollLaneKind" };
    const juce::String laneTargetAutomationKey { "pianoRollLaneAutomation" };
    const juce::String laneTargetControllerKey { "pianoRollLaneController" };

    // 選択中トラックの中で、何番目のMIDIクリップを編集しているか（Phase 15）。
    // クリップにはIDがあるが、同一トラック内で番号が変わる操作（削除）をしても
    // 「近い位置のクリップ」へ収まればよいため、番号で覚えている。

    juce::TextButton addMidiTrackButton { "+ Track" };

    // 仕様書5.3：音源スロット（Phase 32で1つのボタンに畳んだ）。
    // ボタン自身が「今どの音源が載っているか」を表示する。
    juce::TextButton instrumentButton   { "+ Instrument" };

    juce::TextButton quantiseButton     { "Quantise" };

    // 仕様書5.3.3：CCレーン（Phase 23）。1つのボタンに追加・並べ替え・表示切替をまとめる
    // （操作の行が増えすぎると、狭いエディタパネルでボタンが押せなくなるため）。

    /** 仕様書6.2：ツールのボタン（Phase 69）。

        **Phase 68まで、ボタンはアレンジ画面にしかありませんでした。**
        Phase 69で「ノートを置くのはペンツールの仕事」に変えたので、
        ピアノロール側にも入口が要ります（ポップアウトすると
        アレンジ画面のツールバーは別ウィンドウで、手が届きません）。

        **押しても自分では切り替えません**（`onEditToolSelected`で上へ返す。8.11）。 */
    // **文字は`utf8()`が使えるコンストラクタで入れること**（1.2）。ヘッダでは使えない
    /** 8.133：本人が用意した絵を使う（Phase 169／改善案44）。
        **アレンジ画面のツールと同じ絵**——同じツールが画面によって違う顔だと、
        どちらがどれか読めなくなります（1.27）。 */
    IconAssets::SvgButton arrowToolButton;
    IconAssets::SvgButton pencilToolButton;
    IconAssets::SvgButton cutToolButton;
    IconAssets::SvgButton eraserToolButton;   // Phase 83（C11）

    /** ツールボタンの見た目を、いまのツールに合わせる。 */
    void updateToolButtons();

    // 仕様書5.3.2・設計書2.3.3：ピアノロール／ドラムエディターの切り替え（Phase 25）
    juce::TextButton drumEditorButton   { "Drum Editor" };

    /** 仕様書5.3.1・設計書2.3.3：構成音カラーリングの切り替え（Phase 46）。
        押すたびに コード構成音 → スケール音 → オフ と回る。 */
    juce::TextButton colouringButton    { "Chord Tones" };

    /** 仕様書5.5・5.9：編集の刻み（Phase 55）。

        **クオンタイズの`gridBox`とは別物。** どちらも「1/16」のような表示になるので、
        部品側に「Snap」の見出しを付けてある（`SnapGridSelector`の説明を参照）。 */
    SnapGridSelector snapSelector;

    // 仕様書5.3.4：グルーヴクオンタイズ（Phase 24）。
    //
    // **Phase 32でツールバーの段から外し、重ねて出すパネルにした。**
    // 4つの操作を常時出しておくと、狭いエディタパネルではノートグリッドが潰れる。
    // 使うのは「たまに」なので、開いている間だけ場所を取れば足りる。
    juce::TextButton grooveButton { "Groove..." };

    /** 重ねて出すパネルの地。**背景を塗らないと、下のノートグリッドが透けて
        操作部が読めなくなる**（`juce::Component`は既定で何も描かないため）。 */
    struct PanelBackground : public juce::Component
    {
        void paint (juce::Graphics& g) override;
    };

    PanelBackground groovePanel; // 下の4つを子として持つ器（開閉と描画をまとめるため）
    juce::TextButton extractGrooveButton { "Extract Groove" };
    juce::ComboBox grooveBox;
    juce::Label grooveStrengthLabel;

    /** 8.118：**Swingと同じ理由で`ValueEntrySlider`にした**（Phase 153／改善案24）。
        すぐ上のSwingと同じ0〜100の値なので、片方だけ見た目が違うと
        「別のもの」に見えてしまう。 */
    ValueEntrySlider grooveStrengthSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton applyGrooveButton   { "Apply Groove" };

    // 選択中のテンプレートID（コンボの並び順ではなくこちらを正とする）
    juce::String selectedGrooveId;
    juce::ComboBox gridBox;
    /** 8.118：**`ValueEntrySlider`に差し替えた**（Phase 153／改善案24）。
        ここも`juce::Slider`のままで初期の見た目（丸いつまみ）が残っていた。
        差し替えるだけで、掴んだところ以外では値が飛ばなくなり
        （`setSliderSnapsToMousePosition(false)`）、右クリックで数値も打てる。 */
    ValueEntrySlider swingSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label swingLabel;
    juce::ToggleButton selectedOnlyToggle { "Selected note only" };
    juce::Label statusLabel;


    /** 8.1のG4／D5：左のMIDIトラック一覧（Phase 73）。

        **ツールバーのトラック選択コンボの置き換えです。**
        「+ Track」ボタンも一覧に吸収したので、ツールバーは1段目が空きました。 */
    PianoRollTrackList trackList { project };

    /** 8.1のD5：他トラックのノートの透かし（Phase 73）。 */

    /** 設計書2.5：透かしの入切を覚えておくキー（Phase 73）。 */
    PianoRollComponent pianoRoll { project };

    // 仕様書5.9：ルーラーとコード帯（Phase 67／8.1のG3）。
    // **Viewportの外に置く。** 中に入れると、鍵盤を上下にスクロールしたときに
    // 目盛りごと流れて消える（アレンジ画面の固定行と同じ理由。8.20）
    PianoRollHeaderComponent header { project, pianoRoll };

    /** 縦のスクロール量を知らせてくれるViewport（Phase 76／8.36）。

        **下部のレーンを固定表示にするために要ります。** `Viewport`は
        スクロールしても中身へは何も言わないので、`visibleAreaChanged()`を
        拾ってピアノロール本体へ渡します。 */
    struct ScrollAwareViewport : public juce::Viewport
    {
        std::function<void()> onVisibleAreaChanged;

        void visibleAreaChanged (const juce::Rectangle<int>&) override
        {
            if (onVisibleAreaChanged != nullptr)
                onVisibleAreaChanged();
        }
    };

    ScrollAwareViewport viewport;

    /** 横スクロールバー（Phase 67）。

        **Viewportには横スクロールをさせない。** させると鍵盤も左へ流れて消え、
        ルーラーとコード帯も別経路でスクロール量を知る必要が出る。
        アレンジ画面と同じく、**横は自前**（`PianoRollComponent::scrollStartSeconds`）。 */
    juce::ScrollBar horizontalScrollBar { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
