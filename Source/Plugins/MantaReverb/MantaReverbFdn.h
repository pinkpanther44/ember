#pragma once

#include "MantaReverbAlgorithm.h"
#include "MantaReverbAllPass.h"
#include "MantaReverbDamping.h"
#include "MantaReverbDelayLine.h"

#include <array>
#include <cmath>

//==============================================================================
/**
    8.240：**フィードバック・ディレイ・ネットワーク（FDN）**
    （Phase 254／リバーブ仕様書2-2・設計書4-2）。

    後部残響（レイトリバーブ）を作るところです。

    ─────────────────────────────────────────────────────────────────────────
    ライト版であること（設計書4-2）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**4×4のアダマール行列による基本FDNのみ。
    8〜16ラインへの拡張やダンピングの多帯域化は行わない。**

    落としたもの：**大規模FDNならではの超高密度な残響感**。
    「広さ」そのものは4本でも作れます。

    ─────────────────────────────────────────────────────────────────────────
    8.243：**寸法は持っていません**（Phase 255）
    ─────────────────────────────────────────────────────────────────────────

    ライン長は`MantaReverbAlgorithm`の表から来ます（`setTuning()`）。
    **`Room`と`Hall`はこの箱を共有していて、違いは寸法だけ**です——
    設計書4-2で「Hallも4×4の基本FDNのみ」と決めたので、形を2つ持つ理由がありません。

    **確保はいちばん長い表で**行うので、切り替えで取り直しは起きません（9.4）。

    ─────────────────────────────────────────────────────────────────────────
    形
    ─────────────────────────────────────────────────────────────────────────

    ```
        ┌→ 遅延1 → オールパス → ダンピング → ×g1 ─┐
        │→ 遅延2 → オールパス → ダンピング → ×g2 ─┤
    入力┤→ 遅延3 → オールパス → ダンピング → ×g3 ─┼→ アダマール行列 ─┐
        │→ 遅延4 → オールパス → ダンピング → ×g4 ─┘                  │
        └──────────────────────────────────────────────────────────────┘
    ```

    ### なぜアダマール行列なのか

    **直交行列だからです**（`H/2`は正規直交）。ベクトルの長さを変えないので、
    **混ぜること自体では音は増えも減りもしません。**

    > 減衰を決めているのは`g`だけ、という状態を作っておくこと。
    > **混ぜる場所と減らす場所を分ける**と、減衰時間が計算どおりになります。
    > 8.218で引いた線（フィードバックへ足すものは混ぜる）の、いちばん素直な形です。

    ### `g`の決め方

    `RT60`は「60dB落ちるまでの秒数」。1周`T`秒のラインが1周で落とす量は
    `−60·T/RT60` dB なので、

    ```
        g = 10^(−3·T / RT60)
    ```

    **`T`にはオールパスの長さも入れます。** 一周の長さがそのぶん伸びているので、
    入れないと**設定より長く鳴ります**（RT60は「だいたい」ではなく合わせられる値です）。

    ### 読んでから書く

    8.218・8.204で引いた線：**4本とも読んでから、4本とも書くこと。**
    1本ずつ「読んで混ぜて書く」と、後のラインは**今のブロックで書いた値**を
    読むことになり、行列が意味を失います。

    ### 入口を割る理由——**長く伸ばすほど大きくなるのを止める**

    輪の中に溜まる量は`1/(1−g²)`で決まります。`Decay`を伸ばすと`g`が1へ近づくので、
    **つまみを回しただけで音量が上がります**（`Decay` 1秒と10秒で7倍近く違う）。
    `Size`を縮めても同じことが起きます（1周が短くなるぶん`g`が上がる）。

    入口に`√(1−g²)`を掛けておくと、**溜まる量が一定**になります。

    > 8.224で書いたこと：**Driveは上げるほど音が小さくなる**（`1/g`のため）。
    > あれは「直せないと書いた」ものでしたが、**こちらは直せます**——
    > 減衰は`g`が持ち、大きさは入口が持つ、と分けられるためです。

    ### 発散しないこと（設計書4-3の注意点）

    - 行列は正規直交（長さを変えない）
    - `g < 1`（上でそう作っている。上限も掛けてあります）
    - オールパスは振幅1倍（8.220）
    - ダンピングは`|H| ≤ 1`（`MantaReverbDamping`）

    **輪の中に1倍を超えるものがひとつも無い**ので、発散しません。
*/
class MantaReverbFdn
{
public:
    /** ラインの本数（設計書4-2の「4×4程度」）。 */
    static constexpr int numLines = 4;

