#include "MantaPluginToolbar.h"

#include "MantaTheme.h"
#include "../AppSettings.h"
#include "../NameEntry.h"
#include "../Utf8.h"

//==============================================================================

const juce::Identifier MantaPluginToolbar::presetNameProperty { "presetName" };
const juce::Identifier MantaPluginToolbar::presetModifiedProperty { "presetModified" };

void MantaPluginToolbar::styleButton (juce::TextButton& button, const juce::String& text)
{
    button.setButtonText (text);
    button.setColour (juce::TextButton::buttonColourId, MantaTheme::panelBackground());
    button.setColour (juce::TextButton::buttonOnColourId, MantaTheme::accent());
    button.setColour (juce::TextButton::textColourOffId, MantaTheme::text());
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

//==============================================================================
// 8.318：名前を出すPresetsボタン（Phase 311）

void MantaPluginToolbar::PresetButton::paintButton (juce::Graphics& g,
                                                    bool shouldDrawButtonAsHighlighted,
                                                    bool shouldDrawButtonAsDown)
{
    if (! showsName)
    {
        juce::TextButton::paintButton (g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
        return;
    }

    auto& lookAndFeel = getLookAndFeel();

    // **地は隣のボタンと同じもの**を描く（Undo・A・Copyと並んで浮かないように）
    lookAndFeel.drawButtonBackground (g, *this,
                                      findColour (getToggleState() ? buttonOnColourId : buttonColourId),
                                      shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    const auto textColour = findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f);

    auto area = getLocalBounds().reduced (MantaPluginToolbar::presetNamePadding, 0);   // 8.324

    // ▾ ——**押せばメニューが開く**ことを、名前が出ているあいだも伝えるため。
    // 名前だけだと、ただの表示に見えます
    {
        const auto chevron = area.removeFromRight (MantaPluginToolbar::presetChevronWidth).toFloat()
                                 .withSizeKeepingCentre ((float) MantaPluginToolbar::presetChevronWidth, 5.0f);

        juce::Path path;
        path.startNewSubPath (chevron.getX(), chevron.getY());
        path.lineTo (chevron.getCentreX(), chevron.getBottom());
        path.lineTo (chevron.getRight(), chevron.getY());

        g.setColour (textColour.withMultipliedAlpha (0.7f));
        g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    area.removeFromRight (MantaPluginToolbar::presetChevronGap);

    const auto font = MantaPluginToolbar::getPresetNameFont (*this);   // 8.324：数える側と同じ字
    g.setFont (font);

    // **何も当てていないときは「Presets」**——今までと同じ言葉にしておくと、
    // 名前が出る前から同じボタンだと分かります
    const auto text = presetName.isEmpty() ? juce::String ("Presets") : presetName;

    // **「*」は名前のすぐ後ろに付けること。** 名前が長いときは名前のほうを
    // 「…」で詰めます——印のほうを削ると、触ったことが見えなくなります
    const int markWidth = modified ? MantaPluginToolbar::getPresetMarkWidth (font) : 0;   // 8.324：数える側と同じ
    const int nameWidth = juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, text)));

    auto nameArea = area.removeFromLeft (juce::jlimit (0, juce::jmax (0, area.getWidth() - markWidth), nameWidth));

    g.setColour (textColour);
    g.drawText (text, nameArea, juce::Justification::centredLeft, true);

    if (modified)
    {
        area.removeFromLeft (MantaPluginToolbar::presetMarkGap);
        g.drawText ("*", area.removeFromLeft (markWidth - MantaPluginToolbar::presetMarkGap),
                    juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// 8.321：前・次のプリセットへ送るボタン（Phase 312）

void MantaPluginToolbar::StepButton::paintButton (juce::Graphics& g,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    // **地は隣のボタンと同じもの**（名前の欄と並んで浮かないように）
    getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId),
                                           shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

    const auto area = getLocalBounds().toFloat().withSizeKeepingCentre (6.0f, 8.0f);

    juce::Path triangle;

    if (pointsLeft)
        triangle.addTriangle (area.getRight(), area.getY(), area.getRight(), area.getBottom(),
                              area.getX(), area.getCentreY());
    else
        triangle.addTriangle (area.getX(), area.getY(), area.getX(), area.getBottom(),
                              area.getRight(), area.getCentreY());

    g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 0.8f : 0.4f));
    g.fillPath (triangle);
}

//==============================================================================

