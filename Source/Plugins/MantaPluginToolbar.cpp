#include "MantaPluginToolbar.h"

#include "MantaTheme.h"
#include "../AppSettings.h"
#include "../NameEntry.h"
#include "../Utf8.h"

//==============================================================================

void MantaPluginToolbar::styleButton (juce::TextButton& button, const juce::String& text)
{
    button.setButtonText (text);
    button.setColour (juce::TextButton::buttonColourId, MantaTheme::panelBackground());
    button.setColour (juce::TextButton::buttonOnColourId, MantaTheme::accent());
    button.setColour (juce::TextButton::textColourOffId, MantaTheme::text());
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
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

    undoButton.setTooltip (utf8 ("ひとつ前の設定へ戻す"));
    abButton.setTooltip (utf8 ("2つの設定を切り替えて聴き比べる"));
    copyButton.setTooltip (utf8 ("いまの設定を、もう片方へ写す"));

    lastSnapshot = captureParameterValues();
    slotA = lastSnapshot;
    slotB = lastSnapshot;

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

    undoButton.setBounds (area.removeFromLeft (58));
    area.removeFromLeft (2);
    redoButton.setBounds (area.removeFromLeft (58));
    area.removeFromLeft (12);
    abButton.setBounds (area.removeFromLeft (34));
    area.removeFromLeft (2);
    copyButton.setBounds (area.removeFromLeft (54));
    area.removeFromLeft (12);
    presetButton.setBounds (area.removeFromLeft (74));
    area.removeFromLeft (12);

    trailingArea = area;
}

void MantaPluginToolbar::timerCallback()
{
    pushUndoSnapshotIfChanged();
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

void MantaPluginToolbar::pushUndoSnapshotIfChanged()
{
    // **掴んでいるあいだは積まない**（ドラッグの途中が刻まれると、
    // Undoを何十回も押すことになります）
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;

    auto current = captureParameterValues();

    if (current == lastSnapshot)
        return;

    undoStack.push_back (lastSnapshot);

    if (undoStack.size() > maxUndoSteps)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
    lastSnapshot = std::move (current);
}

void MantaPluginToolbar::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (captureParameterValues());
    applyParameterValues (undoStack.back());
    undoStack.pop_back();

    lastSnapshot = captureParameterValues();
}

void MantaPluginToolbar::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (captureParameterValues());
    applyParameterValues (redoStack.back());
    redoStack.pop_back();

    lastSnapshot = captureParameterValues();
}

void MantaPluginToolbar::toggleAB()
{
    // **いま鳴っているほうを、いまの箱へしまってから入れ替える**
    (usingSlotB ? slotB : slotA) = captureParameterValues();
    usingSlotB = ! usingSlotB;

    applyParameterValues (usingSlotB ? slotB : slotA);

    lastSnapshot = captureParameterValues();
    refreshButtons();
}

void MantaPluginToolbar::copyToOtherSlot()
{
    const auto current = captureParameterValues();

    slotA = current;
    slotB = current;
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

    if (onStateRestored != nullptr)
        onStateRestored();
}

void MantaPluginToolbar::showPresetMenu()
{
    auto folder = getPresetFolder();
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.xml");

    files.sort();

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
        juce::StringArray categoriesInOrder;

        for (const auto& preset : factoryPresets)
            categoriesInOrder.addIfNotAlreadyThere (preset.category);

        for (const auto& category : categoriesInOrder)
        {
            juce::PopupMenu submenu;

            for (int i = 0; i < (int) factoryPresets.size(); ++i)
                if (factoryPresets[(size_t) i].category == category)
                    submenu.addItem (2000 + i, factoryPresets[(size_t) i].name);

            if (category.isEmpty())
                menu.addSubMenu (utf8 ("その他"), submenu);
            else
                menu.addSubMenu (category, submenu);
        }

        menu.addSeparator();
    }

    if (files.isEmpty())
    {
        menu.addItem (-1, utf8 ("保存されたプリセットはありません"), false);
    }
    else
    {
        for (int i = 0; i < files.size(); ++i)
            menu.addItem (i + 1, files[i].getFileNameWithoutExtension());
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

    if (onStateRestored != nullptr)
        onStateRestored();
}
