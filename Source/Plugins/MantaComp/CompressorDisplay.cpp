#include "CompressorDisplay.h"

#include "../MantaTheme.h"

#include <cmath>

namespace
{
    /** メーターの並び。**Inがいちばん左**（信号の流れと同じ順）。 */
    enum MeterIndex { inputMeter = 0, outputMeter, reductionMeter, numMeters };
}

//==============================================================================

CompressorDisplay::CompressorDisplay (MantaCompProcessor& processorToUse)
    : processor (processorToUse)
{
    startTimerHz (30);
}

CompressorDisplay::~CompressorDisplay()
{
    stopTimer();
}

void CompressorDisplay::timerCallback()
{
    const auto& engine = processor.getEngine();

    // **上がるのは即座、落ちるのはゆっくり**（Manta EQのメーターと同じ考え）
    auto follow = [] (float& displayed, float level, float fallDb)
    {
        displayed = level > displayed ? level : juce::jmax (level, displayed - fallDb);
    };

    for (int ch = 0; ch < 2; ++ch)
    {
        follow (displayedInputDb[ch], engine.getInputLevelDb (ch), 1.5f);
        follow (displayedOutputDb[ch], engine.getOutputLevelDb (ch), 1.5f);
    }

    // GRは**逆向き**（下へ伸びる）ので、戻るほうをゆっくりにする
    const float reduction = engine.getReductionDb();

    displayedReductionDb = reduction > displayedReductionDb
                             ? reduction
                             : juce::jmax (reduction, displayedReductionDb - 1.2f);

    repaint();
}

//==============================================================================

juce::Rectangle<float> CompressorDisplay::getGraphArea() const
{
    auto area = getLocalBounds().toFloat();

    area.removeFromRight ((float) (numMeters * (meterWidth + meterGap) + 4));

    return area.reduced (1.0f);
}

float CompressorDisplay::levelToX (float db) const
{
    const auto area = getGraphArea();

    return area.getX() + juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)) * area.getWidth();
}

float CompressorDisplay::levelToY (float db) const
{
    const auto area = getGraphArea();

    return area.getBottom() - juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb)) * area.getHeight();
}

float CompressorDisplay::xToLevel (float x) const
{
    const auto area = getGraphArea();

    if (area.getWidth() <= 0.0f)
        return minDb;

    return minDb + juce::jlimit (0.0f, 1.0f, (x - area.getX()) / area.getWidth()) * (maxDb - minDb);
}

float CompressorDisplay::yToLevel (float y) const
{
    const auto area = getGraphArea();

    if (area.getHeight() <= 0.0f)
        return minDb;

    const float proportion = 1.0f - (y - area.getY()) / area.getHeight();

    return minDb + juce::jlimit (0.0f, 1.0f, proportion) * (maxDb - minDb);
}

//==============================================================================

void CompressorDisplay::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::graphBackground());

    // **描く順に意味があります**（Phase 211で入れ替え／本人の要望）。
    //
    // Phase 210では伝達特性を後ろにしていましたが、**前**にしました——
    // 履歴は**動いているので目に入ります**が、カーブは動かないので、
    // 後ろにすると流れる波形に埋もれて読めなくなります。
    // 「いま何が起きているか」は動きだけで十分伝わります。
    drawGrid (g);
    drawHistory (g);
    drawTransferCurve (g);
    drawMeters (g);

    g.setColour (MantaTheme::border());
    g.drawRect (getGraphArea(), 1.0f);
}

