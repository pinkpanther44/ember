#include "StorageSelfTest.h"

#include "ProjectModel.h"
#include "MidiPlayerProcessor.h"   // 8.307：ずらした量が鳴る位置に出ること（Phase 300）
#include "MidiFileExporter.h"     // 8.344：同じ音程が続くノートの書き出し（Phase 333）
#include "EditorSlots.h"          // 8.347：下段と窓の中身（Phase 335）
#include "StorageLocations.h"
#include "ExportOptions.h"   // 8.319：書き出したファイルの中身（Phase 312）
#include "CrashLog.h"        // 8.320：起動時に知らせる記録（Phase 312）
#include "AppMessageBox.h"   // 8.322：ボタンの番号の戻し方（Phase 312）
#include "PunchRecording.h"  // 8.360：パンチイン／アウト（Phase 349）

#include <juce_events/juce_events.h>

#include <iostream>
#include <map>
#include <random>

namespace StorageSelfTest
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
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--storage-selftest"))
            return false;

        problems = 0;

        say ("project storage self-test (8.286)");

        // **一時フォルダの中だけで済ませます**（本人の設定もファイルも触らない）
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("manta-storage-selftest");

        root.deleteRecursively();
        root.createDirectory();

        say ("--- root: " + root.getFullPathName());

        const auto extension = ProjectModel::getFileExtension();

        //----------------------------------------------------------------------
        // ① 根の直下に保存しようとすると、その曲のフォルダへ入る

        say ("--- where a new project lands");

        {
            const auto chosen = root.getChildFile ("My Song" + extension);
            const auto placed = StorageLocations::makeProjectFileInOwnFolder (chosen, root);

            check (placed.getParentDirectory() == root.getChildFile ("My Song"),
                    "saving into the root puts the project in a folder of its own");
            check (placed.getFileName() == chosen.getFileName(), "...with the name that was typed");
            check (placed.getParentDirectory().isDirectory(), "...and the folder exists");
        }

        //----------------------------------------------------------------------
        // ② **それ以外はそのまま**（選んだ場所と結果が食い違わないこと）

        {
            auto elsewhere = root.getChildFile ("Some Folder");
            elsewhere.createDirectory();

            const auto chosen = elsewhere.getChildFile ("Another Song" + extension);
            const auto placed = StorageLocations::makeProjectFileInOwnFolder (chosen, root);

            check (placed == chosen, "a file chosen inside another folder is left where it was");

            // 既にその曲のフォルダの中を選んだとき（2回目の「名前を付けて保存」）
            const auto inOwnFolder = root.getChildFile ("My Song").getChildFile ("My Song" + extension);

            check (StorageLocations::makeProjectFileInOwnFolder (inOwnFolder, root) == inOwnFolder,
                    "a file already inside its own folder does not gain another level");
        }

        //----------------------------------------------------------------------
        // ③ 中の置き場所

        say ("--- the folders inside a project");

        {
            const auto projectFile = root.getChildFile ("My Song").getChildFile ("My Song" + extension);

            using PF = StorageLocations::ProjectFolder;

            check (StorageLocations::getProjectFolder (projectFile, PF::backups, false).getFileName() == "Backup",
                    "Backup sits next to the project file");
            check (StorageLocations::getProjectFolder (projectFile, PF::recordings, false).getFileName() == "Rec",
                    "so does Rec");
            check (StorageLocations::getProjectFolder (projectFile, PF::stems, false).getFileName() == "Stems",
                    "so does Stems");
            check (StorageLocations::getProjectFolder (projectFile, PF::mixdown, false).getFileName() == "Mixdown",
                    "so does Mixdown");

            // **未保存では空を返すこと**（呼び出し側が「まだ無い」と分かるように）
            check (StorageLocations::getProjectFolder (juce::File(), PF::recordings, false) == juce::File(),
                    "an unsaved project has no folder of its own, and says so");

            // **読むだけでは作らないこと**（書き出しをやめただけで空のフォルダが増えない）
            check (! StorageLocations::getProjectFolder (projectFile, PF::stems, false).isDirectory(),
                    "asking where Stems would go does not create it");

            // 8.346：**保存したら、書き出し先（Stems・Mixdown）も作る**（Phase 333／本人の要望）。
            // 無いフォルダを選ぶ画面に渡すと、Windows が「フォルダが見つかりません」と注意していた
            projectFile.getParentDirectory().createDirectory();
            StorageLocations::createExportFolders (projectFile);
            StorageLocations::createExportFolders (projectFile);   // 2回呼んでも困らない

            check (StorageLocations::getProjectFolder (projectFile, PF::stems, false).isDirectory()
                     && StorageLocations::getProjectFolder (projectFile, PF::mixdown, false).isDirectory(),
                    "after a save, Stems and Mixdown are there next to the project file");

            StorageLocations::createExportFolders (juce::File());   // 未保存：何もしない（落ちない）
        }

        //----------------------------------------------------------------------
        // ④ 保存の控えが`Backup`へ入る

        say ("--- the backup copy of a save");

        {
            const auto projectFile = root.getChildFile ("My Song").getChildFile ("My Song" + extension);

            ProjectModel project;
            project.createNewProject();
            project.setName ("My Song", nullptr);

            check (project.saveToFile (projectFile), "the project saves");
            check (projectFile.existsAsFile(), "...and the file is there");

            const auto backup = root.getChildFile ("My Song").getChildFile ("Backup")
                                    .getChildFile (projectFile.getFileName() + ".bak");

            check (! backup.existsAsFile(), "the first save leaves no backup (there was nothing to keep)");

            // 2回目の保存で、1回目の中身が控えになる
            check (project.saveToFile (projectFile), "the project saves again");
            check (backup.existsAsFile(), "...and the second save keeps the previous one in Backup");

            // Phase 278までは**ファイルの隣**でした。そちらには置かないこと
            check (! projectFile.getSiblingFile (projectFile.getFileName() + ".bak").existsAsFile(),
                    "the backup is no longer left beside the project file");
        }

        //----------------------------------------------------------------------
        // ⑤ 後始末

        root.deleteRecursively();
        check (! root.isDirectory(), "the temporary folder is cleaned up");

        //----------------------------------------------------------------------
        // 8.307：**MIDIディレイ**（Phase 300／本人の指定）。
        //
        // **保存して開き直しても残ること**と、**実際に鳴る位置が動くこと**の両方を見ます。
        // 片方だけだと、「設定は残るのに音は動かない」が通り抜けます。

        say ("--- the MIDI delay");

        {
            const auto projectFile = root.getChildFile ("Delay").getChildFile ("Delay" + extension);

            ProjectModel project;

            // **先に作ること。** 既定のままのモデルは保存できません
            // （上の「控え」の節と同じ手順）
            project.createNewProject();
            project.setName ("Delay", nullptr);

            auto track = project.addTrack ("Keys", TrackType::Midi);

            // 4秒の位置に1秒のノート（数えやすい値）
            track.addNote (60, 100, 4.0, 1.0, nullptr);

            check (juce::approximatelyEqual (track.getMidiDelayMs(), 0.0),
                    "a new track does not shift its MIDI");

            // **0のときはプロパティごと消えていること**（既定値をファイルへ書き残さない）
            track.setMidiDelayMs (0.0, nullptr);
            check (! track.state.hasProperty (IDs::midiDelayMs),
                    "zero is not written into the file at all");

            track.setMidiDelayMs (-40.0, nullptr);
            check (juce::approximatelyEqual (track.getMidiDelayMs(), -40.0),
                    "a negative delay (earlier) is kept");

            // **範囲の外は丸める**（手で書き替えたファイルでも壊れない）
            track.setMidiDelayMs (9999.0, nullptr);
            check (juce::approximatelyEqual (track.getMidiDelayMs(), Track::midiDelayLimitMs),
                    "a wild value is pulled back to the limit");

            track.setMidiDelayMs (120.0, nullptr);

            // 保存して開き直す。**入れ物は自分で作ること**——
            // `saveToFile()`はフォルダを作りません（上の節が通っているのは、
            // ①でその曲のフォルダが先に作られているからです）
            projectFile.getParentDirectory().createDirectory();

            check (project.saveToFile (projectFile), "the project saves");

            ProjectModel reopened;
            check (reopened.loadFromFile (projectFile), "...and opens again");

            auto reopenedTrack = reopened.getTrack (0);

            check (juce::approximatelyEqual (reopenedTrack.getMidiDelayMs(), 120.0),
                    "the delay survives the round trip");

            //------------------------------------------------------------------
            // **鳴る位置が動くこと。** ここが肝で、モデルだけ見ても分かりません。
            //
            // `getEndPositionSamples()`は並べ終えたノートの終わりを返すので、
            // **ずらした量がそのまま差**として出ます

            Transport transport;
            MidiPlayerProcessor player { reopened, transport, reopenedTrack.getId() };

            constexpr double sampleRate = 48000.0;

            player.prepareToPlay (sampleRate, 512);

            reopenedTrack.setMidiDelayMs (0.0, nullptr);
            player.prepareNotesForPlayback();
            const auto endWithout = player.getEndPositionSamples();

            reopenedTrack.setMidiDelayMs (120.0, nullptr);
            player.prepareNotesForPlayback();
            const auto endLate = player.getEndPositionSamples();

            reopenedTrack.setMidiDelayMs (-120.0, nullptr);
            player.prepareNotesForPlayback();
            const auto endEarly = player.getEndPositionSamples();

            const auto expected = (juce::int64) (0.120 * sampleRate);

            check (endLate - endWithout == expected,
                    "+120ms moves the notes exactly 120ms later  ("
                      + juce::String (endLate - endWithout) + " samples)");

            check (endWithout - endEarly == expected,
                    "-120ms moves them exactly 120ms earlier  ("
                      + juce::String (endWithout - endEarly) + " samples)");

            // **書かれているノートは動いていないこと**（鳴らし方だけの設定）
            check (juce::approximatelyEqual (reopenedTrack.getNote (0).getStartTime(), 4.0),
                    "...and the note itself never moved");

            // **曲の頭より手前へは行かない**（負のサンプルは鳴らないので、音が消える）
            auto early = reopened.getTrack (0);
            early.addNote (64, 100, 0.01, 0.5, nullptr);
            early.setMidiDelayMs (-Track::midiDelayLimitMs, nullptr);
            player.prepareNotesForPlayback();

            check (player.getEndPositionSamples() > 0,
                    "a note near the start is not thrown away by a big negative delay");
        }

        //----------------------------------------------------------------------
        // 8.344：**同じ音程を続けて打つと、たまに鳴らない**（Phase 333／本人の報告。ベースでよく出た）。
        //
        // 同じ時刻に「前のノートのオフ」と「次のノートのオン」が来ると、**足した順**で並びます。
        // 一覧の順（＝打ち込んだ順）で足していたので、**後ろのノートを先に打つと**「オン → オフ」になり、
        // 鳴らした瞬間に止まっていました。ここでは**わざと後ろから打って**、その形を作ります。
        // 単音の音源と同じ見方（オフはその音程を止める）で、鳴った区間を数えます

        say ("--- the same pitch played back to back");

        {
            ProjectModel project;
            project.createNewProject();

            auto track = project.addTrack ("Bass", TrackType::Midi);

            // 隣り合う 2 つ（後ろを先に打つ）：A [0.5, 1.0) と B [1.0, 1.5)
            track.addNote (40, 100, 1.0, 0.5, nullptr);
            track.addNote (40, 100, 0.5, 0.5, nullptr);
            // 重なる 2 つ（後ろを先に打つ）：C [2.0, 2.8) と D [2.5, 3.0) → C は 2.5 で切れる
            track.addNote (45, 100, 2.5, 0.5, nullptr);
            track.addNote (45, 100, 2.0, 0.8, nullptr);
            // 頭が同じ 2 つ：長いほう [3.5, 3.9) だけ残る
            track.addNote (47, 90, 3.5, 0.2, nullptr);
            track.addNote (47, 100, 3.5, 0.4, nullptr);

            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 512;

            auto at = [] (double seconds) { return (juce::int64) (seconds * sampleRate); };

            struct Segment { int pitch; juce::int64 from, to; };

            const std::vector<Segment> expected { { 40, at (0.5), at (1.0) }, { 40, at (1.0), at (1.5) },
                                                  { 45, at (2.0), at (2.5) }, { 45, at (2.5), at (3.0) },
                                                  { 47, at (3.5), at (3.9) } };

            Transport transport;
            transport.prepare (sampleRate);

            MidiPlayerProcessor player { project, transport, track.getId() };
            player.prepareToPlay (sampleRate, blockSize);
            player.prepareNotesForPlayback();
            transport.start (at (4.5));

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            std::map<int, juce::int64> soundingSince;
            std::vector<Segment> heard;

            while (transport.isPlaying() && transport.getPositionSamples() < at (4.5))
            {
                const auto blockStart = transport.getPositionSamples();
                midi.clear();
                player.processBlock (buffer, midi);

                for (const auto metadata : midi)
                {
                    const auto message = metadata.getMessage();
                    const auto time = blockStart + metadata.samplePosition;

                    if (message.isNoteOn())
                    {
                        soundingSince[message.getNoteNumber()] = time;
                    }
                    else if (message.isNoteOff())
                    {
                        auto it = soundingSince.find (message.getNoteNumber());

                        if (it != soundingSince.end())
                        {
                            heard.push_back ({ it->first, it->second, time });
                            soundingSince.erase (it);
                        }
                    }
                }

                transport.advance (blockSize);
            }

            auto describe = [] (const std::vector<Segment>& segments)
            {
                juce::StringArray parts;

                for (const auto& s : segments)
                    parts.add (juce::String (s.pitch) + " " + juce::String ((double) s.from / sampleRate, 3)
                                 + "-" + juce::String ((double) s.to / sampleRate, 3));

                return parts.joinIntoString (", ");
            };

            std::sort (heard.begin(), heard.end(), [] (const Segment& a, const Segment& b)
                       { return a.pitch != b.pitch ? a.pitch < b.pitch : a.from < b.from; });

            bool same = heard.size() == expected.size();

            for (size_t i = 0; same && i < heard.size(); ++i)
                same = heard[i].pitch == expected[i].pitch && heard[i].from == expected[i].from && heard[i].to == expected[i].to;

            check (same && soundingSince.empty(),
                   "playback: each note sounds for its whole length, even when the later one was entered first  ("
                     + describe (heard) + ")");

            // **MIDI ファイルの書き出しも同じ**（同じ時刻で「オン → オフ」だと、読み込んだ側で次のノートが長さ 0 になる）
            const auto midiFile = root.getChildFile ("SamePitch").getChildFile ("SamePitch.mid");
            midiFile.getParentDirectory().createDirectory();

            const auto error = MidiFileExporter::exportToFile (project, midiFile);

            juce::MidiFile read;
            bool readOk = false;

            if (error.isEmpty())
                if (auto stream = midiFile.createInputStream())
                    readOk = read.readFrom (*stream);

            std::map<int, int> notesPerPitch;
            int zeroLength = 0;

            for (int t = 0; readOk && t < read.getNumTracks(); ++t)
            {
                juce::MidiMessageSequence sequence (*read.getTrack (t));
                sequence.updateMatchedPairs();

                for (int e = 0; e < sequence.getNumEvents(); ++e)
                {
                    const auto* event = sequence.getEventPointer (e);

                    if (! event->message.isNoteOn())
                        continue;

                    ++notesPerPitch[event->message.getNoteNumber()];

                    if (event->noteOffObject == nullptr
                         || event->noteOffObject->message.getTimeStamp() <= event->message.getTimeStamp())
                        ++zeroLength;
                }
            }

            check (readOk && zeroLength == 0 && notesPerPitch[40] == 2 && notesPerPitch[45] == 2 && notesPerPitch[47] == 1,
                   "MIDI export: no note comes out zero-length  (" + juce::String (zeroLength) + " zero-length; notes "
                     + juce::String (notesPerPitch[40]) + "/" + juce::String (notesPerPitch[45]) + "/"
                     + juce::String (notesPerPitch[47]) + ", expected 2/2/1)" + (error.isEmpty() ? juce::String() : ": " + error));
        }

        //----------------------------------------------------------------------
        // 8.347：**下段と Pop out の窓に、別々の中身**（Phase 335／本人の要望）。
        //
        // どこに何を出すかは`EditorSlots`だけが決める（`MainComponent`はそのとおりに付け替えるだけ）。
        // 窓を出さずに、決まりそのものを数えます

        say ("--- two places for the editors (the bottom panel and the pop-out window)");

        {
            using EC = EditorContent;

            auto describe = [] (const EditorSlots& s)
            {
                return "dock " + EditorSlots::toString (s.dock) + (s.dockOpen ? " (open)" : " (closed)")
                     + ", window " + (s.windowOpen ? EditorSlots::toString (s.window) : juce::String ("-"));
            };

            // 本人の例1：下段にコードパッド、Pop out にピアノロール
            {
                EditorSlots s;
                s.toggle (EC::pianoRoll);
                s.popOut();
                const bool closedAfterPopOut = ! s.dockOpen && s.windowOpen && s.window == EC::pianoRoll;
                s.toggle (EC::chordPad);

                check (closedAfterPopOut && s.dockOpen && s.dock == EC::chordPad && s.windowOpen && s.window == EC::pianoRoll
                         && s.isButtonLit (EC::pianoRoll) && s.isButtonLit (EC::chordPad) && ! s.isButtonLit (EC::console),
                       "example 1: Chord Pad below and the piano roll popped out, both buttons lit  (" + describe (s) + ")");

                // フッターの Editor を押すと、窓を前面に出すだけ（動かさない）
                const bool front = s.toggle (EC::pianoRoll);
                check (front && s.dock == EC::chordPad && s.dockOpen && s.window == EC::pianoRoll,
                       "pressing Editor while the piano roll is in the window brings the window forward and moves nothing");

                // Pop Out を押すと入れ替わる
                s.popOut();
                check (s.dockOpen && s.dock == EC::pianoRoll && s.windowOpen && s.window == EC::chordPad,
                       "Pop Out with something already in the window swaps the two  (" + describe (s) + ")");
            }

            // 本人の例2：下段にピアノロール、Pop out に Console
            {
                EditorSlots s;
                s.toggle (EC::console);
                s.popOut();
                s.toggle (EC::pianoRoll);

                check (s.dockOpen && s.dock == EC::pianoRoll && s.windowOpen && s.window == EC::console,
                       "example 2: the piano roll below and the Console popped out  (" + describe (s) + ")");

                // トラックを選ぶと、エディタの側（下段）が付いていく。Console は取り上げない
                s.followSelection (EC::audioEditor);
                const bool audioBelow = s.dock == EC::audioEditor && s.window == EC::console;
                s.followSelection (EC::chordPad);

                check (audioBelow && s.dock == EC::chordPad && s.window == EC::console,
                       "selecting tracks changes the editor below and never takes the Console  (" + describe (s) + ")");

                // 下段を閉じてから窓を閉じると、窓の中身が下段へ戻る
                s.closeDock();
                s.closeWindow();
                check (! s.windowOpen && s.dockOpen && s.dock == EC::console,
                       "closing the window when the bottom panel is closed brings its content back below  (" + describe (s) + ")");
            }

            // 下段に別のものがあるときに窓を閉じると、窓だけ閉じる
            {
                EditorSlots s;
                s.toggle (EC::pianoRoll);
                s.popOut();
                s.toggle (EC::console);
                s.closeWindow();

                check (! s.windowOpen && s.dockOpen && s.dock == EC::console,
                       "closing the window while something else is below just closes the window  (" + describe (s) + ")");
            }

            // 窓に出ているエディタは、トラックを選ぶと窓の中で付いていく
            {
                EditorSlots s;
                s.toggle (EC::pianoRoll);
                s.popOut();
                s.toggle (EC::console);
                s.followSelection (EC::audioEditor);

                check (s.window == EC::audioEditor && s.dock == EC::console && s.lastEditor == EC::audioEditor,
                       "the editor in the window follows the selected track there  (" + describe (s) + ")");
            }

            // 保存したプロジェクトを開いたとき：下段が同じものを持っていれば、下段から外す
            {
                EditorSlots s;
                s.toggle (EC::console);
                s.openWindowWith (EditorSlots::fromString (EditorSlots::toString (EC::console)));

                check (s.windowOpen && s.window == EC::console && s.dock == EC::none && ! s.dockOpen,
                       "reopening a project puts the saved content back in the window, never in both places");

                bool namesRoundTrip = EditorSlots::fromString ("") == EC::none && EditorSlots::fromString ("bogus") == EC::none;

                for (auto c : { EC::pianoRoll, EC::audioEditor, EC::chordPad, EC::console })
                    namesRoundTrip = namesRoundTrip && EditorSlots::fromString (EditorSlots::toString (c)) == c;

                check (namesRoundTrip, "the saved names read back (and an unknown name reads as nothing)");
            }

            // **何をどの順で押しても、同じ中身が2か所に出ない**（部品は1つずつしか無い）。
            // 窓は空にならず、下段が開いているなら中身を持っている
            {
                EditorSlots s;
                std::mt19937 random (12345);
                const EC contents[] { EC::pianoRoll, EC::audioEditor, EC::chordPad, EC::console };
                juce::String broken;

                for (int step = 0; step < 4000 && broken.isEmpty(); ++step)
                {
                    const auto c = contents[random() % 4];

                    switch (random() % 7)
                    {
                        case 0: s.toggle (c); break;
                        case 1: s.show (c); break;
                        case 2: s.popOut(); break;
                        case 3: s.closeWindow(); break;
                        case 4: s.closeDock(); break;
                        case 5: s.followSelection (c); break;
                        case 6: s.openWindowWith (c); break;
                    }

                    if (s.windowOpen && s.window == EC::none)
                        broken = "an empty window";
                    else if (s.windowOpen && s.window == s.dock)
                        broken = "the same content in both places";
                    else if (s.dockOpen && s.dock == EC::none)
                        broken = "an open bottom panel with nothing in it";
                    else if (s.lastEditor != EC::pianoRoll && s.lastEditor != EC::audioEditor)
                        broken = "the Editor button pointing at something else";

                    if (broken.isNotEmpty())
                        broken << " at step " << step << " (" << describe (s) << ")";
                }

                check (broken.isEmpty(), "4000 random presses never show one content twice, an empty window or an empty open panel"
                                           + (broken.isEmpty() ? juce::String() : ": " + broken));
            }
        }

        //----------------------------------------------------------------------
        // 8.311：**前に開いていた窓**（Phase 304／本人の要望）。
        //
        // 窓そのものは道具からは出せませんが、**覚えている中身**は数えられます
        // ——ここが落ちると「開き直しても出てこない」になります。

        say ("--- the windows that were open");

        {
            const auto projectFile = root.getChildFile ("Windows").getChildFile ("Windows" + extension);

            ProjectModel project;
            project.createNewProject();
            project.setName ("Windows", nullptr);

            auto midi = project.addTrack ("Keys", TrackType::Midi);

            // インサートを2つ（1つめのGUIだけ開いていた、という形にする）
            juce::PluginDescription description;
            description.name = "Manta EQ";
            description.pluginFormatName = "Manta";
            description.fileOrIdentifier = "manta:eq";

            midi.addInsert (description, nullptr);
            midi.addInsert (description, nullptr);

            check (midi.getNumInserts() == 2, "the track has two inserts");

            // **エンジンの代わりに、印だけ立てる**（窓は道具からは開けません）
            midi.state.setProperty (IDs::instrumentEditorOpen, true, nullptr);
            midi.getInsert (0).state.setProperty (IDs::insertEditorOpen, true, nullptr);
            project.getState().setProperty (IDs::editorPoppedOut, true, nullptr);

            projectFile.getParentDirectory().createDirectory();
            check (project.saveToFile (projectFile), "the project saves");

            ProjectModel reopened;
            check (reopened.loadFromFile (projectFile), "...and opens again");

            auto reopenedTrack = reopened.getTrack (0);

            check ((bool) reopenedTrack.state.getProperty (IDs::instrumentEditorOpen, false),
                    "the instrument window is remembered");

            check ((bool) reopenedTrack.getInsert (0).state.getProperty (IDs::insertEditorOpen, false),
                    "the first insert's window is remembered");

            // **開いていなかったものに印が付いていないこと。** 付いていると、
            // 開き直すたびに**触ってもいない窓が出ます**
            check (! (bool) reopenedTrack.getInsert (1).state.getProperty (IDs::insertEditorOpen, false),
                    "...and the second insert's is not");

            check ((bool) reopened.getState().getProperty (IDs::editorPoppedOut, false),
                    "the popped out editor is remembered");

            // **閉じたら印も消えること**（消さないと、次の保存まで残り続けます）
            reopenedTrack.state.removeProperty (IDs::instrumentEditorOpen, nullptr);

            check (! (bool) reopenedTrack.state.getProperty (IDs::instrumentEditorOpen, false),
                    "closing it takes the mark away again");
        }

        //======================================================================
        // 8.319：**書き出したファイルの中身**（Phase 312／FLACを足したとき）
        //
        // 置き場所と同じく、**間違えてもその場では何も起きません**——
        // ファイルはでき、エラーも出ません。気づくのは**別のソフトで開いたとき**です。
        //
        // 書いて、**読み戻して、1サンプルずつ見比べます**。
        // 形式の部品は`ExportOptions::createWriter()`が作ります（書き出しと同じ道）
        say ("--- what an exported file contains");
        {
            auto roundTrip = [] (ExportOptions::Format format, int requestedBits,
                                 int& bitsOut, int& channelsOut, double& rateOut,
                                 juce::int64& lengthOut, float& worstErrorOut,
                                 juce::String& formatNameOut, juce::String& errorOut)
            {
                ExportOptions options;
                options.format = format;
                options.bitsPerSample = requestedBits;

                auto file = juce::File::createTempFile (options.getFileExtension());

                constexpr int numSamples = 12000;   // 48kHzで0.25秒
                juce::AudioBuffer<float> written (2, numSamples);

                // **左右を違う音にする**——同じだと、左右を取り違えても通ってしまいます
                for (int i = 0; i < numSamples; ++i)
                {
                    const float phase = juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 48000.0f;
                    written.setSample (0, i, 0.5f * std::sin (phase));
                    written.setSample (1, i, 0.25f * std::sin (phase * 1.5f));
                }

                {
                    auto writer = options.createWriter (file, 48000.0, 2, errorOut);

                    if (writer == nullptr)
                        return false;

                    writer->writeFromAudioSampleBuffer (written, 0, numSamples);
                }   // **ここで閉じる**（閉じるまで最後のブロックが書かれません）

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();

                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

                if (reader == nullptr)
                {
                    errorOut = "could not read it back";
                    file.deleteFile();
                    return false;
                }

                formatNameOut = reader->getFormatName();
                bitsOut = (int) reader->bitsPerSample;
                channelsOut = (int) reader->numChannels;
                rateOut = reader->sampleRate;
                lengthOut = reader->lengthInSamples;

                juce::AudioBuffer<float> read (2, numSamples);
                reader->read (&read, 0, numSamples, 0, true, true);

                worstErrorOut = 0.0f;

                for (int channel = 0; channel < 2; ++channel)
                    for (int i = 0; i < numSamples; ++i)
                        worstErrorOut = juce::jmax (worstErrorOut,
                                                    std::abs (read.getSample (channel, i) - written.getSample (channel, i)));

                reader.reset();
                file.deleteFile();
                return true;
            };

            int bits = 0, channels = 0;
            double rate = 0.0;
            juce::int64 length = 0;
            float worst = 1.0f;
            juce::String name, error;

            // **32bitを選んだまま**FLACにする——前回WAVの32bit floatで出した人は、
            // この状態で書き出しを押します（FLACに32bit floatはありません）
            const bool made = roundTrip (ExportOptions::Format::flac, 32,
                                         bits, channels, rate, length, worst, name, error);

            check (made, "a FLAC file is written even when 32 bit was left selected"
                           + (error.isNotEmpty() ? juce::String ("  (") + error + ")" : juce::String()));

            if (made)
            {
                check (name == "FLAC file", "...and it reads back as FLAC  (" + name + ")");
                check (bits == 24, "...at 24 bit  (" + juce::String (bits) + ")");
                check (channels == 2 && rate == 48000.0 && length == 12000,
                       "...with the channels, rate and length it was given");

                // 24bitの1段は2^-23。**丸めの分だけ**ずれてよい（圧縮そのものは劣化しません）
                check (worst <= 2.0f / 8388608.0f,
                       "...and every sample comes back as written  (worst "
                           + juce::String (worst * 8388608.0f, 2) + " steps of 24 bit)");
            }

            // **WAVも同じ道を通るようになったので、壊していないことを見ます**（外へ出した関数）
            const bool madeWav = roundTrip (ExportOptions::Format::wav, 32,
                                            bits, channels, rate, length, worst, name, error);

            check (madeWav && bits == 32 && worst == 0.0f,
                   "a 32 bit float WAV still comes back bit for bit");

            ExportOptions flacOptions;
            flacOptions.format = ExportOptions::Format::flac;
            check (flacOptions.getFileExtension() == ".flac", "a FLAC export is named .flac");
        }

        //======================================================================
        // 8.320：**起動時に知らせる記録はどれか**（Phase 312）
        //
        // 落ちたときの記録は3種類あります（本体・サンドボックスの子・`--crash-selftest`）。
        // 知らせるのは**本体のものだけ**です——子が落ちても本体は落ちていないし、
        // 試しに落としたものを「前回落ちました」と言うのは嘘になります。
        //
        // **本人の置き場所には書きません。** 一時フォルダへ並べて数えます
        say ("--- which crash reports are announced at startup");
        {
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("PersonalDAW-crash-selftest");
            folder.deleteRecursively();
            folder.createDirectory();

            auto touch = [&folder] (const juce::String& name)
            {
                folder.getChildFile (name).replaceWithText ("x");
            };

            touch ("crash-20260101-120000.log");
            touch ("crash-20260102-120000.log");            // 本体でいちばん新しい
            touch ("sandbox-crash-20260105-120000.log");    // もっと新しいが、子のもの
            touch ("selftest-crash-20260106-120000.log");   // もっと新しいが、試験のもの
            touch ("crash-20260107-120000.txt");            // 拡張子が違う

            check (CrashLog::isApplicationReport (folder.getChildFile ("crash-20260101-120000.log")),
                   "a report written by the application counts");
            check (! CrashLog::isApplicationReport (folder.getChildFile ("sandbox-crash-20260105-120000.log")),
                   "...one written by a sandboxed plugin does not (the application did not crash)");
            check (! CrashLog::isApplicationReport (folder.getChildFile ("selftest-crash-20260106-120000.log")),
                   "...nor one written by --crash-selftest");

            const auto newest = CrashLog::findNewestReport (folder);

            check (newest.getFileName() == "crash-20260102-120000.log",
                   "the newest one announced is the application's own  (" + newest.getFileName() + ")");

            check (CrashLog::findNewestReport (folder.getChildFile ("nothing-here")) == juce::File(),
                   "no folder means nothing to announce");

            folder.deleteRecursively();
        }

        //======================================================================
        // 8.322：**メッセージボックスの番号の戻し方**（Phase 312）
        //
        // `AppMessageBox`は`showScopedAsync`（「(押した番号＋1) % ボタンの数」）で出し、
        // **押した順へ戻してから**コールバックへ渡します。戻し方を間違えると、
        // **31箇所の選択が黙って入れ替わります**（「復元する」を押したのに破棄される）。
        //
        // 本物の箱で押して確かめる道具は`--msgbox-answer-test`（窓を出す）。
        // ここは**表だけ**を、どの機械でも数えます
        say ("--- message box buttons come back in the order they were pressed");
        {
            auto alertWindowResult = [] (int pressed, int numButtons) { return (pressed + 1) % numButtons; };

            bool allRight = true;

            for (int numButtons = 1; numButtons <= 4; ++numButtons)
                for (int pressed = 0; pressed < numButtons; ++pressed)
                    if (AppMessageBox::toPressedIndex (alertWindowResult (pressed, numButtons), numButtons) != pressed)
                        allRight = false;

            check (allRight, "every button of a 1 to 4 button box comes back as the one pressed");
            check (AppMessageBox::toPressedIndex (1, 2) == 0 && AppMessageBox::toPressedIndex (0, 2) == 1,
                   "...in a two button box, the first is 0 and the second is 1 (as before)");
        }

        //----------------------------------------------------------------------
        // 8.360：**パンチイン／アウト**（Phase 349／本人の要望「録音のパンチイン／アウトって実装できるかな？」）。
        // 本人と決めたこと：ループとは別の専用の範囲（ルーラーの赤い帯）／範囲の中の古い音とノートは差し替える／オーディオと MIDI の両方。
        // 録音は今までどおり回し続け、止めたあとで**録れた範囲とパンチ範囲の重なりだけ**を残す（`PunchRecording`）。
        // ここでは画面を通さず、**画面が呼ぶのと同じ関数**（`replaceAudio()`・`replaceMidi()`）を数える
        say ("--- punch in / out recording (8.360)");
        {
            // 範囲：拍で持つ。向きは揃える。長さ0は無視。テンポを変えても小節の位置のまま。Undo で戻る
            {
                ProjectModel project;
                project.createNewProject();
                project.setTempo (120.0, nullptr);

                project.setPunchRange (10.0, 4.0, nullptr);   // 逆向き
                const bool swapped = std::abs (project.getPunchStartTime() - 4.0) < 1e-6 && std::abs (project.getPunchEndTime() - 10.0) < 1e-6
                                       && std::abs (project.getPunchStartBeats() - 8.0) < 1e-6;

                project.setPunchRange (6.0, 6.0, nullptr);    // 長さ0 → そのまま
                const bool zeroIgnored = std::abs (project.getPunchStartTime() - 4.0) < 1e-6;

                project.setTempo (60.0, nullptr);             // 拍で持つので、時刻は倍になる
                const bool followsTempo = std::abs (project.getPunchStartTime() - 8.0) < 1e-6 && std::abs (project.getPunchEndTime() - 20.0) < 1e-6;

                project.beginAction ("punch");
                project.setPunchEnabled (true, &project.getUndoManager());
                project.getUndoManager().undo();

                check (swapped && zeroIgnored && followsTempo && ! project.isPunchEnabled() && project.hasPunchRange(),
                       "the punch range is kept in beats (4-10 s at 120 BPM is beats 8-20; at 60 BPM it becomes 8-20 s), a backwards range "
                       "is turned round, a zero-length one is ignored, and turning punch on can be undone");

                check (project.getState().hasProperty ("punchStartBeats") && project.getState().hasProperty ("punchEndBeats"),
                       "the punch range is saved with the project");
            }

            // 重なり：録れた範囲とパンチ範囲
            {
                const auto a = PunchRecording::effectiveRange (2.0, 14.0, 5.0, 10.0);
                const auto b = PunchRecording::effectiveRange (7.0, 14.0, 5.0, 10.0);
                const auto c = PunchRecording::effectiveRange (11.0, 14.0, 5.0, 10.0);

                check (a == juce::Range<double> (5.0, 10.0) && b == juce::Range<double> (7.0, 10.0) && c.isEmpty(),
                       "what is kept is where the recording and the punch range overlap: recorded 2-14 s keeps 5-10 s; started at 7 s keeps 7-10 s; "
                       "started after the range keeps nothing");
            }

            // オーディオ：古いクリップ 0〜20 秒の上に、2〜14 秒を録ってパンチ 5〜10 秒
            {
                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Vocal", TrackType::Audio);
                track.addAudioClip ("C:/old.wav", 0.0, 20.0, nullptr);
                track.addAudioClip ("C:/inside.wav", 6.0, 2.0, nullptr);   // 範囲の中にすっぽり
                track.addAudioClip ("C:/end.wav", 8.0, 4.0, nullptr);      // 範囲の終わりをまたぐ

                const auto keep = PunchRecording::effectiveRange (2.0, 14.0, 5.0, 10.0);
                project.beginAction ("punch audio");
                const int replaced = PunchRecording::replaceAudio (track, "C:/take.wav", 2.0, 12.0, keep, &project.getUndoManager());

                struct Seen { juce::String file; double start, length, offset; };
                std::vector<Seen> clips;

                for (int i = 0; i < track.getNumClips(); ++i)
                {
                    auto clip = track.getClip (i);
                    clips.push_back ({ clip.getSourceFilePath(), clip.getStartTime(), clip.getLength(), clip.getOffset() });
                }

                std::sort (clips.begin(), clips.end(), [] (const Seen& x, const Seen& y)
                           { return x.start != y.start ? x.start < y.start : x.file < y.file; });

                auto has = [&] (const juce::String& file, double start, double length, double offset)
                {
                    for (const auto& c : clips)
                        if (c.file == file && std::abs (c.start - start) < 1e-6 && std::abs (c.length - length) < 1e-6
                            && std::abs (c.offset - offset) < 1e-6)
                            return true;

                    return false;
                };

                juce::String listing;

                for (const auto& c : clips)
                    listing << c.file.fromLastOccurrenceOf ("/", false, false) << " " << juce::String (c.start, 2) << "+"
                            << juce::String (c.length, 2) << "@" << juce::String (c.offset, 2) << "  ";

                check (clips.size() == 4 && has ("C:/old.wav", 0.0, 5.0, 0.0) && has ("C:/old.wav", 10.0, 10.0, 10.0)
                         && has ("C:/end.wav", 10.0, 2.0, 2.0) && has ("C:/take.wav", 5.0, 5.0, 3.0) && ! has ("C:/inside.wav", 6.0, 2.0, 0.0),
                       "punching 5-10 s into a 2-14 s recording leaves the old clip before and after (0-5, 10-20), trims the one crossing the end "
                       "to 10-12, removes the one inside, and places the take at 5-10 s showing its 3-8 s  (" + listing + ")");

                check (replaced == 3,
                       "...three old pieces inside the range were replaced (" + juce::String (replaced) + ")");

                project.getUndoManager().undo();
                check (track.getNumClips() == 3,
                       "one Undo brings the old clips back (" + juce::String (track.getNumClips()) + " clips)");
            }

            // 範囲の途中から録り始めたら、その手前の古い音は残す
            {
                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Vocal", TrackType::Audio);
                track.addAudioClip ("C:/old.wav", 0.0, 20.0, nullptr);

                const auto keep = PunchRecording::effectiveRange (7.0, 14.0, 5.0, 10.0);
                PunchRecording::replaceAudio (track, "C:/take.wav", 7.0, 7.0, keep, nullptr);

                double oldUntil = 0.0;

                for (int i = 0; i < track.getNumClips(); ++i)
                    if (track.getClip (i).getSourceFilePath() == "C:/old.wav" && track.getClip (i).getStartTime() < 1.0)
                        oldUntil = track.getClip (i).getStartTime() + track.getClip (i).getLength();

                check (std::abs (oldUntil - 7.0) < 1e-6,
                       "recording that starts at 7 s inside a 5-10 s range keeps the old audio up to 7 s (" + juce::String (oldUntil, 2) + " s)");
            }

            // MIDI：古いノートの上に、3〜12 秒を録ってパンチ 5〜10 秒
            {
                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Keys", TrackType::Midi);

                track.addNote (60, 100, 4.0, 2.0, nullptr);    // 手前から範囲へ → 5 秒で切る
                track.addNote (62, 100, 6.0, 1.0, nullptr);    // 範囲の中 → 消す
                track.addNote (64, 100, 9.0, 0.5, nullptr);    // 範囲の中 → 消す
                track.addNote (65, 100, 11.0, 1.0, nullptr);   // 範囲の後ろ → 残す
                track.addCCEvent (1, 20, 8.0, nullptr);        // 録り直した番号（モジュレーション）→ 消す
                track.addCCEvent (7, 90, 8.0, nullptr);        // 録っていない番号（ボリューム）→ 残す

                const double sr = 48000.0;
                auto at = [sr] (double seconds) { return (juce::int64) std::llround (seconds * sr); };

                RecordedMidiTake take;
                take.trackId = track.getId();
                take.events = {
                    { 0xb0, 1, 30, at (4.0) },            // 範囲の手前の CC → 入れない
                    { 0x90, 67, 100, at (4.5) },          // 範囲の手前で押した → 入れない
                    { 0x80, 67, 0, at (5.5) },
                    { 0x90, 69, 100, at (6.5) },          // 範囲の中 → 入れる
                    { 0x80, 69, 0, at (7.0) },
                    { 0xb0, 1, 64, at (7.0) },            // 範囲の中の CC → 入れる
                    { 0x90, 71, 100, at (9.5) },          // 範囲の中で押して、終わりをまたぐ → 10 秒で切る
                    { 0x80, 71, 0, at (11.0) },
                    { 0xb0, 1, 90, at (11.0) },           // 範囲の後ろの CC → 入れない
                };

                const auto keep = PunchRecording::effectiveRange (3.0, 12.0, 5.0, 10.0);
                const auto result = PunchRecording::replaceMidi (project, take, sr, 12.0, keep, nullptr);

                juce::String listing;
                std::map<int, std::pair<double, double>> notes;

                for (int i = 0; i < track.getNumNotes(); ++i)
                {
                    const auto note = track.getNote (i);
                    notes[note.getPitch()] = { note.getStartTime(), note.getLength() };
                    listing << note.getPitch() << " " << juce::String (note.getStartTime(), 2) << "+" << juce::String (note.getLength(), 2) << "  ";
                }

                auto noteIs = [&] (int pitch, double start, double length)
                {
                    const auto it = notes.find (pitch);
                    return it != notes.end() && std::abs (it->second.first - start) < 1e-3 && std::abs (it->second.second - length) < 1e-3;
                };

                check (notes.size() == 4 && noteIs (60, 4.0, 1.0) && noteIs (65, 11.0, 1.0) && noteIs (69, 6.5, 0.5) && noteIs (71, 9.5, 0.5)
                         && notes.count (62) == 0 && notes.count (64) == 0 && notes.count (67) == 0,
                       "punching MIDI 5-10 s: the note running into the range is cut at 5 s, the two inside are replaced, the one after stays; "
                       "only what was pressed inside the range goes in, cut at 10 s  (" + listing + ")");

                int mod = 0, volume = 0, modValue = -1;

                for (int i = 0; i < track.getNumCCEvents(); ++i)
                {
                    const auto cc = track.getCCEvent (i);

                    if (cc.getControllerNumber() == 1) { ++mod; modValue = cc.getValue(); }
                    if (cc.getControllerNumber() == 7) ++volume;
                }

                check (mod == 1 && modValue == 64 && volume == 1 && result.replacedNotes == 2,
                       "...the modulation recorded inside the range replaces the old one there, the volume that was not played stays "
                       "(mod " + juce::String (mod) + " = " + juce::String (modValue) + ", volume " + juce::String (volume)
                       + "), 2 old notes replaced (" + juce::String (result.replacedNotes) + ")");

                // 範囲の後ろから録り始めたら、何も入れない（空の範囲で全部入ってしまわない）
                const int before = track.getNumNotes();
                const auto none = PunchRecording::replaceMidi (project, take, sr, 14.0, PunchRecording::effectiveRange (11.0, 14.0, 5.0, 10.0), nullptr);
                check (none.summary.numNotes == 0 && track.getNumNotes() == before,
                       "recording that starts after the range puts nothing in (" + juce::String (track.getNumNotes() - before) + " notes added)");
            }
        }

        //----------------------------------------------------------------------
        // 8.361：**センドトラックは色を固定（グレー）、並びは一番下**（Phase 350／本人の要望）。
        // > 「センドトラックのトラック色は固定に（他トラックと分かりやすくするため）。グレーで試す」
        // > 「センドトラックはMIDIトラックやオーディオトラック、フォルダトラックの下に並べて配置。
        // >  Consoleでは、マスターの左、その他トラックの右側に」（Console はトラックの並びのまま並ぶ）
        say ("--- send tracks: fixed grey, always at the bottom (8.361)");
        {
            ProjectModel project;
            project.createNewProject();

            auto names = [&]
            {
                juce::StringArray list;

                for (int i = 0; i < project.getNumTracks(); ++i)
                    list.add (project.getTrack (i).getName());

                return list.joinIntoString (",");
            };

            auto a = project.addTrack ("A", TrackType::Midi);
            auto s1 = project.addTrack ("S1", TrackType::Send);
            project.addTrack ("B", TrackType::Audio, a.getId());   // A の真下へ（センドより上）
            project.addTrack ("S2", TrackType::Send, a.getId());   // A を選んでいても、センドは一番下
            project.addTrack ("C", TrackType::Midi);               // 末尾へ足しても、センドの手前

            const auto base = names();
            check (base.endsWith ("A,B,C,S1,S2"),
                   "new tracks go above the sends and new sends go to the bottom (" + base + ")");

            s1.state.setProperty (IDs::trackColor, "FFFF0000", nullptr);   // 保存されている色が何でも
            check (s1.getColourString() == Track::sendTrackColour && ! s1.hasEditableColour() && a.hasEditableColour(),
                   "a send track's colour is the fixed grey whatever is stored, and it cannot be changed (" + s1.getColourString() + ")");

            // 並べ替え：センドを一番上へ引いても、センドの中にとどまる。ほかを一番下へ引いても、センドの手前まで
            const int numTracks = project.getNumTracks();
            project.moveTrack (numTracks - 1, 0);            // S2 を一番上へ
            const auto afterSendUp = names();
            project.moveTrack (numTracks - 5, numTracks - 1); // A を一番下へ
            const auto afterTrackDown = names();

            check (afterSendUp.endsWith ("A,B,C,S2,S1") && afterTrackDown.endsWith ("B,C,A,S2,S1"),
                   "dragging a send to the top keeps it among the sends (" + afterSendUp + "); dragging a track to the bottom stops above the sends ("
                     + afterTrackDown + ")");

            auto folder = project.addTrack ("F", TrackType::Folder);
            check (! project.canMoveTrackIntoFolder (s1, folder.getId()),
                   "a send cannot be put into a folder");

            // 前の版のプロジェクト（センドが途中・フォルダの中）を開くと、一番下へ揃う
            auto tracksNode = project.getState().getChildWithName (IDs::TRACKS);
            tracksNode.moveChild (tracksNode.indexOf (s1.state), 0, nullptr);
            Track (s1.state).setParentFolderId (folder.getId(), nullptr);

            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("send_order_selftest.mdaw");
            project.writeCopyToFile (file);

            ProjectModel reopened;
            reopened.loadFromFile (file);
            file.deleteFile();

            juce::StringArray reopenedNames;

            for (int i = 0; i < reopened.getNumTracks(); ++i)
                reopenedNames.add (reopened.getTrack (i).getName());

            const auto reopenedList = reopenedNames.joinIntoString (",");
            const auto reopenedS1 = reopened.getTrack (reopened.getNumTracks() - 2);   // ファイルでは S1 が先頭＝センドの中でも先

            check (reopenedList.endsWith ("F,S1,S2") && reopenedS1.getName() == "S1" && reopenedS1.getParentFolderId().isEmpty(),
                   "opening a project made before this version moves its sends to the bottom and out of folders (" + reopenedList + ")");
        }

        //----------------------------------------------------------------------
        // 8.365：**ノートのピッチベンド**（Phase 353／本人の要望）。
        // > 「MIDIノートにベンド機能を追加できるかな？」「ノートの開始からもフォール／ライズできると嬉しい」
        // 決めたこと：単音向け（ノートの曲線 → 普通のピッチベンド）／頭と終わりのフォール・ライズ、次のノートへのスライド、ビブラート
        say ("--- pitch bend on notes (8.365)");
        {
            // 曲線の形
            {
                NoteBend fall;
                fall.endSemitones = -5.0;   // 終わりで5半音下がる（画像の「A fall」）
                fall.endPosition = 0.6;

                NoteBend scoop;
                scoop.startSemitones = -2.0; // 頭で2半音下から上がってくる
                scoop.startPosition = 0.3;

                NoteBend vibrato;
                vibrato.vibratoDepth = 0.5;
                vibrato.vibratoRate = 6.0;

                double vibratoPeak = 0.0;

                for (int i = 0; i <= 1000; ++i)
                    vibratoPeak = juce::jmax (vibratoPeak, std::abs (vibrato.semitonesAt (i / 1000.0, i / 1000.0)));

                check (std::abs (fall.semitonesAt (0.0, 0.0)) < 1e-9 && std::abs (fall.semitonesAt (0.6, 0.6)) < 1e-9
                         && std::abs (fall.semitonesAt (0.8, 0.8) + 2.5) < 1e-6 && std::abs (fall.semitonesAt (1.0, 1.0) + 5.0) < 1e-9,
                       "a fall stays on the note until 60 %, then bends smoothly down to -5 at the end (-2.5 half way)");

                check (std::abs (scoop.semitonesAt (0.0, 0.0) + 2.0) < 1e-9 && std::abs (scoop.semitonesAt (0.3, 0.3)) < 1e-9
                         && std::abs (scoop.semitonesAt (1.0, 1.0)) < 1e-9,
                       "a bend from the start begins 2 below and is back on the note by 30 % (\"from the start of the note\")");

                check (std::abs (vibrato.semitonesAt (0.2, 0.2)) < 1e-9 && vibratoPeak > 0.45 && vibratoPeak <= 0.5 + 1e-9,
                       "vibrato is still for the first quarter, then swings up to its depth (peak " + juce::String (vibratoPeak, 3) + " of 0.5)");
            }

            // 保存：ノートに書いて読み戻す。空にすると消える（ベンドの無いノートは今までどおりの保存）
            {
                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Lead", TrackType::Midi);
                auto note = track.addNote (64, 100, 1.0, 1.0, nullptr);

                NoteBend bend;
                bend.startSemitones = 1.0;
                bend.endSemitones = -3.5;
                bend.endPosition = 0.7;
                bend.vibratoDepth = 0.3;
                note.setBend (bend, nullptr);

                const auto back = note.getBend();
                const bool same = std::abs (back.startSemitones - 1.0) < 1e-9 && std::abs (back.endSemitones + 3.5) < 1e-9
                                    && std::abs (back.endPosition - 0.7) < 1e-9 && std::abs (back.vibratoDepth - 0.3) < 1e-9;

                note.setBend ({}, nullptr);
                const bool cleared = note.getBend().isEmpty() && ! note.state.hasProperty (IDs::noteBendEnd)
                                       && ! note.state.hasProperty (IDs::noteVibratoDepth);

                check (same && cleared && std::abs (track.getPitchBendRange() - 2.0) < 1e-9,
                       "a note keeps its bend (start, end, where, vibrato) and drops it entirely when cleared; a track's bend range is 2 unless set");
            }

            // ピッチベンドの値の並び（単音向け）
            {
                const double range = 12.0;
                const int bottom = NoteBendRender::toPitchWheel (-5.0, range);

                NoteBend fall;
                fall.endSemitones = -5.0;

                NoteBend scoop;
                scoop.startSemitones = -2.0;

                // A [0,1) フォール、B [1,2) 頭から上がる、C [3,4) ベンド無し
                const auto events = NoteBendRender::render ({ { 0.0, 1.0, fall }, { 1.0, 2.0, scoop } }, { 0.0, 1.0, 3.0 }, range);

                auto valueAt = [&] (double seconds)
                {
                    int value = 8192;

                    for (const auto& [t, v] : events)
                        if (t <= seconds + 1e-9)
                            value = v;

                    return value;
                };

                const bool startsCentred = ! events.empty() && events.front().first == 0.0 && events.front().second == 8192;
                bool reachesBottom = false;   // A の終わり（1 秒）に一番下の値。同じ時刻にそのあと戻し・B の頭が続く

                for (const auto& [t, v] : events)
                    if (std::abs (t - 1.0) < 1e-9 && v == bottom)
                        reachesBottom = true;
                const bool nextStartsLow = valueAt (1.0) == NoteBendRender::toPitchWheel (-2.0, range);   // 戻しの直後に B の頭の値
                const bool backToCentre = valueAt (2.5) == 8192 && valueAt (3.5) == 8192;

                check (startsCentred && reachesBottom && nextStartsLow && backToCentre,
                       "the bends become pitch-wheel values: A falls to " + juce::String (bottom) + " by its end, B starts 2 below at 1 s "
                       "(after the reset), and the wheel is back at the centre after B (" + juce::String ((int) events.size()) + " events)");

                // 単音向け：A が長くても、B が鳴ったら A の曲線は止まる
                NoteBend earlyFall = fall;
                earlyFall.endPosition = 0.3;   // 0.6 秒から曲がり始める（B の頭 1 秒より前）
                const auto overlapped = NoteBendRender::render ({ { 0.0, 2.0, earlyFall } }, { 0.0, 1.0 }, range);
                bool stopsAtNext = true;

                for (const auto& [t, v] : overlapped)
                    if (t > 1.0 + 1e-9)
                        stopsAtNext = false;

                check (stopsAtNext && ! overlapped.empty() && overlapped.back().first == 1.0 && overlapped.back().second == 8192,
                       "one voice: when the next note starts, the bent note's curve stops and the wheel goes back to the centre there");

                // 幅を超えたら端で止まる
                check (NoteBendRender::toPitchWheel (-5.0, 2.0) == 0 && NoteBendRender::toPitchWheel (5.0, 2.0) == 16383
                         && NoteBendRender::toPitchWheel (0.0, 2.0) == 8192,
                       "a bend beyond the instrument's range stops at the end of the wheel (-5 with a range of 2 is 0)");
            }

            // 再生：本物の`MidiPlayerProcessor`から、フォールしていくピッチベンドが出る
            {
                ProjectModel project;
                project.createNewProject();
                auto track = project.addTrack ("Lead", TrackType::Midi);
                track.setPitchBendRange (12.0, nullptr);

                auto note = track.addNote (60, 100, 0.5, 1.0, nullptr);   // [0.5, 1.5)
                NoteBend fall;
                fall.endSemitones = -5.0;
                note.setBend (fall, nullptr);
                track.addNote (62, 100, 2.0, 0.5, nullptr);                // 次のノート（ここで中央へ戻る）

                constexpr double sampleRate = 48000.0;
                constexpr int blockSize = 512;
                auto at = [] (double seconds) { return (juce::int64) (seconds * sampleRate); };

                Transport transport;
                transport.prepare (sampleRate);

                MidiPlayerProcessor player { project, transport, track.getId() };
                player.prepareToPlay (sampleRate, blockSize);
                player.prepareNotesForPlayback();
                transport.start (at (3.0));

                juce::AudioBuffer<float> buffer (2, blockSize);
                juce::MidiBuffer midi;
                int lowest = 8192, wheelAtNextNote = -1, lastWheel = 8192, count = 0;

                while (transport.isPlaying() && transport.getPositionSamples() < at (3.0))
                {
                    midi.clear();
                    player.processBlock (buffer, midi);

                    for (const auto metadata : midi)
                    {
                        const auto message = metadata.getMessage();

                        if (message.isPitchWheel())
                        {
                            lastWheel = message.getPitchWheelValue();
                            lowest = juce::jmin (lowest, lastWheel);
                            ++count;
                        }
                        else if (message.isNoteOn() && message.getNoteNumber() == 62)
                        {
                            wheelAtNextNote = lastWheel;
                        }
                    }

                    transport.advance (blockSize);
                }

                // MIDI 書き出しにも、同じベンドが入る（再生と同じ`Track::getNoteBendEvents()`）
                {
                    const auto midiFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("note_bend_selftest.mid");
                    const auto error = MidiFileExporter::exportToFile (project, midiFile);

                    juce::MidiFile readBack;
                    int wheels = 0, lowestWritten = 8192;

                    if (juce::FileInputStream in (midiFile); in.openedOk() && readBack.readFrom (in))
                        for (int t = 0; t < readBack.getNumTracks(); ++t)
                            for (const auto* event : *readBack.getTrack (t))
                                if (event->message.isPitchWheel())
                                {
                                    ++wheels;
                                    lowestWritten = juce::jmin (lowestWritten, event->message.getPitchWheelValue());
                                }

                    midiFile.deleteFile();

                    check (error.isEmpty() && wheels > 20 && std::abs (lowestWritten - NoteBendRender::toPitchWheel (-5.0, 12.0)) <= 2,
                           "exporting a MIDI file writes the same bend (" + juce::String (wheels) + " pitch-wheel events, lowest "
                             + juce::String (lowestWritten) + ")");
                }

                const int expectedLowest = NoteBendRender::toPitchWheel (-5.0, 12.0);
                check (std::abs (lowest - expectedLowest) <= 2 && wheelAtNextNote == 8192 && count > 20,
                       "playing it sends the fall as pitch-wheel messages (" + juce::String (count) + " of them, lowest "
                         + juce::String (lowest) + " for -5 in a range of 12), and the wheel is centred before the next note starts ("
                         + juce::String (wheelAtNextNote) + ")");
            }
        }

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
