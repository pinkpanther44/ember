#include "PluginScanView.h"
#include "AppColours.h"
#include "Utf8.h"

void PluginScanView::PluginListModel::paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowIsSelected)
        g.fillAll (AppColours::purple.withAlpha (0.3f));

    if (! juce::isPositiveAndBelow (rowNumber, owner.foundPlugins.size()))
        return;

    const auto& description = owner.foundPlugins.getReference (rowNumber);
    const auto identifier = description.createIdentifierString();
    const int crashCount = owner.engine.getCrashTracker().getCrashCount (identifier);

    juce::String text = description.name;

    // 設計書3.5：クラッシュ履歴のあるプラグインは、その旨をリスト上で明示する
    if (crashCount > 0)
    {
        text += "   [crashes: " + juce::String (crashCount) + "]";
        g.setColour (owner.engine.getCrashTracker().shouldSandbox (identifier)
                         ? juce::Colours::red
                         : AppColours::orange);
    }
    else
    {
        g.setColour (AppColours::textPrimary);
    }

    g.drawText (text, 4, 0, width - 8, height, juce::Justification::centredLeft);
}

//==============================================================================
PluginScanView::PluginScanView (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    scanButton.onClick = [this] { scanClicked(); };
    addAndMakeVisible (scanButton);

    chooseFolderButton.onClick = [this] { chooseFolderClicked(); };
    addAndMakeVisible (chooseFolderButton);

    resetCrashButton.onClick = [this] { resetCrashClicked(); };
    addAndMakeVisible (resetCrashButton);

    // 8.157：飛ばしているものをもう一度試す（Phase 195/E2）
    clearBlacklistButton.onClick = [this]
    {
        engine.getPluginManager().clearBlacklist();
        updateStatus();
    };
    addAndMakeVisible (clearBlacklistButton);

    pluginListBox.setModel (&listModel);
    pluginListBox.setColour (juce::ListBox::backgroundColourId, AppColours::canvas);
    addAndMakeVisible (pluginListBox);

    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (statusLabel);

    // 8.157：**覚えてある一覧をそのまま出す**（Phase 195/E2）。
    // Phase 194まではここが空で、「Scan」を押すまで何も出ませんでした
    foundPlugins = engine.getPluginManager().getKnownPlugins();
    pluginListBox.updateContent();

    updateStatus();
}

void PluginScanView::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void PluginScanView::resized()
{
    auto area = getLocalBounds().reduced (16);

    auto buttonRow = area.removeFromTop (30);
    scanButton.setBounds (buttonRow.removeFromLeft (200));
    buttonRow.removeFromLeft (8);
    chooseFolderButton.setBounds (buttonRow.removeFromLeft (110));

    area.removeFromTop (10);
    pluginListBox.setBounds (area.removeFromTop (160));

    area.removeFromTop (10);
    auto bottomRow = area.removeFromTop (30);
    resetCrashButton.setBounds (bottomRow.removeFromLeft (160));
    bottomRow.removeFromLeft (8);
    clearBlacklistButton.setBounds (bottomRow.removeFromLeft (170));   // 8.157（Phase 195）

    area.removeFromTop (10);
    statusLabel.setBounds (area);
}

