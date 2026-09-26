#include "ProjectChooser.h"
#include "AppMessageBox.h"   // 8.322：メッセージボックスは必ずここを通す（Phase 312）
#include "AppColours.h"
#include "AppIcon.h"    // 8.234：窓のアイコン（Phase 251）
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

    //--------------------------------------------------------------------------
    // 8.254：読み込み中の表示（Phase 262／本人の要望）。
    //
    // **作るだけ作って、隠しておきます**——`showLoading()`まで出番はありません

    loadingLabel.setFont (juce::Font (juce::FontOptions (20.0f)));
    loadingLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    loadingLabel.setJustificationType (juce::Justification::centred);
    loadingLabel.setVisible (false);
    addChildComponent (loadingLabel);

    loadingDetail.setFont (juce::Font (juce::FontOptions (13.0f)));
    loadingDetail.setColour (juce::Label::textColourId, AppColours::textSecondary);
    loadingDetail.setJustificationType (juce::Justification::centred);
    loadingDetail.setVisible (false);
    addChildComponent (loadingDetail);

    // 8.254：**待つ理由を先に書いておく**（Phase 262）。
    //
    // 読み込みのあいだ、くるくるは**止まります**（ヘッダの`BusySpinner`）。
    // 止まった絵だけだと「固まった」に見えますが、**時間がかかると
    // 先に書いてあれば「働いている」に見えます。**
    //
    // 8.225で引いた線と同じ：**説明するしかないものは、画面に出す。**
    loadingHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    loadingHint.setColour (juce::Label::textColourId, AppColours::textSecondary.withAlpha (0.7f));
    loadingHint.setJustificationType (juce::Justification::centred);
    loadingHint.setText (utf8 ("プラグインを使っているプロジェクトは、その数だけ時間がかかります"),
                          juce::dontSendNotification);
    loadingHint.setVisible (false);
    addChildComponent (loadingHint);

    spinner.setVisible (false);
    addChildComponent (spinner);

    setSize (880, 540);
}

ProjectChooserComponent::~ProjectChooserComponent() = default;

//==============================================================================
// 8.254：くるくる（Phase 262）。**止まる理由はヘッダに書いてあります**

ProjectChooserComponent::BusySpinner::BusySpinner()
{
    // 押しても何も起きないものを、押せそうに見せない（8.161）
    setInterceptsMouseClicks (false, false);
}

void ProjectChooserComponent::BusySpinner::start()  { startTimerHz (30); }
void ProjectChooserComponent::BusySpinner::stop()   { stopTimer(); }

void ProjectChooserComponent::BusySpinner::timerCallback()
{
    phase += 0.035f;

    if (phase >= 1.0f)
        phase -= 1.0f;

    repaint();
}

void ProjectChooserComponent::BusySpinner::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (3.0f);

    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

    if (radius <= 2.0f)
        return;

    const auto centre = area.getCentre();
    const float thickness = juce::jmax (2.0f, radius * 0.22f);

    // 地の輪。**弧だけだと「何も無いところを回っている」ように見えます**
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                         0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);

    g.setColour (AppColours::border);
    g.strokePath (ring, juce::PathStrokeType (thickness));

    // 回る弧（3/4周ぶん）
    const float start = phase * juce::MathConstants<float>::twoPi;

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius - thickness * 0.5f, radius - thickness * 0.5f,
                        0.0f, start, start + juce::MathConstants<float>::pi * 1.5f, true);

    g.setColour (AppColours::purple);
    g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

//==============================================================================

void ProjectChooserComponent::showLoading (const juce::String& message, const juce::String& detail)
{
    loading = true;

    // **子を名前で並べて隠さないこと。** 後から部品を足したときに
    // 書き忘れて、読み込み中の画面に1つだけボタンが残ります（1.15と同じ性質）
    for (auto* child : getChildren())
        child->setVisible (child == &loadingLabel || child == &loadingDetail
                            || child == &loadingHint || child == &spinner);

    loadingLabel.setText (message, juce::dontSendNotification);
    loadingDetail.setText (detail, juce::dontSendNotification);

    spinner.start();

    resized();
    repaint();
}

void ProjectChooserComponent::setLoadingMessage (const juce::String& message)
{
    if (! loading)
        return;

    loadingLabel.setText (message, juce::dontSendNotification);
    loadingLabel.repaint();
}

void ProjectChooserComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void ProjectChooserComponent::resized()
{
    // 8.254：読み込み中は**真ん中に3つだけ**（Phase 262）
    if (loading)
    {
        auto middle = getLocalBounds().withSizeKeepingCentre (getWidth(), 150);

        spinner.setBounds (middle.removeFromTop (48).withSizeKeepingCentre (44, 44));
        middle.removeFromTop (18);
        loadingLabel.setBounds (middle.removeFromTop (28));
        middle.removeFromTop (6);
        loadingDetail.setBounds (middle.removeFromTop (20));
        middle.removeFromTop (10);
        loadingHint.setBounds (middle.removeFromTop (18));

        return;
    }

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

    AppMessageBox::showAsync (
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

    // 8.234：**この窓にもアイコンを渡します**（Phase 251）。
    // 起動していちばん先に出るのはここなので、**これが無いと最初の絵が歯車**です
    AppIcon::applyToWindow (*this);

    toFront (true);
}

void ProjectChooserWindow::showLoading (const juce::String& message, const juce::String& detail)
{
    // 8.254：**×も消します**（Phase 262）。読み込みの途中で閉じられると、
    // 本体が出てくる先が無くなります——押せないボタンを残さない（8.161）
    setTitleBarButtonsRequired (0, false);

    if (auto* content = dynamic_cast<ProjectChooserComponent*> (getContentComponent()))
        content->showLoading (message, detail);
}

void ProjectChooserWindow::setLoadingMessage (const juce::String& message)
{
    if (auto* content = dynamic_cast<ProjectChooserComponent*> (getContentComponent()))
        content->setLoadingMessage (message);
}

void ProjectChooserWindow::closeButtonPressed()
{
    // 1.5：**自分のコールバックの中で自分を破棄させない。**
    // 受け取る側（`Main.cpp`）はここでこのウィンドウを捨てるので、
    // 依頼だけ投げて、実際の破棄は向こうがこのスタックを抜けてから行う
    if (onChosen != nullptr)
        onChosen ({ ProjectChooser::Result::Type::quit, {}, {} });
}
