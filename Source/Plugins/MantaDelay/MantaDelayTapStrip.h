#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaDelayTaps.h"
#include "MantaDelayTheme.h"
#include "../MantaTheme.h"

#include <functional>

//==============================================================================
/**
    8.215：**タップの一覧**（Phase 243）。

    ```
        ┌────┬────┬────┬────┬────┬────┬────┬────┐
        │ ▇  │  ▅ │ ▂  │    │    │    │    │    │   ← Levelの高さ、横の位置がPan
        │ 1  │ 3  │ 5  │ 4  │ 5  │ 6  │ 7  │ 8  │   ← step
        └────┴────┴────┴────┴────┴────┴────┴────┘
          ↑選んでいるもの        └── 本数の外（灰色）
    ```

    ─────────────────────────────────────────────────────────────────────────
    なぜ「1本ずつ選んで直す」形なのか
    ─────────────────────────────────────────────────────────────────────────

    8本 × 3つ＝24個のつまみを並べる場所はありません
    （**画面は固定**。8.172）。**Manta EQと同じ形**にしました——
    あちらも12バンド × 12個を「選んでいるバンドのつまみだけ出す」で捌いています。

    だから**全体が見える窓が要ります。** つまみは1本ぶんしか映さないので、
    これが無いと**いま何本鳴っていて、どこに置いたのか**が読めません。

    ─────────────────────────────────────────────────────────────────────────
    押せるのは「選ぶ」だけ
    ─────────────────────────────────────────────────────────────────────────

    **ドラッグで値は変わりません。** 値はつまみで変えます
    （設計書4-6の方針をそのまま当てています）。

    `MantaDelayDisplay`が`setInterceptsMouseClicks (false, false)`なのとは逆で、
    こちらは**押せます**——**押すと何かが起きるからです**（8.161の裏返し）。
*/
class MantaDelayTapStrip : public juce::Component
{
public:
    MantaDelayTapStrip()
    {
        setWantsKeyboardFocus (false);
    }

    /** 押されたら呼ばれます（引数は0起点のタップ番号）。 */
    std::function<void (int)> onTapSelected;

    void setPattern (const MantaDelayTaps::Pattern& newPattern, int newSelected)
    {
        // **変わったときだけ描き直す**（タイマーで毎回呼ばれます）
        if (newSelected == selected && ! hasChanged (newPattern))
            return;

        pattern = newPattern;
        selected = newSelected;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        const int active = juce::jlimit (1, MantaDelayTaps::maxTaps, pattern.count);
        const int cellWidth = area.getWidth() / MantaDelayTaps::maxTaps;

        if (cellWidth <= 4 || area.getHeight() <= 20)
            return;

        for (int tap = 0; tap < MantaDelayTaps::maxTaps; ++tap)
        {
            auto cell = juce::Rectangle<int> (area.getX() + tap * cellWidth, area.getY(),
                                               cellWidth, area.getHeight()).reduced (2, 0);

            const bool isActive = tap < active;
            const bool isSelected = tap == selected;

            g.setColour (MantaTheme::graphBackground().withAlpha (isActive ? 1.0f : 0.45f));
            g.fillRoundedRectangle (cell.toFloat(), 3.0f);

            //------------------------------------------------------------------
            // 下の行：step（「Timeつまみの何個ぶん後ろか」）

            auto numberRow = cell.removeFromBottom (13);

            const auto& tapValues = pattern.taps[(size_t) tap];

            g.setColour ((isActive ? MantaTheme::text() : MantaTheme::textDim())
                            .withAlpha (isActive ? 0.9f : 0.4f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (juce::String (tapValues.step), numberRow, juce::Justification::centred);

            //------------------------------------------------------------------
            // 上：Levelの高さ、横の位置がPan

            auto plot = cell.reduced (4, 3);

            if (isActive && plot.getHeight() > 6)
            {
                const float level = juce::jlimit (0.0f, 1.0f, tapValues.level);
                const float pan = juce::jlimit (-1.0f, 1.0f, tapValues.pan);

                // **Levelが0でも細い棒を残します**——消してしまうと
                // 「そこにタップがある」ことまで見えなくなります
                const float height = juce::jmax (2.0f, level * (float) plot.getHeight());

                const float centreX = (float) plot.getCentreX()
                                        + pan * (float) plot.getWidth() * 0.5f;

                juce::Rectangle<float> bar (centreX - 2.0f, (float) plot.getBottom() - height,
                                             4.0f, height);

                g.setColour (MantaDelayTheme::accent().withAlpha (level > 0.0f ? 1.0f : 0.35f));
                g.fillRoundedRectangle (bar, 1.5f);

                // 真ん中の目印（Panが振れているかが一目で分かる）
                g.setColour (MantaTheme::grid());
                g.drawVerticalLine (plot.getCentreX(), (float) plot.getBottom() - 3.0f,
                                     (float) plot.getBottom());
            }

            //------------------------------------------------------------------
            // 選んでいるものの枠

            if (isSelected)
            {
                g.setColour (MantaDelayTheme::highlight());
                g.drawRoundedRectangle (juce::Rectangle<int> (area.getX() + tap * cellWidth, area.getY(),
                                                               cellWidth, area.getHeight())
                                            .reduced (2, 0).toFloat().reduced (0.5f), 3.0f, 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const int cellWidth = getWidth() / MantaDelayTaps::maxTaps;

        if (cellWidth <= 0)
            return;

        const int tap = juce::jlimit (0, MantaDelayTaps::maxTaps - 1, event.x / cellWidth);

        if (onTapSelected != nullptr)
            onTapSelected (tap);
    }

private:
    bool hasChanged (const MantaDelayTaps::Pattern& other) const
    {
        if (other.count != pattern.count)
            return true;

        for (size_t i = 0; i < (size_t) MantaDelayTaps::maxTaps; ++i)
        {
            const auto& a = pattern.taps[i];
            const auto& b = other.taps[i];

            if (a.step != b.step
                 || std::abs (a.level - b.level) > 0.002f
                 || std::abs (a.pan - b.pan) > 0.002f)
                return true;
        }

        return false;
    }

    MantaDelayTaps::Pattern pattern;
    int selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayTapStrip)
};
