#include "MixerLookAndFeel.h"
#include "AppColours.h"

void MixerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    // 正円で描く（幅と高さが違っても潰さない）。**中央に寄せる**
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();

    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto outlineColour = slider.findColour (juce::Slider::rotarySliderOutlineColourId);

    //--------------------------------------------------------------------------
    // 8.298：**弧を付けます**（Phase 291／本人の指定）。
    //
    // Phase 290までは「塗った円＋指針1本」でした。指針は**向きしか**示さないので、
    // **どこまで回せるのか**と**いまどれくらい振ってあるのか**が、
    // 数値（下の欄）を読むまで分かりません。
    //
    // 弧の描き方は**内蔵プラグインのつまみと同じ**です（`MantaKnobLookAndFeel`）
    // ——同じ形のものが画面によって違う描かれ方をするのは避けます（1.27）。
    // 色だけ`AppColours`から取ります（あちらは`MantaTheme`）。
    //
    // 本人の指定は「**弧を付ける分、ノブのサイズは縮小してみよう**」。
    // 器の大きさは変えず、**本体の円を弧のぶん内側へ**引いてあります
    // ——器を縮めると、下の数値やボタンとの間隔まで動きます。
    const float arcRadius = diameter * 0.5f - arcThickness * 0.5f;
    const float bodyRadius = juce::jmax (4.0f, arcRadius - arcThickness * 0.5f - arcGap);

    // ① 弧の地（端から端まで）。**薄く残すこと**——
    //    どこまで回せるのかが見えないと、端に当たったのか壊れたのか分からない
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (AppColours::border);
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // ② いまの値ぶんの弧。
    //
    // **真ん中が0のつまみは12時から伸ばします。** パンは-1〜+1で、
    // 左端から塗ると「中央」が半分塗られた状態になり、**振っていないのに振って見えます**。
    //
    // 判定は**範囲から**します（`MantaKnobLookAndFeel`は呼ぶ側が印を付ける形ですが、
    // こちらは`juce::Slider`をそのまま使うので、印を付け忘れる口を作りたくない）。
    // 下端が負・上端が正で、**同じだけ振れる**ものが「真ん中が0」です
    const double lo = slider.getMinimum();
    const double hi = slider.getMaximum();
    const bool bipolar = lo < 0.0 && hi > 0.0 && std::abs (lo + hi) < 1.0e-6;

    const float originAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) * 0.5f : rotaryStartAngle;

    if (std::abs (angle - originAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                              juce::jmin (originAngle, angle), juce::jmax (originAngle, angle), true);

        g.setColour (fillColour);
        g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    // ③ 本体。**塗ること**：溝や下の文字が透けると、指針が読みにくくなる
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (AppColours::panel);
    g.fillEllipse (body);

    // 外周。**塗りより暗い色**にして、小さくても輪郭が残るようにする
    g.setColour (outlineColour.isTransparent() ? AppColours::border : outlineColour);
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    //--------------------------------------------------------------------------
    // ④ 8.62：**中心から外へ伸びる指針**（Phase 100）。
    // 扇形の塗りだと16px角ではつぶれて向きが読めないので、線1本にしてある
    juce::Path pointer;
    const float pointerThickness = juce::jmax (1.6f, bodyRadius * 0.20f);

    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -bodyRadius * 0.86f,
                                  pointerThickness, bodyRadius * 0.62f,
                                  pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle)
                                .translated (centre.x, centre.y));

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
