#include "MantaLimiterEditor.h"

#include "MantaLimiterPresets.h"
#include "../MantaTheme.h"
#include "../MantaAnalyzer/AnalyzerThemeColors.h"   // OKLCH の変換を借りる
#include "../../Branding.h"

#include <cmath>

//==============================================================================
// 色

namespace
{
    double relativeLuminance (juce::Colour c)
    {
        auto channel = [] (float v)
        {
            return v <= 0.03928f ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
        };

        return 0.2126 * channel (c.getFloatRed()) + 0.7152 * channel (c.getFloatGreen()) + 0.0722 * channel (c.getFloatBlue());
    }

    float hueDifferenceDegrees (float a, float b)
    {
        float d = std::abs (a - b) * 180.0f / juce::MathConstants<float>::pi;
        return d > 180.0f ? 360.0f - d : d;
    }

    juce::Colour onColourFor (juce::Colour accent)
    {
        // 白と黒のうち、**コントラスト比 4.5:1 以上**のほう（両方満たせば大きいほう）
        const double white = LimiterTheme::contrastRatio (accent, juce::Colours::white);
        const double black = LimiterTheme::contrastRatio (accent, juce::Colours::black);
        return white >= black ? juce::Colours::white : juce::Colours::black;
    }
}

double LimiterTheme::contrastRatio (juce::Colour a, juce::Colour b)
{
    const double la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

LimiterTheme LimiterTheme::fromDaw()
{
    using C = AnalyzerThemeColors;

    LimiterTheme t;
    const auto mainColour = MantaTheme::accent();
    auto subColour = MantaTheme::curve();

    const auto mainLch = C::toOklch (mainColour);
    auto subLch = C::toOklch (subColour);

    // 色相が30°未満なら、副の明度をメインから離れる向きに0.20ずらす（区別のため）
    if (hueDifferenceDegrees (mainLch.h, subLch.h) < 30.0f)
    {
        const float shifted = subLch.l >= mainLch.l ? subLch.l + 0.2f : subLch.l - 0.2f;
        subColour = C::withLightness (subColour, juce::jlimit (0.15f, 0.95f, shifted));
        subLch = C::toOklch (subColour);
    }

    t.main = mainColour;
    t.mainStrong = C::withLightness (mainColour, juce::jmin (0.97f, mainLch.l + 0.10f));
    t.mainWeak = mainColour.withAlpha (0.30f);
    t.onMain = onColourFor (mainColour);

    t.sub = subColour;
    t.subStrong = C::withLightness (subColour, juce::jmin (0.97f, subLch.l + 0.10f));
    t.subWeak = subColour.withAlpha (0.30f);
    t.onSub = onColourFor (subColour);

    t.background = MantaTheme::graphBackground();
    t.panel = MantaTheme::panelBackground();
    t.text = MantaTheme::text();
    t.textDim = MantaTheme::textDim();
    t.grid = MantaTheme::grid();
    t.waveIn = t.text.withAlpha (0.25f);
    t.waveOut = t.text.withAlpha (0.55f);

    // 警告色は**アクセントから作らない**（意味を持つので）。赤が既定、メインか副が赤に近いなら黄
    const auto red = juce::Colour (0xffe5484d);
    const float redHue = C::toOklch (red).h;
    const bool clash = hueDifferenceDegrees (mainLch.h, redHue) < 30.0f || hueDifferenceDegrees (subLch.h, redHue) < 30.0f;
    t.warn = clash ? juce::Colour (0xfff5c542) : red;

    return t;
}

//==============================================================================
// スクロール表示

void LimiterScrollDisplay::resized()
{
    ring.assign ((size_t) juce::jmax (1, getWidth()), {});
    writePos = 0;
}

void LimiterScrollDisplay::addColumn (const LimiterMeters::Column& column)
{
    if (ring.empty())
        return;

    ring[(size_t) writePos] = column;
    writePos = (writePos + 1) % (int) ring.size();
}

void LimiterScrollDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme.background);
    g.fillRoundedRectangle (bounds, AppColours::corner (4.0f));

    const int width = (int) ring.size();
    const float h = bounds.getHeight();

    // **dB の目盛り**（上端 0 dB、下端 `rangeDb`）。入力と出力は下から、GRは上から**同じ目盛り**で下ろす
    // ——「入力の高さ − GR ≒ 出力の高さ」が目で読めます。
    // （はじめは直線の振幅で描いていて、天井近くで鳴る音は画面いっぱいの塊になりました）
    auto yFor = [h] (float db) { return juce::jlimit (0.0f, h, -db / rangeDb * h); };
    auto toDb = [] (float linear) { return linear > 1.0e-6f ? 20.0f * std::log10 (linear) : -200.0f; };

    g.setColour (theme.grid);
    for (float db = -6.0f; db > -rangeDb; db -= 6.0f)
        g.drawHorizontalLine ((int) yFor (db), 0.0f, bounds.getRight());

    // 新しい列が右端。x=0 がいちばん古い
    auto columnAt = [this, width] (int x) -> const LimiterMeters::Column&
    {
        return ring[(size_t) ((writePos + x) % width)];
    };

    // 出力は**塗りを薄く、上の縁をはっきり**。入力は**出力より上にはみ出たぶん（削ったぶん）だけ**を塗る。
    // 詰まった音ではピークがどれも高いので、棒を全部同じ濃さで塗ると灰色の板になります
    juce::Path outputEdge;

    for (int x = 0; x < width; ++x)
    {
        const auto& c = columnAt (x);
        const float outY = yFor (toDb (c.outAbsMax));
        const float inY = yFor (toDb (juce::jmax (c.inMax, -c.inMin)));

        g.setColour (theme.waveOut.withMultipliedAlpha (0.35f));
        g.drawVerticalLine (x, outY, h);

        if (inY < outY)
        {
            g.setColour (theme.waveIn);
            g.drawVerticalLine (x, inY, outY);
        }

        if (x == 0) outputEdge.startNewSubPath ((float) x, outY);
        else        outputEdge.lineTo ((float) x, outY);
    }

    g.setColour (theme.waveOut);
    g.strokePath (outputEdge, juce::PathStrokeType (1.0f));

    // 天井（破線。副の色）
    {
        const float y = yFor (ceilingDb);
        const float dashes[] { 5.0f, 4.0f };
        g.setColour (theme.sub.withAlpha (0.85f));
        g.drawDashedLine ({ 0.0f, y, bounds.getRight(), y }, dashes, 2, 1.0f);
    }

    // GR（上端から下向き。線はメイン、塗りはメインの30%）
    juce::Path fill, line;
    fill.startNewSubPath (0.0f, 0.0f);

    for (int x = 0; x < width; ++x)
    {
        const float y = yFor (columnAt (x).grMax);
        fill.lineTo ((float) x, y);

        if (x == 0) line.startNewSubPath ((float) x, y);
        else        line.lineTo ((float) x, y);
    }

    fill.lineTo ((float) (width - 1), 0.0f);
    fill.closeSubPath();

    g.setColour (theme.mainWeak);
    g.fillPath (fill);
    g.setColour (theme.main);
    g.strokePath (line, juce::PathStrokeType (1.5f));

    // 目盛りの数字
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (theme.textDim);
    for (float db = -12.0f; db > -rangeDb; db -= 12.0f)
        g.drawText (juce::String ((int) db), 4, (int) yFor (db) - 12, 30, 12, juce::Justification::centredLeft);
}

