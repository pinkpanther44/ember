#include "JavaRhinoBassParameters.h"

namespace JavaRhinoBassParams
{
    juce::StringArray getStyleNames()
    {
        return { "Finger", "Pick", "Slap", "Mute", "Ghost", "Harmonic" };
    }

    BassStyle styleFromChoice (int choice)
    {
        // **`getStyleNames()`と同じ並び**であること（1.27）
        static const BassStyle map[numBassStyles]
        {
            BassStyle::Finger,
            BassStyle::Pick,
            BassStyle::Slap,
            BassStyle::Mute,
            BassStyle::Ghost,
            BassStyle::Harmonic,
        };

        return map[juce::jlimit (0, numBassStyles - 1, choice)];
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Float = juce::AudioParameterFloat;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { style, 1 }, "Style", getStyleNames(), 0));

        //----------------------------------------------------------------------
        // 弦の音色
        layout.add (std::make_unique<Float> (
            juce::ParameterID { brightness, 1 }, "Brightness",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.62f));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { sustain, 1 }, "Sustain",
            juce::NormalisableRange<float> (0.5f, 15.0f, 0.01f, 0.5f), 9.0f,
            juce::AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { pluckPos, 1 }, "Pluck Position",
            juce::NormalisableRange<float> (0.05f, 0.45f, 0.001f), 0.22f));

        //----------------------------------------------------------------------
        // 弾き方
        layout.add (std::make_unique<Float> (
            juce::ParameterID { hardness, 1 }, "Hardness",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.60f));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { attack, 1 }, "Attack",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.70f));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { clank, 1 }, "Clank",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f));

        //----------------------------------------------------------------------
        // 出口
        layout.add (std::make_unique<Float> (
            juce::ParameterID { blend, 1 }, "Blend",
            juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.50f));

        layout.add (std::make_unique<Float> (
            juce::ParameterID { tone, 1 }, "Tone",
            juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 7.0f));

        // **既定は0.35**（`JBass5`は0.80）。
        //
        // あちらは**後ろに常時コンプとアンプ段**がありました。それを持って来ていないので、
        // 弦の素の大きさがそのまま出ます——**実測で峰が1.6〜2.2**（ベロシティ0.9、
        // いちばん大きいのはG4とスラップ）。0.80のままだと、弾いただけで
        // 0dBFSを超えます。0.35なら峰は0.8前後に収まり、`Racco Guitar`とも釣り合います
        layout.add (std::make_unique<Float> (
            juce::ParameterID { gain, 1 }, "Output",
            juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 0.35f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { legato, 1 }, "Legato", true));

        return layout;
    }
}
