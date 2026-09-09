#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "LevelMeterComponent.h"
#include "ValueEntrySlider.h"   // Phase 61：ダブルクリックでの数値入力（8.1のC2）
#include "TrackRackComponent.h"

class AudioEngine;

//==============================================================================
/**
    仕様書5.7・設計書2.3.2「コンソール（ミキサー）ビュー」のチャンネルストリップ1本ぶん。

    表示・操作するのはトラックの音量／パン／ミュート／ソロで、値の実体は
    ProjectModel（ValueTree）側にある。このコンポーネント自身は状態を持たない
    （設計書1.2の「UI側に状態を持たせない」原則）。

    Phase 12bでレベルメーター、Phase 12c-2でインサートスロット、
    Phase 13でセンド（仕様書5.2.2）、Phase 14で音源スロット（仕様書5.3）を追加した。

    **Phase 29で、音源・インサート・センド・VCA・書き込みモードを
    `TrackRackComponent`へ切り出した。** インスペクタパネルからも同じ操作を
    できるようにするためで、このストリップは「フェーダーまわり＋ラックを置く場所」になった。
    ラックの中身を触りたいときは`TrackRackComponent`を見ること。
*/
class ChannelStripComponent : public juce::Component,
                               public juce::DragAndDropTarget,
                               private juce::ValueTree::Listener
{
public:
    ChannelStripComponent (const Track& trackToControl, ProjectModel& projectToUse, AudioEngine& audioEngineToUse);
    ~ChannelStripComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 音量・パン・ミュート／ソロが操作されたときに呼ばれる（エンジンへの反映用）。 */
    std::function<void()> onMixerValueChanged;

    /** 仕様書5.7：メーターに表示するレベルを外から与える（ConsoleViewがタイマーで更新する）。 */
    void setLevels (float leftLevel, float rightLevel) { meter.setLevels (leftLevel, rightLevel); }

    /** このストリップが担当しているトラックのID（メーターの値をエンジンから引くのに使う）。 */
    juce::String getTrackId() const { return track.getId(); }

    /** 8.54：**再生位置を受け取って、フェーダーをそこの値に合わせる**（Phase 93）。

        オートメーションはモデルの値を書き換えないので、**画面が自分で引きに行く**
        必要がある（`Track::getEffectiveVolumeDbAt()`）。配るのは
        `MainComponent::setPlayheadDisplay()`1箇所（8.33と同じ形）。 */
    void setPlayheadSeconds (double seconds);

    /** ストリップ1本の標準幅。ConsoleView側のレイアウト計算に使う。 */
    static constexpr int stripWidth = 104;

    /** 仕様書5.7：インサートスロットの表示を作り直す（サイドチェインのUndo/Redo追従用）。 */
    void refreshInsertSlots() { rack.refreshInsertSlots(); }

    /** 仕様書5.2.4：VCAの割り当て表示を更新する（Phase 12d-2）。

        リンク情報はVCAトラック側のValueTreeに入っているため、
        **リンクされる側のストリップは自分の購読では変化に気づけない。**
        割り当てが変わったら、ConsoleViewが全ストリップに対してこれを呼ぶ。 */
    void refreshVcaAssignment() { rack.refreshVcaAssignment(); }

    /** 仕様書5.7.1：このトラックのプラグインが持つレイテンシの表示を更新する（Phase 12e）。

        **どのトラックが全体の補正量を押し上げているか**を見るための表示なので、
        0のトラックには何も出さない（並んだときに情報量が増えすぎるため）。
        ConsoleViewがメーターと同じタイマーで呼ぶ。 */
    void refreshLatencyDisplay();

    //==========================================================================
    // 仕様書4.4・6章：ブラウザからのドラッグ&ドロップの受け口（Phase 21）。
    //
    // **落とし先はストリップ全体**にしてある（ラックの範囲だけに狭めると、
    // フェーダーの上へ落としたときに何も起きず、理由も分からないため）。
    // 判定と実行そのものはTrackRackComponentの静的関数に集約している。

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    //==========================================================================
    /** 8.67：**空いているところを掴んでトラックを並べ替える**（Phase 106／改善案㉛）。

        Consoleでミックスしている最中に並びを変えたくなったとき、
        それまではアレンジ画面まで戻る必要がありました（8.60の「+ Track」と同じ話）。

        **掴めるのはストリップの地とトラック名**です。フェーダー・つまみ・ボタン・
        ラックはそれぞれ自分でマウスを受けるので、ここへは落ちてきません
        （＝**操作の邪魔をしない**）。トラック名は`setInterceptsMouseClicks(false)`に
        してあり、名前を掴む＝そのトラックを掴む、になります。

        受け口は`ConsoleView`（並びを知っているのはあちら）。 */
    void mouseDrag (const juce::MouseEvent& e) override;

    /** 8.162：**トラック名をダブルクリックで書き換える**（Phase 200／本人の要望）。

        名前のラベルはマウスを受けない（並べ替えのために透明にしてある）ので、
        **ダブルクリックはストリップの地へ落ちてきます。**
        押した場所が名前の上かどうかは、ここで見ます。 */
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    void updateControlsFromModel();

    /** 8.162：名前の板をトラックの色で塗り直す（Phase 200）。

        **色は`updateControlsFromModel()`から呼んで合わせること**——
        インスペクタで色を変えたときも、ここへ届くのはモデルの変更通知だけです（1.15）。 */
    void updateNameColours();

    /** 8.162：入力欄で確定した名前をモデルへ書く（Phase 200）。 */
    void commitNameEdit();

    /** 8.115：そのパラメータをオートメーションのレーンが握っているか（Phase 150）。

        **点が1つ以上あって、バイパスされていない**ときだけ真です。
        レーンを作っただけ（点が0本）では握っていません。 */
    bool isParameterAutomated (const juce::String& targetId) const;
    void notifyChanged();

    // 他の場所（ソロによる表示変化など）でモデルが変わったときも表示を合わせる
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    Track track;
    ProjectModel& project;
    AudioEngine& audioEngine;

    /** 8.54：いまの再生位置（Phase 93）。フェーダーに出す値を決めるのに使う。
        **停止中も意味を持つ**（止めた場所のオートメーション値が出る）。 */
    double playheadSeconds = 0.0;

    juce::Label nameLabel;
    // Phase 61：ダブルクリックで数値入力（8.1のC2）。Alt＋クリックで初期値に戻る
    ValueEntrySlider panSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    /** 8.63：パンの数値（Phase 101／改善案⑦）。

        **dB表示と同じ扱い**：クリックで打ち込み、右クリックでつまみと同じメニュー。
        ノブは小さく、つまみの上では打ち込めないため（8.22と同じ理由）。 */
    ValueReadoutLabel panReadout;
    ValueEntrySlider volumeSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    /** Phase 63：右クリックで数値入力、ダブルクリックで初期値（8.24）。 */
    ValueReadoutLabel volumeValueLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    LevelMeterComponent meter;

    // 仕様書5.3/5.7/5.2.2/5.2.4/5.6：音源・インサート・センド・VCA・書き込みモード（Phase 29）
    TrackRackComponent rack;

    /** ラックを入れる枠（Phase 66／8.1のC14）。

        **インサートやセンドが増えると、ラックが縦を食い尽くしていました。**
        フェーダーとメーターは残りをもらう作りなので、スロットを4つも挿すと
        メーターが数ピクセルまで縮み、何も読めなくなります。

        ラックへ渡す高さに上限を設け、**入り切らないぶんはスクロールで届く**
        ようにしてあります。Consoleを下部パネルへ移した（Phase 66）ことで
        縦がさらに限られるため、ここを直さないと使えません。 */
    juce::Viewport rackViewport;

    /** フェーダーとメーターに必ず残す高さ。**ここを下回らせない**のが目的なので、
        「メーターの目盛りが読める最小」から決めてある。 */
    static constexpr int minimumFaderAreaHeight = 130;

    //==========================================================================
    /** VCAトラックのストリップか。設計書2.3.2にならい、フェーダー・ミュート・ソロ以外
        （パン・メーター）を持たない簡易表示にする。 */
    bool isVca = false;

    // 仕様書5.7.1：このトラックのプラグインが持つレイテンシ（Phase 12e）。
    // 0のときは非表示にして場所も取らない。
    juce::Label latencyLabel;
    int lastShownLatencySamples = -1; // -1＝未表示。無駄な描き直しを避けるために持つ

    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    // 仕様書4.4：ドラッグ中のプラグインがこのストリップの上にあるか（Phase 21）
    bool isDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripComponent)
};
