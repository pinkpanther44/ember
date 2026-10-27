#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>

/**
    8.350：**オーバービューの換算**（Phase 339／本人の要望）。

    > 「トラック全体をざっと確認できるオーバービューを設置することできる？」

    8.359：**曲全体の縮図を固定で出す**（Phase 348／本人の要望）。

    > 「オーバービューは、ズーム機能は廃止。常にArrangeの全体縮図を固定表示（FITを押した時の状態で固定）。枠も要らなくなると思う」

    前は「いまアレンジ画面に見えている範囲」を枠で重ね、枠を掴んで動かす・端でズームする作りだった（8.350〜8.356）。
    いまは**曲の頭から終わり（Fit と同じ長さ）を幅いっぱいに、スクロールする行の全部を高さいっぱいに**縮めるだけ。
    アレンジ画面を動かすのはオーバービューに付けたスクロールバー、クリックは再生位置を動かす（`ArrangeOverview`）。

    ここは**数だけ**を扱います（部品にも画面にも触らない）。

    | | アレンジ画面の側 | オーバービューの側 |
    |---|---|---|
    | 横 | 秒（0〜`totalSeconds`） | 0〜`width` px |
    | 縦 | スクロールする行の画素（0〜`contentHeight`） | 0〜`height` px |
*/
struct ArrangeOverviewMap
{
    int width = 0;
    int height = 0;

    /** 横の全体（秒）。**Fit と同じ長さ**（曲の中身の端。`TimelineComponent::getContentLengthSeconds()`）。
        スクロールやズームでは変わらない（変わると縮図をまるごと描き直すことになる。8.352）。 */
    double totalSeconds = 0.0;

    /** 縦の全体（スクロールする行の合計の画素）。 */
    double contentHeight = 0.0;

    //==========================================================================
    float timeToX (double seconds) const
    {
        return width > 0 ? (float) (seconds / safeTotalSeconds() * (double) width) : 0.0f;
    }

    double xToTime (float x) const
    {
        return width > 0 ? (double) x / (double) width * safeTotalSeconds() : 0.0;
    }

    /** 行の画素 → オーバービューの y。**行の全部を高さいっぱいに**縮める（行が多いと細くなる）。 */
    float contentYToY (double contentY) const
    {
        return (float) (contentY * pixelsPerContentY());
    }

    double pixelsPerContentY() const
    {
        return (double) height / std::max (contentHeight, 1.0);
    }

private:
    double safeTotalSeconds() const { return std::max (totalSeconds, 1.0); }
};
