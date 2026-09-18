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
        // 8.271：音価を選び直しても**3連のままにする**（Phase 272）。
        // 1/8の3連から1/16の3連へ移るのに、いちいち「3」を押し直さずに済みます。
        // 小節やフリーを選んだときは`snapGridWithTriplet()`がそのまま返します
        updateTripletButton();
        sendCurrentGrid();
    };

    addAndMakeVisible (box);

    //==========================================================================
    // 8.271：3連の切り替え（Phase 272／本人の要望）

    tripletButton.setTooltip (utf8 ("3連符：選んでいる音価を3等分した刻みへ寄せます\n"
                                     "（1/8なら、3つで1拍）"));
    tripletButton.setClickingTogglesState (true);

    tripletButton.onClick = [this]
    {
        updateTripletButton();
        sendCurrentGrid();
    };

    addAndMakeVisible (tripletButton);

    updateTripletButton();
}

void SnapGridSelector::sendCurrentGrid()
{
    if (isUpdatingFromModel || onSnapGridChanged == nullptr)
        return;

    onSnapGridChanged (snapGridWithTriplet ((SnapGrid) (box.getSelectedId() - 1),
                                             tripletButton.getToggleState()));
}

void SnapGridSelector::updateTripletButton()
{
    const auto base = (SnapGrid) (box.getSelectedId() - 1);
    const bool canUse = snapGridCanBeTriplet (base);

    // **小節とフリーでは押せない。** 押したままそこへ移ったときは戻しておく——
    // 見た目が点いているのに効かない、という食い違いを残さないため
    if (! canUse && tripletButton.getToggleState())
        tripletButton.setToggleState (false, juce::dontSendNotification);

    tripletButton.setEnabled (canUse);

    // 選択中のパープル（設計書2.6）。ツールのボタンと同じ見せ方で揃えてある
    const bool on = tripletButton.getToggleState();

    tripletButton.setColour (juce::TextButton::buttonColourId,
                              on ? AppColours::purple : AppColours::background);
    tripletButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    tripletButton.setColour (juce::TextButton::textColourOffId,
                              canUse ? AppColours::textPrimary : AppColours::textSecondary);
    tripletButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void SnapGridSelector::resized()
{
    auto area = getLocalBounds();

    // 幅が足りないときは見出しから削る（本体が読めなくなるほうが困る）
    caption.setBounds (area.removeFromLeft (juce::jmin (34, area.getWidth() / 3)));
    area.removeFromLeft (juce::jmin (4, area.getWidth()));

    // 8.271：「3」はコンボの右隣（Phase 272）。**先に右から取る**ので、
    // 幅が足りないときに削られるのはコンボのほうです——
    // どちらも読めなくなるより、押せる場所が残っているほうがよい
    tripletButton.setBounds (area.removeFromRight (juce::jmin (tripletButtonWidth,
                                                                area.getWidth())));
    area.removeFromRight (juce::jmin (tripletButtonGap, area.getWidth()));

    box.setBounds (area);
}

void SnapGridSelector::setSnapGrid (SnapGrid grid)
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    // 8.271：**合わさった値をここで2つへ分ける**（Phase 272）。
    // モデルは`eighthTriplet`のような1つの値で持っています（`SnapGrid.h`）
    box.setSelectedId ((int) snapGridWithoutTriplet (grid) + 1, juce::dontSendNotification);
    tripletButton.setToggleState (snapGridIsTriplet (grid), juce::dontSendNotification);

    updateTripletButton();
}
