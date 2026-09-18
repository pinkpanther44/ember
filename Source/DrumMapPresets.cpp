#include "DrumMapPresets.h"

#include "AppSettings.h"
#include "MidiEditModel.h"
#include "ProjectIds.h"

namespace DrumMapPresets
{
    namespace
    {
        /** ファイルへ書くときの根。**`<DRUMMAP>`そのままにしないこと**——
            プロジェクトの中のマップと同じ形にすると、
            「どちらのファイルか」がタグだけでは分からなくなります。 */
        const juce::Identifier presetTag ("DRUMMAPPRESET");

        juce::File getFileFor (const juce::String& name)
        {
            const auto safe = juce::File::createLegalFileName (name).trim();

            if (safe.isEmpty())
                return {};

            return getFolder().getChildFile (safe + ".xml");
        }
    }

    juce::File getFolder()
    {
        auto folder = AppSettings::getDataFolder().getChildFile ("DrumMaps");
        folder.createDirectory();

        return folder;
    }

    juce::StringArray getNames()
    {
        juce::StringArray names;

        for (const auto& file : getFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
            names.add (file.getFileNameWithoutExtension());

        // **並べておくこと。** フォルダの返す順はOS任せなので、
        // メニューの並びが起動ごとに変わって見えます
        names.sortNatural();

        return names;
    }

    bool save (const DrumMap& map, const juce::String& name)
    {
        const auto file = getFileFor (name);

        if (file == juce::File() || ! map.state.isValid())
            return false;

        juce::ValueTree preset (presetTag);
        preset.setProperty (IDs::drumMapName, name, nullptr);

        for (int i = 0; i < map.getNumEntries(); ++i)
        {
            auto entry = map.getEntry (i);

            juce::ValueTree saved (IDs::DRUMENTRY);
            saved.setProperty (IDs::drumMidiNote, entry.getMidiNote(), nullptr);
            saved.setProperty (IDs::drumPartName, entry.getPartName(), nullptr);
            saved.setProperty (IDs::drumMuteGroup, entry.getMuteGroup(), nullptr);

            // **ミュートは書かない**（ヘッダの表。読んだ瞬間に鳴らない行ができる）
            preset.addChild (saved, -1, nullptr);
        }

        if (auto xml = preset.createXml())
            return xml->writeTo (file);

        return false;
    }

    bool load (DrumMap& map, const juce::String& name, juce::UndoManager* undoManager)
    {
        const auto file = getFileFor (name);

        if (file == juce::File() || ! file.existsAsFile() || ! map.state.isValid())
            return false;

        auto xml = juce::XmlDocument::parse (file);

        if (xml == nullptr)
            return false;

        const auto preset = juce::ValueTree::fromXml (*xml);

        if (! preset.hasType (presetTag))
            return false;   // 別のXMLを掴んだ（拡張子だけでは決めない）

        bool changedAnything = false;

        for (int i = 0; i < preset.getNumChildren(); ++i)
        {
            const auto saved = preset.getChild (i);

            if (! saved.hasType (IDs::DRUMENTRY))
                continue;

            const int midiNote = (int) saved.getProperty (IDs::drumMidiNote, -1);

            if (! juce::isPositiveAndBelow (midiNote, 128))
                continue;   // 壊れた値でテーブルを引かない（1.17）

            const auto partName = saved.getProperty (IDs::drumPartName).toString();
            const int muteGroup = (int) saved.getProperty (IDs::drumMuteGroup, 0);

            auto entry = map.findEntry (midiNote);

            // **無ければ足す**（消しはしない。ヘッダの表）
            if (! entry.state.isValid())
            {
                map.addEntry (midiNote, partName, muteGroup, undoManager);
                changedAnything = true;
                continue;
            }

            if (entry.getPartName() != partName)
            {
                entry.setPartName (partName, undoManager);
                changedAnything = true;
            }

            if (entry.getMuteGroup() != muteGroup)
            {
                entry.setMuteGroup (muteGroup, undoManager);
                changedAnything = true;
            }
        }

        return changedAnything;
    }

    bool remove (const juce::String& name)
    {
        const auto file = getFileFor (name);

        return file != juce::File() && file.existsAsFile() && file.deleteFile();
    }
}
