#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

//==============================================================================
/**
    内蔵プラグインで共有する**Biquadの土台**（Phase 208）。

    Manta EQ（`MantaEQ/EQFilterDesign.h`）と Manta Comp（サイドチェインの検出用）が
    同じものを使います。**入れ物と1段ぶんの計算だけ**で、
    「どの形にするか」は使う側が決めます——EQは9つの形状を段に分けて組み、
    コンプは検出用のハイパスとローパスを1つずつしか使いません。

    ### なぜ`juce::dsp::IIR::Filter`ではないか

    `juce::dsp::IIR::Coefficients`は**参照カウント付きの確保**を伴います。
    ダイナミックEQは32サンプルごとに係数を作り直すので、そのたびに確保が走ると
    `processBlock()`の決まりを破ります（HANDOVER 9.5）。

    ここには確保がどこにもありません。
*/
namespace MantaBiquad
{
    /** 係数（a0で正規化済み）。1次のときは`b2 = a2 = 0`。 */
    struct Coeffs
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    /** 1段ぶんの状態（Transposed Direct Form II）。**係数は持ちません。**

        係数を持たせないのは、**同じ係数を何チャンネルにも使う**ためです
        （ステレオなら`Biquad`が2つ、`Coeffs`は1つ）。 */
    struct Biquad
    {
        void reset() noexcept { z1 = 0.0; z2 = 0.0; }

        inline float process (float input, const Coeffs& c) noexcept
        {
            const double x = (double) input;
            const double y = c.b0 * x + z1;

            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;

            return (float) y;
        }

        double z1 = 0.0, z2 = 0.0;
    };

    //==========================================================================
    /** a0で割って正規化する。**RBJの式はどれもa0が1ではない**ので、ここを通す。 */
    inline Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        const double inv = (std::abs (a0) > 1.0e-12) ? 1.0 / a0 : 1.0;

        Coeffs c;
        c.b0 = (float) (b0 * inv);
        c.b1 = (float) (b1 * inv);
        c.b2 = (float) (b2 * inv);
        c.a1 = (float) (a1 * inv);
        c.a2 = (float) (a2 * inv);

        return c;
    }

    /** ナイキストに寄りすぎた周波数を戻す。

        **これが無いと、高いほうへ寄せたときに係数が壊れます**（`tan(w0/2)`が発散する）。 */
    inline double limitFrequency (double hz, double sampleRate) noexcept
    {
        return juce::jlimit (5.0, juce::jmax (10.0, sampleRate * 0.4995), hz);
    }

    /** 2次のローパス（RBJ Cookbook）。 */
    inline Coeffs designLowPass (double hz, double q, double sampleRate) noexcept
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * limitFrequency (hz, sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.025, q));
        const double base = 1.0 - cosw;

        return normalise (base * 0.5, base, base * 0.5,
                           1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    /** 2次のハイパス（RBJ Cookbook）。 */
    inline Coeffs designHighPass (double hz, double q, double sampleRate) noexcept
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * limitFrequency (hz, sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.025, q));
        const double base = 1.0 + cosw;

        return normalise (base * 0.5, -base, base * 0.5,
                           1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    //==========================================================================
    // 8.210：ここから下はPhase 242で移してきたもの。
    //
    // **Manta EQの`.cpp`の中に閉じていた式**です。ディレイのフィードバック内
    // フィルターが同じ形を要るようになったので、**共有のほうへ出しました**（1.27）——
    // 2つ目の写しを作ると、片方だけ直したときに**同じ名前の違う音**ができます。
    //
    // EQ側は`designLowPass()`のときと同じ形で呼び直しています
    // （Qの範囲はEQが自分で詰めてから渡す。`MantaEQParams::minQ`が
    // ちょうど下の`jmax (0.025, q)`と同じ値なので、**音は変わりません**）。

    /** ピークが0dBのバンドパス（RBJの"constant 0 dB peak gain"）。 */
    inline Coeffs designBandPass (double hz, double q, double sampleRate) noexcept
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * limitFrequency (hz, sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.025, q));

        return normalise (alpha, 0.0, -alpha,
                           1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    /** ベル（ピーキング）。`gainDb`が正なら持ち上げ、負なら削ります。 */
    inline Coeffs designBell (double hz, double q, double gainDb, double sampleRate) noexcept
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * limitFrequency (hz, sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.025, q));
        const double A = std::pow (10.0, gainDb / 40.0);

        return normalise (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A,
                           1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
    }

    /** ノッチ（その周波数だけを落とす）。 */
    inline Coeffs designNotch (double hz, double q, double sampleRate) noexcept
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * limitFrequency (hz, sampleRate) / sampleRate;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.025, q));

        return normalise (1.0, -2.0 * cosw, 1.0,
                           1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
    }

    /** 8.210：**分子だけを倍する**（Phase 242）。

        フィードバックループの中では、**フィルターの山が1倍を超えてはいけません**
        （8.209と同じ理由——一周の利得が`feedback`を超えると発振します）。
        後ろで掛け算を1つ足すのではなく、**係数を作るときに畳んでおきます。**

        分母（`a1`・`a2`）は触りません——**触ると極が動いて、形そのものが変わります。** */
    inline Coeffs withGain (Coeffs c, float scale) noexcept
    {
        c.b0 *= scale;
        c.b1 *= scale;
        c.b2 *= scale;

        return c;
    }
}
