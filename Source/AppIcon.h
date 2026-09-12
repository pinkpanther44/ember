#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Branding.h"
#include "BinaryData.h"

//==============================================================================
/**
    8.229：**アプリのアイコンを読む**（Phase 249／本人の報告「Linux版のアイコンが歯車」）。

    ─────────────────────────────────────────────────────────────────────────
    なぜ1本にまとめたか
    ─────────────────────────────────────────────────────────────────────────

    `SplashWindow`と`AboutDialog`が**同じ`loadEmbedded()`を1つずつ持っていました。**
    窓のアイコンにも要るので、**3つめを書く前にここへ出しました**（1.27）。

    ─────────────────────────────────────────────────────────────────────────
    名前で引くこと
    ─────────────────────────────────────────────────────────────────────────

    `BinaryData::app_icon_png`と**直に書きません。**
    CMakeは**そのブランドのぶんだけ**埋め込むので、直に書くと
    もう片方のブランドでビルドが通りません（8.175）。

    ─────────────────────────────────────────────────────────────────────────
    Linuxの窓のアイコン
    ─────────────────────────────────────────────────────────────────────────

    `juce_add_gui_app`の`ICON_BIG`は、**WindowsのexeとmacOSのバンドル**に効きます。
    **Linuxには効きません**——juceaideが作るのは`.ico`と`.icns`だけで、
    X11の窓に付けるアイコンは**アプリが自分で`setIcon()`する**ことになっています。

    渡さないと、デスクトップ環境が**「実行ファイル」の既定の絵**を出します
    （多くの環境で**歯車**）。AppImageに`.desktop`とアイコンを入れてあっても、
    **走り出したあとの窓とタスクバーはそれとは別**です。
*/
namespace AppIcon
{
    /** 埋め込みから名前で引く（無ければ無効な`Image`）。 */
    inline juce::Image loadEmbedded (const char* binaryDataName)
    {
        int size = 0;

        if (const auto* data = BinaryData::getNamedResource (binaryDataName, size))
            return juce::ImageFileFormat::loadFrom (data, (size_t) size);

        return {};
    }

    /** いまのブランドのアプリアイコン。

        **毎回読み直さないこと**——PNGを解く処理が入ります。
        呼ぶ側が1度だけ読んで持っておきます（`SplashWindow`・`AboutDialog`もそうしています）。 */
    inline juce::Image load()
    {
        return loadEmbedded (Branding::isEmber ? "ember_icon_png" : "app_icon_png");
    }

    //==========================================================================
    /** 8.234：**窓そのものにアイコンを渡す**（Phase 251／本人の報告「まだ歯車のまま」）。

        ### `DocumentWindow::setIcon()`では届きません

        Phase 249ではこう書いて、**何も起きませんでした**：

        ```cpp
        void DocumentWindow::setIcon (const Image& imageToUse)
        {
            titleBarIcon = imageToUse;   // ← これだけ
            repaintTitleBar();
        }
        ```

        **JUCEが自分で描くタイトルバーの絵**を変えるだけです。
        `setUsingNativeTitleBar (true)`にしてあるので**そのタイトルバーは描かれず**、
        呼んでも呼ばなくても同じでした。

        X11へ渡す（`_NET_WM_ICON`を立てる）のは**`ComponentPeer::setIcon()`**のほうです。

        > **同じ名前で、届く先が違います。** 呼べたから効いている、とは限りません。

        ### 窓が画面に出てから呼ぶこと

        ピアは`setVisible(true)`（`addToDesktop()`）で初めてできます。
        **それより前に呼ぶと`getPeer()`が`nullptr`**で、やはり何も起きません。

        ### 8.235：**縮めてから渡すこと**（Phase 251／2度目の「まだ歯車」）

        ピアへ渡しても、**まだ歯車のままでした。**

        リポジトリのアイコンは**1000×1000**（Emberは1000、Mantaは1024）。
        JUCEの`XWindowSystem::setIcon()`は`_NET_WM_ICON`を
        **`幅×高さ+2`個の`unsigned long`**で組むので、64bitでは

        ```
            (1000 × 1000 + 2) × 8バイト ≒ 8MB
        ```

        **X11のリクエスト上限に当たって、黙って捨てられます。**
        Xのエラーは非同期で返るうえ、JUCEはそれを握り潰すので、
        **呼び出しは成功したように見えます。**

        `_NET_WM_ICON`が想定しているのは16〜256px程度です。
        **128×128**にしています——これで約131KBに収まり、
        ドックの実表示（48〜64px、HiDPIでも128px程度）には十分です。

        > **「呼べた」と「届いた」は別。** ここは2回続けて、
        > **呼べているのに届いていない**でつまずきました
        > （1度目は`DocumentWindow::setIcon()`、2度目は大きさ）。 */
    inline constexpr int windowIconSize = 128;

    inline void applyToWindow (juce::Component& window)
    {
        auto* peer = window.getPeer();

        if (peer == nullptr)
            return;   // まだ画面に出ていない（上の説明）

        const auto icon = load();

        if (! icon.isValid())
            return;

        // **縮めてから渡す**（上の説明）。ついでに`getPixelAt()`を100万回
        // 呼ばずに済みます——あれは1画素ずつロックを取る作りです
        peer->setIcon (icon.getWidth() == windowIconSize && icon.getHeight() == windowIconSize
                          ? icon
                          : icon.rescaled (windowIconSize, windowIconSize,
                                            juce::Graphics::highResamplingQuality));
    }
}