MantaPluginToolbar::MantaPluginToolbar (juce::AudioProcessor& processorToUse,
                                         juce::AudioProcessorValueTreeState& stateToUse,
                                         juce::String presetFolderName)
    : processor (processorToUse),
      state (stateToUse),
      presetFolder (std::move (presetFolderName))
{
    struct { juce::TextButton* button; const char* text; } buttons[]
    {
        { &undoButton, "Undo" }, { &redoButton, "Redo" }, { &abButton, "A" },
        { &copyButton, "Copy" }, { &presetButton, "Presets" },
    };

    for (const auto& entry : buttons)
    {
        styleButton (*entry.button, entry.text);
        addAndMakeVisible (*entry.button);
    }

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    abButton.onClick = [this] { toggleAB(); };
    copyButton.onClick = [this] { copyToOtherSlot(); };
    presetButton.onClick = [this] { showPresetMenu(); };

    // 8.321：**前・次のプリセットへ送る**（Phase 312）。名前を出す窓でだけ見せます
    // （`setShowsCurrentPreset()`）。Kakapoでは出ません
    for (auto* step : { &previousPresetButton, &nextPresetButton })
    {
        styleButton (*step, {});
        addChildComponent (*step);
    }

    previousPresetButton.onClick = [this] { stepPreset (-1); };
    nextPresetButton.onClick = [this] { stepPreset (+1); };

    previousPresetButton.setTooltip (utf8 ("前のプリセット"));
    nextPresetButton.setTooltip (utf8 ("次のプリセット"));

    undoButton.setTooltip (utf8 ("ひとつ前の設定へ戻す"));
    abButton.setTooltip (utf8 ("2つの設定を切り替えて聴き比べる"));
    copyButton.setTooltip (utf8 ("いまの設定を、もう片方へ写す"));

    // 8.318：**保存されていた名前を先に読むこと**（Phase 311）。
    // A/Bの2つの箱は「開いた時点の音」から始まるので、名前もそれに揃えます
    currentPreset = readPresetFromState();

    lastSnapshot = captureParameterValues();
    slotA = captureSnapshot();
    slotB = slotA;

    refreshButtons();

    // 5回／秒。Undoの粒度（このファイルの冒頭）とボタンの追従を兼ねている
    startTimerHz (5);
}

MantaPluginToolbar::~MantaPluginToolbar()
{
    stopTimer();
}

void MantaPluginToolbar::resized()
{
    auto area = getLocalBounds().reduced (6, 4);

    undoButton.setBounds (area.removeFromLeft (undoRedoWidth));   // 8.323：58→48px（Phase 312）
    area.removeFromLeft (2);
    redoButton.setBounds (area.removeFromLeft (undoRedoWidth));
    area.removeFromLeft (12);
    abButton.setBounds (area.removeFromLeft (34));
    area.removeFromLeft (2);
    copyButton.setBounds (area.removeFromLeft (54));
    area.removeFromLeft (12);

    // 8.318：**名前を出すときは広げる**（Phase 311）。上限は使う側が決め、
    // 帯が狭ければ残りいっぱいまで。**74pxより狭くはしません**（今までの幅）。
    //
    // **右に部品を置いている窓のぶんは残すこと**（`reservedTrailing`）。
    // 間の12pxも一緒に取っておきます——取らないと、部品と欄がくっつきます
    const int room = area.getWidth() - (reservedTrailing > 0 ? reservedTrailing + 12 : 0);

    // 8.321：**◀と▶は、名前の欄を削らないときだけ出します**（Phase 312）。
    //
    // 最初は「◀▶のぶんを先に取り、名前は残りで」にしていて、
    // **Orangutanで`808 … *`、EQで`Guita… *`**になりました（名前の欄が124→80px、130→86px）。
    // 本人が頼んだのは**名前を一目で見えるようにすること**で、◀▶はこちらの提案です。
    // **頼まれたほうを削ってまで、提案したほうを入れない。**
    //
    // なので、**名前の欄が上限まで広がれる窓でだけ**出します。
    // 広い窓（Red Panda・Racco・Java Rhino・Comp・Delay・Reverb）では右の空きから出て、
    // 名前の幅は1pxも変わりません。Orangutan・EQでは出ません（メニューからは選べます）
    const int fullStepsWidth = 2 * (stepButtonWidth + stepButtonGap);
    //
    // 8.323：**名前の欄に`minimumNameWidthWithSteps`（110px）残るなら出す**（Phase 312）。
    // 本人の指定でUndo・Redoを細くし（58→40px）、狭い窓にも場所ができました。
    // それでも名前が110pxを切る窓では出しません——**名前が主役**なのは変わりません
    const bool showSteps = showsCurrentPreset
                            && room - fullStepsWidth >= juce::jmin (presetDisplayWidth, minimumNameWidthWithSteps);
    const int stepsWidth = showSteps ? fullStepsWidth : 0;

    previousPresetButton.setVisible (showSteps);
    nextPresetButton.setVisible (showSteps);

    const int presetWidth = showsCurrentPreset
                              ? juce::jmax (presetButtonWidth, juce::jmin (presetDisplayWidth, room - stepsWidth))
                              : presetButtonWidth;

    if (showSteps)
    {
        previousPresetButton.setBounds (area.removeFromLeft (stepButtonWidth));
        area.removeFromLeft (stepButtonGap);
    }

    presetButton.setBounds (area.removeFromLeft (presetWidth));

    if (showSteps)
    {
        area.removeFromLeft (stepButtonGap);
        nextPresetButton.setBounds (area.removeFromLeft (stepButtonWidth));
    }

    area.removeFromLeft (juce::jmin (12, area.getWidth()));

    trailingArea = area;
}