    /** `Size`の上限。**確保する長さはこれで決まります。** */
    static constexpr double maxSizeScale = 2.0;

    /** `g`の上限。計算上1未満ですが、**丸めで1に届かせないため**に掛けます。 */
    static constexpr float maxFeedbackGain = 0.9995f;

    //==========================================================================
    /** 8.250：**揺らすライン**（Phase 260／`Random Hall`。設計書4-3）。

        ─────────────────────────────────────────────────────────────────
        ライト版であること（設計書4-3）
        ─────────────────────────────────────────────────────────────────

        設計書の決定：**1〜2本のディレイラインだけを緩やかに変調する簡易版。
        全ラインを複雑にランダム化する本格版は行わない。**

        落としたもの：全ラインが独立に揺れることで生まれる、
        本家Random Hallの「掴みどころのない」揺らぎ。

        ─────────────────────────────────────────────────────────────────
        なぜ2本だけでいいのか
        ─────────────────────────────────────────────────────────────────

        FDNは**行列で毎周混ざります**。1本を揺らせば、次の周でその揺れが
        4本すべてへ配られます。**4本とも揺らす必要はありません。**

        ─────────────────────────────────────────────────────────────────
        速さは**互いに素に近い**2つ（8.220と同じ理由）
        ─────────────────────────────────────────────────────────────────

        同じ速さで2本揺らすと、**2本が揃って動いて1本ぶんの揺れ**になります。 */
    static constexpr int modulatedLines[2] { 1, 2 };
    static constexpr double modulationRatesHz[2] { 0.13, 0.19 };

    /** 揺れの深さの上限（ミリ秒）。

        **これ以上は「揺れ」ではなく「うねり」**になります——
        リバーブのテールで音程が動いて聞こえてしまう。 */
    static constexpr double maxModulationMs = 4.0;

