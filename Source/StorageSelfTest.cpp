#include "StorageSelfTest.h"

#include "ProjectModel.h"
#include "MidiPlayerProcessor.h"   // 8.307：ずらした量が鳴る位置に出ること（Phase 300）
#include "StorageLocations.h"

#include <juce_events/juce_events.h>

#include <iostream>

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

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
