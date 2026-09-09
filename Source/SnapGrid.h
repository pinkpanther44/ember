#pragma once

#include <juce_core/juce_core.h>
#include "Utf8.h"

//==============================================================================
/**
    仕様書5.5・5.9：**編集の刻み（スナップ）**。Phase 54で新設（HANDOVER 8.1のB1）。

    ### なぜ1箇所に集めたか

    Phase 53まで、寄せ先は**操作ごとにばらばら**でした。

    | 操作 | それまでの刻み |
    |---|---|
    | コード区間の作成 | 小節 |
    | コード区間のドラッグ | 拍 |
    | ループ範囲 | 小節 |
    | マーカー | 拍 |
    | クリップ・再生カーソル・ノート | 寄せない |

    どれも「その操作のときに妥当そうな値」を各所で直接計算していたため、
    **設定を1つ足しても、効かない場所が残る**形になっていました。
    値と計算をここへ集め、`ProjectModel::snapTime()`だけを全員が呼ぶようにしてあります。

    ### 増やすとき

    **`snapGridToString()`の文字列は、プロジェクトファイルに書かれます。**
    既存の綴りを変えると、保存済みのプロジェクトを開いたときに
    `snapGridFromString()`が読み取れず、既定へ落ちます。
    並び順を変えるのは自由ですが（intでは保存していないため）、綴りは変えないこと。
*/
enum class SnapGrid
{
    off,           // フリー（寄せない）
    bar,           // 小節
    quarter,       // 1/4（拍）
    eighth,        // 1/8
    sixteenth,     // 1/16
    thirtySecond   // 1/32
};

/** プロジェクトファイルへ書く文字列。**綴りを変えないこと**（上の説明を参照）。 */
inline juce::String snapGridToString (SnapGrid grid)
{
    switch (grid)
    {
        case SnapGrid::bar:          return "bar";
        case SnapGrid::quarter:      return "1/4";
        case SnapGrid::eighth:       return "1/8";
        case SnapGrid::sixteenth:    return "1/16";
        case SnapGrid::thirtySecond: return "1/32";
        case SnapGrid::off:
        default:                     return "off";
    }
}

/** 読み取れない文字列（古いプロジェクト、壊れた値）は**拍**として扱う。

    フリーへ落とさないのは、**「スナップが効かない」ほうが事故に見えるから**。
    寄りすぎているのは操作すればすぐ気づきますが、寄らないのは
    「設定が壊れている」と気づきにくい。 */
inline SnapGrid snapGridFromString (const juce::String& text)
{
    if (text == "off")  return SnapGrid::off;
    if (text == "bar")  return SnapGrid::bar;
    if (text == "1/8")  return SnapGrid::eighth;
    if (text == "1/16") return SnapGrid::sixteenth;
    if (text == "1/32") return SnapGrid::thirtySecond;

    return SnapGrid::quarter;
}

/** 画面に出す名前。**記号文字は使わない**（この環境のフォントに無い。HANDOVER 1.30）。 */
inline juce::String snapGridDisplayName (SnapGrid grid)
{
    switch (grid)
    {
        case SnapGrid::bar:          return utf8 ("小節");
        case SnapGrid::quarter:      return "1/4";
        case SnapGrid::eighth:       return "1/8";
        case SnapGrid::sixteenth:    return "1/16";
        case SnapGrid::thirtySecond: return "1/32";
        case SnapGrid::off:
        default:                     return utf8 ("フリー");
    }
}

/** 刻み1つぶんの長さ（秒）。**フリーなら0を返す**ので、呼び出し側は
    「0なら寄せない」の1つだけを見ればよい。

    テンポと拍子から求めるので、**テンポを変えると刻みも一緒に動きます**
    （秒で覚えていないため、後からテンポを変えても拍の上に乗ったまま）。

    `ProjectModel::getSnapSecondsAt()`から呼ばれます。ここを直接呼ぶのは、
    プロジェクトを持っていない場所だけにすること。 */
inline double snapSecondsFor (SnapGrid grid, double tempo, int beatsPerBar)
{
    if (grid == SnapGrid::off)
        return 0.0;

    const double secondsPerBeat = 60.0 / juce::jmax (1.0, tempo);

    switch (grid)
    {
        case SnapGrid::bar:          return secondsPerBeat * juce::jmax (1, beatsPerBar);
        case SnapGrid::eighth:       return secondsPerBeat * 0.5;
        case SnapGrid::sixteenth:    return secondsPerBeat * 0.25;
        case SnapGrid::thirtySecond: return secondsPerBeat * 0.125;
        case SnapGrid::quarter:
        case SnapGrid::off:
        default:                     return secondsPerBeat;
    }
}

/** 刻み1つぶんの長さを**拍で**返す（Phase 139／8.98）。

    **小節（bar）とフリー（off）は0**です。小節を拍で表せないのは、
    **拍子が変われば小節の拍数も変わる**から——「4拍」と書いた瞬間に3/4で狂います。
    そのため`ProjectModel::snapTime()`は**小節だけ別扱い**にして、
    小節線そのもの（`getBarStartTime()`）へ寄せています。

    ### なぜ秒ではなく拍なのか

    `snapSecondsFor()`は「いまのテンポでの秒数」です。テンポが曲の途中で
    変わる形にすると、**秒で刻んだ目盛りは変化点の先で拍から外れます**。
    拍で刻んで最後に秒へ直せば、どこでも拍の上に乗ります。 */
inline double snapGridBeats (SnapGrid grid)
{
    switch (grid)
    {
        case SnapGrid::quarter:      return 1.0;
        case SnapGrid::eighth:       return 0.5;
        case SnapGrid::sixteenth:    return 0.25;
        case SnapGrid::thirtySecond: return 0.125;
        case SnapGrid::bar:
        case SnapGrid::off:
        default:                     return 0.0;
    }
}
