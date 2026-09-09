#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AutomationModel.h"

//==============================================================================
/**
    8.37：**オートメーションの線の「見せ方」と「触り方」を1箇所にまとめたもの**（Phase 77）。

    同じ形の線を、アレンジ画面のオートメーション・ピアノロール下のレーン
    （オートメーションとMIDI CC）の**3箇所**が描いています。
    Phase 76で「オートメーションとCCで同じにする」と決めたのに、
    アレンジ画面だけ別の場所に同じ式が書いてあり、**曲がり具合を足したときに
    そこだけ直線のまま**になりかけました。そこでここへ集めています。

    **形そのもの（式）はモデル側**（`applyAutomationCurve()`）にあります。
    ここにあるのは「画面の座標でどう描くか」と「メニューに何を並べるか」だけです
    ——鳴り方と見た目が食い違わないように、式は1つしか持たせません。
*/
namespace AutomationCurveUI
{
    //==========================================================================
    // 線の描画

    /** 点と点のあいだを、繋ぎ方に従って`path`へ足す。

        `path`には既に始点が入っていること（`startNewSubPath()`済み）。 */
    void appendCurve (juce::Path& path, float fromX, float fromY, float toX, float toY,
                       AutomationCurve curve, float amount);

    //==========================================================================
    // 曲がり具合のつまみ（線の途中に出る小さな丸）

    /** つまみの半径（描画用）と、当たり判定の半径。
        **当たり判定のほうを大きく**しておく（小さい丸は掴みにくい）。 */
    constexpr float handleRadius    = 3.5f;
    constexpr float handleHitRadius = 8.0f;

    /** つまみを出す最小の区間幅（px）。
        **狭い区間にまで出すと、点そのものと重なって掴み分けられない。** */
    constexpr float minSegmentWidthForHandle = 22.0f;

    /** この区間につまみを出すか。

        **出さないのは3つ**：
        - ステップ（形が決まっている。曲げるならメニューで種別を変えてから）
        - 区間が狭すぎるとき（点と重なる）
        - **高さが同じ**とき（水平な区間は、どう曲げても線が動かない）。 */
    bool segmentHasHandle (float fromX, float fromY, float toX, float toY, AutomationCurve curve);

    /** つまみの位置（区間のまんなか。線の上に乗る）。 */
    juce::Point<float> getHandlePosition (float fromX, float fromY, float toX, float toY,
                                           AutomationCurve curve, float amount);

    /** つまみを描く（中を塗らない丸。点そのものと見分けがつくように）。 */
    void drawHandle (juce::Graphics& g, juce::Point<float> position, juce::Colour colour,
                      bool isBeingDragged);

    /** つまみを`newY`まで動かしたときの曲がり具合。

        **区間の高さで割ってから逆算する**ので、拡大していても縦に潰れていても
        同じ操作感になる。 */
    float amountForHandleDrag (float fromY, float toY, float newY);

    //==========================================================================
    // 右クリックメニューの「ここから次の点まで」

    /** メニュー項目のID。**他の項目と重ならない大きい番号**にしてある
        （呼ぶ側は1〜9あたりを自分の用途に使っている）。 */
    constexpr int linearItemId = 100;
    constexpr int easeItemId   = 101;
    constexpr int stepItemId   = 102;

    /** 「ここから次の点まで」の項目をメニューへ足す。

        曲がっているときは、直線の項目が**「直線に戻す」**になる
        （選ぶと曲がり具合も0へ戻る。曲げた後に元へ帰る道をここに置いている）。 */
    void addCurveItems (juce::PopupMenu& menu, AutomationCurve currentCurve, float currentAmount);

    /** この結果は「ここから次の点まで」の項目か。 */
    bool isCurveItem (int itemId);

    AutomationCurve curveForItem (int itemId);

    /** その項目を選んだ後の曲がり具合。**直線を選んだときだけ0へ戻す。** */
    float amountForItem (int itemId, float currentAmount);
}
