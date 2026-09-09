#include "LevelMeterComponent.h"
#include <juce_audio_basics/juce_audio_basics.h>  // juce::Decibels（GUIモジュールには含まれない）
#include "AppColours.h"
#include "Branding.h"       // 8.177：レトロ表示かどうか（Phase 218）
#include "Utf8.h"

void LevelMeterComponent::decayPeaks (double nowMs)
{
    // 初回は起点だけ決めて何もしない（前回時刻が0のままだと、経過が数十年になる）
    if (lastUpdateMs <= 0.0)
    {
        lastUpdateMs = nowMs;
        return;
    }

    const double elapsedSeconds = juce::jmax (0.0, (nowMs - lastUpdateMs) / 1000.0);
    lastUpdateMs = nowMs;

    for (int ch = 0; ch < 2; ++ch)
    {
        if (peaks[ch] <= 0.0f)
            continue;

        if (nowMs - peakSetTimeMs[ch] < peakHoldMs)
            continue;   // まだ保持時間の中

        // **dBで下げること。** 振幅を等速で引くと、大きいところでは一瞬で落ち、
        // 小さいところでは張り付いたまま止まって見える
        const float currentDb = juce::Decibels::gainToDecibels (peaks[ch], minimumDecibels);
        const float loweredDb = currentDb - peakDecayDbPerSecond * (float) elapsedSeconds;

        peaks[ch] = loweredDb <= minimumDecibels ? 0.0f
                                                  : juce::Decibels::decibelsToGain (loweredDb);
    }
}

