#include "BrowserPanel.h"
#include "AppColours.h"
#include "AppSettings.h"
#include "AudioEngine.h"
#include "DragAndDropIds.h"
#include "PanelResizerBar.h"
#include "Utf8.h"

namespace
{
    // 設計書2.5と同じ考え方で、お気に入りはアプリ全体の設定へ保存する
    const juce::String favouritePluginsKey { "favouritePlugins" };
    const juce::String browserFolderKey    { "browserFolder" };

    /** お気に入りの区切り文字。プラグインの識別子には現れない文字を選ぶ。 */
    const juce::String favouriteSeparator { "|" };

    /** 8.68：カテゴリを名乗っていないプラグインの置き場所（Phase 107／改善案⑧）。

        **「分類なし」も1つの分類として並べる。** 出さないと、
        名乗っていないプラグインが**どのカテゴリを選んでも出てこない**ことになり、
        「絞ると消えるプラグインがある」という分かりにくい振る舞いになる。 */
    const juce::String uncategorisedLabel { utf8 ("(分類なし)") };
}

//==============================================================================
BrowserPanel::BrowserPanel (AudioEngine& audioEngineToUse)
    : audioEngine (audioEngineToUse),
      audioFileFilter ("*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3;*.mid;*.midi", "*",
                        utf8 ("オーディオ／MIDIファイル"))
{
    pluginsTabButton.onClick = [this] { setTab (Tab::Plugins); };
    filesTabButton.onClick   = [this] { setTab (Tab::Files); };
    addAndMakeVisible (pluginsTabButton);
    addAndMakeVisible (filesTabButton);

    //==========================================================================
    // プラグインタブ（仕様書4.4：検索ボックスを上部に固定表示）
    searchBox.setTextToShowWhenEmpty (utf8 ("検索..."), AppColours::textSecondary);
    searchBox.onTextChange = [this] { applyPluginFilter(); };
    addAndMakeVisible (searchBox);

    // 8.164：**種別はトグルボタン3つ**（Phase 202／本人の要望）。
    //
    // `setRadioGroupId()`で1組にすること。**1つ選ぶと他は外れ、
    // 選ばれているものを押しても外れません**——「どれも選んでいない」状態は、
    // 一覧が空になるだけで意味がないので作らせない。
    //
    // `setConnectedEdges()`で3つを繋げて描くと、**1組であることが見た目で分かります**
    // 8.163：**`utf8 (...)`の中に日本語を直接書くこと**（Phase 201の訳の表）。
    // ここで`const char*`を持ち回して後から`utf8 (t.text)`と渡すと、
    // **`Tools/Update-Translations.ps1`からは文字列が見えません**
    // ——実際にこの並びで一度やって、「エフェクト」がSTALEとして落ちました
    struct TypeButton { juce::TextButton* button; juce::String text; int connectedEdges; };

    const TypeButton typeButtons[] =
    {
        { &typeAllButton,        utf8 ("すべて"),     juce::Button::ConnectedOnRight },
        { &typeInstrumentButton, utf8 ("音源"),       juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight },
        { &typeEffectButton,     utf8 ("エフェクト"), juce::Button::ConnectedOnLeft }
    };

    for (const auto& t : typeButtons)
    {
        t.button->setButtonText (t.text);
        t.button->setClickingTogglesState (true);
        t.button->setRadioGroupId (1);
        t.button->setConnectedEdges (t.connectedEdges);
        t.button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        t.button->setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
        t.button->setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
        t.button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        t.button->onClick = [this] { applyPluginFilter(); };
        addAndMakeVisible (*t.button);
    }

    typeAllButton.setToggleState (true, juce::dontSendNotification);

    // 8.68：カテゴリで絞る（Phase 107／改善案⑧。仕様書4.4）。
    // 中身はスキャン結果から作るので、ここでは空のまま置いておく
    categoryFilterBox.onChange = [this] { applyPluginFilter(); };
    categoryFilterBox.setTooltip (utf8 ("プラグインが名乗っている分類で絞る"));
    addAndMakeVisible (categoryFilterBox);

    // 8.164：**★だけのボタン**（Phase 202／本人の報告）。
    //
    // **`fromUTF8`を通すこと。** Phase 201までは`ToggleButton { "★ only" }`と
    // リテラルを直接渡していて、画面には`â□□`と出ていました
    // （`juce::String`の`const char*`版は7bit ASCIIとして読む。`Utf8.h`）。
    // 一覧の行の★はずっと正しく出ていたので、**同じ書き方に揃えます**
    favouritesOnlyButton.setButtonText (juce::String::fromUTF8 ("\xe2\x98\x85"));
    favouritesOnlyButton.setClickingTogglesState (true);
    favouritesOnlyButton.setTooltip (utf8 ("お気に入り（★）を付けたプラグインだけを出す"));
    favouritesOnlyButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    favouritesOnlyButton.setColour (juce::TextButton::buttonOnColourId, AppColours::orange);
    favouritesOnlyButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
    favouritesOnlyButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    favouritesOnlyButton.onClick = [this] { applyPluginFilter(); };
    addAndMakeVisible (favouritesOnlyButton);

    pluginListBox.setModel (&pluginListModel);
    pluginListBox.setRowHeight (22);
    pluginListBox.setColour (juce::ListBox::backgroundColourId, AppColours::background);
    addAndMakeVisible (pluginListBox);

    emptyLabel.setJustificationType (juce::Justification::centredTop);
    emptyLabel.setFont (juce::FontOptions (12.0f));
    emptyLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    emptyLabel.setText (utf8 ("環境設定でスキャンすると\nここに一覧が出ます。"),
                         juce::dontSendNotification);
    addAndMakeVisible (emptyLabel);

    //==========================================================================
    // ファイルタブ
    directoryScanThread.startThread (juce::Thread::Priority::background);

    directoryList = std::make_unique<juce::DirectoryContentsList> (&audioFileFilter, directoryScanThread);
    directoryList->addChangeListener (this);

    // 前回見ていたフォルダを覚えておく（毎回選び直さずに済むように）
    const auto savedFolder = AppSettings::getString (browserFolderKey);
    auto startFolder = savedFolder.isNotEmpty() ? juce::File (savedFolder)
                                                 : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    if (! startFolder.isDirectory())
        startFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    directoryList->setDirectory (startFolder, true, true);

    fileTree = std::make_unique<juce::FileTreeComponent> (*directoryList);
    fileTree->setColour (juce::FileTreeComponent::backgroundColourId, AppColours::background);

    // 8.133：行の絵を差し替える（Phase 169／改善案44）。
    // **壊れる前に外すこと**（デストラクタで`setLookAndFeel(nullptr)`）
    fileTree->setLookAndFeel (&fileRowLookAndFeel);

    fileTree->addListener (this);
    addChildComponent (fileTree.get());

    chooseFolderButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (utf8 ("表示するフォルダを選択"),
                                                            directoryList->getDirectory());

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& chooser)
            {
                const auto folder = chooser.getResult();

                if (! folder.isDirectory())
                    return;

                directoryList->setDirectory (folder, true, true);
                AppSettings::setString (browserFolderKey, folder.getFullPathName());
            });
    };
    addChildComponent (chooseFolderButton);

    refreshPluginList();
    setTab (Tab::Plugins);
}

