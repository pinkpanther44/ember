#include "ProjectChooser.h"
#include "AppColours.h"
#include "ProjectModel.h"
#include "RecentProjects.h"
#include "StorageLocations.h"
#include "Utf8.h"

//==============================================================================
void ProjectChooserComponent::RowListModel::paintListBoxItem (int rowNumber, juce::Graphics& g,
                                                               int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow (rowNumber, (int) rows.size()))
        return;

    const auto& row = rows[(size_t) rowNumber];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 3);

    if (rowIsSelected)
    {
        g.setColour (AppColours::purple.withAlpha (0.30f));
        g.fillRoundedRectangle (area.toFloat(), AppColours::corner (4.0f));
    }

    area.reduce (8, 2);

    g.setColour (AppColours::textPrimary);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (row.title, area.removeFromTop (20), juce::Justification::centredLeft, true);

    g.setColour (AppColours::textSecondary);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (row.subtitle, area, juce::Justification::centredLeft, true);
}

void ProjectChooserComponent::RowListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (onRowDoubleClicked != nullptr)
        onRowDoubleClicked (row);
}

void ProjectChooserComponent::RowListModel::selectedRowsChanged (int)
{
    if (onSelectionChanged != nullptr)
        onSelectionChanged();
}

//==============================================================================
ProjectChooserComponent::ProjectChooserComponent()
{
    titleLabel.setText (JUCE_APPLICATION_NAME_STRING, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addAndMakeVisible (titleLabel);

    versionLabel.setText (utf8 ("バージョン ") + JUCE_APPLICATION_VERSION_STRING,
                           juce::dontSendNotification);
    versionLabel.setFont (juce::FontOptions (12.0f));
    versionLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (versionLabel);

    templateCaption.setText (utf8 ("テンプレートから始める"), juce::dontSendNotification);
    recentCaption.setText (utf8 ("最近開いたプロジェクト"), juce::dontSendNotification);

    for (auto* caption : { &templateCaption, &recentCaption })
    {
        caption->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        caption->setColour (juce::Label::textColourId, AppColours::textSecondary);
        addAndMakeVisible (*caption);
    }

    for (auto* list : { &templateList, &recentList })
    {
        list->setRowHeight (44);
        list->setColour (juce::ListBox::backgroundColourId, AppColours::canvas);
        list->setColour (juce::ListBox::outlineColourId, AppColours::border);
        list->setOutlineThickness (1);
        addAndMakeVisible (*list);
    }

    templateModel.onRowDoubleClicked = [this] (int row)
    {
        templateList.selectRow (row);
        startFromSelectedTemplate();
    };
    templateModel.onSelectionChanged = [this] { updateButtonStates(); };

    recentModel.onRowDoubleClicked = [this] (int row)
    {
        recentList.selectRow (row);
        openSelectedRecent();
    };
    recentModel.onSelectionChanged = [this] { updateButtonStates(); };

    startFromTemplateButton.setButtonText (utf8 ("このテンプレートで始める"));
    startFromTemplateButton.onClick = [this] { startFromSelectedTemplate(); };
    addAndMakeVisible (startFromTemplateButton);

    deleteTemplateButton.setButtonText (utf8 ("削除"));
    deleteTemplateButton.onClick = [this] { deleteSelectedTemplate(); };
    addAndMakeVisible (deleteTemplateButton);

    openRecentButton.setButtonText (utf8 ("開く"));
    openRecentButton.onClick = [this] { openSelectedRecent(); };
    addAndMakeVisible (openRecentButton);

    browseButton.setButtonText (utf8 ("ファイルを開く..."));
    browseButton.onClick = [this] { browseForProject(); };
    addAndMakeVisible (browseButton);

    quitButton.setButtonText (utf8 ("終了"));
    quitButton.onClick = [this] { report ({ ProjectChooser::Result::Type::quit, {}, {} }); };
    addAndMakeVisible (quitButton);

    emptyRecentLabel.setText (utf8 ("まだありません。テンプレートから始めてください。"),
                               juce::dontSendNotification);
    emptyRecentLabel.setFont (juce::FontOptions (12.0f));
    emptyRecentLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    emptyRecentLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (emptyRecentLabel);

    refreshTemplates();
    refreshRecent();

    // **既定の選択を入れておく。** 何も選ばれていないと、
    // 「始める」が押せない画面が最初に出ることになります
    if (! templateEntries.empty())
        templateList.selectRow (0);

    updateButtonStates();

    setSize (880, 540);
}

ProjectChooserComponent::~ProjectChooserComponent() = default;

void ProjectChooserComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void ProjectChooserComponent::resized()
{
    auto area = getLocalBounds().reduced (18);

    auto header = area.removeFromTop (36);
    versionLabel.setBounds (header.removeFromRight (160));
    titleLabel.setBounds (header);

    area.removeFromTop (10);

    auto footer = area.removeFromBottom (34);
    browseButton.setBounds (footer.removeFromLeft (150));
    quitButton.setBounds (footer.removeFromRight (100));

    area.removeFromBottom (12);

    auto left = area.removeFromLeft (area.getWidth() / 2 - 9);
    area.removeFromLeft (18);
    auto right = area;

    templateCaption.setBounds (left.removeFromTop (22));
    auto leftButtons = left.removeFromBottom (32);
    left.removeFromBottom (8);
    templateList.setBounds (left);

    deleteTemplateButton.setBounds (leftButtons.removeFromRight (80));
    leftButtons.removeFromRight (8);
    startFromTemplateButton.setBounds (leftButtons);

    recentCaption.setBounds (right.removeFromTop (22));
    auto rightButtons = right.removeFromBottom (32);
    right.removeFromBottom (8);
    recentList.setBounds (right);
    emptyRecentLabel.setBounds (right.reduced (10));

    openRecentButton.setBounds (rightButtons.removeFromLeft (120));
}

void ProjectChooserComponent::refreshTemplates()
{
    templateEntries = ProjectTemplates::getAll();
    templateModel.rows.clear();

    for (const auto& entry : templateEntries)
        templateModel.rows.push_back ({ entry.name, entry.description });

    templateList.updateContent();
    templateList.repaint();
}

void ProjectChooserComponent::refreshRecent()
{
    recentFiles = RecentProjects::getFiles();
    recentModel.rows.clear();

    for (const auto& file : recentFiles)
        recentModel.rows.push_back ({ file.getFileNameWithoutExtension(),
                                       file.getParentDirectory().getFullPathName() });

    recentList.updateContent();
    recentList.repaint();

    // 履歴が空のときは、一覧の上に案内を出す（枠だけの空箱にしない）
    emptyRecentLabel.setVisible (recentFiles.isEmpty());
}

void ProjectChooserComponent::updateButtonStates()
{
    const int templateRow = templateList.getSelectedRow();
    const bool hasTemplate = juce::isPositiveAndBelow (templateRow, (int) templateEntries.size());

    startFromTemplateButton.setEnabled (hasTemplate);

    // 組み込みは消せない（`ProjectTemplates::remove()`も断りますが、
    // **押せるのに何も起きないボタンを出さない**ほうが分かりやすい）
    deleteTemplateButton.setEnabled (hasTemplate
                                       && ! templateEntries[(size_t) templateRow].isBuiltIn());

    openRecentButton.setEnabled (juce::isPositiveAndBelow (recentList.getSelectedRow(),
                                                            recentFiles.size()));
}

void ProjectChooserComponent::startFromSelectedTemplate()
{
    const int row = templateList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, (int) templateEntries.size()))
        return;

    report ({ ProjectChooser::Result::Type::fromTemplate, templateEntries[(size_t) row], {} });
}