void LevelMeterComponent::setLevels (float leftLevel, float rightLevel)
{
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const float previousPeaks[2] { peaks[0], peaks[1] };

    // **ピークの減衰は、棒が動いていなくても進める。** 「値が変わらなければ何もしない」で
    // 早期に戻ると、鳴り止んだ瞬間のピークが下がらないまま残る
    decayPeaks (nowMs);

    const float newLevels[2] { leftLevel, rightLevel };
    bool needsRepaint = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        // 微小な変化で毎回再描画すると無駄なので、目に見える差があるときだけ更新する
        if (std::abs (levels[ch] - newLevels[ch]) >= 0.001f)
            needsRepaint = true;

        levels[ch] = newLevels[ch];

        if (newLevels[ch] >= peaks[ch])
        {
            peaks[ch] = newLevels[ch];
            peakSetTimeMs[ch] = nowMs;
        }

        // 減衰したぶんも「見た目が変わった」に数える。**ここを忘れると印が動かない**
        if (std::abs (previousPeaks[ch] - peaks[ch]) >= 0.001f)
            needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void LevelMeterComponent::resetPeak()
{
    peaks[0] = peaks[1] = 0.0f;
    repaint();
}

void LevelMeterComponent::mouseDown (const juce::MouseEvent& e)
{
    // **左クリックだけ。** 右クリックには何も割り当てていないので、
    // 押した拍子にピークが消えることが無いようにしておく（Phase 64／8.25の表）
    if (e.mods.isPopupMenu())
        return;

    resetPeak();
}

float LevelMeterComponent::levelToProportion (float level)
{
    if (level <= 0.0f)
        return 0.0f;

    const float decibels = juce::Decibels::gainToDecibels (level, minimumDecibels);
    return juce::jlimit (0.0f, 1.0f, (decibels - minimumDecibels) / (0.0f - minimumDecibels));
}

void LevelMeterComponent::setShowPeakText (bool shouldShow)
{
    if (showPeakText == shouldShow)
        return;

    showPeakText = shouldShow;
    repaint();
}

void LevelMeterComponent::fillLevelBar (juce::Graphics& g, juce::Rectangle<float> filled,
                                         juce::Rectangle<float> whole, bool isVertical)
{
    // 8.177：**Emberは段組みで塗る**（Phase 218／レトロ表示）。
    //
    // 色は変えていません。**塗り方だけ**です——連続した棒か、
    // 一定間隔で切った段か。段のほうが「いまどのあたりか」を数えて読めます
    // （本物のLEDメーターがそう見えるのも同じ理由）。
    if (! Branding::retroUI)
    {
        g.fillRoundedRectangle (filled, AppColours::corner (2.0f));
        return;
    }

    // **段の大きさは棒の太さから決めます。** 固定の段数にすると、
    // 細いメーター（トラックヘッダー）で段が潰れて見えなくなります
    const float thickness = isVertical ? whole.getWidth() : whole.getHeight();
    const float step = juce::jmax (3.0f, thickness * 0.62f);
    const float gap = juce::jmax (1.0f, step * 0.28f);

    if (isVertical)
    {
        for (float y = whole.getBottom() - step; y > whole.getY() - step; y -= step)
        {
            const auto segment = juce::Rectangle<float> (whole.getX(), juce::jmax (y, whole.getY()),
                                                          whole.getWidth(), step - gap)
                                     .getIntersection (filled);

            if (! segment.isEmpty())
                g.fillRect (segment);
        }

        return;
    }

    for (float x = whole.getX(); x < whole.getRight(); x += step)
    {
        const auto segment = juce::Rectangle<float> (x, whole.getY(), step - gap, whole.getHeight())
                                 .getIntersection (filled);

        if (! segment.isEmpty())
            g.fillRect (segment);
    }
}

void LevelMeterComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // 仕様書5.7：ピークのdB表示（Phase 59／8.1のC3）。
    // **棒の領域を先に削っておく。** 後から重ねると、数字が棒に潰される
    const bool drawText = showPeakText
                           && bounds.getWidth() >= (vertical ? minimumWidthForVerticalText
                                                             : minimumWidthForHorizontalText)
                           && bounds.getHeight() > peakTextHeight + 8;

    juce::Rectangle<int> textArea;

    if (drawText)
        textArea = vertical ? bounds.removeFromBottom (peakTextHeight)
                            : bounds.removeFromRight (juce::jmin (34, bounds.getWidth() / 3));

    auto area = bounds.toFloat();

    g.setColour (AppColours::background);
    g.fillRoundedRectangle (area, AppColours::corner (3.0f));

    g.setColour (AppColours::border);
    g.drawRoundedRectangle (area.reduced (0.5f), AppColours::corner (3.0f), 1.0f);

    const float barGap = 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float proportion = levelToProportion (levels[ch]);
        const float peakProportion = levelToProportion (peaks[ch]);

        // 0dB付近（クリップ直前）はオレンジ、振り切りは赤で警告する（設計書2.6の配色ルール）
        const auto colour = levels[ch] >= 0.99f ? juce::Colours::red
                                                 : (levels[ch] > 0.7f ? AppColours::orange : AppColours::purple);

        // ピークの印は、棒が0でも残る（「さっきどこまで出たか」を見るためのもの）
        const auto peakColour = peaks[ch] >= 0.99f ? juce::Colours::red
                                                    : (peaks[ch] > 0.7f ? AppColours::orange
                                                                        : AppColours::textPrimary);

        if (vertical)
        {
            // 縦向き：2本の棒を左右に並べ、下から上へ伸ばす
            const float barWidth = (area.getWidth() - barGap * 3.0f) * 0.5f;

            auto barArea = juce::Rectangle<float> (area.getX() + barGap + (barWidth + barGap) * (float) ch,
                                                    area.getY() + barGap,
                                                    barWidth,
                                                    area.getHeight() - barGap * 2.0f);

            if (proportion > 0.0f)
            {
                g.setColour (colour);
                fillLevelBar (g, barArea.withTop (barArea.getBottom() - barArea.getHeight() * proportion),
                               barArea, true);
            }

            if (peakProportion > 0.0f)
            {
                const float peakY = barArea.getBottom() - barArea.getHeight() * peakProportion;

                g.setColour (peakColour);
                g.fillRect (barArea.getX(), juce::jmax (barArea.getY(), peakY - 1.0f), barArea.getWidth(), 2.0f);
            }
        }
        else
        {
            // 横向き：2本の棒を上下に並べ、左から右へ伸ばす
            const float barHeight = (area.getHeight() - barGap * 3.0f) * 0.5f;

            auto barArea = juce::Rectangle<float> (area.getX() + barGap,
                                                    area.getY() + barGap + (barHeight + barGap) * (float) ch,
                                                    area.getWidth() - barGap * 2.0f,
                                                    barHeight);

            if (proportion > 0.0f)
            {
                g.setColour (colour);
                fillLevelBar (g, barArea.withWidth (barArea.getWidth() * proportion), barArea, false);
            }

            if (peakProportion > 0.0f)
            {
                const float peakX = barArea.getX() + barArea.getWidth() * peakProportion;

                g.setColour (peakColour);
                g.fillRect (juce::jmin (peakX - 1.0f, barArea.getRight() - 2.0f), barArea.getY(),
                             2.0f, barArea.getHeight());
            }
        }
    }

    if (! drawText)
        return;

    // **左右のうち大きいほう**を出す。2つ並べると桁が読めないうえ、
    // 「割れていないか」を見るときに欲しいのは大きいほうの値
    const float loudestPeak = juce::jmax (peaks[0], peaks[1]);

    g.setColour (loudestPeak >= 0.99f ? juce::Colours::red : AppColours::textSecondary);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText (loudestPeak <= 0.0f
                    ? utf8 ("--")
                    : juce::String (juce::Decibels::gainToDecibels (loudestPeak, minimumDecibels), 1),
                 textArea, juce::Justification::centred);
}

void LevelMeterComponent::setVertical (bool shouldBeVertical)
{
    if (vertical == shouldBeVertical)
        return;

    vertical = shouldBeVertical;
    repaint();
}