BrowserPanel::~BrowserPanel()
{
    // ツリー／リストがまだ生きているうちに購読を外す。
    // DirectoryContentsListはスキャン用スレッドから通知を投げるため、
    // 先にスレッドを止めてから壊す必要がある。
    directoryList->removeChangeListener (this);

    if (fileTree != nullptr)
    {
        // 8.133：**LookAndFeelを外してから壊す**（Phase 169）。
        // 付けたままにすると、破棄の途中で引きに行って落ちることがある
        fileTree->setLookAndFeel (nullptr);
        fileTree->removeListener (this);
    }

    fileTree.reset();
    directoryList.reset();
    directoryScanThread.stopThread (2000);

    pluginListBox.setModel (nullptr);
}

//==============================================================================
void BrowserPanel::setTab (Tab newTab)
{
    currentTab = newTab;

    const bool showPlugins = (currentTab == Tab::Plugins);

    // Pluginsタブへ戻ってきたときに、その間にスキャンされた結果を取り込む
    if (showPlugins)
    {
        allPlugins = audioEngine.getPluginManager().getKnownPlugins();
        refreshCategoryFilter();   // 8.68：増えたぶんの分類も並べ直す（Phase 107）
    }

    searchBox.setVisible (showPlugins);

    // 8.164：種別のトグル3つ（Phase 202）
    typeAllButton.setVisible (showPlugins);
    typeInstrumentButton.setVisible (showPlugins);
    typeEffectButton.setVisible (showPlugins);

    // 8.68：分類が1つも無いなら、Pluginsタブでも出さない（Phase 107）
    categoryFilterBox.setVisible (showPlugins && ! pluginCategories.isEmpty());

    favouritesOnlyButton.setVisible (showPlugins);
    pluginListBox.setVisible (showPlugins);
    emptyLabel.setVisible (showPlugins && filteredPlugins.isEmpty());

    if (fileTree != nullptr)
        fileTree->setVisible (! showPlugins);

    chooseFolderButton.setVisible (! showPlugins);

    updateTabButtons();
    resized();
}

