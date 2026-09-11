#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaDelayParameters.h"
#include "MantaDelayTaps.h"     // 8.214：タップの並び（Phase 243）
#include "MantaDelayTheme.h"

#include <cmath>

//==============================================================================
/**
    8.206：**簡易タイムライン表示**（Phase 238／ディレイ設計書4-6・5章）。

    ─────────────────────────────────────────────────────────────────────────
    何を描くか
    ─────────────────────────────────────────────────────────────────────────

    **反復が、いつ、どのくらいの大きさで返ってくるか**だけです。

    ```
        │█                                        原音
        │    ▌                                    1回目
        │        ▖                                2回目
        │          ·                              3回目 …
        └────────────────────────────────────────
         0        Time      2×Time    3×Time
    ```

    ─────────────────────────────────────────────────────────────────────────
    ドラッグできません——**それでよい**
    ─────────────────────────────────────────────────────────────────────────

    設計書5章の決定：**Timeless 3型のフル対話表示は★4〜★5**なので、
    今回は**静的な表示から始める**。値はつまみで変えます。

    掴めるように見せないため、`setInterceptsMouseClicks (false, false)`。
    **押しても何も起きないものを、押せそうに見せない**（8.161）。

    ─────────────────────────────────────────────────────────────────────────
    横の目盛り
    ─────────────────────────────────────────────────────────────────────────

    **いちばん最後に見える反復まで**を画面いっぱいに収めます。
    固定の秒数にすると、Timeを短くしたときに**左端に全部固まって読めません。**
*/
class MantaDelayDisplay : public juce::Component
{
public:
    MantaDelayDisplay()
    {
        setInterceptsMouseClicks (false, false);
    }

    /** 画面から渡される、いま鳴っている状態。 */
    struct State
    {
        double delaySeconds = 0.375;
        float  feedback     = 0.35f;
        float  mix          = 0.30f;

        /** 8.214：タップの並び（Phase 243）。1本なら**Phase 3までと同じ絵**になります。 */
        MantaDelayTaps::Pattern taps;

        /** つまみが指しているタップ（0起点）。**その棒だけ色を変えます**——
            どれを直しているのかが、つまみの側からは分からないため。 */
        int selectedTap = 0;
    };

