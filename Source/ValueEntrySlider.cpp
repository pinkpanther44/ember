#include "ValueEntrySlider.h"
#include "AppColours.h"
#include "Utf8.h"

ValueEntrySlider::~ValueEntrySlider()
{
    stopWatchingForClicksOutside();

    // 8.62：**壊れる前に外すこと**（Phase 100）。付けたままにすると、
    // 破棄の途中でLookAndFeelを引きに行って落ちることがある
    setLookAndFeel (nullptr);
}

//==============================================================================
// 8.60：見せ方だけを別の単位にする（Phase 97／改善案⑥）

void ValueEntrySlider::setDisplayUnit (DisplayUnit unit)
{
    if (displayUnit == unit)
        return;

    displayUnit = unit;
    updateText();   // 既に出ているテキストボックスを新しい単位で書き直す
}

juce::String ValueEntrySlider::getTextFromValue (double value)
{
    // **どちらも100倍して整数で出す。** 小数を出しても手では合わせられないうえ、
    // 「-0.35」より「-35」のほうが左右の振れ幅として読みやすい
    if (displayUnit == DisplayUnit::panPercent || displayUnit == DisplayUnit::percent)
        return juce::String (juce::roundToInt (value * 100.0));

    return juce::Slider::getTextFromValue (value);
}

double ValueEntrySlider::getValueFromText (const juce::String& text)
{
    if (displayUnit == DisplayUnit::panPercent || displayUnit == DisplayUnit::percent)
    {
        // **出す側と対にしておくこと。** ここを直し忘れると、
        // 打ち込んだ「50」が0.5ではなく50として入り、範囲外として端に張り付く
        return text.retainCharacters ("0123456789.-").getDoubleValue() / 100.0;
    }

    return juce::Slider::getValueFromText (text);
}

void ValueEntrySlider::mouseDown (const juce::MouseEvent& e)
{
    // 入力欄を開いている間だけ、ウィンドウ全体のクリックがここへ回ってくる（8.22）。
    // **自分以外から来たものは「外側をクリックした」の合図**として扱う
    if (e.eventComponent != this)
    {
        if (valueEntry != nullptr
             && e.eventComponent != valueEntry.get()
             && ! valueEntry->isParentOf (e.eventComponent))
            commitValueEntry();

        return;
    }

    // **右クリックは基底へ渡さない。** `juce::Slider`は右ボタンでもドラッグとして扱い、
    // `setSliderSnapsToMousePosition()`が既定でtrueなので、押した位置へ値が飛ぶ
    if (e.mods.isPopupMenu())
    {
        showValueMenu();
        return;
    }

    juce::Slider::mouseDown (e);
}

void ValueEntrySlider::showValueEntry()
{
    if (valueEntry != nullptr)
        return;   // 既に開いている

    // **テキストボックス付きのスライダーでは、そちらを使う。**
    // 自前の入力欄を重ねると、すぐ隣に同じものが2つ並ぶことになる
    if (getTextBoxPosition() != juce::Slider::NoTextBox)
    {
        showTextBox();
        return;
    }

    valueEntry = std::make_unique<juce::TextEditor>();

    // つまみの上に重ねるので、**地を塗ること**（半透明だと下の目盛りが透けて読めない）
    valueEntry->setColour (juce::TextEditor::backgroundColourId, AppColours::background);
    valueEntry->setColour (juce::TextEditor::textColourId, AppColours::textPrimary);
    valueEntry->setColour (juce::TextEditor::outlineColourId, AppColours::purple);
    valueEntry->setColour (juce::TextEditor::focusedOutlineColourId, AppColours::purple);
    valueEntry->setColour (juce::TextEditor::highlightColourId, AppColours::purple.withAlpha (0.35f));

    valueEntry->setFont (juce::FontOptions (12.0f));
    valueEntry->setJustification (juce::Justification::centred);
    valueEntry->setSelectAllWhenFocused (true);
    valueEntry->setMultiLine (false);
    valueEntry->setReturnKeyStartsNewLine (false);

    // 末尾の0を落として「0」「-6.5」のように出す（打ち直すときに邪魔になるため）。
    // 8.60：**別の単位で見せているときは、その単位で開く**（Phase 97／改善案⑥）。
    // 画面に「-35」と出ているのに入力欄が「-0.35」で開いたら、打ち直す前に混乱する
    valueEntry->setText (displayUnit == DisplayUnit::plain
                             ? juce::String (getValue(), entryDecimals)
                                   .trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                             : getTextFromValue (getValue()),
                          juce::dontSendNotification);

    // 縦長のフェーダーでは、全面に重ねると入力欄が細長くなって読めない。
    // **高さは文字が入るぶんだけ**にして、縦中央へ置く
    const int height = juce::jmin (getHeight(), 20);
    valueEntry->setBounds (getLocalBounds().withHeight (height)
                                            .withY ((getHeight() - height) / 2));

    valueEntry->onReturnKey  = [this] { commitValueEntry(); };
    valueEntry->onEscapeKey  = [this] { dismissValueEntry(); };
    valueEntry->onFocusLost  = [this] { commitValueEntry(); };

    addAndMakeVisible (*valueEntry);
    valueEntry->grabKeyboardFocus();

    startWatchingForClicksOutside();
}

