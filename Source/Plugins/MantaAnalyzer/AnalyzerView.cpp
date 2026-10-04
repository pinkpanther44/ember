#include "AnalyzerView.h"

#include "../../AppColours.h"

#include <cmath>

namespace
{
    /** 仕様書3.2の目盛り。 */
    constexpr double frequencyTicks[]
    {
        20, 30, 40, 50, 60, 80, 100, 200, 300, 400, 500, 600,
        1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 20000
    };

    juce::String frequencyLabel (double hz)
    {
        if (hz >= 1000.0)
            return juce::String (hz / 1000.0, hz >= 10000.0 || std::fmod (hz, 1000.0) == 0.0 ? 0 : 1) + "k";

        return juce::String ((int) std::round (hz));
    }

    /** 塗りの色を、画素へ書ける形で持つ（乗算済みのRGBA）。 */
    struct Rgba
    {
        float r = 0, g = 0, b = 0, a = 0;

        static Rgba of (juce::Colour c)
        {
            const float a = c.getFloatAlpha();
            return { c.getFloatRed() * a, c.getFloatGreen() * a, c.getFloatBlue() * a, a };
        }

        Rgba lerp (const Rgba& o, float t) const
        {
            return { r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t };
        }

        Rgba scaled (float k) const { return { r * k, g * k, b * k, a * k }; }

        juce::PixelARGB pixel() const
        {
            auto to8 = [] (float v) { return (juce::uint8) juce::jlimit (0, 255, (int) (v * 255.0f + 0.5f)); };
            return juce::PixelARGB (to8 (a), to8 (r), to8 (g), to8 (b));
        }
    };

    /** 上限を超えたぶんを、何 dB で最も明るくするか（仕様書3.4「超過量が大きいほど明るく」）。 */
    constexpr float excessFullDb = 12.0f;

    const char* const noteNames[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

AnalyzerView::AnalyzerView()
{
    colours = AnalyzerThemeColors::fromAccent (juce::Colours::mediumpurple);
    setOpaque (true);
}

void AnalyzerView::setSettings (const AnalyzerSettings& newSettings)
{
    settings = newSettings;
    repaint();
}

void AnalyzerView::setColours (const AnalyzerThemeColors& newColours)
{
    colours = newColours;
    repaint();
}

juce::Rectangle<int> AnalyzerView::getPlotBounds() const
{
    return getLocalBounds().withTrimmedRight (axisWidth)
                           .withTrimmedBottom (frequencyStripHeight)
                           .withTrimmedTop (6)
                           .withTrimmedLeft (4);
}

float AnalyzerView::dbToY (float db) const
{
    const auto plot = getPlotBounds();
    return (float) plot.getY() + (settings.levelTopDb - db) / settings.levelRangeDb * (float) plot.getHeight();
}

float AnalyzerView::yToDb (float y) const
{
    const auto plot = getPlotBounds();
    return settings.levelTopDb - (y - (float) plot.getY()) / (float) plot.getHeight() * settings.levelRangeDb;
}

double AnalyzerView::xToFrequency (float plotX) const
{
    const auto plot = getPlotBounds();
    const double fMin = frame != nullptr ? frame->frequencyMin : settings.frequencyMin;
    const double fMax = frame != nullptr ? frame->frequencyMax : settings.frequencyMax;

    if (plot.getWidth() <= 1)
        return fMin;

    return fMin * std::pow (fMax / fMin, (double) plotX / (double) (plot.getWidth() - 1));
}

float AnalyzerView::frequencyToX (double hz) const
{
    const auto plot = getPlotBounds();
    const double fMin = frame != nullptr ? frame->frequencyMin : settings.frequencyMin;
    const double fMax = frame != nullptr ? frame->frequencyMax : settings.frequencyMax;

    return (float) plot.getX() + (float) ((plot.getWidth() - 1) * std::log (hz / fMin) / std::log (fMax / fMin));
}

bool AnalyzerView::isOnAxis (juce::Point<int> p) const
{
    return p.x >= getWidth() - axisWidth && p.y < getHeight() - frequencyStripHeight;
}

//==============================================================================

void AnalyzerView::paint (juce::Graphics& g)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();

    g.fillAll (colours.background);

    const auto plot = getPlotBounds();

    if (plot.getWidth() > 2 && plot.getHeight() > 2)
    {
        paintGrid (g, plot);

        const bool ready = frame != nullptr && frame->hasData
                            && (int) frame->average.size() == plot.getWidth();

        if (ready)
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (plot);

            if (settings.rangeVisible)
                paintRangeBand (g, plot);

            if (settings.curveVisible)
                paintFills (g, plot);

            paintLines (g, plot);
            paintReadout (g, plot);
        }
    }

