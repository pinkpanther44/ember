#include "TimeRangeSelfTest.h"

#include "ProjectModel.h"
#include "SelectionState.h"
#include "WaveformCache.h"
#include "TimelineComponent.h"
#include "ArrangeOverview.h"     // 8.350：オーバービュー（Phase 339）
#include "EditClipboard.h"
#include "MidiFileImport.h"      // 8.362：MIDI ファイルを落とす（Phase 351）
#include "PianoRollComponent.h"   // 8.365：ノートのベンド（Phase 353）
#include <juce_audio_formats/juce_audio_formats.h>

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
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
        // 8.327：**横スクロールバーのつまみを掴んで動かす**（Phase 317／本人の報告「カクカクする」）。
        //
        // 右はどこまでも行けるように、全体の長さを「いまの位置＋画面2つぶん」で作っています（8.162）。
        // JUCEのスクロールバーは、つまみの1pxを「全体の長さ÷動ける幅」で秒に直すので、
        // **掴んでいるあいだに全体が伸びると、1pxあたりの秒が毎回変わります**。
        // マウスを同じだけ動かしたら、画面も同じだけ動くこと——を数えます
        say ("--- dragging the horizontal scroll bar's thumb");
        {
            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            project.createNewProject();

            TimelineComponent timeline (project, cache, selection);
            timeline.setSize (1600, 900);

            auto& bar = timeline.getHorizontalScrollBarForTesting();
            const double limitBefore = bar.getMaximumRangeLimit();
            const int y = bar.getHeight() / 2;

            say ("        bar " + juce::String (bar.getWidth()) + "x" + juce::String (bar.getHeight())
                   + ", range " + juce::String (bar.getCurrentRangeStart(), 2) + "+" + juce::String (bar.getCurrentRangeSize(), 2)
                   + " of " + juce::String (bar.getMaximumRangeLimit(), 2));

            // 曲が空なので、全体は画面2つぶん＝つまみは左半分。**その真ん中あたり**を掴む
            const juce::Point<int> grab (bar.getWidth() / 4, y);
            const auto modifiers = buttons (false);

            bar.mouseDown (makeEvent (bar, grab, modifiers, grab, false));

            std::vector<double> steps;
            double previous = timeline.getScrollStartSecondsForTesting();

            for (int i = 1; i <= 30; ++i)
            {
                const juce::Point<int> at (grab.x + i * 8, y);
                bar.mouseDrag (makeEvent (bar, at, modifiers, grab, true));


                // 8.327：**知らせを流さずに読む**。つまみを動かしたその出来事の中で、
                // 画面も動いていること（後回しにすると、つまみと中身が1コマずれて描かれる）
                const double now = timeline.getScrollStartSecondsForTesting();
                steps.push_back (now - previous);
                previous = now;
            }

            const juce::Point<int> end (grab.x + 30 * 8, y);
            bar.mouseUp (makeEvent (bar, end, modifiers, grab, true));

            const auto [smallest, largest] = std::minmax_element (steps.begin(), steps.end());

            check (*smallest > 0.0, "every 8 px to the right scrolls the view right away, in the same mouse event");
            check (*smallest > 0.0 && *largest <= *smallest * 1.02,
                    "every 8 px scrolls the same amount (smallest " + juce::String (*smallest, 4)
                      + " s, largest " + juce::String (*largest, 4) + " s)");
            check (bar.getMaximumRangeLimit() > limitBefore,
                    "after letting go, the bar reaches further to the right (as before, 8.162)");
        }

        //----------------------------------------------------------------------
        // 8.350：**オーバービュー**（Phase 339／本人の要望「トラック全体をざっと確認できるオーバービュー」）。
        //
        // 8.359：**作りを改めた**（Phase 348／本人の要望）。
        // > 「オーバービューに横スクロールバーと縦スクロールバーを配置する」
        // > 「ズーム機能は廃止。常にArrangeの全体縮図を固定表示（FITを押した時の状態で固定）。枠も要らなくなると思う」
        // > 「オーバービュー上をクリックでカーソルを移動できる」
        // > 「オーバービューエリア左側が空白の状態であるが、この空白エリアにもオーバービューを広げよう」
        //
        // 決めたこと：スクロールバーは**アレンジ画面を動かす**、クリックは**再生位置を動かす**（本人の答え）。
        // 前の枠・端のズーム・オーバービュー自身の縦スクロール（8.351・8.354〜8.356）は無くなった
        say ("--- the overview (8.359): the whole song, fixed");

        ProjectModel ovProject;
        SelectionState ovSelection;
        WaveformCache ovCache;
        ovProject.createNewProject();

        // 20本の MIDI トラック。2本目は 40〜48 秒、3本目は曲の頭（1〜3 秒）にノート。20本目の 120 秒で曲の終わり
        for (int i = 0; i < 20; ++i)
            ovProject.addTrack ("Track " + juce::String (i + 1), TrackType::Midi);

        for (int n = 0; n < 16; ++n)
            ovProject.getTrack (1).addNote (60, 100, 40.0 + n * 0.5, 0.5, nullptr);

        for (int n = 0; n < 4; ++n)
            ovProject.getTrack (2).addNote (64, 100, 1.0 + n * 0.5, 0.5, nullptr);

        ovProject.getTrack (19).addNote (60, 100, 120.0, 1.0, nullptr);

        TimelineComponent ovTimeline (ovProject, ovCache, ovSelection);
        ovTimeline.setSize (1400, 600);
        ovTimeline.setAutoScroll (false);

        ArrangeOverview overview (ovProject, ovTimeline);
        overview.setSize (1400, ArrangeOverview::preferredHeight);
        overview.resized();
        overview.tickForTesting();

        juce::Image scratch (juce::Image::RGB, overview.getWidth(), overview.getHeight(), true);
        auto paintOverview = [&]
        {
            juce::Graphics g (scratch);
            overview.paint (g);
        };

        {
            const auto map = overview.getMapForTesting();
            const auto area = overview.getMapArea();

            check (area.getX() == 0 && map.width == 1400 - ArrangeOverview::scrollBarThickness,
                   "the overview starts at the left edge, above the track headers too, and runs to the scroll bar ("
                     + area.toString() + ")");

            check (std::abs (map.totalSeconds - ovTimeline.getSongEndSecondsForOverview()) < 1e-9 && map.totalSeconds >= 121.0,
                   "it shows the whole song, the same length as Fit (" + juce::String (map.totalSeconds, 1) + " s)");

            const auto rows = ovTimeline.getOverviewRows();
            bool contiguous = rows.size() == 20;

            for (size_t i = 1; contiguous && i < rows.size(); ++i)
                contiguous = rows[i].top == rows[i - 1].top + rows[i - 1].height && rows[i].trackIndex == (int) i;

            const float lastBottom = rows.empty() ? 0.0f : map.contentYToY (rows.back().top + rows.back().height);
            check (contiguous && lastBottom <= (float) map.height + 0.5f && lastBottom >= (float) map.height * 0.9f,
                   "all 20 rows, stacked without gaps, fit in its height (the last row ends at "
                     + juce::String (lastBottom, 1) + " of " + juce::String (map.height) + " px; below it is the add-track row)");
        }

        // 8.352／8.359：**アレンジ画面をスクロール・ズームしても、縮図は変わらず描き直さない**
        {
            paintOverview();
            const auto before = overview.getMapForTesting();
            const int rendersBefore = overview.getRenderCountForTesting();
            const int countsBefore = overview.getSongEndCountForTesting();

            ovTimeline.setVisibleSecondsFromOverview (0.0, 20.0);

            for (int i = 1; i <= 100; ++i)
            {
                ovTimeline.setViewFromOverview ((double) i, (double) (i * 5));
                overview.tickForTesting();
                paintOverview();
            }

            ovTimeline.zoomIn();
            ovTimeline.zoomOut();
            ovTimeline.zoomOut();
            overview.tickForTesting();
            paintOverview();

            const auto after = overview.getMapForTesting();
            const int renders = overview.getRenderCountForTesting() - rendersBefore;
            const int counts = overview.getSongEndCountForTesting() - countsBefore;

            check (renders == 0 && counts == 0 && after.totalSeconds == before.totalSeconds && after.contentHeight == before.contentHeight,
                   "scrolling the arrange window 100 steps and zooming it leaves the overview as it is: redrawn " + juce::String (renders)
                     + " times, the song's end recounted " + juce::String (counts) + " times (both should be 0)");

            // 曲を編集したら、端を数え直して描き直す
            ovProject.getTrack (5).addNote (64, 100, 140.0, 2.0, nullptr);
            overview.tickForTesting();
            paintOverview();

            check (overview.getSongEndCountForTesting() > countsBefore && overview.getRenderCountForTesting() > rendersBefore
                     && overview.getMapForTesting().totalSeconds >= 150.0,
                   "editing the song (a note at 140 s) recounts its end and redraws the picture ("
                     + juce::String (overview.getMapForTesting().totalSeconds, 1) + " s)");
        }

        // 絵：2本目の行の 44 秒の所はトラックの色、何も無い所は地の色、**3本目の 2 秒（左の、ヘッダーの上の所）も描く**
        {
            ovTimeline.setViewFromOverview (0.0, 0.0);
            overview.tickForTesting();

            juce::Image shot (juce::Image::RGB, overview.getWidth(), overview.getHeight(), true);
            {
                juce::Graphics g (shot);
                overview.paint (g);
            }

            const auto map = overview.getMapForTesting();
            const auto rows = ovTimeline.getOverviewRows();
            auto rowY = [&] (int index) { return juce::roundToInt (map.contentYToY (rows[(size_t) index].top + rows[(size_t) index].trackAreaHeight * 0.5)); };

            const int xNotes = juce::roundToInt (map.timeToX (44.0));
            const int xEmpty = juce::roundToInt (map.timeToX (10.0));
            const int xStart = juce::roundToInt (map.timeToX (2.0));

            const auto notesPixel = shot.getPixelAt (xNotes, rowY (1));
            const auto emptyPixel = shot.getPixelAt (xEmpty, rowY (1));
            const auto startPixel = shot.getPixelAt (xStart, rowY (2));
            const auto track2 = juce::Colour::fromString (ovProject.getTrack (1).getColourString());
            const auto track3 = juce::Colour::fromString (ovProject.getTrack (2).getColourString());

            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("overview_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot, *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            check (notesPixel != emptyPixel && std::abs (notesPixel.getHue() - track2.getHue()) < 0.08f,
                   "track 2's notes at 44 s are drawn in the track's colour, and empty time is not  (notes "
                     + notesPixel.toDisplayString (false) + ", track " + track2.toDisplayString (false)
                     + ", empty " + emptyPixel.toDisplayString (false) + ")");

            check (xStart < ovTimeline.getTrackHeaderWidth() && std::abs (startPixel.getHue() - track3.getHue()) < 0.08f
                     && startPixel != emptyPixel,
                   "the song's start (track 3 at 2 s) is drawn at x " + juce::String (xStart) + ", where the blank above the track headers used to be  ("
                     + startPixel.toDisplayString (false) + ")");
        }

        //----------------------------------------------------------------------
        say ("--- the overview's scroll bars move the arrange window (8.359)");
        {
            auto& hBar = overview.getHorizontalScrollBarForTesting();
            auto& vBar = overview.getVerticalScrollBarForTesting();

            ovTimeline.setVisibleSecondsFromOverview (0.0, 20.0);
            ovTimeline.setViewFromOverview (0.0, 0.0);
            overview.tickForTesting();

            auto state = ovTimeline.getScrollStateForOverview();
            check (std::abs (hBar.getMaximumRangeLimit() - state.totalSeconds) < 1e-6
                     && std::abs (hBar.getCurrentRangeSize() - state.visibleSeconds) < 1e-6
                     && std::abs (vBar.getMaximumRangeLimit() - state.contentHeight) < 1e-6
                     && std::abs (vBar.getCurrentRangeSize() - state.visibleHeight) < 1e-6,
                   "the overview's scroll bars cover the same range as the arrange window's (" + juce::String (state.visibleSeconds, 1)
                     + " of " + juce::String (state.totalSeconds, 1) + " s, " + juce::String (state.visibleHeight) + " of "
                     + juce::String (state.contentHeight) + " px)");

            hBar.setCurrentRangeStart (30.0, juce::sendNotificationSync);
            state = ovTimeline.getScrollStateForOverview();
            check (std::abs (state.startSeconds - 30.0) < 1e-6,
                   "moving the overview's horizontal scroll bar to 30 s scrolls the arrange window there ("
                     + juce::String (state.startSeconds, 2) + " s)");

            vBar.setCurrentRangeStart (200.0, juce::sendNotificationSync);
            state = ovTimeline.getScrollStateForOverview();
            check (state.scrollY == 200 && std::abs (state.startSeconds - 30.0) < 1e-6,
                   "moving its vertical scroll bar scrolls the arrange window's rows (" + juce::String (state.scrollY)
                     + " px), leaving the time where it was");

            ovTimeline.setViewFromOverview (50.0, 120.0);   // アレンジ画面の側で動かす
            overview.tickForTesting();
            check (std::abs (hBar.getCurrentRangeStart() - 50.0) < 1e-6 && std::abs (vBar.getCurrentRangeStart() - 120.0) < 1e-6,
                   "scrolling the arrange window moves the overview's scroll bars with it ("
                     + juce::String (hBar.getCurrentRangeStart(), 1) + " s, " + juce::String (vBar.getCurrentRangeStart(), 0) + " px)");
        }

        //----------------------------------------------------------------------
        say ("--- clicking the overview moves the playback cursor (8.359)");
        {
            double sought = -1.0;
            ovTimeline.onSeek = [&] (double seconds) { sought = seconds; };

            ovTimeline.setViewFromOverview (0.0, 0.0);
            overview.tickForTesting();

            const auto map = overview.getMapForTesting();
            const auto viewBefore = ovTimeline.getScrollStateForOverview();

            const auto at80 = juce::Point<int> (juce::roundToInt (map.timeToX (80.0)), 20);
            overview.mouseDown (makeEvent (overview, at80, buttons (false), at80, false));
            const double expected80 = ovProject.snapTime (map.xToTime ((float) at80.x));

            check (std::abs (sought - expected80) < 1e-6 && std::abs (ovTimeline.getPlayheadSecondsForOverview() - expected80) < 1e-6
                     && std::abs (ovTimeline.getScrollStateForOverview().startSeconds - viewBefore.startSeconds) < 1e-9,
                   "clicking at 80 s moves the playback cursor there, snapped like the ruler (" + juce::String (sought, 3)
                     + " s), and leaves the arrange window's view alone (follow off)");

            const auto at100 = juce::Point<int> (juce::roundToInt (map.timeToX (100.0)), 20);
            overview.mouseDrag (makeEvent (overview, at100, buttons (false), at80, true));
            overview.mouseUp (makeEvent (overview, at100, buttons (false), at80, true));

            check (std::abs (sought - ovProject.snapTime (map.xToTime ((float) at100.x))) < 1e-6,
                   "dragging to 100 s takes the playback cursor along (" + juce::String (sought, 3) + " s)");

            const auto onLeft = juce::Point<int> (10, 20);   // ヘッダーの上だった所
            overview.mouseDown (makeEvent (overview, onLeft, buttons (false), onLeft, false));
            overview.mouseUp (makeEvent (overview, onLeft, buttons (false), onLeft, false));

            check (sought >= 0.0 && sought < 3.0,
                   "clicking near the left edge (above the track headers) moves the cursor to the song's start (" + juce::String (sought, 3) + " s)");

            ovTimeline.onSeek = nullptr;
        }

        //----------------------------------------------------------------------
        // 8.357：**マーカーを旗と線で**（Phase 346／本人の要望「追加でオーバービューにマーカーを表示させることできる？」
        // 「マーカーラベル旗も表示したい」）。前は中身の絵に 1px の線で焼き込んでいて、見分けにくかった
        say ("--- markers in the overview: a flag with the name, and a line (8.357)");
        {
            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            project.createNewProject();

            for (int i = 0; i < 6; ++i)
                project.addTrack ("Track " + juce::String (i + 1), TrackType::Midi);

            for (int n = 0; n < 40; ++n)
                project.getTrack (1).addNote (60, 100, 20.0 + n * 2.0, 1.5, nullptr);

            project.getTrack (0).addNote (60, 100, 120.0, 1.0, nullptr);
            project.addMarker (30.0, "Verse", nullptr);
            project.addMarker (60.0, "Chorus", nullptr);
            project.addMarker (61.0, "Outro", nullptr);   // Chorus のすぐ右（約10px）。Chorus の旗は名前を省く

            TimelineComponent timeline (project, cache, selection);
            timeline.setSize (1400, 600);
            ArrangeOverview markersOverview (project, timeline);
            markersOverview.setSize (1400, ArrangeOverview::preferredHeight);
            markersOverview.resized();
            markersOverview.tickForTesting();

            juce::Image shot (juce::Image::RGB, markersOverview.getWidth(), markersOverview.getHeight(), true);
            {
                juce::Graphics g (shot);
                markersOverview.paint (g);
            }

            const auto map = markersOverview.getMapForTesting();
            auto xOf = [&] (double seconds) { return (int) map.timeToX (seconds); };

            auto isPurple = [] (juce::Colour c)
            {
                const auto p = AppColours::purple;
                return std::abs (c.getRed() - p.getRed()) < 40 && std::abs (c.getGreen() - p.getGreen()) < 40
                    && std::abs (c.getBlue() - p.getBlue()) < 40;
            };

            // 旗の中（下端の行が紫の列）の白い画素＝名前の文字
            auto whiteIn = [&] (int x1, int x2)
            {
                int n = 0;

                for (int x = x1; x < x2; ++x)
                {
                    if (! isPurple (shot.getPixelAt (x, (int) ArrangeOverview::markerFlagHeight - 1)))   // 旗の下端の行（上端は外枠の線が重なる）
                        continue;

                    for (int y = 1; y < (int) ArrangeOverview::markerFlagHeight; ++y)
                        if (shot.getPixelAt (x, y).getGreen() > AppColours::purple.getGreen() + 70)   // 紫の上の白（細い字はにじむので緑の強さで見る）
                            ++n;
                }

                return n;
            };

            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("overview_markers_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot, *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            const int verse = xOf (30.0), chorus = xOf (60.0), outro = xOf (61.0);

            check (isPurple (shot.getPixelAt (verse, 40)) && isPurple (shot.getPixelAt (verse + 3, (int) ArrangeOverview::markerFlagHeight - 1))
                     && whiteIn (verse, verse + 60) > 10,
                   "the marker \"Verse\" at 30 s shows as a purple line down the overview and a purple flag at the top with its name in white  (line "
                     + shot.getPixelAt (verse, 40).toDisplayString (false) + ", " + juce::String (whiteIn (verse, verse + 60)) + " text pixels)");

            check (whiteIn (chorus, outro) == 0 && isPurple (shot.getPixelAt (chorus + 2, (int) ArrangeOverview::markerFlagHeight - 1))
                     && whiteIn (outro, outro + 60) > 10,
                   "a marker with a neighbour " + juce::String (outro - chorus) + " px to its right keeps a small flag without its name (no overlap); "
                   "the neighbour \"Outro\" shows its name");

            // マーカーを足すと、オーバービューにも出る
            project.addMarker (90.0, "Bridge", nullptr);
            markersOverview.tickForTesting();
            {
                juce::Graphics g (shot);
                markersOverview.paint (g);
            }

            check (isPurple (shot.getPixelAt (xOf (90.0), 40)),
                   "adding a marker at 90 s shows it in the overview");
        }

        //----------------------------------------------------------------------
        // 8.360：**パンチの範囲はルーラーの赤い帯で引く**（Phase 349）。ループの帯のすぐ下。操作はループの帯と同じ
        say ("--- the punch range strip on the ruler (8.360)");
        {
            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            project.createNewProject();
            project.addTrack ("Vocal", TrackType::Audio);
            project.setLoopRange (2.0, 3.0, nullptr);

            TimelineComponent timeline (project, cache, selection);
            timeline.setSize (1400, 600);
            timeline.setVisibleSecondsFromOverview (0.0, 20.0);

            const auto strip = timeline.getPunchStripAreaForTesting();
            const auto loopStrip = timeline.getLoopStripAreaForTesting();
            const int y = strip.getCentreY();
            auto xAt = [&] (double seconds) { return timeline.timeToXForTesting (seconds); };

            check (strip.getY() == loopStrip.getBottom() && strip.getHeight() > 0,
                   "the punch strip sits right under the loop strip (" + strip.toString() + ")");

            // 4 秒から 8 秒へ引く → 範囲ができて、パンチが入る。ループはそのまま
            {
                const juce::Point<int> from (xAt (4.0), y), to (xAt (8.0), y);
                timeline.mouseDown (makeEvent (timeline, from, buttons (false), from, false));
                timeline.mouseDrag (makeEvent (timeline, to, buttons (false), from, true));
                timeline.mouseUp (makeEvent (timeline, to, buttons (false), from, true));
            }

            check (project.isPunchEnabled() && std::abs (project.getPunchStartTime() - project.snapTime (4.0)) < 1e-6
                     && std::abs (project.getPunchEndTime() - project.snapTime (8.0)) < 1e-6
                     && std::abs (project.getLoopStartTime() - 2.0) < 1e-6 && std::abs (project.getLoopEndTime() - 3.0) < 1e-6,
                   "dragging 4-8 s on the red strip sets the punch range (" + juce::String (project.getPunchStartTime(), 2) + "-"
                     + juce::String (project.getPunchEndTime(), 2) + " s), turns punch on, and leaves the loop alone");

            // 終わりの端を掴んで 10 秒へ
            {
                const juce::Point<int> from (xAt (project.getPunchEndTime()), y), to (xAt (10.0), y);
                timeline.mouseDown (makeEvent (timeline, from, buttons (false), from, false));
                timeline.mouseDrag (makeEvent (timeline, to, buttons (false), from, true));
                timeline.mouseUp (makeEvent (timeline, to, buttons (false), from, true));
            }

            check (std::abs (project.getPunchStartTime() - project.snapTime (4.0)) < 1e-6
                     && std::abs (project.getPunchEndTime() - project.snapTime (10.0)) < 1e-6,
                   "dragging its end edge to 10 s stretches it, keeping the start (" + juce::String (project.getPunchStartTime(), 2) + "-"
                     + juce::String (project.getPunchEndTime(), 2) + " s)");

            // ダブルクリックで入切
            {
                const juce::Point<int> at (xAt (6.0), y);
                timeline.mouseDown (makeEvent (timeline, at, buttons (false), at, false));
                timeline.mouseUp (makeEvent (timeline, at, buttons (false), at, false));
                timeline.mouseDoubleClick (makeEvent (timeline, at, buttons (false), at, false));
            }

            check (! project.isPunchEnabled() && project.hasPunchRange(),
                   "double-clicking the red strip turns punch off and keeps the range");

            // 帯の色：範囲の中は赤
            juce::Image shot (juce::Image::RGB, 1400, 600, true);
            project.setPunchEnabled (true, nullptr);
            {
                juce::Graphics g (shot);
                timeline.paint (g);
            }

            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("punch_strip_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot.getClippedImage ({ 0, 0, 1400, 120 }), *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            const auto inside = shot.getPixelAt (xAt (7.0), y);
            check (std::abs (inside.getRed() - AppColours::punch().getRed()) < 30 && inside.getGreen() < 120,
                   "the range is drawn red on the strip (" + inside.toDisplayString (false) + ")");
        }

        //----------------------------------------------------------------------
        // 8.362：**落としたら入る範囲を、ドラッグ中に出す**（Phase 351／本人の要望）。
        // > 「ブラウザや外のフォルダなどから、オーディオデータやMIDIデータをArrange画面に挿入する際に、
        // >  挿入後どの位置に挿入されるかわかるように、ドラッグ中に範囲を表示できないかな？」
        // あわせて、**MIDI ファイルも置けるようにした**（前は音声ファイルしか受けていなかった）
        say ("--- where dropped files will land, shown while dragging (8.362)");
        {
            // 試験用のファイル（一時フォルダ。本人のフォルダには書かない）：2 秒の WAV を2つと、8 拍の MIDI
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("manta_drop_selftest");
            dir.createDirectory();

            auto writeWav = [] (const juce::File& file, double seconds)
            {
                file.deleteFile();
                juce::WavAudioFormat wav;
                juce::AudioBuffer<float> buffer (1, (int) (seconds * 44100.0));
                buffer.clear();

                if (auto stream = file.createOutputStream())
                {
                    std::unique_ptr<juce::OutputStream> out (stream.release());

                    if (auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                                                .withNumChannels (1)
                                                                                                .withBitsPerSample (16)))
                        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
                }
            };

            const auto wavA = dir.getChildFile ("Kick.wav");
            const auto wavB = dir.getChildFile ("Snare.wav");
            const auto mid = dir.getChildFile ("Riff.mid");
            writeWav (wavA, 2.0);
            writeWav (wavB, 2.0);

            {
                juce::MidiMessageSequence sequence;
                sequence.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
                sequence.addEvent (juce::MidiMessage::noteOff (1, 60), 960.0);       // 2 拍
                sequence.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 90), 1920.0);
                sequence.addEvent (juce::MidiMessage::noteOff (1, 64), 3840.0);      // 4〜8 拍
                sequence.updateMatchedPairs();

                juce::MidiFile file;
                file.setTicksPerQuarterNote (480);
                file.addTrack (sequence);
                mid.deleteFile();

                if (auto stream = mid.createOutputStream())
                    file.writeTo (*stream);
            }

            // MIDI ファイルの読み込み：拍で読み、落とした拍から入れる
            {
                const auto contents = MidiFileImport::read (mid);

                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Keys", TrackType::Midi);
                const int added = MidiFileImport::addToTrack (track, contents, 8.0, nullptr);

                bool placed = added == 2;

                for (int i = 0; i < track.getNumNotes(); ++i)
                {
                    const auto note = track.getNote (i);
                    placed = placed && ((note.getPitch() == 60 && std::abs (note.getStartBeats() - 8.0) < 1e-6 && std::abs (note.getLengthBeats() - 2.0) < 1e-6)
                                     || (note.getPitch() == 64 && std::abs (note.getStartBeats() - 12.0) < 1e-6 && std::abs (note.getLengthBeats() - 4.0) < 1e-6));
                }

                check (contents.ok && std::abs (contents.lengthBeats - 8.0) < 1e-6 && placed,
                       "a MIDI file is read in beats (2 notes, 8 beats long) and goes in from the beat it was dropped on (beat 8: notes at 8 and 12)");
            }

            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            project.createNewProject();
            project.addTrack ("Drums", TrackType::Audio);
            project.addTrack ("Keys", TrackType::Midi);
            project.addTrack ("Reverb", TrackType::Send);

            TimelineComponent timeline (project, cache, selection);
            timeline.setSize (1400, 600);
            timeline.setVisibleSecondsFromOverview (0.0, 20.0);

            const int x4 = timeline.timeToXForTesting (4.0);
            const int drumsY = timeline.getPointForTesting (0, 4.0).y;
            const int keysY = timeline.getPointForTesting (1, 4.0).y;
            const double snapped4 = project.snapTime (4.0);

            // 音声 2 つを Drums の行へ → その行に 4 秒から順に 2 秒ずつ
            timeline.setFileDragForTesting ({ wavA.getFullPathName(), wavB.getFullPathName() }, x4, drumsY);
            auto preview = timeline.getDropPreview();

            check (preview.size() == 2
                     && std::abs (preview[0].bounds.getX() - timeline.timeToXForTesting (snapped4)) <= 1
                     && std::abs (preview[0].bounds.getRight() - timeline.timeToXForTesting (snapped4 + 2.0)) <= 1
                     && std::abs (preview[1].bounds.getX() - timeline.timeToXForTesting (snapped4 + 2.0)) <= 1
                     && preview[0].name == "Kick.wav" && preview[1].name == "Snare.wav",
                   "dragging two 2-second audio files over an audio row shows where they will go: one after the other from 4 s ("
                     + (preview.empty() ? juce::String ("nothing") : preview[0].bounds.toString()) + ")");

            // MIDI を Keys の行へ → 8 拍（120BPM で 4 秒）の枠
            timeline.setFileDragForTesting ({ mid.getFullPathName() }, x4, keysY);
            preview = timeline.getDropPreview();
            const double midEnd = project.getTimeForBeatPosition (project.getBeatPositionAt (snapped4) + 8.0);

            check (preview.size() == 1 && std::abs (preview[0].bounds.getRight() - timeline.timeToXForTesting (midEnd)) <= 1,
                   "dragging an 8-beat MIDI file over a MIDI row shows it lasting 8 beats (to " + juce::String (midEnd, 2) + " s)");

            // 絵（目で見る用。一時フォルダ）
            {
                timeline.setFileDragForTesting ({ wavA.getFullPathName(), wavB.getFullPathName() }, x4, drumsY);
                juce::Image shot (juce::Image::RGB, 1400, 300, true);
                {
                    juce::Graphics g (shot);
                    timeline.paint (g);
                }

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drop_preview_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot, *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            // 音声を MIDI の行へ → 置けない（枠を出さない）
            timeline.setFileDragForTesting ({ wavA.getFullPathName() }, x4, keysY);
            check (timeline.getDropPreview().empty(),
                   "an audio file over a MIDI row shows nothing (it cannot go there)");

            // 空白へ → 1ファイル＝1本の新しい行。**センドの手前**（8.361）
            const int emptyY = timeline.getHeight() - 30;
            timeline.setFileDragForTesting ({ wavA.getFullPathName(), mid.getFullPathName() }, x4, emptyY);
            preview = timeline.getDropPreview();
            const int sendTop = timeline.getPointForTesting (2, 4.0).y;   // Reverb の行の中ほど

            check (preview.size() == 2 && preview[1].bounds.getY() > preview[0].bounds.getY()
                     && preview[0].bounds.getY() < sendTop,
                   "dropping on the empty area shows one new row per file, starting above the send tracks ("
                     + (preview.empty() ? juce::String ("nothing") : preview[0].bounds.toString()) + ")");

            timeline.clearFileDragForTesting();
            check (timeline.getDropPreview().empty(), "leaving the window clears the preview");

            dir.deleteRecursively();
        }

        //----------------------------------------------------------------------
        // 8.365：**ピアノロールでベンドを付ける**（Phase 353／本人の要望）。「ベンド」を入れて、ノートの終わり・頭を上下に引く
        say ("--- bending notes in the piano roll (8.365)");
        {
            ProjectModel project;
            project.createNewProject();
            auto track = project.addTrack ("Lead", TrackType::Midi);
            track.setPitchBendRange (12.0, nullptr);
            auto c4 = track.addNote (60, 100, 1.0, 1.0, nullptr);   // [1, 2)
            track.addNote (64, 100, 2.0, 1.0, nullptr);             // 次のノート E4

            PianoRollComponent roll (project);
            roll.setTrack (track);
            roll.setSize (1400, 3000);
            roll.setVisibleVerticalRange (0, 3000);

            auto drag = [&] (juce::Point<float> from, juce::Point<int> by, bool shift)
            {
                const auto a = from.roundToInt();
                const auto b = a + by;
                const auto mods = shift ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)
                                        : buttons (false);
                roll.mouseDown (makeEvent (roll, a, mods, a, false));
                roll.mouseDrag (makeEvent (roll, b, mods, a, true));
                roll.mouseUp (makeEvent (roll, b, mods, a, true));
            };

            const auto handles = roll.getBendHandles (c4);
            // 1 行の高さ：E4 と C4 の真ん中の差 ÷ 4
            auto e4 = track.getNote (1);
            const float rowHeight = (roll.getBendHandles (c4).end.y - roll.getBendHandles (e4).start.y) / 4.0f;

            // ベンドを入れずに終わりを引いても、曲がらない（今までどおり伸縮）
            drag (handles.end + juce::Point<float> (-2.0f, 0.0f), { 0, juce::roundToInt (rowHeight * 5) }, false);
            const bool untouched = c4.getBend().isEmpty();
            c4.setStartTime (1.0, nullptr);   // 引いたぶんを戻す（Undo だとトラックの追加まで戻る）
            c4.setLength (1.0, nullptr);
            c4.setPitch (60, nullptr);

            roll.setBendEditing (true);

            // 終わりを 5 行下へ → フォール -5
            drag (roll.getBendHandles (c4).end, { 0, juce::roundToInt (rowHeight * 5) }, false);
            const double fall = c4.getBend().endSemitones;

            // 頭を 2 行上へ → 頭から +2（上から降りてくる）
            drag (roll.getBendHandles (c4).start, { 0, -juce::roundToInt (rowHeight * 2) }, false);
            const double fromStart = c4.getBend().startSemitones;

            check (untouched && std::abs (fall + 5.0) < 1e-9 && std::abs (fromStart - 2.0) < 1e-9,
                   "with Bend on, pulling a note's end 5 rows down makes a fall of -5, and pulling its start 2 rows up bends it in from +2 "
                   "(without Bend, the same pull does not bend: " + juce::String (fall, 2) + ", " + juce::String (fromStart, 2) + ")");

            // 0.5 半音刻み。Shift で刻まない
            drag (roll.getBendHandles (c4).end, { 0, -juce::roundToInt (rowHeight * 0.3f) }, false);
            const double snapped = c4.getBend().endSemitones;
            drag (roll.getBendHandles (c4).end, { 0, -juce::roundToInt (rowHeight * 0.3f) }, true);
            const double free = c4.getBend().endSemitones;

            check (std::abs (snapped + 4.5) < 1e-9 && std::abs (free - snapped) > 0.1 && std::abs (free - snapped) < 0.5,
                   "the amount moves in half steps (-5 nudged up a little becomes -4.5); with Shift it follows the mouse (" + juce::String (free, 2) + ")");

            // 折れ目（曲がり始め）を左右に
            const double kneeBefore = c4.getBend().endPosition;
            drag (roll.getBendHandles (c4).endKnee, { -40, 0 }, false);
            check (c4.getBend().endPosition < kneeBefore - 0.05,
                   "dragging the knee left starts the fall earlier (" + juce::String (kneeBefore, 2) + " -> " + juce::String (c4.getBend().endPosition, 2) + ")");

            // メニューの操作（いま選ばれているのは C4）
            roll.slideSelectedNotesToNext();
            const double slide = c4.getBend().endSemitones;
            roll.setVibratoOnSelectedNotes (0.3, 5.5);
            const double vibrato = c4.getBend().vibratoDepth;

            check (std::abs (slide - 4.0) < 1e-9 && std::abs (vibrato - 0.3) < 1e-9,
                   "\"slide into the next note\" ends the bend on the next note's pitch (+4 to E4), and vibrato can be added");

            // 割ると、頭のベンドは前半、終わりのベンドとビブラートは後半
            roll.setBendEditing (false);
            roll.setEditTool (EditTool::cut);
            {
                const auto middle = juce::Point<int> (juce::roundToInt ((roll.getBendHandles (c4).start.x + roll.getBendHandles (c4).end.x) * 0.5f),
                                                      juce::roundToInt (roll.getBendHandles (c4).startKnee.y));
                roll.mouseDown (makeEvent (roll, middle, buttons (false), middle, false));
                roll.mouseUp (makeEvent (roll, middle, buttons (false), middle, false));
            }

            NoteBend firstHalf, secondHalf;
            int halves = 0;

            for (int i = 0; i < track.getNumNotes(); ++i)
            {
                const auto note = track.getNote (i);

                if (note.getPitch() != 60)
                    continue;

                ++halves;
                (note.getStartTime() < 1.2 ? firstHalf : secondHalf) = note.getBend();
            }

            check (halves == 2 && firstHalf.hasStart() && ! firstHalf.hasEnd() && ! firstHalf.hasVibrato()
                     && secondHalf.hasEnd() && secondHalf.hasVibrato() && ! secondHalf.hasStart(),
                   "cutting a bent note keeps the start bend on the first half and the end bend and vibrato on the second");

            // 絵（目で見る用）
            {
                roll.setEditTool (EditTool::arrow);
                roll.setBendEditing (true);
                const auto area = juce::Rectangle<int> (0, juce::roundToInt (roll.getBendHandles (e4).start.y - rowHeight * 8),
                                                        1400, juce::roundToInt (rowHeight * 18));
                juce::Image shot (juce::Image::RGB, roll.getWidth(), roll.getHeight(), true);
                {
                    juce::Graphics g (shot);
                    roll.paint (g);
                }

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("note_bend_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot.getClippedImage (area), *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }
        }

        //----------------------------------------------------------------------
        // 8.366：**レーンに「戻ってくる値」の線**（Phase 354／本人の要望）。
        // > 「ボリュームやPAN、その他の項目、デフォルトで開くと空白であるが、最初から設定値の線を引くことはできる？
        // >  ボリュームやPAN、ピッチベンドなど、最初に設定した値に戻ってきたい時に迷子になる。アレンジ画面でのレーンと同じ見た目で構わない」
        // 点が無ければ実線（アレンジ画面と同じ）、あれば点線の目印。点を目印の近くに置く・動かすと、その値に吸い付く
        say ("--- a line for the value to come back to, in the piano roll's lane (8.366)");
        {
            ProjectModel project;
            project.createNewProject();
            auto track = project.addTrack ("Keys", TrackType::Midi);
            track.setVolumeDb (-6.0f, nullptr);
            track.addNote (60, 100, 1.0, 1.0, nullptr);

            PianoRollComponent roll (project);
            roll.setTrack (track);
            roll.setSize (1400, 3000);
            roll.setVisibleVerticalRange (0, 3000);

            auto laneFor = [&] (PianoRollComponent::LaneTarget::Kind kind, int controller, const juce::String& target)
            {
                PianoRollComponent::LaneTarget t;
                t.kind = kind;
                t.controllerNumber = controller;
                t.automationTargetId = target;
                roll.setLaneTarget (t);
            };

            auto pixelIsLine = [&] (int y)
            {
                juce::Image shot (juce::Image::RGB, roll.getWidth(), roll.getHeight(), true);
                {
                    juce::Graphics g (shot);
                    roll.paint (g);
                }

                // 線の上と、その 6px 上の地を比べる（線は紫を重ねてある）。点線の隙間に当たらないよう、8px ぶん見る
                for (int x = 300; x < 308; ++x)
                {
                    const auto on = shot.getPixelAt (x, y);
                    const auto off = shot.getPixelAt (x, y - 6);

                    if (on != off && on.getBlue() >= on.getGreen())
                        return true;
                }

                return false;
            };

            auto clickLane = [&] (int y)
            {
                const juce::Point<int> at (700, y);
                roll.mouseDown (makeEvent (roll, at, buttons (false), at, false));
                roll.mouseUp (makeEvent (roll, at, buttons (false), at, false));
            };

            // CC7 ボリューム：100 の高さに線。3px ずれて置いても 100、12px ずれれば 100 ではない
            laneFor (PianoRollComponent::LaneTarget::Kind::cc, 7, {});
            const int volumeY = roll.ccValueToYForTesting (7, 100);
            const bool volumeLine = pixelIsLine (volumeY);

            clickLane (volumeY + 3);
            const int snapped = track.getNumCCEventsFor (7) > 0 ? track.getCCEventFor (7, 0).getValue() : -1;

            const juce::Point<int> far (900, volumeY + 12);
            roll.mouseDown (makeEvent (roll, far, buttons (false), far, false));
            roll.mouseUp (makeEvent (roll, far, buttons (false), far, false));
            const int notSnapped = track.getNumCCEventsFor (7) > 1 ? track.getCCEventFor (7, 1).getValue() : -1;

            check (volumeLine && snapped == 100 && notSnapped >= 0 && notSnapped != 100,
                   "the CC7 volume lane shows a line at 100 before any point is placed; a point placed 3 px off lands on 100, "
                   "12 px off does not (" + juce::String (snapped) + ", " + juce::String (notSnapped) + ")");

            // 点があっても、目印は残る（点線）
            check (pixelIsLine (volumeY), "with points in the lane, the line stays as a dotted guide");

            {
                juce::Image shot (juce::Image::RGB, roll.getWidth(), roll.getHeight(), true);
                {
                    juce::Graphics g (shot);
                    roll.paint (g);
                }

                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lane_home_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (shot.getClippedImage (roll.getLaneBoundsForTesting().withWidth (1400)), *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            // パン 64・ピッチベンド中央・モジュレーション 0
            check (MidiControllers::getHomeValue (10) == 64 && MidiControllers::getHomeValue (MidiControllers::pitchBend) == 8192
                     && MidiControllers::getHomeValue (11) == 127 && MidiControllers::getHomeValue (1) == 0,
                   "the values to come back to: pan 64, pitch bend centre, expression 127, modulation 0");

            laneFor (PianoRollComponent::LaneTarget::Kind::cc, MidiControllers::pitchBend, {});
            check (pixelIsLine (roll.ccValueToYForTesting (MidiControllers::pitchBend, 8192)),
                   "the pitch bend lane shows a line at the centre");

            // オートメーション（ボリューム）：トラックのいまの値（-6 dB）の高さ。近くに置けばその値
            laneFor (PianoRollComponent::LaneTarget::Kind::automation, 0, AutomationTargets::volume);
            const float home = roll.getAutomationHomeValueForTesting();
            const int homeY = roll.automationValueToYForTesting (home);
            const bool automationLine = pixelIsLine (homeY);

            clickLane (homeY - 2);
            auto lane = track.findAutomationLane (AutomationTargets::volume);
            const float placed = lane.state.isValid() && lane.getNumPoints() > 0 ? lane.getPoint (0).getValue() : -1.0f;

            check (automationLine && std::abs (placed - home) < 1.0e-6
                     && std::abs (home - AutomationTargets::fromParameterValue (AutomationTargets::volume, -6.0)) < 1.0e-6,
                   "the volume automation lane shows a line at the track's own volume (-6 dB), and a point placed next to it takes that value");
        }

        //----------------------------------------------------------------------
        // 8.353：**アレンジ画面の横スクロールを、ピアノロールのように滑らかに**（Phase 342／本人の要望）。
        //
        // 測ってみると（16本×2000音・8区間）、横スクロール1回ぶんの処理が 9.7ms、描き直しが 33ms あった。
        // ①曲の端を全ノートから数え直す計算を、スクロールのたびに2回 ②ノートの塊を描くたびに全トラックぶん作り直し、
        // 塊1つ描くたびに全ノートを2周 ③ヘッダーの列（とその上の部品）まで毎回描き直し。
        // 曲が変わったときだけ数え直して覚えておき、横スクロールではヘッダーの列を描き直さない
        say ("--- smooth horizontal scrolling in the arrange window");
        {
            ProjectModel project;
            SelectionState selection;
            WaveformCache cache;
            project.createNewProject();

            for (int t = 0; t < 16; ++t)
            {
                auto track = project.addTrack ("T" + juce::String (t), TrackType::Midi);

                for (int n = 0; n < 2000; ++n)
                    track.addNote (48 + (n * 7 + t) % 24, 100, n * 0.09 + (n / 250) * 8.0, 0.08, nullptr);   // 8つの区間（あいだに8秒）
            }

            TimelineComponent timeline (project, cache, selection);
            timeline.setSize (1600, 900);
            timeline.setVisibleSecondsFromOverview (0.0, 20.0);

            juce::Image img (juce::Image::RGB, 1600, 900, true);
            { juce::Graphics g (img); timeline.paint (g); }   // 覚えを作らせる

            // 目で見る用に1枚落とす（塊の中のノートが描けているか。一時フォルダ）
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("timeline_selftest.png");
                juce::PNGImageFormat png;

                if (auto stream = file.createOutputStream())
                {
                    stream->setPosition (0);
                    stream->truncate();
                    png.writeImageToStream (img, *stream);
                    say ("        (a picture of it: " + file.getFullPathName() + ")");
                }
            }

            const int runs = 50;
            const double t0 = juce::Time::getMillisecondCounterHiRes();

            for (int i = 1; i <= runs; ++i)
                timeline.setViewFromOverview ((double) i, 0.0);

            const double perScroll = (juce::Time::getMillisecondCounterHiRes() - t0) / runs;

            const auto area = timeline.getRepaintAreaForHorizontalScrollForTesting();
            const double t1 = juce::Time::getMillisecondCounterHiRes();

            for (int i = 0; i < 10; ++i)
            {
                juce::Graphics g (img);
                g.reduceClipRegion (area);
                timeline.paint (g);
            }

            const double perPaint = (juce::Time::getMillisecondCounterHiRes() - t1) / 10.0;
            say ("  info  one scroll step " + juce::String (perScroll, 3) + " ms, repainting the scrolled area "
                   + juce::String (perPaint, 2) + " ms (was 9.7 ms and 33 ms for the whole window)");

            check (perScroll < 2.0,
                   "one horizontal scroll step no longer recounts the whole song (" + juce::String (perScroll, 3) + " ms, limit 2 ms; it was 9.7 ms)");

            check (area.getX() == timeline.getTrackHeaderWidth() && area.getRight() == timeline.getWidth(),
                   "horizontal scrolling repaints only the area right of the track headers (" + area.toString() + ")");

            // 覚えた塊は、曲を編集したら作り直す（古い塊を描き続けない）
            const auto before = timeline.getNoteBlocksForTesting (3).size();
            project.getTrack (3).addNote (60, 100, 400.0, 1.0, nullptr);   // 離れた所に1音 → 塊が1つ増える
            const auto after = timeline.getNoteBlocksForTesting (3).size();

            check (after == before + 1,
                   "after adding a note far away, the remembered note blocks are rebuilt (" + juce::String ((int) before)
                     + " -> " + juce::String ((int) after) + " blocks)");
        }

        //----------------------------------------------------------------------
        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
