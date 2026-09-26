#include "TimeRangeSelfTest.h"

#include "ProjectModel.h"
#include "SelectionState.h"
#include "WaveformCache.h"
#include "TimelineComponent.h"
#include "EditClipboard.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace TimeRangeSelfTest
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

        //======================================================================
        /** 試しの曲（120BPM・4/4なので**1小節＝2秒**。塊は1小節以上の空きで切れる）。

            ```
            A： [0.0-1.0]                      [6.0-7.0]
            B：            [2.5-3.0]                      [8.0-9.0]
            コード：                  [4.0-5.0]  ／  マーカー：4.0
            ```

            **A の 0.0 と B の 8.0 を選ぶ**のが基本の形です。
            1つの窓で持つと 0〜9 秒になり、**A の 6.0 と B の 2.5 が巻き込まれます**。 */
        struct Song
        {
            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            juce::String idA, idB;

            Song()
            {
                project.createNewProject();

                auto a = project.addTrack ("A", TrackType::Midi);
                auto b = project.addTrack ("B", TrackType::Midi);
                auto chords = project.addTrack ("Chords", TrackType::Chord);

                idA = a.getId();
                idB = b.getId();

                a.addNote (60, 100, 0.0, 0.5, nullptr);
                a.addNote (60, 100, 0.5, 0.5, nullptr);
                a.addNote (60, 100, 6.0, 1.0, nullptr);

                b.addNote (60, 100, 2.5, 0.5, nullptr);
                b.addNote (60, 100, 8.0, 0.5, nullptr);
                b.addNote (60, 100, 8.5, 0.5, nullptr);

                chords.addChordRegion (Chord {}, 4.0, 1.0, nullptr);
                project.addMarker (4.0, "Bridge", nullptr);

                // **ここまでをUndoに積まない**（1回目のUndoで曲ごと消えると、何も確かめられない）
                project.getUndoManager().clearUndoHistory();
            }

            int indexOf (const juce::String& id) const
            {
                for (int t = 0; t < project.getNumTracks(); ++t)
                    if (project.getTrack (t).getId() == id)
                        return t;

                return -1;
            }

            int a() const { return indexOf (idA); }
            int b() const { return indexOf (idB); }

            /** その時刻に始まるノート（無ければ無効な`Note`）。 */
            bool findNote (const juce::String& trackId, double time, int* pitch = nullptr) const
            {
                const int t = indexOf (trackId);

                if (t < 0)
                    return false;

                auto track = project.getTrack (t);

                for (int n = 0; n < track.getNumNotes(); ++n)
                {
                    auto note = track.getNote (n);

                    if (std::abs (note.getStartTime() - time) < 1.0e-6)
                    {
                        if (pitch != nullptr)
                            *pitch = note.getPitch();

                        return true;
                    }
                }

                return false;
            }

            int countNotes (const juce::String& trackId) const
            {
                const int t = indexOf (trackId);
                return t < 0 ? 0 : project.getTrack (t).getNumNotes();
            }

            int countChordRegions() const
            {
                int count = 0;

                for (int t = 0; t < project.getNumTracks(); ++t)
                    if (project.getTrack (t).getType() == TrackType::Chord)
                        count += project.getTrack (t).getNumChordRegions();

                return count;
            }

            /** **Undoは1回だけ**押す。何回押せば戻るかを見たいので、ここで繰り返さない。 */
            void undoOnce() { project.getUndoManager().undo(); }
        };

        //======================================================================
        // マウスの出来事を作る。**画面で押すのと同じ入口**（`mouseDown`など）へ渡します

        juce::MouseEvent makeEvent (juce::Component& c, juce::Point<int> position, juce::ModifierKeys mods,
                                    juce::Point<int> downPosition, bool dragged)
        {
            const auto now = juce::Time::getCurrentTime();

            return { juce::Desktop::getInstance().getMainMouseSource(), position.toFloat(), mods,
                     juce::MouseInputSource::defaultPressure,
                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                     &c, &c, now, downPosition.toFloat(), now, 1, dragged };
        }

        juce::ModifierKeys buttons (bool ctrl)
        {
            return juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier
                                        | (ctrl ? juce::ModifierKeys::commandModifier : 0));
        }

        void click (TimelineComponent& timeline, juce::Point<int> at, bool ctrl = false)
        {
            timeline.mouseDown (makeEvent (timeline, at, buttons (ctrl), at, false));
            timeline.mouseUp (makeEvent (timeline, at, buttons (ctrl), at, false));
        }

        void drag (TimelineComponent& timeline, juce::Point<int> from, juce::Point<int> to, bool ctrl = false)
        {
            timeline.mouseDown (makeEvent (timeline, from, buttons (ctrl), from, false));

            // **途中も通る**（一足飛びにすると、囲った行の数え方を通らない）
            timeline.mouseDrag (makeEvent (timeline, (from + to) / 2, buttons (ctrl), from, true));
            timeline.mouseDrag (makeEvent (timeline, to, buttons (ctrl), from, true));
            timeline.mouseUp (makeEvent (timeline, to, buttons (ctrl), from, true));
        }

        void pressDelete (TimelineComponent& timeline)
        {
            timeline.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        }

        /** その行の枠が**ちょうどこれだけ**か（時刻順。**描く枠**を見ます）。

            8.326：1行に枠が複数あり得るので、**数も合わせて**見ます。
            1つだけ見ていると、余計な枠（あいだを含むもの）が一緒に出ていても通ってしまいます。 */
        bool framesAre (TimelineComponent& timeline, int trackIndex,
                        std::initializer_list<std::pair<double, double>> expected)
        {
            const auto frames = timeline.getTimeRangeBoundsForTesting (trackIndex);

            if (frames.size() != expected.size())
                return false;

            size_t i = 0;

            for (const auto& [from, to] : expected)
            {
                const int left = timeline.getPointForTesting (trackIndex, from).x;
                const int right = timeline.getPointForTesting (trackIndex, to).x;

                if (std::abs (frames[i].getX() - left) > 1 || std::abs (frames[i].getRight() - right) > 1)
                    return false;

                ++i;
            }

            return true;
        }

        /** その行の枠が**1つだけ**で、その時刻の位置にあるか。 */
        bool frameSpans (TimelineComponent& timeline, int trackIndex, double from, double to)
        {
            return framesAre (timeline, trackIndex, { { from, to } });
        }

        /** 基本の形：Aの1小節目の塊を押し、Bの5小節目の塊をCtrl＋クリックで足す。 */
        void selectStaggeredBlocks (Song& song, TimelineComponent& timeline)
        {
            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            click (timeline, timeline.getPointForTesting (song.b(), 8.5), true);
        }

        struct Stage
        {
            Song song;
            TimelineComponent timeline { song.project, song.cache, song.selection };

            Stage()
            {
                // **見える大きさにしておくこと**（高さ0では行がどれも画面の外）
                timeline.setSize (1600, 900);
            }
        };
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--timerange-selftest"))
            return false;

        problems = 0;

        say ("time range self-test (8.325, 8.326)");

        //----------------------------------------------------------------------
        say ("--- the stage");
        {
            Stage stage;
            auto& timeline = stage.timeline;
            const auto last = timeline.getPointForTesting (stage.song.b(), 9.0);

            // **ここが崩れると、以下の全部が別のところを押します**
            check (last.x < timeline.getWidth() - 40,
                    "the whole song fits on screen (9 s is at x=" + juce::String (last.x) + ")");
            check (timeline.getPointForTesting (stage.song.a(), 0.0).y
                     != timeline.getPointForTesting (stage.song.b(), 0.0).y,
                    "track A and track B are different rows");
        }

        //----------------------------------------------------------------------
        say ("--- staggered blocks: Delete");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            selectStaggeredBlocks (song, timeline);

            check (timeline.hasTimeRangeForTesting(), "clicking a block and Ctrl-clicking another selects them");
            check (frameSpans (timeline, song.a(), 0.0, 1.0), "A's frame covers only A's block (0-1 s)");
            check (frameSpans (timeline, song.b(), 8.0, 9.0), "B's frame covers only B's block (8-9 s)");

            pressDelete (timeline);

            check (! song.findNote (song.idA, 0.0) && ! song.findNote (song.idA, 0.5), "A's selected block is gone");
            check (! song.findNote (song.idB, 8.0) && ! song.findNote (song.idB, 8.5), "B's selected block is gone");
            check (song.findNote (song.idA, 6.0), "A's other block (6 s, between the two) is still there");
            check (song.findNote (song.idB, 2.5), "B's other block (2.5 s, between the two) is still there");

            song.undoOnce();

            check (song.countNotes (song.idA) == 3 && song.countNotes (song.idB) == 3,
                    "one Undo brings everything back (A " + juce::String (song.countNotes (song.idA))
                      + "/3, B " + juce::String (song.countNotes (song.idB)) + "/3)");
        }

        //----------------------------------------------------------------------
        say ("--- staggered blocks: drag one bar to the right");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            selectStaggeredBlocks (song, timeline);
            drag (timeline, timeline.getPointForTesting (song.a(), 0.5),
                            timeline.getPointForTesting (song.a(), 2.5));

            check (song.findNote (song.idA, 2.0) && song.findNote (song.idA, 2.5), "A's block moved to 2 s");
            check (song.findNote (song.idB, 10.0) && song.findNote (song.idB, 10.5), "B's block moved to 10 s");
            check (song.findNote (song.idA, 6.0), "A's other block stayed at 6 s");
            check (song.findNote (song.idB, 2.5), "B's other block stayed at 2.5 s");
            check (frameSpans (timeline, song.a(), 2.0, 3.0) && frameSpans (timeline, song.b(), 10.0, 11.0),
                    "each frame followed its own block");

            song.undoOnce();

            check (song.findNote (song.idA, 0.0) && song.findNote (song.idB, 8.0)
                     && ! song.findNote (song.idA, 2.0) && ! song.findNote (song.idB, 10.0),
                    "one Undo puts both blocks back");
        }

        //----------------------------------------------------------------------
        say ("--- staggered blocks: transpose");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            selectStaggeredBlocks (song, timeline);
            timeline.transposeSelectedMidiClips (1);

            int pitchA0 = 0, pitchA6 = 0, pitchB25 = 0, pitchB8 = 0;
            song.findNote (song.idA, 0.0, &pitchA0);
            song.findNote (song.idA, 6.0, &pitchA6);
            song.findNote (song.idB, 2.5, &pitchB25);
            song.findNote (song.idB, 8.0, &pitchB8);

            check (pitchA0 == 61 && pitchB8 == 61, "the selected blocks went up a semitone");
            check (pitchA6 == 60 && pitchB25 == 60, "the blocks in between did not");

            song.undoOnce();
            song.findNote (song.idA, 0.0, &pitchA0);
            song.findNote (song.idB, 8.0, &pitchB8);

            check (pitchA0 == 60 && pitchB8 == 60, "one Undo brings both back down");
        }

        //----------------------------------------------------------------------
        say ("--- staggered blocks: Ctrl+X, then paste back");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            selectStaggeredBlocks (song, timeline);
            timeline.cutSelection();

            int notesOnClipboard = 0, othersOnClipboard = 0;

            for (const auto& item : EditClipboard::getItems())
                (item.state.hasType (IDs::NOTE) ? notesOnClipboard : othersOnClipboard)++;

            check (notesOnClipboard == 4, "the clipboard holds the 4 selected notes ("
                                            + juce::String (notesOnClipboard) + ")");
            check (othersOnClipboard == 0, "...and nothing else: no marker, no chord ("
                                             + juce::String (othersOnClipboard) + ")");
            check (song.project.getNumMarkers() == 1, "the marker between them is still there");
            check (song.countChordRegions() == 1, "the chord between them is still there");
            check (song.findNote (song.idA, 6.0) && song.findNote (song.idB, 2.5),
                    "the blocks between them are still there");

            timeline.pasteAt (0.0);

            check (song.findNote (song.idA, 0.0) && song.findNote (song.idA, 0.5)
                     && song.findNote (song.idB, 8.0) && song.findNote (song.idB, 8.5),
                    "pasting at 0 s puts each block back on its own track, same distance apart");
            check (song.countNotes (song.idA) == 3 && song.countNotes (song.idB) == 3,
                    "...and adds nothing else");
        }

        //----------------------------------------------------------------------
        // 8.326：**同じトラックの離れた塊を、あいだを入れずに2つ**（Phase 315／9.2の②）。
        //
        // Aのあいだ（3.25秒）に小さな塊を1つ足して、**それが巻き込まれないこと**を見ます。
        // 8.325までは、Aの6.5秒をCtrl＋クリックするとAの窓が0〜7秒へ広がっていました
        const auto addGapBlockOnA = [] (Song& song)
        {
            song.project.getTrack (song.a()).addNote (60, 100, 3.25, 0.25, nullptr);
        };

        say ("--- same track: Ctrl-click a second block (9.2 part 2)");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            addGapBlockOnA (song);
            selectStaggeredBlocks (song, timeline);
            click (timeline, timeline.getPointForTesting (song.a(), 6.5), true);

            check (framesAre (timeline, song.a(), { { 0.0, 1.0 }, { 6.0, 7.0 } }),
                    "A gets a second frame (6-7 s) and keeps the first (0-1 s), nothing in between");
            check (frameSpans (timeline, song.b(), 8.0, 9.0), "B's frame is untouched");

            pressDelete (timeline);

            check (! song.findNote (song.idA, 0.0) && ! song.findNote (song.idA, 6.0), "both of A's selected blocks are gone");
            check (song.findNote (song.idA, 3.25) && song.countNotes (song.idA) == 1, "A's block in between is still there");
            check (song.findNote (song.idB, 2.5) && song.countNotes (song.idB) == 1, "B keeps its other block");

            song.undoOnce();

            check (song.countNotes (song.idA) == 4 && song.countNotes (song.idB) == 3, "one Undo brings everything back");
        }

        say ("--- same track: drag two blocks 6 s to the right (the first lands where the second was)");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            addGapBlockOnA (song);
            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            click (timeline, timeline.getPointForTesting (song.a(), 6.5), true);
            drag (timeline, timeline.getPointForTesting (song.a(), 0.5),
                            timeline.getPointForTesting (song.a(), 6.5));

            // **窓ごとに動かすと、1つ目（0秒）が6秒へ行き、2つ目の窓でもう一度動いて12秒になります**
            check (song.findNote (song.idA, 6.0) && song.findNote (song.idA, 6.5),
                    "the first block moved to 6 s, once");
            check (song.findNote (song.idA, 12.0), "the second block moved to 12 s");
            check (! song.findNote (song.idA, 0.0) && ! song.findNote (song.idA, 12.5)
                     && ! song.findNote (song.idA, 18.0) && song.countNotes (song.idA) == 4,
                    "nothing was moved twice (nothing at 12.5 s or 18 s)");
            check (song.findNote (song.idA, 3.25), "the block in between stayed");
            check (framesAre (timeline, song.a(), { { 6.0, 7.0 }, { 12.0, 13.0 } }), "both frames followed");

            song.undoOnce();

            check (song.findNote (song.idA, 0.0) && song.findNote (song.idA, 6.0) && ! song.findNote (song.idA, 12.0),
                    "one Undo puts both back");
        }

        say ("--- same track: Ctrl+X, then paste somewhere else");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            addGapBlockOnA (song);
            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            click (timeline, timeline.getPointForTesting (song.a(), 6.5), true);
            timeline.cutSelection();

            check (EditClipboard::getItems().size() == 3, "the clipboard holds the 3 selected notes ("
                                                            + juce::String (EditClipboard::getItems().size()) + ")");
            check (song.findNote (song.idA, 3.25) && song.countNotes (song.idA) == 1,
                    "only the two blocks were cut");

            timeline.pasteAt (14.0);

            check (song.findNote (song.idA, 14.0) && song.findNote (song.idA, 14.5) && song.findNote (song.idA, 20.0),
                    "pasting at 14 s keeps the two blocks 6 s apart");
            check (! song.findNote (song.idA, 17.25) && song.countNotes (song.idA) == 4,
                    "...with nothing in between");
        }

        say ("--- same track: two blocks dragged down onto the other track");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            addGapBlockOnA (song);
            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            click (timeline, timeline.getPointForTesting (song.a(), 6.5), true);
            drag (timeline, timeline.getPointForTesting (song.a(), 0.5),
                            timeline.getPointForTesting (song.b(), 0.5));

            check (song.countNotes (song.idA) == 1 && song.findNote (song.idA, 3.25), "both blocks left A, the one in between stayed");
            check (song.findNote (song.idB, 0.0) && song.findNote (song.idB, 6.0) && song.countNotes (song.idB) == 6,
                    "...and landed on B at the same times");
            check (framesAre (timeline, song.b(), { { 0.0, 1.0 }, { 6.0, 7.0 } })
                     && timeline.getTimeRangeBoundsForTesting (song.a()).empty(),
                    "both frames went with them");
        }

        say ("--- same track: transpose two blocks");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            addGapBlockOnA (song);
            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            click (timeline, timeline.getPointForTesting (song.a(), 6.5), true);
            timeline.transposeSelectedMidiClips (1);

            int first = 0, second = 0, between = 0;
            song.findNote (song.idA, 0.0, &first);
            song.findNote (song.idA, 6.0, &second);
            song.findNote (song.idA, 3.25, &between);

            check (first == 61 && second == 61, "both blocks went up");
            check (between == 60, "the block in between did not");
        }

        say ("--- same track: a block that overlaps a frame joins it");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            // 枠で4.5〜6.5秒を囲い（Aの2つ目の塊の前半まで）、その塊をCtrl＋クリックで足す。
            // **重なる窓は1つにまとめる**——2つのまま持つと、削除は同じノートを2回消しに行きます
            drag (timeline, timeline.getPointForTesting (song.a(), 4.5),
                            timeline.getPointForTesting (song.a(), 6.5));
            click (timeline, timeline.getPointForTesting (song.a(), 6.8), true);
            click (timeline, timeline.getPointForTesting (song.a(), 0.5), true);

            check (framesAre (timeline, song.a(), { { 0.0, 1.0 }, { 4.5, 7.0 } }),
                    "the overlapping frames became one (4.5-7 s); the far block has its own (0-1 s)");

            pressDelete (timeline);

            check (song.countNotes (song.idA) == 0 && song.countNotes (song.idB) == 3,
                    "Delete takes all of A's selected notes, and nothing on B");
        }

        //----------------------------------------------------------------------
        say ("--- drawing a box across two tracks (unchanged)");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            drag (timeline, timeline.getPointForTesting (song.a(), 1.5),
                            timeline.getPointForTesting (song.b(), 3.5));

            check (frameSpans (timeline, song.a(), 1.5, 3.5) && frameSpans (timeline, song.b(), 1.5, 3.5),
                    "both rows get the same window (1.5-3.5 s)");

            pressDelete (timeline);

            check (! song.findNote (song.idB, 2.5), "the note inside the box is gone");
            check (song.countNotes (song.idA) == 3 && song.countNotes (song.idB) == 2,
                    "nothing outside the box is touched");
        }

        //----------------------------------------------------------------------
        say ("--- one block, dragged down onto the other track (unchanged)");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            click (timeline, timeline.getPointForTesting (song.a(), 0.5));
            drag (timeline, timeline.getPointForTesting (song.a(), 0.5),
                            timeline.getPointForTesting (song.b(), 2.5));

            check (! song.findNote (song.idA, 0.0) && song.countNotes (song.idA) == 1, "the block left A");
            check (song.findNote (song.idB, 2.0) && song.findNote (song.idB, 2.5) && song.countNotes (song.idB) == 5,
                    "...and landed on B at 2 s");
            check (frameSpans (timeline, song.b(), 2.0, 3.0) && timeline.getTimeRangeBoundsForTesting (song.a()).empty(),
                    "the frame went with it");
        }

        //----------------------------------------------------------------------
        say ("--- a track in the selection is deleted");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            selectStaggeredBlocks (song, timeline);

            song.project.removeTrack (song.project.getTrack (song.b()));
            timeline.refresh();

            check (timeline.hasTimeRangeForTesting(), "the selection survives");
            check (frameSpans (timeline, song.a(), 0.0, 1.0), "A's frame is unchanged (0-1 s)");
        }

        //----------------------------------------------------------------------
        // **1本が音域の端で止まるなら、どれも動かさない**（8.77）。
        // 1本ずつ確かめながら動かすと、1本目だけ上がったまま残ります
        say ("--- transpose stops as a whole at the edge of MIDI");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            auto b = song.project.getTrack (song.b());

            for (int n = 0; n < b.getNumNotes(); ++n)
                if (std::abs (b.getNote (n).getStartTime() - 8.0) < 1.0e-6)
                    b.getNote (n).setPitch (127, nullptr);

            selectStaggeredBlocks (song, timeline);
            timeline.transposeSelectedMidiClips (1);

            int pitchA0 = 0, pitchB8 = 0;
            song.findNote (song.idA, 0.0, &pitchA0);
            song.findNote (song.idB, 8.0, &pitchB8);

            check (pitchB8 == 127, "B's top note stays at 127");
            check (pitchA0 == 60, "...and A's block is not moved on its own (" + juce::String (pitchA0) + ")");
        }

        //----------------------------------------------------------------------
        // **旗から選んだ区間は、前のまま「曲全体のこの区間」**（8.124）。
        // 8.325でコード区間とマーカーを「旗の区間だけ」にしたので、こちらが減っていないこと
        say ("--- a range picked from a marker still carries markers and chords (unchanged)");
        {
            Stage stage;
            auto& song = stage.song;
            auto& timeline = stage.timeline;

            timeline.selectRangeFromMarkerForTesting (0);
            timeline.copySelection();

            int markers = 0, chords = 0;

            for (const auto& item : EditClipboard::getItems())
            {
                markers += item.state.hasType (IDs::MARKER) ? 1 : 0;
                chords += item.state.hasType (IDs::CHORDREGION) ? 1 : 0;
            }

            check (markers == 1 && chords == 1, "Ctrl+C takes the marker and the chord ("
                                                  + juce::String (markers) + ", " + juce::String (chords) + ")");

            pressDelete (timeline);

            check (song.project.getNumMarkers() == 0 && song.countChordRegions() == 0,
                    "Delete removes them too");
            check (! song.findNote (song.idA, 6.0) && ! song.findNote (song.idB, 8.0),
                    "...together with the notes on every track");
            check (song.findNote (song.idA, 0.0) && song.findNote (song.idB, 2.5),
                    "what comes before the marker stays");
        }

        //----------------------------------------------------------------------
        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
