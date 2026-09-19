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
        「メーターの目盛りが読める最小」から決めてあります（Phase 66からの値）。

        8.298：**130から195へ**（Phase 291／本人の指定で1.5倍）。

        130pxでは、つまみ（20px）を引いた**動かせる幅が110px**しかなく、
        そこへ66dB（-60〜+6）を詰めていました——**1pxで0.6dB**です。
        195pxなら0.38dB。**メーターも同じだけ背が低く**、
        振れ幅を読み取る場所がそれだけしかありませんでした。

        > **この数字が、Consoleの最小の高さを決めます。**
        > ストリップの他の行は高さが決まっているので、
        > 「ここを割らない」と決めた時点で**パネル全体の下限も決まります**
        > （`ChannelStripComponent::minimumConsoleHeight`）。 */
    inline constexpr int minimumFaderAreaHeight = 195;

    /** ラックに最低限渡す高さ。**「+ Insert」の行が見える**ところまで。
        0にできるようにすると、**ラックを畳んだのか壊れたのか分からなくなります。** */
    inline constexpr int minimumRackAreaHeight = 40;

    /** Phase 294まで覚えていた、既定の**ラック**の高さ（引き継ぎにだけ使います）。

        8.297：150から175へ（Phase 290）。見出しが3行ぶん（42px）増えたため。 */
    inline constexpr int defaultRackAreaHeight = 175;

    /** 8.302：**既定のフェーダーの高さ**（Phase 295／本人の指定）。

        `minimumFaderAreaHeight`（195）より少し高いところ。
        **ここから先は掴んで決めるもの**なので、
        「開いた瞬間から窮屈ではない」以上の意味はありません。

        > **Phase 294までの設定からは引き継ぎます**（`ConsoleView`が1度だけ）。
        > ラックの高さで覚えていたので、**いま画面に出ているフェーダーの高さ**を
        > そのまま新しい鍵へ書き移します——開き直したら別の高さだった、を避けるため。 */
    inline constexpr int defaultFaderAreaHeight = 240;

    /** 8.295：**dB表示の行の高さ**（Phase 288／改善案2）。

        トラックのストリップとマスターの**両方**がここを読むこと。
        フェーダーとメーターはこの行の**上**に置かれるので、
        **片方だけ変えると、隣り合ったメーターの行が高さぶんずれます**
        （8.283で直したのと同じ形。1.27）。

        16から18へ上げたのは、**右端にトラックの種類の絵が入った**ためです
        （`ChannelStripComponent`）。**マスターには絵が出ません**——
        マスターはトラックの種類ではないので。**それでも同じ高さにすること**：
        マスターの右端が2px空くだけで、行は揃ったままです。 */
    inline constexpr int volumeReadoutRowHeight = 18;

    /** 8.295：**レイテンシ（遅延）の行の高さ**（Phase 288／本人の指定）。

        本人の指定は「Console内、遅延の数値表示をフェーダー／メーターの上に移動しよう。
        その方が見た目が揃うため」。**置き場所だけでなく、場所の取り方も変えています**：

        > **出ていなくても、場所は取ります。** Phase 287まではトラック側だけ
        > 「遅延が0なら出さない（＝場所も取らない）」でした。つまり
        > **プラグインを挿したトラックだけフェーダーが13px短い**という状態で、
        > メーターを横に並べて読み比べるときにそこがずれていました（8.283と同じ形）。
        >
        > いまは**常にこの高さを空け、文字だけ出し入れします。**
        > マスターは元から常に出しているので、これで両方が揃います。

        8.302：**`stripFixedHeight`に入っています**（Phase 295）。
        ラックとフェーダーの取り分は「ストリップの高さ−`stripFixedHeight`」なので、
        引き忘れようがありません（Phase 291で1度、境目とdB表示を引き忘れました）。 */
    inline constexpr int latencyRowHeight = 14;

    /** 境目の掴みしろ（ラックとフェーダーのあいだ）。 */
    inline constexpr int resizerHeight = 6;

    //==========================================================================
    // 8.298・8.299：ストリップの行の高さ（Phase 291・292）
    //
    // **`ChannelStripComponent`から移しました**（Phase 292）。
    // マスターのストリップも同じ数字で割り付けるためです
    // ——あちらにパンもミュート／ソロもありませんが、
    // **フェーダーとメーターの高さを合わせるには、その取り分を知る必要があります**。

    inline constexpr int stripMargin = 6;        ///< ストリップの外周の余白
    inline constexpr int nameRowHeight = 20;     ///< トラック名（下端）
    inline constexpr int panKnobHeight = 42;
    inline constexpr int panReadoutHeight = 13;
    inline constexpr int buttonRowHeight = 24;   ///< ミュート／ソロ
    inline constexpr int rowGap = 4;

    /** ラックとフェーダー**以外**が使う高さ（ふつうのトラックのストリップ）。

        **基準はふつうのトラック**です。マスターにもVCAにも無い行
        （パン・ミュート／ソロ）をここに数えているのは、
        **フェーダーの高さを全部のストリップで同じにする**ためです（8.299）。 */
    inline constexpr int stripFixedHeight =
          stripMargin * 2
        + nameRowHeight
        + panKnobHeight + panReadoutHeight + rowGap
        + buttonRowHeight + rowGap
        + resizerHeight + latencyRowHeight + volumeReadoutRowHeight;

    //==========================================================================
    // 8.302：**覚えるのはフェーダーの高さ**（Phase 295／本人の指定）
    //
    // 本人の言葉：「Console中のボリュームフェーダーとメーターの縦幅の伸縮は、
    // **フェーダーとメーターの上にある掴みを操作した時だけ**できるようにしてほしい。
    // **Consoleウィンドウの縦幅を伸縮した際は、フェーダーとメーターの縦幅は動かないように**」。
    //
    // Phase 294まで、覚えていたのは**ラックの高さ**で、フェーダーが残りをもらう形でした。
    // つまり**窓を縦に伸ばすとフェーダーが伸びます**——掴みを触っていないのに動く、
    // というのが本人の引っかかったところです。
    //
    // **向きを逆にしました。** 覚えるのはフェーダーの高さで、
    // **ラックが残りをもらいます**：
    //
    //   窓を伸ばす   → ラックが伸びる（フェーダーはそのまま）
    //   掴みを動かす → フェーダーが伸び縮みする
    //
    // **鍵は別にしてあります**（`consoleRackAreaHeight`は残したまま）。
    // 同じ鍵に別の意味の数字を入れると、**古い版で開いたときに桁違いの高さ**になります。

    inline const juce::String rackAreaHeightKey { "consoleRackAreaHeight" };
    inline const juce::String faderAreaHeightKey { "consoleFaderAreaHeight" };

    /** 覚えているフェーダーの高さ。**設定に無ければ既定**。 */
    inline int getFaderAreaHeight()
    {
        return juce::jmax (minimumFaderAreaHeight,
                            AppSettings::getInt (faderAreaHeightKey, defaultFaderAreaHeight));
    }

    /** 覚える。**画面へ配るのは呼び出し側**（`ConsoleView`）の仕事です。 */
    inline void setFaderAreaHeight (int newHeight)
    {
        AppSettings::setInt (faderAreaHeightKey, juce::jmax (minimumFaderAreaHeight, newHeight));
    }

    /** まだ一度も覚えていないか（Phase 294までの設定から引き継ぐときに見ます）。 */
    inline bool hasRememberedFaderAreaHeight()
    {
        return AppSettings::getInt (faderAreaHeightKey, -1) > 0;
    }

    /** Phase 294まで覚えていた**ラックの高さ**（引き継ぎにだけ使います）。 */
    inline int getLegacyRackAreaHeight()
    {
        return juce::jmax (minimumRackAreaHeight,
                            AppSettings::getInt (rackAreaHeightKey, defaultRackAreaHeight));
    }

    /** 8.299：**フェーダーとメーターに渡す高さ**（Phase 292／本人の指定）。

        `stripHeight`はストリップの**外枠**の高さ。

        > **全部のストリップが、この1つの数字を使うこと。**
        >
        > Phase 291まで、それぞれが「自分の残り」から出していました。
        > マスターにはパンもミュート／ソロも無く、VCAにはパンが無いので、
        > **そのぶん（63pxと59px）フェーダーが長く**なっていました。
        >
        > **差はラックが吸います。** マスターとVCAのラックは、
        > そのぶん背が高くなります——どちらも中身（インサート／リンク先）を
        > 縦に並べるところなので、余るより使えます。

        8.302：**覚えている高さをそのまま返します**（Phase 295／本人の指定）。
        窓を伸ばしても**ここは動きません**——伸びたぶんはラックへ行きます。 */
    inline int getFaderAreaHeightFor (int stripHeight)
    {
        const int shared = stripHeight - stripFixedHeight;   // ラックとフェーダーの取り分

        if (shared <= minimumFaderAreaHeight)
            return juce::jmax (0, shared);   // フェーダーのぶんすら無い（ラックは0）

        const int wanted = getFaderAreaHeight();

        // **ラックが読める大きさで残らないなら、フェーダーが全部もらいます。**
        // 20pxのラックは「+ Insert」が半分だけ見える状態で、壊れて見えます
        // （本人の指定：「ラックが見えなくなってしまう場面もあると思うが、それは構わない」）
        if (shared - wanted < minimumRackAreaHeight)
            return shared;

        return wanted;
    }

    /** 8.302：そのストリップの高さで、**ラックがもらう高さ**（Phase 295）。

        **残り物です。** 窓を伸ばしたぶんは、全部こちらへ来ます。 */
    inline int getRackAreaHeightFor (int stripHeight)
    {
        return juce::jmax (0, stripHeight - stripFixedHeight - getFaderAreaHeightFor (stripHeight));
    }

    /** 8.302：その高さで、**フェーダーが取れるいちばん大きい高さ**（Phase 295）。

        ラックが0になるところまでです（`stripFixedHeight`を引いた残り全部）。 */
    inline int getMaximumFaderAreaHeightFor (int stripHeight)
    {
        return stripHeight - stripFixedHeight;
    }

    //==========================================================================
    /** ラックとフェーダーの境目。**ドラッグでフェーダーの高さを変えます**（8.302）。

        **ストリップ本体のマウス処理に混ぜないこと。** あちらは
        「空きとトラック名を掴んで並べ替える」を持っているので、
        そこへ境目の判定を足すと、**並べ替えのつもりが高さが変わります**。 */
    class FaderResizer : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        FaderResizer()
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        }

        /** 8.302：**新しいフェーダーの高さ**が決まるたびに呼ばれる（ドラッグ中も）。

            Phase 294まではラックの高さを渡していました（`RackResizer`）。
            **覚えるものが入れ替わった**ので、名前ごと変えてあります
            ——「ラックの境目」と呼んだまま中身がフェーダーだと、
            次に読む人が必ず取り違えます。 */
        std::function<void (int)> onHeightDragged;

        /** 8.300：**いま画面に出ているフェーダーの高さ**（Phase 293／本人の報告）。

            **覚えている値ではなく、これを起点に掴みます。**

            覚えている値は「欲しい高さ」で、画面が低いときは
            `getFaderAreaHeightFor()`が途中で頭打ちにします（＝**出ている高さと違う**）。
            覚えているほうから掴むと、**引いたぶんが、届かない数字に足され続け**、
            **掴んでも何も動かない**ようになります（戻すには、差のぶんだけ逆へ引く必要がある）。

            置いた側（`ChannelStripComponent`）がフェーダーの高さを渡すこと。 */
        std::function<int()> getCurrentHeight;

        void mouseDown (const juce::MouseEvent& e) override
        {
            heightAtDragStart = getCurrentHeight != nullptr ? getCurrentHeight()
                                                             : getFaderAreaHeight();
            screenYAtDragStart = e.getScreenPosition().y;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            // **測るのは画面座標の差**（8.122・8.125と同じ話）。
            //
            // 高さを変えると**この部品自身が動きます**。部品の座標で測ると、
            // 動いたぶんが次の差し引きに入り、**指を止めても動き続ける**か、
            // **半分の速さでしか動かない**かのどちらかになります。
            //
            // 8.302：**引き算です**（Phase 295）。掴みはフェーダーの**上**にあるので、
            // **下へ引けばフェーダーは短く**なります——足し算のままだと、
            // 掴んだ手と逆へ動きます。
            if (onHeightDragged != nullptr)
                onHeightDragged (heightAtDragStart - (e.getScreenPosition().y - screenYAtDragStart));
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
        int heightAtDragStart = defaultFaderAreaHeight;
        int screenYAtDragStart = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaderResizer)
    };
}
