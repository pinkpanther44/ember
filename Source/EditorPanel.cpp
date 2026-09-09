#include "EditorPanel.h"
#include "AppColours.h"
#include "Utf8.h"

//==============================================================================
EditorPanel::EditorPanel()
{
    // 帯はこのパネルの子なので、ドラッグ開始時の大きさは親（＝自分）の高さでよい。
    // 左右パネルでは帯が兄弟なので、あちらは別の値を返している（PanelResizerBar参照）。
    resizer.getCurrentSize = [this] { return getHeight(); };
    resizer.onSizeDragged = [this] (int newHeight)
    {
        if (onHeightChangeRequested != nullptr)
            onHeightChangeRequested (newHeight);
    };
    addAndMakeVisible (resizer);

    titleLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    titleLabel.setText ("Piano Roll", juce::dontSendNotification);
    addAndMakeVisible (titleLabel);

    popOutButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    popOutButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    popOutButton.onClick = [this]
    {
        if (onPopOutClicked != nullptr)
            onPopOutClicked();
    };
    addAndMakeVisible (popOutButton);

    closeButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    closeButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
    closeButton.onClick = [this]
    {
        if (onCloseClicked != nullptr)
            onCloseClicked();
    };
    addAndMakeVisible (closeButton);
}

void EditorPanel::setContent (juce::Component* newContent)
{
    if (content == newContent)
        return;

    // 前の中身は「外す」だけ。所有権を持っていないので破棄はしない
    if (content != nullptr)
        removeChildComponent (content);

    content = newContent;

    if (content != nullptr)
    {
        addAndMakeVisible (content);
        content->setVisible (isVisible());
    }

    resized();
}

void EditorPanel::setTitle (const juce::String& newTitle)
{
    titleLabel.setText (newTitle, juce::dontSendNotification);
}

void EditorPanel::visibilityChanged()
{
    // JUCEのvisibilityChanged()は**自分にしか飛ばない**（子には伝播しない）。
    // 中身のビューはisVisible()を見て自前の更新（トラック一覧の引き直しなど）を
    // 行うため、ここで明示的に合わせておかないと、パネルを開いても中身が
    // 古い内容のままになる。
    if (content != nullptr)
        content->setVisible (isVisible());
}

void EditorPanel::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    // ヘッダー部分だけ、他のパネルと同じ色で塗り分ける
    g.setColour (AppColours::panel);
    g.fillRect (0, resizerHeight, getWidth(), headerHeight);

    g.setColour (AppColours::border);
    g.drawLine (0.0f, (float) resizerHeight, (float) getWidth(), (float) resizerHeight, 1.0f);
    g.drawLine (0.0f, (float) (resizerHeight + headerHeight),
                 (float) getWidth(), (float) (resizerHeight + headerHeight), 1.0f);
}

void EditorPanel::resized()
{
    auto area = getLocalBounds();

    resizer.setBounds (area.removeFromTop (resizerHeight));

    auto header = area.removeFromTop (headerHeight).reduced (8, 3);
    closeButton.setBounds (header.removeFromRight (26));
    header.removeFromRight (6);
    popOutButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (8);
    titleLabel.setBounds (header);

    if (content != nullptr)
        content->setBounds (area);
}