void BrowserPanel::updateTabButtons()
{
    // 設計書2.6：選択中のタブはパープルで示す
    const bool showPlugins = (currentTab == Tab::Plugins);

    pluginsTabButton.setColour (juce::TextButton::buttonColourId,
                                 showPlugins ? AppColours::purple : AppColours::background);
    pluginsTabButton.setColour (juce::TextButton::textColourOffId,
                                 showPlugins ? juce::Colours::white : AppColours::textPrimary);

    filesTabButton.setColour (juce::TextButton::buttonColourId,
                               showPlugins ? AppColours::background : AppColours::purple);
    filesTabButton.setColour (juce::TextButton::textColourOffId,
                               showPlugins ? AppColours::textPrimary : juce::Colours::white);
}

void BrowserPanel::visibilityChanged()
{
    // 他のタブでスキャンした結果を、開いたときに取り込む
    if (isVisible())
        refreshPluginList();
}

void BrowserPanel::refreshPluginList()
{
    allPlugins = audioEngine.getPluginManager().getKnownPlugins();

    refreshCategoryFilter();   // 8.68：一覧は中身から作る（Phase 107）
    applyPluginFilter();
}

juce::StringArray BrowserPanel::getCategoryTokens (const juce::PluginDescription& description)
{
    juce::StringArray tokens;

    // VST3は`"Fx|Reverb"`のように縦棒で連ねる。切って1つずつ集める
    tokens.addTokens (description.category, "|", "");
    tokens.trim();
    tokens.removeEmptyStrings();
    tokens.removeDuplicates (true);

    return tokens;
}

void BrowserPanel::refreshCategoryFilter()
{
    // **選び直しても同じ分類のままにする。** 番号だけ覚えていると、
    // スキャンし直して並びが変わったときに別の分類で絞ってしまう
    const auto previousSelection = categoryFilterBox.getSelectedId() > 1
                                       ? pluginCategories[categoryFilterBox.getSelectedId() - 2]
                                       : juce::String();

    pluginCategories.clearQuick();

    bool hasUncategorised = false;

    for (const auto& description : allPlugins)
    {
        const auto tokens = getCategoryTokens (description);

        if (tokens.isEmpty())
        {
            hasUncategorised = true;
            continue;
        }

        pluginCategories.addArray (tokens);
    }

    pluginCategories.removeDuplicates (true);
    pluginCategories.sort (true);

    // 「分類なし」は最後（分類そのものではないので、名前順の中に混ぜない）
    if (hasUncategorised)
        pluginCategories.add (uncategorisedLabel);

    categoryFilterBox.clear (juce::dontSendNotification);
    categoryFilterBox.addItem (utf8 ("全分類"), 1);

    for (int i = 0; i < pluginCategories.size(); ++i)
        categoryFilterBox.addItem (pluginCategories[i], i + 2);

    const int restoredIndex = pluginCategories.indexOf (previousSelection);

    categoryFilterBox.setSelectedId (restoredIndex >= 0 ? restoredIndex + 2 : 1,
                                      juce::dontSendNotification);

    // 分類が1つも無いなら（スキャン前など）出さない。場所を取るだけなので
    categoryFilterBox.setVisible (currentTab == Tab::Plugins && ! pluginCategories.isEmpty());
    resized();
}

