#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

//==============================================================================
/**
    8.150：**ソースの時刻と、鳴る時刻の対応表**（Phase 188／8.48のワープ・その2。仕様書5.5.1）。

    ### 何を表しているか

    折れ線です。**点と点のあいだは一定の速さ**で、点をまたぐと速さが変わります。

    ```
    鳴る時刻 ↑
            |        ,-'      ← ここは速い（縮んでいる）
            |    ,-''
            |  ,'             ← ここは遅い（伸びている）
            +----------------→ ソースの時刻
    ```

    点が2つだけなら**ただの倍率**で、Phase 187（8.149）とまったく同じ動きになります。
    **クリップ全体の伸縮も、ワープも、この1つの形で表せます**——
    分岐を増やさずに済むよう、わざと同じ入れ物にしてあります（1.27）。

    ### 決めごと

    - 点は**両方の軸で昇順**であること。逆走すると時間が戻るので、
      `sanitise()`が**通らない点を捨てます**（呼ぶ側で弾くより確実）
    - 端より外は、**いちばん端の区間の速さでまっすぐ延長**します。
      端で止めると、トリムを戻したときに音が団子になります
    - **`sourceToWarped()`と`warpedToSource()`は必ず対で使うこと**（8.101）。
      片方を掛け算で書くと、区間をまたいだところで往復しなくなります

    ### なぜ「秒」で持つのか

    拍ではなく秒です。**ソースの側に拍という概念が無い**ためで、
    ここが扱うのは「録った音の中の位置」と「その音が並ぶ位置」の対応だけです。
    小節へ合わせる判断は、外（`TimelineComponent`）が持ちます。
*/
struct WarpMap
{
    struct Point
    {
        double sourceSeconds = 0.0;
        double warpedSeconds = 0.0;
    };

    /** 昇順に並んだ点。**2つ未満のときは素通し**（何もしない map）として扱います。 */
    std::vector<Point> points;

    /** 一定の倍率だけの表を作る（点2つ）。 */
    static WarpMap makeLinear (double ratio, double sourceLengthSeconds)
    {
        WarpMap map;
        const double safeRatio = (ratio > 0.0) ? ratio : 1.0;
        const double length = juce::jmax (0.0, sourceLengthSeconds);

        map.points.push_back ({ 0.0, 0.0 });
        map.points.push_back ({ length, length * safeRatio });
        return map;
    }

    /** 昇順に直し、**逆走する点を捨てる**。作ったら必ず1度通すこと。 */
    void sanitise()
    {
        std::sort (points.begin(), points.end(),
                    [] (const Point& a, const Point& b)
                    {
                        return a.sourceSeconds < b.sourceSeconds;
                    });

        std::vector<Point> kept;

        for (const auto& point : points)
        {
            if (! kept.empty())
            {
                // **どちらの軸でも進んでいること。** 同じ時刻に2つあると
                // その区間の速さが無限大になり、鳴らすと弾けます
                if (point.sourceSeconds <= kept.back().sourceSeconds + minimumSegment
                     || point.warpedSeconds <= kept.back().warpedSeconds + minimumSegment)
                    continue;
            }

            kept.push_back (point);
        }

        // **まっすぐ並んでいる点は落とす。**
        //
        // マーカーを置いただけの時点では、その点は**いまの線の上**にあります
        // ——何も曲がっていないので、作り直す必要がありません。
        // 落とさないと`isLinear()`がfalseのままで、**置いた瞬間にファイル全体を
        // 作り直します**（実機で確認）。**モデルのマーカーは消えません**
        // ——落とすのはここで組み立てる表だけで、印はそのまま出ますし掴めます
        std::vector<Point> reduced;

        for (size_t i = 0; i < kept.size(); ++i)
        {
            if (i == 0 || i + 1 == kept.size() || reduced.empty())
            {
                reduced.push_back (kept[i]);
                continue;
            }

            const auto& before = reduced.back();
            const auto& here = kept[i];
            const auto& after = kept[i + 1];

            const double span = after.sourceSeconds - before.sourceSeconds;

            if (span > minimumSegment)
            {
                const double t = (here.sourceSeconds - before.sourceSeconds) / span;
                const double onLine = before.warpedSeconds
                                        + t * (after.warpedSeconds - before.warpedSeconds);

                if (std::abs (here.warpedSeconds - onLine) < collinearTolerance)
                    continue;
            }

            reduced.push_back (here);
        }

        points.swap (reduced);
    }

