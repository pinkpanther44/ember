#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

//==============================================================================
/**
    8.211：**LFOモジュレーション**（Phase 242／ディレイ仕様書5-3）。

    ─────────────────────────────────────────────────────────────────────────
    Wow／Flutterと何が違うのか
    ─────────────────────────────────────────────────────────────────────────

    **足す先は同じ**（ディレイタイム）ですが、**役割が違います。**

    | | 何のためにあるか | 誰が持っているか |
    |---|---|---|
    | Wow・Flutter | **テープらしさ。** 選んだキャラクターに付いてくるもの | `MantaDelayCharacter`（キャラクターごとに効く／効かない） |
    | **LFO（これ）** | **効果として掛けるもの。** コーラス／ビブラート | エンジン（**どのキャラクターでも効きます**） |

    だから`getCapabilities()`には入れません。Digital Cleanでも効きます——
    「色付けなし」はキャラクターの話で、**効果を掛けないという意味ではありません。**

    ─────────────────────────────────────────────────────────────────────────
    Random S&Hは寄せません
    ─────────────────────────────────────────────────────────────────────────

    Flutterは目標へ寄せて角を取っていますが（`MantaDelayCharacter::advanceModulation()`）、
    こちらは**折り返しのたびに値が飛びます。** それがSample & Holdです——
    寄せてしまうと、TriangleとRandomの区別が付かなくなります。

    ─────────────────────────────────────────────────────────────────────────
    チャンネルごとに呼ばないこと
    ─────────────────────────────────────────────────────────────────────────

    Wow／Flutterと同じ理由です。**左右で違う揺れになると定位が動きます。**
    ブロックの中で1回進めて、同じ値を両チャンネルへ使います。
*/
namespace MantaDelayLfo
{
    /** 波形。**並びを変えないこと**（保存されるのは番号です）。 */
    enum class Shape
    {
        sine,        ///< なめらか（ビブラート）
        triangle,    ///< 折り返しが角張る
        randomHold,  ///< 1周期ごとに値が飛ぶ（Sample & Hold）

        count
    };

    inline constexpr int getShapeCount() { return (int) Shape::count; }

    /** 表示名。**ASCIIのみ**（つまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getShapeNames()
    {
        return { "Sine", "Triangle", "Random" };
    }

    //==========================================================================
    /** ディレイタイムへ足す揺れの**いちばん深いところ**（秒）。

        **ミリ秒で決めます**（`MantaDelayCharacter`のWowと同じ理由）——
        割合にすると、長いディレイほど揺れが大きくなって使いものになりません。

        20msはコーラスとしては深いほうです。ビブラートにするなら数msで足ります。 */
    inline constexpr double maxDepthSeconds = 0.020;

    //==========================================================================
    class Oscillator
    {
    public:
        void prepare (double sampleRateToUse)
        {
            sampleRate = juce::jmax (8000.0, sampleRateToUse);
            reset();
        }

        void reset()
        {
            phase = 0.0;
            heldValue = 0.0f;
            current = 0.0f;
        }

        /** ブロックの頭で1回だけ。**`depth`は0〜1**、`rateHz`はHz。 */
        void setSettings (Shape newShape, float rateHz, float depth)
        {
            shape = newShape;
            increment = (double) juce::jmax (0.001f, rateHz) / sampleRate;

            // **深さはサンプル数で持ちます。** 毎サンプル掛け算するので、
            // 秒からの換算をここで済ませておきます
            depthSamples = juce::jlimit (0.0f, 1.0f, depth) * (float) (maxDepthSeconds * sampleRate);

            enabled = depthSamples > 0.0f;
        }

        /** ディレイタイムに足すサンプル数。**毎サンプル進めます。** */
        double advance()
        {
            if (! enabled)
                return 0.0;

            phase += increment;

            if (phase >= 1.0)
            {
                phase -= 1.0;

                // **Random S&Hは折り返しのときだけ引く**（上の説明）
                heldValue = random.nextFloat() * 2.0f - 1.0f;
            }

            switch (shape)
            {
                case Shape::sine:
                    current = (float) std::sin (phase * juce::MathConstants<double>::twoPi);
                    break;

                case Shape::triangle:
                    // 位相0で-1、0.5で+1、1で-1に戻る
                    current = (phase < 0.5) ? (float) (phase * 4.0 - 1.0)
                                             : (float) (3.0 - phase * 4.0);
                    break;

                case Shape::randomHold:
                    current = heldValue;   // **寄せません**（上の説明）
                    break;

                case Shape::count:
                default:
                    current = 0.0f;
                    break;
            }

            return (double) (current * depthSamples);
        }

    private:
        Shape shape = Shape::sine;

        double sampleRate = 44100.0;
        double increment = 0.0;
        double phase = 0.0;

        float depthSamples = 0.0f;
        float heldValue = 0.0f;
        float current = 0.0f;
        bool enabled = false;

        juce::Random random;
    };
}
