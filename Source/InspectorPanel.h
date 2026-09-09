#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "SelectionState.h"
#include "TrackRackComponent.h"
#include "LevelMeterComponent.h"   // 仕様書5.7：レベルメーター（Phase 59／8.1のC4）
#include "ValueEntrySlider.h"      // Phase 61：ダブルクリックでの数値入力（8.1のC2）
#include "ColourSwatchButton.h"  // 8.125：色見本はヘッダーと共用（Phase 161）

class AudioEngine;

//==============================================================================
/**
    設計書2.2・2.3.7・仕様書4.5に対応するサイドパネル（Phase 17）。
    **Phase 28で右から左へ移した**（選択対象＝左に並ぶトラックヘッダーのすぐ隣になる）。

    **選択対象に応じて表示内容が切り替わるコンテキストパネル**：

    - トラック選択時：名前・色・種別・音量・パン・ミュート／ソロ、
      および**Consoleと同じラック**（音源・インサート・センド・VCA・書き込みモード）
    - クリップ選択時：上記に加えて開始位置・長さ（オーディオはフェード、MIDIはノート数）

    **Phase 29でラック（`TrackRackComponent`）を載せた。** これにより
    「音を作る」「音を混ぜる」の操作がアレンジ画面を離れずに行えるようになった。
    ラックの中身はConsoleのチャンネルストリップと同一の部品で、
    どちらで操作しても同じ結果になる（片方だけ古くなることがない）。

    載せる要素が増えて縦に収まらなくなったため、中身はViewportに入れてある。

    値の実体はすべてProjectModel（ValueTree）側にあり、このパネルは状態を持たない
    （設計書1.2の「UI側に状態を持たせない」原則）。編集はUndo可能にするため、
    必ず`project.getUndoManager()`を通す（HANDOVER 3.1）。
*/
class InspectorPanel : public juce::Component,
                        public juce::DragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::ValueTree::Listener,
                        /** 仕様書5.7：レベルメーターの更新（Phase 59）。
                            **見えている間・トラックを選んでいる間だけ回す。** */
                        private juce::Timer
{
public:
    InspectorPanel (ProjectModel& projectToUse, SelectionState& selectionToUse, AudioEngine& audioEngineToUse);
    ~InspectorPanel() override;

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

    /** 仕様書5.1：プロジェクトを読み込み/新規作成した後に、表示を作り直す。 */
    void refreshAfterProjectChanged();

    /** 8.54：再生位置を受け取って、フェーダーをそこの値に合わせる（Phase 93）。
        値を決めるのは`Track::getEffectiveVolumeDbAt()`（Consoleと同じもの）。 */
    void setPlayheadSeconds (double seconds);

    static constexpr int defaultWidth = 230;
    static constexpr int minimumWidth = 170;

    /** 8.77：MIDIノートをまとめて上下させたい（Phase 117／改善案㉝）。

        **自分では動かしません。** アレンジ画面の
        `TimelineComponent::transposeSelection()`が
        「どのクリップに効かせるか」（複数選択・IDでの引き直し・範囲外の扱い）を
        全部持っているので、そこへ返します。
        ここで書くと、**メニューから呼んだときと結果が違う**ことになります（8.32）。 */
    std::function<void (int semitones)> onTransposeRequested;

    //==========================================================================
    // 仕様書4.4・6章：ブラウザからのドラッグ&ドロップの受け口（Phase 29）。
    // 判定と実行はTrackRackComponentの静的関数に集約してある（Consoleと同じ経路）。

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    /** 選択が変わったときに呼ばれる（SelectionStateの通知）。 */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** 他の画面（ミキサー等）から同じ値が変えられたときも表示を合わせる。 */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override;

    /** 並べ替えに追従する（Phase 34）。

        **並べ替えは「親の子順の変更」なので、トラック自身を購読していても届かない。**
        ルートを購読して、`<TRACKS>`の並びが変わったときだけ表示を作り直す
        （「上へ／下へ」を押せるかは、そのトラックが何番目かで決まるため）。 */
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;

    /** 選択内容に合わせて、出すコントロールを決め直す。 */
    void rebuildForSelection();

    /** モデルの現在値をコントロールへ流し込む（表示の更新だけ）。 */
    void updateControlsFromModel();

    /** 中身の縦並び。`apply`がfalseなら位置を動かさず、必要な高さだけを返す
        （Viewportへ渡す中身の高さを決めるのに使う）。 */
    int layOutContents (int width, bool apply);

    /** 選択中のトラック。無ければ`state.getParent()`が無効なTrackを返す。 */
    Track getSelectedTrack() const;

    /** 8.59：選択中のオートメーションの行（Phase 96）。

        レーンを選んでいないときは`state.isValid()`がfalse。
        **マスターのレーンもここで拾う**（`SelectionState`はtrackIdが空文字でマスターを表す）。 */
    AutomationLane getSelectedAutomationLane() const;

    /** 8.59：色見本の枠と「トラックの色に戻す」を、レーンの状態に合わせる（Phase 96）。 */
    void updateLaneColourSwatches (const AutomationLane& lane);

    void colourSwatchClicked (int swatchIndex);
    void nameChanged();

    /** Phase 33：選択中のトラックを削除する。中身があるときは確認する。 */
    void deleteSelectedTrack();

    /** 選択中のトラックが今何番目か。見つからなければ-1。 */
    int getSelectedTrackIndex() const;

    ProjectModel& project;
    SelectionState& selection;
    AudioEngine& audioEngine;

    // 購読中のプロジェクトのルート（Phase 34でトラック単位からここへ広げた）。
    // トラックだけを見ていると、**並べ替え（親の子順の変更）に気づけない**。
    // ルートなら子孫の変更がすべて届くので、必要なものだけ選んで反応する。
    juce::ValueTree subscribedProjectState;

    // 表示中のトラック。ルートには関係の無い通知も来るので、選り分けに使う
    juce::ValueTree selectedTrackState;

    /** 8.59：表示中のオートメーションの行（Phase 96）。

        **マスターのレーンは`selectedTrackState`では拾えない**（トラックの下に無い）ので、
        通知の選り分け用に別で持つ。 */
    juce::ValueTree selectedLaneState;

    /** 8.54：いまの再生位置（Phase 93）。フェーダーに出す値を決めるのに使う。 */
    double playheadSeconds = 0.0;

    juce::Label titleLabel;
    juce::Label emptyLabel;

    // 載せる要素が増えて縦に収まらなくなったため、中身はスクロールできるようにしてある。
    // コントロールはすべて`content`の子で、位置は`layOutContents()`が決める。
    juce::Viewport viewport;
    juce::Component content;

    //==========================================================================
    // トラック用のコントロール
    juce::Label nameCaption;
    /** 8.53：トラック名の欄（Phase 92）。

        **`TextEditor`から`Label`へ変えました。** `TextEditor`はウィンドウが前面に来ただけで
        入力欄として選ばれてしまい、**インスペクタのどこを触っても名前が選択された状態**に
        なっていました。`Label`は**自分をクリックしたときだけ**入力欄になります。 */
    juce::Label nameEditor;
    juce::Label typeLabel;

    juce::Label colourCaption;

    /** 8.119：色見本のボタン（Phase 154／改善案39）。

        8.125：**`ColourSwatchButton.h`へ出しました**（Phase 161／改善案26）。
        トラックヘッダーの色帯からも同じパレットを出すので、
        ここだけの部品にしておくと見た目が2種類になります（1.27）。 */
    juce::OwnedArray<ColourSwatchButton> colourSwatches;

    /** 8.59：レーンの色を親トラックの色へ戻す（Phase 96）。
        **オートメーションの行を選んでいるときだけ出る**（トラックには戻す先が無い）。 */
    juce::TextButton resetLaneColourButton;

    juce::Label volumeCaption;
    // Phase 61：ダブルクリックで数値入力、Alt＋クリックで初期値へ（Consoleと同じ扱い。8.21）
    ValueEntrySlider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label panCaption;
    ValueEntrySlider panSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    /** 仕様書5.7：レベルメーター（Phase 59／8.1のC4）。

        **Consoleのストリップと同じ部品**（`LevelMeterComponent`）を横向きで置いている。
        音を通さないトラック（コード・フォルダ・VCA）では隠す。 */
    juce::Label meterCaption;
    LevelMeterComponent meter;

    void timerCallback() override;
    void visibilityChanged() override;

    /** メーターを回すべき状態か（見えていて、音を通すトラックを選んでいる）。 */
    bool shouldRunMeter() const;

    /** メーターのタイマーを、いまの状態に合わせて回す／止める。 */
    void updateMeterTimer();

    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };

    // Phase 33：トラックの削除。
    // タイムラインのヘッダーを右クリックしても同じことができるが、
    // **右クリックは見つけてもらえない**ので、設定の並ぶここにも置いている。
    //
    // 8.60：**「上へ」「下へ」は外しました**（Phase 97）。並べ替えはヘッダーの
    // ドラッグでやるものになっていて（Phase 34・36）、1マスずつ動かすボタンは
    // 使われていませんでした。
    //
    // ラベルはコンストラクタで`utf8()`を通して入れる（HANDOVER 1.2）。
    // ここで直接リテラルを書くと、日本語環境のビルドで文字化けの原因になる。
    juce::TextButton deleteTrackButton;

    // 仕様書5.3/5.7/5.2.2/5.2.4/5.6：Consoleと共通のラック（Phase 29）。
    // **選択が変わるたびに作り直す。** 担当トラックはコンストラクタで決まる作りなので、
    // 付け替えではなく作り直しのほうが購読の外し忘れが起きない。
    // 音声を通さないトラック（コード等）では nullptr のまま。
    std::unique_ptr<TrackRackComponent> rack;

    //==========================================================================
    // クリップ用のコントロール。値は「秒」で、編集可能なラベルとして出す
    juce::Label clipStartCaption;
    juce::Label clipStartLabel;
    juce::Label clipLengthCaption;
    juce::Label clipLengthLabel;
    juce::Label clipExtraCaption;
    juce::Label clipExtraLabel;

    //==========================================================================
    // 8.84：**MIDIトラックの入力設定**（Phase 124／改善案⑯。仕様書5.4）
    //
    // **MIDIトラックを選んでいるときだけ出します。** 他の種別では意味を持ちません。

    juce::Label midiInputCaption;
    juce::ComboBox midiInputDeviceBox;
    juce::ComboBox midiInputChannelBox;
    juce::Label midiOutputCaption;
    juce::ComboBox midiOutputChannelBox;

    /** つないでいるMIDI入力の一覧を並べ直す（トラックを選び直すたびに呼ぶ）。

        **保存しているのは名前**なので、いま挿さっていないデバイスの名前も
        一覧に足します——挿し直すまで設定が見えなくなるのは不親切です。 */
    void refreshMidiInputDeviceList();


    /** 8.77：MIDIクリップのトランスポーズ（Phase 117/改善案㉝）。
        **MIDIクリップを選んでいるときだけ出す**（オーディオには当てはまらない）。 */
    juce::Label transposeCaption;
    juce::OwnedArray<juce::TextButton> transposeButtons;
    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    /** 8.52：名前の入力欄が、いまどのトラックのものか（Phase 91）。
        **フォーカスが外れたときに書き込む先**。選択はもう次へ移っていることがある。 */
    juce::String nameEditorTrackId;

    // 仕様書4.4：ドラッグ中のプラグインがこのパネルの上にあるか（Phase 29）
    bool isDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};
