#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.217：**デュアルエンジンとルーティング**（Phase 244／ディレイ仕様書3-1）。

    ─────────────────────────────────────────────────────────────────────────
    2段に分けて入れました
    ─────────────────────────────────────────────────────────────────────────

    | | エンジン同士の繋がり | 入った段階 |
    |---|---|---|
    | Single・Dual・Series・Split L/R | **出口で混ぜるだけ** | Phase 5a（8.217） |
    | **Ping-Pong／Cross-Feedback** | **フィードバックの中で相互に**（Aの戻りがBの線へ入る） | **Phase 5b（8.218）** |

    前の4つはブロックごとに回して足せば済みます。Ping-Pongは**エンジンの中を開いて**、
    **1サンプルごと**に「読む → 相手の戻りを混ぜる → 書く」をやることになります
    （`MantaDelayEngine`の`beginSample()`／`readChannel()`／`writeChannel()`）。

    **一度に両方やると、動かないときにどちらが原因か分かりません。**

    ─────────────────────────────────────────────────────────────────────────
    並びを変えないこと
    ─────────────────────────────────────────────────────────────────────────

    保存されるのは**番号**です。**Ping-Pongはいちばん後ろへ足しました**——
    仕様書3-1の並び（…Series／Ping-Pong／Split L-R）に合わせて途中へ入れると、
    **Split L/Rで保存したものがPing-Pongで開きます。**

    > **選択肢を増やすと、ホストのオートメーションはずれます**（0〜1で覚えているため）。
    > 保存済みのプロジェクトは実値で持っているのでずれません
    > （`AudioProcessorValueTreeState`は木に**実値**を書きます）。
    > `routingMode`はPhase 244で足したばかりで、まだ公開していないので実害はありません。

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

        /** 8.218：**左右に跳ねる**（Phase 245）。入口はAだけ、戻りは全交換。

            仕様書3-1の並びでは`Series`と`Split L/R`のあいだですが、
            **ここは末尾**です（上の説明）。 */
        pingPong,

        // ここへ足すこと（上の説明）

        count
    };

    inline constexpr int getModeCount() { return (int) Mode::count; }

    /** 表示名。**ASCIIのみ**（つまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getModeNames()
    {
        return { "Single", "Dual", "Series", "Split L/R", "Ping-Pong" };
    }

    /** 8.218：`Cross`のつまみが効くか（Phase 245）。

        **Dualだけ**です。仕様書3-1のCross-Feedbackは
        「**Parallel的な挙動とPing-Pong的な挙動を連続的にブレンド**」するもので、
        Parallel＝Dualだからです。

        | | なぜ効かないか |
        |---|---|
        | Single | 相手がいません |
        | Series | AはもうBの中を通っています。そこへ戻りも回すと**道が2本**になり、何が起きているか読めません |
        | Split L/R | AとBは**別のチャンネル**に居るので、同じチャンネルどうしで交換しても**聞こえるところへ出てきません** |
        | **Ping-Pong** | **全交換そのもの**がこのモードの定義です（つまみで減らせると、ただのDualになります） | */
    inline bool usesCrossFeedback (Mode mode) { return mode == Mode::dual; }

    /** 8.218：戻りをどれだけ入れ替えるか（0＝自分のまま、1＝そっくり相手のもの）。

        **Ping-Pongは常に1**です（上の表）。 */
    inline float getCrossAmount (Mode mode, float knob)
    {
        if (mode == Mode::pingPong)
            return 1.0f;

        return usesCrossFeedback (mode) ? juce::jlimit (0.0f, 1.0f, knob) : 0.0f;
    }

    /** Engine Bが鳴るか。**画面のグレーアウトと、音を回すかどうかの両方**がこれを見ます。

        **判断は1箇所**（1.27）——別々に決めると、
        **画面ではBが生きているのに音が出ない**という状態ができます。 */
    inline bool usesEngineB (Mode mode)
    {
        return mode != Mode::single && mode != Mode::count;
    }

    /** 8.218：`Pan`が効くか（Phase 245）。

        **Ping-Pongでは効きません**——**左右を決めるのがモードそのもの**だからです
        （Aは左、Bは右）。触れるのに効かないつまみを作らないこと（8.208）。 */
    inline bool usesEnginePan (Mode mode) { return mode != Mode::pingPong; }

    /** 8.218：**入口をモノラルにまとめるか**（Phase 245）。

        Ping-Pongは**両エンジンをハードパン**するので、
        ステレオのまま入れても**左右の情報は残りません。**
        ならば**入口でまとめておくほうが、跳ね方が素直**です——
        右に寄った音だけAの右チャンネルに残って、**1回目の反復が片側だけ欠ける**のを避けます。 */
    inline bool collapsesInputToMono (Mode mode) { return mode == Mode::pingPong; }

    /** そのモードの説明（画面の下に1行で出します）。

        **モードの名前だけでは何が起きるか分かりません**——
        「Series」と書いてあっても、AとBのどちらが前か読めない。 */
    inline const char* getModeDescription (Mode mode)
    {
        switch (mode)
        {
            case Mode::single:   return "Engine A only";
            case Mode::dual:     return "A and B in parallel";
            case Mode::series:   return "A into B";
            case Mode::splitLR:  return "A on the left, B on the right";
            case Mode::pingPong: return "Bouncing left to right";

            case Mode::count:
            default:             return "";
        }
    }
}