    lastPaintMs = juce::Time::getMillisecondCounterHiRes() - started;
}

void AnalyzerView::paintGrid (juce::Graphics& g, juce::Rectangle<int> plot)
{
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    // 縦の目盛り（10 dB ごと）と、右端の数字
    const float bottomDb = settings.levelTopDb - settings.levelRangeDb;

    for (float db = std::ceil (bottomDb / 10.0f) * 10.0f; db <= settings.levelTopDb + 0.01f; db += 10.0f)
    {
        const float y = dbToY (db);

        if (settings.gridVisible)
        {
            g.setColour (colours.grid);
            g.drawHorizontalLine ((int) std::round (y), (float) plot.getX(), (float) plot.getRight());
        }

        g.setColour (colours.gridText);
        g.drawText (juce::String ((int) db), plot.getRight() + 4, (int) y - 7, axisWidth - 8, 14,
                    juce::Justification::centredLeft, false);
    }

    // 横の目盛り（周波数）と、下の数字。**数字が重なるところは飛ばす**
    float lastLabelRight = -1000.0f;

    for (const double hz : frequencyTicks)
    {
        const double fMin = frame != nullptr ? frame->frequencyMin : settings.frequencyMin;
        const double fMax = frame != nullptr ? frame->frequencyMax : settings.frequencyMax;

        if (hz < fMin || hz > fMax)
            continue;

        const float x = frequencyToX (hz);

        if (settings.gridVisible)
        {
            g.setColour (colours.grid);
            g.drawVerticalLine ((int) std::round (x), (float) plot.getY(), (float) plot.getBottom());
        }

        const auto text = frequencyLabel (hz);
        const float w = 26.0f;

        if (x - w * 0.5f > lastLabelRight + 2.0f)
        {
            g.setColour (colours.gridText);
            g.drawText (text, juce::Rectangle<float> (x - w * 0.5f, (float) plot.getBottom() + 2.0f,
                                                      w, (float) frequencyStripHeight - 4.0f),
                        juce::Justification::centred, false);
            lastLabelRight = x + w * 0.5f;
        }
    }
}

void AnalyzerView::paintRangeBand (juce::Graphics& g, juce::Rectangle<int> plot)
{
    // レイヤー3：上限と下限のあいだ。**規格の外は不透明度を半分**（仕様書5.4）
    const int width = plot.getWidth();
    int start = 0;

    while (start < width)
    {
        const bool outside = frame->outside[(size_t) start] != 0;
        int end = start;

        while (end + 1 < width && (frame->outside[(size_t) end + 1] != 0) == outside)
            ++end;

        juce::Path band;
        const int from = juce::jmax (0, start - 1);   // 隣の区間と1点重ねて、継ぎ目を出さない

        for (int x = from; x <= end; ++x)
        {
            const float px = (float) (plot.getX() + x);
            const float py = dbToY (frame->upper[(size_t) x]);

            if (x == from) band.startNewSubPath (px, py);
            else           band.lineTo (px, py);
        }

        for (int x = end; x >= from; --x)
            band.lineTo ((float) (plot.getX() + x), dbToY (frame->lower[(size_t) x]));

        band.closeSubPath();

        const float alpha = settings.rangeOpacity * (outside ? 0.5f : 1.0f);
        g.setColour (colours.rangeBand.withAlpha (alpha));
        g.fillPath (band);

        start = end + 1;
    }
}

