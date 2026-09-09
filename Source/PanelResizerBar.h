#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
/**
    パネルの端に置いて、ドラッグでパネルの幅／高さを変えるための細い帯（Phase 17）。

    設計書2.2の「左右パネルは折りたたみ可能」「下部エディタパネル」で共通して必要になる。

    **ドラッグ量は画面座標で測る。** この帯はパネルの中にあるため、パネルが伸縮すると
    帯自身も動く。コンポーネント内の座標で測ると基準点が一緒に動いてしまい、
    震えるような挙動になる（Phase 16で実際に踏んだ）。
*/
class PanelResizerBar : public juce::Component
{
public:
    /** 帯がパネルのどの辺にあるか。どちらへドラッグすると大きくなるかが決まる。 */
    enum class Edge
    {
        Right, // 左パネルの右端：右へドラッグすると広がる
        Left,  // 右パネルの左端：左へドラッグすると広がる
        Top    // 下パネルの上端：上へドラッグすると高くなる
    };

    explicit PanelResizerBar (Edge edgeToUse);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    /** ドラッグ中、希望する新しい大きさ（横帯なら高さ、縦帯なら幅）を通知する。
        実際にその大きさにするかは、全体のレイアウトを知っている側が決める。 */
    std::function<void (int)> onSizeDragged;

    /** ドラッグ開始時点のパネルの大きさを問い合わせる。**必ず設定すること。**

        帯はパネルの「中」にあるとは限らない（このプロジェクトでは、パネルの端に
        重ねて置く兄弟コンポーネントにしている）。そのため親のサイズを見ても
        パネルの大きさは分からず、外から教えてもらう必要がある。 */
    std::function<int()> getCurrentSize;

    /** 帯の太さ。レイアウト計算に使う。 */
    static constexpr int thickness = 6;

private:
    bool isHorizontalBar() const { return edge == Edge::Top; }

    const Edge edge;

    int dragStartSize = 0;
    juce::Point<int> dragStartScreenPosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelResizerBar)
};
