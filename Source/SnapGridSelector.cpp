#include "SnapGridSelector.h"
#include "AppColours.h"
#include "Utf8.h"

//==============================================================================
SnapGridSelector::SnapGridSelector()
{
    // 8.307：**見出しではなくボタン**（Phase 300／本人の指定）。
    //
    // **既定は入**（本人の指定）。切ると`SnapGrid::off`＝フリーになります
    snapButton.setTooltip (utf8 ("スナップの入切：切ると、どの刻みにも寄らなくなります\n"
                                  "（入れ直すと、隣で選んでいる音価へ戻ります）"));
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (true, juce::dontSendNotification);

    snapButton.onClick = [this]
    {
        updateSnapButton();
        sendCurrentGrid();
    };

    addAndMakeVisible (snapButton);

    // 配色は他のコンボボックスと揃える（直にjuce::Coloursを書かない。1.34）
    box.setColour (juce::ComboBox::backgroundColourId, AppColours::background);
    box.setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
    box.setColour (juce::ComboBox::outlineColourId, AppColours::border);
    box.setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);

    // **項目のIDは`SnapGrid`の値＋1。** ComboBoxはID=0を「選択なし」に使うので、
    // enumをそのまま入れると先頭の`off`（フリー）が選べない
    //
    // 8.307：**「フリー」は入れません**（Phase 300／本人の指定）。
    // 入切は左のボタンの仕事になったので、ここに残すと
    // **同じことをする入口が2つ**になります（どちらで切ったか分からなくなる）
    for (int i = (int) SnapGrid::bar; i <= (int) SnapGrid::thirtySecond; ++i)
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

    updateSnapButton();
}

void SnapGridSelector::sendCurrentGrid()
{
    if (isUpdatingFromModel || onSnapGridChanged == nullptr)
        return;

    // 8.307：**切っているあいだはフリー**（Phase 300／本人の指定）。
    //
    // **コンボの値はそのままにしておくこと。** ここで`off`へ書き替えてしまうと、
    // 入れ直したときに何へ戻ればよいか分からなくなります
    // ——「切る前に何を選んでいたか」を覚えているのは、コンボ自身です
    if (! snapButton.getToggleState())
    {
        onSnapGridChanged (SnapGrid::off);
        return;
    }

    onSnapGridChanged (snapGridWithTriplet ((SnapGrid) (box.getSelectedId() - 1),
                                             tripletButton.getToggleState()));
}

void SnapGridSelector::updateSnapButton()
{
    // ツールボタンと同じ見せ方（設計書2.6）。
    // **`ArrangeView::updateAutoScrollButton()`と同じ形にすること**
    const bool on = snapButton.getToggleState();

    snapButton.setColour (juce::TextButton::buttonColourId,
                           on ? AppColours::purple : AppColours::background);
    snapButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    snapButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    snapButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    // **切っているあいだは、刻みも触らせない。** 押せるのに何も起きないのは、
    // 理由が分からず困ります（「3」で決めたのと同じ扱い）
    box.setEnabled (on);

    // 「3」の入切は`updateTripletButton()`が決めます（同じことを2箇所で決めない）
    updateTripletButton();

    snapButton.repaint();
}

void SnapGridSelector::updateTripletButton()
{
    const auto base = (SnapGrid) (box.getSelectedId() - 1);

    // 8.307：**切っているあいだは押せません**（Phase 300）。
    // **押せるかどうかを決めるのはここ1箇所**にすること——
    // `updateSnapButton()`でも決めていると、呼ぶ順で答えが変わります
    const bool canUse = snapGridCanBeTriplet (base) && snapButton.getToggleState();

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

    // 8.307：**入切のボタンが左端**（Phase 300）。大きさはツールと同じで、
    // 幅が足りないときもここは削りません——**押せなくなるより、コンボが狭いほうがまし**
    snapButton.setBounds (area.removeFromLeft (juce::jmin (ToolbarLayout::toolButtonWidth,
                                                            area.getWidth())));
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

    // 8.307：**フリーなら、ボタンを切るだけ**（Phase 300）。
    //
    // **コンボは触りません。** モデルの`off`には「どの音価から切ったか」が
    // 入っていないので、ここで合わせに行くと**選んでいた音価を失います**
    // （保存して開き直したときに、前の音価が戻るのはこのおかげです）
    if (grid == SnapGrid::off)
    {
        snapButton.setToggleState (false, juce::dontSendNotification);
        updateSnapButton();
        return;
    }

    snapButton.setToggleState (true, juce::dontSendNotification);

    // 8.271：**合わさった値をここで2つへ分ける**（Phase 272）。
    // モデルは`eighthTriplet`のような1つの値で持っています（`SnapGrid.h`）
    box.setSelectedId ((int) snapGridWithoutTriplet (grid) + 1, juce::dontSendNotification);
    tripletButton.setToggleState (snapGridIsTriplet (grid), juce::dontSendNotification);

    updateSnapButton();   // 中で`updateTripletButton()`も呼ばれます
}
