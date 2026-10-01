#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice を窓なしで確かめる（Phase 325）。

    ```
    "Manta Studio.exe" --shifter-selftest [--png=<ファイル>]
    ```

    本人の設計書（ライト版）7章「テスト計画」のうち、**数値で合否を出せるものは全部**：

    | 対象 | 合格基準 |
    |---|---|
    | PitchDetector | 80〜1000 Hz の正弦波・のこぎり波・合成母音で誤差 ±10セント、オクターブ誤り0件 |
    | PitchDetector（無声） | 白色雑音・無音で有声と誤判定する率 5% 未満 |
    | ScaleQuantizer | 境目＋0.15半音で切り替わる |
    | Transpose | 220 Hz の正弦波を ±12半音で、出力の周波数の誤差 ±5セント |
    | レイテンシー | Mix 0% でインパルスが報告値の位置に出る（±1サンプル） |
    | Mix 50% | Pitch 0 で原音と混ぜても櫛形にならない |
    | ブロック長 | 1／64／512／4096 で**出力がビット一致** |
    | リアルタイム安全 | `process()`中のメモリ確保 0回（**Debugビルドだけ**。CRTの確保フックで数える） |
    | CPU | 48 kHz・ブロック256で1コアの2%以下（情報として出す） |
    | 状態保存 | 保存→読込で全パラメータ一致、欠けたIDは既定値 |

    ほかに、Quantize・Robot・MIDIの3モードが狙いの音高へ寄ること、窓の大きさと部品の配置。
    **試聴（男声・女声・ささやき声）はここでは回しません。**

    問題の数を終了コードで返します（0なら合格）。
*/
namespace ShifterSelfTest
{
    bool runIfRequested (const juce::String& commandLine);
}
