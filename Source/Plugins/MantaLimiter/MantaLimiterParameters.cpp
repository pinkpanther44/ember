#include "MantaLimiterParameters.h"

#include <cmath>

namespace MantaLimiterParams
{
    namespace
    {
        /** 対数の目盛り（Manta Compと同じ作り。端の値がぴったり出る）。 */
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

        juce::String formatDb (float value, int)      { return juce::String (value, 1) + " dB"; }
        juce::String formatPercent (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; }

        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto addFloat = [&layout] (const char* id, const juce::String& name, juce::NormalisableRange<float> range,
                                   float defaultValue, auto format, bool automatable = true)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (format)
                                                     .withAutomatable (automatable)));
        };

        auto addBool = [&layout] (const char* id, const juce::String& name, bool defaultValue, bool automatable = true)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, defaultValue,
                juce::AudioParameterBoolAttributes().withAutomatable (automatable)));
        };

        auto addChoice = [&layout] (const char* id, const juce::String& name, const juce::StringArray& choices,
                                    int defaultIndex, bool automatable = true)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id, 1 }, name, choices, defaultIndex,
                juce::AudioParameterChoiceAttributes().withAutomatable (automatable)));
        };

        // **並びは設計書のIDの順**（9.5：オートメーションは番号で覚える）
        addFloat (gain, "Gain", { 0.0f, maxGainDb, 0.1f }, 0.0f, formatDb);
        addFloat (output, "Output Level", { minOutputDb, 0.0f, 0.1f }, -1.0f, formatDb);
        addChoice (style, "Style", styleNames(), 0);
        addFloat (lookahead, "Lookahead", { minLookaheadMs, maxLookaheadMs, 0.1f }, 1.0f, formatMs, false);
        addFloat (attack, "Attack", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPercent);
        addFloat (release, "Release", makeLogRange (1.0f, 1000.0f), 100.0f, formatMs);
        addBool (autoRelease, "Auto Release", true);
        addFloat (link, "Channel Link", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPercent);
        addChoice (oversampling, "Oversampling", oversamplingNames(), 0, false);
        addBool (truePeak, "True Peak", true, false);
        addBool (unity, "Unity Gain", false, false);
        addBool (audition, "Audition", false, false);
        addBool (dcFilter, "DC Filter", false);
        addChoice (dither, "Dither", ditherNames(), 0, false);
        addChoice (noiseShaping, "Noise Shaping", noiseShapingNames(), 0, false);

        return layout;
    }
}
