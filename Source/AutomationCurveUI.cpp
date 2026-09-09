#include "AutomationCurveUI.h"

#include "Utf8.h"

namespace AutomationCurveUI
{
    void appendCurve (juce::Path& path, float fromX, float fromY, float toX, float toY,
                       AutomationCurve curve, float amount)
    {
        if (curve == AutomationCurve::Step)
        {
            // 次の点に到達するまで前の値を保つ（値が階段状に変わる）
            path.lineTo (toX, fromY);
            path.lineTo (toX, toY);
            return;
        }

        // 直線で済むならそのまま引く（点の多いレーンで無駄に刻まない）
        const bool isStraight = (curve == AutomationCurve::Linear && std::abs (amount) < 0.001f);

        if (isStraight || toX <= fromX)
        {
            path.lineTo (toX, toY);
            return;
        }

        // 形が分かる程度に刻んで描く。**刻みは幅から決める**
        // （拡大したときだけ細かくなればよい）
        const int steps = juce::jlimit (2, 64, (int) ((toX - fromX) / 3.0f));

        for (int s = 1; s <= steps; ++s)
        {
            const float t = (float) s / (float) steps;
            const float shaped = applyAutomationCurve (curve, t, amount);

            path.lineTo (fromX + (toX - fromX) * t, fromY + (toY - fromY) * shaped);
        }
    }

    bool segmentHasHandle (float fromX, float fromY, float toX, float toY, AutomationCurve curve)
    {
        if (curve == AutomationCurve::Step)
            return false;

        if (toX - fromX < minSegmentWidthForHandle)
            return false;

        // **水平な区間は曲げられない。** 値が同じなら、途中の係数を変えても
        // 高さが変わらない（`from + (to - from) * shaped`のto-fromが0）。
        // つまみだけ出しても動かないので、はじめから出さない
        return std::abs (toY - fromY) >= 1.0f;
    }

    juce::Point<float> getHandlePosition (float fromX, float fromY, float toX, float toY,
                                           AutomationCurve curve, float amount)
    {
        const float shaped = applyAutomationCurve (curve, 0.5f, amount);

        return { (fromX + toX) * 0.5f, fromY + (toY - fromY) * shaped };
    }

    void drawHandle (juce::Graphics& g, juce::Point<float> position, juce::Colour colour,
                      bool isBeingDragged)
    {
        const juce::Rectangle<float> bounds (position.x - handleRadius, position.y - handleRadius,
                                              handleRadius * 2.0f, handleRadius * 2.0f);

        // **中を塗らない。** 点そのもの（塗り潰した丸）と見分けられるようにしてある
        g.setColour (colour.withAlpha (isBeingDragged ? 0.35f : 0.15f));
        g.fillEllipse (bounds);

        g.setColour (colour.withAlpha (isBeingDragged ? 1.0f : 0.7f));
        g.drawEllipse (bounds, isBeingDragged ? 2.0f : 1.2f);
    }

    float amountForHandleDrag (float fromY, float toY, float newY)
    {
        const float span = toY - fromY;

        if (std::abs (span) < 1.0f)
            return 0.0f;   // 水平な区間（つまみも出していない）

        // 0＝手前の点、1＝次の点、という高さに直してから逆算する
        return automationCurveAmountFromDrag (0.5f, (newY - fromY) / span);
    }

    //==========================================================================
    void addCurveItems (juce::PopupMenu& menu, AutomationCurve currentCurve, float currentAmount)
    {
        const bool isBent = (std::abs (currentAmount) >= 0.001f);
        const bool isStraight = (currentCurve == AutomationCurve::Linear && ! isBent);

        menu.addSectionHeader (utf8 ("ここから次の点まで"));

        // 曲がっているときは、ここが**戻り道**になる（曲がり具合も0へ）
        menu.addItem (linearItemId,
                       isBent && currentCurve == AutomationCurve::Linear ? utf8 ("直線に戻す")
                                                                         : utf8 ("直線"),
                       true, isStraight);

        menu.addItem (easeItemId, utf8 ("曲線（S字）"), true, currentCurve == AutomationCurve::Ease);
        menu.addItem (stepItemId, utf8 ("ステップ"),   true, currentCurve == AutomationCurve::Step);
    }

    bool isCurveItem (int itemId)
    {
        return itemId >= linearItemId && itemId <= stepItemId;
    }

    AutomationCurve curveForItem (int itemId)
    {
        if (itemId == easeItemId) return AutomationCurve::Ease;
        if (itemId == stepItemId) return AutomationCurve::Step;

        return AutomationCurve::Linear;
    }

    float amountForItem (int itemId, float currentAmount)
    {
        // **直線を選んだら曲がり具合も0へ。** 種別だけ戻しても、
        // 曲がったまま残っていては「直線」を選んだ意味がない
        return itemId == linearItemId ? 0.0f : currentAmount;
    }
}
