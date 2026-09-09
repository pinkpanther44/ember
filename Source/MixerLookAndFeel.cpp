#include "MixerLookAndFeel.h"
#include "AppColours.h"

void MixerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    // 正円で描く（幅と高さが違っても潰さない）。**中央に寄せる**
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    const auto circle = juce::Rectangle<float> (diameter, diameter).withCentre (area.getCentre());

    const float radius = diameter * 0.5f;
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto outlineColour = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    // 地の円。**塗ること**：溝や下の文字が透けると、指針が読みにくくなる
    g.setColour (AppColours::panel);
    g.fillEllipse (circle);

    // 外周。**塗りより暗い色**にして、小さくても輪郭が残るようにする
    g.setColour (outlineColour.isTransparent() ? AppColours::border : outlineColour);
    g.drawEllipse (circle.reduced (0.75f), 1.5f);

    // 8.62：**中心から外へ伸びる指針**（Phase 100）。
    // 扇形の塗りだと16px角ではつぶれて向きが読めないので、線1本にしてある
    juce::Path pointer;
    const float pointerThickness = juce::jmax (1.6f, diameter * 0.11f);

    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -radius * 0.82f,
                                  pointerThickness, radius * 0.62f,
                                  pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                .translated (circle.getCentreX(), circle.getCentreY()));

    g.setColour (fillColour);
    g.fillPath (pointer);
}

void MixerLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // 回転式や2つまみの形はここでは扱わない（このアプリでは使っていない）。
    // **知らない形は基底へ渡すこと**：黙って何も描かないと、増やしたときに気づけない
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal
         && style != juce::Slider::LinearBar && style != juce::Slider::LinearBarVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                 minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const bool isVertical = (style == juce::Slider::LinearVertical
                              || style == juce::Slider::LinearBarVertical);

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto fillColour = slider.findColour (juce::Slider::trackColourId);
    const auto grooveColour = AppColours::background;

    //==========================================================================
    // 溝。**つまみが端まで行けるよう、両端につまみの半分ぶんの余白**を見込む
    // 8.117：**つまみを行の中へ収める**（Phase 152／改善案32）。
    //
    // つまみの長辺は18pxですが、**Consoleのセンド量の行は14px**しかありません。
    // はみ出したぶんが切れて、**つまみが見切れて**いました。
    // **置ける大きさは置く場所が決める**ので、ここで詰めます。
    //
    // 詰めるのは**長辺（＝溝と直交する側）**だけ。短辺を詰めると、
    // 掴める幅が狭くなって別の使いにくさになります
    const float thumbLong = (float) juce::jmin (thumbLongSide,
                                                 isVertical ? width : height);
    const float groove = (float) grooveThickness;

    auto grooveArea = isVertical
                         ? juce::Rectangle<float> (area.getCentreX() - groove * 0.5f, area.getY(),
                                                    groove, area.getHeight())
                         : juce::Rectangle<float> (area.getX(), area.getCentreY() - groove * 0.5f,
                                                    area.getWidth(), groove);

    g.setColour (grooveColour);
    g.fillRoundedRectangle (grooveArea, AppColours::corner (groove * 0.5f));
    g.setColour (AppColours::border);
    g.drawRoundedRectangle (grooveArea.reduced (0.5f), AppColours::corner (groove * 0.5f), 1.0f);

    //==========================================================================
    // 8.62：**いま出ている量を溝の中に塗る**（Phase 100）。
    // 縦は下から、横は左から伸ばす（フェーダーの手応えと向きを合わせる）
    auto filled = grooveArea;

    if (isVertical)
        filled = filled.withTop (juce::jlimit (grooveArea.getY(), grooveArea.getBottom(), sliderPos));
    else
        filled = filled.withRight (juce::jlimit (grooveArea.getX(), grooveArea.getRight(), sliderPos));

    if (! filled.isEmpty())
    {
        g.setColour (fillColour);
        g.fillRoundedRectangle (filled, AppColours::corner (groove * 0.5f));
    }

    //==========================================================================
    // 8.62：**つまみは長方形**（Phase 100）。
    //
    // 既定の円だと、縦フェーダーで「いまどの目盛りか」を指しにくいものでした
    // （円は上下の端が曖昧になる）。長方形なら端がはっきりします。
    // 真ん中の線は**掴む場所の目印**で、実機のフェーダーのつまみにならったもの
    const auto thumb = isVertical
                          ? juce::Rectangle<float> (thumbLong, (float) thumbShortSide)
                                .withCentre ({ area.getCentreX(), sliderPos })
                          : juce::Rectangle<float> ((float) thumbShortSide, thumbLong)
                                .withCentre ({ sliderPos, area.getCentreY() });

    g.setColour (AppColours::panel);
    g.fillRoundedRectangle (thumb, AppColours::corner (2.0f));
    g.setColour (fillColour);
    g.drawRoundedRectangle (thumb.reduced (0.5f), AppColours::corner (2.0f), 1.5f);

    // 真ん中の線。**つまみの向きと直交**させる（縦フェーダーなら横線）
    g.setColour (fillColour);

    if (isVertical)
        g.fillRect (juce::Rectangle<float> (thumb.getX() + 3.0f, thumb.getCentreY() - 0.75f,
                                             thumb.getWidth() - 6.0f, 1.5f));
    else
        g.fillRect (juce::Rectangle<float> (thumb.getCentreX() - 0.75f, thumb.getY() + 3.0f,
                                             1.5f, thumb.getHeight() - 6.0f));
}
