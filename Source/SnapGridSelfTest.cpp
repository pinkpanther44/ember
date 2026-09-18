#include "SnapGridSelfTest.h"

#include "ProjectModel.h"
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
        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
