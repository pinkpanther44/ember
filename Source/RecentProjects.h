#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppSettings.h"
#include "ProjectModel.h"

//==============================================================================
/**
    8.151：**最近開いたプロジェクト**（Phase 189／改善案⑰。仕様書10.3）。

    ### 置き場所

    `%APPDATA%\PersonalDAW\settings.xml`（`AppSettings`）です。
    **プロジェクトファイルには入れられません**——「どのプロジェクトを開いても
    同じでいてほしい値」なので、`AppSettings.h`が分けている理由がそのまま当てはまります。

    ### `juce::RecentlyOpenedFilesList`を使っていない

    あれは`juce_gui_extra`にあり、**この小さな用途のために重いヘッダを
    引き込むことになります**（`ProjectChooser.h`は`juce_gui_basics`だけで足りる）。
    やることは3つだけなので、ここに書いてあります——
    **重複を除く・上限で切る・もう無いファイルを落とす**。

    ### 足すのは1箇所ではない

    **開いたときと、名前を付けて保存したときの両方で足すこと。**
    保存で足し忘れると、作ったばかりのプロジェクトが一覧に出ず、
    「さっきまで開いていたのに無い」という見え方になります（1.15と同じ形の抜け）。
*/
namespace RecentProjects
{
    /** 設定のキー。**綴りを変えると履歴が消えます**（読めないだけで、実害はない）。 */
    inline const char* const settingsKey = "recentProjects";

    /** 一覧に残す本数。画面に一度に出る量として決めています。 */
    inline constexpr int maximumEntries = 12;

    /** 一覧（**新しいものが先頭**）。無くなったファイルは外して返します。

        外す判定を書き出す側でやると、消したファイルが並んだままになり、
        **押して初めて開けないと分かります**（8.17の「黙って既定へ落とす」と同じ考え方）。 */
    inline juce::Array<juce::File> getFiles()
    {
        juce::StringArray paths;
        paths.addLines (AppSettings::getString (settingsKey));

        juce::Array<juce::File> files;

        for (const auto& path : paths)
        {
            if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
                continue;   // 設定ファイルは手で編集できる。壊れた値がある前提で見る

            const juce::File file (path);

            if (file.existsAsFile() && ! files.contains (file))
                files.add (file);

            if (files.size() >= maximumEntries)
                break;
        }

        return files;
    }

    inline void add (const juce::File& file)
    {
        // **プロジェクトファイル以外は入れない。** 書き出したWAVやMIDIを
        // 履歴へ混ぜると、選んでも開けないものが並びます
        if (! ProjectModel::isProjectFile (file) || ! file.existsAsFile())
            return;

        auto files = getFiles();

        // **同じものが二度並ばないよう、先に外してから先頭へ入れる**
        files.removeAllInstancesOf (file);
        files.insert (0, file);

        juce::StringArray paths;

        for (const auto& f : files)
        {
            if (paths.size() >= maximumEntries)
                break;

            paths.add (f.getFullPathName());
        }

        AppSettings::setString (settingsKey, paths.joinIntoString ("\n"));
    }

    inline void clear()
    {
        AppSettings::setString (settingsKey, juce::String());
    }
}
