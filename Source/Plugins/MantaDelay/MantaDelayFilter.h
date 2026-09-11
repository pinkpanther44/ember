#pragma once

#include <juce_audio_basics/juce_audio_basics.h>   // juce::Decibels

#include "../MantaBiquad.h"

#include <cmath>

//==============================================================================
/**
    8.210：**フィードバックループ内フィルター**（Phase 242／ディレイ仕様書5-2）。

    ─────────────────────────────────────────────────────────────────────────
    ループの中にあること
    ─────────────────────────────────────────────────────────────────────────

    出口に1つ掛けるのとは、**まるで違う音**になります。

    | | 聞こえ方 |
    |---|---|
    | 出口に1回 | どの反復も同じ色。**EQを後ろに挿したのと同じ** |
    | **ループの中（これ）** | 反復するたびに掛かる。**回を追うごとに細くなっていく** |

    キャラクター（`MantaDelayCharacter`）と同じ場所です。
    順番はつまみで選べます（`Settings::post`）——仕様書5-2の`Position: Pre/Post`。

    ─────────────────────────────────────────────────────────────────────────
    **山が1倍を超えてはいけません**（8.209の続き）
    ─────────────────────────────────────────────────────────────────────────

    ここがこのファイルの肝です。

    一周の利得は`feedback`が決めるもので、**フィルターがそれを上回ってはいけません。**
    ところがフィルターは、素のままだと1倍を超えます：

    | 形 | 山の高さ |
    |---|---|
    | LP・HP | Qが`1/√2`を超えると共振して持ち上がる（Q=8で約+18dB） |
    | Bell | `gain`が正のぶんそのまま（+12dBなら4倍） |
    | BP・Notch | 1倍（RBJの"constant 0 dB peak gain"） |

    Q=8のローパスを`feedback`0.9で回すと、**一周の利得が共振点で7倍**になります。
    Phase 241でDriveの補正を間違えたときと**同じ壊れ方**です——
    静かな反復がひと回りごとに膨らみ、やがて飽和したまま鳴り続けます。

    なので`getPeakGain()`で山の高さを出し、**係数の分子へ畳んで1倍に戻します**
    （`MantaBiquad::withGain()`）。結果、**フィルターは必ず「削る」側にだけ働きます**——
    Bellを持ち上げると、その帯だけが**他より遅く減衰する**という形で効きます。

    > **後ろで掛け算を足すのではなく、係数に畳むこと。**
    > 毎サンプルの掛け算が1つ増えるのと、ブロックの頭で3回掛けるのとの違いです。

    ─────────────────────────────────────────────────────────────────────────
    係数は1つ、状態はチャンネルごと
    ─────────────────────────────────────────────────────────────────────────

    `MantaBiquad`の決まりどおりです（あちらの`Biquad`は係数を持ちません）。
    `Design`を1つ作って、`MantaBiquad::Biquad`をチャンネルごとに持ちます。
*/
namespace MantaDelayFilter
{
    /** 形。**並びを変えないこと**（保存されるのは番号です）。

        **先頭は`off`。** 8.209で決めたことをそのまま当てています——
        **0は「何もしない」であるべき**。`off`が無いと、
        「フィルターを使わない」を表すのに別のON/OFFパラメータが要ります。 */
    enum class Type
    {
        off,        ///< 素通し
        lowPass,    ///< 高いほうを落とす（いちばんよく使う形）
        highPass,   ///< 低いほうを落とす
        bandPass,   ///< その帯だけ残す
        bell,       ///< その帯を持ち上げる／削る
        notch,      ///< その帯だけ落とす

        count
    };

    inline constexpr int getTypeCount() { return (int) Type::count; }

    /** 表示名。**ASCIIのみ**（つまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getTypeNames()
    {
        return { "Off", "Low Pass", "High Pass", "Band Pass", "Bell", "Notch" };
    }

    /** `Gain`が効くのはBellだけ。**画面のグレーアウトはこれを見ます。**

        効くかどうかの判断は**ここ1箇所**（1.27。`MantaDelayCharacter::getCapabilities()`と同じ考え）。 */
    inline bool usesGain (Type type) { return type == Type::bell; }

