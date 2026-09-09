#include "PreferencesDialog.h"
#include "AppColours.h"
#include "AudioEngine.h"
#include "Utf8.h"
#include "AppSettings.h"

namespace
{
    /** 左のカテゴリ一覧に並べる項目。追加するときはCategoryのenumと順序を揃えること。 */
    const char* const categoryNames[] = { "General", "Audio", "Appearance", "Shortcuts", "Plugins" };
    constexpr int numCategories = (int) (sizeof (categoryNames) / sizeof (categoryNames[0]));

    /** 設計書2.3.8：Generalに並べる保存先（Phase 57）。**enumの並びと対**。 */
    constexpr StorageLocations::Kind folderKinds[] =
    {
        StorageLocations::Kind::projects,
        StorageLocations::Kind::backups,
        StorageLocations::Kind::recordings,
        StorageLocations::Kind::templates
    };

    /** 仕様書6.2：ショートカットの割り当てを入れておくアプリ設定のキー（Phase 47）。
        中身は`KeyPressMappingSet::createXml()`が作るXMLの文字列。 */
    const juce::String keyMappingsKey { "keyMappings" };
}

PreferencesDialog::PreferencesDialog (AudioEngine& engineToUse,
                                       juce::ApplicationCommandManager* commandManagerToUse)
    : engine (engineToUse), commandManager (commandManagerToUse), pluginScanView (engineToUse)
{
    categoryList.setModel (this);
    categoryList.setColour (juce::ListBox::backgroundColourId, AppColours::panel);
    categoryList.setRowHeight (28);
    addAndMakeVisible (categoryList);

    audioSettings = engine.createAudioSettingsComponent();

    if (audioSettings != nullptr)
        addChildComponent (audioSettings.get());

    //==========================================================================
    // 設計書2.3.8：General＝保存先フォルダ（Phase 57／8.1のB3）
    folderCaption.setText (utf8 ("保存先"), juce::dontSendNotification);
    folderCaption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    folderCaption.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addChildComponent (folderCaption);

    for (const auto kind : folderKinds)
    {
        auto* row = folderRows.add (new FolderRow());
        row->kind = kind;

        row->caption.setText (StorageLocations::getDisplayName (kind), juce::dontSendNotification);
        row->caption.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        row->caption.setColour (juce::Label::textColourId, AppColours::textPrimary);
        addChildComponent (row->caption);

        // パスは**読むだけ**。手で打たせると、存在しない場所や相対パスが入る
        // （StorageLocations側で弾いてはいるが、弾かれた理由が画面から分からない）
        row->pathLabel.setFont (juce::FontOptions (12.0f));
        row->pathLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
        row->pathLabel.setColour (juce::Label::backgroundColourId, AppColours::background);
        row->pathLabel.setMinimumHorizontalScale (1.0f);   // 縮めずに末尾を省略させる
        addChildComponent (row->pathLabel);

        row->browseButton.setButtonText (utf8 ("変更..."));
        row->browseButton.onClick = [this, kind] { chooseFolderFor (kind); };
        addChildComponent (row->browseButton);

        row->resetButton.setButtonText (utf8 ("既定に戻す"));
        row->resetButton.onClick = [this, kind]
        {
            StorageLocations::setFolder (kind, juce::File());
            refreshFolderRows();
        };
        addChildComponent (row->resetButton);

        row->descriptionLabel.setText (StorageLocations::getDescription (kind), juce::dontSendNotification);
        row->descriptionLabel.setFont (juce::FontOptions (11.0f));
        row->descriptionLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
        row->descriptionLabel.setJustificationType (juce::Justification::topLeft);
        addChildComponent (row->descriptionLabel);
    }

    folderNoticeLabel.setFont (juce::FontOptions (11.0f));
    folderNoticeLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    folderNoticeLabel.setJustificationType (juce::Justification::topLeft);
    folderNoticeLabel.setText (utf8 ("変更はすぐ保存され、次に保存・録音するときから使われます。"
                                      "既にあるファイルは移動しません。"),
                                juce::dontSendNotification);
    addChildComponent (folderNoticeLabel);

    refreshFolderRows();

    //==========================================================================
    // 仕様書6.2：配色（Phase 35）
    themeCaption.setText (utf8 ("配色"), juce::dontSendNotification);
    themeCaption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    themeCaption.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addChildComponent (themeCaption);

    lightThemeButton.setButtonText (utf8 ("ライト"));
    lightThemeButton.setClickingTogglesState (false); // 見た目は手動で制御する
    lightThemeButton.onClick = [this]
    {
        AppColours::saveThemeToSettings (AppColours::Theme::Light);
        themeChanged();
    };
    addChildComponent (lightThemeButton);

    darkThemeButton.setButtonText (utf8 ("ダーク"));
    darkThemeButton.setClickingTogglesState (false);
    darkThemeButton.onClick = [this]
    {
        AppColours::saveThemeToSettings (AppColours::Theme::Dark);
        themeChanged();
    };
    addChildComponent (darkThemeButton);

    themeNoticeLabel.setFont (juce::FontOptions (12.0f));
    themeNoticeLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    themeNoticeLabel.setJustificationType (juce::Justification::topLeft);
    addChildComponent (themeNoticeLabel);

    themeChanged(); // 今の設定に合わせてボタンの見た目を整える

    //==========================================================================
    // 8.163：表示の言語（Phase 201／本人の要望。仕様書6章）
    languageCaption.setText (utf8 ("言語"), juce::dontSendNotification);
    languageCaption.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    languageCaption.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addChildComponent (languageCaption);

    // **並びはenumの順**（日本語→英語→英語＋説明は日本語）。
    // IDは`(int) Mode + 1`——`ComboBox`は0を「選択なし」に使うため
    for (auto mode : { Localise::Mode::japanese,
                        Localise::Mode::english,
                        Localise::Mode::englishWithJapaneseHelp })
        languageBox.addItem (Localise::getModeName (mode), (int) mode + 1);

    languageBox.setSelectedId ((int) Localise::getMode() + 1, juce::dontSendNotification);
    languageBox.setTooltip (utf8 ("画面に出る言葉の切り替え。「English + 日本語の説明」は、"
                                   "ボタン名などは英語・ツールチップや案内文は日本語になります"));

    languageBox.onChange = [this]
    {
        const int id = languageBox.getSelectedId();

        if (id <= 0)
            return;

        // **設定へ書くだけ。** いま出ている画面は作り直しません（下の注意書き）
        Localise::setMode ((Localise::Mode) (id - 1));
        languageNoticeLabel.setColour (juce::Label::textColourId, AppColours::orange);
    };

    addChildComponent (languageBox);

    languageNoticeLabel.setFont (juce::FontOptions (12.0f));
    languageNoticeLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    languageNoticeLabel.setJustificationType (juce::Justification::topLeft);
    languageNoticeLabel.setText (utf8 ("次回アプリを起動したときから反映されます。"),
                                  juce::dontSendNotification);
    addChildComponent (languageNoticeLabel);

    addChildComponent (pluginScanView);

    // スキャン結果はブラウザパネル（設計書2.3.6）も見ているので、そのまま中継する
    pluginScanView.onPluginsScanned = [this]
    {
        if (onPluginsScanned != nullptr)
            onPluginsScanned();
    };

    // 仕様書6.2・設計書2.3.8：ショートカットの一覧と再割り当て（Phase 47）
    if (commandManager != nullptr)
    {
        // 第2引数true＝「既定に戻す」ボタンを出す
        keyMappingEditor = std::make_unique<juce::KeyMappingEditorComponent> (
                               *commandManager->getKeyMappings(), true);

        keyMappingEditor->setColours (AppColours::background, AppColours::textPrimary);
        addChildComponent (*keyMappingEditor);
    }

    shortcutsNoticeLabel.setFont (juce::FontOptions (12.0f));
    shortcutsNoticeLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    shortcutsNoticeLabel.setJustificationType (juce::Justification::topLeft);
    shortcutsNoticeLabel.setText (
        commandManager != nullptr
            ? utf8 ("項目をクリックしてから、割り当てたいキーを押してください。"
                     "変更はすぐ保存され、次回起動時も残ります。\n"
                     "既定の割り当てはStudio Oneに合わせてあります（仕様書6章）。")
            : utf8 ("この画面からはショートカットを変更できません。"),
        juce::dontSendNotification);
    addChildComponent (shortcutsNoticeLabel);

    categoryList.selectRow (0);
    showCategory (Category::General);
}