void MantaPluginToolbar::timerCallback()
{
    pushUndoSnapshotIfChanged();
    refreshPresetDisplay();
    refreshButtons();
}

void MantaPluginToolbar::refreshButtons()
{
    abButton.setButtonText (usingSlotB ? "B" : "A");
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
}

//==============================================================================

std::vector<float> MantaPluginToolbar::captureParameterValues() const
{
    std::vector<float> values;

    for (auto* parameter : processor.getParameters())
        values.push_back (parameter->getValue());

    return values;
}

void MantaPluginToolbar::applyParameterValues (const std::vector<float>& values)
{
    const auto& parameters = processor.getParameters();

    // **本数が違うときは何もしない。** 将来パラメータを足したあとで
    // 古い写しを当てると、番号がずれた値が入ります
    if ((int) values.size() != parameters.size())
        return;

    for (int i = 0; i < parameters.size(); ++i)
        parameters[i]->setValueNotifyingHost (values[(size_t) i]);

    if (onStateRestored != nullptr)
        onStateRestored();
}

MantaPluginToolbar::Snapshot MantaPluginToolbar::captureSnapshot() const
{
    return { captureParameterValues(), currentPreset };
}

void MantaPluginToolbar::restoreSnapshot (const Snapshot& snapshot)
{
    applyParameterValues (snapshot.values);

    // 8.318：**名前も一緒に戻すこと**（Phase 311）。
    // 値だけ戻すと、Undoで当てる前の音に戻ったのに**名前は当てたまま**になります
    setCurrentPreset (snapshot.preset);
}

void MantaPluginToolbar::pushUndoSnapshotIfChanged()
{
    // **掴んでいるあいだは積まない**（ドラッグの途中が刻まれると、
    // Undoを何十回も押すことになります）
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    auto current = captureParameterValues();

    if (current == lastSnapshot)
        return;

    // 積むのは「変わる前の値」と「そのときのプリセット」。
    // つまみを回してもプリセットは変わらないので、**いまの名前がそのまま**入ります
    undoStack.push_back ({ lastSnapshot, currentPreset });

    if (undoStack.size() > maxUndoSteps)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
    lastSnapshot = std::move (current);
}

void MantaPluginToolbar::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (captureSnapshot());
    restoreSnapshot (undoStack.back());
    undoStack.pop_back();

    lastSnapshot = captureParameterValues();
}

void MantaPluginToolbar::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (captureSnapshot());
    restoreSnapshot (redoStack.back());
    redoStack.pop_back();

    lastSnapshot = captureParameterValues();
}

void MantaPluginToolbar::toggleAB()
{
    // **いま鳴っているほうを、いまの箱へしまってから入れ替える**
    (usingSlotB ? slotB : slotA) = captureSnapshot();
    usingSlotB = ! usingSlotB;

    restoreSnapshot (usingSlotB ? slotB : slotA);

    lastSnapshot = captureParameterValues();
    refreshButtons();
}

void MantaPluginToolbar::copyToOtherSlot()
{
    const auto current = captureSnapshot();

    slotA = current;
    slotB = current;
}

//==============================================================================
// 8.318：いまのプリセット（Phase 311）

