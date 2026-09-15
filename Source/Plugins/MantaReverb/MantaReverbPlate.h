#pragma once

#include "MantaReverbAllPass.h"
#include "MantaReverbDamping.h"
#include "MantaReverbDelayLine.h"

#include <array>
#include <cmath>

//==============================================================================
/**
    8.244：**Dattorro型プレートリバーブ**（Phase 255／仕様書2-2・設計書4-1）。

    Jon Dattorro, *Effect Design Part 1: Reverberator and Other Filters*
    (JAES 1997) のトポロジーです。

    ─────────────────────────────────────────────────────────────────────────
    ライト版であること（設計書4-1）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**原論文のディレイ長・係数をそのまま採用し、
    独自のチューニングや改良は行わない。**

    失われるもの：**自社独自の音の個性づけ**（他のDattorro系実装と似た響きになる）。

    > **これはそのまま守ります。** 数字を「よさそうだから」動かし始めると、
    > **原典なのかこちらの手が入ったのか、あとで誰にも分からなくなります。**
    > 変えたくなったら、**変えたと書いてから変えること。**

    ─────────────────────────────────────────────────────────────────────────
    形
    ─────────────────────────────────────────────────────────────────────────

    ```
    入力 →(帯域制限)→ AP142 → AP107 → AP379 → AP277 ─┬──────────────┐
                                                      │              │
      ┌───────────────────────────────────────────────┘              │
      │  左半分                                                       │
      ├→ AP672 → D4453 → ダンピング → ×decay → AP1800 → D3720 → ×decay ┤
      │                                                               │
      │  右半分                                    ┌──────────────────┘
      └← ×decay ← D3163 ← AP2656 ← ×decay ← ダンピング ← D4217 ← AP908 ←┘
    ```

    **8の字**です。左半分の出口が右半分の入口へ、右半分の出口が左半分の入口へ戻ります。

    ─────────────────────────────────────────────────────────────────────────
    数字は 29761 Hz のもの
    ─────────────────────────────────────────────────────────────────────────

    原論文は**29761 Hz**（当時のハードウェアのレート）でサンプル数を書いています。
    ここでは**秒に直してから**いまのレートへ掛け直します——
    そうしないと、96kHzで**3倍速いプレート**になります。

    ─────────────────────────────────────────────────────────────────────────
    `Decay`のつまみとの関係
    ─────────────────────────────────────────────────────────────────────────

    原論文の`decay`は**タンクの係数そのもの**で、秒ではありません。
    8の字を1周するあいだに**4回**掛かるので、

    ```
        decay⁴ = 10^(−3·T / RT60)   →   decay = 10^(−0.75·T / RT60)
    ```

    `T`は8つの部品の長さの合計（29761Hzで21589サンプル＝**0.725秒**）。
    `Size`で伸び縮みするので、`T`も掛け直します。

    > **一周の利得は`decay⁴`を超えません。** オールパスは振幅1倍（8.220）、
    > ダンピングは`|H| ≤ 1`（`MantaReverbDamping`）。
    > FDNのときと同じで、**減らすのは`decay`だけ**という状態にしてあります。

    ─────────────────────────────────────────────────────────────────────────
    出口の7タップ
    ─────────────────────────────────────────────────────────────────────────

    原論文が指定している、**タンクの途中から拾う場所**です。
    左右で違う場所から拾うので、**1本のタンクからステレオが出ます。**
*/
class MantaReverbPlate
{
public:
    /** 原論文のレート。**ここから秒へ直します**（上の説明）。 */
    static constexpr double referenceSampleRate = 29761.0;

    /** `MantaReverbFdn::maxSizeScale`と同じであること（確保の長さが決まります）。 */
    static constexpr double maxSizeScale = 2.0;

    /** 入口の拡散4段（原論文 Figure 1）。 */
    static constexpr int inputSamples[4] { 142, 107, 379, 277 };
    static constexpr float inputCoefficients[4] { 0.75f, 0.75f, 0.625f, 0.625f };

    /** タンクの中。**左半分・右半分の順**（原論文 Figure 1）。 */
    static constexpr int tankAllPassSamples[4] { 672, 1800, 908, 2656 };
    static constexpr float tankAllPassCoefficients[4] { 0.70f, 0.50f, 0.70f, 0.50f };
    static constexpr int tankDelaySamples[4] { 4453, 3720, 4217, 3163 };

    /** 原論文の`diffusion`の基準点。**つまみの70%がここに当たります**——
        つまり**既定値で原典そのもの**です（設計書4-1）。 */
    static constexpr float referenceDiffusion = 0.70f;

    /** 出口のタップ（原論文 Figure 1 の`y_L` / `y_R`）。

        `line`は0〜3が`tankDelay`、4〜7が`tankAllPass`（同じ並び）。 */
    struct OutputTap
    {
        int line;
        int offset;
        float sign;
    };