void AnalyzerView::paintFills (juce::Graphics& g, juce::Rectangle<int> plot)
{
    const int w = plot.getWidth();
    const int h = plot.getHeight();

    if (fillImage.getWidth() != w || fillImage.getHeight() != h)
        fillImage = juce::Image (juce::Image::ARGB, w, h, true);
    else
        fillImage.clear (fillImage.getBounds());

    const auto below = Rgba::of (colours.belowLower);
    const auto rangeBottom = Rgba::of (colours.rangeBottom);
    const auto rangeTop = Rgba::of (colours.rangeTop);
    const auto excessNear = Rgba::of (colours.excessNear);
    const auto excessFar = Rgba::of (colours.excessFar);
    const auto fillBottom = Rgba::of (colours.fillBottom);
    const auto fillTop = Rgba::of (colours.fillTop);

    const bool withRange = settings.rangeVisible;
    const bool highlight = settings.highlightDeviation;
    const float pxPerDb = (float) h / settings.levelRangeDb;
    const float top = (float) plot.getY();

    {
        juce::Image::BitmapData bitmap (fillImage, juce::Image::BitmapData::writeOnly);

        for (int x = 0; x < w; ++x)
        {
            const float yCurve = dbToY (frame->average[(size_t) x]) - top;

            if (yCurve >= (float) h)
                continue;

            // レンジの線は、レンジを出すときだけ読む（出さないときは中身が入っていない。8.330）
            const float yLow = withRange ? dbToY (frame->lower[(size_t) x]) - top : 0.0f;
            const float yUp = withRange ? dbToY (frame->upper[(size_t) x]) - top : 0.0f;
            const float excessSpan = juce::jmax (1.0f, excessFullDb * pxPerDb);

            const int firstRow = juce::jmax (0, (int) std::floor (yCurve));

            for (int y = firstRow; y < h; ++y)
            {
                const float row = (float) y + 0.5f;
                Rgba colour;

                if (! withRange)
                {
                    // レンジを出さないとき：下端の色から上端の色へ（ダークは暗→明、ライトは淡→アクセント。8.330）
                    colour = fillBottom.lerp (fillTop, 1.0f - row / (float) h);
                }
                else if (row >= yLow)
                {
                    colour = below;
                }
                else if (row >= yUp)
                {
                    // レンジ内：下限（暗め）→ 上限（明るめ）
                    const float t = (yLow - row) / juce::jmax (1.0f, yLow - yUp);
                    colour = rangeBottom.lerp (rangeTop, juce::jlimit (0.0f, 1.0f, t));
                }
                else if (highlight)
                {
                    // 過多：上限線から離れるほど明るく（仕様書3.4）
                    const float t = juce::jlimit (0.0f, 1.0f, (yUp - row) / excessSpan);
                    colour = excessNear.lerp (excessFar, t);
                }
                else
                {
                    colour = rangeTop;
                }

                // **上端の1画素は、掛かっているぶんだけ塗る**（ギザギザを出さない）
                if (y == firstRow)
                    colour = colour.scaled (juce::jlimit (0.0f, 1.0f, (float) (y + 1) - yCurve));

                *reinterpret_cast<juce::PixelARGB*> (bitmap.getPixelPointer (x, y)) = colour.pixel();
            }
        }
    }

    // **不透明度を戻してから描くこと。** JUCEは画像を「いまの色の不透明度」で描くので、
    // 直前のレンジ帯（25%）のまま描くと、塗り全体が4分の1の濃さになっていました
    g.setOpacity (1.0f);
    g.drawImageAt (fillImage, plot.getX(), plot.getY());
}