void MantaPluginToolbar::setShowsCurrentPreset (bool shouldShow, int preferredWidth, int reservedTrailingWidth)
{
    showsCurrentPreset = shouldShow;
    presetDisplayWidth = juce::jmax (presetButtonWidth, preferredWidth);
    reservedTrailing = juce::jmax (0, reservedTrailingWidth);

    presetButton.showsName = shouldShow;   // ◀▶を出すかは`resized()`が決めます（8.321）

    // 名前は**コンストラクタで読んであります**。ここで書き戻すのは、
    // 印（`presetModified`）を今の値に揃えるためです——
    // **見比べる前に書くこと**（`setCurrentPreset()`と同じ理由）
    writePresetToState (isCurrentPresetModified());
    refreshPresetDisplay();
    resized();
    presetButton.repaint();
}

bool MantaPluginToolbar::isCurrentPresetModified() const
{
    if (currentPreset.name.isEmpty())
        return false;

    if (currentPreset.reference.empty())
        return currentPreset.modifiedWithoutReference;

    return captureParameterValues() != currentPreset.reference;
}

MantaPluginToolbar::PresetIdentity MantaPluginToolbar::readPresetFromState() const
{
    PresetIdentity preset;
    preset.name = state.state.getProperty (presetNameProperty).toString();

    if (preset.name.isEmpty())
        return preset;

    const bool modified = (bool) state.state.getProperty (presetModifiedProperty, false);

    // **触っていなかったなら、いまの値がそのまま「当てた直後」です。**
    // 触ってあったなら、当てた直後の値はもう分かりません——
    // 次に何か当てるまで`*`を付けたままにします
    if (modified)
        preset.modifiedWithoutReference = true;
    else
        preset.reference = captureParameterValues();

    return preset;
}

void MantaPluginToolbar::setCurrentPreset (PresetIdentity preset)
{
    currentPreset = std::move (preset);

    // **状態へ先に書くこと。** `refreshPresetDisplay()`は「状態の名前と
    // 覚えている名前が違えば、外から入れ替えられた」と読みます。
    // ユーザープリセットを読んだ直後の状態には**ファイルの中の古い名前**が
    // 入っているので、書く前に見比べると、そちらを信じてしまいます
    writePresetToState (isCurrentPresetModified());
    refreshPresetDisplay();
}

void MantaPluginToolbar::writePresetToState (bool modified)
{
    // **点けていないプラグインの状態には書きません**（8.318）。
    // いま点けていないのはKakapoだけ（本人の判断で「要らない」）で、
    // **その保存内容を黙って変えない**ためです
    if (! showsCurrentPreset)
        return;

    auto tree = state.state;

    // **空なら属性ごと消すこと。** `presetName=""`を書くと、
    // 何も当てていないプラグインの保存内容に無意味な1行が増えます
    if (currentPreset.name.isEmpty())
    {
        tree.removeProperty (presetNameProperty, nullptr);
        tree.removeProperty (presetModifiedProperty, nullptr);
        return;
    }

    if (tree.getProperty (presetNameProperty).toString() != currentPreset.name)
        tree.setProperty (presetNameProperty, currentPreset.name, nullptr);

    if ((bool) tree.getProperty (presetModifiedProperty, false) != modified)
        tree.setProperty (presetModifiedProperty, modified, nullptr);
}

void MantaPluginToolbar::refreshPresetDisplay()
{
    if (! showsCurrentPreset)
        return;

    // **窓が開いているあいだに、外から状態を入れ替えられることがあります**
    // （ホストが`setStateInformation()`を呼んだとき）。そのときは
    // 保存されている名前のほうを信じます——こちらが覚えている名前は、もう古い
    const auto storedName = state.state.getProperty (presetNameProperty).toString();

    if (storedName != currentPreset.name)
        currentPreset = readPresetFromState();

    const bool modified = isCurrentPresetModified();

    writePresetToState (modified);

    if (presetButton.presetName == currentPreset.name && presetButton.modified == modified)
        return;

    presetButton.presetName = currentPreset.name;
    presetButton.modified = modified;

    if (currentPreset.name.isEmpty())
        presetButton.setTooltip (utf8 ("プリセットを選ぶ・保存する"));
    else
        presetButton.setTooltip (currentPreset.name
                                  + (modified ? utf8 ("（当てたあとで変えています）") : juce::String())
                                  + utf8 ("　クリックでプリセットを選び直せます"));

    presetButton.repaint();
}

//==============================================================================

