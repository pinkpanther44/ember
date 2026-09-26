#include "StorageSelfTest.h"

#include "ProjectModel.h"
#include "MidiPlayerProcessor.h"   // 8.307：ずらした量が鳴る位置に出ること（Phase 300）
#include "StorageLocations.h"
#include "ExportOptions.h"   // 8.319：書き出したファイルの中身（Phase 312）
#include "CrashLog.h"        // 8.320：起動時に知らせる記録（Phase 312）
#include "AppMessageBox.h"   // 8.322：ボタンの番号の戻し方（Phase 312）

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

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