juce::Path AnalyzerView::makeLine (const std::vector<float>& values, juce::Rectangle<int> plot, int from, int to) const
{
    juce::Path path;

    // **下へはみ出す値は、下端の少し下で止める**（無音で −120 dB になっても、線が遠くへ飛ばない）
    const float floorY = (float) plot.getBottom() + 3.0f;

    for (int x = from; x <= to; ++x)
    {
        const float px = (float) (plot.getX() + x);
        const float py = juce::jmin (floorY, dbToY (values[(size_t) x]));

        if (x == from) path.startNewSubPath (px, py);
        else           path.lineTo (px, py);
    }

    return path;
}

void AnalyzerView::paintLines (juce::Graphics& g, juce::Rectangle<int> plot)
{
    const int last = plot.getWidth() - 1;
    const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;

    // レイヤー6：境界線（1.5 px）。**規格の外は破線**（仕様書5.4）
    if (settings.rangeVisible)
    {
        g.setColour (colours.boundary);

        for (const auto* values : { &frame->upper, &frame->lower })
        {
            int start = 0;

            while (start <= last)
            {
                const bool outside = frame->outside[(size_t) start] != 0;
                int end = start;

                while (end + 1 <= last && (frame->outside[(size_t) end + 1] != 0) == outside)
                    ++end;

                const auto line = makeLine (*values, plot, juce::jmax (0, start - 1), end);
                juce::PathStrokeType stroke (1.5f, joint, juce::PathStrokeType::rounded);

                if (outside)
                {
                    juce::Path dashed;
                    const float dashes[] { 4.0f, 3.0f };
                    stroke.createDashedStroke (dashed, line, dashes, 2);
                    g.fillPath (dashed);
                }
                else
                {
                    g.strokePath (line, stroke);
                }

                start = end + 1;
            }
        }
    }

    // リアルタイム曲線（任意）
    if (settings.realtimeVisible)
    {
        g.setColour (colours.realtime);
        g.strokePath (makeLine (frame->realtime, plot, 0, last), juce::PathStrokeType (1.0f, joint));
    }

    // レイヤー7：スペクトラム曲線（2 px）
    if (settings.curveVisible)
    {
        g.setColour (colours.curve);
        g.strokePath (makeLine (frame->average, plot, 0, last),
                      juce::PathStrokeType (2.0f, joint, juce::PathStrokeType::rounded));
    }

    // レイヤー8：ピーク曲線（1 px、文字色 70%）
    if (settings.peakVisible)
    {
        g.setColour (colours.peak);
        g.strokePath (makeLine (frame->peak, plot, 0, last), juce::PathStrokeType (1.0f, joint));
    }
}

juce::String AnalyzerView::getReadoutText (int plotX) const
{
    if (frame == nullptr || ! frame->hasData || ! juce::isPositiveAndBelow (plotX, (int) frame->average.size()))
        return {};

    const double hz = xToFrequency ((float) plotX);

    juce::String text = hz >= 1000.0 ? juce::String (hz / 1000.0, 2) + " kHz"
                                     : juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz";

    // 音名（設計書6.6）：n = 69 + 12·log2(f/440) を丸め、差をセントで
    const double exact = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int note = (int) std::round (exact);
    const int cents = (int) std::round ((exact - note) * 100.0);

    if (note >= 0)
    {
        text << "\n" << noteNames[note % 12] << (note / 12 - 1)
             << " " << (cents >= 0 ? "+" : "") << cents << " cent";
    }

    const float level = frame->average[(size_t) plotX];
    text << "\n" << (level <= -119.0f ? juce::String ("-inf") : juce::String (level, 1)) << " dB";

    if (settings.rangeVisible)
    {
        const float lo = frame->lower[(size_t) plotX];
        const float up = frame->upper[(size_t) plotX];

        if (level > up)
            text << "\nUpper +" << juce::String (level - up, 1) << " dB";
        else if (level < lo)
            text << "\nLower " << juce::String (level - lo, 1) << " dB";
        else
            text << "\nIn range";
    }

    return text;
}

