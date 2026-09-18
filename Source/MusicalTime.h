#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include "TempoMap.h"

//==============================================================================
/**
    8.137：**ノードから「その曲のテンポの表」を引き当てる**（Phase 175／8.105の宿題3）。

    ### 何のためにあるか

    これから、ノートの時刻は**拍で保存**します（8.105の宿題3）。すると
    `Note::getStartTime()`は「保存されている拍を、そのときのテンポで秒へ直す」
    という仕事になります。**テンポの表が要るということです。**

    ところが`Note`は`juce::ValueTree`を包んだだけの薄いクラスで、
    **プロジェクトを知りません**。`track.getNote(i)`が返すのはツリーだけです。

    ### なぜ「引数で渡す」ではないのか

    渡す形にすると、`getStartTime()`の見た目が変わります。
    秒を読んでいる場所は**234箇所**あり、**そのすべてを直すことになります**（8.105）。

    **見た目（秒）を変えなければ、234箇所は1つも変わりません。**
    そのために、`Note`が自力で表へ辿り着ける道を1本だけ用意します。

    ### なぜ「1つしか無いのだから、グローバルに置く」ではないのか

    いま`ProjectModel`はアプリに1つだけです。**その前提には乗りません**——
    8.12の「1つしか無い前提は必ず崩れる」がそのまま当てはまるからです
    （プロジェクトを2つ開く、書き出し用に一時的にもう1つ作る、テストで並べる）。

    **ノード自身が答えを持っています。** ノートのツリーは`<PROJECT>`の子孫なので、
    根まで遡れば「どのプロジェクトのノートか」が分かります。ここがやっているのは
    **「根 → その根を持つ表」の対応付け**だけです。

    ### 使いかた

    表を持っている側（`ProjectModel`）が`TempoMapSource`を実装して、
    生きているあいだ`MusicalTime::registerSource()`しておきます。
    引く側は`MusicalTime::findMapFor (note.state)`と訊くだけです。

    ### 登録が無いとき

    **既定の表（120BPM・4/4・変化点なし）を返します。** 失敗にしないのは、
    クリップボードへ取った`createCopy()`のツリーのように、**どのプロジェクトにも
    属していないノード**が普通に出てくるからです（1.32）。
    そういうノードの時刻は「拍をそのままのテンポで読む」ことになります。

    ### スレッド

    **登録・解除・引き当てはメッセージスレッドから**を想定しています。
    再生用のノート表を組み立てる`MidiPlayerProcessor::rebuildNoteList()`は
    オーディオスレッドではありませんが、**別スレッドから呼ばれても壊れないように**
    `SpinLock`で守ってあります（`notesLock`と同じ扱い）。
    奪い合いが起きない前提なので、待たされることはありません。
*/
struct TempoMapSource
{
    virtual ~TempoMapSource() = default;

    /** その時点で正しいテンポの表。**呼ばれた側が作り直しの面倒を見ること**
        （`ProjectModel::getTempoMap()`は必要なら作り直してから返す）。 */
    virtual const TempoMap& getTempoMap() const = 0;

    /** この表が受け持つプロジェクトの根（`<PROJECT>`ノード）。 */
    virtual juce::ValueTree getTempoMapRoot() const = 0;
};

namespace MusicalTime
{
    //==========================================================================
    // 8.277：**「同じ位置」と言える近さ**（Phase 274／本人の報告）
    //
    // ### なぜ要るか
    //
    // **再生カーソルはサンプル単位**です。48kHzなら1サンプルは約20.8マイクロ秒で、
    // 「8拍目ちょうど」の秒数はサンプルの切れ目とは一致しません。
    // カーソルの位置に入れるもの（コードパッドの旗とノート。8.75）は、
    // **拍へ戻すと半端な値になります**。
    //
    // 本人のプロジェクトで実際に数えた値：
    //
    // | | 拍 |
    // |---|---|
    // | マーカー`Chorus` | `8.0` |
    // | その位置のコード区間と、書かれた1音目 | **`7.999995833333333`** |
    //
    // **4.2e-6拍（167BPMで約1.5マイクロ秒）手前**です。
    // 許容を`1.0e-6`秒にしていた場所は、これを「範囲の外」と判定していました
    // ——旗で選んだ区間をコピーすると**先頭の1つだけが付いてこない**、という形で出ます。
    //
    // ### 値の決め方
    //
    // 1ミリ秒は、**いちばん細かい目盛りよりずっと短く**（999BPMの1/32音符でも7.5ms）、
    // **サンプル1つよりはるかに長い**（20.8マイクロ秒）。
    // 拍のほうは960分解能の1ティックとほぼ同じです。
    //
    // **範囲に入るかを比べる場所は、必ずこれを使うこと。** 直に書いた許容が1つでも
    // 残ると、**コピーと削除で対象が違う**（＝カットが別のものを消す）が起きます。

    constexpr double samePositionSeconds = 0.001;
    constexpr double samePositionBeats = 0.001;

    /** その時刻が範囲の中か。**始まりは含み、終わりは含まない。** */
    inline bool isWithinRange (double timeSeconds, double fromSeconds, double toSeconds)
    {
        return timeSeconds >= fromSeconds - samePositionSeconds
            && timeSeconds <  toSeconds - samePositionSeconds;
    }

    /** 表を持っている側が、生きているあいだ登録しておく。

        **同じsourceを二重に登録しても1つとして扱います。**
        根が差し替わったとき（`ProjectModel::setState()`）は、
        **もう一度呼んでください**——対応表の根を書き替えます。 */
    void registerSource (TempoMapSource* source);

    void unregisterSource (TempoMapSource* source);

    /** そのノードが属するプロジェクトのテンポの表。

        **見つからなければ既定の表**（120BPM・4/4・変化点なし）を返します。
        `node`が無効でも同じです。**nullを返さないので、呼び出し側に分岐は要りません。** */
    const TempoMap& findMapFor (const juce::ValueTree& node);

    /** 秒 → 拍。`findMapFor()`を通すぶんだけの薄い包みです。 */
    double getBeatsFor (const juce::ValueTree& node, double timeSeconds);

    /** 拍 → 秒。`getBeatsFor()`の逆。**必ず対で使うこと**（8.101）。 */
    double getSecondsFor (const juce::ValueTree& node, double beatPosition);

    /** 8.138：`<PROJECT>`のツリーからテンポの表を組む（Phase 176／8.105の宿題3）。

        **表を作る式はここ1箇所だけ**です（1.27）。`ProjectModel::getTempoMap()`も、
        読み込み時の変換（`migrateTimesToBeats()`）も、どちらもここを通ります。

        変換は**`setState()`より前**、つまり**まだどのProjectModelにも属していない
        ツリー**に対して走るので、`findMapFor()`では引き当てられません。
        だから「ツリーを渡せば表が返る」形が要ります。 */
    TempoMap buildTempoMapFrom (const juce::ValueTree& projectState);
}