//==============================================================================
// メーター列

void LimiterMeterView::setFrame (const LimiterMeters::Frame& newFrame, float ceilingDb, float target)
{
    frame = newFrame;
    ceiling = ceilingDb;
    loudnessTarget = target;

    // 入力ピークは2秒保持（仕様書）
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

    for (int c = 0; c < 2; ++c)
    {
        if (frame.inputPeak[c] >= heldInput[c] || now - heldSince[c] > 2.0)
        {
            heldInput[c] = frame.inputPeak[c];
            heldSince[c] = now;
        }
    }

    repaint();
}

void LimiterMeterView::mouseDown (const juce::MouseEvent& e)
{
    // 仕様書：最大値はクリックでリセット
    if (readoutArea.contains (e.getPosition()) && onReset != nullptr)
        onReset();
}

void LimiterMeterView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (theme.background);
    g.fillRoundedRectangle (area.toFloat(), AppColours::corner (4.0f));

    area.reduce (8, 8);
    readoutArea = area.removeFromBottom (92);
    auto labels = area.removeFromBottom (16);
    auto bars = area;

    const int barWidth = 14, gap = 4, groupGap = 12;

    auto yFor = [&bars] (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb));
        return (float) bars.getBottom() - t * (float) bars.getHeight();
    };

    auto drawBar = [&] (int x, float db, juce::Colour colour, bool fromTop = false)
    {
        juce::Rectangle<float> r ((float) x, (float) bars.getY(), (float) barWidth, (float) bars.getHeight());
        g.setColour (theme.grid);
        g.fillRect (r);

        if (fromTop)
        {
            // GR：上から下へ（0 → −24 dB を全高に）
            const float depth = juce::jlimit (0.0f, 1.0f, -db / 24.0f);
            g.setColour (colour);
            g.fillRect (r.withHeight (depth * r.getHeight()));
        }
        else
        {
            const float top = yFor (db);
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (r.getX(), top, r.getWidth(), r.getBottom() - top));
        }
    };

    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    int x = bars.getX();
    auto caption = [&] (const juce::String& text, int from, int to)
    {
        g.setColour (theme.textDim);
        g.drawText (text, from, labels.getY(), to - from, labels.getHeight(), juce::Justification::centred);
    };

    // 入力（L／R）と保持
    const int inStart = x;
    for (int c = 0; c < 2; ++c)
    {
        drawBar (x, frame.inputPeak[c], theme.waveOut);
        g.setColour (theme.text);
        g.drawHorizontalLine ((int) yFor (heldInput[c]), (float) x, (float) (x + barWidth));
        x += barWidth + gap;
    }
    caption ("In", inStart, x - gap);
    x += groupGap - gap;

    // GR（瞬時値＋区間最大の線）
    const int grStart = x;
    drawBar (x, frame.gainReductionNow, theme.main, true);
    {
        const float depth = juce::jlimit (0.0f, 1.0f, -frame.gainReductionMax / 24.0f);
        g.setColour (theme.mainStrong);
        g.drawHorizontalLine (bars.getY() + (int) (depth * bars.getHeight()), (float) x, (float) (x + barWidth));
    }
    x += barWidth;
    caption ("GR", grStart - 4, x + 4);
    x += groupGap;

    // 出力（TP。L／R）。天井の線と、クリップ
    const int outStart = x;
    for (int c = 0; c < 2; ++c)
    {
        const bool over = frame.outputTruePeak[c] > ceiling + 0.1f;
        drawBar (x, frame.outputTruePeak[c], over ? theme.warn : theme.waveOut.withAlpha (1.0f).interpolatedWith (theme.main, 0.35f));
        x += barWidth + gap;
    }
    g.setColour (theme.sub);
    g.drawHorizontalLine ((int) yFor (ceiling), (float) outStart, (float) (x - gap));
    caption ("Out", outStart, x - gap);
    x += groupGap - gap;

    // ラウドネス（M・S。副の色）と目標の破線
    const int loudStart = x;
    drawBar (x, frame.momentary, theme.subWeak.withAlpha (0.6f));
    x += barWidth + gap;
    drawBar (x, frame.shortTerm, theme.sub);
    x += barWidth;
    caption ("M  S", loudStart - 2, x + 2);

    if (loudnessTarget < 0.0f)
    {
        const float y = yFor (loudnessTarget);
        const float dashes[] { 4.0f, 3.0f };
        g.setColour (theme.subStrong);
        g.drawDashedLine ({ (float) loudStart - 2.0f, y, (float) x + 2.0f, y }, dashes, 2, 1.5f);
    }

    // 目盛り（右端）
    g.setColour (theme.textDim);
    for (float db = 0.0f; db >= floorDb; db -= 12.0f)
        g.drawText (juce::String ((int) db), x + 6, (int) yFor (db) - 6, 30, 12, juce::Justification::centredLeft);

    //--------------------------------------------------------------------------
    // 数値（クリックでリセット）
    auto formatLufs = [] (float v) { return v <= LimiterMeters::silenceDb + 1.0f ? juce::String ("--") : juce::String (v, 1); };

    auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour colour, int index)
    {
        auto r = readoutArea.withHeight (16).translated (0, index * 16);
        g.setColour (theme.textDim);
        g.drawText (name, r.removeFromLeft (70), juce::Justification::centredLeft);
        g.setColour (colour);
        g.drawText (value, r, juce::Justification::centredLeft);
    };

    juce::String integrated = formatLufs (frame.integrated) + " LUFS";
    juce::Colour integratedColour = theme.text;

    if (loudnessTarget < 0.0f && frame.integrated > LimiterMeters::silenceDb + 1.0f)
    {
        const float difference = frame.integrated - loudnessTarget;
        integrated << "  (" << (difference >= 0.0f ? "+" : "") << juce::String (difference, 1) << " LU)";

        if (std::abs (difference) <= 1.0f)
            integratedColour = theme.subStrong;
    }

    row ("Integrated", integrated, integratedColour, 0);
    row ("M / S", formatLufs (frame.momentary) + " / " + formatLufs (frame.shortTerm) + " LUFS", theme.text, 1);
    row ("LRA / PLR", juce::String (frame.loudnessRange, 1) + " / " + juce::String (frame.peakToLoudness, 1) + " LU", theme.text, 2);
    row ("True peak", formatLufs (frame.outputTruePeakMax) + " dBTP", frame.clipped ? theme.warn : theme.text, 3);
    row ("GR max", juce::String (frame.gainReductionMax, 1) + " dB", theme.main, 4);
}

