#include "FooterValuesSelfTest.h"

#include "ProjectModel.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace FooterValuesSelfTest
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

        /** 4/4・145BPMの曲に、4小節目から120BPM・Dメジャー、8小節目から3/4を置く。

            **画面で置いたときと同じ形**にしてあります（本人の報告の絵がこれ）。 */
        void buildProject (ProjectModel& project)
        {
            project.setTempo (145.0, nullptr);
            project.setTimeSignature ("4/4", nullptr);

            // コードトラックが無いとキーは置けません（`setKeyChange()`の決まり）
            project.addTrack ("Chords", TrackType::Chord);
            project.setProjectKey (Scale { 0, false }, nullptr);   // C major

            // 4/4なので、4小節目の頭は12拍目
            project.setTempoChange (12.0, 120.0, nullptr);
            project.setKeyChange (3, Scale { 2, false }, nullptr); // 0始まりなので4小節目
            project.setTimeSignatureChange (7, "3/4", nullptr);    // 8小節目
        }
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--footer-selftest"))
            return false;

        problems = 0;

        say ("footer values self-test (8.268)");

        ProjectModel project;
        buildProject (project);

        const double barOne = project.getBarStartTime (0);
        const double barThree = project.getBarStartTime (2);   // 変化点の手前
        const double barFour = project.getBarStartTime (3);    // テンポとキーが変わる
        const double barEight = project.getBarStartTime (7);   // 拍子が変わる

        //----------------------------------------------------------------------
        // ① 映す値

        {
            const auto atStart = project.getValuesInForceAt (barOne);

            check (juce::approximatelyEqual (atStart.tempo, 145.0), "bar 1 shows 145");
            check (atStart.timeSignature == "4/4", "bar 1 shows 4/4");
            check (atStart.key.root == 0 && ! atStart.key.minor, "bar 1 shows C major");
            check (atStart.tempoBar < 0 && atStart.keyBar < 0 && atStart.timeSignatureBar < 0,
                    "bar 1 says the values come from the start of the song");

            const auto beforeChange = project.getValuesInForceAt (barThree);
            check (juce::approximatelyEqual (beforeChange.tempo, 145.0),
                    "bar 3 still shows 145 (before the change)");

            const auto atChange = project.getValuesInForceAt (barFour);

            check (juce::approximatelyEqual (atChange.tempo, 120.0), "bar 4 shows 120");
            check (atChange.key.root == 2, "bar 4 shows D");
            check (atChange.tempoBar == 3, "bar 4 says the tempo comes from bar 4");
            check (atChange.keyBar == 3, "bar 4 says the key comes from bar 4");

            // **拍子はまだ変わっていない**（8小節目から）
            check (atChange.timeSignature == "4/4", "bar 4 still shows 4/4");
            check (atChange.timeSignatureBar < 0,
                    "bar 4 says the time signature still comes from the start");

            const auto atMeter = project.getValuesInForceAt (barEight);
            check (atMeter.timeSignature == "3/4", "bar 8 shows 3/4");
            check (atMeter.timeSignatureBar == 7, "bar 8 says it comes from bar 8");
        }

        //----------------------------------------------------------------------
        // ② **書き換える先**。ここが本番です

        {
            // 変化点より手前で打つと、曲頭の値が変わる（Phase 269までと同じ）
            project.setTempoAtTime (barThree, 150.0, nullptr);

            check (juce::approximatelyEqual (project.getTempo(), 150.0),
                    "typing before the change edits the song's own tempo");
            check (juce::approximatelyEqual (project.getValuesInForceAt (barFour).tempo, 120.0),
                    "...and leaves the change at bar 4 alone");

            // 変化点より後ろで打つと、**その変化点**が変わる
            project.setTempoAtTime (barFour, 130.0, nullptr);

            check (juce::approximatelyEqual (project.getValuesInForceAt (barFour).tempo, 130.0),
                    "typing after the change edits the change");
            check (juce::approximatelyEqual (project.getTempo(), 150.0),
                    "...and leaves the song's own tempo alone");

            // **札が増えないこと。** カーソルの拍で置くと、同じ値の変化点が2つになります
            check (project.getTempoMap().tempoChanges.size() == 1,
                    "no second tempo marker appeared on the lane");
        }

        {
            project.setProjectKeyAtTime (barFour, Scale { 5, true }, nullptr);   // F minor

            const auto atChange = project.getValuesInForceAt (barFour);
            check (atChange.key.root == 5 && atChange.key.minor, "the key at bar 4 became F minor");
            check (project.getProjectKey().root == 0,
                    "...and the key at the start of the song is untouched");
            check (project.getKeyMap().changes.size() == 1,
                    "no second key marker appeared on the lane");
        }

        {
            project.setTimeSignatureAtTime (barEight, "6/8", nullptr);

            const auto atMeter = project.getValuesInForceAt (barEight);
            check (atMeter.timeSignature == "6/8", "the time signature at bar 8 became 6/8");
            check (project.getTimeSignature() == "4/4",
                    "...and the one at the start of the song is untouched");
            check (project.getTempoMap().meterChanges.size() == 1,
                    "no second time signature marker appeared on the lane");
        }

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと。** `JUCEApplication`は`juce_gui_basics`で、
        // ここは画面を持たないので引き込みたくありません
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