int BrowserPanel::getTypeFilter() const
{
    // 8.164：**番号はPhase 201までのコンボボックスのIDと同じ**（Phase 202）。
    // 揃えてあるので、絞り込みの分岐（`applyPluginFilter()`）は書き換えずに済む
    if (typeInstrumentButton.getToggleState()) return 2;
    if (typeEffectButton.getToggleState())     return 3;

    return 1;   // すべて
}

void BrowserPanel::applyPluginFilter()
{
    const auto searchText = searchBox.getText().trim();
    const int typeFilter = getTypeFilter();   // 1=すべて 2=音源 3=エフェクト（8.164）
    const bool favouritesOnly = favouritesOnlyButton.getToggleState();

    // 8.68：カテゴリで絞る（Phase 107／改善案⑧）。1＝全分類なので空文字＝絞らない
    const auto categoryFilter = categoryFilterBox.getSelectedId() > 1
                                    ? pluginCategories[categoryFilterBox.getSelectedId() - 2]
                                    : juce::String();

    filteredPlugins.clearQuick();

    for (const auto& description : allPlugins)
    {
        if (typeFilter == 2 && ! description.isInstrument)
            continue;

        if (typeFilter == 3 && description.isInstrument)
            continue;

        if (categoryFilter.isNotEmpty())
        {
            const auto tokens = getCategoryTokens (description);

            // 「分類なし」を選んだときは、**何も名乗っていないもの**を出す
            const bool matches = (categoryFilter == uncategorisedLabel)
                                     ? tokens.isEmpty()
                                     : tokens.contains (categoryFilter, true);

            if (! matches)
                continue;
        }

        if (favouritesOnly && ! isFavourite (description))
            continue;

        // 名前だけでなく製造元でも引っかかるようにする（"Valhalla"のような探し方ができる）
        if (searchText.isNotEmpty()
             && ! description.name.containsIgnoreCase (searchText)
             && ! description.manufacturerName.containsIgnoreCase (searchText))
            continue;

        filteredPlugins.add (description);
    }

    rebuildPluginRows();   // 8.162（Phase 200）。メーカーごとにまとめる

    pluginListBox.updateContent();
    pluginListBox.deselectAllRows();
    pluginListBox.repaint();

    if (currentTab == Tab::Plugins)
    {
        emptyLabel.setVisible (filteredPlugins.isEmpty());

        emptyLabel.setText (allPlugins.isEmpty()
                                ? utf8 ("環境設定でスキャンすると\nここに一覧が出ます。")
                                : utf8 ("条件に合うプラグインが\nありません。"),
                             juce::dontSendNotification);

        // 8.164：**出す／引っ込めるだけでは足りない**（Phase 202）。
        //
        // この案内の場所を決めているのは`resized()`だけです。
        // 絞り込みを変えて空になったとき、**見えてはいるが幅も高さも無い**ままなので、
        // 「1件も無い」と言うべき場面で**何も出ないパネル**になっていました
        // （Phase 202で種別をボタンにしたら、押すたびに空が出るので目に付いた）。
        //
        // **場所を決める人が別にいるなら、見え方を変えたら呼ぶこと。**
        resized();
    }
}

//==============================================================================
bool BrowserPanel::isFavourite (const juce::PluginDescription& description) const
{
    const auto saved = AppSettings::getString (favouritePluginsKey);

    if (saved.isEmpty())
        return false;

    juce::StringArray favourites;
    favourites.addTokens (saved, favouriteSeparator, {});

    return favourites.contains (description.createIdentifierString());
}