    void prepare (double sampleRateToUse)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);

        // 8.243：**確保はどの表でもいちばん長い寸法で**（Phase 255）。
        // アルゴリズムを切り替えるたびに取り直すと、音のスレッドで確保が起きます（9.4）
        const auto longest = MantaReverbAlgorithm::getLongestFdnTuning();

        for (int line = 0; line < numLines; ++line)
        {
            const auto samples = [this] (double milliseconds)
            {
                return juce::jmax (1, (int) std::ceil (milliseconds * maxSizeScale
                                                         * 0.001 * sampleRate));
            };

            lines[(size_t) line].prepare (samples (longest.lineMs[(size_t) line]));
            allPasses[(size_t) line].prepare (samples (longest.allPassMs[(size_t) line]));
            dampers[(size_t) line].prepare (sampleRate);
        }

        sizeScale = 0.0f;   // `setSize()`が必ず1度は走るように
        setTuning (MantaReverbAlgorithm::roomTuning);
        setSize (1.0f);
        reset();
    }

    /** 8.243：寸法の表を差し替える（Phase 255）。**`Room`と`Hall`の違いはこれだけ。** */
    void setTuning (const MantaReverbAlgorithm::FdnTuning& newTuning)
    {
        if (&newTuning == tuning)
            return;

        tuning = &newTuning;
        updateLengths();
    }

    void reset()
    {
        for (int line = 0; line < numLines; ++line)
        {
            lines[(size_t) line].reset();
            allPasses[(size_t) line].reset();
            dampers[(size_t) line].reset();
        }
    }

    /** 部屋の大きさ（`maxSizeScale`まで）。**ライン長そのものが変わります。** */
    void setSize (float scale)
    {
        const float wanted = juce::jlimit (0.25f, (float) maxSizeScale, scale);

        if (wanted == sizeScale)
            return;

        sizeScale = wanted;
        updateLengths();
    }

    /** RT60（秒）。 */
    void setDecaySeconds (float seconds)
    {
        const float wanted = juce::jlimit (0.05f, 60.0f, seconds);

        if (wanted == decaySeconds)
            return;

        decaySeconds = wanted;
        updateGains();
    }

    /** ライン内オールパスの係数（0〜`MantaReverbAllPass::maxCoefficient`）。 */
    void setDiffusion (float coefficient)
    {
        for (auto& allPass : allPasses)
            allPass.setCoefficient (coefficient);
    }

    void setDamping (float highFrequency, float highAmount, float lowFrequency, float lowAmount)
    {
        for (auto& damper : dampers)
            damper.setParameters (highFrequency, highAmount, lowFrequency, lowAmount);
    }

    /** 8.250：揺れの深さ（0〜1）。**0なら揺らしません**（8.209の②）。 */
    void setModulation (float depth)
    {
        modulationDepth = juce::jlimit (0.0f, 1.0f, depth);
        modulationActive = modulationDepth > 0.0001f;

        for (int i = 0; i < 2; ++i)
        {
            const int line = modulatedLines[i];

            // **深さはラインの長さの1/4まで。** 短いラインで深く揺らすと、
            // 読む位置が1サンプルより手前へ行こうとします
            const double wanted = maxModulationMs * modulationDepth * 0.001 * sampleRate;

            modulationSamples[(size_t) i] = juce::jmin (wanted,
                                                         lines[(size_t) line].getLength() * 0.25);

            modulationIncrement[(size_t) i] = modulationRatesHz[i] / sampleRate;
        }
    }

    /** 1サンプルぶん。**4本とも読んでから、4本とも書きます**（上の説明）。 */
    void processSample (float inputLeft, float inputRight, float& outputLeft, float& outputRight)
    {
        std::array<float, (size_t) numLines> taps {};

        // 8.250：**揺れを進めるのはサンプルごとに1回**（Phase 260）。
        // チャンネルやラインごとに進めると、**線ごとに違う位相**になって狙いが崩れます
        std::array<double, 2> offsets { { 0.0, 0.0 } };

        if (modulationActive)
        {
            for (int i = 0; i < 2; ++i)
            {
                modulationPhase[(size_t) i] += modulationIncrement[(size_t) i];

                if (modulationPhase[(size_t) i] >= 1.0)
                    modulationPhase[(size_t) i] -= 1.0;

                // **0〜1の範囲へ**（`length`より後ろは読めないので、手前へだけ動かします）
                const double sine = 0.5 + 0.5 * std::sin (modulationPhase[(size_t) i]
                                                            * juce::MathConstants<double>::twoPi);

                offsets[(size_t) i] = modulationSamples[(size_t) i] * sine;
            }
        }

        // 1. 読む（いちばん古いもの）→ オールパス → ダンピング → ×g
        for (int line = 0; line < numLines; ++line)
        {
            auto& delay = lines[(size_t) line];

            float value = 0.0f;

            if (modulationActive && (line == modulatedLines[0] || line == modulatedLines[1]))
            {
                const int which = (line == modulatedLines[0]) ? 0 : 1;

                value = delay.readAtFractional ((double) delay.getLength() - offsets[(size_t) which]);
            }
            else
            {
                value = delay.readAt (delay.getLength());
            }

            value = allPasses[(size_t) line].processSample (value);
            value = dampers[(size_t) line].processSample (value);

            taps[(size_t) line] = value * gains[(size_t) line];
        }

        // 2. 出口。**斜めに取り出すこと**——`0+1`と`2+3`のように隣どうしで束ねると、
        //    長さの近いライン（＝似た響き）が同じ側へ寄って、左右が似てきます
        outputLeft = taps[0] + taps[2];
        outputRight = taps[1] + taps[3];

        // 3. 混ぜる（アダマール行列 ÷ 2 ＝ 正規直交）
        const float mixed0 = 0.5f * ( taps[0] + taps[1] + taps[2] + taps[3]);
        const float mixed1 = 0.5f * ( taps[0] - taps[1] + taps[2] - taps[3]);
        const float mixed2 = 0.5f * ( taps[0] + taps[1] - taps[2] - taps[3]);
        const float mixed3 = 0.5f * ( taps[0] - taps[1] - taps[2] + taps[3]);

        // 4. 書く。**入口は4本へばらして入れます**——
        //    左右をそのまま2本ずつに入れると、同じ入力のライン同士が揃って動き、
        //    せっかく4本あるのに2本ぶんの響きにしかなりません（8.223と同じ話）
        constexpr float diagonal = 0.70710678f;   // 1/√2

        const float left = inputLeft * inputGain;
        const float right = inputRight * inputGain;

        lines[0].push (left + mixed0);
        lines[1].push (right + mixed1);
        lines[2].push ((left - right) * diagonal + mixed2);
        lines[3].push ((left + right) * diagonal + mixed3);
    }

