#include "StorageLocations.h"
#include "Branding.h"   // 8.175：保存先の名前（Phase 216）
#include "AppSettings.h"
#include "Utf8.h"

namespace StorageLocations
{
    namespace
    {
        /** アプリ設定のキー。**接頭辞を付けてあるのは、settings.xmlが1枚の平たいツリーだから**
            （8.13のA3と同じ理由）。綴りを変えると設定が読めなくなる。 */
        juce::String getSettingsKey (Kind kind)
        {
            switch (kind)
            {
                case Kind::projects:   return "folderProjects";
                case Kind::backups:    return "folderBackups";
                case Kind::recordings: return "folderRecordings";
                case Kind::templates:  return "folderTemplates";
            }

            return {};
        }
    }

    juce::File getDefaultFolder (Kind kind)
    {
        switch (kind)
        {
            case Kind::projects:
                return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

            case Kind::backups:
                // **Phase 56で改名してもフォルダ名は据え置き**（8.16）。
                // ここを変えると、既にあるオートセーブが見つからなくなる
                return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile (Branding::dataFolderName);

            case Kind::recordings:
                // 仕様書9章：オーディオファイルはプロジェクトへコピーせず外部パス参照とする方針。
                // 録音ファイルもユーザーのドキュメント配下に置き、クリップからはパスで参照する
                return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (Branding::recordingsFolderName);

            case Kind::templates:
                return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (Branding::templatesFolderName);
        }

        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    juce::File getFolder (Kind kind)
    {
        const auto path = AppSettings::getString (getSettingsKey (kind));

        if (path.isEmpty())
            return getDefaultFolder (kind);

        // **相対パスをjuce::Fileへ渡すとアサーションで止まる**（Main.cppの引数解釈と同じ話）。
        // 設定ファイルは手で編集できるので、壊れた値が入っている前提で見ること
        if (! juce::File::isAbsolutePath (path))
            return getDefaultFolder (kind);

        const juce::File folder (path);

        // 外付けドライブやネットワークドライブを指したまま、次の起動で消えていることがある。
        // **黙って既定へ落とす**（保存に失敗して初めて気づく形にしないため）
        if (! folder.isDirectory())
            return getDefaultFolder (kind);

        return folder;
    }

    juce::File getFolderForWriting (Kind kind)
    {
        auto folder = getFolder (kind);

        if (folder.createDirectory().wasOk())
            return folder;

        // 設定された場所が作れない（権限が無い、ドライブが読み取り専用）。
        // 既定へ逃がす。**既定も作れない場合は、そのまま返して呼び出し側に失敗させる**
        // （ここで握り潰すと、どこにも書けていないことが分からなくなる）
        auto fallback = getDefaultFolder (kind);
        fallback.createDirectory();
        return fallback;
    }

    void setFolder (Kind kind, const juce::File& folder)
    {
        if (folder == juce::File())
        {
            AppSettings::setString (getSettingsKey (kind), {});   // 既定へ戻す
            return;
        }

        folder.createDirectory();
        AppSettings::setString (getSettingsKey (kind), folder.getFullPathName());
    }

    bool isUsingDefault (Kind kind)
    {
        return AppSettings::getString (getSettingsKey (kind)).isEmpty();
    }

    juce::String getDisplayName (Kind kind)
    {
        switch (kind)
        {
            case Kind::projects:   return utf8 ("プロジェクト");
            case Kind::backups:    return utf8 ("バックアップ");
            case Kind::recordings: return utf8 ("録音ファイル");
            case Kind::templates:  return utf8 ("テンプレート");
        }

        return {};
    }

    juce::String getDescription (Kind kind)
    {
        switch (kind)
        {
            case Kind::projects:
                return utf8 ("「開く」「名前を付けて保存」で最初に開くフォルダです。");

            case Kind::backups:
                // 8.175：**フォルダ名を文に埋め込まないこと**（Phase 216）。
                // 訳の表に入る文字列は**ビルド時に決まる**ので、ここに`PersonalDAW`と
                // 書いてあると、Emberでも「%APPDATA%\PersonalDAW」と出ます（実際に出ました）
                return utf8 ("オートセーブ（3分ごとの自動保存）の置き場所です。"
                              "設定・ショートカット・クラッシュ検知の目印は、"
                              "ここではなく次の場所に固定です：")
                        + juce::String (" %APPDATA%\\") + Branding::dataFolderName;

            case Kind::recordings:
                return utf8 ("録音した音声ファイル（WAV）の置き場所です。"
                              "クリップは録った場所をパスで参照するので、"
                              "変更しても、録音済みのファイルは移動しません。");

            case Kind::templates:
                // 8.151：Phase 189でここが使われ始めました（D8）
                return utf8 ("プロジェクトテンプレートの置き場所です。"
                              "ファイルメニューの「テンプレートとして保存...」で"
                              "ここへ保存され、起動時の選択画面と"
                              "「テンプレートから新規...」に出ます。");
        }

        return {};
    }
}