    static constexpr int numOutputTaps = 7;

    static constexpr OutputTap leftTaps[numOutputTaps]
    {
        { 2,  266, +1.0f }, { 2, 2974, +1.0f }, { 7, 1913, -1.0f }, { 3, 1996, +1.0f },
        { 0, 1990, -1.0f }, { 5,  187, -1.0f }, { 1, 1066, -1.0f }
    };

    static constexpr OutputTap rightTaps[numOutputTaps]
    {
        { 0,  353, +1.0f }, { 0, 3627, +1.0f }, { 5, 1228, -1.0f }, { 1, 2673, +1.0f },
        { 2, 2111, -1.0f }, { 7,  335, -1.0f }, { 3,  121, -1.0f }
    };

    /** 原論文の出口の係数。 */
    static constexpr float outputGain = 0.6f;

    /** 8.244：**FDN側と音量を合わせるための倍率**（Phase 255）。

        アルゴリズムを切り替えただけで音量が変わると、
        **どちらが好きか**ではなく**どちらが大きいか**を聞くことになります。
        `MantaReverbFdn`と同じ条件で測って決めた数字です。

        > **原典の係数は触っていません**（設計書4-1）。
        > これは**タンクの外**で掛けるもので、響きの形は変わりません。 */
    static constexpr float levelMatch = 0.60f;

    //==========================================================================
    void prepare (double sampleRateToUse)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);

        // **確保はいちばん大きいとき**（9.4）。`Size`で変わるのは読む位置だけです
        const auto capacity = [this] (int referenceSamples)
        {
            return juce::jmax (1, (int) std::ceil (referenceSamples * maxSizeScale
                                                     * sampleRate / referenceSampleRate));
        };

        for (int stage = 0; stage < 4; ++stage)
        {
            inputDiffusers[(size_t) stage].prepare (capacity (inputSamples[stage]));
            tankAllPasses[(size_t) stage].prepare (capacity (tankAllPassSamples[stage]));
            tankDelays[(size_t) stage].prepare (capacity (tankDelaySamples[stage]));
        }

        for (auto& damper : dampers)
            damper.prepare (sampleRate);

        sizeScale = 0.0f;   // `setSize()`が必ず1度は走るように
        setSize (1.0f);
        reset();
    }

    void reset()
    {
        for (int stage = 0; stage < 4; ++stage)
        {
            inputDiffusers[(size_t) stage].reset();
            tankAllPasses[(size_t) stage].reset();
            tankDelays[(size_t) stage].reset();
        }

        for (auto& damper : dampers)
            damper.reset();

        leftFeedback = 0.0f;
        rightFeedback = 0.0f;
    }

    void setSize (float scale)
    {
        const float wanted = juce::jlimit (0.25f, (float) maxSizeScale, scale);

        if (wanted == sizeScale)
            return;

        sizeScale = wanted;

        const auto length = [this] (int referenceSamples)
        {
            return juce::jmax (1, (int) std::round (referenceSamples * sizeScale
                                                      * sampleRate / referenceSampleRate));
        };

        for (int stage = 0; stage < 4; ++stage)
        {
            inputDiffusers[(size_t) stage].setLength (length (inputSamples[stage]));
            tankAllPasses[(size_t) stage].setLength (length (tankAllPassSamples[stage]));
            tankDelays[(size_t) stage].setLength (length (tankDelaySamples[stage]));
        }

        // **拾う場所も`Size`で伸び縮みします。** 固定にすると、板を大きくしたときに
        // **頭のほうばかり**を拾うことになります。
        // **ここで出しておくこと**——サンプルごとに掛け算と丸めを14回やる場所ではありません
        for (int tap = 0; tap < numOutputTaps; ++tap)
        {
            leftTapOffsets[(size_t) tap] = length (leftTaps[tap].offset);
            rightTapOffsets[(size_t) tap] = length (rightTaps[tap].offset);
        }

        updateDecay();
    }

    void setDecaySeconds (float seconds)
    {
        const float wanted = juce::jlimit (0.05f, 60.0f, seconds);

        if (wanted == decaySeconds)
            return;

        decaySeconds = wanted;
        updateDecay();
    }

    /** つまみの0〜1。**0.7で原典の係数そのもの**（`referenceDiffusion`）。

        0.84あたりから上は頭打ちになります（`MantaReverbAllPass::maxCoefficient`）——
        原典の0.75がもともと上限で、**それ以上は段の長さが聞こえてきます。** */
    void setDiffusion (float amount)
    {
        const float scale = juce::jlimit (0.0f, 1.0f, amount) / referenceDiffusion;

        for (int stage = 0; stage < 4; ++stage)
        {
            inputDiffusers[(size_t) stage].setCoefficient (inputCoefficients[stage] * scale);
            tankAllPasses[(size_t) stage].setCoefficient (tankAllPassCoefficients[stage] * scale);
        }
    }

    void setDamping (float highFrequency, float highAmount, float lowFrequency, float lowAmount)
    {
        for (auto& damper : dampers)
            damper.setParameters (highFrequency, highAmount, lowFrequency, lowAmount);
    }

    /** 1サンプルぶん。 */
    void processSample (float inputLeft, float inputRight, float& outputLeft, float& outputRight)
    {
        // 原論文は**モノラル1本**をタンクへ入れます（板は1枚なので）。
        // 左右は**出口のタップの取り方**で作ります
        float x = 0.5f * (inputLeft + inputRight) * inputGain;

        for (auto& stage : inputDiffusers)
            x = stage.processSample (x);

        //----------------------------------------------------------------------
        // 8の字。**左半分は右の戻りを、右半分は左の戻りを受けます**

        float left = tankAllPasses[0].processSample (x + rightFeedback);
        left = tankDelays[0].processSample (left);
        left = dampers[0].processSample (left);
        left *= decayCoefficient;
        left = tankAllPasses[1].processSample (left);
        left = tankDelays[1].processSample (left);

        float right = tankAllPasses[2].processSample (x + leftFeedback);
        right = tankDelays[2].processSample (right);
        right = dampers[1].processSample (right);
        right *= decayCoefficient;
        right = tankAllPasses[3].processSample (right);
        right = tankDelays[3].processSample (right);

        // **両方を読んでから、両方を書くこと**（8.218・8.204）。
        // 先に`leftFeedback`を更新すると、右半分が**今のサンプルで書いた値**を読みます
        const float nextLeft = left * decayCoefficient;
        const float nextRight = right * decayCoefficient;

        leftFeedback = nextLeft;
        rightFeedback = nextRight;

        //----------------------------------------------------------------------
        // 出口の7タップ

        outputLeft = outputGain * readTaps (leftTaps, leftTapOffsets);
        outputRight = outputGain * readTaps (rightTaps, rightTapOffsets);
    }

