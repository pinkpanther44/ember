#include "AutoSaveManager.h"
#include "StorageLocations.h"

namespace
{
    juce::File getDataFolder()
    {
        // 設計書2.3.8：Phase 57から**環境設定で置き場所を変えられる**（8.17）。
        // 既定は、プラグインのクラッシュ履歴（PluginCrashTracker）と同じ`%APPDATA%\PersonalDAW`。
        //
        // **クラッシュ検知の目印（crash_marker.txt）と設定（settings.xml）は動かさないこと。**
        // どちらもユーザーが触る場所に置くものではなく、
        // 設定に至っては「置き場所の設定」自体がそこに入っている。
        return StorageLocations::getFolderForWriting (StorageLocations::Kind::backups);
    }

    /** オートセーブ元のプロジェクトファイルのパスを控えておくファイル。
        復元時に「どのファイルの続きだったか」を復元するために使う。
        プロジェクトのValueTree自体には入れない（保存されるプロジェクトの中身に、
        リカバリ用の情報を混ぜたくないため）。 */
    juce::File getAutoSaveSourceFile()
    {
        return getDataFolder().getChildFile ("autosave_source.txt");
    }
}

juce::File AutoSaveManager::getAutoSaveFile()
{
    return getDataFolder().getChildFile ("autosave" + ProjectModel::getFileExtension());
}

AutoSaveManager::AutoSaveManager (ProjectModel& projectToUse)
    : project (projectToUse)
{
}

AutoSaveManager::~AutoSaveManager()
{
    stopTimer();
}

void AutoSaveManager::start()
{
    startTimer (autoSaveIntervalMilliseconds);
}

void AutoSaveManager::stop()
{
    stopTimer();
}

void AutoSaveManager::timerCallback()
{
    saveNow();
}

void AutoSaveManager::saveNow()
{
    // 変更が無いのに書き込むと、ディスクを触るだけで何の役にも立たない
    if (! project.hasUnsavedChanges())
        return;

    if (onBeforeAutoSave != nullptr)
        onBeforeAutoSave();

    auto file = getAutoSaveFile();

    if (! project.writeCopyToFile (file))
        return;

    // 元ファイルのパスを控える。未保存の新規プロジェクトなら空文字になる。
    getAutoSaveSourceFile().replaceWithText (project.getCurrentFile().getFullPathName());

    if (onAutoSaved != nullptr)
        onAutoSaved (file);
}

void AutoSaveManager::clearAutoSave()
{
    getAutoSaveFile().deleteFile();
    getAutoSaveSourceFile().deleteFile();
}

bool AutoSaveManager::hasRecoverableAutoSave() const
{
    return getAutoSaveFile().existsAsFile();
}

bool AutoSaveManager::restoreFromAutoSave()
{
    auto file = getAutoSaveFile();

    if (! file.existsAsFile())
        return false;

    if (! project.loadFromFile (file))
        return false;

    // 読み込み元はAppData配下のオートセーブファイルなので、そのまま「編集中のファイル」に
    // してしまうと、以降の「保存」がAppDataへ書き込まれてしまう。
    // 元のプロジェクトファイル（あれば）を編集対象に戻し、未保存扱いにする
    // ——復元した内容は、まだディスク上の元ファイルには反映されていないため。
    const auto sourcePath = getAutoSaveSourceFile().existsAsFile()
                                ? getAutoSaveSourceFile().loadFileAsString().trim()
                                : juce::String();

    project.markAsRecovered (sourcePath.isNotEmpty() ? juce::File (sourcePath) : juce::File());

    return true;
}
