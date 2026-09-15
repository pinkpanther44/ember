#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

//==============================================================================
/**
    8.240：**テールのダンピング**（Phase 254／リバーブ仕様書5章・設計書2章）。

    実際の部屋では、**高い音から先に消えます**（空気と壁が高域を吸うため）。
    低い側を削るのは部屋の性質ではありませんが、**混ざったときに濁らせない**ために要ります。

    ─────────────────────────────────────────────────────────────────────────
    **フィードバックの中に置くものは、1倍を超えてはいけない**
    ─────────────────────────────────────────────────────────────────────────

    ディレイで3回引いた線です（8.209・8.210・8.218）。ここも同じで、
    **これはFDNの輪の中に入ります。**

    - 1極のローパスは、どの周波数でも`|H| ≤ 1`
    - 1極のハイパス（`x − lowpass(x)`）も、どの周波数でも`|H| ≤ 1`
    - **混ぜ具合（`amount`）は足し算ではなく混ぜ**：`x·(1−a) + filtered·a`

    大きさが1以下の複素数を2つ混ぜても1を超えません。
    つまり**どのつまみをどこまで回しても、減衰時間より長く鳴ることはありません。**

    > 8.218で書いたとおり：**フィードバックへ足すものは「混ぜる」。**
    > `x + a·filtered`にすると、`a`を上げたぶんだけ輪の利得が増えます。

    ### 係数

    1極の`a = exp(−2π·f / fs)`。**`prepare()`と、つまみが動いたときだけ**計算します
    （サンプルごとに`exp`を呼ぶ余裕はありません）。
*/
class MantaReverbDamping
{
public:
    void prepare (double sampleRateToUse)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);
        reset();
        updateCoefficients();
    }

    void reset()
    {
        highState = 0.0f;
        lowState = 0.0f;
    }

    /** ブロックの頭で1回。周波数はHz、量は0〜1。 */
    void setParameters (float newHighFrequency, float newHighAmount,
                        float newLowFrequency, float newLowAmount)
    {
        const float high = juce::jlimit (200.0f, 20000.0f, newHighFrequency);
        const float low = juce::jlimit (20.0f, 2000.0f, newLowFrequency);

        highAmount = juce::jlimit (0.0f, 1.0f, newHighAmount);
        lowAmount = juce::jlimit (0.0f, 1.0f, newLowAmount);

        if (high != highFrequency || low != lowFrequency)
        {
            highFrequency = high;
            lowFrequency = low;
            updateCoefficients();
        }

        // **0は「何もしない」**（ディレイで引いた線の②）。
        // 掛からないときは1極ぶんの計算も飛ばします
        active = (highAmount > 0.0001f) || (lowAmount > 0.0001f);
    }

    bool isActive() const { return active; }

    float processSample (float input)
    {
        if (! active)
            return input;

        float x = input;

        // 高域を削る：ローパスへ寄せていく
        highState += highCoefficient * (x - highState);
        x = x + highAmount * (highState - x);

        // 低域を削る：**ハイパスへ寄せていく**（`x − lowpass`）。
        // 入れる`x`は上で高域を削った後のもの——順番は問いません（どちらも1倍以下）
        lowState += lowCoefficient * (x - lowState);
        x = x + lowAmount * ((x - lowState) - x);

        return x;
    }

private:
    void updateCoefficients()
    {
        // 1極：`a = 1 − exp(−2π·f / fs)`。**`f`が高いほど1へ近づく**（＝素通し）
        const auto onePole = [this] (float frequency)
        {
            return 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi
                                      * frequency / (float) sampleRate);
        };

        highCoefficient = juce::jlimit (0.0001f, 1.0f, onePole (highFrequency));
        lowCoefficient = juce::jlimit (0.0001f, 1.0f, onePole (lowFrequency));
    }

    double sampleRate = 44100.0;

    float highFrequency = 6000.0f;
    float lowFrequency = 200.0f;
    float highAmount = 0.0f;
    float lowAmount = 0.0f;

    float highCoefficient = 0.5f;
    float lowCoefficient = 0.02f;

    float highState = 0.0f;
    float lowState = 0.0f;

    bool active = false;
};
