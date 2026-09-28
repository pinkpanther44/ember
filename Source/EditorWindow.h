#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    仕様書4.2：エディタをポップアウトするためのウィンドウ（Phase 16）。

    中身（ピアノロール・Consoleなど）は**借りるだけで、所有しない**。
    メインウィンドウへ戻すときに、そのまま親を付け替えられるようにするため。
    **借りるときは`setBorrowedContent()`、返すときは`clearBorrowedContent()`を使うこと**
    （`setContentNonOwned()`を直に呼ぶと、下の受け持ち役を通らなくなります）。

    8.328：**この窓の中でもドラッグできるようにする**（Phase 318／本人の報告）。

    > 「コンソールウィンドウをPop outすると、トラックをドラッグして順番を入れ替えができなくなってしまう」

    JUCEのドラッグは、送り手の**祖先から**`DragAndDropContainer`を探して始まります
    （`findParentDragContainerFor()`）。ドックにいるあいだは`MainComponent`がそれでしたが、
    **Pop outすると中身はこの窓の子になり、祖先に受け持ち役がいなくなります**。
    ストリップの`mouseDrag()`は「見つからなければ何もしない」ので、黙って動きませんでした。

    借りた中身を**受け持ち役を兼ねた入れ物**（`Holder`）に入れて、窓にはその入れ物を持たせます。
    窓そのものを受け持ち役にしないのは、ドラッグ中の絵が受け持ち役の**子として足される**ためです
    ——JUCEは`ResizableWindow`へ直に子を足すことを想定していません（中身を通せ、と書いてある）。

    > **窓をまたぐドラッグは、これでは効きません**（メインウィンドウのブラウザから
    > この窓のConsoleへ、など）。受け持ち役が窓ごとに別なので、送り手の窓の中でしか
    > 受け手を探しません。要るなら`startDragging()`の`allowDraggingToOtherJuceWindows`です。
*/
class EditorWindow : public juce::DocumentWindow
{
public:
    /** `addToDesktop`は試しのため（窓を出さずに中身の親子だけ作る）。 */
    EditorWindow (const juce::String& name, juce::Colour backgroundColour, bool addToDesktop = true)
        : DocumentWindow (name, backgroundColour, juce::DocumentWindow::allButtons, addToDesktop)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentOwned (new Holder(), false);
    }

    /** 中身を借りる（前に借りていたものは返す）。 */
    void setBorrowedContent (juce::Component* content)
    {
        if (auto* holder = dynamic_cast<Holder*> (getContentComponent()))
            holder->setBorrowed (content);
    }

    /** 借りていた中身を返す。**窓を壊す前に呼ぶこと**（呼ばなくても入れ物が外しますが、順番を読みやすく）。 */
    void clearBorrowedContent() { setBorrowedContent (nullptr); }

    std::function<void()> onCloseRequested;

    void closeButtonPressed() override
    {
        // HANDOVER 1.5：**ウィンドウが自分のコールバックの中で自分を破棄してはいけない。**
        // 呼び出し元のスタックが解放済みメモリを触ることになる。
        // ここでは依頼を投げるだけにして、実際の破棄は呼ばれた側が
        // このスタックを抜けてから行う（MainComponent::dockEditor）。
        if (onCloseRequested != nullptr)
            onCloseRequested();
    }

private:
    /** 借りた中身を入れる入れ物。**ドラッグの受け持ち役を兼ねます**（8.328）。 */
    struct Holder : public juce::Component,
                    public juce::DragAndDropContainer
    {
        ~Holder() override
        {
            // **借りたものは消さずに外すだけ**（持ち主は`MainComponent`）
            setBorrowed (nullptr);
        }

        void setBorrowed (juce::Component* content)
        {
            if (borrowed != nullptr)
                removeChildComponent (borrowed.getComponent());

            borrowed = content;

            if (content != nullptr)
            {
                addAndMakeVisible (content);
                resized();
            }
        }

        void resized() override
        {
            if (borrowed != nullptr)
                borrowed->setBounds (getLocalBounds());
        }

        juce::Component::SafePointer<juce::Component> borrowed;
    };
};
