#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.333：Manta Limiter／Af Elephant Limiter を窓なしで確かめる（Phase 322）。

    ```
    "Manta Studio.exe" --limiter-selftest
    ```

    リミッター設計書「テスト設計」の関門を、**自動で回せるものは全部**：

    | 関門 | 見るもの |
    |---|---|
    | 1 | **オーバーシュート0**（全スタイル × Lookahead × Attack × Link、途中で窓の長さを変えても）／レイテンシー＝報告値／ヌルテスト（−120 dBFS以下） |
    | 2 | 出力のトゥルーピーク ≤ 天井 ＋0.1 dB（オーバーサンプリング Off／2x／4x／8x。**別の方法（16倍・長いsinc）で測る**）／オーバーサンプラーの通過域／Audition＋出力＝遅れた入力 |
    | 3 | K特性の係数（48 kHzで規格の表）／EBU Tech 3341（I）／EBU Tech 3342（LRA）／TPメーター |
    | 4 | ディザー（LSBの格子に乗る・無音で雑音以外が出ない）／CPU（情報として出すだけ） |

    **24時間の連続試験と試聴は、ここでは回しません**（関門1の長時間・関門4の試聴）。

    問題の数を終了コードで返します（0なら合格）。
*/
namespace LimiterSelfTest
{
    bool runIfRequested (const juce::String& commandLine);
}