void BrowserPanel::toggleFavourite (const juce::PluginDescription& description)
{
    juce::StringArray favourites;
    favourites.addTokens (AppSettings::getString (favouritePluginsKey), favouriteSeparator, {});
    favourites.removeEmptyStrings();

    const auto identifier = description.createIdentifierString();

    if (favourites.contains (identifier))
        favourites.removeString (identifier);
    else
        favourites.add (identifier);

    AppSettings::setString (favouritePluginsKey, favourites.joinIntoString (favouriteSeparator));

    applyPluginFilter(); // 「★のみ」表示中なら、その場で一覧から消える／現れる
}

void BrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // フォルダの走査が進むたびに通知が来る。ツリーは自分で描き直すので、
    // ここでは特にすることが無い（購読しておかないと通知先が無いだけ）。
}

void BrowserPanel::selectionChanged()
{
    if (fileTree == nullptr)
        return;

    const auto file = fileTree->getSelectedFile();

    // フォルダを掴んだ場合は空文字にしておく。ドラッグ自体は始まってしまうが、
    // 受け手側が「ファイルのドラッグではない」と判断して何もしない。
    fileTree->setDragAndDropDescription (file.existsAsFile()
                                            ? DragAndDropIds::makeFileDescription (file)
                                            : juce::String());
}

void BrowserPanel::fileClicked (const juce::File&, const juce::MouseEvent&)
{
}

void BrowserPanel::fileDoubleClicked (const juce::File& file)
{
    // 仕様書4.4：ここからトラックへ取り込む。フォルダのダブルクリックは
    // ツリーの開閉なので、ファイルのときだけ通知する。
    if (file.existsAsFile() && onAudioFileChosen != nullptr)
        onAudioFileChosen (file);
}

void BrowserPanel::browserRootChanged (const juce::File& newRoot)
{
    // 「Folder...」以外の経路（ツリー上での移動）でも、見ていた場所を覚えておく
    if (newRoot.isDirectory())
        AppSettings::setString (browserFolderKey, newRoot.getFullPathName());
}

//==============================================================================
// 8.162：メーカーごとのまとめ（Phase 200／本人の要望）
//==============================================================================

juce::String BrowserPanel::getManufacturerLabel (const juce::PluginDescription& description)
{
    const auto name = description.manufacturerName.trim();

    // **名乗っていないものも1つの束にする。** 束から漏らすと、
    // 「一覧には数えられているのに、どこにも出てこない」プラグインができます
    // （分類の「(分類なし)」と同じ考え方。8.68）
    return name.isNotEmpty() ? name : utf8 ("(メーカー不明)");
}

bool BrowserPanel::isManufacturerExpanded (const juce::String& manufacturer) const
{
    // 8.163：**探しているあいだは全部開く**（Phase 201）。
    // 検索の文字は「この名前のものを出して」という意味なので、
    // **畳んだまま見出しだけ並べると「無い」と読めてしまいます**（1.9）
    if (searchBox.getText().trim().isNotEmpty())
        return true;

    return expandedManufacturers.contains (manufacturer);
}

void BrowserPanel::rebuildPluginRows()
{
    // **並べ替えてから束ねる。** 束ねながら並べ替えると、
    // 同じメーカーが離れた場所に2つ出ます（スキャンの順に依るため）
    std::sort (filteredPlugins.begin(), filteredPlugins.end(),
                [] (const juce::PluginDescription& a, const juce::PluginDescription& b)
                {
                    const auto makerA = getManufacturerLabel (a);
                    const auto makerB = getManufacturerLabel (b);
                    const int byMaker = makerA.compareIgnoreCase (makerB);

                    if (byMaker != 0)
                        return byMaker < 0;

                    return a.name.compareIgnoreCase (b.name) < 0;
                });

    pluginRows.clear();

    juce::String currentMaker;
    size_t headerRow = 0;

    for (int i = 0; i < filteredPlugins.size(); ++i)
    {
        const auto maker = getManufacturerLabel (filteredPlugins.getReference (i));

        if (pluginRows.empty() || maker != currentMaker)
        {
            currentMaker = maker;
            headerRow = pluginRows.size();

            PluginRow header;
            header.manufacturer = maker;
            pluginRows.push_back (header);
        }

        // **数は畳んでいても出す**（「畳んだ中に何本あるか」が分からないと開く気になれない）
        ++pluginRows[headerRow].count;

        if (! isManufacturerExpanded (maker))
            continue;   // 8.163：既定は畳んだまま（Phase 201）

        PluginRow row;
        row.pluginIndex = i;
        pluginRows.push_back (row);
    }
}

