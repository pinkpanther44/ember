#include "SnapGridSelector.h"
#include "AppColours.h"
#include "Utf8.h"

//==============================================================================
SnapGridSelector::SnapGridSelector()
{
    caption.setText ("Snap", juce::dontSendNotification);
    caption.setFont (juce::FontOptions (11.0f));
    caption.setJustificationType (juce::Justification::centredRight);
    caption.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (caption);

    // 配色は他のコンボボックスと揃える（直にjuce::Coloursを書かない。1.34）
    box.setColour (juce::ComboBox::backgroundColourId, AppColours::background);
    box.setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
    box.setColour (juce::ComboBox::outlineColourId, AppColours::border);
    box.setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);

    // **項目のIDは`SnapGrid`の値＋1。** ComboBoxはID=0を「選択なし」に使うので、
    // enumをそのまま入れると先頭の`off`（フリー）が選べない
    for (int i = 0; i <= (int) SnapGrid::thirtySecond; ++i)
        box.addItem (snapGridDisplayName ((SnapGrid) i), i + 1);

    box.setSelectedId ((int) SnapGrid::quarter + 1, juce::dontSendNotification);
    box.setTooltip (utf8 ("スナップ：クリップ・ノート・コード区間・ループ・マーカー・"
                           "再生カーソルを、この刻みへ寄せます\n"
                           "（クオンタイズのグリッドとは別の設定です）"));

    box.onChange = [this]
    {
        if (isUpdatingFromModel || onSnapGridChanged == nullptr)
            return;

        onSnapGridChanged ((SnapGrid) (box.getSelectedId() - 1));
    };

    addAndMakeVisible (box);
}

void SnapGridSelector::resized()
{
    auto area = getLocalBounds();

    // 幅が足りないときは見出しから削る（本体が読めなくなるほうが困る）
    caption.setBounds (area.removeFromLeft (juce::jmin (34, area.getWidth() / 3)));
    area.removeFromLeft (juce::jmin (4, area.getWidth()));
    box.setBounds (area);
}

void SnapGridSelector::setSnapGrid (SnapGrid grid)
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    box.setSelectedId ((int) grid + 1, juce::dontSendNotification);
}
