#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.217：**デュアルエンジンとルーティング**（Phase 244／ディレイ仕様書3-1）。

    ─────────────────────────────────────────────────────────────────────────
    Ping-Pongが**まだ**ここに無い理由
    ─────────────────────────────────────────────────────────────────────────

    仕様書3-1は5つ挙げています（Single／Dual／Series／**Ping-Pong**／Split L-R）。
    ここに入っているのは**4つ**です。

    **Ping-Pongとクロスフィードバックだけ、作りがまるで違うため**です。

    | | エンジン同士の繋がり | ブロック単位で足りるか |
    |---|---|---|
    | Single・Dual・Series・Split L-R | **出口で混ぜるだけ** | **足ります** |
    | **Ping-Pong／Cross-Feedback** | **フィードバックの中で相互に**（Aの戻りがBの線へ入る） | **足りません**——1サンプルずつ交互に進める必要があります |

    前の4つは`processWet()`をブロックごとに呼んで、出てきたものを足せば済みます。
    Ping-Pongは**エンジンの中を開いて**、1サンプルごとに
    「読む → 相手の戻りを混ぜる → 書く」を交互にやることになります。

    **一度に両方やると、動かないときにどちらが原因か分かりません。**
    ルーティングが正しいことを確かめてから、中を開けます。

    > 仕様書3-1のCross-Feedbackは「ParallelとPing-Pongを連続的にブレンド」するもので、
    > **Ping-Pongと同じ仕掛け**です。2つで1つなので、一緒に入れます。

    ─────────────────────────────────────────────────────────────────────────
    並びを変えないこと
    ─────────────────────────────────────────────────────────────────────────

    保存されるのは**番号**です。Ping-Pongを足すときは**いちばん後ろへ**——
    仕様書3-1の並び（…Series／Ping-Pong／Split L-R）に合わせて途中へ入れると、
    **Split L/Rで保存したものがPing-Pongで開きます。**

    **表示の並びと保存の番号は、別のものにできます**（並べ替えたければ画面側で）。
*/
namespace MantaDelayRouting
{
    enum class Mode
    {
        single,     ///< Engine Aだけ。**Phase 4までと同じ**
        dual,       ///< AとBを並べて足す（別のディレイタイムの重ね掛け）
        series,     ///< Aの出口をBの入口へ（音色の重ね掛け）
        splitLR,    ///< Aは左、Bは右（左右で別のディレイ）

        // ここへ足すこと（上の説明）：pingPong は Phase 5b で

        count
    };

    inline constexpr int getModeCount() { return (int) Mode::count; }

    /** 表示名。**ASCIIのみ**（つまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getModeNames()
    {
        return { "Single", "Dual", "Series", "Split L/R" };
    }

    /** Engine Bが鳴るか。**画面のグレーアウトと、音を回すかどうかの両方**がこれを見ます。

        **判断は1箇所**（1.27）——別々に決めると、
        **画面ではBが生きているのに音が出ない**という状態ができます。 */
    inline bool usesEngineB (Mode mode)
    {
        return mode != Mode::single && mode != Mode::count;
    }

    /** そのモードの説明（画面の下に1行で出します）。

        **モードの名前だけでは何が起きるか分かりません**——
        「Series」と書いてあっても、AとBのどちらが前か読めない。 */
    inline const char* getModeDescription (Mode mode)
    {
        switch (mode)
        {
            case Mode::single:  return "Engine A only";
            case Mode::dual:    return "A and B in parallel";
            case Mode::series:  return "A into B";
            case Mode::splitLR: return "A on the left, B on the right";

            case Mode::count:
            default:            return "";
        }
    }
}
