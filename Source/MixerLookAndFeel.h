#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SegmentDisplay.h"   // 8.177：数字を棒で描く（Phase 219）
#include "AppColours.h"   // 8.117：アプリの配色を被せる（Phase 152／改善案25）

//==============================================================================
/**
    8.62：**つまみとフェーダーの見た目**（Phase 100。設計書2.6）。

    ### なぜ自前で描くか

    JUCEの既定（`LookAndFeel_V4`）のロータリーは**塗りつぶした扇形**で、
    トラックヘッダーのような16px角では**扇がつぶれて、どちらを向いているのか読めません**。
    フェーダーのつまみも既定は円で、**縦フェーダーだと「いまどの目盛りか」が指しにくい**
    （円は上下方向の当たりが曖昧になる）。

    - **ノブ**：地の円＋外周の線＋**中心から外へ伸びる指針**。
      指針は細くても向きが読めるので、小さくしても意味が残ります
    - **フェーダー**：溝＋**長方形のつまみ**（真ん中に線）。
      長方形は上下の端がはっきりするので、縦フェーダーで狙いやすくなります

    ### 色は変えていません

    塗りは`Slider::trackColourId`／`rotarySliderFillColourId`から取ります。
    **配色を決めるのは呼び出し側のまま**なので、テーマ（1.34）もそのまま効きます。

    ### 使い方

    **`ValueEntrySlider`が自動で被せます。** Console・インスペクタ・
    トラックヘッダー・センド量は全部これを通っているので、
    **1箇所直せば全部の見た目が揃います**（1.27）。

    `juce::SharedResourcePointer`で持つこと：LookAndFeelは**使っている
    コンポーネントより長生きする必要**があります。
*/
class MixerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    /** 8.117：**アプリの配色を必ず被せる**（Phase 152／改善案25）。

        `LookAndFeel_V4`の既定コンストラクタは**ダーク配色**です。
        `= default`のままだったので、**ライトテーマでも文字色が白**のまま焼き付き、
        **Inspectorの音量とパンの数値が地に同化して読めません**でした。
        Console・トラックヘッダー・センド量も全部この部品を通っています。

        HANDOVER 1.43で一度つまずいた罠と**まったく同じ形**です。 */
    MixerLookAndFeel() { setColourScheme (AppColours::createColourScheme()); }

    /** 8.177：**つまみに付いている数値欄も棒で**（Phase 219）。

        `juce::Slider`のテキストボックスは`juce::Label`なので、ここを通ります。
        **1箇所でConsole・インスペクタ・トランスポート全部が変わります**
        （`ValueEntrySlider`が、どこでもこのLookAndFeelを被せているため。8.62）。

        Manta Studioでは`SevenSegment::canDraw()`が常にfalseなので、今までどおりです。 */
    void drawLabel (juce::Graphics& g, juce::Label& label) override
    {
        // **編集中は基底に任せる。** 打ち込んでいる最中の文字が棒になったら、
        // 何を書いているか分かりません
        if (label.isBeingEdited() || ! SevenSegment::canDraw (label.getText()))
        {
            juce::LookAndFeel_V4::drawLabel (g, label);
            return;
        }

        SevenSegment::draw (g, label.getLocalBounds().toFloat(), label.getText(),
                             label.findColour (juce::Label::textColourId),
                             label.getJustificationType());
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                            juce::Slider& slider) override;

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPos, float minSliderPos, float maxSliderPos,
                            juce::Slider::SliderStyle style, juce::Slider& slider) override;

    /** フェーダーのつまみの大きさ。**溝より広くする**（掴む場所が分かるように）。 */
    static constexpr int thumbLongSide = 18;
    static constexpr int thumbShortSide = 11;

    /** 溝の太さ。 */
    static constexpr int grooveThickness = 5;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerLookAndFeel)
};
