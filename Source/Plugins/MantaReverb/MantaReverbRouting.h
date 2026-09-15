#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
/**
    8.249：**ルーティング**（Phase 259／リバーブ仕様書2-1・3-1）。

    LX480のデュアルエンジン構成をそのまま写しています。

    | | 中身 |
    |---|---|
    | `Single` | **Aだけ**が動きます |
    | `Cascade` | **Aの出口をBの入口へ**（直列） |
    | `Mono Split` | **左chをA、右chをB**で個別に処理して、出口で合算 |
    | `Stereo Split` | **ステレオを両方へ送って**、それぞれの出口を合成 |

    ─────────────────────────────────────────────────────────────────────────
    ライト版であること（設計書4-5）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**切替時はシームレスなクロスフェードを行わず、
    短いミュート/バッファリセットで対応する。**

    落としたもの：**切り替えても前の響きが滑らかに引き継がれること**。
    切り替えた瞬間に鳴っていたものは切れます。

    ─────────────────────────────────────────────────────────────────────────
    足すときは`1/√2`を掛けること
    ─────────────────────────────────────────────────────────────────────────

    2つを足すと、**関係の無い音どうしなら大きさは√2倍**になります。
    そのままだと`Single`から切り替えた瞬間に3dB上がり、
    **どちらの響きが好きか**ではなく**どちらが大きいか**を聞くことになります
    （8.244でPlateとFDNの音量を揃えたのと同じ話）。

    `Cascade`は直列なので掛けません（足していないため）。

    ─────────────────────────────────────────────────────────────────────────
    `Mono Split`と`Stereo Split`の違いは、**入口だけ**
    ─────────────────────────────────────────────────────────────────────────

    出口はどちらも「両方を足す」です。違うのは何を入れるか：

    ```
        Mono Split   ：A ← (L, L)      B ← (R, R)
        Stereo Split ：A ← (L, R)      B ← (L, R)
    ```

    `Mono Split`で「不自然なほど広い」と言われるのは、
    **左の音と右の音に別々の響きが付く**からです——2つのエンジンの設定が
    同じだと、**そこまで広がりません**（8.225でCrossに書いたのと同じ形）。

    ─────────────────────────────────────────────────────────────────────────
    **AとBが同じだと、`Stereo Split`は3dB大きいだけ**（測ってあります）
    ─────────────────────────────────────────────────────────────────────────

    上の`1/√2`は「**関係の無い2つを足す**」ときの倍率です。
    `Stereo Split`で同じものを同じ設定で2つ回すと、**出てくる音もそっくり同じ**なので、
    足すと`√2`倍ではなく**2倍**になります。

    ```
        Room + Room（同じ設定）：Single 0.113 → Stereo Split 0.160（＋3.0dB）
        Hall + Plate           ：Single 0.114 → Stereo Split 0.099（−1.2dB）
        Mono Split（左右別の音）：Single 0.113 → Mono Split   0.113（差なし）
    ```

    **直せません**——「2つが似ているか」を測って倍率を変えると、
    音を出しているあいだに倍率が動きます。**`Stereo Split`は、
    AとBを違う設定にして使うもの**だ、というだけの話です。

    8.225でCrossについて書いたのと同じで、**説明するしかないものは画面に出す**
    （`routingBox`のツールチップ）。
*/
namespace MantaReverbRouting
{
    /** **並びを変えないこと。** `AudioParameterChoice`は木へ番号を書くので、
        途中へ挿すと保存済みのプロジェクトが別のモードで開きます（8.243と同じ）。 */
    enum class Mode
    {
        single = 0,
        cascade,
        monoSplit,
        stereoSplit
    };

    inline int getModeCount() { return 4; }

    /** **`utf8()`へ通さないこと**（ホストのオートメーション一覧に出る文字列）。 */
    inline juce::StringArray getModeNames()
    {
        return { "Single", "Cascade", "Mono Split", "Stereo Split" };
    }

    /** 一行の説明。**名前だけでは、AとBのどちらが前か読めません**（8.217）。 */
    inline const char* getModeDescription (Mode mode)
    {
        switch (mode)
        {
            case Mode::single:      return "Engine A only";
            case Mode::cascade:     return "A into B";
            case Mode::monoSplit:   return "Left through A, right through B";
            case Mode::stereoSplit: return "A and B in parallel";

            default:                return "";
        }
    }

    /** Bが動くか。**`Single`以外は全部動きます。** */
    inline bool usesEngineB (Mode mode) { return mode != Mode::single; }

    /** 出口で足すか。**`Cascade`は直列なので足しません。** */
    inline bool sumsEngines (Mode mode)
    {
        return mode == Mode::monoSplit || mode == Mode::stereoSplit;
    }

    /** 足すときに掛ける大きさ（上の説明）。 */
    inline float getSumGain (Mode mode) { return sumsEngines (mode) ? 0.70710678f : 1.0f; }
}
