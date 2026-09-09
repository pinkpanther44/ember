#include "MantaSynthParameters.h"

#include <cmath>

namespace MantaSynthParams
{
    namespace
    {
        //----------------------------------------------------------------------
        // 表示の作り方。
        //
        // EAGLE type0のつまみは**数値を出していませんでした**（感覚で回す作り）。
        // Manta Synthesizerも見た目は同じですが、**掴んでいるあいだだけ吹き出しに出す**
        // ようにしてあります（`MantaSynthEditor`の`setPopupDisplayEnabled()`）。
        // そのため、ここで単位まで作っておく必要があります——
        // 「0.35」とだけ出ても、それが秒なのか割合なのか分かりません。

        juce::String formatPercent (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        }

        juce::String formatCents (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + " cent";
        }

        juce::String formatHz (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + " Hz";
        }

        juce::String formatLfoHz (float value, int)
        {
            return juce::String (value, 2) + " Hz";
        }

        juce::String formatDb (float value, int)
        {
            return juce::String (value, 1) + " dB";
        }

        /** 秒で持っている時間を、**読みやすい単位に切り替えて**出す。
            0.005秒を「0.01 s」と出すと、動かしても数字が変わりません。 */
        juce::String formatSeconds (float value, int)
        {
            if (value < 1.0f)
                return juce::String (value * 1000.0f, value < 0.1f ? 1 : 0) + " ms";

            return juce::String (value, 2) + " s";
        }

        juce::String formatGlide (float value, int)
        {
            if (value <= 0.0005f)
                return "Off";

            return formatSeconds (value, 0);
        }

        juce::String formatDrive (float value, int)
        {
            return juce::String (value, 1) + " x";
        }

        juce::String formatOctave (int value)
        {
            return (value > 0 ? "+" : "") + juce::String (value) + " Oct";
        }

        /** マトリクスの量は**符号が要る**（-100%と+100%は逆向きに効く）。 */
        juce::String formatSignedPercent (float value, int)
        {
            const int percent = juce::roundToInt (value * 100.0f);
            return (percent > 0 ? "+" : "") + juce::String (percent) + " %";
        }
    }

    //==========================================================================

    juce::String matrixSource (int slot)      { return "mtxSrc" + juce::String (slot); }
    juce::String matrixDestination (int slot) { return "mtxDst" + juce::String (slot); }
    juce::String matrixAmount (int slot)      { return "mtxAmt" + juce::String (slot); }

    juce::StringArray getWaveNames()          { return { "Saw", "Square", "Triangle" }; }
    juce::StringArray getSubWaveNames()       { return { "Sine", "Square" }; }
    juce::StringArray getSubOctaveNames()     { return { "-1 Oct", "-2 Oct" }; }
    juce::StringArray getNoiseNames()         { return { "White", "Pink", "Brown", "Blue" }; }
    juce::StringArray getFilterTypeNames()    { return { "LP", "BP", "HP" }; }
    juce::StringArray getLfoDestinationNames(){ return { "Filter", "Pitch", "Amp" }; }

    juce::StringArray getFxTypeNames()
    {
        return { "Off", "Chorus", "Flanger", "Phaser", "Tremolo", "Delay", "Reverb" };
    }

    juce::StringArray getMatrixSourceNames()
    {
        return { "None", "LFO 1", "Mod Env", "Amp Env", "Velocity", "Note", "LFO 2" };
    }

    juce::StringArray getMatrixDestinationNames()
    {
        return { "None", "Pitch", "Cutoff", "Amp", "OSC1 Lvl", "OSC2 Lvl", "OSC1 Sprd", "Detune" };
    }

    //==========================================================================

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

