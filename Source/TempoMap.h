#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    仕様書5.1・5.9：**テンポと拍子の変化点**（Phase 140／HANDOVER 8.98の3）。

    ### なぜValueTreeではなく、ただの配列なのか

    2つ理由があります。

    1. **オーディオスレッドから読める**（1.12）。メトロノームや再生の準備は、
       毎ブロックValueTreeを辿ってはいけません。**再生の前に写し取った表**を
       読む形にしておくこと——`MidiPlayerProcessor`と同じ扱いです
    2. **画面を出さずに値を確かめられる**（8.4）。ここはjuce_coreとSTLだけに
       依存しているので、`Tools/TempoMapTests`がJUCEをビルドせずに走ります

    `ProjectModel`がValueTreeからこの表を作り、**換算の質問には全部この表が答えます**
    （8.100・8.101の関数はどれもここへ落ちます）。

    ### 座標が3つあります

    ```
    小節 ──(拍子)──> 拍 ──(テンポ)──> 秒
    ```

    **この2段に分けているのが、この設計のいちばん大事なところです。**

    - **拍子は「小節 <-> 拍」だけを決めます**（`bar`で位置を持つ）
    - **テンポは「拍 <-> 秒」だけを決めます**（`beatPosition`で位置を持つ）

    どちらも**musicalな位置で持つ**ので、**片方を変えても、もう片方は動きません。**
    秒で持つと、手前のテンポを変えただけで拍子の変わり目が小節の途中へずれます
    ——**「小節の途中で拍子が変わる」という、あってはならない状態が作れてしまう。**
    8.91の「表現できない状態は、作らせない」と同じ考え方です。

    ### 変化点が1つも無いときは、これまでと同じ

    `tempoChanges`と`meterChanges`が空なら、答えは
    `initialTempo`と`initialBeatsPerBar`だけで決まります——**Phase 139までと同じ値**です。
    そのため**古いプロジェクトの読み替えは要りません**（`<TEMPOMAP>`が無いだけ）。
*/
struct TempoMap
{
    /** テンポの変化点。位置は**拍**（曲の頭から数えた拍数）。 */
    struct TempoChange
    {
        double beatPosition = 0.0;
        double bpm = 120.0;
    };

    /** 拍子の変化点。位置は**小節**（0始まり）。

        **小節でしか置けません。** 小節の途中で拍子が変わる状態を
        作れないようにするためです（上の説明）。 */
    struct MeterChange
    {
        int bar = 0;
        int beatsPerBar = 4;   // 分子。**小節の長さに効くのはこちら**

        /** 分母（4分音符なら4）。**小節の長さには効きませんが、持たないと消えます**
            （Phase 145で足した。持たずに分子だけ表へ入れていたため、
            "6/8"の札が"6/4"と表示され、ドラッグで確定した瞬間に本当に書き換わっていた）。 */
        int denominator = 4;
    };

    double initialTempo = 120.0;
    int initialBeatsPerBar = 4;
    int initialDenominator = 4;

    /** **どちらも位置の昇順**。先頭（0拍目／0小節目）は`initial…`が持つので入れません。 */
    std::vector<TempoChange> tempoChanges;
    std::vector<MeterChange> meterChanges;

    /** 位置で並べ替え、同じ位置のものは後から入れたほうを残す。
        **表を作ったら必ず1回通すこと**（walkが昇順を前提にしています）。 */
    void sortAndDeduplicate()
    {
        std::stable_sort (tempoChanges.begin(), tempoChanges.end(),
                          [] (const TempoChange& a, const TempoChange& b)
                          { return a.beatPosition < b.beatPosition; });

        std::stable_sort (meterChanges.begin(), meterChanges.end(),
                          [] (const MeterChange& a, const MeterChange& b)
                          { return a.bar < b.bar; });

        // 0以下の位置は先頭の値（initial…）と重なるので落とす
        tempoChanges.erase (std::remove_if (tempoChanges.begin(), tempoChanges.end(),
                                             [] (const TempoChange& c)
                                             { return ! (c.beatPosition > 0.0) || c.bpm < 1.0; }),
                             tempoChanges.end());

        meterChanges.erase (std::remove_if (meterChanges.begin(), meterChanges.end(),
                                             [] (const MeterChange& c)
                                             { return c.bar <= 0 || c.beatsPerBar < 1; }),
                             meterChanges.end());
    }

    bool isEmpty() const { return tempoChanges.empty() && meterChanges.empty(); }

    /** 区間の走査と切り捨てを、わずかに甘く見るための誤差（Phase 145）。

        **拍 -> 秒 -> 拍 と往復すると、ちょうどの値が 7.9999999999999991 になります。**
        そのまま切り捨てると、**2小節目の頭が1小節目**になり、
        置いた札が1小節手前へ落ちる・札の文字が1つ前の値になる、といった形で出ます。

        `ProjectModel::snapToGridPosition()`が同じ理由で同じ甘さを持っています。
        **後から補正するのではなく、換算の根元で吸収すること**——
        補正は「どの呼び出しに要るか」を数え続けることになります。 */
    static constexpr double tolerance = 1.0e-9;

