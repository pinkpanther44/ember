#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaDelayTheme.h"
#include "../MantaTheme.h"

#include <cmath>

//==============================================================================
/**
    8.212：**ダッキングの効き具合を出す細い帯**（Phase 242）。

    左から右へ伸びるのが「いま絞っているぶん」です。

    ```
        ├███████░░░░░░░░░░░░░░░░┤     ← 4割ほど絞っている
    ```

    ### なぜ要るのか

    **絞られたぶんは「音が小さい」だけ**なので、耳では掛かり具合が読めません。
    Attackを50msにしたのか5msにしたのかは、**帯の動き出しの速さ**で見えます。

    ### 押せません

    `MantaDelayDisplay`と同じ`setInterceptsMouseClicks (false, false)`。
    **押しても何も起きないものを、押せそうに見せない**（8.161）。

    ### 描き直すのは変わったときだけ

    タイマーで毎回呼ばれるので、そのまま`repaint()`すると
    **止まっていても描き続けます。** 1%未満の差は捨てます——
    音が止まっていても包絡は完全な0にはならず、
    **下の桁が動き続けるだけで描き直し続けることになります。**
*/
class MantaDelayDuckMeter : public juce::Component
{
public:
    MantaDelayDuckMeter()
    {
        setInterceptsMouseClicks (false, false);
    }

    /** `reduction`は0〜1（0＝絞っていない）。`active`が`false`なら灰色のまま。 */
    void setReduction (float reduction, bool active)
    {
        const float clamped = juce::jlimit (0.0f, 1.0f, reduction);

        if (active == isActive && std::abs (clamped - value) < 0.01f)
            return;

        value = clamped;
        isActive = active;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        const float radius = area.getHeight() * 0.5f;

        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (area, radius);

        if (isActive && value > 0.0f)
        {
            auto filled = area.withWidth (juce::jmax (area.getHeight(), area.getWidth() * value));

            // **副の色**（Mix・Wow・Flutterと同じ「原音との関わり」の側。8.204の表）
            g.setColour (MantaDelayTheme::highlight());
            g.fillRoundedRectangle (filled, radius);
        }

        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    }

private:
    float value = 0.0f;
    bool isActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayDuckMeter)
};
