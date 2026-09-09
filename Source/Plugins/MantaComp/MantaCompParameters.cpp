#include "MantaCompParameters.h"

#include <cmath>

namespace MantaCompParams
{
    namespace
    {
        /** 対数の目盛り（Manta EQと同じ作り）。端の値がぴったり出るように、
            skewではなく変換そのものを書いています。 */
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

        juce::String formatDb (float value, int)  { return juce::String (value, 1) + " dB"; }
        juce::String formatHz (float value, int)  { return juce::String (juce::roundToInt (value)) + " Hz"; }
        juce::String formatPercent (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; }

        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 2 : (value < 100.0f ? 1 : 0)) + " ms";
        }

        /** 比率は"4.0:1"の形で出す。**数字だけだと何の値か分からない。** */
        juce::String formatRatio (float value, int)
        {
            if (value >= 19.95f)
                return juce::String::fromUTF8 ("\xe2\x88\x9e") + ":1";   // ∞:1

            return juce::String (value, 1) + ":1";
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

        auto addBool = [&layout] (const char* id, const juce::String& name, bool defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, defaultValue));
        };

        // 2-1：Threshold / Ratio / Knee
        addFloat (threshold, "Threshold", { minThresholdDb, maxThresholdDb, 0.1f }, -20.0f, formatDb);

        // **比率は対数で。** 4:1と5:1の差より、1.5:1と2:1の差のほうが大きく効きます
        addFloat (ratio, "Ratio", makeLogRange (minRatio, maxRatio), 4.0f, formatRatio);
        addFloat (knee, "Knee", { 0.0f, maxKneeDb, 0.1f }, 6.0f, formatDb);

        // 2-4：Envelope
        addFloat (attack, "Attack", makeLogRange (0.1f, 100.0f), 10.0f, formatMs);
        addFloat (release, "Release", makeLogRange (10.0f, 1000.0f), 100.0f, formatMs);
        addBool (autoEnvelope, "Auto Envelope", false);
        addBool (adaptiveRelease, "Adaptive Release", false);

        // 2-5：Gain
        addFloat (inputGain, "Input Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, formatDb);
        addFloat (makeupGain, "Makeup", { -24.0f, 24.0f, 0.1f }, 0.0f, formatDb);
        addBool (autoGain, "Auto Gain", false);

        // 2-3：Look Ahead / Stereo Link
        addBool (lookAhead, "Look Ahead", true);
        addBool (stereoLink, "Stereo Link", true);

        // 2-6：Sidechain
        addBool (scFilterOn, "Sidechain Filter", false);
        addFloat (scLowCut, "SC Low Cut", makeLogRange (20.0f, 2000.0f), 20.0f, formatHz);
        addFloat (scHighCut, "SC High Cut", makeLogRange (200.0f, 20000.0f), 20000.0f, formatHz);
        addBool (scListen, "SC Listen", false);

        // 2-7：Global
        addFloat (mix, "Mix", { 0.0f, 100.0f, 0.1f }, 100.0f, formatPercent);

        return layout;
    }
}
