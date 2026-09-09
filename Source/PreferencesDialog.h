#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginScanView.h"
#include "StorageLocations.h"   // 設計書2.3.8：Generalカテゴリの保存先（Phase 57）

class AudioEngine;

//==============================================================================
/**
    仕様書6.2・設計書2.3.8「環境設定（Preferences）ダイアログ」。

    設計書2.3.8のとおり、**左にカテゴリ一覧、右に選択カテゴリの設定項目**という
    マスター・ディテール構成にしている。

    Phase 26で新設。それまで設定の置き場が無く、
    - プラグインのスキャンは独立した「Plugins」タブ
    - オーディオデバイスの選択はArrangeツールバーのボタン
    と、性質の同じものが画面のあちこちに散らばっていた。

    **Phase 57でGeneralを足し、設計書2.3.8の5カテゴリが揃いました**
    （General／Audio／Appearance／Shortcuts／Plugins）。
    Generalの中身は保存先フォルダの設定で、値と既定は`StorageLocations`が持っています（8.17）。

    **カテゴリを足すときは3箇所を同時に直すこと**：`Category`のenum、
    `categoryNames`の配列、`showCategory()`の出し分け。
    enumと配列は**並び順で対応**しているので、片方だけ足すと別のカテゴリが開きます。
*/
class PreferencesDialog : public juce::Component,
                           private juce::ListBoxModel
{
public:
    /** @param commandManagerToUse  仕様書6.2のShortcutsカテゴリで使う（Phase 47）。
                                    nullptrを渡すとそのカテゴリを出さない。 */
    PreferencesDialog (AudioEngine& engineToUse,
                       juce::ApplicationCommandManager* commandManagerToUse);
    ~PreferencesDialog() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** プラグインのスキャンが終わったときに呼ばれる（ブラウザパネルの更新用）。 */
    std::function<void()> onPluginsScanned;

    /** このダイアログを、独立したウィンドウとして開く。

        DialogWindow::LaunchOptionsは閉じられた時点で自分を破棄するため、
        呼び出し側が保持する必要はない。 */
    static void launch (AudioEngine& engine, juce::ApplicationCommandManager* commandManager,
                        std::function<void()> onPluginsScanned);

    //==========================================================================
    // 仕様書6.2：ショートカットの割り当ての保存（Phase 47）。
    //
    // **アプリ設定に入れている**（プロジェクトではなく）。曲の内容ではなく
    // 「この環境での操作の好み」なので、どのプロジェクトを開いても同じであってほしい。

    /** 保存済みの割り当てを読み込む（起動時に1回、既定値を作った後で呼ぶ）。 */
    static void loadKeyMappings (juce::ApplicationCommandManager& commandManager);

    /** いまの割り当てをアプリ設定へ書く。 */
    static void saveKeyMappings (juce::ApplicationCommandManager& commandManager);

private:
    enum class Category
    {
        General,
        Audio,
        Appearance,
        Shortcuts,
        Plugins
    };

    void showCategory (Category category);

    //==========================================================================
    // 設計書2.3.8：General＝保存先フォルダの設定（Phase 57／8.1のB3）

    /** 保存先1つぶんの行（見出し・パス・「変更...」・「既定に戻す」・説明）。

        **4種類ぶんを配列で持って、同じコードで並べている。**
        1つずつメンバーを書くと、種類を足すたびに
        「作る・見せる・並べる・更新する」の4箇所へ手を入れることになる。 */
    struct FolderRow
    {
        StorageLocations::Kind kind {};
        juce::Label caption;
        juce::Label pathLabel;
        juce::TextButton browseButton;
        juce::TextButton resetButton;
        juce::Label descriptionLabel;
    };

    /** 各行の表示を、いまの設定に合わせる（変更・リセットの後に呼ぶ）。 */
    void refreshFolderRows();

    /** フォルダ選択ダイアログを出して、選ばれたら設定へ書く。 */
    void chooseFolderFor (StorageLocations::Kind kind);

    /** 仕様書6.2：配色を選ぶ（Phase 35）。選んだ時点でアプリ設定へ書き、
        「次回起動から反映される」ことを画面に出す。 */
    void themeChanged();

    // ListBoxModel（左のカテゴリ一覧）
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    AudioEngine& engine;
    juce::ApplicationCommandManager* commandManager = nullptr;

    juce::ListBox categoryList;
    Category currentCategory = Category::General;

    /** 設計書2.3.8：保存先の設定（Phase 57）。`StorageLocations::Kind`の順に並べる。 */
    juce::OwnedArray<FolderRow> folderRows;
    juce::Label folderCaption;
    juce::Label folderNoticeLabel;

    /** フォルダ選択ダイアログ。**メンバーで保持すること**：
        `launchAsync()`は非同期なので、ローカル変数だと選ぶ前に壊れる。 */
    std::unique_ptr<juce::FileChooser> folderChooser;

    /** 仕様書6.2：オーディオデバイスの選択（JUCE標準のコンポーネント）。
        Phase 26より前は独立したダイアログで出していた。 */
    std::unique_ptr<juce::Component> audioSettings;

    /** 仕様書6.2：配色（Phase 35）。ライト／ダークの2つだけなので、
        コンボボックスではなく並んだボタンで直接選ばせる。 */
    juce::Label themeCaption;
    juce::TextButton lightThemeButton;
    juce::TextButton darkThemeButton;
    juce::Label themeNoticeLabel;

    /** 8.163：**表示の言語**（Phase 201／本人の要望。仕様書6章）。

        配色と同じ「見た目」の欄に置いています——**どちらも次の起動から効く**ので、
        同じ注意書きの下に並んでいるほうが、片方だけ「すぐ変わる」と思われずに済みます。

        **こちらはコンボボックス**です：3つあり、しかも1つは名前が長い
        （"English + 日本語の説明"）ので、横並びのボタンでは幅が揃いません。 */
    juce::Label languageCaption;
    juce::ComboBox languageBox;
    juce::Label languageNoticeLabel;

    /** 仕様書6.2・設計書2.3.8：ショートカットの一覧と再割り当て（Phase 47）。

        **JUCE標準の`KeyMappingEditorComponent`をそのまま使っている。**
        コマンドのカテゴリごとに折りたためる一覧、クリックでの再割り当て、
        既定へ戻すボタンまで揃っていて、自作しても同じものになる。

        `commandManager`が渡されていないときだけ`nullptr`のまま。 */
    std::unique_ptr<juce::KeyMappingEditorComponent> keyMappingEditor;

    juce::Label shortcutsNoticeLabel;

    /** 仕様書5.8：プラグインのスキャンと一覧。 */
    PluginScanView pluginScanView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreferencesDialog)
};
