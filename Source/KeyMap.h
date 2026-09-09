#pragma once

#include <juce_core/juce_core.h>
#include "ChordModel.h"   // 仕様書5.11.1：Scale（キーとスケール。純粋な計算だけ）

#include <algorithm>
#include <vector>

//==============================================================================
/**
    仕様書5.11.1：**キーの変化点**（Phase 143／改善案㉔㉕。HANDOVER 8.105）。

    ### なぜ`TempoMap`と別なのか

    **同じレーンに出しますが、別のものです。**

    - `TempoMap`は**オーディオスレッドが読みます**（メトロノーム。8.103）。
      キーは音の出方に関係しないので、そこへ混ぜる理由がありません
    - `TempoMap`は**juce_coreとSTLだけ**で出来ていて、`Scale`（音楽理論）を
      持ち込むと、その約束が崩れます

    **ここも juce_core とSTLだけ**なので、`Tools/`のテストからそのまま使えます
    （`ChordModel.h`も同じ約束で書かれています）。

    ### 位置は「小節」で持ちます

    テンポは拍、拍子とキーは小節です。キーを小節に限ったのは：

    - **転調は小節の頭で起こります**（少なくとも、そう書くのが読みやすい）
    - 拍の途中に置けると、**小節線とスケール表示の切り替わりが食い違って見えます**

    足りなくなったら拍へ広げられますが、**そのときはレーンの寄せ方も一緒に変えること**
    （`TimelineComponent::updateSignatureDrag()`）。

    ### 変化点が1つも無いときは、これまでと同じ

    `changes`が空なら、答えは`initialKey`だけで決まります——**Phase 142までと同じ値**です。
    そのため**古いプロジェクトの読み替えは要りません**（`<KEYMAP>`が無いだけ）。
*/
struct KeyMap
{
    /** キーの変化点。位置は**小節**（0始まり）。 */
    struct KeyChange
    {
        int bar = 0;
        Scale key;
    };

    /** 曲の頭のキー。**実体はコードトラックが持っています**（`Track::getChordKey()`）。
        置き場所を変えていないのは、キーが「コード進行の前提」だからです（8.24）。 */
    Scale initialKey;

    /** **小節の昇順**。0小節目は`initialKey`が持つので入れません。 */
    std::vector<KeyChange> changes;

    /** 小節で並べ替え、0以下の位置は落とす。
        **表を作ったら必ず1回通すこと**（`getKeyAtBar()`が昇順を前提にしています）。 */
    void sortAndDeduplicate()
    {
        std::stable_sort (changes.begin(), changes.end(),
                          [] (const KeyChange& a, const KeyChange& b) { return a.bar < b.bar; });

        changes.erase (std::remove_if (changes.begin(), changes.end(),
                                        [] (const KeyChange& c) { return c.bar <= 0; }),
                        changes.end());
    }

    bool isEmpty() const { return changes.empty(); }

    /** その小節で効いているキー。 */
    Scale getKeyAtBar (int bar) const
    {
        Scale key = initialKey;

        for (const auto& change : changes)
        {
            if (change.bar > bar)
                break;

            key = change.key;
        }

        return key;
    }
};
