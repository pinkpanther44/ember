#include "ShifterParameters.h"

namespace ShifterParams
{
    namespace
    {
        juce::String formatSemitones (float value, int)
        {
            // 0.01半音刻み（仕様書）。0 のときは符号を付けない
            if (std::abs (value) < 0.005f)
                return "0.00 st";

            return (value > 0.0f ? "+" : "") + juce::String (value, 2) + " st";
        }

        juce::String formatDb (float value, int)      { return juce::String (value, 1) + " dB"; }
        juce::String formatPercent (float value, int) { return juce::String (juce::roundToInt (value)) + " %"; }
        juce::String formatMs (float value, int)      { return juce::String (juce::roundToInt (value)) + " ms"; }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto addFloat = [&layout] (const char* id, const juce::String& name, juce::NormalisableRange<float> range,
                                   float defaultValue, auto format)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (format)));
        };

        auto addBool = [&layout] (const char* id, const juce::String& name, bool defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue));
        };

        auto addChoice = [&layout] (const char* id, const juce::String& name, const juce::StringArray& choices, int defaultIndex)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, defaultIndex));
        };

        // **並びは設計書の ID 0〜11 の順**（9.5：オートメーションは番号で覚える）
        addFloat (pitch, "Pitch", { -maxShiftSemitones, maxShiftSemitones, 0.01f }, 0.0f, formatSemitones);
        addFloat (formant, "Formant", { -maxShiftSemitones, maxShiftSemitones, 0.01f }, 0.0f, formatSemitones);
        addBool (link, "Link", false);
        addChoice (mode, "Mode", modeNames(), 0);
        addChoice (key, "Key", keyNames(), 0);
        addChoice (scale, "Scale", scaleNames(), 0);
        addFloat (retune, "Retune Speed", { 0.0f, maxRetuneMs, 1.0f }, 20.0f, formatMs);
        addBool (driveOn, "Drive On", false);
        addFloat (drive, "Drive", { 0.0f, 100.0f, 1.0f }, 0.0f, formatPercent);
        addFloat (mix, "Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPercent);
        addFloat (output, "Output", { minOutputDb, maxOutputDb, 0.1f }, 0.0f, formatDb);
        addBool (bypass, "Bypass", false);

        // 12：設計書に無かったもの（仕様書3章の選択肢。末尾へ）
        addBool (midiHold, "MIDI Hold", false);

        return layout;
    }
}
