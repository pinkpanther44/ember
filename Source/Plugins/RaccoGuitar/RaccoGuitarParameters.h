#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    8.256：**Racco Guitar のパラメータ**（Phase 264）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。
    **足すときは、いちばん末尾へ。**

    ### `KSGuitar`から持って来なかったもの

    元（別リポジトリの`KSGuitar`）には**アンプ段が4つ**ありました
    （`ampOn` / `drive` / `tone` / `ampLevel`）。**本人の指定で載せていません**
    ——アンプとキャビネットは外のプラグインでやる、という切り分けです。

    IDと範囲は残した7つを**そのまま写して**あります。

    ### 奏法はパラメータではありません

    キースイッチ（A#1〜D#2）で切り替わるもので、**MIDIで決まる状態**です。
    `RaccoGuitarProcessor::currentArticulation`（`std::atomic<int>`）が持ち、
    プロジェクトには`apvts.state`の`articulation`プロパティとして保存されます。

    > **パラメータにしなかった理由**：キースイッチは音のスレッドから来ます。
    > そこで`setValueNotifyingHost()`を呼ぶと、ホストへの通知が音のスレッドから
    > 出ることになります。**画面のチップから押したときだけ**値が変わる形なら、
    > 通知の要る道が1本も要りません。
*/
namespace RaccoGuitarParams
{
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* sustain    = "sustain";
    inline constexpr const char* pickPos    = "pickPos";
    inline constexpr const char* pickupSel  = "pickupSel";
    inline constexpr const char* hardness   = "hardness";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* gain       = "gain";

    /** ピックアップの選択肢。**画面と音の両方がここを見ます**（1.27）。 */
    juce::StringArray getPickupNames();

    /** 選択（0＝Front／1＝Center／2＝Rear）を、弦長に対するブリッジからの割合へ。

        実機のピックアップ位置に対応します：Front（ネック側）＝0.35 /
        Center＝0.22 / Rear（ブリッジ側）＝0.10。 */
    float pickupPosition (int selection);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
