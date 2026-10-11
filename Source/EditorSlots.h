#pragma once

#include <juce_core/juce_core.h>

/** 設計書2.3.5：下部パネル／Pop out の窓に出す中身（Phase 43・66・79・114）。 */
enum class EditorContent { none, pianoRoll, audioEditor, chordPad, console };

//==============================================================================
/**
    8.347：**下段と Pop out の窓に、別々の中身を出す**（Phase 335／本人の要望）。

    > 「Arrange画面+1項目を開くことができる。これをArrange画面+2項目を開くことができるようにすることは可能？」
    > 例：下段にコードパッド＋Pop out にピアノロール／下段にピアノロール＋Pop out に Console

    Phase 334 までは**器が1つ**で、下段か窓のどちらか一方にしか出せなかった。
    いまは**置き場所が2つ**（下段 `dock`、窓 `window`）。

    ### 決まり（本人と決めたもの）

    | 操作 | どうなるか |
    |---|---|
    | 下段の「Pop Out」 | **下段のものを窓へ送る**。窓に別のものがあれば**下段と入れ替える** |
    | フッターのボタン | 押した中身が**窓に出ていれば、窓を前面に出す**。下段に出ていれば閉じる（今までどおり） |
    | 窓を閉じる | 下段が空いていれば（閉じている／空）、窓の中身を下段へ戻して開く。下段に別のものがあれば、窓だけ閉じる |
    | トラックを選ぶ | ピアノロール／オーディオエディタ／コードパッドを出している側が付いていく（Console は取り上げない） |

    **同じ中身を両方には出せません**（中身の部品は1つずつしか無い）。窓が開いているあいだは
    いつも `dock != window`。窓を開いた瞬間の下段は `none`（何も持たない）になります。

    **ここは状態だけを持つ**（部品には触らない）。`MainComponent::applyEditorSlots()`が
    この状態どおりに部品を付け替えます——判断を1か所に集め（1.26）、窓を出さずに自己検査できるように。
*/
class EditorSlots
{
public:
    EditorContent dock = EditorContent::pianoRoll;   ///< 下段が持っている中身（閉じていても持っている）
    bool dockOpen = false;                           ///< 下段が開いているか
    EditorContent window = EditorContent::none;      ///< 窓の中身（`windowOpen`のときだけ意味がある）
    bool windowOpen = false;

    /** Editor ボタンで戻る先（ピアノロールかオーディオエディタ。8.75）。 */
    EditorContent lastEditor = EditorContent::pianoRoll;

    //==========================================================================
    /** フッターの同じボタンが受け持つか（Editor ボタンはピアノロールとオーディオエディタの両方）。 */
    static bool sameButton (EditorContent a, EditorContent b)
    {
        auto isEditor = [] (EditorContent c) { return c == EditorContent::pianoRoll || c == EditorContent::audioEditor; };
        return a == b || (isEditor (a) && isEditor (b));
    }

    bool isInWindow (EditorContent c) const   { return windowOpen && window == c; }
    bool isShown (EditorContent c) const      { return (dockOpen && dock == c) || isInWindow (c); }

    /** フッターのボタンを灯すか（その中身がどちらかに見えている）。 */
    bool isButtonLit (EditorContent buttonContent) const
    {
        return (dockOpen && dock != EditorContent::none && sameButton (dock, buttonContent))
            || (windowOpen && sameButton (window, buttonContent));
    }

    //==========================================================================
    /** フッターのボタン。**窓を前面に出すべきなら true**。 */
    bool toggle (EditorContent content)
    {
        if (windowOpen && sameButton (window, content))
            return true;

        if (dockOpen && dock != EditorContent::none && sameButton (dock, content))
        {
            dockOpen = false;
            return false;
        }

        setDock (content);
        dockOpen = true;
        return false;
    }

    /** ダブルクリックなどで「これを見せて」。窓に（同じボタンの中身が）出ていれば窓のほうで見せる。
        **窓を前面に出すべきなら true**。 */
    bool show (EditorContent content)
    {
        if (windowOpen && sameButton (window, content))
        {
            setWindow (content);
            return true;
        }

        setDock (content);
        dockOpen = true;
        return false;
    }

