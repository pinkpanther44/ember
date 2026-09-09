#include "PluginCrashTracker.h"
#include "Branding.h"   // 8.175：保存先の名前（Phase 216）

PluginCrashTracker::PluginCrashTracker()
{
    loadCrashCounts();
}

juce::File PluginCrashTracker::getDataFolder() const
{
    auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                      .getChildFile (Branding::dataFolderName);

    folder.createDirectory();
    return folder;
}

juce::File PluginCrashTracker::getMarkerFile() const
{
    return getDataFolder().getChildFile ("crash_marker.txt");
}

juce::File PluginCrashTracker::getCrashCountsFile() const
{
    return getDataFolder().getChildFile ("plugin_crashes.xml");
}

juce::Identifier PluginCrashTracker::makeSafeKey (const juce::String& pluginIdentifier)
{
    // プラグイン識別子にはパス区切りや記号が含まれうるため、
    // ValueTreeのプロパティ名として使えるようハッシュ化する。
    return juce::Identifier ("p" + juce::String (pluginIdentifier.hashCode64()).replace ("-", "_"));
}

void PluginCrashTracker::loadCrashCounts()
{
    auto file = getCrashCountsFile();

    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto loaded = juce::ValueTree::fromXml (*xml);
        if (loaded.isValid())
            crashCounts = loaded;
    }
}

void PluginCrashTracker::saveCrashCounts()
{
    if (auto xml = crashCounts.createXml())
        xml->writeTo (getCrashCountsFile());
}

juce::String PluginCrashTracker::checkForPreviousCrash()
{
    auto marker = getMarkerFile();

    if (! marker.existsAsFile())
        return {};

    const auto pluginIdentifier = marker.loadFileAsString().trim();
    marker.deleteFile();

    if (pluginIdentifier.isEmpty())
        return {};

    // 前回このプラグインの処理中に正常終了しなかった＝クラッシュとみなす
    const auto key = makeSafeKey (pluginIdentifier);
    const int newCount = (int) crashCounts.getProperty (key, 0) + 1;
    crashCounts.setProperty (key, newCount, nullptr);

    // 識別子そのものも保存しておく（UI表示やリセット時に参照するため）
    crashCounts.setProperty (juce::Identifier (key.toString() + "_name"), pluginIdentifier, nullptr);

    saveCrashCounts();

    return pluginIdentifier;
}

void PluginCrashTracker::beginPluginOperation (const juce::String& pluginIdentifier)
{
    // ロード直前にマーカーを書く。ここでクラッシュしても、次回起動時に検知できる。
    getMarkerFile().replaceWithText (pluginIdentifier);
}

void PluginCrashTracker::endPluginOperation()
{
    getMarkerFile().deleteFile();
}

int PluginCrashTracker::getCrashCount (const juce::String& pluginIdentifier) const
{
    return (int) crashCounts.getProperty (makeSafeKey (pluginIdentifier), 0);
}

bool PluginCrashTracker::shouldSandbox (const juce::String& pluginIdentifier) const
{
    return getCrashCount (pluginIdentifier) >= sandboxThreshold;
}

void PluginCrashTracker::resetCrashCount (const juce::String& pluginIdentifier)
{
    crashCounts.removeProperty (makeSafeKey (pluginIdentifier), nullptr);
    saveCrashCounts();
}