PreferencesDialog::~PreferencesDialog()
{
    // 仕様書6.2：閉じるときに割り当てを書き出す（Phase 47）。
    // **1つ変えるたびではなく閉じるときに1回**にしてある。
    // KeyMappingEditorComponentは変更の通知を出さないので、
    // 変更を拾おうとすると自前でKeyPressMappingSetを購読することになる。
    if (commandManager != nullptr)
        saveKeyMappings (*commandManager);

    // 破棄後に呼ばれないよう、購読を先に外す
    pluginScanView.onPluginsScanned = nullptr;
    categoryList.setModel (nullptr);
}

//==============================================================================
// 仕様書6.2：ショートカットの割り当ての保存（Phase 47）

void PreferencesDialog::loadKeyMappings (juce::ApplicationCommandManager& commandManager)
{
    const auto saved = AppSettings::getString (keyMappingsKey);

    if (saved.isEmpty())
        return;   // 一度も変えていなければ、既定のまま

    auto xml = juce::parseXML (saved);

    if (xml == nullptr)
        return;

    // **Phase 47の保存形式（一式）は捨てる。** あちらは`createXml(false)`で
    // 書いていたため、`restoreFromXml()`が「既存の割り当てを全部消してから
    // 保存されたものだけを載せる」動きになる。その結果、
    // **保存より後に足したコマンドには、既定のキーが1つも付かない**
    // （Phase 48で足したループ系のキーが効かなかった原因）。
    //
    // 捨てると、その形式で保存された変更は失われる。それでも、
    // 新しいコマンドが黙って無効になるほうが後から気づきにくいので、こちらを採った。
    if (! xml->getBoolAttribute ("basedOnDefaults", true))
    {
        AppSettings::setString (keyMappingsKey, {});
        return;
    }

    commandManager.getKeyMappings()->restoreFromXml (*xml);
}

