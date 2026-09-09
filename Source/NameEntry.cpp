#include "NameEntry.h"
#include "Utf8.h"

namespace NameEntry
{
    void show (const juce::String& title,
               const juce::String& message,
               const juce::String& initialText,
               std::function<void (const juce::String& newName)> onAccepted,
               const juce::String& fieldLabel)
    {
        auto* window = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon);

        // 8.103：**入力欄の見出しは呼び出し側が決める**（Phase 142）。
        // 「名前」で固定していたので、拍子やテンポを打ち込むダイアログでも
        // 「名前」と出ていました——**何を入れる欄なのかが読めません**（1.9）
        window->addTextEditor ("name", initialText,
                                fieldLabel.isNotEmpty() ? fieldLabel : utf8 ("名前"));
        window->addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (utf8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

        window->enterModalState (true,
            juce::ModalCallbackFunction::create ([window, onAccepted] (int result) mutable
            {
                if (result == 1 && onAccepted != nullptr)
                {
                    const auto newName = window->getTextEditorContents ("name").trim();

                    // **空の名前は通さない**（見出しが消えて、何なのか分からなくなる）
                    if (newName.isNotEmpty())
                        onAccepted (newName);
                }

                delete window;
            }),
            false);
    }
}
