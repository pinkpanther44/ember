#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Manta Comp のパラメータ定義（コンプ設計書2章「パラメータID一覧」）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。
    **足すときは、いちばん末尾へ。**

    ### 味付けはしない

    仕様書0章の方針は「アナログモデリング系ではなく、素直でクセのない
    デジタルコンプ」です。**サチュレーションも周波数特性の色付けも入れていません**
    ——`Ratio`を1:1にすれば、`Input Gain`と`Makeup`を除いて素通りになります。
*/
namespace MantaCompParams
{
    // 2-1：Threshold / Ratio / Knee
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* knee      = "knee";

    // 2-4：Envelope
    inline constexpr const char* attack          = "attack";
    inline constexpr const char* release         = "release";
    inline constexpr const char* autoEnvelope    = "autoEnvelope";
    inline constexpr const char* adaptiveRelease = "adaptiveRelease";

    // 2-5：Gain
    inline constexpr const char* inputGain  = "inputGain";
    inline constexpr const char* makeupGain = "makeupGain";
    inline constexpr const char* autoGain   = "autoGain";

    // 2-3：Look Ahead / Stereo Link
    inline constexpr const char* lookAhead  = "lookAhead";
    inline constexpr const char* stereoLink = "stereoLink";

    // 2-6：Sidechain
    inline constexpr const char* scFilterOn = "scFilterOn";
    inline constexpr const char* scLowCut   = "scLowCut";
    inline constexpr const char* scHighCut  = "scHighCut";
    inline constexpr const char* scListen   = "scListen";

    // 2-7：Global
    inline constexpr const char* mix = "mix";

    //==========================================================================
    /** 先読みの長さ（仕様書2-3では「内部固定で数ms、例：5ms」）。

        **レイテンシーはこの値そのもの**です（`setLatencySamples()`で申告）。
        長くすると急なピークによく追従しますが、そのぶん遅れます。 */
    inline constexpr float lookAheadMs = 5.0f;

    /** つまみで触れる範囲。**画面と音の両方がここを見ます**（1.27）。 */
    inline constexpr float minThresholdDb = -60.0f;
    inline constexpr float maxThresholdDb = 0.0f;
    inline constexpr float minRatio = 1.0f;
    inline constexpr float maxRatio = 20.0f;
    inline constexpr float maxKneeDb = 24.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