    //==========================================================================
    // 拍 <-> 秒（テンポ）

    /** その拍でのテンポ（BPM）。 */
    double getTempoAtBeat (double beatPosition) const
    {
        double bpm = juce::jmax (1.0, initialTempo);

        for (const auto& change : tempoChanges)
        {
            if (change.beatPosition > beatPosition)
                break;

            bpm = juce::jmax (1.0, change.bpm);
        }

        return bpm;
    }

    /** 拍 -> 秒。**変化点までを1区間ずつ足していきます。** */
    double getTimeForBeat (double beatPosition) const
    {
        const double target = juce::jmax (0.0, beatPosition);

        double seconds = 0.0;
        double fromBeat = 0.0;
        double bpm = juce::jmax (1.0, initialTempo);

        for (const auto& change : tempoChanges)
        {
            if (change.beatPosition >= target)
                break;

            seconds += (change.beatPosition - fromBeat) * 60.0 / bpm;
            fromBeat = change.beatPosition;
            bpm = juce::jmax (1.0, change.bpm);
        }

        return seconds + (target - fromBeat) * 60.0 / bpm;
    }

    /** 秒 -> 拍。`getTimeForBeat()`の逆。**必ず対で使うこと**（8.101）。 */
    double getBeatAtTime (double timeSeconds) const
    {
        const double target = juce::jmax (0.0, timeSeconds);

        double seconds = 0.0;
        double fromBeat = 0.0;
        double bpm = juce::jmax (1.0, initialTempo);

        for (const auto& change : tempoChanges)
        {
            const double segmentSeconds = (change.beatPosition - fromBeat) * 60.0 / bpm;

            if (seconds + segmentSeconds > target + tolerance)
                break;

            seconds += segmentSeconds;
            fromBeat = change.beatPosition;
            bpm = juce::jmax (1.0, change.bpm);
        }

        return fromBeat + (target - seconds) * bpm / 60.0;
    }

    //==========================================================================
    // 小節 <-> 拍（拍子）

    /** その小節の拍数（拍子の分子）。 */
    int getBeatsPerBarAtBar (int bar) const
    {
        int beats = juce::jmax (1, initialBeatsPerBar);

        for (const auto& change : meterChanges)
        {
            if (change.bar > bar)
                break;

            beats = juce::jmax (1, change.beatsPerBar);
        }

        return beats;
    }

    /** その小節の拍子の分母。**小節の長さには効きません**が、表示と保存に要ります。 */
    int getDenominatorAtBar (int bar) const
    {
        int denominator = juce::jmax (1, initialDenominator);

        for (const auto& change : meterChanges)
        {
            if (change.bar > bar)
                break;

            denominator = juce::jmax (1, change.denominator);
        }

        return denominator;
    }

    /** 小節の頭が、曲の頭から何拍目か。 */
    double getBeatForBarStart (int bar) const
    {
        const int target = juce::jmax (0, bar);

        double beats = 0.0;
        int fromBar = 0;
        int beatsPerBar = juce::jmax (1, initialBeatsPerBar);

        for (const auto& change : meterChanges)
        {
            if (change.bar >= target)
                break;

            beats += (double) (change.bar - fromBar) * beatsPerBar;
            fromBar = change.bar;
            beatsPerBar = juce::jmax (1, change.beatsPerBar);
        }

        return beats + (double) (target - fromBar) * beatsPerBar;
    }

    /** その拍が何小節目か（0始まり）と、小節の中で何拍目か。 */
    struct BarPosition
    {
        int bar = 0;
        double beatsIntoBar = 0.0;
    };

    BarPosition getBarPositionAtBeat (double beatPosition) const
    {
        const double target = juce::jmax (0.0, beatPosition);

        double beats = 0.0;
        int fromBar = 0;
        int beatsPerBar = juce::jmax (1, initialBeatsPerBar);

        for (const auto& change : meterChanges)
        {
            const double segmentBeats = (double) (change.bar - fromBar) * beatsPerBar;

            if (beats + segmentBeats > target + tolerance)
                break;

            beats += segmentBeats;
            fromBar = change.bar;
            beatsPerBar = juce::jmax (1, change.beatsPerBar);
        }

        BarPosition result;

        const double intoSegment = (target - beats) / beatsPerBar;

        // **切り捨ても甘く見る**（`tolerance`の説明を参照）。
        // 甘く見たぶん`beatsIntoBar`がわずかに負へ振れるので、0で止めること
        const double wholeBars = std::floor (intoSegment + tolerance);

        result.bar = fromBar + (int) wholeBars;
        result.beatsIntoBar = juce::jmax (0.0, (intoSegment - wholeBars) * beatsPerBar);

        return result;
    }
};
