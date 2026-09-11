#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

//==============================================================================
/**
    8.220：**ディフュージョン**（Phase 246／ディレイ仕様書5-5・設計書4-3）。

    反復を**滲ませて**、リバーブに近づけるものです。

    ─────────────────────────────────────────────────────────────────────────
    ライト版であること（設計書4-3）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**固定係数のSchroeder型オールパスを4段直列に並べるのみ。
    パラメータで可変にはしない**（★4→★3へ圧縮したぶん）。

    落としたもの：**滲みの質感そのものを細かく調整する自由度**。
    触れるのは`Diffuse`（混ぜ具合）1つだけです。

    ─────────────────────────────────────────────────────────────────────────
    オールパスは「音を変えない」——**時間だけずらす**
    ─────────────────────────────────────────────────────────────────────────

    振幅特性は平ら（だからオールパス）で、変わるのは**位相**だけです。
    それを4段重ねると、**1つの音が短い尾を引いて広がります。**

    > **一周の利得は1のままです**（8.209・8.210・8.218で3回引いた線）。
    > オールパスは**増やしも減らしもしない**ので、
    > フィードバックの中に置いても`feedback`の上限がそのまま効きます。
    > **これがオールパスを選ぶ理由**でもあります。

    ─────────────────────────────────────────────────────────────────────────
    段の長さは**互いに素**に近い数字
    ─────────────────────────────────────────────────────────────────────────

    倍数どうしにすると**同じところで山と谷が重なって、金属的に響きます。**
    素数に近いミリ秒を選んであります。
*/
class MantaDelayDiffuser
{
public:
    /** 段の数（設計書4-3の「4段」）。 */
    static constexpr int numStages = 4;

    /** 各段の長さ（ミリ秒）。**互いに素に近いこと**（上の説明）。

        8.223：**31.6ms→92.6msへ伸ばしました**（Phase 247／本人の報告
        「馴染む感覚はあるけど、変化は少ない」）。

        はじめは`4.7 / 6.7 / 8.9 / 11.3`で、合計31.6msでした。
        **これは「厚み」であって「滲み」ではない長さ**です——
        リバーブらしく聞こえるには50〜150msくらい要ります。 */
    static constexpr double stageMs[numStages] = { 10.1, 17.3, 26.9, 38.3 };

    /** 8.223：**左チャンネルに対する右の倍率**（Phase 247）。

        左右で**同じ長さ**にすると、滲みが真ん中に固まって広がりません
        （左右がそっくり同じ形に濁るため）。**少しずらすだけで、横へ開きます。**

        倍数にならない半端な数字であること——`1.5`のようにすると、
        右の1段目が左の2段目と重なります。 */
    static constexpr double rightChannelRatio = 1.19;

    /** オールパスの係数。**固定**（設計書4-3）。

        8.223：`0.6`→`0.68`（Phase 247）。**振幅はどの値でも1倍のまま**なので
        （オールパスなので）、上げても**一周の利得は増えません**——変わるのは
        1段あたりの尾の長さだけです。`0.75`を超えると、段の長さが離散的に聞こえてきます。 */
    static constexpr float coefficient = 0.68f;

    /** `channel`が0以外なら、段を少し長くします（上の`rightChannelRatio`）。 */
    void prepare (double sampleRateToUse, int channel)
    {
        const double sampleRate = juce::jmax (8000.0, sampleRateToUse);
        const double ratio = (channel == 0) ? 1.0 : rightChannelRatio;

        for (int stage = 0; stage < numStages; ++stage)
        {
            auto& line = stages[(size_t) stage];

            line.length = juce::jmax (1, (int) std::round (stageMs[stage] * ratio * 0.001 * sampleRate));
            line.buffer.assign ((size_t) line.length, 0.0f);
            line.index = 0;
        }
    }

    void reset()
    {
        for (auto& line : stages)
        {
            std::fill (line.buffer.begin(), line.buffer.end(), 0.0f);
            line.index = 0;
        }
    }

    /** ブロックの頭で1回。`newAmount`は0〜1。 */
    void setAmount (float newAmount)
    {
        amount = juce::jlimit (0.0f, 1.0f, newAmount);
        active = amount > 0.0001f;
    }

    bool isActive() const { return active; }

    /** 1サンプルぶん。**`active`が`false`なら素通し**（段を回しません）。 */
    float processSample (float input)
    {
        if (! active)
            return input;

        float x = input;

        for (auto& line : stages)
        {
            // Schroeder型オールパス：`v = x + g·buf`、`y = buf − g·v`、`buf ← v`
            const float delayed = line.buffer[(size_t) line.index];
            const float v = x + coefficient * delayed;

            line.buffer[(size_t) line.index] = v;

            if (++line.index >= line.length)
                line.index = 0;

            x = delayed - coefficient * v;
        }

        // **混ぜ具合だけがつまみ**（設計書4-3の「オン/オフ、またはミックス量のみ」）
        return input * (1.0f - amount) + x * amount;
    }

private:
    struct Stage
    {
        std::vector<float> buffer;
        int length = 1;
        int index = 0;
    };

    std::array<Stage, (size_t) numStages> stages;

    float amount = 0.0f;
    bool active = false;
};