void CompressorDisplay::drawHistory (juce::Graphics& g) const
{
    const auto area = getGraphArea();
    const int numColumns = (int) area.getWidth();

    if (numColumns <= 1)
        return;

    auto& buffer = historyBuffer;

    // **`paint()`で確保しない**ように、足りないときだけ広げる
    if ((int) buffer.size() < numColumns)
        buffer.resize ((size_t) numColumns);

    processor.getEngine().readHistory (buffer.data(), numColumns);

    //--------------------------------------------------------------------------
    // 入力と出力の折れ線を、同時に組み立てる（あいだを塗るため）
    juce::Path inputPath, outputPath;

    for (int column = 0; column < numColumns; ++column)
    {
        const float x = area.getX() + (float) column;
        const auto& frame = buffer[(size_t) column];

        if (column == 0)
        {
            inputPath.startNewSubPath (x, levelToY (frame.inputDb));
            outputPath.startNewSubPath (x, levelToY (frame.outputDb));
        }
        else
        {
            inputPath.lineTo (x, levelToY (frame.inputDb));
            outputPath.lineTo (x, levelToY (frame.outputDb));
        }
    }

    // ① 入力：うっすら塗って「波形」に見せる
    {
        auto filled = inputPath;
        filled.lineTo (area.getRight(), area.getBottom());
        filled.lineTo (area.getX(), area.getBottom());
        filled.closeSubPath();

        g.setColour (MantaTheme::textDim().withAlpha (0.20f));
        g.fillPath (filled);
    }

    // ② **削った量**＝入力と出力のあいだ。
    // 別の目盛りを作らずに済むのがこの描き方の要点（`CompressorDisplay.h`）。
    //
    // 入力の線を左から右へ辿り、**出力の線を右から左へ**辿って閉じます
    {
        juce::Path band = inputPath;

        for (int column = numColumns - 1; column >= 0; --column)
            band.lineTo (area.getX() + (float) column, levelToY (buffer[(size_t) column].outputDb));

        band.closeSubPath();

        g.setColour (AppColours::orange.withAlpha (0.34f));
        g.fillPath (band);
    }

    // ③ 線そのもの
    g.setColour (MantaTheme::textDim().withAlpha (0.85f));
    g.strokePath (inputPath, juce::PathStrokeType (1.0f));

    g.setColour (MantaTheme::accent());
    g.strokePath (outputPath, juce::PathStrokeType (1.6f));
}

void CompressorDisplay::drawGrid (juce::Graphics& g) const
{
    const auto area = getGraphArea();

    g.setFont (juce::Font (juce::FontOptions (9.0f)));

    // 横線とその数字。**数字は縦軸だけ**（`CompressorDisplay.h`——
    // 横軸は履歴では時間、カーブでは入力レベルの二役なので、数字を置けません）
    for (float db = minDb; db <= maxDb + 0.01f; db += 12.0f)
    {
        const float y = levelToY (db);

        g.setColour (MantaTheme::grid());
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        if (db > minDb + 0.01f && db < maxDb - 0.01f)
        {
            g.setColour (MantaTheme::textDim().withAlpha (0.75f));
            g.drawText (juce::String ((int) db),
                         juce::Rectangle<float> (area.getX() + 2.0f, y - 6.0f, (float) scaleWidth, 12.0f),
                         juce::Justification::centredLeft, false);
        }
    }

    // 1秒ごとの縦線。**右端がいま**なので、そこから左へ数える
    for (int column = (int) area.getWidth() - CompressorEngine::historyRateHz; column > 0;
         column -= CompressorEngine::historyRateHz)
    {
        g.setColour (MantaTheme::grid());
        g.drawVerticalLine ((int) (area.getX() + (float) column), area.getY(), area.getBottom());
    }
}