const juce::PluginDescription* BrowserPanel::getPluginForRow (int row) const
{
    if (! juce::isPositiveAndBelow (row, (int) pluginRows.size()))
        return nullptr;

    const int index = pluginRows[(size_t) row].pluginIndex;

    if (! juce::isPositiveAndBelow (index, filteredPlugins.size()))
        return nullptr;

    return &filteredPlugins.getReference (index);
}

//==============================================================================
void BrowserPanel::PluginListModel::paintListBoxItem (int rowNumber, juce::Graphics& g,
                                                        int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, (int) owner.pluginRows.size()))
        return;

    const auto& row = owner.pluginRows[(size_t) rowNumber];

    // 8.162：**メーカーの見出し**（Phase 200）。押すと畳める
    if (row.isHeader())
    {
        const bool collapsed = ! owner.isManufacturerExpanded (row.manufacturer);

        g.setColour (AppColours::panel);
        g.fillRect (0, 0, width, height);
        g.setColour (AppColours::border);
        g.drawHorizontalLine (height - 1, 0.0f, (float) width);

        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 0);

        // **記号文字は使わない**（この環境のフォントに無いものがある。1.30）。
        // 三角は`Path`で描く——畳んでいるときは右向き、開いているときは下向き
        {
            auto markArea = area.removeFromLeft (14).toFloat().reduced (3.0f, 6.0f);
            juce::Path mark;

            if (collapsed)
                mark.addTriangle (markArea.getX(), markArea.getY(),
                                   markArea.getX(), markArea.getBottom(),
                                   markArea.getRight(), markArea.getCentreY());
            else
                mark.addTriangle (markArea.getX(), markArea.getY(),
                                   markArea.getRight(), markArea.getY(),
                                   markArea.getCentreX(), markArea.getBottom());

            g.setColour (AppColours::textSecondary);
            g.fillPath (mark);
        }

        g.setColour (AppColours::textSecondary);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (juce::String (row.count), area.removeFromRight (26),
                     juce::Justification::centredRight);

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (row.manufacturer, area, juce::Justification::centredLeft, true);
        return;
    }

    const auto* pluginPtr = owner.getPluginForRow (rowNumber);

    if (pluginPtr == nullptr)
        return;

    const auto& description = *pluginPtr;

    if (rowIsSelected)
    {
        g.setColour (AppColours::purple.withAlpha (0.25f));
        g.fillRect (0, 0, width, height);
    }

    // 8.162：**見出しのぶんだけ右へ寄せる**（Phase 200）。
    // 見出しと同じ位置から始めると、どこまでが1つの束か目で追えない
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (4, 0).withTrimmedLeft (10);

    // お気に入りの★は左端。ここをクリックすると切り替わる（listBoxItemClicked参照）
    auto starArea = area.removeFromLeft (18);

    g.setColour (owner.isFavourite (description) ? AppColours::orange : AppColours::border);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x98\x85"), starArea, juce::Justification::centredLeft);

    // 音源とエフェクトを色で見分けられるようにする（挿し先が違うため）
    g.setColour (description.isInstrument ? AppColours::purple : AppColours::textSecondary);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (description.isInstrument ? "INS" : "FX", area.removeFromRight (26),
                 juce::Justification::centredRight);

    g.setColour (AppColours::textPrimary);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (description.name, area, juce::Justification::centredLeft, true);
}