//==============================================================================
// 画面

MantaLimiterEditor::MantaLimiterEditor (MantaLimiterProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::limiterPresetFolder)
{
    addAndMakeVisible (toolbar);

    toolbar.setFactoryPresets (MantaFactoryPresets::makeToolbarPresets (processor.getValueTreeState(),
                                                                        MantaLimiterPresets::all()));
    toolbar.setShowsCurrentPreset (true);
    toolbar.onStateRestored = [this] { refreshControls(); };

    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (statusLabel);

    addAndMakeVisible (scroll);
    addAndMakeVisible (meterView);
    meterView.onReset = [this] { processor.getMeters().requestReset(); };

    // スクロールの表示時間（5／10秒）・計測のやり直し・再生で自動リセット
    MantaPluginToolbar::styleButton (secondsButton, "5 s");
    secondsButton.onClick = [this]
    {
        const int seconds = scroll.getSeconds() == 5 ? 10 : 5;
        scroll.setSeconds (seconds);
        processor.setDisplaySeconds (seconds);
        secondsButton.setButtonText (juce::String (seconds) + " s");
    };
    addAndMakeVisible (secondsButton);

    MantaPluginToolbar::styleButton (resetButton, "Reset");
    resetButton.onClick = [this] { processor.getMeters().requestReset(); };
    addAndMakeVisible (resetButton);

    MantaPluginToolbar::styleButton (autoResetButton, "Reset on Play");
    autoResetButton.setClickingTogglesState (true);
    autoResetButton.setToggleState (processor.getAutoResetOnPlay(), juce::dontSendNotification);
    autoResetButton.onClick = [this] { processor.setAutoResetOnPlay (autoResetButton.getToggleState()); };
    addAndMakeVisible (autoResetButton);

    scroll.setSeconds (processor.getDisplaySeconds());
    secondsButton.setButtonText (juce::String (scroll.getSeconds()) + " s");

    // スタイル（4つの押しボタン。選んでいるものはメインの色）
    styleTitle.setText ("Style", juce::dontSendNotification);
    styleTitle.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (styleTitle);

    for (int i = 0; i < 4; ++i)
    {
        auto& button = styleButtons[i];
        MantaPluginToolbar::styleButton (button, MantaLimiterParams::styleNames()[i]);
        button.setRadioGroupId (0x4c53);   // 'LS'
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.getValueTreeState().getParameter (MantaLimiterParams::style))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
                parameter->endChangeGesture();
            }

            refreshControls();
        };

        addAndMakeVisible (button);
    }

    setupSwitch (autoReleaseButton, "Auto Release", MantaLimiterParams::autoRelease);
    setupSwitch (truePeakButton, "True Peak", MantaLimiterParams::truePeak);
    setupSwitch (unityButton, "Unity Gain", MantaLimiterParams::unity);
    setupSwitch (auditionButton, "Audition", MantaLimiterParams::audition);
    setupSwitch (dcButton, "DC Filter", MantaLimiterParams::dcFilter);

    setupKnob (gainSlider, gainCaption, "Gain", MantaLimiterParams::gain);
    setupKnob (outputSlider, outputCaption, "Output", MantaLimiterParams::output);
    setupKnob (lookaheadSlider, lookaheadCaption, "Lookahead", MantaLimiterParams::lookahead);
    setupKnob (attackSlider, attackCaption, "Attack", MantaLimiterParams::attack);
    setupKnob (releaseSlider, releaseCaption, "Release", MantaLimiterParams::release);
    setupKnob (linkSlider, linkCaption, "Link", MantaLimiterParams::link);

    setupCombo (oversamplingBox, oversamplingCaption, "Oversampling", MantaLimiterParams::oversamplingNames(), MantaLimiterParams::oversampling);
    setupCombo (ditherBox, ditherCaption, "Dither", MantaLimiterParams::ditherNames(), MantaLimiterParams::dither);
    setupCombo (noiseShapingBox, noiseShapingCaption, "Noise Shaping", MantaLimiterParams::noiseShapingNames(), MantaLimiterParams::noiseShaping);

    // ラウドネスの目標（パラメータではなく状態。仕様書）
    targetCaption.setText ("Loudness Target", juce::dontSendNotification);
    targetCaption.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (targetCaption);

    targetBox.addItemList ({ "Off", "-14 LUFS", "-16 LUFS", "-23 LUFS", "Custom" }, 1);
    targetBox.onChange = [this]
    {
        const int index = targetBox.getSelectedItemIndex();
        const float values[] { 0.0f, -14.0f, -16.0f, -23.0f };

        if (index >= 0 && index < 4)
            processor.setLoudnessTarget (values[index]);
        else if (index == 4)
            processor.setLoudnessTarget ((float) customTargetSlider.getValue());

        refreshTargetBox();
    };
    addAndMakeVisible (targetBox);

    customTargetSlider.setRange (-40.0, -5.0, 0.5);
    customTargetSlider.setValue (-18.0, juce::dontSendNotification);
    customTargetSlider.setTextValueSuffix (" LUFS");
    customTargetSlider.onValueChange = [this] { processor.setLoudnessTarget ((float) customTargetSlider.getValue()); };
    addChildComponent (customTargetSlider);

    applyTheme();
    refreshControls();
    refreshTargetBox();

    // 大きさは固定（9.5）
    setSize (fixedWidth, fixedHeight);

    startTimerHz (30);   // 設計書：描画は 30 fps
}