    /** `Freq`・`Q`が効くか（＝`off`でないか）。 */
    inline bool isActive (Type type) { return type != Type::off && type != Type::count; }

    //==========================================================================
    struct Settings
    {
        Type type = Type::off;

        float frequencyHz = 1000.0f;
        float q = 0.707f;
        float gainDb = 0.0f;

        /** 仕様書5-2の`Position`。`false`＝Pre（フィルター→キャラクター）、
            `true`＝Post（キャラクター→フィルター）。

            **Preだと、削ったあとに歪みます**（歪みが新しい高域を作る）。
            **Postだと、歪んでから削ります**（角が取れる）。
            Tapeで飽和を上げたときに、いちばん差が出ます。 */
        bool post = false;
    };

    //==========================================================================
    /** そのフィルターの**山の高さ**（倍。1.0なら持ち上がらない）。

        アナログの2次の式そのままで出しています。双一次変換で少しずれますが、
        **1倍より下へ寄せるための見積もり**なので、外すとしても安全な側です。 */
    inline float getPeakGain (Type type, float q, float gainDb)
    {
        switch (type)
        {
            case Type::lowPass:
            case Type::highPass:
            {
                // `H(s) = ω0² / (s² + (ω0/Q)s + ω0²)`の最大値。
                // **Qが`1/√2`以下なら山はできません**（そこが平坦の境目）
                const double qq = juce::jmax (0.025, (double) q);

                if (qq <= juce::MathConstants<double>::sqrt2 * 0.5)
                    return 1.0f;

                return (float) (qq / std::sqrt (1.0 - 1.0 / (4.0 * qq * qq)));
            }

            case Type::bell:
                // 持ち上げたぶんがそのまま山になります。**削る側は山になりません**
                return gainDb > 0.0f ? juce::Decibels::decibelsToGain (gainDb) : 1.0f;

            case Type::bandPass:   // RBJの"constant 0 dB peak gain"
            case Type::notch:      // 落とすだけ
            case Type::off:
            case Type::count:
            default:
                return 1.0f;
        }
    }

    //==========================================================================
    /** 係数（**チャンネルで共有します**）。

        `active`が`false`のときは、使う側がフィルターの段を**丸ごと飛ばします**——
        素通しの係数を通すのではありません。`off`のときに毎サンプル
        biquadを1段回すのは、何もしないための計算です。 */
    struct Design
    {
        bool active = false;
        MantaBiquad::Coeffs coeffs;
    };

    /** ブロックの頭で1回だけ呼ぶこと（`std::cos`・`std::pow`が入っています）。 */
    inline Design design (const Settings& settings, double sampleRate)
    {
        Design result;

        if (! isActive (settings.type))
            return result;

        const double hz = (double) settings.frequencyHz;
        const double q  = (double) settings.q;

        MantaBiquad::Coeffs coeffs;

        switch (settings.type)
        {
            case Type::lowPass:   coeffs = MantaBiquad::designLowPass (hz, q, sampleRate); break;
            case Type::highPass:  coeffs = MantaBiquad::designHighPass (hz, q, sampleRate); break;
            case Type::bandPass:  coeffs = MantaBiquad::designBandPass (hz, q, sampleRate); break;
            case Type::notch:     coeffs = MantaBiquad::designNotch (hz, q, sampleRate); break;

            case Type::bell:
                coeffs = MantaBiquad::designBell (hz, q, (double) settings.gainDb, sampleRate);
                break;

            case Type::off:
            case Type::count:
            default:
                return result;
        }

        // **1倍に戻す**（上の説明）。一周の利得は`feedback`が決めるもの
        const float peak = getPeakGain (settings.type, settings.q, settings.gainDb);

        result.coeffs = (peak > 1.0f) ? MantaBiquad::withGain (coeffs, 1.0f / peak) : coeffs;
        result.active = true;

        return result;
    }
}