private:
    void updateLengths()
    {
        if (tuning == nullptr || sizeScale <= 0.0f)
            return;

        for (int line = 0; line < numLines; ++line)
        {
            const auto samples = [this] (double milliseconds)
            {
                return juce::jmax (1, (int) std::round (milliseconds * sizeScale
                                                          * 0.001 * sampleRate));
            };

            lines[(size_t) line].setLength (samples (tuning->lineMs[(size_t) line]));
            allPasses[(size_t) line].setLength (samples (tuning->allPassMs[(size_t) line]));
        }

        // **長さが変われば一周の時間も変わります。** 掛け直さないと、
        // `Size`を回しただけで減衰時間まで動きます（つまみ1つが2つの意味を持つ）
        updateGains();
    }

    void updateGains()
    {
        double sumOfSquares = 0.0;

        for (int line = 0; line < numLines; ++line)
        {
            // **一周の長さ。** オールパスのぶんも入れること（上の説明）
            const double loopSamples = lines[(size_t) line].getLength()
                                         + allPasses[(size_t) line].getLength();

            const double loopSeconds = loopSamples / sampleRate;

            const float gain = (float) std::pow (10.0, -3.0 * loopSeconds / (double) decaySeconds);

            gains[(size_t) line] = juce::jlimit (0.0f, maxFeedbackGain, gain);
            sumOfSquares += (double) gains[(size_t) line] * gains[(size_t) line];
        }

        // **入口で割ること**（上の「入口を割る理由」）。
        //
        // 輪の中に溜まる量は`1/(1−g²)`で決まるので、
        // `√(1−g²)`を先に掛けておくと**溜まる量が一定**になります。
        const double meanSquare = sumOfSquares / numLines;

        inputGain = (float) std::sqrt (juce::jmax (1.0e-4, 1.0 - meanSquare));
    }

    double sampleRate = 44100.0;

    /** いま使っている寸法の表（`MantaReverbAlgorithm`のもの）。 */
    const MantaReverbAlgorithm::FdnTuning* tuning = nullptr;

    std::array<MantaReverbDelayLine, (size_t) numLines> lines;
    std::array<MantaReverbAllPass, (size_t) numLines> allPasses;
    std::array<MantaReverbDamping, (size_t) numLines> dampers;

    std::array<float, (size_t) numLines> gains { { 0.5f, 0.5f, 0.5f, 0.5f } };

    /** 入口に掛ける大きさ（`updateGains()`が決めます）。**溜まる量を揃えるため。** */
    float inputGain = 1.0f;

    float sizeScale = 0.0f;       // `setSize()`が必ず1度は走るように、範囲の外
    float decaySeconds = 1.2f;

    //==========================================================================
    // 8.250：`Random Hall`の揺れ（Phase 260）

    float modulationDepth = 0.0f;
    bool modulationActive = false;

    std::array<double, 2> modulationSamples { { 0.0, 0.0 } };
    std::array<double, 2> modulationIncrement { { 0.0, 0.0 } };
    std::array<double, 2> modulationPhase { { 0.0, 0.5 } };   // **位相をずらしておくこと**
};
