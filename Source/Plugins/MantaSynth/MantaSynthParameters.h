#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Manta Synthesizer のパラメータ定義（Phase 213）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。
    **足すときは、いちばん末尾へ。**

    ### EAGLE type0から変えたところ

    元は本人が作った **EAGLE type0** です（別リポジトリ）。
    パラメータIDと範囲は**そのまま写して**あります——
    向こうで作った音の設定（`MantaSynthPresets.h`の157個）が、
    IDが同じなのでそのまま使えるためです。

    変えたのは**モジュレーション・マトリクスのスロット数だけ**です。

    | | EAGLE type0 | Manta Synthesizer |
    |---|---|---|
    | マトリクス | 8スロット | **4スロット**（本人の指定） |

    工場プリセット157個のうち、**スロット3以降を使っているものは1つもありません**
    （使っているのは0〜2）。減らしても音は1つも欠けません。

    ### 「セクションのON/OFF」もパラメータ

    OSC1やFILTERの見出しにある縦線のボタンです。
    **オートメーションできること**に意味があります——曲の途中でノイズだけ足す、
    サビでFXを入れる、といった使い方がそのままできます。
*/
namespace MantaSynthParams
{
    // --- OSC 1 ---
    inline constexpr const char* osc1Wave   = "osc1Wave";
    inline constexpr const char* osc1Voices = "osc1Voices";
    inline constexpr const char* osc1Detune = "osc1Detune";
    inline constexpr const char* osc1Level  = "osc1Level";
    inline constexpr const char* osc1Spread = "osc1Spread";

    // --- OSC 2 ---
    inline constexpr const char* osc2Wave   = "osc2Wave";
    inline constexpr const char* osc2Voices = "osc2Voices";
    inline constexpr const char* osc2Detune = "osc2Detune";
    inline constexpr const char* osc2Level  = "osc2Level";
    inline constexpr const char* osc2Spread = "osc2Spread";
    inline constexpr const char* osc2Octave = "osc2Octave";

    // --- SUB / NOISE ---
    inline constexpr const char* subWave    = "subWave";
    inline constexpr const char* subOctave  = "subOctave";
    inline constexpr const char* subLevel   = "subLevel";
    inline constexpr const char* noiseType  = "noiseType";
    inline constexpr const char* noiseLevel = "noiseLevel";

    // --- FILTER ---
    inline constexpr const char* filterType = "filterType";
    inline constexpr const char* cutoff     = "cutoff";
    inline constexpr const char* resonance  = "resonance";

    // --- AMP ENV / MOD ENV ---
    inline constexpr const char* ampA = "ampA";
    inline constexpr const char* ampD = "ampD";
    inline constexpr const char* ampS = "ampS";
    inline constexpr const char* ampR = "ampR";
    inline constexpr const char* modA = "modA";
    inline constexpr const char* modD = "modD";
    inline constexpr const char* modS = "modS";
    inline constexpr const char* modR = "modR";

    // --- LFO 1 / LFO 2 / ModEnv→Cutoff ---
    inline constexpr const char* lfoRate   = "lfoRate";
    inline constexpr const char* lfoDepth  = "lfoDepth";
    inline constexpr const char* lfoDest   = "lfoDest";
    inline constexpr const char* lfo2Rate  = "lfo2Rate";
    inline constexpr const char* lfo2Depth = "lfo2Depth";
    inline constexpr const char* lfo2Dest  = "lfo2Dest";
    inline constexpr const char* lfo2On    = "lfo2On";
    inline constexpr const char* modEnvCut = "modEnvCut";

    // --- DRIVE / MASTER / GLIDE ---
    inline constexpr const char* drive = "drive";
    inline constexpr const char* gain  = "gain";
    inline constexpr const char* glide = "glide";

    // --- セクションのON/OFF ---
    inline constexpr const char* osc1On   = "osc1On";
    inline constexpr const char* osc2On   = "osc2On";
    inline constexpr const char* subOn    = "subOn";
    inline constexpr const char* noiseOn  = "noiseOn";
    inline constexpr const char* filterOn = "filterOn";
    inline constexpr const char* driveOn  = "driveOn";
    inline constexpr const char* lfoOn    = "lfoOn";
    inline constexpr const char* modEnvOn = "modEnvOn";
    inline constexpr const char* fx1On    = "fx1On";
    inline constexpr const char* fx2On    = "fx2On";
    inline constexpr const char* eqOn     = "eqOn";

    // --- FX（2スロット） ---
    inline constexpr const char* fx1Type  = "fx1Type";
    inline constexpr const char* fx1Rate  = "fx1Rate";
    inline constexpr const char* fx1Depth = "fx1Depth";
    inline constexpr const char* fx1Mix   = "fx1Mix";
    inline constexpr const char* fx1Fb    = "fx1Fb";
    inline constexpr const char* fx2Type  = "fx2Type";
    inline constexpr const char* fx2Rate  = "fx2Rate";
    inline constexpr const char* fx2Depth = "fx2Depth";
    inline constexpr const char* fx2Mix   = "fx2Mix";
    inline constexpr const char* fx2Fb    = "fx2Fb";

    // --- EQ（3バンド） ---
    inline constexpr const char* eqLow  = "eqLow";
    inline constexpr const char* eqMid  = "eqMid";
    inline constexpr const char* eqMidF = "eqMidF";
    inline constexpr const char* eqHigh = "eqHigh";

    //==========================================================================
    /** モジュレーション・マトリクスのスロット数。**本人の指定で4**（EAGLE type0は8）。 */
    inline constexpr int numMatrixSlots = 4;

    /** マトリクスのIDは番号で作ります（`"mtxSrc0"`のように）。

        **文字列を並べて書かないのは、スロット数を変えたときに
        書き漏らしが出ないようにするため**です。 */
    juce::String matrixSource (int slot);
    juce::String matrixDestination (int slot);
    juce::String matrixAmount (int slot);

    //==========================================================================
    // 選択肢。**画面と音の両方がここを見ます**（1.27）

    juce::StringArray getWaveNames();
    juce::StringArray getSubWaveNames();
    juce::StringArray getSubOctaveNames();
    juce::StringArray getNoiseNames();
    juce::StringArray getFilterTypeNames();
    juce::StringArray getLfoDestinationNames();
    juce::StringArray getFxTypeNames();

    /** マトリクスの「何で」。0番の`None`は**必ず先頭**（切ってある状態）。 */
    juce::StringArray getMatrixSourceNames();

    /** マトリクスの「何を」。番号は`MantaSynthVoice`の`destMod[]`の添字と対です。 */
    juce::StringArray getMatrixDestinationNames();

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
