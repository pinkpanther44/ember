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
    thirtySecond,  // 1/32

    //==========================================================================
    // 8.271：**3連符**（Phase 272／本人の要望）。
    //
    // 「1/8の3連」は**1/8を3つで1拍**——つまり1/8の2/3の長さです。
    // 値は`snapGridBeats()`と`snapSecondsFor()`が作っており、ここには持たせていません。
    //
    // **必ず末尾に足すこと。** `SnapGridSelector`のコンボボックスは
    // `off`から`thirtySecond`までを順に並べる作りで（3連の入口は「3」ボタンのほう）、
    // あいだへ割り込ませると**コンボに3連が混ざって出ます**。

    quarterTriplet,       // 1/4の3連（2拍を3つで割る）
    eighthTriplet,        // 1/8の3連（1拍を3つで割る）
    sixteenthTriplet,     // 1/16の3連
    thirtySecondTriplet   // 1/32の3連
};

//==============================================================================
// 8.271：3連かどうかは**刻みの値そのもの**が持つ（Phase 272）
//
// ### なぜ`SnapGrid`＋`bool`の2つにしなかったか
//
// 刻みは`ProjectModel::getSnapGrid()`から読まれて、寄せ先・グリッド線・
// ノートの既定の長さ・ループの最短……と方々で使われます。
// 「3連か」を別の値にすると、**渡し忘れた場所が黙って3連を無視します**——
// 「たまに寄り方が違う」という、いちばん気づきにくい壊れ方です。
// 値が1つなら、渡し忘れようがありません（1.26と同じ話）。
//
// 画面のほうは**コンボ＋「3」ボタン**の2つに分かれていますが、
// 組み立てと分解は`SnapGridSelector`の中だけで済んでいます。

/** その刻みが3連か。 */
inline bool snapGridIsTriplet (SnapGrid grid)
{
    return grid == SnapGrid::quarterTriplet || grid == SnapGrid::eighthTriplet
        || grid == SnapGrid::sixteenthTriplet || grid == SnapGrid::thirtySecondTriplet;
}

/** 3連を外した元の刻み。3連でなければそのまま返す。 */
inline SnapGrid snapGridWithoutTriplet (SnapGrid grid)
{
    switch (grid)
    {
        case SnapGrid::quarterTriplet:      return SnapGrid::quarter;
        case SnapGrid::eighthTriplet:       return SnapGrid::eighth;
        case SnapGrid::sixteenthTriplet:    return SnapGrid::sixteenth;
        case SnapGrid::thirtySecondTriplet: return SnapGrid::thirtySecond;
        default:                            return grid;
    }
}

/** **3連にできる刻みか。** 小節とフリーはできません——
    フリーは寄せないので刻みが無く、小節は3つに割ると小節線から外れます
    （拍子を変えれば小節の長さも変わるので、「1小節の3連」は位置が定まりません）。 */
inline bool snapGridCanBeTriplet (SnapGrid grid)
{
    const auto base = snapGridWithoutTriplet (grid);

    return base == SnapGrid::quarter || base == SnapGrid::eighth
        || base == SnapGrid::sixteenth || base == SnapGrid::thirtySecond;
}

/** 刻みと「3連か」から、対応する値を作る。

    **3連にできない刻み（小節・フリー）へtrueを渡しても、そのまま返します。**
    呼び出し側で場合分けを書かずに済むようにするためで、
    ボタンのほうは`snapGridCanBeTriplet()`で押せなくしてあります。 */
inline SnapGrid snapGridWithTriplet (SnapGrid grid, bool triplet)
{
    const auto base = snapGridWithoutTriplet (grid);

    if (! triplet)
        return base;

    switch (base)
    {
        case SnapGrid::quarter:      return SnapGrid::quarterTriplet;
        case SnapGrid::eighth:       return SnapGrid::eighthTriplet;
        case SnapGrid::sixteenth:    return SnapGrid::sixteenthTriplet;
        case SnapGrid::thirtySecond: return SnapGrid::thirtySecondTriplet;
        default:                     return base;
    }
}

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

        // 8.271：3連は末尾に`T`を付ける（Phase 272）。**この綴りも変えないこと**
        case SnapGrid::quarterTriplet:      return "1/4T";
        case SnapGrid::eighthTriplet:       return "1/8T";
        case SnapGrid::sixteenthTriplet:    return "1/16T";
        case SnapGrid::thirtySecondTriplet: return "1/32T";

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

    // 8.271：3連（Phase 272）。**古い版はこれを読めず、拍へ落ちます**——
    // 読めない値を拍にしてあるのはそのためで、開けなくなることはありません
    if (text == "1/4T")  return SnapGrid::quarterTriplet;
    if (text == "1/8T")  return SnapGrid::eighthTriplet;
    if (text == "1/16T") return SnapGrid::sixteenthTriplet;
    if (text == "1/32T") return SnapGrid::thirtySecondTriplet;

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

        // 8.271：3連（Phase 272）。**コンボボックスには出しません**——
        // 入口は「3」ボタンのほうです（`SnapGridSelector`）。
        // ここに名前があるのは、値を文字で出したい場所のため
        case SnapGrid::quarterTriplet:      return "1/4T";
        case SnapGrid::eighthTriplet:       return "1/8T";
        case SnapGrid::sixteenthTriplet:    return "1/16T";
        case SnapGrid::thirtySecondTriplet: return "1/32T";

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

        // 8.271：3連は**同じ音価の2/3**（Phase 272）。1/8なら3つで1拍になる
        case SnapGrid::quarterTriplet:      return secondsPerBeat * (2.0 / 3.0);
        case SnapGrid::eighthTriplet:       return secondsPerBeat * (1.0 / 3.0);
        case SnapGrid::sixteenthTriplet:    return secondsPerBeat * (1.0 / 6.0);
        case SnapGrid::thirtySecondTriplet: return secondsPerBeat * (1.0 / 12.0);

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

        // 8.271：3連（Phase 272）。**`snapSecondsFor()`と2箇所に分かれています**——
        // 片方だけ直すと、寄せ先とグリッド線がずれます
        case SnapGrid::quarterTriplet:      return 2.0 / 3.0;
        case SnapGrid::eighthTriplet:       return 1.0 / 3.0;
        case SnapGrid::sixteenthTriplet:    return 1.0 / 6.0;
        case SnapGrid::thirtySecondTriplet: return 1.0 / 12.0;

        case SnapGrid::bar:
        case SnapGrid::off:
        default:                     return 0.0;
    }
}
