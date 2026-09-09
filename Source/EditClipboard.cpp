#include "EditClipboard.h"

namespace EditClipboard
{
    // アプリ全体で1つ。**プロジェクトには保存しません**（画面をまたいだ一時的なもの）
    static Kind currentKind = Kind::none;
    static juce::Array<Item> currentItems;

    void set (Kind kind, juce::Array<Item> items)
    {
        // 空のものを入れて「コピーした気になる」状態を作らない
        if (items.isEmpty())
        {
            clear();
            return;
        }

        currentKind = kind;
        currentItems = std::move (items);
    }

    Kind getKind()                          { return currentItems.isEmpty() ? Kind::none : currentKind; }
    bool isEmpty()                          { return currentItems.isEmpty(); }
    const juce::Array<Item>& getItems()     { return currentItems; }

    void clear()
    {
        currentKind = Kind::none;
        currentItems.clear();
    }
}
