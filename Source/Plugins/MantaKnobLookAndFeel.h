#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MantaTheme.h"

//==============================================================================
/**
    Manta EQ のつまみ（Phase 206／本人の要望）。

    ### 本体の`MixerLookAndFeel`と何が違うか

    本体のつまみは**トラックヘッダーの16px角でも向きが読める**ことを狙って、
    「地の円＋指針1本」にしてあります（8.62）。

    プラグインのつまみは**60px近くあります**。そこまで小さくならないので、
    **弧で量を出す**ほうが読み取りが速くなります——
    「いまどのくらいか」が、指針の角度を目で追わなくても分かります。

    ```
        ╭──╮      外側の弧：0から現在値まで塗る
       ╱ ┃  ╲     内側の円：地（弧のぶんだけ小さい）
       ╲    ╱     真ん中の線：指針
        ╰──╯
    ```

    ### 色は呼び出し側が決める

    `juce::Slider::rotarySliderFillColourId`を弧と指針に使います。

    - バンドのつまみ … **そのバンドの色**（＝ステレオ配置の色。`MantaTheme::bandColour()`）
    - 出力セクション … パープル

    ### 真ん中から伸びる弧

    Gain・Pan・M/Sのように**真ん中が0**のつまみは、
    弧を左端からではなく**12時から**塗ります（`bipolarProperty`をtrueに）。

    真ん中から塗らないと、「-3dB」と「+3dB」が
    **まったく違う長さの弧**になって、対称に見えません。
*/
class MantaKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MantaKnobLookAndFeel() { setColourScheme (AppColours::createColourScheme()); }

    /** つまみに `getProperties().set (bipolarProperty, true)` を入れると、
        弧を12時から塗ります。 */
    static const juce::Identifier& bipolarProperty();

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider& slider) override;

    /** 8.177：**つまみの数値も棒で**（Phase 219）。

        つまみの下の数値欄は`juce::Label`なので、ここを通ります。
        **1箇所で3つのプラグイン全部が変わります**（Manta EQ・Comp・Synth）。
        Manta Studioでは`SevenSegment::canDraw()`が常にfalseなので、今までどおりです。 */
    void drawLabel (juce::Graphics& g, juce::Label& label) override;

    /** 弧の太さ。 */
    static constexpr float arcThickness = 3.0f;

    /** 弧と本体のあいだの隙間。 */
    static constexpr float arcGap = 3.0f;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaKnobLookAndFeel)
};