MantaLimiterEditor::~MantaLimiterEditor()
{
    stopTimer();

    for (auto* slider : { &gainSlider, &outputSlider, &lookaheadSlider, &attackSlider, &releaseSlider, &linkSlider })
        slider->setLookAndFeel (nullptr);
}

void MantaLimiterEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text, const char* parameterId)
{
    slider.setLookAndFeel (&knobLookAndFeel.get());
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(), parameterId, slider));
}

void MantaLimiterEditor::setupSwitch (juce::TextButton& button, const juce::String& text, const char* parameterId)
{
    MantaPluginToolbar::styleButton (button, text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);

    buttonAttachments.add (new ButtonAttachment (processor.getValueTreeState(), parameterId, button));
}

void MantaLimiterEditor::setupCombo (juce::ComboBox& box, juce::Label& caption, const juce::String& text,
                                     const juce::StringArray& items, const char* parameterId)
{
    box.addItemList (items, 1);
    addAndMakeVisible (box);

    caption.setText (text, juce::dontSendNotification);
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (caption);

    comboAttachments.add (new ComboBoxAttachment (processor.getValueTreeState(), parameterId, box));
}

void MantaLimiterEditor::applyTheme()
{
    theme = LimiterTheme::fromDaw();
    lastMain = MantaTheme::accent();
    lastSub = MantaTheme::curve();
    lastDawTheme = AppColours::getTheme();

    scroll.setTheme (theme);
    meterView.setTheme (theme);

    for (auto* slider : { &gainSlider, &outputSlider, &lookaheadSlider, &attackSlider, &releaseSlider, &linkSlider })
    {
        // つまみの値の弧はメイン（仕様書）
        slider->setColour (juce::Slider::rotarySliderFillColourId, theme.main);
        slider->setColour (juce::Slider::textBoxTextColourId, theme.text);
    }

    for (auto* label : { &gainCaption, &outputCaption, &lookaheadCaption, &attackCaption, &releaseCaption, &linkCaption,
                         &oversamplingCaption, &ditherCaption, &noiseShapingCaption, &targetCaption })
        label->setColour (juce::Label::textColourId, theme.textDim);

    styleTitle.setColour (juce::Label::textColourId, theme.textDim);
    statusLabel.setColour (juce::Label::textColourId, theme.textDim);

    // 選んでいるスタイルはメイン、オンのスイッチは副（仕様書「描画の割り当て」）
    for (auto& button : styleButtons)
    {
        MantaPluginToolbar::styleButton (button, button.getButtonText());
        button.setColour (juce::TextButton::buttonOnColourId, theme.main);
        button.setColour (juce::TextButton::textColourOnId, theme.onMain);
    }

    for (auto* button : { &autoReleaseButton, &truePeakButton, &unityButton, &auditionButton, &dcButton, &autoResetButton })
    {
        MantaPluginToolbar::styleButton (*button, button->getButtonText());
        button->setColour (juce::TextButton::buttonOnColourId, theme.sub);
        button->setColour (juce::TextButton::textColourOnId, theme.onSub);
    }

    for (auto* button : { &secondsButton, &resetButton })
        MantaPluginToolbar::styleButton (*button, button->getButtonText());

    repaint();
}