private:
    float readTaps (const OutputTap* taps, const std::array<int, (size_t) numOutputTaps>& offsets) const
    {
        float sum = 0.0f;

        for (int tap = 0; tap < numOutputTaps; ++tap)
        {
            const auto& entry = taps[tap];
            const int offset = offsets[(size_t) tap];

            sum += entry.sign * (entry.line < 4
                                     ? tankDelays[(size_t) entry.line].readAt (offset)
                                     : tankAllPasses[(size_t) (entry.line - 4)].readAt (offset));
        }

        return sum;
    }

    void updateDecay()
    {
        double loopSamples = 0.0;

        for (int stage = 0; stage < 4; ++stage)
            loopSamples += tankAllPasses[(size_t) stage].getLength()
                             + tankDelays[(size_t) stage].getLength();

        const double loopSeconds = loopSamples / sampleRate;

        // 8の字1周で**4回**掛かるので、1回ぶんは`3/4`乗（上の説明）
        const float coefficient = (float) std::pow (10.0, -0.75 * loopSeconds / (double) decaySeconds);

        decayCoefficient = juce::jlimit (0.0f, 0.9995f, coefficient);

        // 8.244：**入口で割ること**（Phase 255）。FDNと同じ理由です（`MantaReverbFdn`）——
        // これが無いと、`Decay`を回しただけで音量が上がります。
        //
        // 1周の利得は`decay⁴`なので、溜まる量は`1/(1−decay⁸)`。
        // **測って確かめました**：これを入れる前は0.5秒と12秒で4.9dB違い、
        // 入れた後は0.5dBに収まりました
        const double loopGainSquared = std::pow ((double) decayCoefficient, 8.0);

        inputGain = levelMatch * (float) std::sqrt (juce::jmax (1.0e-4, 1.0 - loopGainSquared));
    }

    double sampleRate = 44100.0;

    std::array<MantaReverbAllPass, 4> inputDiffusers;
    std::array<MantaReverbAllPass, 4> tankAllPasses;
    std::array<MantaReverbDelayLine, 4> tankDelays;
    std::array<MantaReverbDamping, 2> dampers;

    /** 出口の拾う場所（`Size`を掛けた後）。**`setSize()`で出しておきます。** */
    std::array<int, (size_t) numOutputTaps> leftTapOffsets { { 1, 1, 1, 1, 1, 1, 1 } };
    std::array<int, (size_t) numOutputTaps> rightTapOffsets { { 1, 1, 1, 1, 1, 1, 1 } };

    float leftFeedback = 0.0f;
    float rightFeedback = 0.0f;

    float decayCoefficient = 0.5f;

    /** 入口に掛ける大きさ（`updateDecay()`が決めます）。**溜まる量を揃えるため。** */
    float inputGain = 1.0f;

    float sizeScale = 0.0f;
    float decaySeconds = 1.2f;
};
