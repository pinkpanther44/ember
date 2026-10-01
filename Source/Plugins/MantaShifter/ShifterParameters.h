#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice のパラメータ（本人の仕様書2章・設計書（ライト版）4章）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    設計書の表の ID 0〜11 の順そのまま。足すときは末尾へ（`midiHold`は12番）。

    ### すべて自動化できる

    仕様書2章「すべてオートメーション可能」。リミッターと違い、**レイテンシーはどの設定でも同じ**
    （`prepare()`で決まる。設計書5章）なので、自動化から外すものがありません。

    ### `midiHold`（設計書には無い。仕様書3章の選択肢）

    仕様書「MIDIモード：ノートなしの間は原音（**または直前ノート保持を選択可**）」。
    設計書のパラメータ表には載っていなかったので、**末尾に足しました**（既定は原音）。
*/
namespace ShifterParams
{
    inline constexpr const char* pitch    = "pitch";
    inline constexpr const char* formant  = "formant";
    inline constexpr const char* link     = "link";
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* key      = "key";
    inline constexpr const char* scale    = "scale";
    inline constexpr const char* retune   = "retune";
    inline constexpr const char* driveOn  = "driveOn";
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* midiHold = "midiHold";

    /** 選択肢の並び（**保存される番号**。並べ替えないこと）。 */
    enum class Mode { transpose = 0, quantize = 1, robot = 2, midi = 3 };
    enum class Scale { chromatic = 0, major = 1, minor = 2 };

    inline const juce::StringArray& modeNames()
    {
        static const juce::StringArray names { "Transpose", "Quantize", "Robot", "MIDI" };
        return names;
    }

    inline const juce::StringArray& keyNames()
    {
        static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return names;
    }

    inline const juce::StringArray& scaleNames()
    {
        static const juce::StringArray names { "Chromatic", "Major", "Minor" };
        return names;
    }

    /** 範囲。**画面と音の両方がここを見ます**（1.27）。 */
    inline constexpr float maxShiftSemitones = 12.0f;
    inline constexpr float maxRetuneMs = 200.0f;
    inline constexpr float minOutputDb = -24.0f;
    inline constexpr float maxOutputDb = 12.0f;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