void MantaLimiterEditor::refreshControls()
{
    const auto p = processor.getLimiterParameters();

    for (int i = 0; i < 4; ++i)
        styleButtons[i].setToggleState (p.style == i, juce::dontSendNotification);

    // Safe は先読み 5 ms 固定（仕様書）。Dither Off のときノイズシェーピングは無効表示
    const bool lookaheadFree = ! LimiterEngine::styleConstants (p.style).forceMaxLookahead;
    lookaheadSlider.setEnabled (lookaheadFree);
    lookaheadCaption.setText (lookaheadFree ? "Lookahead" : "Lookahead (5 ms)", juce::dontSendNotification);

    noiseShapingBox.setEnabled (p.dither > 0);
    noiseShapingCaption.setEnabled (p.dither > 0);

    // True Peak オンのとき、天井は dBTP 表記（仕様書）
    outputCaption.setText (p.truePeak ? "Output (dBTP)" : "Output", juce::dontSendNotification);

    // 遅れと倍率
    const double rate = processor.getSampleRateForDisplay();
    const int latency = processor.getReportedLatencySamples();
    juce::String status;

    if (p.oversampling > 0)
        status << "OS " << (1 << p.oversampling) << "x  ";

    status << "Latency " << juce::String (rate > 0.0 ? latency * 1000.0 / rate : 0.0, 2) << " ms";
    statusLabel.setText (status, juce::dontSendNotification);
}

