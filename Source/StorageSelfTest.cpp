#include "StorageSelfTest.h"

#include "ProjectModel.h"
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

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
