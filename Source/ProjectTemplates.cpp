#include "ProjectTemplates.h"
#include "ProjectModel.h"
#include "StorageLocations.h"
#include "Utf8.h"

namespace ProjectTemplates
{
    namespace
    {
        /** 組み込みの中身を組み立てる。**`createNewProject()`から始めること**——
            そこが「今の保存形式」を作る1箇所です（8.138）。 */
        void buildBuiltIn (ProjectModel& project, BuiltIn which)
        {
            project.createNewProject();

            // **番号は1から通しで振る。** `getNumTracks() + 1`で数えると、
            // MIDIとオーディオが混ざったときに番号が飛びます
            // （ArrangeViewの「トラックを足す」はその場の通し番号で構いませんが、
            //   ここは10本まとめて作るので、並びが読めることを優先しています）
            if (which == BuiltIn::midi10 || which == BuiltIn::midi10Audio10)
                for (int i = 1; i <= 10; ++i)
                    project.addTrack ("MIDI " + juce::String (i), TrackType::Midi);

            if (which == BuiltIn::midi10Audio10)
                for (int i = 1; i <= 10; ++i)
                    project.addTrack ("Audio " + juce::String (i), TrackType::Audio);

            // **作った直後は「未保存の変更なし」にする。** `createNewProject()`は
            // そうしてくれますが、その後の`addTrack()`が変更として数えられます。
            // 残したままだと、開いた瞬間にタイトルへ「*」が付き、
            // 何も触っていないのに終了時へ確認が出ます
            project.markAsNewFromTemplate();
        }

        std::vector<Entry> getBuiltIns()
        {
            return
            {
                { utf8 ("空のプロジェクト"),
                  utf8 ("トラックはありません。"),
                  {}, BuiltIn::empty },

                { utf8 ("MIDI 10トラック"),
                  utf8 ("MIDIトラックを10本作ります。"),
                  {}, BuiltIn::midi10 },

                { utf8 ("MIDI 10＋オーディオ 10トラック"),
                  utf8 ("MIDIトラック10本と、オーディオトラック10本を作ります。"),
                  {}, BuiltIn::midi10Audio10 }
            };
        }
    }

    juce::File getFolder()
    {
        return StorageLocations::getFolder (StorageLocations::Kind::templates);
    }

    std::vector<Entry> getAll()
    {
        auto entries = getBuiltIns();

        auto folder = getFolder();

        if (folder.isDirectory())
        {
            // **新旧どちらの拡張子も拾う**（`ProjectModel::getFileWildcard()`。8.2）。
            // 判定を書き写すと、片方を忘れます
            auto files = folder.findChildFiles (juce::File::findFiles, false,
                                                 ProjectModel::getFileWildcard());

            files.sort();

            for (const auto& file : files)
                entries.push_back ({ file.getFileNameWithoutExtension(),
                                      file.getLastModificationTime().formatted (utf8 ("%Y-%m-%d %H:%M に保存")),
                                      file, BuiltIn::empty });
        }

        return entries;
    }

    juce::String apply (ProjectModel& project, const Entry& entry)
    {
        if (entry.isBuiltIn())
        {
            buildBuiltIn (project, entry.builtIn);
            return {};
        }

        if (! entry.file.existsAsFile())
            return utf8 ("テンプレートが見つかりません:\n") + entry.file.getFullPathName();

        if (! project.loadFromFile (entry.file))
            return utf8 ("テンプレートを読み込めませんでした:\n") + entry.file.getFullPathName();

        // **ここが「テンプレートである」ことの全部です。**
        // 読み込んだ時点では、編集中のファイルがテンプレート本体を指しています。
        // 外さないと、Ctrl+Sでテンプレートを上書きします（D8）
        project.markAsNewFromTemplate();
        return {};
    }

    juce::String saveCurrentAsTemplate (const ProjectModel& project, const juce::String& name)
    {
        const auto trimmed = name.trim();

        if (trimmed.isEmpty())
            return utf8 ("名前を入れてください。");

        // **ファイル名に使えない文字を落とす。** 「Drums / Bass」のような名前を
        // そのまま渡すと、フォルダを掘ろうとして失敗します
        const auto legal = juce::File::createLegalFileName (trimmed);

        if (legal.isEmpty())
            return utf8 ("その名前はファイル名に使えません。");

        auto folder = StorageLocations::getFolderForWriting (StorageLocations::Kind::templates);
        auto file = folder.getChildFile (legal + ProjectModel::getFileExtension());

        // **上書きは黙って行います。** 同じ名前で保存し直すのは
        // 「更新したい」という意味なので、ここで訊くと毎回2度手間になります
        // （訊くなら呼ぶ側。`MainComponent::saveCurrentAsTemplate()`が確認しています）
        if (! project.writeCopyToFile (file))
            return utf8 ("書き込めませんでした:\n") + file.getFullPathName();

        return {};
    }

    bool remove (const Entry& entry)
    {
        if (entry.isBuiltIn())
            return false;

        return entry.file.moveToTrash() || entry.file.deleteFile();
    }
}