void MantaLimiterEditor::refreshTargetBox()
{
    const float target = processor.getLoudnessTarget();
    int index = 4;

    if (target >= 0.0f)            index = 0;
    else if (target == -14.0f)     index = 1;
    else if (target == -16.0f)     index = 2;
    else if (target == -23.0f)     index = 3;

    if (targetBox.getSelectedItemIndex() != index)
        targetBox.setSelectedItemIndex (index, juce::dontSendNotification);

    if (index == 4)
        customTargetSlider.setValue (target, juce::dontSendNotification);

    customTargetSlider.setVisible (index == 4);
}

void MantaLimiterEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    // 下の帯の地（8.342：配置と同じ矩形。前は別々に計算していて、ボタンの列が帯の上端に貼り付いていた）
    g.setColour (theme.panel);
    g.fillRoundedRectangle (bandArea.toFloat(), AppColours::corner (6.0f));
}

void MantaLimiterEditor::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (MantaPluginToolbar::preferredHeight));
    statusLabel.setBounds (toolbar.getLocalBounds().removeFromRight (220).reduced (8, 0));

    area.reduce (10, 6);

    // 下の帯は先に取る（左右は窓の端から 8 px）
    bandArea = getLocalBounds().withTrimmedBottom (6).removeFromBottom (bandHeight).reduced (8, 0);
    area.setBottom (bandArea.getY() - 10);

    // 上：スクロール表示＋メーター
    auto top = area;
    auto meterArea = top.removeFromRight (meterWidth);
    top.removeFromRight (10);

    auto scrollButtons = top.removeFromBottom (26);
    scroll.setBounds (top.withTrimmedBottom (4));

    secondsButton.setBounds (scrollButtons.removeFromLeft (54).reduced (0, 2));
    scrollButtons.removeFromLeft (6);
    resetButton.setBounds (scrollButtons.removeFromLeft (60).reduced (0, 2));
    scrollButtons.removeFromLeft (6);
    autoResetButton.setBounds (scrollButtons.removeFromLeft (110).reduced (0, 2));

    meterView.setBounds (meterArea);

    // 下の帯（8.342：帯の内側に余白）
    auto bottom = bandArea.reduced (bandPaddingX, bandPaddingY);

    // スタイルとスイッチの列。8.334：**ボタンは文字に合わせた幅**（同じ幅に揃えると、
    // いちばん長い「Transparent」に引っぱられて横に余りが出ていた）
    auto buttonsRow = bottom.removeFromTop (28);
    styleTitle.setBounds (buttonsRow.removeFromLeft (40));

    // 文字の幅＋左右 7 px（JUCE の既定は左右 12 px で、1列に収まらない）。枠のぶん +4
    auto widthFor = [] (juce::TextButton& button)
    {
        return juce::GlyphArrangement::getStringWidthInt (button.getLookAndFeel().getTextButtonFont (button, 24),
                                                          button.getButtonText()) + 14 + 4;
    };

    // スタイルも文字に合わせる（同じ幅に揃えると、800 px では1列に収まらない）。
    // 余りは9つに等しく配って、列の右端を下の選択欄の右端に揃える
    juce::TextButton* rowButtons[] { &styleButtons[0], &styleButtons[1], &styleButtons[2], &styleButtons[3],
                                     &autoReleaseButton, &truePeakButton, &unityButton, &auditionButton, &dcButton };
    const int groupGap = 12;
    int used = groupGap;

    for (auto* button : rowButtons)
        used += widthFor (*button);

    const int extra = juce::jlimit (0, 12, (buttonsRow.getWidth() - used) / (int) std::size (rowButtons));

    for (auto* button : rowButtons)
    {
        if (button == &autoReleaseButton)
            buttonsRow.removeFromLeft (groupGap);

        button->setBounds (buttonsRow.removeFromLeft (widthFor (*button) + extra).reduced (2, 2));
    }

    bottom.removeFromTop (8);

    // つまみ（6つ）と、**そのすぐ右**に選択欄（8.334：前は右端に寄せていて、間が空いていた）
    for (auto [slider, caption] : { std::pair { &gainSlider, &gainCaption }, { &outputSlider, &outputCaption },
                                    { &lookaheadSlider, &lookaheadCaption }, { &attackSlider, &attackCaption },
                                    { &releaseSlider, &releaseCaption }, { &linkSlider, &linkCaption } })
    {
        auto column = bottom.removeFromLeft (knobWidth);
        caption->setBounds (column.removeFromTop (14));
        slider->setBounds (column.reduced (4, 0));
    }

    bottom.removeFromLeft (16);
    auto combos = bottom.removeFromLeft (comboColumnWidth);
    const int captionWidth = 86;

    auto comboRow = [&combos, captionWidth] (juce::Label& caption, juce::Component& box)
    {
        auto row = combos.removeFromTop (28);
        caption.setBounds (row.removeFromLeft (captionWidth));
        box.setBounds (row.reduced (0, 3));
    };

    comboRow (oversamplingCaption, oversamplingBox);
    comboRow (ditherCaption, ditherBox);
    comboRow (noiseShapingCaption, noiseShapingBox);
    comboRow (targetCaption, targetBox);

    // Custom の値は目標の欄の下の行に（横に並べると欄の幅が足りない）
    customTargetSlider.setBounds (combos.removeFromTop (28).withTrimmedLeft (captionWidth).reduced (0, 3));
}

void MantaLimiterEditor::timerCallback()
{
    // テーマが変わったら作り直す（DAWのテーマ変更に即時追従。仕様書）
    if (MantaTheme::accent() != lastMain || MantaTheme::curve() != lastSub || AppColours::getTheme() != lastDawTheme)
        applyTheme();

    auto& meters = processor.getMeters();

    // スクロール表示の1列の長さ（表示時間 × レート ÷ 幅）
    const int width = juce::jmax (1, scroll.getWidth());
    meters.setSamplesPerColumn ((int) std::lround (scroll.getSeconds() * processor.getSampleRateForDisplay() / width));

    LimiterMeters::Column column;
    int added = 0;

    while (meters.popColumn (column))
    {
        scroll.addColumn (column);
        ++added;
    }

    LimiterMeters::Frame frame;
    bool gotFrame = false;

    while (meters.popFrame (frame))
    {
        latestFrame = frame;
        gotFrame = true;
    }

    const auto p = processor.getLimiterParameters();
    scroll.setCeilingDb (p.outputDb);

    if (gotFrame)
        meterView.setFrame (latestFrame, p.outputDb, processor.getLoudnessTarget());

    if (added > 0)
        scroll.repaint();

    refreshControls();
}