    /** 下段の「Pop Out」。 */
    void popOut()
    {
        if (! dockOpen || dock == EditorContent::none)
            return;

        if (! windowOpen)
        {
            window = dock;
            windowOpen = true;
            dock = EditorContent::none;
            dockOpen = false;
            return;
        }

        std::swap (dock, window);   // 窓に別のものがあれば入れ替える（下段は開いたまま）
    }

    /** 窓を閉じた。 */
    void closeWindow()
    {
        if (! windowOpen)
            return;

        const auto content = window;
        windowOpen = false;
        window = EditorContent::none;

        if (! dockOpen || dock == EditorContent::none)
        {
            dock = content;
            dockOpen = true;
            remember (content);
        }
    }

    /** 下段を閉じる（中身は持ったまま）。 */
    void closeDock()   { dockOpen = false; }

    /** 8.311：プロジェクトを開いたときに、控えてあった窓を開き直す。 */
    void openWindowWith (EditorContent content)
    {
        if (content == EditorContent::none)
            return;

        if (windowOpen)
        {
            setWindow (content);
            return;
        }

        window = content;
        windowOpen = true;

        if (dock == content)
        {
            dock = EditorContent::none;
            dockOpen = false;
        }

        remember (content);
    }

    /** トラックを選んだ。`wanted`は選んだトラックの種別に合う中身（8.39）。
        **どちらも付いていけないとき（Console だけ／何も無い）は、Editor ボタンの戻り先だけ覚える**（8.27）。 */
    void followSelection (EditorContent wanted)
    {
        if (dock == wanted || isInWindow (wanted))
            return;   // もうどこかにある。読み直すだけ

        const bool windowFollows = windowOpen && window != EditorContent::console;
        const bool dockFollows = dock != EditorContent::console && dock != EditorContent::none;

        if (windowFollows && sameButton (window, wanted))
            setWindow (wanted);
        else if (dockFollows)
            setDock (wanted);
        else if (windowFollows)
            setWindow (wanted);
        else
            remember (wanted);
    }

    //==========================================================================
    // 保存（8.311）。**綴りを変えないこと**（保存したプロジェクトが読めなくなる）

    static juce::String toString (EditorContent c)
    {
        switch (c)
        {
            case EditorContent::pianoRoll:   return "pianoRoll";
            case EditorContent::audioEditor: return "audioEditor";
            case EditorContent::chordPad:    return "chordPad";
            case EditorContent::console:     return "console";
            case EditorContent::none:
            default:                         return {};
        }
    }

    static EditorContent fromString (const juce::String& s)
    {
        for (auto c : { EditorContent::pianoRoll, EditorContent::audioEditor, EditorContent::chordPad, EditorContent::console })
            if (s == toString (c))
                return c;

        return EditorContent::none;
    }

private:
    /** 下段へ置く。**窓が同じものを持っていれば入れ替える**（同じ中身を2か所に出さない）。 */
    void setDock (EditorContent content)
    {
        // 呼ぶ側は「窓に出ていない」ことを先に見ている（`toggle`・`show`・`followSelection`）。
        // それでも来たら入れ替える。入れ替える相手が無ければ窓を閉じる（空の窓は出さない）
        if (isInWindow (content))
        {
            window = dock;

            if (window == EditorContent::none)
                windowOpen = false;
        }

        dock = content;
        remember (content);
    }

    /** 窓へ置く。**下段が同じものを持っていれば入れ替える**。 */
    void setWindow (EditorContent content)
    {
        if (dock == content)
        {
            dock = window;

            if (dock == EditorContent::none)
                dockOpen = false;
        }

        window = content;
        remember (content);
    }

    /** 8.75：Editor ボタンの戻り先に覚えてよいのは、Editor ボタンのものだけ。 */
    void remember (EditorContent content)
    {
        if (content == EditorContent::pianoRoll || content == EditorContent::audioEditor)
            lastEditor = content;
    }
};