juce::var BrowserPanel::PluginListModel::getDragSourceDescription (const juce::SparseSet<int>& selectedRows)
{
    // 仕様書4.4：ドラッグでトラックへ挿す（Phase 21）。
    // 複数選択は扱わないので、先頭の行だけを運ぶ。
    if (selectedRows.isEmpty())
        return {};

    const int row = selectedRows[0];

    // 8.162：**見出しはドラッグさせない**（Phase 200）。空のvarを返せばドラッグは始まらない
    const auto* description = owner.getPluginForRow (row);

    if (description == nullptr)
        return {};

    return DragAndDropIds::makePluginDescription (*description);
}

void BrowserPanel::PluginListModel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) owner.pluginRows.size()))
        return;

    // 8.162：**見出しを押すと畳む／開く**（Phase 200）。
    // **押した場所ではなく行そのもの**で決める——三角だけを当たり判定にすると、
    // 6pxの図形を狙って押すことになる
    const auto& clickedRow = owner.pluginRows[(size_t) row];

    if (clickedRow.isHeader())
    {
        // 8.163：**探しているあいだは畳めません**（Phase 201）。
        // 見た目は開いたままなのに中で状態だけ変わる、を作らない
        // ——検索を消した瞬間に、押した覚えのないものが開く／閉じることになる
        if (owner.searchBox.getText().trim().isNotEmpty())
            return;

        if (owner.expandedManufacturers.contains (clickedRow.manufacturer))
            owner.expandedManufacturers.removeString (clickedRow.manufacturer);
        else
            owner.expandedManufacturers.add (clickedRow.manufacturer);

        // **並べ替えはやり直さない**（`rebuildPluginRows()`は並べ替えも含むが、
        // 同じ結果になるので害はない）。行の数が変わるので`updateContent()`は要る
        owner.rebuildPluginRows();
        owner.pluginListBox.updateContent();
        owner.pluginListBox.deselectAllRows();
        owner.pluginListBox.repaint();
        return;
    }

    const auto* description = owner.getPluginForRow (row);

    if (description == nullptr)
        return;

    // 行の左端（★の位置）をクリックしたときだけ、お気に入りを切り替える。
    // 行全体で切り替えると、選ぼうとしただけで状態が変わってしまう。
    // **見出しのぶん右へ寄せたので、境目も同じだけ動かすこと**（8.162）
    if (e.x < 32)
        owner.toggleFavourite (*description);
}

void BrowserPanel::PluginListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    // 見出しのダブルクリックは`listBoxItemClicked`が2回動くだけ（畳んで開く）なので、
    // ここでは何もしない
    const auto* description = owner.getPluginForRow (row);

    if (description == nullptr)
        return;

    if (e.x < 32)
        return; // ★のダブルクリックは「2回切り替え」になるだけなので無視する

    if (owner.onPluginChosen != nullptr)
        owner.onPluginChosen (*description);
}

