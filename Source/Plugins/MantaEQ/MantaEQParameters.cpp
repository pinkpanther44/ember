#include "MantaEQParameters.h"

#include "LinearPhaseEngine.h"   // 処理解像度の選択肢（数字はエンジンが持つ。1.27）

#include <cmath>

namespace MantaEQParams
{
    namespace
    {
        /** 対数の目盛りを作る。

            `juce::NormalisableRange`のskewでも近い形にはできますが、
            **端の値がぴったり出ない**ので、変換そのものを書いています
            （20Hzが0、20kHzが1、1kHzがほぼ真ん中）。 */
        juce::NormalisableRange<float> makeLogRange (float minimum, float maximum)
        {
            const float logMin = std::log (minimum);
            const float logMax = std::log (maximum);

            return
            {
                minimum, maximum,
                [logMin, logMax] (float start, float end, float proportion)
                {
                    juce::ignoreUnused (start, end);
                    return std::exp (logMin + proportion * (logMax - logMin));
                },
                [logMin, logMax] (float start, float end, float value)
                {
                    juce::ignoreUnused (start, end);
                    return (std::log (juce::jmax (value, 1.0e-6f)) - logMin) / (logMax - logMin);
                },
                [] (float start, float end, float value)
                {
                    return juce::jlimit (start, end, value);
                }
            };
        }

        /** 周波数。**小数点以下は出しません**（Phase 208／本人の要望）。

            Phase 207までは1kHz以上を"3.00 kHz"、100Hz未満を"63.5 Hz"と出していました。
            **どちらも読み取りの役に立っていません**——EQで合わせるのは
            「だいたいこのあたり」で、0.5Hzの差を目で追うことはないためです。

            kHz表記もやめて**Hzで通し**ます。"3 kHz"にすると3150Hzが同じ表示になり、
            **打ち込んだ値と出てくる値が違う**（メニューの中で打てるようになったので、
            そこが噛み合わないと気持ち悪い）。 */
        juce::String formatHz (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + " Hz";
        }