void PreferencesDialog::saveKeyMappings (juce::ApplicationCommandManager& commandManager)
{
    // **第1引数はtrue（既定との差分だけ）にすること。**
    //
    // falseにすると「完全な一式」として書き出され、読み込み側は
    // 既存の割り当てを全部消してからそれを載せる。すると
    // **保存より後に足したコマンドが、既定のキーを持たないまま生まれる。**
    // Phase 47ではfalseにしていて、Phase 48で足したコマンドが効かなくなった。
    //
    // trueなら読み込み側が先に既定へ戻してから差分を載せるので、
    // 新しいコマンドは既定のまま、変えた項目だけがユーザーの設定を保つ。
    if (auto xml = commandManager.getKeyMappings()->createXml (true))
        AppSettings::setString (keyMappingsKey, xml->toString());
}

void PreferencesDialog::launch (AudioEngine& engine, juce::ApplicationCommandManager* commandManager,
                                 std::function<void()> onPluginsScanned)
{
    auto dialog = std::make_unique<PreferencesDialog> (engine, commandManager);
    dialog->onPluginsScanned = std::move (onPluginsScanned);
    dialog->setSize (760, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (dialog.release());
    options.dialogTitle = utf8 ("環境設定");
    options.dialogBackgroundColour = AppColours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    // launchAsync()が返すウィンドウは、閉じられた時点で自分自身を破棄する。
    // こちらで保持するとMainComponent破棄時の順序を気にする必要が出るため、任せる
    // （AudioEngine::showAudioSettingsDialog()も同じ方式だった）。
    options.launchAsync();
}

void PreferencesDialog::showCategory (Category category)
{
    currentCategory = category;

    // 設計書2.3.8：General＝保存先（Phase 57）
    const bool showGeneral = (category == Category::General);
    folderCaption.setVisible (showGeneral);
    folderNoticeLabel.setVisible (showGeneral);

    for (auto* row : folderRows)
    {
        row->caption.setVisible (showGeneral);
        row->pathLabel.setVisible (showGeneral);
        row->browseButton.setVisible (showGeneral);
        row->resetButton.setVisible (showGeneral);
        row->descriptionLabel.setVisible (showGeneral);
    }

    if (audioSettings != nullptr)
        audioSettings->setVisible (category == Category::Audio);

    const bool showAppearance = (category == Category::Appearance);
    themeCaption.setVisible (showAppearance);
    lightThemeButton.setVisible (showAppearance);
    darkThemeButton.setVisible (showAppearance);
    themeNoticeLabel.setVisible (showAppearance);

    // 8.163：言語も「見た目」の欄（Phase 201）
    languageCaption.setVisible (showAppearance);
    languageBox.setVisible (showAppearance);
    languageNoticeLabel.setVisible (showAppearance);

    // 仕様書6.2：ショートカット（Phase 47）
    const bool showShortcuts = (category == Category::Shortcuts);

    if (keyMappingEditor != nullptr)
        keyMappingEditor->setVisible (showShortcuts);

    shortcutsNoticeLabel.setVisible (showShortcuts);

    pluginScanView.setVisible (category == Category::Plugins);

    resized();
}

void PreferencesDialog::refreshFolderRows()
{
    for (auto* row : folderRows)
    {
        const auto folder = StorageLocations::getFolder (row->kind);
        const bool isDefault = StorageLocations::isUsingDefault (row->kind);

        row->pathLabel.setText (folder.getFullPathName(), juce::dontSendNotification);

        // 既定のままなら「戻す」ことは何も無いので、押せなくしておく
        // （押しても何も起きないボタンを出すと、効いていないのか迷わせる）
        row->resetButton.setEnabled (! isDefault);

        // 既定から変えてあることが、パスを読まなくても分かるようにする
        row->pathLabel.setColour (juce::Label::textColourId,
                                   isDefault ? AppColours::textSecondary : AppColours::textPrimary);
    }
}

void PreferencesDialog::chooseFolderFor (StorageLocations::Kind kind)
{
    folderChooser = std::make_unique<juce::FileChooser> (
                        StorageLocations::getDisplayName (kind) + utf8 ("の保存先を選ぶ"),
                        StorageLocations::getFolder (kind));

    folderChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
        [this, kind] (const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();

            if (folder == juce::File())
                return;   // キャンセル

            StorageLocations::setFolder (kind, folder);
            refreshFolderRows();
        });
}

