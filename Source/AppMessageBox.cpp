#include "AppMessageBox.h"

#include <map>

namespace AppMessageBox
{
    namespace
    {
        /** 開いている箱。**番号で持つ**のは、答えが出たときに自分のぶんだけ外すため。

            **関数の中の`static`にしてあります**が、頼りにしているのは`closeAll()`のほうです。
            ここが消えるのはプロセスの最後で、そのときにはもう`MessageManager`がありません
            ——**消える前に空にしておかないと、同じ落ち方をします**。 */
        std::map<int, juce::ScopedMessageBox>& openBoxes()
        {
            static std::map<int, juce::ScopedMessageBox> boxes;
            return boxes;
        }

        int nextId = 1;
    }

    int toPressedIndex (int alertWindowResult, int numButtons)
    {
        // `AlertWindow`の数え方は「(押した番号＋1) % ボタンの数」
        // （`juce_NativeMessageBox.cpp`の`ResultCodeMappingMode::alertWindow`）。
        // 逆向きにたどると「(結果−1＋ボタンの数) % ボタンの数」です
        if (numButtons <= 0)
            return 0;

        return (alertWindowResult - 1 + numButtons) % numButtons;
    }

    void showAsync (const juce::MessageBoxOptions& options, std::function<void (int)> callback)
    {
        JUCE_ASSERT_MESSAGE_THREAD

        const int id = nextId++;
        const int numButtons = options.getNumButtons();

        auto box = juce::NativeMessageBox::showScopedAsync (options,
            [id, numButtons, callback = std::move (callback)] (int alertWindowResult)
            {
                // **自分のコールバックの中で自分を消さない**（1.5）。
                // 一度メッセージループへ返してから、持っている一覧から外します
                juce::MessageManager::callAsync ([id] { openBoxes().erase (id); });

                if (callback != nullptr)
                    callback (toPressedIndex (alertWindowResult, numButtons));
            });

        // 答えは**非同期で**返ってくるので、ここで持つ前に呼ばれることはありません
        openBoxes().emplace (id, std::move (box));
    }

    void closeAll()
    {
        // **一覧を先に空にしてから消す。** 消している最中に、閉じた箱の後片付け
        // （`openBoxes().erase`）が走っても、触る相手がいないように
        auto closing = std::move (openBoxes());
        openBoxes().clear();

        closing.clear();   // ここで1つずつ閉じ、別スレッドの終わりを待ちます
    }

    int getNumOpen()
    {
        return (int) openBoxes().size();
    }
}
