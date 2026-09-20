#include "SnapGridSelfTest.h"

#include "ProjectModel.h"
#include "MusicalTime.h"   // 8.277：位置の比べ方（Phase 274）
#include "SnapGrid.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace SnapGridSelfTest
{
    namespace
    {
        int problems = 0;

        void say (const juce::String& line)
        {
            std::cout << line << std::endl;
        }

        void check (bool condition, const juce::String& what)
        {
            if (condition)
            {
                say ("  ok    " + what);
            }
            else
            {
                ++problems;
                say ("  FAIL  " + what);
            }
        }

        void checkNear (double actual, double expected, const juce::String& what)
        {
            // 拍から秒へ、秒から拍へと往復するので、丸めのぶんは見逃す
            check (std::abs (actual - expected) < 1.0e-6,
                    what + "  (" + juce::String (actual, 9) + " / "
                         + juce::String (expected, 9) + ")");
        }

        /** 確かめたい刻みを全部並べたもの。**増やしたらここにも足すこと。** */
        const SnapGrid allGrids[] =
        {
            SnapGrid::off, SnapGrid::bar,
            SnapGrid::quarter, SnapGrid::eighth, SnapGrid::sixteenth, SnapGrid::thirtySecond,
            SnapGrid::quarterTriplet, SnapGrid::eighthTriplet,
            SnapGrid::sixteenthTriplet, SnapGrid::thirtySecondTriplet
        };
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--snap-selftest"))
            return false;

        problems = 0;

        say ("snap grid self-test (8.271)");

        //----------------------------------------------------------------------
        // ① 綴りの往復。**プロジェクトファイルに書かれる文字**なので、
        //    ここが崩れると「保存して開き直したら刻みが戻る」ことになります

        say ("--- spelling round trip");

        for (const auto grid : allGrids)
            check (snapGridFromString (snapGridToString (grid)) == grid,
                    "\"" + snapGridToString (grid) + "\" comes back as itself");

        // **古い版が書いたファイル**（3連を知らない）を読んでも、開けること
        check (snapGridFromString ("1/8") == SnapGrid::eighth, "an old file still reads 1/8");
        check (snapGridFromString ("nonsense") == SnapGrid::quarter,
                "an unreadable value falls back to the beat, not to free");

        //----------------------------------------------------------------------
        // ② 拍の刻み。3連は**同じ音価の2/3**

        say ("--- grid size in beats");

        checkNear (snapGridBeats (SnapGrid::quarter), 1.0, "1/4 is one beat");
        checkNear (snapGridBeats (SnapGrid::quarterTriplet), 2.0 / 3.0, "1/4T is two thirds of a beat");
        checkNear (snapGridBeats (SnapGrid::eighthTriplet), 1.0 / 3.0, "1/8T is a third of a beat");
        checkNear (snapGridBeats (SnapGrid::sixteenthTriplet), 1.0 / 6.0, "1/16T is a sixth of a beat");
        checkNear (snapGridBeats (SnapGrid::thirtySecondTriplet), 1.0 / 12.0, "1/32T is a twelfth of a beat");

        // **3つで、1つ上の音価ぶん。** 3連の定義そのもの
        checkNear (snapGridBeats (SnapGrid::eighthTriplet) * 3.0, snapGridBeats (SnapGrid::quarter),
                    "three 1/8T make one beat");
        checkNear (snapGridBeats (SnapGrid::sixteenthTriplet) * 3.0, snapGridBeats (SnapGrid::eighth),
                    "three 1/16T make one 1/8");

        // 秒のほうも同じ値になること（2箇所に分かれているので、片方だけ直す事故がある）
        checkNear (snapSecondsFor (SnapGrid::eighthTriplet, 120.0, 4), 0.5 / 3.0,
                    "at 120BPM one 1/8T lasts a sixth of a second");

        say ("--- which grids can be triplets");

        check (! snapGridCanBeTriplet (SnapGrid::off), "free cannot be a triplet");
        check (! snapGridCanBeTriplet (SnapGrid::bar), "a bar cannot be a triplet");
        check (snapGridCanBeTriplet (SnapGrid::eighth), "1/8 can be a triplet");
        check (snapGridWithTriplet (SnapGrid::bar, true) == SnapGrid::bar,
                "asking for a triplet bar gives the bar back, not something else");
        check (snapGridWithTriplet (SnapGrid::eighth, true) == SnapGrid::eighthTriplet,
                "1/8 plus the button is 1/8T");
        check (snapGridWithoutTriplet (SnapGrid::eighthTriplet) == SnapGrid::eighth,
                "1/8T without the button is 1/8 again");

        //----------------------------------------------------------------------
        // ③ 寄せ先。**モデルを通して**確かめる（`setSnapGrid()`は文字で覚えるので、
        //    ここまで通して初めて「保存したものが効く」ことになります）

        say ("--- snapping through the model (120BPM, 4/4)");

        ProjectModel project;
        project.setTempo (120.0, nullptr);        // 1拍 = 0.5秒
        project.setTimeSignature ("4/4", nullptr);

        project.setSnapGrid (SnapGrid::eighthTriplet);
        check (project.getSnapGrid() == SnapGrid::eighthTriplet, "the model keeps 1/8T");

        const double tripletSeconds = 0.5 / 3.0;   // 1/8の3連 = 1/6秒

        checkNear (project.snapTime (0.0), 0.0, "0 stays at 0");
        checkNear (project.snapTime (tripletSeconds * 0.9), tripletSeconds, "just before a triplet snaps up to it");
        checkNear (project.snapTime (tripletSeconds * 1.1), tripletSeconds, "just after it snaps back to it");
        checkNear (project.snapTime (tripletSeconds * 3.0), 0.5, "three triplets land exactly on the beat");
        checkNear (project.snapTime (0.49), 0.5, "just before the beat snaps to the beat");

        // **目盛りちょうどの値が1つ手前へ落ちないこと**（8.98の許容誤差）
        checkNear (project.snapTimeDown (tripletSeconds * 2.0), tripletSeconds * 2.0,
                    "rounding down at an exact triplet stays there");

        //----------------------------------------------------------------------
        // ④ **テンポの変化点をまたいでも拍に乗ること。** この道具の肝

        say ("--- across a tempo change (this is the point)");

        // 4/4の3小節目（8拍目）から60BPMへ。1拍が0.5秒から1.0秒に変わる
        project.setTempoChange (8.0, 60.0, nullptr);

        const double barThree = project.getBarStartTime (2);
        checkNear (barThree, 4.0, "bar 3 starts at 4 seconds (8 beats at 120BPM)");

        // 変化点の先では、1/8の3連は1/3拍＝1/3秒（60BPMなので1拍=1秒）
        const double afterChange = 1.0 / 3.0;

        checkNear (project.snapTime (barThree + afterChange * 0.9), barThree + afterChange,
                    "a triplet after the change is a third of a second, not a sixth");
        checkNear (project.snapTime (barThree + 0.9), barThree + 1.0,
                    "and three of them still land on the beat");

        // **拍の座標で確かめる。** 秒で刻んでいると、ここが半端な値になります
        for (const double seconds : { barThree + 0.1, barThree + 0.7, barThree + 2.4 })
        {
            const double snapped = project.snapTime (seconds);
            const double beats = project.getBeatPositionAt (snapped) / snapGridBeats (SnapGrid::eighthTriplet);

            checkNear (beats, std::floor (beats + 0.5),
                        "snapped " + juce::String (seconds, 3) + "s sits on a whole number of triplets");
        }

        //----------------------------------------------------------------------
        // ⑤ 8.277：**範囲の縁**（Phase 274／本人の報告）。
        //
        // 刻みの話と同じ「位置をどう比べるか」なので、ここで一緒に見ています。
        // 数字は**本人のプロジェクトから取った実測値**です——
        // マーカーが8.0拍、その位置のコード区間と1音目が7.999995833333333拍。

        say ("--- the edge of a range (real numbers from a project)");

        {
            ProjectModel song;
            song.setTempo (167.0, nullptr);
            song.setTimeSignature ("4/4", nullptr);

            const double markerSeconds = song.getTimeForBeatPosition (8.0);

            // **カーソルから入ったものは、サンプル単位のぶんだけ手前にいます**
            const double placedSeconds = song.getTimeForBeatPosition (7.999995833333333);
            const double gap = markerSeconds - placedSeconds;

            check (gap > 1.0e-6 && gap < 1.0e-5,
                    "the gap is bigger than the old 1e-6 tolerance but still microscopic  ("
                      + juce::String (gap * 1.0e6, 3) + " us)");

            // **これが直したかったこと**
            check (MusicalTime::isWithinRange (placedSeconds, markerSeconds, markerSeconds + 8.0),
                    "something placed from the playhead counts as inside the range");

            check (! MusicalTime::isWithinRange (markerSeconds - 0.5, markerSeconds, markerSeconds + 8.0),
                    "...but half a second early is still outside");

            // **終わりは含まない**（次の区間のものを二重に数えない）
            check (! MusicalTime::isWithinRange (markerSeconds + 8.0, markerSeconds, markerSeconds + 8.0),
                    "the end of the range belongs to the next one, not this one");

            check (MusicalTime::isWithinRange (markerSeconds, markerSeconds, markerSeconds + 8.0),
                    "the start of the range belongs to it");

            // **隣り合う2つの区間で、どちらにも入らない／両方に入るものが出ないこと。**
            // コピーと削除が同じ判定を通る以上、ここが破れるとカットが取りこぼします
            const double edge = markerSeconds + 8.0;

            for (const double offset : { -0.002, -0.0005, 0.0, 0.0005, 0.002 })
            {
                const double at = edge + offset;
                const int count = (MusicalTime::isWithinRange (at, markerSeconds, edge) ? 1 : 0)
                                + (MusicalTime::isWithinRange (at, edge, edge + 8.0) ? 1 : 0);

                check (count == 1, "a point " + juce::String (offset * 1000.0, 1)
                                     + "ms from the join lands in exactly one of the two ranges");
            }
        }

        //----------------------------------------------------------------------
        // ⑥ 8.304：**コード区間の縁**（Phase 297／本人の報告）。
        //
        // 「Writeで出るコードが1つ古いまま」の正体は⑤と同じ**比べ方**でした。
        // ここは**コード区間まで組んで**、カーソルが区間の頭へ来る2本の道を
        // それぞれ通します。**どちらも必ず手前側へ落ちる**のが肝で、
        // 手前へ落ちると答えは「1つ前のコード」になります。

        say ("--- landing on a chord flag (what the Write button does)");

        {
            ProjectModel song;
            song.setTempo (167.0, nullptr);          // 本人のプロジェクトのテンポ
            song.setTimeSignature ("4/4", nullptr);

            auto chords = song.addTrack ("Chords", TrackType::Chord);

            Chord fSharpMinor;
            fSharpMinor.root = 6;                     // F#
            fSharpMinor.type = ChordType::Min;

            Chord bMajor;
            bMajor.root = 11;                         // B
            bMajor.type = ChordType::Maj;

            // **小節の途中でコードが変わる形**（本人の発生条件）。
            // 155拍目＝39小節4拍目で F#m → B
            const double joinBeats = 155.0;

            chords.addChordRegionBeats (fSharpMinor, 152.0, joinBeats - 152.0, nullptr);
            chords.addChordRegionBeats (bMajor, joinBeats, 4.0, nullptr);

            const double joinSeconds = song.getTimeForBeatPosition (joinBeats);

            check (chords.findChordRegionAt (joinSeconds).getChord().getName() == "B",
                    "the flag itself is B");

            // 道1：**Writeが秒を足して進める。** 区間の頭は拍から換算されるので、
            // 足し算の答えとは最後の桁が合いません
            {
                const double from = song.getTimeForBeatPosition (joinBeats - 2.0);
                const double stepped = from + 2.0 * song.getBeatSecondsAt (from);   // 2分音符1個ぶん

                // **最後の1ビットの話**なので、ナノ秒でも0と出ます。並べて出すこと
                check (stepped < joinSeconds,
                        "adding two beats in seconds lands short of the flag  ("
                          + juce::String (stepped, 15) + " / " + juce::String (joinSeconds, 15) + ")");

                check (chords.findChordRegionAt (stepped).getChord().getName() == "B",
                        "...and the Write target is still B, not the chord before it");
            }

            // 道2：**再生カーソルはサンプルで切り捨て**（`AudioEngine::setPlayheadSeconds()`）。
            // こちらは**桁違いに大きい**（20.8マイクロ秒）うえ、**必ず起きます**
            {
                const double sampleRate = 48000.0;
                const double truncated = std::floor (joinSeconds * sampleRate) / sampleRate;
                const double gap = joinSeconds - truncated;

                check (gap > 0.0 && gap < 1.0 / sampleRate,
                        "the playhead truncated to samples sits just before the flag  ("
                          + juce::String (gap * 1.0e6, 3) + " us)");

                check (chords.findChordRegionAt (truncated).getChord().getName() == "B",
                        "...and the chord there is B, not the chord before it");
            }

            // **1ミリ秒より先は、まだ手前のコード。** 許容を広げすぎていないこと
            check (chords.findChordRegionAt (joinSeconds - 0.01).getChord().getName() == "F#m",
                    "10ms before the flag is still the chord before it");

            // 区間の**終わりは含まない**（縁に立つと次のものが答え）
            check (chords.findChordRegionAt (song.getTimeForBeatPosition (159.0)).state.isValid() == false,
                    "the end of the last region belongs to nothing, not to that region");
        }

        //----------------------------------------------------------------------
        // ⑦ 8.305：**目盛りから外れたノートを、外れたまま動かす**（Phase 298／本人の指定・動画）。

        say ("--- snapping that also remembers where a free note sat");

        {
            ProjectModel song;
            song.setTempo (120.0, nullptr);          // 1拍 = 0.5秒
            song.setTimeSignature ("4/4", nullptr);
            song.setSnapGrid (SnapGrid::eighth);     // 1/8 = 0.25秒

            const double onGridNote = 1.0;           // 目盛りの上
            const double freeNote   = 1.07;          // **0.07秒だけ後ろ**（手で置いた音）

            // **目盛りの上のノートは、今までと同じ。** ここが変わると、
            // 「揃えて打ち込んだものが揃わなくなる」という最悪の壊れ方になります
            for (const double wanted : { 1.2, 1.4, 1.51, 1.99 })
                checkNear (song.snapTimeRelativeTo (wanted, onGridNote), song.snapTime (wanted),
                            "a note that sits on the grid snaps exactly as before  ("
                              + juce::String (wanted, 2) + "s)");

            // **ずれたノートは、ずれたまま1目盛り動ける**（動画のやりたいこと）
            checkNear (song.snapTimeRelativeTo (freeNote + 0.25, freeNote), freeNote + 0.25,
                        "a free note moves by exactly one grid step, keeping its offset");

            checkNear (song.snapTimeRelativeTo (freeNote + 0.24, freeNote), freeNote + 0.25,
                        "...and a little short of it still lands there");

            // **元の位置そのものにも戻れる**（同じタイミングの別の音を作れる）
            checkNear (song.snapTimeRelativeTo (freeNote + 0.02, freeNote), freeNote,
                        "it can land back exactly where it was");

            // **目盛りのほうが近ければ、目盛りへ。** 片方だけになると、
            // ずれたノートを目盛りへ乗せ直せなくなります
            checkNear (song.snapTimeRelativeTo (1.51, freeNote), 1.5,
                        "but near a grid line the grid still wins");

            check (song.snapTimeRelativeTo (1.51, freeNote) != freeNote + 0.5,
                    "...so both kinds of stop exist, not just the offset one");

            // 0秒より手前へは行かない（`snapTime()`と同じ約束）
            check (song.snapTimeRelativeTo (-5.0, freeNote) >= 0.0, "it never goes before zero");

            // **フリー（寄せない）ではそのまま**
            song.setSnapGrid (SnapGrid::off);
            checkNear (song.snapTimeRelativeTo (1.234, freeNote), 1.234,
                        "with snapping off, nothing moves");
        }

        //----------------------------------------------------------------------
        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
