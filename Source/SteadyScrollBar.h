#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    8.327：**掴んでいるあいだは、全体の長さを変えないスクロールバー**（Phase 317／本人の報告）。

    > 「Arrangeの横スクロールバーで横スクロールするときだけ、カクカクする気がする」

    ### 何が起きていたか

    アレンジ画面とピアノロールの横は、**右へどこまでも行ける**ように、全体の長さを
    「いまの位置＋画面2つぶん」で作っています（8.162）。スクロールするたびに全体が伸びます。

    JUCEのスクロールバーは、つまみのドラッグを

    ```
    掴んだときの位置 ＋ 動かしたpx × (全体 − 見えている幅) ÷ (つまみが動ける幅)
    ```

    で秒に直します。**掴んでいるあいだに全体が伸びると、この「1pxあたりの秒」が
    マウスが動くたびに変わり**、つまみの大きさも変わります。同じだけ動かしても、
    画面の動きが揃いません（8pxごとに0.173〜0.230秒）。ホイールはこの式を通らないので滑らかでした。

    ### やっていること

    **掴んでいるあいだは`setRangeLimits()`を受け付けません。** 離したら`onReleased`を呼ぶので、
    持ち主はそこで全体を作り直します（そこで初めて、さらに右へ行けるようになります）。

    一度のドラッグで行ける先は「掴んだときの全体の右端」までです。離してもう一度掴めば、その先へ行けます。

    **全体の長さを変えるときは、`setRangeLimits()`ではなく`setRangeLimitsUnlessHeld()`を通すこと。**
*/
class SteadyScrollBar : public juce::ScrollBar
{
public:
    using juce::ScrollBar::ScrollBar;

    /** 掴まれていなければ全体の長さを変える。掴まれているあいだは何もしない。 */
    void setRangeLimitsUnlessHeld (juce::Range<double> newLimits)
    {
        if (! held)
            setRangeLimits (newLimits, juce::dontSendNotification);
    }

    bool isHeld() const { return held; }

    /** 離したとき。持ち主はここで全体の長さを作り直す。 */
    std::function<void()> onReleased;

    void mouseDown (const juce::MouseEvent& e) override
    {
        // つまみの外（ページ送り）でも固めます。押しっぱなしで送り続けるあいだに
        // 全体が伸びると、送る幅が毎回変わるので
        held = true;
        juce::ScrollBar::mouseDown (e);
        handleUpdateNowIfNeeded();
    }

    /** 8.327：**動かしたその場で、持ち主へ知らせる**（Phase 317。本人の2度目の報告）。

        JUCEのスクロールバーは、動いたことを**後回しで**知らせます（`triggerAsyncUpdate()`）。
        つまみ（こちら）はすぐ描き直されるのに、中身が動くのはメッセージループを1周したあと——
        **つまみと中身が1コマずつずれて交互に描かれ**、ガタついて見えます。
        ホイールは持ち主が直接動かすので、このずれがありませんでした。

        **ここで流してしまえば、つまみと中身は同じ出来事の中で動きます**
        （描き直しは1回にまとまる）。 */
    void mouseDrag (const juce::MouseEvent& e) override
    {
        juce::ScrollBar::mouseDrag (e);
        handleUpdateNowIfNeeded();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::ScrollBar::mouseUp (e);

        // **最後の知らせを先に流す。** 知らせは非同期なので、流さずに作り直すと
        // 離す直前の位置がまだ持ち主へ届いていません
        handleUpdateNowIfNeeded();

        held = false;

        if (onReleased != nullptr)
            onReleased();
    }

private:
    bool held = false;
};
