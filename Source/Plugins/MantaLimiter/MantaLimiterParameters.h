#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    8.333：Manta Limiter／Af Elephant Limiter のパラメータ（リミッター仕様書「パラメータ仕様」・設計書「パラメータ」）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    足すときは、いちばん末尾へ。IDは設計書の固定文字列そのままです。

    ### 自動化できないもの

    仕様書：**レイテンシーを変えるもの（Lookahead・Oversampling）と試聴用のスイッチ**は自動化の対象外。
    `AudioParameterFloatAttributes::withAutomatable (false)`で名乗らせ、本体のオートメーションの
    メニューに出しません（8.332：`AudioEngine::getPluginParameterNames()`が空の名前で返す）。

    **パラメータのままにしてある**のは、A/B・Undo・プリセットに入れるためです
    （`MantaPluginToolbar`はパラメータの値だけを写します）。
*/
namespace MantaLimiterParams
{
    inline constexpr const char* gain         = "gain";
    inline constexpr const char* output       = "output";
    inline constexpr const char* style        = "style";
    inline constexpr const char* lookahead    = "lookahead";
    inline constexpr const char* attack       = "attack";
    inline constexpr const char* release      = "release";
    inline constexpr const char* autoRelease  = "autoRelease";
    inline constexpr const char* link         = "link";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* truePeak     = "truePeak";
    inline constexpr const char* unity        = "unity";
    inline constexpr const char* audition     = "audition";
    inline constexpr const char* dcFilter     = "dcFilter";
    inline constexpr const char* dither       = "dither";
    inline constexpr const char* noiseShaping = "noiseShaping";

    /** 選択肢の並び（**保存される番号**。並べ替えないこと）。 */
    enum class Style { transparent = 0, punchy = 1, aggressive = 2, safe = 3 };
    enum class Oversampling { off = 0, x2 = 1, x4 = 2, x8 = 3 };
    enum class Dither { off = 0, bits16 = 1, bits20 = 2, bits24 = 3 };
    enum class NoiseShaping { off = 0, light = 1 };

    inline const juce::StringArray& styleNames()
    {
        static const juce::StringArray names { "Transparent", "Punchy", "Aggressive", "Safe" };
        return names;
    }

    inline const juce::StringArray& oversamplingNames()
    {
        static const juce::StringArray names { "Off", "2x", "4x", "8x" };
        return names;
    }

    inline const juce::StringArray& ditherNames()
    {
        static const juce::StringArray names { "Off", "16 bit", "20 bit", "24 bit" };
        return names;
    }

    inline const juce::StringArray& noiseShapingNames()
    {
        static const juce::StringArray names { "Off", "Light" };
        return names;
    }

    /** つまみで触れる範囲。**画面と音の両方がここを見ます**（1.27）。 */
    inline constexpr float maxGainDb = 30.0f;
    inline constexpr float minOutputDb = -30.0f;
    inline constexpr float minLookaheadMs = 0.1f;
    inline constexpr float maxLookaheadMs = 5.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