//==============================================================================
void BrowserPanel::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    // Phase 28：ブラウザは画面の**右**側に移した。仕切り線とリサイザーの余白も左端に来る
    g.setColour (AppColours::border);
    g.drawLine ((float) PanelResizerBar::thickness, 0.0f,
                 (float) PanelResizerBar::thickness, (float) getHeight(), 1.0f);
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds();

    // 左端はリサイザー用に空けておく（帯そのものはMainComponentが置く）
    area.removeFromLeft (PanelResizerBar::thickness);
    area = area.reduced (6, 6);

    auto tabRow = area.removeFromTop (24);
    pluginsTabButton.setBounds (tabRow.removeFromLeft (tabRow.getWidth() / 2).reduced (1, 0));
    filesTabButton.setBounds (tabRow.reduced (1, 0));

    area.removeFromTop (6);

    if (currentTab == Tab::Plugins)
    {
        // 8.164：**★は検索の行の右端へ**（Phase 202）。
        // 種別のボタンと同じ行に置くと、パネルがいちばん狭いとき（160px）に
        // 「エフェクト」が入りません——**★は幅が要らない**ので、こちらへ寄せます
        {
            auto searchRow = area.removeFromTop (24);
            favouritesOnlyButton.setBounds (searchRow.removeFromRight (26));
            searchRow.removeFromRight (4);
            searchBox.setBounds (searchRow);
        }

        area.removeFromTop (4);

        // 8.164：種別のトグル3つ。**3等分**（Phase 202）。
        // 端数は最後のボタンが受けるので、右端がぴったり揃う
        {
            auto filterRow = area.removeFromTop (24);
            const int third = filterRow.getWidth() / 3;

            typeAllButton.setBounds (filterRow.removeFromLeft (third));
            typeInstrumentButton.setBounds (filterRow.removeFromLeft (third));
            typeEffectButton.setBounds (filterRow);
        }

        // 8.68：カテゴリは**行を分ける**（Phase 107／改善案⑧）。
        // パネルは狭いので（既定240px・最小160px）、3つ横に並べると
        // どれも中身が読めない幅になる。分類が1つも無いときは行ごと出さない
        if (categoryFilterBox.isVisible())
        {
            area.removeFromTop (4);
            categoryFilterBox.setBounds (area.removeFromTop (24));
        }

        area.removeFromTop (6);

        // 一覧が空のときだけ案内を出し、その下に（残りがあれば）リストを置く
        if (emptyLabel.isVisible())
            emptyLabel.setBounds (area.removeFromTop (40));

        pluginListBox.setBounds (area);
    }
    else
    {
        chooseFolderButton.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);

        if (fileTree != nullptr)
            fileTree->setBounds (area);
    }
}

//==============================================================================
// 8.133：フォルダとファイルの絵（Phase 169／改善案リスト3の44）

void BrowserPanel::FileRowLookAndFeel::drawFileBrowserRow (juce::Graphics& g, int width, int height,
                                                            const juce::File&,
                                                            const juce::String& filename,
                                                            juce::Image*,
                                                            const juce::String& fileSizeDescription,
                                                            const juce::String& fileTimeDescription,
                                                            bool isDirectory, bool isItemSelected,
                                                            int, juce::DirectoryContentsDisplayComponent& dcc)
{
    // **JUCEが渡してくる`Image* icon`は使いません**（OSのアイコン）。
    // もらった絵に差し替えるのがこの回の目的です
    auto* component = dynamic_cast<juce::Component*> (&dcc);

    if (isItemSelected)
    {
        g.fillAll (component != nullptr
                     ? component->findColour (juce::DirectoryContentsDisplayComponent::highlightColourId)
                     : AppColours::purple);
    }

    auto area = juce::Rectangle<int> (0, 0, width, height);
    auto iconArea = area.removeFromLeft (height).reduced (3);

    // **色は塗り替えません**（フォルダ＝紫／ファイル＝オレンジが見分けの手がかり）
    if (auto* drawable = isDirectory ? folderIcon.get() : fileIcon.get())
        drawable->drawWithin (g, iconArea.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    g.setColour (component != nullptr
                   ? component->findColour (juce::DirectoryContentsDisplayComponent::textColourId)
                   : AppColours::textPrimary);
    g.setFont (juce::FontOptions ((float) height * 0.7f));

    // 大きさと日付は、幅に余裕があるときだけ（狭いパネルでは名前を優先する）
    if (width > 450 && ! isDirectory)
    {
        auto sizeArea = area.removeFromRight (80);
        auto timeArea = area.removeFromRight (140);

        g.setFont (juce::FontOptions ((float) height * 0.55f));
        g.drawFittedText (fileSizeDescription, sizeArea, juce::Justification::centredRight, 1);
        g.drawFittedText (fileTimeDescription, timeArea, juce::Justification::centredRight, 1);
        g.setFont (juce::FontOptions ((float) height * 0.7f));
    }

    g.drawFittedText (filename, area.reduced (4, 0), juce::Justification::centredLeft, 1);
}
