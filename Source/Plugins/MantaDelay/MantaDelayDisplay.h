#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaDelayParameters.h"
#include "MantaDelayTheme.h"

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
    };

    void setState (const State& newState)
    {
        // **変わったときだけ描き直す。** タイマーで毎回呼ばれるので、
        // そのまま`repaint()`すると止まっていても描き続けます
        if (juce::approximatelyEqual (state.delaySeconds, newState.delaySeconds)
             && juce::approximatelyEqual (state.feedback, newState.feedback)
             && juce::approximatelyEqual (state.mix, newState.mix))
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
        // 何回ぶん描くか。**聞こえなくなるところまで**

        constexpr float audibleFloor = 0.02f;   // これ以下は描かない（-34dBくらい）
        constexpr int maxTaps = 24;

        int numTaps = 0;
        float level = 1.0f;

        for (int i = 0; i < maxTaps; ++i)
        {
            level *= state.feedback;

            if (level < audibleFloor)
                break;

            ++numTaps;
        }

        // **原音のぶんを足して、横幅を決めます。** 反復が0回でも
        // 目盛りが潰れないよう、最低でも4つぶんの幅を取ります
        const double spanSeconds = state.delaySeconds * juce::jmax (4, numTaps + 1);

        const auto timeToX = [&] (double seconds)
        {
            return (float) plot.getX()
                    + (float) (seconds / spanSeconds) * (float) plot.getWidth();
        };

        //----------------------------------------------------------------------
        // 目盛り（Timeの倍数ごと）

        g.setColour (MantaTheme::grid());

        for (int i = 1; i <= juce::jmax (4, numTaps + 1); ++i)
        {
            const float x = timeToX (state.delaySeconds * i);

            if (x > (float) plot.getRight())
                break;

            g.drawVerticalLine ((int) x, (float) plot.getY(), (float) baseline);
        }

        g.setColour (MantaTheme::gridStrong());
        g.drawHorizontalLine (baseline, (float) plot.getX(), (float) plot.getRight());

        //----------------------------------------------------------------------
        // 原音と反復

        const auto drawTap = [&] (double seconds, float amplitude, juce::Colour colour)
        {
            const float x = timeToX (seconds);

            if (x < (float) plot.getX() - 1.0f || x > (float) plot.getRight())
                return;

            const float height = juce::jmax (2.0f, amplitude * (float) plot.getHeight());

            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (x - 1.5f, (float) baseline - height, 3.0f, height));
        };

        // **原音はMixで決まります**（Wetが1.0なら原音は消える）。
        // 「Mixを上げたのに原音がそのまま」に見えると、何が起きているか読めません
        drawTap (0.0, 1.0f - state.mix, MantaTheme::textDim());

        level = state.mix;

        for (int i = 1; i <= numTaps; ++i)
        {
            level *= state.feedback;
            drawTap (state.delaySeconds * i, level, MantaDelayTheme::accent());
        }

        g.setColour (MantaTheme::border());
        g.drawRect (area, 1);
    }

private:
    State state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayDisplay)
};
