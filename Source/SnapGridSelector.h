#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SnapGrid.h"

//==============================================================================
/**
    仕様書5.5・5.9：**編集の刻み（スナップ）を選ぶ小さな部品**（Phase 55）。

    ### なぜ部品にしたか

    Phase 54では入口をトランスポートバーの1つだけにしていましたが、
    **エディタをポップアウトすると別ウィンドウになる**ため、そこからは触れませんでした
    （ノートを打ち込んでいる最中こそ刻みを変えたい）。
    そこでアレンジ画面とピアノロールの両方に置いています。

    **同じ部品を2箇所に出すことになるので、値は必ずモデルから来ること**（HANDOVER 1.27）。
    このクラスは表示役で、**選ばれた値を自分では覚えません。**
    変更は`onSnapGridChanged`で外へ渡し、表示は`setSnapGrid()`で外から合わせます。
    片方で変えたらもう片方も追従させるのは`MainComponent`の仕事です。

    ### 「Snap」と書いてあるのはなぜか

    ピアノロールには**クオンタイズのグリッド**（`gridBox`）が並んでいて、
    そちらも「1/16」のような同じ表示になります。**どちらがどちらか分からなくなる**ので、
    見出しを付けています。記号は使いません（この環境のフォントに無い。1.30）。
*/
class SnapGridSelector : public juce::Component
{
public:
    SnapGridSelector();

    void resized() override;

    /** 刻みが選び直されたときに呼ばれる。**モデルを書き換えるのは受け手の仕事。** */
    std::function<void (SnapGrid)> onSnapGridChanged;

    /** 表示をモデルの値へ合わせる（通知は飛ばさない）。

        **もう片方の入口で変えられたときにも呼ぶこと。** 呼ばないと、
        アレンジとピアノロールで違う値が出たまま残ります（1.27）。 */
    void setSnapGrid (SnapGrid grid);

    /** 見出しと本体を並べたときの推奨幅。呼び出し側のレイアウト計算に使う。 */
    static constexpr int preferredWidth = 112;

private:
    juce::Label caption;
    juce::ComboBox box;

    /** モデル→表示の反映中に、表示→モデルの書き戻しを防ぐ（TransportBarComponentと同じ形）。 */
    bool isUpdatingFromModel = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SnapGridSelector)
};
