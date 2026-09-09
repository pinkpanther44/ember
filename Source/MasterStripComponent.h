#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "LevelMeterComponent.h"
#include "ValueEntrySlider.h"   // Phase 61：ダブルクリックでの数値入力（8.1のC2）
#include "TrackRackComponent.h" // 8.69：マスターへのインサート（Phase 108/D6）

class AudioEngine;

//==============================================================================
/**
    仕様書5.7「マスターチャンネル」のチャンネルストリップ。

    通常のトラックと違い、パン・ミュート・ソロは持たない（音を集約して出すだけの
    最終段のため）。値の実体はProjectModelの`<MASTERBUS>`にある。

    8.69：**マスターへのインサート**（Phase 108/D6。設計書1.4の`MASTERBUS/INSERTS`）。
    ラックの中身はトラックと同じ`TrackRackComponent`で、
    **インサートだけを出す形**で作っています（`createForMasterBus()`）。
*/
class MasterStripComponent : public juce::Component,
                              public juce::DragAndDropTarget,
                              private juce::ValueTree::Listener
{
public:
    MasterStripComponent (ProjectModel& projectToUse, AudioEngine& audioEngineToUse);
    ~MasterStripComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setLevels (float leftLevel, float rightLevel) { meter.setLevels (leftLevel, rightLevel); }

    /** 仕様書5.7.1：現在の補正量（PDC）の表示を更新する。
        ConsoleViewがメーターと同じタイマーで呼ぶ。値が変わったときだけ描き直す。 */
    void refreshLatencyDisplay();

    /** 仕様書5.1：プロジェクトが差し替わったら、新しい`<MASTERBUS>`へ購読し直す。
        これを忘れると、読み込み後も古いツリーを掴んだままになり、
        フェーダーを動かしても現在のプロジェクトに反映されない。 */
    void refreshAfterProjectChanged();

    /** マスター音量が操作されたときに呼ばれる（エンジンへの反映用）。 */
    std::function<void()> onMixerValueChanged;

    /** 8.54：再生位置を受け取って、フェーダーをそこの値に合わせる（Phase 93）。
        トラックのストリップと同じ形（`ChannelStripComponent::setPlayheadSeconds`）。 */
    void setPlayheadSeconds (double seconds);

    static constexpr int stripWidth = 110;

    //==========================================================================
    /** 8.69：**ブラウザからマスターへプラグインを落とす**（Phase 108／D6。仕様書4.4）。

        落とし先はストリップ全体です（トラックのストリップと同じ扱い。
        `ChannelStripComponent`の説明を読むこと）。

        **音源は受け取りません。** マスターは最終段で、MIDIを受ける道がありません。 */
    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

private:
    void updateControlsFromModel();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    ProjectModel& project;
    AudioEngine& audioEngine;
    juce::ValueTree masterBusState;

    /** 8.54：いまの再生位置（Phase 93）。 */
    double playheadSeconds = 0.0;

    juce::Label nameLabel;
    ValueEntrySlider volumeSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    /** Phase 63：右クリックで数値入力、ダブルクリックで初期値（8.24）。 */
    ValueReadoutLabel volumeValueLabel;
    LevelMeterComponent meter;

    // 仕様書5.6：マスターのオートメーション書き込みモード（Phase 20）
    juce::ComboBox automationModeBox;

    /** 8.69：マスターのインサート（Phase 108／D6）。

        **プロジェクトを開き直すと作り直します**：`<MASTERBUS>`のノードごと
        差し替わるので、古いツリーを掴んだままのラックは
        フェーダーと同じく現在のプロジェクトを見なくなる（1.15）。 */
    std::unique_ptr<TrackRackComponent> rack;

    /** ラックを入れる枠。スロットが増えてもフェーダーとメーターを潰さないため
        （`ChannelStripComponent::rackViewport`と同じ作り。8.1のC14）。 */
    juce::Viewport rackViewport;

    /** フェーダーとメーターに必ず残す高さ（チャンネルストリップと同じ値）。 */
    static constexpr int minimumFaderAreaHeight = 130;

    /** ラックを今の`<MASTERBUS>`に合わせて作り直す。 */
    void rebuildRack();

    // 仕様書5.7.1：PDCの補正量（Phase 12e）。0のときも「PDC 0.0 ms」と出す。
    // 「表示が無い＝機能が無い」と読めてしまうより、0だと分かるほうが切り分けしやすい。
    juce::Label latencyLabel;
    int lastShownLatencySamples = -1; // -1＝未表示。無駄な描き直しを避けるために持つ

    bool isUpdatingFromModel = false;

    /** 8.69：ドラッグ中のプラグインがこのストリップの上にあるか（Phase 108／D6）。 */
    bool isDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterStripComponent)
};
