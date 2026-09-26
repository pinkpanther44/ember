#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

//==============================================================================
/**
    8.322：**メッセージボックスの入口**（Phase 312／1.0.1で見つかった落ち方）。

    **アプリの中から出すメッセージボックスは、必ずここを通すこと。**
    `juce::NativeMessageBox::showAsync`を直に呼ばないでください。

    ### なぜ直に呼んではいけないのか

    `NativeMessageBox::showAsync`は**管理されない**箱を作り、閉じられるまで自分で生き続けます。
    Windowsでは**別スレッド**でタスクダイアログを出し、閉じたら結果を
    `MessageManager::getInstance()`で本体へ返します——**無ければ作ります**。

    1. 箱が開いている（持ち主の窓を渡していないので、**本体の窓を閉じられます**）
    2. アプリが終わり、`MessageManager`が消える
    3. 箱も片付けで閉じ、別スレッドが`getInstance()`を呼ぶ
       → **終わりかけのプロセスで作り直して落ちる**

    本人の手元で1.0.1が実際にこれで落ちていました（`crash-20260923-161825.log`。
    9/19のManta Studio 0.6.0も同じ場所）。**終わるつもりの時に落ちる**ので、気づけません。

    ### ここでやっていること

    - **`showScopedAsync`で出して、手元に持つ**（閉じられるまで）
    - **終了の早いところで`closeAll()`**——持っている箱を全部閉じます。
      箱の別スレッドは`std::future`の中にいて、**消すときにスレッドの終わりを待つ**ので、
      `MessageManager`が生きているうちに片付きます

    ### ボタンの番号は、今までと同じ（押した順）

    **`showScopedAsync`はボタンの数え方が違います。** `showAsync`は押した順（0,1,2…）、
    `showScopedAsync`は`AlertWindow`と同じ「(押した番号＋1) % ボタンの数」です。
    **ここで押した順へ戻してから**コールバックへ渡します——戻さないと、
    31箇所のコールバックが全部ずれます（「復元する」を押したのに破棄される、など）。

    ### 足すときは

    **`juce::NativeMessageBox::showAsync`と書いたら、ここへ置き換えること。**
    `grep`で0件になっているのが正しい姿です。
*/
namespace AppMessageBox
{
    /** `NativeMessageBox::showAsync`と**同じ使い方**。`callback`へ渡る番号は**押した順**（0から）。 */
    void showAsync (const juce::MessageBoxOptions& options, std::function<void (int)> callback);

    /** 開いている箱を全部閉じる。**アプリの終了時、`MessageManager`が消える前に1回呼ぶこと**。
        閉じた箱のコールバックは呼ばれません（答えは出ていないので）。 */
    void closeAll();

    /** いま開いている（まだ答えが出ていない）箱の数。試験用。 */
    int getNumOpen();

    /** `showScopedAsync`の番号（`AlertWindow`の数え方）を、**押した順**へ戻す。
        試験のために外へ出してあります。 */
    int toPressedIndex (int alertWindowResult, int numButtons);
}