void ValueEntrySlider::showValueMenu()
{
    juce::PopupMenu menu;

    menu.addItem (1, utf8 ("数値を入力..."));

    // **「初期値に戻す」は、戻る先を書いて出す。** 「初期値」とだけ書かれても、
    // それが0dBなのか中央なのかは触ってみないと分からない
    if (isDoubleClickReturnEnabled())
    {
        menu.addSeparator();
        menu.addItem (2, defaultValueDescription.isNotEmpty()
                             ? utf8 ("初期値に戻す（") + defaultValueDescription + utf8 ("）")
                             : utf8 ("初期値に戻す"));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [safe = juce::Component::SafePointer<ValueEntrySlider> (this)] (int result)
        {
            // メニューを開いている間にストリップが作り直されている可能性がある
            // （トラックの増減で`ChannelStripComponent`ごと入れ替わる）
            if (safe == nullptr)
                return;

            if (result == 1)
                safe->showValueEntry();
            else if (result == 2)
                safe->resetToDefaultValue();
        });
}

void ValueEntrySlider::resetToDefaultValue()
{
    if (! isDoubleClickReturnEnabled())
        return;

    // **区切りとTouch/Latchはコールバックに書いてある**ので、そこを通す（8.2）
    if (onDragStart != nullptr)
        onDragStart();

    setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);

    if (onDragEnd != nullptr)
        onDragEnd();
}

void ValueEntrySlider::applyTextValue (const juce::String& text)
{
    // 読み取れない文字列は無視する。**0として扱わないこと**：
    // 空欄のままEnterを押しただけで、フェーダーが0dBへ飛ぶことになる
    // （dB表示のラベルには"-∞ dB"が出るので、これも数字なしとして落ちる）
    if (! text.containsAnyOf ("0123456789"))
        return;

    const double wanted = getValueFromText (text.trim());
    const auto range = getRange();
    const double limited = juce::jlimit (range.getStart(), range.getEnd(), wanted);

    // **区切りとTouch/Latchの記録開始は、どのスライダーもコールバックに書いてある。**
    // ここで同じことを書き写すと、つまみで動かしたときと結果がずれる（HANDOVER 8.2）
    if (onDragStart != nullptr)
        onDragStart();

    setValue (limited, juce::sendNotificationSync);

    if (onDragEnd != nullptr)
        onDragEnd();
}

void ValueEntrySlider::commitValueEntry()
{
    if (valueEntry == nullptr)
        return;

    applyTextValue (valueEntry->getText());
    dismissValueEntry();
}

void ValueEntrySlider::dismissValueEntry()
{
    if (valueEntry == nullptr)
        return;

    stopWatchingForClicksOutside();

    // **コールバックを先に外すこと。** 下の`setVisible(false)`でフォーカスが外れ、
    // `onFocusLost`がもう一度飛んでくる（＝同じ値を二重に確定してしまう）
    valueEntry->onReturnKey = nullptr;
    valueEntry->onEscapeKey = nullptr;
    valueEntry->onFocusLost = nullptr;

    // **コールバックの中から直接deleteしない**（`onFocusLost`はTextEditor自身の
    // 処理中に呼ばれる）。いったんメッセージスレッドへ回してから捨てる
    valueEntry->setVisible (false);

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ValueEntrySlider> (this)]
    {
        if (safe != nullptr)
            safe->valueEntry.reset();
    });
}

void ValueEntrySlider::startWatchingForClicksOutside()
{
    stopWatchingForClicksOutside();

    // 第2引数true＝**子孫のクリックも回してもらう**。これが無いと、
    // トップレベル自身を直接クリックしたときしか気づけない
    if (auto* top = getTopLevelComponent())
    {
        watchedTopLevel = top;
        top->addMouseListener (this, true);
    }
}

void ValueEntrySlider::stopWatchingForClicksOutside()
{
    // **見張りは必ず外すこと。** 外し忘れると、破棄済みのスライダーへ
    // クリックが回り続ける
    if (watchedTopLevel != nullptr)
    {
        watchedTopLevel->removeMouseListener (this);
        watchedTopLevel = nullptr;
    }
}
