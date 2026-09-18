#include "DrumMapPresetSelfTest.h"

#include "DrumMapPresets.h"
#include "ProjectModel.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace DrumMapPresetSelfTest
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

        /** **本人のプリセットと紛れない名前**にしておく（後で消しますが、
            途中で落ちても「何のファイルか」が分かるように）。 */
        const juce::String presetName { "zz-selftest-drummap" };
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--drummap-selftest"))
            return false;

        problems = 0;

        say ("drum map preset self-test (8.284)");
        say ("--- folder: " + DrumMapPresets::getFolder().getFullPathName());

        ProjectModel project;
        auto map = project.getOrCreateDefaultDrumMap();

        check (map.state.isValid() && map.getNumEntries() > 0, "the project has a default drum map");

        if (map.getNumEntries() == 0)
        {
            say ("--- " + juce::String (problems + 1) + " problem(s) ---");
            juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (1);
            return true;
        }

        //----------------------------------------------------------------------
        // ① 保存 → 名前を変える → 読み込むと戻る

        say ("--- save and load");

        auto first = map.getEntry (0);
        const int firstNote = first.getMidiNote();
        const auto originalName = first.getPartName();

        check (DrumMapPresets::save (map, presetName), "the map saves to a file");
        check (DrumMapPresets::getNames().contains (presetName), "...and the name is listed");

        first.setPartName ("changed by the self-test", nullptr);
        check (map.findEntry (firstNote).getPartName() != originalName, "the name was changed");

        DrumMapPresets::load (map, presetName, nullptr);
        check (map.findEntry (firstNote).getPartName() == originalName,
                "loading the preset brings the old name back");

        //----------------------------------------------------------------------
        // ② **消さない**。プリセットに無い行が残ること（1.9）

        say ("--- what loading must NOT do");

        const int addedNote = 120;   // 既定のGMマップには無い音
        map.addEntry (addedNote, "row added after saving", 0, nullptr);

        const int entriesBefore = map.getNumEntries();

        DrumMapPresets::load (map, presetName, nullptr);

        check (map.findEntry (addedNote).state.isValid(),
                "a row that is not in the preset survives the load");
        check (map.getNumEntries() == entriesBefore, "...and no row disappeared");

        //----------------------------------------------------------------------
        // ③ **ミュートは持ち運ばない**

        auto second = map.getEntry (1);
        second.setMuted (true, nullptr);

        DrumMapPresets::save (map, presetName);
        second.setMuted (false, nullptr);
        DrumMapPresets::load (map, presetName, nullptr);

        check (! map.getEntry (1).isMuted(), "a muted row does not travel in the preset");

        //----------------------------------------------------------------------
        // ④ 足りない行は**足す**

        say ("--- what loading must do");

        {
            // いま保存したプリセットには`addedNote`が入っています。
            // その行を消してから読み直すと、戻ってくるはず
            auto entry = map.findEntry (addedNote);
            map.state.removeChild (entry.state, nullptr);

            check (! map.findEntry (addedNote).state.isValid(), "the row was removed by hand");

            DrumMapPresets::load (map, presetName, nullptr);

            check (map.findEntry (addedNote).state.isValid(),
                    "a row that is in the preset but not in the map is added back");
        }

        //----------------------------------------------------------------------
        // ⑤ 後始末（本人の一覧に試験用の名前を残さない）

        check (DrumMapPresets::remove (presetName), "the test preset is deleted again");
        check (! DrumMapPresets::getNames().contains (presetName), "...and no longer listed");

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