void PreferencesDialog::themeChanged()
{
    // 保存済みの値を正とする（AppColours::getTheme()は「今描いている色」で、
    // 再起動するまで選んだものとは食い違うため）。
    const auto saved = AppSettings::getInt ("theme", 0) == 1 ? AppColours::Theme::Dark
                                                              : AppColours::Theme::Light;
    const bool isDark = (saved == AppColours::Theme::Dark);

    // 設計書2.6：選ばれているほうをパープルで示す
    auto applySelected = [] (juce::TextButton& button, bool selected)
    {
        button.setColour (juce::TextButton::buttonColourId,
                           selected ? AppColours::purple : AppColours::background);
        button.setColour (juce::TextButton::textColourOffId,
                           selected ? juce::Colours::white : AppColours::textPrimary);
    };

    applySelected (lightThemeButton, ! isDark);
    applySelected (darkThemeButton, isDark);

    // 反映されるのが次回起動からであることを、選んだ場合だけはっきり出す。
    // 黙っていると「押したのに変わらない」と受け取られる。
    themeNoticeLabel.setText (saved == AppColours::getTheme()
                                  ? utf8 ("配色はアプリ全体の設定として保存されます。")
                                  : utf8 ("次回アプリを起動したときから反映されます。"),
                               juce::dontSendNotification);

    themeNoticeLabel.setColour (juce::Label::textColourId,
                                 saved == AppColours::getTheme() ? AppColours::textSecondary
                                                                  : AppColours::orange);
    repaint();
}