juce::File MantaPluginToolbar::getPresetFolder() const
{
    auto folder = AppSettings::getDataFolder().getChildFile ("Presets").getChildFile (presetFolder);

    folder.createDirectory();

    return folder;
}

void MantaPluginToolbar::setFactoryPresets (std::vector<FactoryPreset> presets)
{
    factoryPresets = std::move (presets);
}

void MantaPluginToolbar::applyFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factoryPresets.size()))
        return;

    const auto& preset = factoryPresets[(size_t) index];

    if (preset.apply == nullptr)
        return;

    preset.apply();

    // **ユーザープリセットとまったく同じ後片付け**（`loadPreset()`）。
    // 忘れると、当てた直後にUndoが1つ余計に積まれます
    lastSnapshot = captureParameterValues();

    // 8.318：**当てた直後の値を、名前と一緒に覚えます**（Phase 311）。
    // これと見比べて「触ったかどうか」を決めます
    setCurrentPreset ({ preset.name, lastSnapshot, false });

    if (onStateRestored != nullptr)
        onStateRestored();
}

void MantaPluginToolbar::showPresetMenu()
{
    auto folder = getPresetFolder();
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.xml");

    files.sort();

    // 8.318：**いま当たっているものに印を付ける**（Phase 311）。
    // 表示欄と同じことを、選び直すときにも見せます
    auto isCurrent = [this] (const juce::String& name)
    {
        return showsCurrentPreset && name == currentPreset.name;
    };

    juce::PopupMenu menu;

    // 8.173：**工場プリセットを先に**（Phase 213）。
    //
    // 分けているのは、**上書きできるかどうかが違う**ためです——
    // 工場のものはexeの中にあり、保存も削除もできません。
    // 同じ並びに混ぜると、「消したのに残っている」が起きます。
    //
    // 小分けは`category`ごと。**150個を平らに並べると、画面からはみ出します**
    if (! factoryPresets.empty())
    {
        // 8.321：**並べ方は`getFactoryPresetsInMenuOrder()`に任せる**（Phase 312）。
        // ▶で送る順と同じものを使うためです。小分けは**並びの中で切れ目が来たら**閉じます
        // （その関数が小分けごとに固めて返すので、同じ小分けが2回出てくることはありません）
        juce::PopupMenu submenu;
        juce::String submenuCategory;
        bool submenuOpen = false;
        bool submenuContainsCurrent = false;

        auto closeSubmenu = [&]
        {
            if (! submenuOpen)
                return;

            // 小分けの見出しにも印を付ける。**開くまで中が見えない**ので、
            // どの小分けに入っているかが分からないと、150個の中を探すことになります
            //
            // **`nullptr`と書かないこと**——絵の引数は`Image`版と`Drawable`版があり、
            // どちらにも取れて曖昧になります
            menu.addSubMenu (submenuCategory.isEmpty() ? utf8 ("その他") : submenuCategory,
                             submenu, true, std::unique_ptr<juce::Drawable>(), submenuContainsCurrent);

            submenu = {};
            submenuContainsCurrent = false;
            submenuOpen = false;
        };

        for (const int i : getFactoryPresetsInMenuOrder())
        {
            const auto& preset = factoryPresets[(size_t) i];

            if (! submenuOpen || preset.category != submenuCategory)
            {
                closeSubmenu();
                submenuCategory = preset.category;
                submenuOpen = true;
            }

            const bool current = isCurrent (preset.name);
            submenuContainsCurrent = submenuContainsCurrent || current;

            submenu.addItem (2000 + i, preset.name, true, current);
        }

        closeSubmenu();
        menu.addSeparator();
    }

    if (files.isEmpty())
    {
        menu.addItem (-1, utf8 ("保存されたプリセットはありません"), false);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
        {
            const auto name = files[i].getFileNameWithoutExtension();
            menu.addItem (i + 1, name, true, isCurrent (name));
        }
    }

    menu.addSeparator();
    menu.addItem (1000, utf8 ("いまの設定を保存..."));
    menu.addItem (1001, utf8 ("プリセットのフォルダを開く"));

    menu.showMenuAsync (juce::PopupMenu::Options()
                           .withTargetComponent (presetButton)
                           .withDeletionCheck (*this),
                         [this, files, folder] (int result)
                         {
                             if (result <= 0)
                                 return;

                             if (result == 1000)
                             {
                                 NameEntry::show (utf8 ("プリセットを保存"),
                                                   utf8 ("名前を付けて保存します。"),
                                                   presetFolder,
                                                   [this] (const juce::String& name) { savePresetAs (name); });
                                 return;
                             }

                             if (result == 1001)
                             {
                                 folder.revealToUser();
                                 return;
                             }

                             if (result >= 2000)
                             {
                                 applyFactoryPreset (result - 2000);
                                 return;
                             }

                             if (juce::isPositiveAndBelow (result - 1, files.size()))
                                 loadPreset (files[result - 1]);
                         });
}

