#pragma once

#include "MantaReverbAlgorithm.h"
#include "MantaReverbDelayLine.h"

#include <array>
#include <cmath>

//==============================================================================
/**
    8.240：**初期反射（Early Reflections）**
    （Phase 254／リバーブ仕様書2-2・3-2）。

    壁や天井から**最初に返ってくる数十発**です。テール（後部残響）が
    「どのくらい広いか」を伝えるのに対して、初期反射は
    **「どんな形の部屋か」「音源はどのくらい近いか」**を伝えます。

    ```
        入力 → ┬→ 8.3ms ×+0.84 ┐
               ├→ 14.7ms ×−0.72 ┤
               ├→ 21.1ms ×+0.61 ┼→ 出力
               └→ …             ┘
    ```

    ### 1本の線を8回読む

    タップごとに線を持つと、**同じ音を8本ぶん書くことになります**。
    `push()`で1回書いて、`readAt()`で8回読むほうが、書く量が1/8で済みます
    （ディレイのマルチタップと同じ形。8.214）。

    ### 8.243：パターンは**アルゴリズムが持っています**（Phase 255）

    数字は`MantaReverbAlgorithm`の表です。ここはそれを読むだけ——
    **左右の違いも、符号の交互も、向こうの表の説明にあります。**

    ### 大きさで割ること

    8本を足すと、素のままでは最大4倍近くになります。
    **二乗和の平方根で割ります**（`normalisation`）——これは
    「ばらばらの時刻に届くものを足したときの大きさ」に合った割り方で、
    最大値で割ると今度は小さくなりすぎます。

    > **パターンごとに割り直すこと。** Room と Hall で係数の合計が違うので、
    > 固定の数字にすると**アルゴリズムを変えただけで音量が動きます。**
*/
class MantaReverbEarly
{
public:
    static constexpr int numTaps = MantaReverbAlgorithm::numEarlyTaps;

    /** `MantaReverbFdn::maxSizeScale`と同じであること（確保の長さが決まります）。 */
    static constexpr double maxSizeScale = 2.0;

    /** `channel`が0なら左のパターン、それ以外は右を読みます。 */
    void prepare (double sampleRateToUse, int channelToUse)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);
        channel = channelToUse;

        // **確保はいちばん長いパターン×いちばん大きいSize×いちばん広いSpread**（9.4）。
        // 8.246：`Spread`のぶんを掛け忘れると、**広げきったところで頭打ち**になります
        // （落ちはしません——`setLength()`が中へ丸めるので、**黙って狭くなります**）
        line.prepare (juce::jmax (1, (int) std::ceil (MantaReverbAlgorithm::getLongestEarlyMs()
                                                        * maxSizeScale
                                                        * MantaReverbAlgorithm::maxSpreadScale
                                                        * 0.001 * sampleRate)));

        sizeScale = 0.0f;   // `setSize()`が必ず1度は走るように
        setPattern (MantaReverbAlgorithm::roomEarly);
        setSize (1.0f);
        reset();
    }

    void reset() { line.reset(); }

    void setPattern (const MantaReverbAlgorithm::EarlyPattern& newPattern)
    {
        const auto* wanted = (channel == 0) ? &newPattern.left : &newPattern.right;

        if (wanted == taps)
            return;

        taps = wanted;
        updateTaps();
    }

    void setSize (float scale)
    {
        const float wanted = juce::jlimit (0.25f, (float) maxSizeScale, scale);

        if (wanted == sizeScale)
            return;

        sizeScale = wanted;
        updateTaps();
    }

    /** 8.246：`Shape`と`Spread`（Phase 256）。**どちらも0.5が「表のまま」**。 */
    void setShapeAndSpread (float newShape, float newSpread)
    {
        const float wantedShape = juce::jlimit (0.0f, 1.0f, newShape);
        const float wantedSpread = juce::jlimit (0.0f, 1.0f, newSpread);

        if (wantedShape == shape && wantedSpread == spread)
            return;

        shape = wantedShape;
        spread = wantedSpread;
        updateTaps();
    }

    float processSample (float input)
    {
        line.push (input);

        float sum = 0.0f;

        for (int tap = 0; tap < numTaps; ++tap)
            sum += tapGains[(size_t) tap] * line.readAt (tapSamples[(size_t) tap]);

        return sum;
    }

private:
    /** 位置と大きさを出し直す。**`Size`・`Spread`・`Shape`・パターンのどれが
        変わっても、ここ1つを通します**（1.27）——別々に書くと、
        片方だけ掛け忘れて「Shapeを回すと音量が変わる」ような状態になります。 */
    void updateTaps()
    {
        if (taps == nullptr || sizeScale <= 0.0f)
            return;

        const double timeScale = sizeScale * MantaReverbAlgorithm::spreadScaleFor (spread);
        const float slope = MantaReverbAlgorithm::shapeSlopeFor (shape);

        double sumOfSquares = 0.0;

        for (int tap = 0; tap < numTaps; ++tap)
        {
            tapSamples[(size_t) tap] = juce::jmax (1, (int) std::round (
                (*taps)[(size_t) tap].milliseconds * timeScale * 0.001 * sampleRate));

            // 8.246：**山の位置を傾ける。** 0.5なら`slope`が0で、重みは全部1倍
            const float position = (float) tap / (float) (numTaps - 1);
            const float weight = std::exp (slope * (position - 0.5f));

            tapGains[(size_t) tap] = (*taps)[(size_t) tap].gain * weight;
            sumOfSquares += (double) tapGains[(size_t) tap] * tapGains[(size_t) tap];
        }

        // **傾けたら、割り直すこと。** これが無いと`Shape`を回すだけで音量が動き、
        // 「立ち上がりが変わった」のか「大きくなった」のか聞き分けられません
        const float normalisation = (float) (1.0 / std::sqrt (juce::jmax (1.0e-6, sumOfSquares)));

        for (auto& gain : tapGains)
            gain *= normalisation;

        // **線そのものも縮めること。** 長さを残したままだと、`Size`を縮めても
        // 確保した最大の長さで回り続けて、無駄に古い音を抱えます
        line.setLength (tapSamples[(size_t) (numTaps - 1)]);
    }

    MantaReverbDelayLine line;

    double sampleRate = 44100.0;
    int channel = 0;

    const std::array<MantaReverbAlgorithm::EarlyTap, (size_t) numTaps>* taps = nullptr;

    std::array<int, (size_t) numTaps> tapSamples { { 1, 1, 1, 1, 1, 1, 1, 1 } };

    /** 表の大きさ × `Shape`の重み ÷ 全体。**`processSample()`はこれを読むだけ。** */
    std::array<float, (size_t) numTaps> tapGains { { 0.0f } };

    float sizeScale = 0.0f;
    float shape = 0.5f;
    float spread = 0.5f;
};
