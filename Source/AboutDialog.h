#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentDisplay.h"   // 8.188：Emberではバージョン番号を7セグメントで（Phase 226）

/**
    8.188：**アプリが自分の身元を名乗る一枚**（Phase 226／本人の要望）。

    ─────────────────────────────────────────────────────────────────────────
    なぜ要るか
    ─────────────────────────────────────────────────────────────────────────

    **AGPLv3で配るから**です（8.182）。

    zipには`LICENSE.txt`が入っていますが、**インストーラーで入れた人や、
    AppImageを1つだけ受け取った人には届きません**。

    AGPLv3 §5(d)は「もともと法的表示を出していないUIなら、出すようにする義務はない」
    としているので、**無くても違反ではありません**。ただ、
    **使う人がアプリの中からライセンスとソースの場所を知る手段が無い**状態は、
    オープンソースのアプリとしては欠けています。

    ─────────────────────────────────────────────────────────────────────────
    載せているもの
    ─────────────────────────────────────────────────────────────────────────

    | | なぜ |
    |---|---|
    | 名前とバージョン | 不具合の報告で**どの版の話か**が分かる。無いと再現できない |
    | 著作権表示 | 誰が作ったか |
    | ライセンスと**無保証** | AGPLv3が求めるところ |
    | **ソースの場所** | AGPLの肝。**中身を見に行ける**ようにしておく |
    | 他人のもの | JUCE、VST3 SDK、VSTがSteinbergの商標であること |

    **名前・バージョン・URLは全部`Branding.h`から引きます**（8.186で3件取りこぼした）。
    ここに直に書かないこと。
*/
class AboutDialog : public juce::Component
{
public:
    AboutDialog();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** メニューから呼ぶ入口。**窓は自分で自分を片付けます**（`PreferencesDialog`と同じ）。 */
    static void launch();

private:
    juce::Image appIcon;

    juce::Label titleLabel;
    // **`SegmentLabel`はEmber以外では普通のラベルとして描きます**（`canDraw()`が
    // `Branding::retroUI`を見る）ので、ここで分岐する必要はありません
    SegmentLabel versionLabel;
    juce::Label copyrightLabel;

    juce::TextEditor licenceText;  // 選んでコピーできるように**エディタ**（読み取り専用）

    juce::HyperlinkButton sourceButton;
    juce::TextButton closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutDialog)
};