void ProjectChooserComponent::openSelectedRecent()
{
    const int row = recentList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, recentFiles.size()))
        return;

    report ({ ProjectChooser::Result::Type::openFile, {}, recentFiles[row] });
}

void ProjectChooserComponent::browseForProject()
{
    // 設計書2.3.8：最初に開くフォルダは環境設定から引く（8.17）
    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("プロジェクトを開く"),
                                                        StorageLocations::getFolder (StorageLocations::Kind::projects),
                                                        ProjectModel::getFileWildcard());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
                report ({ ProjectChooser::Result::Type::openFile, {}, file });
        });
}

void ProjectChooserComponent::deleteSelectedTemplate()
{
    const int row = templateList.getSelectedRow();

    if (! juce::isPositiveAndBelow (row, (int) templateEntries.size()))
        return;

    const auto entry = templateEntries[(size_t) row];

    if (entry.isBuiltIn())
        return;

    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle (utf8 ("テンプレートを削除しますか"))
            .withMessage (utf8 ("「") + entry.name + utf8 ("」を削除します。\n")
                              + entry.file.getFullPathName())
            .withButton (utf8 ("削除する"))
            .withButton (utf8 ("やめる")),
        [this, entry] (int buttonIndex)
        {
            if (buttonIndex != 0)
                return;

            ProjectTemplates::remove (entry);
            refreshTemplates();
            updateButtonStates();
        });
}

void ProjectChooserComponent::report (const ProjectChooser::Result& result)
{
    // ダブルクリックとボタンで同じ行が2回通ることがある。
    // **2度目は捨てる**——2回目のときには、もうこのウィンドウは畳まれている
    if (alreadyChosen)
        return;

    alreadyChosen = true;

    if (onChosen != nullptr)
        onChosen (result);
}

//==============================================================================
ProjectChooserWindow::ProjectChooserWindow()
    : DocumentWindow (utf8 ("プロジェクトを選ぶ"),
                       AppColours::background,
                       DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);

    auto* content = new ProjectChooserComponent();

    content->onChosen = [this] (const ProjectChooser::Result& result)
    {
        if (onChosen != nullptr)
            onChosen (result);
    };

    setContentOwned (content, true);
    setResizable (true, false);
    setResizeLimits (700, 440, 1600, 1200);

    centreWithSize (getWidth(), getHeight());
    setVisible (true);
    toFront (true);
}

void ProjectChooserWindow::closeButtonPressed()
{
    // 1.5：**自分のコールバックの中で自分を破棄させない。**
    // 受け取る側（`Main.cpp`）はここでこのウィンドウを捨てるので、
    // 依頼だけ投げて、実際の破棄は向こうがこのスタックを抜けてから行う
    if (onChosen != nullptr)
        onChosen ({ ProjectChooser::Result::Type::quit, {}, {} });
}
