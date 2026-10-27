#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

/**
    8.365：**ノートに付けるピッチベンド**（Phase 353／本人の要望）。

    > 「MIDIノートにベンド機能を追加できるかな？」（画像：フォール・次のノートへのスライド・手描きのビブラート）
    > 「ノートの開始からもフォール／ライズできると嬉しい」

    本人と決めたこと：**単音向けから**（ノートに付けた曲線を、普通のピッチベンドに直して送る。どの音源でも効く。
    和音で重なった音は一緒に曲がる。MPE はあとから足せる形にしておく）／操作は**フォール・ライズ（頭と終わり）・
    次のノートへのスライド・ビブラート**。

    ### 形（数だけ。ノートの長さに対する割合で持つので、ノートを伸ばすと曲線も伸びる）

    ```
    半音
     startSemitones ●╮
                      ╲___________________________╭─ ● endSemitones   （ここではフォール＝下がる）
                       startPosition        endPosition                  1.0
    ```

    | | |
    |---|---|
    | 頭（スクープ） | 頭で`startSemitones`ずれた所から、`startPosition`までに元の音へ戻る |
    | 終わり（フォール／ライズ） | `endPosition`から曲がり始め、終わりで`endSemitones` |
    | ビブラート | 深さ`vibratoDepth`（半音、片側）・速さ`vibratoRate`（Hz）。ノートの`vibratoOnset`から 0.15 かけて深くなる |

    曲がり方はなめらか（0.5 − 0.5cos）。次のノートへのスライドは「終わりで次のノートの音程に着く」フォール／ライズ。
*/
struct NoteBend
{
    double startSemitones = 0.0;
    double startPosition = 0.3;    ///< 0〜1。頭のずれが戻りきる所
    double endSemitones = 0.0;
    double endPosition = 0.6;      ///< 0〜1。終わりの曲がりが始まる所
    double vibratoDepth = 0.0;     ///< 半音（片側）。0 で無し
    double vibratoRate = 5.5;      ///< Hz

    static constexpr double vibratoOnset = 0.25;
    static constexpr double vibratoFadeIn = 0.15;

    bool hasStart() const    { return std::abs (startSemitones) > 1.0e-6; }
    bool hasEnd() const      { return std::abs (endSemitones) > 1.0e-6; }
    bool hasVibrato() const  { return vibratoDepth > 1.0e-6 && vibratoRate > 0.0; }
    bool isEmpty() const     { return ! hasStart() && ! hasEnd() && ! hasVibrato(); }

    /** 位置を 0〜1 に収め、頭の戻りが終わりの曲がり始めより後にならないようにする。 */
    NoteBend normalised() const
    {
        auto b = *this;
        b.startPosition = juce::jlimit (0.02, 0.98, b.startPosition);
        b.endPosition = juce::jlimit (0.02, 0.98, b.endPosition);

        if (b.hasStart() && b.hasEnd() && b.startPosition > b.endPosition)
            b.startPosition = b.endPosition = 0.5 * (b.startPosition + b.endPosition);

        b.vibratoDepth = juce::jmax (0.0, b.vibratoDepth);
        b.vibratoRate = juce::jlimit (0.5, 20.0, b.vibratoRate);
        return b;
    }

    static double ease (double x)
    {
        x = juce::jlimit (0.0, 1.0, x);
        return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x);
    }

    /** ノートの中のその位置（`fraction`＝0〜1）でのずれ（半音）。ビブラートは秒で回るので`secondsIntoNote`も渡す。 */
    double semitonesAt (double fraction, double secondsIntoNote) const
    {
        const auto b = normalised();
        const double f = juce::jlimit (0.0, 1.0, fraction);
        double semis = 0.0;

        if (b.hasStart() && f < b.startPosition)
            semis += b.startSemitones * (1.0 - ease (f / b.startPosition));

        if (b.hasEnd() && f > b.endPosition)
            semis += b.endSemitones * ease ((f - b.endPosition) / (1.0 - b.endPosition));

        if (b.hasVibrato() && f > vibratoOnset)
        {
            const double depth = b.vibratoDepth * juce::jlimit (0.0, 1.0, (f - vibratoOnset) / vibratoFadeIn);
            semis += depth * std::sin (juce::MathConstants<double>::twoPi * b.vibratoRate * secondsIntoNote);
        }

        return semis;
    }
};

namespace NoteBendRender
{
    /** 14 ビットのピッチベンドの値（中央 8192）。`rangeSemitones`は音源のベンド幅（片側）。範囲の外は端で止める。 */
    inline int toPitchWheel (double semitones, double rangeSemitones)
    {
        const double range = juce::jmax (0.01, rangeSemitones);
        return juce::jlimit (0, 16383, (int) std::lround (8192.0 + semitones / range * 8192.0));
    }

    /** 送る刻み（秒）。10ms ならビブラートも段に聞こえない */
    inline constexpr double step = 0.01;

    /** ベンドの付いたノート1つぶん。曲の秒。 */
    struct BentNote
    {
        double start = 0.0;
        double end = 0.0;
        NoteBend bend;
    };

    /** **単音向け**の書き出し：ノートごとの曲線を、時刻とピッチベンドの値の並びにする（再生と MIDI 書き出しで共有）。

        - ノートの曲線は**次のノートの頭まで**（単音の前提。次の音が鳴ったら、その音の曲線へ渡す）
        - 曲がったノートのあとは、**次のノートの頭で中央へ戻す**（次が無ければ終わりから 1 秒後）。
          ノートの終わりで戻すと、余韻のあいだに音程が跳ねる
        - 同じ値が続くところは送らない

        `noteStarts`は**全部のノート**の頭（ベンドの無いものも。次のノートを探すのに使う）。 */
    inline std::vector<std::pair<double, int>> render (std::vector<BentNote> bentNotes, std::vector<double> noteStarts,
                                                       double rangeSemitones)
    {
        std::vector<std::pair<double, int>> events;
        std::sort (bentNotes.begin(), bentNotes.end(), [] (const BentNote& a, const BentNote& b) { return a.start < b.start; });
        std::sort (noteStarts.begin(), noteStarts.end());

        int lastValue = 8192;

        for (const auto& note : bentNotes)
        {
            if (note.end <= note.start || note.bend.isEmpty())
                continue;

            const auto next = std::upper_bound (noteStarts.begin(), noteStarts.end(), note.start + 1.0e-9);
            const double nextStart = next != noteStarts.end() ? *next : -1.0;
            const double segmentEnd = nextStart > note.start ? juce::jmin (note.end, nextStart) : note.end;
            const double length = note.end - note.start;

            for (double t = note.start; ; t += step)
            {
                const double at = juce::jmin (t, segmentEnd);
                const int value = toPitchWheel (note.bend.semitonesAt ((at - note.start) / length, at - note.start), rangeSemitones);

                if (value != lastValue || at == note.start)
                {
                    events.push_back ({ at, value });
                    lastValue = value;
                }

                if (at >= segmentEnd)
                    break;
            }

            // 中央へ戻す（次のノートの頭。その音にもベンドがあれば、すぐ後にその値が続く）
            const double resetAt = nextStart > note.start ? nextStart : note.end + 1.0;

            if (lastValue != 8192)
            {
                events.push_back ({ resetAt, 8192 });
                lastValue = 8192;
            }
        }

        std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
        return events;
    }
}
