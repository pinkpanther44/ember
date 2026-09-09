#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PanelResizerBar.h"

//==============================================================================
/**
    設計書2.2「下：エディタパネル」／仕様書4.2に対応する、画面下部のエディタ領域。

    Phase 16で新設。それ以前はピアノロールが独立したタブ（ページ）で、
    打ち込みとアレンジを行き来するのにページを切り替える必要があった。
    設計書2.2はアレンジ画面の**下部へのインライン表示**を基本としているため、
    ウィンドウ下端に置くパネルへ移した。

    このクラス自身は「枠」だけを提供する：
    - ヘッダー（タイトル／ポップアウト／閉じる）
    - 上端のリサイズ用ストリップ（ドラッグで高さを変える）
    - 中身を置く領域

    **中身（ピアノロール等）の所有権は持たない。** `setContent()`で渡された
    コンポーネントを子として並べるだけで、実体はMainComponentが持ち続ける。
    こうしておくと、ポップアウト（別ウィンドウ）へ移すときに
    所有権を移し替えずに親だけ付け替えられる（仕様書4.2）。
*/
class EditorPanel : public juce::Component
{
public:
    EditorPanel();

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /** 表示する中身を差し替える（nullptrで空にする）。所有権は呼び出し側のまま。 */
    void setContent (juce::Component* newContent);

    /** ヘッダーに出す名前。将来オーディオエディタ／コードパッドを載せるときに使う。 */
    void setTitle (const juce::String& newTitle);

    /** ヘッダーの「×」が押されたとき。 */
    std::function<void()> onCloseClicked;

    /** ヘッダーの「Pop Out」が押されたとき（仕様書4.2）。 */
    std::function<void()> onPopOutClicked;

    /** 上端をドラッグして高さを変えたときに、希望の高さ（ピクセル）を通知する。
        実際にその高さにするかは、ウィンドウ全体を見ているMainComponentが決める。 */
    std::function<void (int)> onHeightChangeRequested;

    static constexpr int headerHeight = 26;

    /** 上端の帯の高さ。左右パネルと同じ`PanelResizerBar`を使う（Phase 26で統合）。 */
    static constexpr int resizerHeight = PanelResizerBar::thickness;

    /** これより低くはできない高さ（ヘッダーだけが残って中身が見えない状態を防ぐ）。 */
    static constexpr int minimumHeight = headerHeight + resizerHeight + 120;

private:
    juce::Component* content = nullptr; // 所有しない

    // 上端の細い帯。ドラッグでパネルの高さを変える。
    //
    // **Phase 26で、左右パネルと同じ`PanelResizerBar`に統合した。**
    // それ以前はこのクラスの中に専用のResizerStripがあり、同じことを
    // 別々に実装していた（HANDOVER 1.11・6.3で「実装が2つある」と記録していた点）。
    //
    // 左右パネルの帯はパネルの**兄弟**だが、こちらはパネルの**子**なので、
    // 大きさの問い合わせ（getCurrentSize）には親の高さをそのまま返せばよい。
    PanelResizerBar resizer { PanelResizerBar::Edge::Top };
    juce::Label titleLabel;
    juce::TextButton popOutButton { "Pop Out" };
    juce::TextButton closeButton { "X" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorPanel)
};