void CompressorDisplay::drawTransferCurve (juce::Graphics& g) const
{
    const auto area = getGraphArea();
    const auto settings = processor.getSettings();

    juce::Path path;
    bool started = false;

    for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
    {
        const float inputDb = xToLevel (x);

        // **音を出しているのと同じ関数**（1.27）
        const float outputDb = CompressorEngine::computeOutputDb (inputDb, settings.thresholdDb,
                                                                   settings.ratio, settings.kneeDb);
        const float y = levelToY (outputDb);

        if (! started)
        {
            path.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    if (! started)
        return;

    // 無圧縮の線（斜め45度）。**これが基準**なので薄く残す
    g.setColour (MantaTheme::gridStrong().withMultipliedAlpha (0.7f));
    g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

    // 伝達特性そのもの。**履歴の前に出す**ので、薄めずそのまま（`paint()`の順）
    g.setColour (MantaTheme::accent());
    g.strokePath (path, juce::PathStrokeType (2.2f));

    // Thresholdの横線。**縦軸と横軸が同じ目盛りなので、
    // この線はちょうど折れ目を通ります**——線1本で、履歴とカーブの両方が読めます
    const float thresholdY = levelToY (settings.thresholdDb);
    const float dashes[] { 4.0f, 4.0f };

    g.setColour (MantaTheme::curve().withAlpha (0.75f));
    g.drawDashedLine ({ area.getX(), thresholdY, area.getRight(), thresholdY },
                       dashes, juce::numElementsInArray (dashes), 1.0f);

    // いまの入力レベルの位置。**折れ線の上に丸を1つ**
    const float inputDb = juce::jmax (displayedInputDb[0], displayedInputDb[1]);

    if (inputDb > minDb)
    {
        const float outputDb = CompressorEngine::computeOutputDb (inputDb, settings.thresholdDb,
                                                                   settings.ratio, settings.kneeDb);

        const juce::Point<float> point { levelToX (inputDb), levelToY (outputDb) };

        // **圧縮しているあいだはオレンジ**（効いていることが一目で分かる）
        g.setColour (displayedReductionDb > 0.2f ? AppColours::orange : MantaTheme::accent());
        g.fillEllipse (point.x - 4.0f, point.y - 4.0f, 8.0f, 8.0f);

        g.setColour (MantaTheme::graphBackground());
        g.drawEllipse (point.x - 4.0f, point.y - 4.0f, 8.0f, 8.0f, 1.0f);
    }
}

void CompressorDisplay::drawMeters (juce::Graphics& g) const
{
    auto area = getLocalBounds().toFloat();

    auto meters = area.removeFromRight ((float) (numMeters * (meterWidth + meterGap)));

    const char* const captions[numMeters] { "In", "Out", "GR" };

    for (int index = 0; index < numMeters; ++index)
    {
        auto column = meters.removeFromLeft ((float) meterWidth);
        meters.removeFromLeft ((float) meterGap);

        auto bar = column.reduced (0.0f, 1.0f);
        bar.removeFromBottom (12.0f);

        g.setColour (MantaTheme::graphBackground().darker (0.4f));
        g.fillRect (bar);

        if (index == reductionMeter)
        {
            // 仕様書2-2の**最重要メーター**。0dBから**下へ**伸ばす
            // （「どれだけ削ったか」なので、増える向きが下）
            const float proportion = juce::jlimit (0.0f, 1.0f, displayedReductionDb / 24.0f);

            g.setColour (AppColours::orange);
            g.fillRect (bar.withHeight (bar.getHeight() * proportion));
        }
        else
        {
            const float* levels = index == inputMeter ? displayedInputDb : displayedOutputDb;
            const float halfWidth = bar.getWidth() * 0.5f;

            for (int ch = 0; ch < 2; ++ch)
            {
                const float proportion = juce::jlimit (0.0f, 1.0f, (levels[ch] + 60.0f) / 60.0f);
                const float height = bar.getHeight() * proportion;

                const juce::Rectangle<float> channelBar (bar.getX() + (float) ch * halfWidth,
                                                          bar.getBottom() - height,
                                                          halfWidth - 0.5f, height);

                // 0dBを超えたらオレンジ（Manta EQのメーターと同じ決まり）
                g.setColour (levels[ch] > -0.1f ? AppColours::orange : MantaTheme::accent());
                g.fillRect (channelBar);
            }
        }

        g.setColour (MantaTheme::border());
        g.drawRect (bar, 1.0f);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText (captions[index], column.withTop (bar.getBottom()), juce::Justification::centred, false);
    }
}

//==============================================================================

juce::RangedAudioParameter* CompressorDisplay::getParameter (const char* id) const
{
    return processor.getValueTreeState().getParameter (id);
}

void CompressorDisplay::mouseDown (const juce::MouseEvent& event)
{
    const auto settings = processor.getSettings();

    juce::ignoreUnused (event);

    dragStartRatio = settings.ratio;
    dragging = true;

    for (auto* id : { MantaCompParams::threshold, MantaCompParams::ratio })
        if (auto* parameter = getParameter (id))
            parameter->beginChangeGesture();
}

void CompressorDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    const auto area = getGraphArea();
    const auto offset = event.getOffsetFromDragStart();

    // 縦＝Threshold（Phase 210）。**掴んだ高さがそのまましきい値**——
    // オレンジの点線を持ち上げる操作です。
    // 重ねたことで縦軸がdBの1本道になったので、これがいちばん素直になりました
    if (auto* parameter = getParameter (MantaCompParams::threshold))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (
            juce::jlimit (MantaCompParams::minThresholdDb, MantaCompParams::maxThresholdDb,
                           yToLevel (event.position.y))));

    // 横＝Ratio。**右へ引くほど強く**
    if (area.getWidth() > 0.0f)
    {
        const float factor = std::pow (2.0f, (float) offset.x / 80.0f);

        if (auto* parameter = getParameter (MantaCompParams::ratio))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (
                juce::jlimit (MantaCompParams::minRatio, MantaCompParams::maxRatio,
                               dragStartRatio * factor)));
    }
}

void CompressorDisplay::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;

    for (auto* id : { MantaCompParams::threshold, MantaCompParams::ratio })
        if (auto* parameter = getParameter (id))
            parameter->endChangeGesture();
}

void CompressorDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    auto* parameter = getParameter (MantaCompParams::knee);

    if (parameter == nullptr)
        return;

    const float current = processor.getSettings().kneeDb;

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (
        juce::jlimit (0.0f, MantaCompParams::maxKneeDb, current + wheel.deltaY * 24.0f)));
}