void PluginScanView::scanClicked()
{
    // 8.52：**スキャンは別スレッドで**（Phase 91）。
    //
    // Phase 90まではここで同期的に走らせていたため、**プラグインの数だけ
    // アプリ全体が固まって**いました（「スキャンすると一時的におかしくなる」）。
    //
    // **一覧を書き換えるのは終わってから、メッセージスレッドで**行います
    // （ブラウザパネルも同じ一覧を見ているので、走っている最中に書き換えると衝突する）。
    if (isScanning)
        return;

    isScanning = true;
    scanButton.setEnabled (false);
    chooseFolderButton.setEnabled (false);
    statusLabel.setText (utf8 ("スキャン中... （終わるまで一覧は変わりません）"),
                          juce::dontSendNotification);

    juce::Component::SafePointer<PluginScanView> safeThis (this);
    const auto folders = extraFolders;
    auto* manager = &engine.getPluginManager();

    // 8.157：**前回落ちたものを、始める前に弾く**（Phase 195／E2）。
    // ここはメッセージスレッド。返ってきた名前は下のステータスに出す
    const auto skipped = manager->blacklistWhatCrashedLastTime();

    if (! skipped.isEmpty())
        statusLabel.setText (utf8 ("スキャン中... （前回落ちた ")
                                 + juce::String (skipped.size())
                                 + utf8 (" 個は飛ばします）"),
                              juce::dontSendNotification);

    // 8.157：**分かっているものを渡す**（Phase 195）。**メッセージスレッドで取ること**
    // ——別スレッドから`knownPlugins`を読むと、画面側の読み出しと衝突します
    const auto alreadyKnown = manager->getKnownPlugins();

    scanPool.addJob ([safeThis, folders, manager, alreadyKnown]
    {
        const auto found = manager->scanFoldersWithoutApplying (folders, alreadyKnown);

        juce::MessageManager::callAsync ([safeThis, found]
        {
            // **戻ってきたときに画面が生きているとは限らない**（閉じられている）
            if (auto* view = safeThis.getComponent())
                view->finishScan (found);
        });
    });
}

void PluginScanView::finishScan (const juce::Array<juce::PluginDescription>& found)
{
    engine.getPluginManager().mergeScannedPlugins (found);

    foundPlugins = engine.getPluginManager().getKnownPlugins();
    pluginListBox.updateContent();

    isScanning = false;
    scanButton.setEnabled (true);
    chooseFolderButton.setEnabled (true);

    updateStatus();

    // 設計書2.3.6：ブラウザパネル（Phase 17）も同じ一覧を出しているので、
    // 開いたままスキャンしても中身が古いままにならないよう知らせる。
    if (onPluginsScanned != nullptr)
        onPluginsScanned();
}

void PluginScanView::chooseFolderClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("追加でスキャンするフォルダを選択"));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();

            if (folder != juce::File())
            {
                extraFolders.add (folder);
                statusLabel.setText (utf8 ("フォルダを追加しました: ") + folder.getFullPathName()
                                          + juce::newLine + utf8 ("「Scan」を押して再スキャンしてください。"),
                                      juce::dontSendNotification);
            }
        });
}

void PluginScanView::resetCrashClicked()
{
    const auto selectedRow = pluginListBox.getSelectedRow();

    if (! juce::isPositiveAndBelow (selectedRow, foundPlugins.size()))
    {
        statusLabel.setText (utf8 ("リストからプラグインを選択してください。"), juce::dontSendNotification);
        return;
    }

    const auto identifier = foundPlugins.getReference (selectedRow).createIdentifierString();
    engine.getCrashTracker().resetCrashCount (identifier);

    pluginListBox.updateContent();
    statusLabel.setText (utf8 ("クラッシュ履歴をリセットしました。"), juce::dontSendNotification);
}

void PluginScanView::updateStatus()
{
    juce::String text;
    text << utf8 ("見つかったプラグイン: ") << foundPlugins.size() << juce::newLine;

    // 8.157：飛ばしているものがあれば見せる（Phase 195/E2）。
    // **黙って減っていると、入れたはずのものが出ない理由が分かりません**
    if (const auto skipped = engine.getPluginManager().getBlacklistedPlugins(); ! skipped.isEmpty())
        text << utf8 ("スキャン中に落ちたため飛ばしているもの: ") << skipped.size()
             << utf8 ("（「Retry Skipped Plugins」でもう一度試せます）") << juce::newLine;

    text << utf8 ("プラグインの挿入は、Consoleタブのチャンネルストリップか、"
                   "ブラウザパネルからのドラッグ&ドロップで行います。");

    statusLabel.setText (text, juce::dontSendNotification);
}

void PluginScanView::refreshAfterProjectChanged()
{
    updateStatus();
}
