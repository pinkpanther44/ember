#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "ChannelStripComponent.h"
#include "MasterStripComponent.h"

class AudioEngine;

//==============================================================================
/**
    設計書2.3.2「コンソール（ミキサー）ビュー」。

    Phase 12aでチャンネルストリップ（音量・パン・ミュート／ソロ）、
    Phase 12bでレベルメーターとマスターチャンネルを実装した。
    トラックの表示順はアレンジビューと同期する（設計書2.3.2）。

    Phase 14でMIDIトラックも並ぶようになった（トラックごとの音源割り当てに対応し、
    フェーダー・インサート・センドが使えるようになったため）。

    Phase 12d-2でVCAトラック（仕様書5.2.4）が並ぶようになった。
*/
class ConsoleView : public juce::Component,
                     public juce::DragAndDropTarget,
                     private juce::Timer,
                     private juce::AsyncUpdater,
                     private juce::ValueTree::Listener
{
public:
    ConsoleView (ProjectModel& projectToUse, AudioEngine& audioEngineToUse);
    ~ConsoleView() override;

    void paint (juce::Graphics& g) override;

    /** 8.67：並べ替えの予告線（Phase 106／改善案㉛）。

        **子の上へ描く。** ストリップはViewportの中にいるので、
        地に描くと隠れて見えない（`TrackRackComponent`の予告線と同じ話。8.66）。 */
    void paintOverChildren (juce::Graphics& g) override;

    void resized() override;
    void visibilityChanged() override;

    /** 8.60：**空いているところの右クリックでトラックを追加**（Phase 97／改善案㉚）。

        Consoleでミックスしている最中に「センドをもう1本」と思ったとき、
        アレンジ画面まで戻る必要がありました。 */
    void mouseDown (const juce::MouseEvent& e) override;

    /** 8.60：トラック追加のメニューを出してほしいときに呼ばれる（Phase 97）。

        **メニューを組むのはArrangeView**（`showAddTrackMenuAt()`）。
        種別の並びと「どこへ足すか」を2箇所に書かないため（8.27）。 */
    std::function<void (juce::Rectangle<int> anchorScreenBounds)> onAddTrackRequested;

    /** 仕様書5.1：プロジェクトを読み込み/新規作成した後に、表示を作り直す。 */
    void refreshAfterProjectChanged();

    /** 8.54：再生位置を各ストリップへ配る（Phase 93）。

        **ここで値を判断しない。** 「その位置で効いている値」を決めるのは
        `Track::getEffectiveVolumeDbAt()`1箇所で、ここは配るだけ。 */
    void setPlayheadSeconds (double seconds);

    //==========================================================================
    /** 8.67：**ストリップを掴んでトラックを並べ替える**（Phase 106／改善案㉛。8.51）。

        ドラッグを始めるのは`ChannelStripComponent`（空きとトラック名）。
        **どこへ落ちたかを決めるのはこちら**で、並びを知っているのがここだけのため。

        アレンジ画面の並べ替え（Phase 34・36）と**同じモデルの入口**
        （`ProjectModel::moveTrackToSlot()`）を通るので、両方の画面に効きます。

        ### フォルダの深さは選べません

        アレンジ画面では**横の位置で階層を決めています**が（8.51）、
        Consoleは横がトラックの並びそのものなので、その余地がありません。
        **入れ先は「落とした位置のすぐ左のトラック」から受け継ぎます**
        （左がフォルダならその中へ、そうでなければ左と同じフォルダへ）。
        階層を組み替えたいときはアレンジ画面で行うこと。 */
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragMove (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    /** 8.67：その位置に落としたら**何本目の前**に入るか（0〜ストリップ数）。 */
    int getReorderSlotForPosition (juce::Point<int> localPosition) const;

    /** 8.67：予告線を引くX座標（このビューの座標）。 */
    int getReorderLineX (int slot) const;

    /** 8.67：ドラッグ中の落とし先（-1＝ドラッグしていない）。 */
    int reorderSlot = -1;

    /** トラック構成に合わせてチャンネルストリップを作り直す。 */
    void rebuildStrips();

    /** 仕様書5.7：メーター表示を更新する（およそ30fps）。 */
    void timerCallback() override;

    /** 仕様書5.2.4：VCAのリンクはVCAトラック側のValueTreeに入っているため、
        リンクされる側のストリップは自分の購読では変化に気づけない（Phase 12d-2）。
        プロジェクトのルートを購読して、リンクが変わったら全ストリップを更新する。
        こうしておくとUndo/Redoでの変化にも追従できる。 */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    /** 8.67：**トラックの並びが変わったら並べ直す**（Phase 106／改善案㉛）。

        Consoleで並べ替えたときだけでなく、**アレンジ画面での並べ替えとUndo/Redo**も
        ここを通る。これが無いと、タブを切り替えるまで古い並びのまま残る
        （`visibilityChanged()`でしか作り直していなかったため）。 */
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;

    /** 8.67：ストリップの作り直しを**いったん後回しにする**（Phase 106）。

        通知の中でその場で作り直すと、2つの困りごとが起きる：

        - `moveTrackToSlot()`は**フォルダの中身を1本ずつ動かす**ので（8.51）、
          1回の並べ替えで何度も通知が飛ぶ。そのたびに全ストリップを作り直すことになる。
        - ドロップの処理は**掴んでいたストリップのマウスイベントの途中**なので、
          その場で消すと「配っている最中の相手が居なくなる」（1.15と同じ形）。

        `AsyncUpdater`はまとめてくれるので、何度呼ばれても作り直しは1回で済む。 */
    void handleAsyncUpdate() override;

    /** 購読先を今のプロジェクトのルートへ合わせる（読み込みでルートが差し替わるため）。 */
    void updateProjectSubscription();

    ProjectModel& project;

    // 購読中のルート。差し替え時に古い方の購読を外すために持つ（HANDOVER 1.15）
    juce::ValueTree subscribedState;
    AudioEngine& audioEngine;

    // **見出しのラベルは持っていません**（Phase 71で外しました）。
    // 「Console」はパネルのヘッダー（`EditorPanel`）に出ているので、
    // 中にもう一度出すと、その40pxぶんストリップが縮むだけでした（8.32）
    juce::Label emptyLabel;

    juce::Component stripContainer; // ストリップを横に並べる器（Viewportの中身）
    juce::Viewport viewport;
    juce::OwnedArray<ChannelStripComponent> strips;
    MasterStripComponent masterStrip { project, audioEngine };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConsoleView)
};