void PreferencesDialog::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    // 設計書2.3.8：左のカテゴリ一覧と右の設定項目の境目
    g.setColour (AppColours::border);
    g.drawVerticalLine (160, 0.0f, (float) getHeight());
}

void PreferencesDialog::resized()
{
    auto area = getLocalBounds();

    categoryList.setBounds (area.removeFromLeft (160));

    auto content = area.reduced (12);

    if (audioSettings != nullptr && audioSettings->isVisible())
        audioSettings->setBounds (content);

    // 設計書2.3.8：General＝保存先（Phase 57）。1種類ぶんを1ブロックとして縦に積む
    if (folderCaption.isVisible())
    {
        folderCaption.setBounds (content.removeFromTop (22));
        content.removeFromTop (6);

        for (auto* row : folderRows)
        {
            row->caption.setBounds (content.removeFromTop (18));

            auto pathRow = content.removeFromTop (24);

            // ボタンを先に右から取る。**パスは残りに置く**ので、
            // ダイアログを縮めてもボタンが押せなくならない
            row->resetButton.setBounds (pathRow.removeFromRight (90));
            pathRow.removeFromRight (6);
            row->browseButton.setBounds (pathRow.removeFromRight (72));
            pathRow.removeFromRight (8);
            row->pathLabel.setBounds (pathRow);

            content.removeFromTop (2);
            row->descriptionLabel.setBounds (content.removeFromTop (28));
            content.removeFromTop (10);
        }

        folderNoticeLabel.setBounds (content.removeFromTop (30));
    }

    if (themeCaption.isVisible())
    {
        themeCaption.setBounds (content.removeFromTop (22));
        content.removeFromTop (8);

        auto buttonRow = content.removeFromTop (28);
        lightThemeButton.setBounds (buttonRow.removeFromLeft (110));
        buttonRow.removeFromLeft (8);
        darkThemeButton.setBounds (buttonRow.removeFromLeft (110));

        content.removeFromTop (12);
        themeNoticeLabel.setBounds (content.removeFromTop (40));
    }

    // 8.163：言語（Phase 201）。**配色のすぐ下**——どちらも次の起動から効く
    if (languageCaption.isVisible())
    {
        content.removeFromTop (12);
        languageCaption.setBounds (content.removeFromTop (22));
        content.removeFromTop (8);
        languageBox.setBounds (content.removeFromTop (28).withWidth (260));
        content.removeFromTop (8);
        languageNoticeLabel.setBounds (content.removeFromTop (24));
    }

    // 仕様書6.2：ショートカット（Phase 47）。説明を上に、一覧を残り全部に置く
    if (shortcutsNoticeLabel.isVisible())
    {
        shortcutsNoticeLabel.setBounds (content.removeFromTop (48));
        content.removeFromTop (8);

        if (keyMappingEditor != nullptr && keyMappingEditor->isVisible())
            keyMappingEditor->setBounds (content);
    }

    if (pluginScanView.isVisible())
        pluginScanView.setBounds (content);
}

//==============================================================================
int PreferencesDialog::getNumRows()
{
    return numCategories;
}

void PreferencesDialog::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                                           bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, numCategories))
        return;

    if (rowIsSelected)
    {
        g.setColour (AppColours::purple);
        g.fillRect (0, 0, width, height);
    }

    g.setColour (rowIsSelected ? juce::Colours::white : AppColours::textPrimary);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (categoryNames[rowNumber], 12, 0, width - 16, height, juce::Justification::centredLeft);
}

void PreferencesDialog::selectedRowsChanged (int lastRowSelected)
{
    if (juce::isPositiveAndBelow (lastRowSelected, numCategories))
        showCategory (static_cast<Category> (lastRowSelected));
}