void MantaPluginToolbar::savePresetAs (const juce::String& name)
{
    const auto safeName = juce::File::createLegalFileName (name).trim();

    if (safeName.isEmpty())
        return;

    // 8.318：**書く前に名前を付けること**（Phase 311）。
    // 保存した瞬間から、いまの音は**そのプリセットそのもの**です。
    // 先に付けておけば、ファイルの中にも自分の名前が入ります
    setCurrentPreset ({ safeName, captureParameterValues(), false });

    // **`apvts.state`ごと保存する。** 画面の見せ方も一緒に入るので、
    // 「この設定で見ていた」までそのまま戻ります
    if (auto xml = state.copyState().createXml())
        xml->writeTo (getPresetFolder().getChildFile (safeName + ".xml"));
}

void MantaPluginToolbar::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
        return;

    state.replaceState (juce::ValueTree::fromXml (*xml));

    lastSnapshot = captureParameterValues();

    // 8.318：**名前はファイル名で付け直すこと**（Phase 311）。
    // ファイルの中の`presetName`は保存した時点のもので、
    // 後からファイル名を変えていれば食い違います——表に出ている名前が正です
    setCurrentPreset ({ file.getFileNameWithoutExtension(), lastSnapshot, false });

    if (onStateRestored != nullptr)
        onStateRestored();
}

//==============================================================================
// 8.321：前・次のプリセットへ送る（Phase 312）

std::vector<int> MantaPluginToolbar::getFactoryPresetsInMenuOrder() const
{
    // **小分けは出てきた順**、中は渡された順。
    // 150個を平らに並べると画面からはみ出すので、メニューは小分けごとに畳みます（8.173）
    juce::StringArray categoriesInOrder;

    for (const auto& preset : factoryPresets)
        categoriesInOrder.addIfNotAlreadyThere (preset.category);

    std::vector<int> order;
    order.reserve (factoryPresets.size());

    for (const auto& category : categoriesInOrder)
        for (int i = 0; i < (int) factoryPresets.size(); ++i)
            if (factoryPresets[(size_t) i].category == category)
                order.push_back (i);

    return order;
}

juce::Array<juce::File> MantaPluginToolbar::findUserPresetFiles() const
{
    const auto folder = AppSettings::getDataFolder().getChildFile ("Presets").getChildFile (presetFolder);

    if (! folder.isDirectory())
        return {};

    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();   // **メニューと同じ並べ方**（`showPresetMenu()`）

    return files;
}

std::vector<MantaPluginToolbar::PresetStep> MantaPluginToolbar::getPresetsInMenuOrder() const
{
    // **メニューと同じ順**：工場のもの（小分けごと）→ ユーザーのもの（名前順）
    std::vector<PresetStep> steps;

    for (const int i : getFactoryPresetsInMenuOrder())
        steps.push_back ({ i, {}, factoryPresets[(size_t) i].name });

    for (const auto& file : findUserPresetFiles())
        steps.push_back ({ -1, file, file.getFileNameWithoutExtension() });

    return steps;
}

void MantaPluginToolbar::stepPreset (int direction)
{
    const auto steps = getPresetsInMenuOrder();

    if (steps.empty())
        return;

    const int count = (int) steps.size();
    int current = -1;

    if (currentPreset.name.isNotEmpty())
        for (int i = 0; i < count; ++i)
            if (steps[(size_t) i].name == currentPreset.name)
            {
                current = i;
                break;
            }

    // **何も当てていないとき**は、▶で先頭、◀で末尾へ。
    // **端まで来たら反対の端へ回ります**——止めると、端で押しても何も起きず、
    // 壊れたように見えます
    const int next = current < 0 ? (direction > 0 ? 0 : count - 1)
                                  : ((current + direction) % count + count) % count;

    const auto& step = steps[(size_t) next];

    // **当てる道はメニューから選んだときと同じ**（名前・Undoの後片付けも同じになる）
    if (step.factoryIndex >= 0)
        applyFactoryPreset (step.factoryIndex);
    else
        loadPreset (step.file);
}
