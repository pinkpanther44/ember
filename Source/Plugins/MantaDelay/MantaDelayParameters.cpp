#include "MantaDelayParameters.h"

#include <cmath>

namespace MantaDelayParams
{
    namespace
    {
        /** 対数の目盛り（Manta EQ・Manta Compと同じ作り）。
            端の値がぴったり出るように、skewではなく変換そのものを書いています。 */
        juce::NormalisableRange<float> makeLogRange (float minimum, float maximum)
        {
            const float logMin = std::log (minimum);
            const float logMax = std::log (maximum);

            return
            {
                minimum, maximum,
                [logMin, logMax] (float, float, float proportion)
                {
                    return std::exp (logMin + proportion * (logMax - logMin));
                },
                [logMin, logMax] (float, float, float value)
                {
                    return (std::log (juce::jmax (value, 1.0e-6f)) - logMin) / (logMax - logMin);
                },
                [] (float start, float end, float value) { return juce::jlimit (start, end, value); }
            };
        }

        juce::String formatDb (float value, int) { return juce::String (value, 1) + " dB"; }

        juce::String formatPercent (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        }

        /** ディレイタイム。**桁で小数点を変えます**——
            「1000.00 ms」は読みにくく、「3.00 ms」は3msと区別が要ります */
        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 2 : (value < 100.0f ? 1 : 0)) + " ms";
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto floatAttributes = [] (auto function)
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction (function);
        };

        auto addFloat = [&layout, &floatAttributes] (const char* id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue, auto function)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, floatAttributes (function)));
        };

        //----------------------------------------------------------------------
        // 5-1：基本
        //
        // **並び順を変えないこと**（オートメーションは番号で覚えています。9.5）

        // **対数で配ります。** 20msと40msの差は聞いて分かりますが、
        // 2000msと2020msは同じに聞こえます——線形だと短い側が使いものになりません
        addFloat (timeMs, "Time",
                   makeLogRange ((float) minDelayMs, (float) maxDelayMs), 375.0f, formatMs);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { sync, 1 }, "Sync", true));

        // **`AudioParameterChoice`にすること。** floatで番号を持つと、
        // オートメーションで中間の値が入ったときにどの音価か決まりません
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { syncDivision, 1 }, "Division",
            getSyncDivisionNames(), (int) SyncDivision::eighthDotted));

        addFloat (feedback, "Feedback", { 0.0f, maxFeedback, 0.001f }, 0.35f, formatPercent);
        addFloat (mix, "Mix", { 0.0f, 1.0f, 0.001f }, 0.30f, formatPercent);

        addFloat (outputGain, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb);

        return layout;
    }
}