    /** 点が2つだけ（＝ただの倍率）か。 */
    bool isLinear() const { return points.size() <= 2; }

    /** 全体をならしたときの倍率（鳴る秒 ÷ ソースの秒）。 */
    double getOverallRatio() const
    {
        if (points.size() < 2)
            return 1.0;

        const double sourceSpan = points.back().sourceSeconds - points.front().sourceSeconds;
        const double warpedSpan = points.back().warpedSeconds - points.front().warpedSeconds;

        return (sourceSpan > minimumSegment) ? warpedSpan / sourceSpan : 1.0;
    }

    /** ソースの時刻 → 鳴る時刻。 */
    double sourceToWarped (double sourceSeconds) const
    {
        return convert (sourceSeconds, true);
    }

    /** 鳴る時刻 → ソースの時刻（`sourceToWarped()`の逆）。 */
    double warpedToSource (double warpedSeconds) const
    {
        return convert (warpedSeconds, false);
    }

    /** その位置での速さ（鳴る秒 ÷ ソースの秒）。**作り直しのときの刻み幅**に使います。 */
    double getRatioAtSource (double sourceSeconds) const
    {
        if (points.size() < 2)
            return 1.0;

        const auto index = findSegment (sourceSeconds, true);

        return segmentRatio (index);
    }

    /** 表の終わり（鳴る側の長さ）。 */
    double getWarpedLength() const
    {
        return points.empty() ? 0.0 : points.back().warpedSeconds;
    }

    /** これより短い区間は作らない（0除算と、聞き取れない極端な速さを避ける）。 */
    static constexpr double minimumSegment = 1.0e-4;

    /** これだけ線から外れていなければ「まっすぐ」とみなす（秒）。

        **0.5msです。** 1サンプル（48kHzで0.02ms）まで見ると、
        置いただけのマーカーが丸め誤差で「曲がっている」ことになり、
        **毎回ファイル全体を作り直します**。 */
    static constexpr double collinearTolerance = 5.0e-4;

private:
    /** 区間の番号（`points[i]`から`points[i+1]`まで）。端より外は端の区間を返す。 */
    size_t findSegment (double value, bool fromSource) const
    {
        const size_t last = points.size() - 2;

        for (size_t i = 0; i < points.size() - 1; ++i)
        {
            const double edge = fromSource ? points[i + 1].sourceSeconds
                                            : points[i + 1].warpedSeconds;

            if (value < edge)
                return i;
        }

        return last;
    }

    double segmentRatio (size_t index) const
    {
        const double sourceSpan = points[index + 1].sourceSeconds - points[index].sourceSeconds;
        const double warpedSpan = points[index + 1].warpedSeconds - points[index].warpedSeconds;

        return (sourceSpan > minimumSegment) ? warpedSpan / sourceSpan : 1.0;
    }

    double convert (double value, bool fromSource) const
    {
        if (points.size() < 2)
            return value;   // 素通し

        const auto index = findSegment (value, fromSource);
        const auto& from = points[index];
        const double ratio = segmentRatio (index);

        // **端より外はまっすぐ延長する。** 端で止めると、トリムを戻したときに
        // クリップの外の音が全部同じ位置に団子になります
        return fromSource ? from.warpedSeconds + (value - from.sourceSeconds) * ratio
                          : from.sourceSeconds + (value - from.warpedSeconds) / juce::jmax (minimumSegment, ratio);
    }
};