        juce::String formatDb (float value, int)
        {
            return juce::String (value, 1) + " dB";
        }

        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 2 : 1) + " ms";
        }

        /** -1〜+1 を「L50 / C / R50」で見せる。

            **つまみの数字は小数で出しても手では合わせられません**
            （本体のパンと同じ考え方。`ValueEntrySlider::DisplayUnit::panPercent`）。 */
        juce::String formatPan (float value, int)
        {
            const int percent = juce::roundToInt (std::abs (value) * 100.0f);

            if (percent == 0)
                return "C";

            return (value < 0.0f ? "L" : "R") + juce::String (percent);
        }

        /** -1（Midのみ）〜+1（Sideのみ）を「M50 / 0 / S50」で見せる。 */
        juce::String formatMidSide (float value, int)
        {
            const int percent = juce::roundToInt (std::abs (value) * 100.0f);

            if (percent == 0)
                return "0";

            return (value < 0.0f ? "M" : "S") + juce::String (percent);
        }
    }

    //==========================================================================

    juce::StringArray getShapeNames()
    {
        // **画面に出す名前ですが、訳しません。** `juce::AudioParameterChoice`は
        // 選択肢の**文字列**も状態に書き出すので、言語を変えると
        // 保存済みのプロジェクトで読めなくなります。
        // 他のDAWのEQとも用語が揃います（9.7の表の「ラベルは英語」と同じ考え）
        return { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                 "Notch", "Band Pass", "Tilt Shelf", "All Pass" };
    }

    juce::StringArray getChannelNames()
    {
        return { "Stereo", "Left", "Right", "Mid", "Side" };
    }

    juce::StringArray getSlopeNames()
    {
        juce::StringArray names;

        for (int i = 0; i < numSlopeChoices; ++i)
            names.add (juce::String (slopeChoicesDbPerOctave[i]) + " dB/oct");

        return names;
    }

    juce::StringArray getProcessingModeNames()
    {
        return { "Zero Latency", "Linear Phase" };
    }

    juce::StringArray getResolutionNames()
    {
        // **数字も出すこと。** レイテンシーはこの数字そのもの（`LinearPhaseEngine.h`）
        juce::StringArray names;

        static const char* const labels[] { "Low", "Medium", "High", "Very High" };

        for (int i = 0; i < LinearPhaseEngine::numResolutionChoices; ++i)
            names.add (juce::String (labels[i]) + " ("
                        + juce::String (LinearPhaseEngine::resolutionChoices[i]) + ")");

        return names;
    }

    bool shapeUsesGain (Shape shape)
    {
        return shape == Shape::bell || shape == Shape::lowShelf
                || shape == Shape::highShelf || shape == Shape::tiltShelf;
    }

    bool shapeUsesSlope (Shape shape)
    {
        return shape == Shape::lowCut || shape == Shape::highCut;
    }

    juce::String bandParamId (int bandIndex, const char* suffix)
    {
        return "band" + juce::String (bandIndex + 1) + "_" + suffix;
    }

    float getDefaultFrequency (int bandIndex)
    {
        // 1/3オクターブの標準的な刻みから12個。**バンドを増やしても重ならない**
        static constexpr float defaults[numBands]
        {
            60.0f, 100.0f, 160.0f, 250.0f, 400.0f, 630.0f,
            1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f, 10000.0f
        };

        return defaults[juce::jlimit (0, numBands - 1, bandIndex)];
    }

    float frequencyToProportion (float hz)
    {
        const float logMin = std::log (minFrequency);
        const float logMax = std::log (maxFrequency);

        return juce::jlimit (0.0f, 1.0f,
                              (std::log (juce::jmax (hz, 1.0e-3f)) - logMin) / (logMax - logMin));
    }

    float proportionToFrequency (float proportion)
    {
        const float logMin = std::log (minFrequency);
        const float logMax = std::log (maxFrequency);

        return std::exp (logMin + juce::jlimit (0.0f, 1.0f, proportion) * (logMax - logMin));
    }

    //==========================================================================

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto freqRange    = makeLogRange (minFrequency, maxFrequency);
        const auto qRange       = makeLogRange (minQ, maxQ);
        const auto attackRange  = makeLogRange (0.1f, 200.0f);
        const auto releaseRange = makeLogRange (5.0f, 2000.0f);
        const juce::NormalisableRange<float> gainRange { -maxGainDb, maxGainDb, 0.01f };
        const juce::NormalisableRange<float> threshRange { -60.0f, 0.0f, 0.1f };

        // **バンドごとにまとめて並べる。** 番号順に読むとそのまま
        // 「Band 1のFreq、Gain、Q……」になるので、オートメーションの
        // 選択メニュー（仕様書5.6）が読めるものになります
        for (int band = 0; band < numBands; ++band)
        {
            const auto prefix = "Band " + juce::String (band + 1) + " ";
            const int version = 1;

            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { bandParamId (band, bandEnabled), version },
                prefix + "On", false));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandFreq), version },
                prefix + "Freq", freqRange, getDefaultFrequency (band),
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatHz)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandGain), version },
                prefix + "Gain", gainRange, 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDb)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandQ), version },
                prefix + "Q", qRange, 0.707f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                    [] (float value, int) { return juce::String (value, value < 10.0f ? 3 : 1); })));

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (band, bandShape), version },
                prefix + "Shape", getShapeNames(), (int) Shape::bell));

            // 既定は24 dB/oct（choicesの添字3）。カット以外では使われない
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (band, bandSlope), version },
                prefix + "Slope", getSlopeNames(), 3));

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { bandParamId (band, bandChannel), version },
                prefix + "Channel", getChannelNames(), (int) Channel::stereo));

            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { bandParamId (band, bandDynOn), version },
                prefix + "Dynamic", false));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandDynThresh), version },
                prefix + "Dyn Threshold", threshRange, -20.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDb)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandDynRange), version },
                prefix + "Dyn Range", gainRange, 0.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDb)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandDynAttack), version },
                prefix + "Dyn Attack", attackRange, 20.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatMs)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { bandParamId (band, bandDynRelease), version },
                prefix + "Dyn Release", releaseRange, 200.0f,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatMs)));
        }

        // 仕様書4.16：出力セクション。**バンドより後ろ**（足すときはさらに後ろへ）
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { outputGain, 1 }, "Output Gain", gainRange, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatDb)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { outputPan, 1 }, "Output Pan",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatPan)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { outputMsBalance, 1 }, "Mid/Side Balance",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (formatMidSide)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { outputPhaseInvert, 1 }, "Phase Invert", false));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { outputAutoGain, 1 }, "Auto Gain", false));

        // 仕様書4.7：処理モード（Phase 205）。**いちばん末尾へ足すこと**——
        // ここより前へ入れると、Phase 204で保存したオートメーションが1つずつずれます
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { processingMode, 1 }, "Processing Mode",
            getProcessingModeNames(), (int) ProcessingMode::zeroLatency));

        // 既定は「High (4096)」＝48kHzで85ms。
        // **短くするほど低域が甘くなります**（`LinearPhaseEngine.h`）
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { processingResolution, 1 }, "Linear Phase Resolution",
            getResolutionNames(), 2));

        // Phase 206：バンドのバイパス。
        //
        // **バンドごとにまとめず、いちばん末尾へ12本並べています。**
        // バンドの並びの中へ割り込ませると、Phase 204・205で保存された
        // オートメーションが**バンド1つぶんずつずれます**（9.5「足すときは末尾へ」）。
        for (int band = 0; band < numBands; ++band)
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { bandParamId (band, bandActive), 1 },
                "Band " + juce::String (band + 1) + " Active", true));

        return layout;
    }
}
