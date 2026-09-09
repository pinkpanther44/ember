#include "AppSettings.h"
#include "Branding.h"   // 8.175：保存先の名前（Phase 216）

juce::File AppSettings::getDataFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile (Branding::dataFolderName);

    folder.createDirectory();
    return folder;
}

juce::File AppSettings::getSettingsFile()
{
    return getDataFolder().getChildFile ("settings.xml");
}

juce::ValueTree AppSettings::load()
{
    if (auto xml = juce::XmlDocument::parse (getSettingsFile()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);

        if (tree.isValid())
            return tree;
    }

    // ファイルが無い／壊れている場合は空の設定として扱う。
    // ここで失敗を表に出さないのは意図的で、設定が読めないだけで
    // アプリが使えなくなるのは割に合わないため（既定値で動けばよい）。
    return juce::ValueTree ("SETTINGS");
}

void AppSettings::save (const juce::ValueTree& settings)
{
    if (auto xml = settings.createXml())
        xml->writeTo (getSettingsFile());
}

juce::String AppSettings::getString (const juce::String& key, const juce::String& defaultValue)
{
    auto settings = load();
    const juce::Identifier id (key);

    return settings.hasProperty (id) ? settings[id].toString() : defaultValue;
}

void AppSettings::setString (const juce::String& key, const juce::String& value)
{
    auto settings = load();
    settings.setProperty (juce::Identifier (key), value, nullptr);
    save (settings);
}

int AppSettings::getInt (const juce::String& key, int defaultValue)
{
    auto settings = load();
    const juce::Identifier id (key);

    return settings.hasProperty (id) ? (int) settings[id] : defaultValue;
}

void AppSettings::setInt (const juce::String& key, int value)
{
    auto settings = load();
    settings.setProperty (juce::Identifier (key), value, nullptr);
    save (settings);
}

double AppSettings::getDouble (const juce::String& key, double defaultValue)
{
    auto settings = load();
    const juce::Identifier id (key);

    return settings.hasProperty (id) ? (double) settings[id] : defaultValue;
}

void AppSettings::setDouble (const juce::String& key, double value)
{
    auto settings = load();
    settings.setProperty (juce::Identifier (key), value, nullptr);
    save (settings);
}
