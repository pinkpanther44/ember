#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    8.184：**幅が足りないとき、ツールバーを2段に折り返します**（Phase 223／本人の報告）。

    ─────────────────────────────────────────────────────────────────────────
    何が起きていたか
    ─────────────────────────────────────────────────────────────────────────

    13インチのノートで、**ツールバーのボタンが潰れる**という報告を受けました。
    エディタ画面がとくにひどい、と。

    コードから必要な幅を足し上げたところ、ピアノロールのツールバー2段目は

        左から  クオンタイズ100 + 刻み110 + Swing 45+160 + 選択のみ150  = 597px
        右から  刻み112 + ツール243 + 色分け120 + ドラム表示120         = 637px
                                                            合計 ≒ 1266px

    を必要としていました。**ところがウィンドウの最小サイズは 800×560 です**
    （`Main.cpp`）。**466px足りません。**

    そのあいだ、各ボタンはこう置かれていました：

        button.setBounds (row.removeFromRight (juce::jmin (width, row.getWidth())));

    `jmin`は「残りが少なければ、その分だけ」です。**はみ出しはしませんが、
    幅0まで縮みます。** 守っているつもりで、**潰れ方を許可していた**わけです。

    ─────────────────────────────────────────────────────────────────────────
    ここでの決め事
    ─────────────────────────────────────────────────────────────────────────

    **広い画面では、いままでと1pxも変わりません。** 足りないときだけ2段になります。

    ボタンの大きさと文字はそのままなので、**押しやすさは変わりません**。
    かわりにツールバーが縦に1段ぶん厚くなり、グリッドがそのぶん狭くなります。

    使うときは、`needsWrap()`で分岐して、段の取り方を変えてください：

        const bool wrap = ToolbarLayout::needsWrap (area.getWidth(), leftWidth, rightWidth);

        auto row1 = area.removeFromTop (rowHeight);
        auto row2 = wrap ? area.removeFromTop (ToolbarLayout::gap + rowHeight)
                                .withTrimmedTop (ToolbarLayout::gap)
                         : row1;

    `wrap`が偽なら`row2`は`row1`と同じ矩形なので、**1本のコードで両方を書けます**
    （`row1`から左詰め、`row2`から右詰めに取れば、いままでと同じ並びになります）。
*/
namespace ToolbarLayout
{
    /** 段と段のあいだ。ツールバー内の他の余白（8px）に合わせてあります。 */
    inline constexpr int gap = 6;

    /** 折り返すかどうか。

        `leftWidth`は左詰めで置くものの合計、`rightWidth`は右詰めで置くものの合計
        （どちらも、あいだの余白を含めた値）。

        **両方が同じ行に収まらないときだけ**真を返します。ぎりぎり収まるときは
        折り返しません——**画面を少し広げただけで段数が跳ねる**のは落ち着かないので、
        少しだけ余裕（`slack`）を見ています。
    */
    inline bool needsWrap (int availableWidth, int leftWidth, int rightWidth)
    {
        constexpr int slack = 8;

        return availableWidth < leftWidth + rightWidth + slack;
    }

    /** 折り返しを考えた、ツールバー全体の高さ。

        `resized()`の外（親のレイアウト）で高さを決めたいときに使います。
    */
    inline int getHeight (bool wrapped, int rowHeight)
    {
        return wrapped ? rowHeight * 2 + gap : rowHeight;
    }
}
