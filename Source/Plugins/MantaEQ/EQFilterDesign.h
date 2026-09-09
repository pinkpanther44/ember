#pragma once

#include "MantaEQParameters.h"
#include "../MantaBiquad.h"

//==============================================================================
/**
    設計書4.1「Zero Latencyパス」：RBJ Cookbook形式のBiquad係数を作るところ。

    ### 音を出す側と、線を描く側が同じ係数を使う

    **`designBand()`は1つだけ**です（1.27）。`MantaEQProcessor`は返ってきた係数で
    音を通し、`EQCurveComponent`は同じ係数から`magnitudeAt()`で振幅を出して描きます。

    別々に書くと、**聴こえている音と描いてある線が食い違います**——
    しかも片方だけ直したときに「直った」ように見えるので、いちばん気づきにくい形です。

    ### なぜ`juce::dsp::IIR::Filter`を使わないか

    `juce::dsp::IIR::Coefficients`は**参照カウント付きの確保**を伴います。
    ダイナミックEQ（仕様書4.5）は**32サンプルごとに係数を作り直す**ので、
    そのたびに確保が走ると`processBlock()`の決まりを破ります（HANDOVER 9.5）。

    ここでは値型の`Coeffs`と、状態だけを持つ`Biquad`に分けてあります。
    **確保はどこにもありません。**

    ### カット系の段数

    Butterworth特性を段に分けたものです（N次 → `N/2`段＋Nが奇数なら1次を1段）。

    | スロープ | 次数 | 段数 |
    |---|---|---|
    | 6 dB/oct | 1 | 1次×1 |
    | 12 | 2 | Biquad×1 |
    | 24 | 4 | Biquad×2 |
    | 96 | 16 | Biquad×8 |

    Qつまみは**いちばんQの高い段**（＝カットオフの肩）へ掛かります。
    12 dB/octのときButterworthのQは0.7071なので、**つまみの値がそのままQ**になります。
*/
namespace EQFilterDesign
{
    /** 8.170：**入れ物は内蔵プラグインで共有**（Phase 208。`../MantaBiquad.h`）。

        Manta Compのサイドチェイン検出も同じものを使います。
        **呼び出し側の書き方は変わりません**（`EQFilterDesign::Coeffs`のまま）。 */
    using Coeffs = MantaBiquad::Coeffs;
    using Biquad = MantaBiquad::Biquad;

    /** 1バンドぶんの縦続段。 */
    struct SectionList
    {
        EQFilterDesign::Coeffs sections[MantaEQParams::maxSectionsPerBand];
        int numSections = 0;

        void add (const Coeffs& c) noexcept
        {
            if (numSections < MantaEQParams::maxSectionsPerBand)
                sections[numSections++] = c;
        }
    };

    /** 1バンドぶんの係数を作る。**オフのバンドは段数0で返ります。**

        `settings.effectiveGainDb()`（＝Gain＋ダイナミクスのぶん）を使うので、
        ダイナミックEQでも呼ぶのはこの関数だけです。 */
    SectionList designBand (const MantaEQParams::BandSettings& settings, double sampleRate);

    /** その周波数での振幅（倍率。1.0が0dB）。**カーブの描画に使います。** */
    double magnitudeAt (const SectionList& sections, double frequencyHz, double sampleRate);

    /** ダイナミクスの検出用バンドパス。バンドの周波数まわりだけを見て動かすためのもの
        （仕様書4.5）。 */
    Coeffs designDetector (float frequencyHz, float q, double sampleRate);
}