    void setState (const State& newState)
    {
        // **変わったときだけ描き直す。** タイマーで毎回呼ばれるので、
        // そのまま`repaint()`すると止まっていても描き続けます
        if (juce::approximatelyEqual (state.delaySeconds, newState.delaySeconds)
             && juce::approximatelyEqual (state.feedback, newState.feedback)
             && juce::approximatelyEqual (state.mix, newState.mix)
             && state.selectedTap == newState.selectedTap
             && ! tapsChanged (newState.taps))
            return;

        state = newState;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        g.setColour (MantaTheme::graphBackground());
        g.fillRect (area);

        auto plot = area.reduced (14, 12);

        if (plot.getWidth() <= 20 || plot.getHeight() <= 20 || state.delaySeconds <= 0.0)
        {
            g.setColour (MantaTheme::border());
            g.drawRect (area, 1);
            return;
        }

        const int baseline = plot.getBottom();

        //----------------------------------------------------------------------
        // 8.214：**何周ぶん描くか**（Phase 243）。**聞こえなくなるところまで**。
        //
        // Phase 3までは「反復1回＝1本」でしたが、いまは**1周ごとにパターンが1組**です。
        // 周回そのものはPhase 3までと同じで、`feedback`が1回ぶん掛かります

        constexpr float audibleFloor = 0.02f;   // これ以下は描かない（-34dBくらい）
        constexpr int maxPasses = 24;

        const int activeTaps = juce::jlimit (1, MantaDelayTaps::maxTaps, state.taps.count);
        const int patternSteps = state.taps.getPatternSteps();

        int numPasses = 0;
        float level = 1.0f;

        for (int i = 0; i < maxPasses; ++i)
        {
            level *= state.feedback;

            if (level < audibleFloor)
                break;

            ++numPasses;
        }

        // **いちばん後ろに見える棒までを入れます。** 反復が0回でも
        // 目盛りが潰れないよう、最低でも4つぶんの幅を取ります
        const int lastStep = numPasses * patternSteps + patternSteps;
        const int spanSteps = juce::jmax (4, lastStep);

        const double spanSeconds = state.delaySeconds * spanSteps;

        const auto timeToX = [&] (double seconds)
        {
            return (float) plot.getX()
                    + (float) (seconds / spanSeconds) * (float) plot.getWidth();
        };

        //----------------------------------------------------------------------
        // 目盛り（Timeの倍数ごと）。**周の変わり目は濃く**——
        // マルチタップのときに「ここからもう1周」が読めるように

        for (int i = 1; i <= spanSteps; ++i)
        {
            const float x = timeToX (state.delaySeconds * i);

            if (x > (float) plot.getRight())
                break;

            const bool isPatternEdge = ! state.taps.isSingleTap() && (i % patternSteps) == 0;

            g.setColour (isPatternEdge ? MantaTheme::gridStrong() : MantaTheme::grid());
            g.drawVerticalLine ((int) x, (float) plot.getY(), (float) baseline);
        }

        g.setColour (MantaTheme::gridStrong());
        g.drawHorizontalLine (baseline, (float) plot.getX(), (float) plot.getRight());

        //----------------------------------------------------------------------
        // 8.214：原音とタップ（Phase 243）。
        //
        // **棒は左右2本**です。Panが真ん中なら高さが揃うので、
        // **Phase 3までの3pxの1本とまったく同じに見えます**——
        // 振ったときにだけ、片側が低くなって見えます

        const auto drawBar = [&] (double seconds, float leftAmplitude, float rightAmplitude,
                                   juce::Colour colour)
        {
            const float x = timeToX (seconds);

            if (x < (float) plot.getX() - 1.0f || x > (float) plot.getRight())
                return;

            g.setColour (colour);

            const auto half = [&] (float offset, float amplitude)
            {
                if (amplitude <= 0.0f)
                    return;

                const float height = juce::jmax (2.0f, amplitude * (float) plot.getHeight());

                g.fillRect (juce::Rectangle<float> (x + offset, (float) baseline - height, 1.5f, height));
            };

            half (-1.5f, leftAmplitude);
            half (0.0f, rightAmplitude);
        };

        // **原音はMixで決まります**（Wetが1.0なら原音は消える）。
        // 「Mixを上げたのに原音がそのまま」に見えると、何が起きているか読めません
        drawBar (0.0, 1.0f - state.mix, 1.0f - state.mix, MantaTheme::textDim());

        level = state.mix;

        for (int pass = 0; pass <= numPasses; ++pass)
        {
            level *= state.feedback;

            for (int tap = 0; tap < activeTaps; ++tap)
            {
                const auto& values = state.taps.taps[(size_t) tap];

                const int step = juce::jlimit (1, MantaDelayTaps::maxStep, values.step);
                const double seconds = state.delaySeconds * (pass * patternSteps + step);

                float left = 1.0f, right = 1.0f;
                MantaDelayTaps::getPanGains (values.pan, left, right);

                const float amplitude = level * juce::jlimit (0.0f, 1.0f, values.level);

                // **選んでいるタップだけ副の色**（1周目にだけ出します——
                // 全部の周で色を変えると、どれが1本目か分からなくなります）
                const bool highlight = pass == 0 && tap == state.selectedTap
                                        && ! state.taps.isSingleTap();

                drawBar (seconds, amplitude * left, amplitude * right,
                          highlight ? MantaDelayTheme::highlight() : MantaDelayTheme::accent());
            }
        }

        g.setColour (MantaTheme::border());
        g.drawRect (area, 1);
    }

private:
    /** 8.214：タップの並びが動いたか（Phase 243）。 */
    bool tapsChanged (const MantaDelayTaps::Pattern& other) const
    {
        if (other.count != state.taps.count)
            return true;

        for (size_t i = 0; i < (size_t) MantaDelayTaps::maxTaps; ++i)
        {
            const auto& a = state.taps.taps[i];
            const auto& b = other.taps[i];

            if (a.step != b.step
                 || std::abs (a.level - b.level) > 0.002f
                 || std::abs (a.pan - b.pan) > 0.002f)
                return true;
        }

        return false;
    }

    State state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayDisplay)
};