void AnalyzerView::paintReadout (juce::Graphics& g, juce::Rectangle<int> plot)
{
    if (cursorX < 0)
        return;

    const int px = cursorX - plot.getX();

    if (! juce::isPositiveAndBelow (px, plot.getWidth()))
        return;

    g.setColour (colours.text.withAlpha (0.35f));
    g.drawVerticalLine (cursorX, (float) plot.getY(), (float) plot.getBottom());

    const auto text = getReadoutText (px);
    const auto lines = juce::StringArray::fromLines (text);

    const juce::Font font (juce::FontOptions (11.0f));
    g.setFont (font);

    int textWidth = 0;
    for (const auto& line : lines)
        textWidth = juce::jmax (textWidth, (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, line)));

    const int boxW = textWidth + 12;
    const int boxH = lines.size() * 14 + 8;

    // カーソルの右に置き、はみ出すなら左へ
    int boxX = cursorX + 10;
    if (boxX + boxW > plot.getRight())
        boxX = cursorX - 10 - boxW;

    const juce::Rectangle<int> box (boxX, plot.getY() + 8, boxW, boxH);

    g.setColour (colours.background.withAlpha (0.85f));
    g.fillRoundedRectangle (box.toFloat(), AppColours::corner (4.0f));
    g.setColour (colours.text.withAlpha (0.25f));
    g.drawRoundedRectangle (box.toFloat(), AppColours::corner (4.0f), 1.0f);

    g.setColour (colours.text);

    for (int i = 0; i < lines.size(); ++i)
        g.drawText (lines[i], box.getX() + 6, box.getY() + 4 + i * 14, boxW - 12, 14,
                    juce::Justification::centredLeft, false);
}

//==============================================================================

void AnalyzerView::mouseMove (const juce::MouseEvent& e)
{
    const auto plot = getPlotBounds();
    const int newX = plot.contains (e.getPosition()) ? e.x : -1;

    if (newX != cursorX)
    {
        cursorX = newX;
        repaint();
    }

    setMouseCursor (isOnAxis (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor
                                               : juce::MouseCursor::NormalCursor);
}

void AnalyzerView::mouseExit (const juce::MouseEvent&)
{
    if (cursorX >= 0)
    {
        cursorX = -1;
        repaint();
    }
}

void AnalyzerView::mouseDown (const juce::MouseEvent& e)
{
    draggingAxis = isOnAxis (e.getPosition());
    dragStartTop = settings.levelTopDb;

    // 仕様書6.2：グラフをクリック → ピーク曲線をリセット
    if (! draggingAxis && getPlotBounds().contains (e.getPosition()) && onResetPeaks != nullptr)
        onResetPeaks();
}

void AnalyzerView::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingAxis || onLevelViewChanged == nullptr)
        return;

    // 下へ引く → 目盛りが下がる（上端の値が大きくなる）
    const float dbPerPixel = settings.levelRangeDb / (float) juce::jmax (1, getPlotBounds().getHeight());
    const float top = juce::jlimit (-24.0f, 12.0f, dragStartTop + (float) e.getDistanceFromDragStartY() * dbPerPixel);

    onLevelViewChanged (std::round (top), settings.levelRangeDb);
}

void AnalyzerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 縦軸をダブルクリック → 初期値へ
    if (isOnAxis (e.getPosition()) && onLevelViewChanged != nullptr)
    {
        const AnalyzerSettings defaults;
        onLevelViewChanged (defaults.levelTopDb, defaults.levelRangeDb);
    }
}

void AnalyzerView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isOnAxis (e.getPosition()) || onLevelViewChanged == nullptr || wheel.deltaY == 0.0f)
        return;

    // 上へ回す → 幅を狭く（拡大）
    const auto& choices = AnalyzerSettings::levelRangeChoices();
    int index = choices.indexOf (settings.levelRangeDb);
    index = juce::jlimit (0, choices.size() - 1, index + (wheel.deltaY > 0.0f ? -1 : 1));

    onLevelViewChanged (settings.levelTopDb, choices[index]);
}
