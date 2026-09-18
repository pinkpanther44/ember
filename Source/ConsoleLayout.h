#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppSettings.h"
#include "AppColours.h"   // 境目に引く線の色（直にjuce::Coloursを書かない。1.34）

//==============================================================================
/**
    8.283：**Consoleのストリップを、どの行も同じ高さに切る**（Phase 276／本人の要望）。

    ### 何が起きていたか

    本人の言葉：「Consoleウィンドウにて、「＋Insert」「＋Send」「ボリュームメーター」項目が
    増えるに従い各々の縦幅が伸縮する。また、Insertされているエフェクト数の違いから
    トラック間での見た目に違いが生まれる。これを解消したい。」

    ラックの高さが**そのトラックの中身で決まっていた**ためです：

        rackHeight = jmin (ラックが欲しい高さ, 残り - フェーダーのぶん)

    インサートが2つのトラックと4つのトラックでは、ラックの高さが違い、
    **そのぶんフェーダーとメーターの高さも違いました**。
    メーターは横に並べて見比べるものなので、**行がずれていると読み比べられません。**

    ### 決めたこと

    | | |
    |---|---|
    | ラックの高さ | **全ストリップ共通の1つの数字**（マスターも同じ） |
    | 入り切らないぶん | **スクロール**（`rackViewport`。前からそうなっています） |
    | 高さを変える | 境目をドラッグ。**変えた値は全ストリップへ同時に効きます** |
    | メーターとフェーダー | **絶対に`minimumFaderAreaHeight`を下回らせない** |

    最後の1つが本人の指定です（「ボリュームメーターに関しては、スクロールは発生せずに
    したいから最小サイズを設定しよう」）。**メーターは縮めてもスクロールさせません**
    ——縮んだメーターは読めず、スクロールするメーターは見た瞬間の比較ができないためです。
    ラックのほうは**スクロールできるので、削られてよい側**です。

    ### 値の置き場所

    **`AppSettings`**（設計書2.5）。ピアノロールのレーンの高さ（8.122）と同じ扱いで、
    「いまどう作業したいか」であって曲の内容ではないため、プロジェクトには保存しません。

    **読む側は`getRackAreaHeight()`だけを見ること。** ストリップが自分で覚えると、
    トラックを足したときに**新しい1本だけ既定の高さ**になります。
*/
namespace ConsoleLayout
{
    /** フェーダーとメーターに必ず残す高さ。**ここを下回らせない**のが目的なので、
        「メーターの目盛りが読める最小」から決めてあります（Phase 66からの値）。 */
    inline constexpr int minimumFaderAreaHeight = 130;

    /** ラックに最低限渡す高さ。**「+ Insert」の行が見える**ところまで。
        0にできるようにすると、**ラックを畳んだのか壊れたのか分からなくなります。** */
    inline constexpr int minimumRackAreaHeight = 40;

    /** 既定のラックの高さ。インサート3つ＋「+ Insert」がだいたい収まる高さです。 */
    inline constexpr int defaultRackAreaHeight = 150;

    /** 境目の掴みしろ（ラックとフェーダーのあいだ）。 */
    inline constexpr int resizerHeight = 6;

    inline const juce::String rackAreaHeightKey { "consoleRackAreaHeight" };

    /** いまのラックの高さ。**設定に無ければ既定**。 */
    inline int getRackAreaHeight()
    {
        return juce::jmax (minimumRackAreaHeight,
                            AppSettings::getInt (rackAreaHeightKey, defaultRackAreaHeight));
    }

    /** 覚える。**画面へ配るのは呼び出し側**（`ConsoleView`）の仕事です。 */
    inline void setRackAreaHeight (int newHeight)
    {
        AppSettings::setInt (rackAreaHeightKey, juce::jmax (minimumRackAreaHeight, newHeight));
    }

    /** そのストリップで実際に使えるラックの高さ。

        **メーターのぶんを必ず残します**（本人の指定）。
        パネルを縮めて`available`がとても小さいときは、
        **ラックのほうから削ります**——あちらはスクロールで届くためです。 */
    inline int getRackHeightFor (int availableHeight)
    {
        const int maxRack = availableHeight - minimumFaderAreaHeight;

        // **メーターのぶんすら無いとき**は、ラックを畳んで全部フェーダーへ回す
        if (maxRack < minimumRackAreaHeight)
            return juce::jmax (0, juce::jmin (maxRack, availableHeight / 3));

        return juce::jlimit (minimumRackAreaHeight, maxRack, getRackAreaHeight());
    }

    //==========================================================================
    /** ラックとフェーダーの境目。**ドラッグで高さを変えます。**

        **ストリップ本体のマウス処理に混ぜないこと。** あちらは
        「空きとトラック名を掴んで並べ替える」を持っているので、
        そこへ境目の判定を足すと、**並べ替えのつもりが高さが変わります**。 */
    class RackResizer : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        RackResizer()
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        /** 新しい高さが決まるたびに呼ばれる（ドラッグ中も）。 */
        std::function<void (int)> onHeightDragged;

        void mouseDown (const juce::MouseEvent& e) override
        {
            heightAtDragStart = getRackAreaHeight();
            screenYAtDragStart = e.getScreenPosition().y;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // **測るのは画面座標の差**（8.122・8.125と同じ話）。
            //
            // 高さを変えると**この部品自身が動きます**。部品の座標で測ると、
            // 動いたぶんが次の差し引きに入り、**指を止めても動き続ける**か、
            // **半分の速さでしか動かない**かのどちらかになります。
            if (onHeightDragged != nullptr)
                onHeightDragged (heightAtDragStart + (e.getScreenPosition().y - screenYAtDragStart));
        }

        void paint (juce::Graphics& g) override
        {
            // **掴めることが見えないと、誰も掴みません。** 中央に短い線を1本だけ
            // （目立たせすぎると、ストリップが線で分断されて見えます）
            g.setColour (AppColours::border);

            const auto line = getLocalBounds().withSizeKeepingCentre (juce::jmin (24, getWidth()), 1);
            g.fillRect (line);
        }

    private:
        int heightAtDragStart = defaultRackAreaHeight;
        int screenYAtDragStart = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackResizer)
    };
}