        auto addChoice = [&layout] (const char* id, const juce::String& name,
                                     const juce::StringArray& items, int defaultIndex)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id, 1 }, name, items, defaultIndex));
        };

        auto addInt = [&layout] (const char* id, const juce::String& name,
                                  int minimum, int maximum, int defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { id, 1 }, name, minimum, maximum, defaultValue));
        };

        auto addBool = [&layout] (const char* id, const juce::String& name, bool defaultValue)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, defaultValue));
        };

        const auto waves = getWaveNames();

        //----------------------------------------------------------------------
        // OSC 1
        addChoice (osc1Wave, "OSC1 Wave", waves, 0);
        addInt    (osc1Voices, "OSC1 Unison", 1, 7, 3);
        addFloat  (osc1Detune, "OSC1 Detune", { 0.0f, 50.0f }, 15.0f, formatCents);
        addFloat  (osc1Level, "OSC1 Level", { 0.0f, 1.0f }, 0.8f, formatPercent);
        addFloat  (osc1Spread, "OSC1 Spread", { 0.0f, 1.0f }, 0.0f, formatPercent);

        // OSC 2
        addChoice (osc2Wave, "OSC2 Wave", waves, 0);
        addInt    (osc2Voices, "OSC2 Unison", 1, 7, 1);
        addFloat  (osc2Detune, "OSC2 Detune", { 0.0f, 50.0f }, 10.0f, formatCents);
        addFloat  (osc2Level, "OSC2 Level", { 0.0f, 1.0f }, 0.0f, formatPercent);
        addFloat  (osc2Spread, "OSC2 Spread", { 0.0f, 1.0f }, 0.0f, formatPercent);

        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { osc2Octave, 1 }, "OSC2 Octave", -2, 2, 0,
            juce::AudioParameterIntAttributes().withStringFromValueFunction (
                [] (int value, int) { return formatOctave (value); })));

        //----------------------------------------------------------------------
        // SUB / NOISE
        addChoice (subWave, "Sub Wave", getSubWaveNames(), 0);
        addChoice (subOctave, "Sub Octave", getSubOctaveNames(), 0);
        addFloat  (subLevel, "Sub Level", { 0.0f, 1.0f }, 0.0f, formatPercent);
        addChoice (noiseType, "Noise Type", getNoiseNames(), 0);
        addFloat  (noiseLevel, "Noise Level", { 0.0f, 1.0f }, 0.0f, formatPercent);

        //----------------------------------------------------------------------
        // FILTER
        addChoice (filterType, "Filter Type", getFilterTypeNames(), 0);

        // **skew 0.3**：低いほうを細かく触れるように（20Hzと200Hzの差は、
        // 10kHzと12kHzの差よりずっと大きく聞こえます）
        addFloat (cutoff, "Cutoff", { 20.0f, 18000.0f, 1.0f, 0.3f }, 8000.0f, formatHz);
        addFloat (resonance, "Resonance", { 0.0f, 1.0f }, 0.3f, formatPercent);

        //----------------------------------------------------------------------
        // AMP ENV
        addFloat (ampA, "Amp Attack", { 0.001f, 3.0f, 0.001f, 0.3f }, 0.005f, formatSeconds);
        addFloat (ampD, "Amp Decay", { 0.001f, 3.0f, 0.001f, 0.3f }, 0.1f, formatSeconds);
        addFloat (ampS, "Amp Sustain", { 0.0f, 1.0f }, 0.8f, formatPercent);
        addFloat (ampR, "Amp Release", { 0.001f, 5.0f, 0.001f, 0.3f }, 0.15f, formatSeconds);

        // MOD ENV
        addFloat (modA, "Mod Attack", { 0.001f, 3.0f, 0.001f, 0.3f }, 0.01f, formatSeconds);
        addFloat (modD, "Mod Decay", { 0.001f, 3.0f, 0.001f, 0.3f }, 0.2f, formatSeconds);
        addFloat (modS, "Mod Sustain", { 0.0f, 1.0f }, 0.0f, formatPercent);
        addFloat (modR, "Mod Release", { 0.001f, 5.0f, 0.001f, 0.3f }, 0.2f, formatSeconds);

        //----------------------------------------------------------------------
        // LFO 1 / LFO 2
        addFloat  (lfoRate, "LFO Rate", { 0.05f, 20.0f, 0.01f, 0.4f }, 5.0f, formatLfoHz);
        addFloat  (lfoDepth, "LFO Depth", { 0.0f, 1.0f }, 0.0f, formatPercent);
        addChoice (lfoDest, "LFO Dest", getLfoDestinationNames(), 0);
        addFloat  (lfo2Rate, "LFO2 Rate", { 0.05f, 20.0f, 0.01f, 0.4f }, 3.0f, formatLfoHz);
        addFloat  (lfo2Depth, "LFO2 Depth", { 0.0f, 1.0f }, 0.0f, formatPercent);
        addChoice (lfo2Dest, "LFO2 Dest", getLfoDestinationNames(), 0);
        addBool   (lfo2On, "LFO2 On", true);
        addFloat  (modEnvCut, "ModEnv>Cutoff", { 0.0f, 1.0f }, 0.0f, formatPercent);

        //----------------------------------------------------------------------
        // DRIVE / MASTER / GLIDE
        addFloat (drive, "Drive", { 1.0f, 20.0f, 0.1f }, 1.0f, formatDrive);
        addFloat (gain, "Gain", { 0.0f, 1.5f }, 0.7f, formatPercent);

        // 0で無効。**低い側を細かく触れるようにskewを付けてある**
        addFloat (glide, "Glide", { 0.0f, 2.0f, 0.001f, 0.35f }, 0.0f, formatGlide);

        //----------------------------------------------------------------------
        // セクションのON/OFF（既定はすべてON）
        addBool (osc1On, "OSC1 On", true);
        addBool (osc2On, "OSC2 On", true);
        addBool (subOn, "Sub On", true);
        addBool (noiseOn, "Noise On", true);
        addBool (filterOn, "Filter On", true);
        addBool (driveOn, "Drive On", true);
        addBool (lfoOn, "LFO On", true);
        addBool (modEnvOn, "Mod Env On", true);
        addBool (fx1On, "FX1 On", true);
        addBool (fx2On, "FX2 On", true);
        addBool (eqOn, "EQ On", true);

        //----------------------------------------------------------------------
        // FX（2スロット）
        const auto fxTypes = getFxTypeNames();

        addChoice (fx1Type, "FX1 Type", fxTypes, 0);
        addFloat  (fx1Rate, "FX1 Rate", { 0.0f, 1.0f }, 0.35f, formatPercent);
        addFloat  (fx1Depth, "FX1 Depth", { 0.0f, 1.0f }, 0.5f, formatPercent);
        addFloat  (fx1Mix, "FX1 Mix", { 0.0f, 1.0f }, 0.3f, formatPercent);
        addFloat  (fx1Fb, "FX1 Feedback", { 0.0f, 1.0f }, 0.3f, formatPercent);
        addChoice (fx2Type, "FX2 Type", fxTypes, 0);
        addFloat  (fx2Rate, "FX2 Rate", { 0.0f, 1.0f }, 0.35f, formatPercent);
        addFloat  (fx2Depth, "FX2 Depth", { 0.0f, 1.0f }, 0.5f, formatPercent);
        addFloat  (fx2Mix, "FX2 Mix", { 0.0f, 1.0f }, 0.3f, formatPercent);
        addFloat  (fx2Fb, "FX2 Feedback", { 0.0f, 1.0f }, 0.3f, formatPercent);

        //----------------------------------------------------------------------
        // EQ（3バンド）
        addFloat (eqLow, "EQ Low", { -15.0f, 15.0f, 0.1f }, 0.0f, formatDb);
        addFloat (eqMid, "EQ Mid", { -15.0f, 15.0f, 0.1f }, 0.0f, formatDb);
        addFloat (eqMidF, "EQ Mid Freq", { 200.0f, 6000.0f, 1.0f, 0.4f }, 1000.0f, formatHz);
        addFloat (eqHigh, "EQ High", { -15.0f, 15.0f, 0.1f }, 0.0f, formatDb);

        //----------------------------------------------------------------------
        // モジュレーション・マトリクス（4スロット × [Src, Dst, Amt]）
        const auto sourceNames = getMatrixSourceNames();
        const auto destinationNames = getMatrixDestinationNames();

        for (int slot = 0; slot < numMatrixSlots; ++slot)
        {
            const auto number = juce::String (slot + 1);

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { matrixSource (slot), 1 },
                "Mtx" + number + " Src", sourceNames, 0));

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { matrixDestination (slot), 1 },
                "Mtx" + number + " Dst", destinationNames, 0));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { matrixAmount (slot), 1 },
                "Mtx" + number + " Amt", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                floatAttributes (formatSignedPercent)));
        }

        return layout;
    }
}
