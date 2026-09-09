#include "MantaKnobLookAndFeel.h"

#include "../SegmentDisplay.h"   // 8.177：数字を棒で描く（Phase 219）
#include <cmath>

const juce::Identifier& MantaKnobLookAndFeel::bipolarProperty()
{
    static const juce::Identifier id { "mantaEqBipolarKnob" };

    return id;
}

void MantaKnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosProportional, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider& slider)
{
    // 正円で描く（幅と高さが違っても潰さない）
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();

    const float arcRadius = diameter * 0.5f - arcThickness * 0.5f;
    const float bodyRadius = juce::jmax (4.0f, arcRadius - arcThickness * 0.5f - arcGap);

    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();
    const auto accent = enabled ? fillColour : fillColour.withMultipliedAlpha (0.45f);

    //--------------------------------------------------------------------------
    // ① 弧の地（端から端まで）。**薄く残すこと**——
    //    どこまで回せるのかが見えないと、端に当たったのか壊れたのか分からない
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (MantaTheme::grid().withMultipliedAlpha (2.2f));
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    //--------------------------------------------------------------------------
    // ② いまの値ぶんの弧。真ん中が0のつまみは**12時から**伸ばす
    const bool bipolar = (bool) slider.getProperties().getWithDefault (bipolarProperty(), false);
    const float originAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);

        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    // ③ 本体。**塗ること**：下の文字やカーブが透けると指針が読みにくい
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (MantaTheme::graphBackground());
    g.fillEllipse (body);

    g.setColour (MantaTheme::border().withMultipliedAlpha (enabled ? 1.0f : 0.5f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    //--------------------------------------------------------------------------
    // ④ 指針。**中心までは引かない**（真ん中が詰まると、小さいときに黒い点に見える）
    juce::Path pointer;
    const float thickness = juce::jmax (1.6f, bodyRadius * 0.16f);

    pointer.addRoundedRectangle (-thickness * 0.5f, -bodyRadius * 0.88f,
                                  thickness, bodyRadius * 0.55f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (accent);
    g.fillPath (pointer);
}

void MantaKnobLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // 8.177：**編集中は基底に任せる**（Phase 219）。
    // 打ち込んでいる最中の文字が棒になったら、何を書いているか分かりません
    if (label.isBeingEdited() || ! SevenSegment::canDraw (label.getText()))
    {
        juce::LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    SevenSegment::draw (g, label.getLocalBounds().toFloat(), label.getText(),
                         label.findColour (juce::Label::textColourId),
                         label.getJustificationType());
}
