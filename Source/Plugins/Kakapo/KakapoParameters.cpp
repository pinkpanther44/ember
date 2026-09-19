#include "KakapoParameters.h"

namespace KakapoParams
{
    juce::StringArray getHoldModeNames()
    {
        // **`NoteHistory::Mode`と同じ並び**（0＝notes／1＝seconds。1.27）
        return { "NOTES", "SECONDS" };
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Float = juce::AudioParameterFloat;
        using Range = juce::NormalisableRange<float>;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { holdMode, 1 }, "Hold Mode", getHoldModeNames(), 0));

        // **刻みは1**。音数のときに3.5音は無いので、秒のほうも整数に揃えてあります
        // （画面の単位が入れ替わるので、片方だけ小数だと読み替えが要ります）
        layout.add (std::make_unique<Float> (
            juce::ParameterID { holdLength, 1 }, "Hold Length",
            Range (1.0f, 32.0f, 1.0f), 16.0f));

        // **既定は0.70。** 内蔵リードは1音しか鳴らないので、峰は0.4前後です
        // （`Racco Guitar`の0.70と同じ値。8.269の測り方で確かめてあります）
        layout.add (std::make_unique<Float> (
            juce::ParameterID { volume, 1 }, "Volume",
            Range (0.0f, 1.5f, 0.001f), 0.70f));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { midiThru, 1 }, "MIDI Thru", false));

        return layout;
    }
}
