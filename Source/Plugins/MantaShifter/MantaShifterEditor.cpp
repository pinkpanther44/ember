#include "MantaShifterEditor.h"

#include "MantaShifterPresets.h"
#include "ShifterControl.h"
#include "../MantaAnalyzer/AnalyzerThemeColors.h"   // OKLCH の変換を借りる（8.333と同じ）
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

    double contrastRatio (juce::Colour a, juce::Colour b)
    {
        const double la = relativeLuminance (a), lb = relativeLuminance (b);
        return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
    }

    juce::Colour onColourFor (juce::Colour accent)
    {
        // 白と黒のうち、コントラスト比の大きいほう（アクセントの上の文字）
        return contrastRatio (accent, juce::Colours::white) >= contrastRatio (accent, juce::Colours::black)
                   ? juce::Colours::white : juce::Colours::black;
    }

    float hueDifferenceDegrees (float a, float b)
    {
        float d = std::abs (a - b) * 180.0f / juce::MathConstants<float>::pi;
        return d > 180.0f ? 360.0f - d : d;
    }

    juce::String noteName (int note)
    {
        return ShifterParams::keyNames()[((note % 12) + 12) % 12] + juce::String (note / 12 - 1);
    }
}

ShifterTheme ShifterTheme::fromDaw()
{
    using C = AnalyzerThemeColors;

    ShifterTheme t;
    const auto mainColour = MantaTheme::accent();
    auto subColour = MantaTheme::curve();

    const auto mainLch = C::toOklch (mainColour);
    auto subLch = C::toOklch (subColour);

    // 色相が30°未満なら、副の明度をメインから離れる向きに0.20ずらす（2本の線を見分けるため）
    if (hueDifferenceDegrees (mainLch.h, subLch.h) < 30.0f)
    {
        const float shifted = subLch.l >= mainLch.l ? subLch.l + 0.2f : subLch.l - 0.2f;
        subColour = C::withLightness (subColour, juce::jlimit (0.15f, 0.95f, shifted));
        subLch = C::toOklch (subColour);
    }

    t.main = mainColour.withAlpha (1.0f);
    t.mainStrong = C::withLightness (t.main, juce::jmin (0.97f, mainLch.l + 0.10f));
    t.mainWeak = t.main.withAlpha (0.30f);
    t.onMain = onColourFor (t.main);

    t.sub = subColour.withAlpha (1.0f);
    t.subStrong = C::withLightness (t.sub, juce::jmin (0.97f, subLch.l + 0.10f));
    t.subWeak = t.sub.withAlpha (0.30f);
    t.onSub = onColourFor (t.sub);

    t.background = MantaTheme::graphBackground();
    t.panel = MantaTheme::panelBackground();
    t.text = MantaTheme::text();
    t.textDim = MantaTheme::textDim();
    t.grid = MantaTheme::grid();
    return t;
}

//==============================================================================
// ピッチの表示

void ShifterPitchView::setScale (int newMode, int newKey, int newScale)
{
    if (newMode == mode && newKey == key && newScale == scale)
        return;

    mode = newMode;
    key = newKey;
    scale = newScale;
    repaint();
}

void ShifterPitchView::addFrame (const ShifterEngine::DisplayFrame& frame)
{
    history[(size_t) writePos] = frame;
    writePos = (writePos + 1) % historyLength;
    count = juce::jmin (count + 1, historyLength);
    latest = frame;

    // 声が端の4半音に入ったら（または初めて声が来たら）、中心を寄せ直す
    if (frame.voiced && frame.detectedHz > 0.0f)
    {
        const float note = 0.5f * (frame.inputNote + frame.outputNote);

        if (! centred || std::abs (note - centre) > halfRange - 4.0f)
        {
            centre = (float) std::lround (note);
            centred = true;
        }
    }
}

void ShifterPitchView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (theme.background);
    g.fillRoundedRectangle (area, AppColours::corner (5.0f));

    auto inner = getLocalBounds().reduced (10, 8);

    //--------------------------------------------------------------------------
    // いまの音
    auto header = inner.removeFromTop (46);

    if (latest.voiced && latest.detectedHz > 0.0f)
    {
        const int outNote = (int) std::lround (latest.outputNote);
        const int outCents = (int) std::lround ((latest.outputNote - (float) outNote) * 100.0f);
        const int inNote = (int) std::lround (latest.inputNote);
        const int inCents = (int) std::lround ((latest.inputNote - (float) inNote) * 100.0f);

        auto big = header.removeFromLeft (120);
        g.setColour (theme.main);
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
        g.drawText (noteName (outNote), big, juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.setColour (theme.text);
        g.drawText ((outCents >= 0 ? "+" : "") + juce::String (outCents) + " ct",
                    header.removeFromTop (22), juce::Justification::bottomLeft);

        g.setColour (theme.sub);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("in " + noteName (inNote) + " " + (inCents >= 0 ? "+" : "") + juce::String (inCents) + " ct   "
                      + juce::String (latest.detectedHz, 1) + " Hz",
                    header, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (theme.textDim);
        g.setFont (juce::Font (juce::FontOptions (30.0f, juce::Font::bold)));
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\x94"), header.removeFromLeft (120), juce::Justification::centredLeft);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("no pitch (unvoiced or silent)", header, juce::Justification::centredLeft);
    }

    //--------------------------------------------------------------------------
    // 軌跡。縦は中心の上下1オクターブ
    inner.removeFromTop (4);

    // 8.343：MIDI モードの案内は表示の下端に（**送り元はプラグインの中では選べない**——本体のインサートの右クリック。8.336）。
    // 前は表示の外に行を取っていて、表示が左の列より短かった
    if (mode == 3)
    {
        auto hint = inner.removeFromBottom (16);
        inner.removeFromBottom (4);
        g.setColour (theme.sub);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (midiSourceHint, hint, juce::Justification::centredLeft);
    }

    const auto graph = inner.toFloat();
    const float lowNote = centre - halfRange, highNote = centre + halfRange;

    auto yFor = [&graph, lowNote, highNote] (float note)
    {
        return graph.getBottom() - (note - lowNote) / (highNote - lowNote) * graph.getHeight();
    };

    // Quantize ではスケールの音の段を薄く
    if (mode == 1)
    {
        g.setColour (theme.mainWeak.withMultipliedAlpha (0.35f));

        for (int note = (int) lowNote; note < (int) highNote; ++note)
            if (ShifterControl::isInScale (note, key, scale) && scale != 0)
                g.fillRect (juce::Rectangle<float> (graph.getX(), yFor ((float) note + 0.5f), graph.getWidth(),
                                                    yFor ((float) note - 0.5f) - yFor ((float) note + 0.5f)));
    }

    g.setFont (juce::Font (juce::FontOptions (9.0f)));

    for (int note = (int) std::ceil (lowNote); note <= (int) highNote; ++note)
    {
        // 半音ごとに薄い線、C は濃い線
        const int pitchClass = ((note % 12) + 12) % 12;
        // 名前は C D F G A だけ（E–F・B–C は半音違いで、並べると字が重なる）
        const bool natural = pitchClass == 0 || pitchClass == 2 || pitchClass == 5 || pitchClass == 7 || pitchClass == 9;
        const float y = yFor ((float) note);

        g.setColour (pitchClass == 0 ? theme.grid : theme.grid.withMultipliedAlpha (0.4f));
        g.drawHorizontalLine ((int) y, graph.getX() + 24.0f, graph.getRight());

        if (natural)
        {
            g.setColour (pitchClass == 0 ? theme.text : theme.textDim);
            g.drawText (noteName (note), juce::Rectangle<float> (graph.getX(), y - 5.0f, 24.0f, 10.0f), juce::Justification::centredLeft);
        }
    }

    // 古いものから順に。無声のところで線を切る
    const float left = graph.getX() + 24.0f;
    const float width = graph.getRight() - left;

    auto drawTrace = [&] (bool output, juce::Colour colour, float thickness)
    {
        juce::Path path;
        bool drawing = false;

        for (int i = 0; i < count; ++i)
        {
            const int index = (writePos - count + i + historyLength) % historyLength;
            const auto& f = history[(size_t) index];
            const float x = left + width * (float) (historyLength - count + i) / (float) (historyLength - 1);

            if (! f.voiced || f.detectedHz <= 0.0f)
            {
                drawing = false;
                continue;
            }

            const float y = juce::jlimit (graph.getY(), graph.getBottom(), yFor (output ? f.outputNote : f.inputNote));

            if (drawing)
                path.lineTo (x, y);
            else
                path.startNewSubPath (x, y);

            drawing = true;
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    // MIDIモードでは、押している音を細い段で
    if (mode == 3)
    {
        g.setColour (theme.subWeak);

        for (int i = 0; i < count; ++i)
        {
            const int index = (writePos - count + i + historyLength) % historyLength;
            const auto& f = history[(size_t) index];

            if (f.midiNote < 0)
                continue;

            const float x = left + width * (float) (historyLength - count + i) / (float) (historyLength - 1);
            g.fillRect (x, yFor ((float) f.midiNote) - 1.5f, juce::jmax (1.0f, width / historyLength + 0.5f), 3.0f);
        }
    }

    // 出力（太い・メイン）を先に、検出（細い・副）を上に。補正が弱いと2本は重なるので、細いほうを上へ
    drawTrace (true, theme.main, 2.6f);
    drawTrace (false, theme.sub, 1.3f);

    // 凡例
    auto legend = getLocalBounds().reduced (12, 10).removeFromTop (14).removeFromRight (140);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.setColour (theme.sub);
    g.drawText ("detected", legend.removeFromLeft (62), juce::Justification::centredRight);
    g.setColour (theme.main);
    g.drawText ("output", legend, juce::Justification::centredRight);
}

//==============================================================================
// 画面

MantaShifterEditor::MantaShifterEditor (MantaShifterProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::shifterPresetFolder)
{
    addAndMakeVisible (toolbar);

    toolbar.setFactoryPresets (MantaFactoryPresets::makeToolbarPresets (processor.getValueTreeState(),
                                                                        MantaShifterPresets::all()));
    // 8.342：右にエンジンとレイテンシーを置くので、そのぶんを残してもらう
    toolbar.setShowsCurrentPreset (true, 200, engineGroupWidth + 12 + statusWidth);
    toolbar.onStateRestored = [this] { refreshControls(); };

    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (statusLabel);

    addAndMakeVisible (pitchView);

    setupKnob (pitchSlider, pitchCaption, "Pitch", ShifterParams::pitch);
    setupKnob (formantSlider, formantCaption, "Formant", ShifterParams::formant);
    setupKnob (retuneSlider, retuneCaption, "Retune", ShifterParams::retune);
    setupKnob (driveSlider, driveCaption, "Drive", ShifterParams::drive);
    setupKnob (mixSlider, mixCaption, "Mix", ShifterParams::mix);
    setupKnob (outputSlider, outputCaption, "Output", ShifterParams::output);

    // ±の2つは12時から塗る
    for (auto* slider : { &pitchSlider, &formantSlider })
    {
        slider->getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);
        slider->setDoubleClickReturnValue (true, 0.0);
    }

    for (auto* caption : { &pitchCaption, &formantCaption })
        caption->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));

    // モード（4つの押しボタン。選んでいるものはメインの色）
    for (int i = 0; i < 4; ++i)
    {
        auto& button = modeButtons[i];
        MantaPluginToolbar::styleButton (button, ShifterParams::modeNames()[i]);
        button.setRadioGroupId (0x5348);   // 'SH'
        button.onClick = [this, i]
        {
            if (auto* parameter = processor.getValueTreeState().getParameter (ShifterParams::mode))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) i));
                parameter->endChangeGesture();
            }

            refreshControls();
        };

        addAndMakeVisible (button);
    }

    // 8.337：エンジン（状態に保存。パラメータではないので、プリセットを選んでも変わらない）
    engineCaption.setText ("Engine", juce::dontSendNotification);
    engineCaption.setFont (juce::Font (juce::FontOptions (10.0f)));
    engineCaption.setJustificationType (juce::Justification::centredLeft);
    engineCaption.setBorderSize ({ 0, 0, 0, 0 });
    toolbar.addAndMakeVisible (engineCaption);   // 8.342：プリセットの右横へ（本人の指定）。ツールバーの子にする

    // 8.339：トグルスイッチ（1 PSOLA ⇔ 2 Spectral。本人の指定）
    engineSwitch.setEngineType (processor.getEngineType());
    engineSwitch.onClick = [this]
    {
        processor.setEngineType (engineSwitch.getEngineType());
        refreshControls();
    };
    engineSwitch.setTooltip ("1 PSOLA: exact pitch, best on clean single voices\n"
                             "2 Spectral (Signalsmith Stretch): smooth on chords, breathy or rough voices");
    toolbar.addAndMakeVisible (engineSwitch);

    setupSwitch (linkButton, "Link", ShifterParams::link);
    setupSwitch (driveOnButton, "Drive", ShifterParams::driveOn);
    setupSwitch (bypassButton, "Bypass", ShifterParams::bypass);
    setupSwitch (midiHoldButton, "MIDI Hold", ShifterParams::midiHold);

    linkButton.setTooltip ("Pitch and Formant move together (turn one and the other follows by the same amount)");

    // 8.341：**Link＝つまみの連動**（本人の想定。手本の Little AlterBoy と同じ）。
    // On のあいだ、Pitch を回すと Formant も同じだけ動き、Formant を回すと Pitch も同じだけ動く（2つの差は保つ）。
    // **手で動かしているあいだだけ**（`onDragStart`〜`onDragEnd`。打ち込み・ダブルクリックで戻すときも来る）——
    // プリセットやオートメーションで値が変わったときまで連動すると、2つとも狂う
    pitchLink.last = pitchSlider.getValue();
    formantLink.last = formantSlider.getValue();

    auto couple = [this] (ValueEntrySlider& moved, LinkState& movedState, ValueEntrySlider& other, LinkState& otherState)
    {
        moved.onDragStart = [&movedState] { movedState.userEditing = true; };
        moved.onDragEnd = [&movedState] { movedState.userEditing = false; };
        moved.onValueChange = [this, &moved, &movedState, &other, &otherState]
        {
            const double value = moved.getValue();
            const double delta = value - movedState.last;
            movedState.last = value;

            if (! movedState.userEditing || coupling || ! linkButton.getToggleState() || delta == 0.0)
                return;

            const juce::ScopedValueSetter<bool> guard (coupling, true);
            other.setValue (juce::jlimit (other.getMinimum(), other.getMaximum(), other.getValue() + delta), juce::sendNotificationSync);
            otherState.last = other.getValue();
        };
    };

    couple (pitchSlider, pitchLink, formantSlider, formantLink);
    couple (formantSlider, formantLink, pitchSlider, pitchLink);
    midiHoldButton.setTooltip ("MIDI mode: keep the last note while no key is held (off = the original voice)");

    setupCombo (keyBox, keyCaption, "Key", ShifterParams::keyNames(), ShifterParams::key);
    setupCombo (scaleBox, scaleCaption, "Scale", ShifterParams::scaleNames(), ShifterParams::scale);

    applyTheme();
    refreshControls();

    // 大きさは固定（9.5）
    setSize (fixedWidth, fixedHeight);

    startTimerHz (30);   // 設計書5章：UIは約30 Hzで受け取る
}

MantaShifterEditor::~MantaShifterEditor()
{
    stopTimer();

    for (auto* slider : { &pitchSlider, &formantSlider, &retuneSlider, &driveSlider, &mixSlider, &outputSlider })
        slider->setLookAndFeel (nullptr);
}

void MantaShifterEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text, const char* parameterId)
{
    slider.setLookAndFeel (&knobLookAndFeel.get());
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 15);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(), parameterId, slider));
}

void MantaShifterEditor::setupSwitch (juce::TextButton& button, const juce::String& text, const char* parameterId)
{
    MantaPluginToolbar::styleButton (button, text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);

    buttonAttachments.add (new ButtonAttachment (processor.getValueTreeState(), parameterId, button));
}

void MantaShifterEditor::setupCombo (juce::ComboBox& box, juce::Label& caption, const juce::String& text,
                                     const juce::StringArray& items, const char* parameterId)
{
    box.addItemList (items, 1);
    addAndMakeVisible (box);

    caption.setText (text, juce::dontSendNotification);
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);

    comboAttachments.add (new ComboBoxAttachment (processor.getValueTreeState(), parameterId, box));
}

void MantaShifterEditor::applyTheme()
{
    theme = ShifterTheme::fromDaw();
    lastMain = MantaTheme::accent();
    lastSub = MantaTheme::curve();
    lastDawTheme = AppColours::getTheme();

    pitchView.setTheme (theme);

    for (auto* slider : { &pitchSlider, &formantSlider, &retuneSlider, &driveSlider, &mixSlider, &outputSlider })
    {
        // つまみの値の弧はメイン
        slider->setColour (juce::Slider::rotarySliderFillColourId, theme.main);
        slider->setColour (juce::Slider::textBoxTextColourId, theme.text);
    }

    for (auto* label : { &pitchCaption, &formantCaption, &retuneCaption, &driveCaption, &mixCaption, &outputCaption,
                         &keyCaption, &scaleCaption })
        label->setColour (juce::Label::textColourId, theme.textDim);

    for (auto* label : { &pitchCaption, &formantCaption })
        label->setColour (juce::Label::textColourId, theme.text);

    statusLabel.setColour (juce::Label::textColourId, theme.textDim);

    // 選んでいるモードとエンジンはメイン、オンのスイッチは副（リミッターと同じ割り当て）
    engineCaption.setColour (juce::Label::textColourId, theme.textDim);

    engineSwitch.setTheme (theme);

    for (auto& button : modeButtons)
    {
        MantaPluginToolbar::styleButton (button, button.getButtonText());
        button.setColour (juce::TextButton::buttonOnColourId, theme.main);
        button.setColour (juce::TextButton::textColourOnId, theme.onMain);
    }

    for (auto* button : { &linkButton, &driveOnButton, &bypassButton, &midiHoldButton })
    {
        MantaPluginToolbar::styleButton (*button, button->getButtonText());
        button->setColour (juce::TextButton::buttonOnColourId, theme.sub);
        button->setColour (juce::TextButton::textColourOnId, theme.onSub);
    }

    repaint();
}

void MantaShifterEditor::refreshControls()
{
    const auto p = processor.getShifterParameters();

    for (int i = 0; i < 4; ++i)
        modeButtons[i].setToggleState (p.mode == i, juce::dontSendNotification);

    engineSwitch.setEngineType (p.engine);

    // モードで効かない欄は薄く（Key/Scale は Quantize だけ、Retune は検出を使う3つ、Hold は MIDI だけ）
    const bool quantize = p.mode == (int) ShifterParams::Mode::quantize;
    const bool corrects = p.mode != (int) ShifterParams::Mode::transpose;
    const bool midi = p.mode == (int) ShifterParams::Mode::midi;

    for (auto* c : std::initializer_list<juce::Component*> { &keyBox, &keyCaption, &scaleBox, &scaleCaption })
        c->setEnabled (quantize);

    retuneSlider.setEnabled (corrects);
    retuneCaption.setEnabled (corrects);
    midiHoldButton.setEnabled (midi);

    driveSlider.setEnabled (p.driveOn);
    driveCaption.setEnabled (p.driveOn);

    // Robot と MIDI では Pitch が「出力音高のずらし」になる（仕様書2章の備考）
    pitchCaption.setText (p.mode == (int) ShifterParams::Mode::robot ? "Pitch (C4 +)"
                              : (midi ? "Pitch (note +)" : "Pitch"),
                          juce::dontSendNotification);

    pitchView.setScale (p.mode, p.key, p.scale);

    const double rate = processor.getSampleRateForDisplay();
    const int latency = processor.getEngine().getLatencySamples();
    statusLabel.setText ("Latency " + juce::String (rate > 0.0 ? latency * 1000.0 / rate : 0.0, 1) + " ms",
                         juce::dontSendNotification);
}

void MantaShifterEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    // 下の帯の地（8.342：配置と同じ矩形。前は塗る位置と置く位置を別々に計算していて、
    // 見出しが帯の上端に貼り付いていた）
    g.setColour (theme.panel);
    g.fillRoundedRectangle (bandArea.toFloat(), AppColours::corner (6.0f));
}

void MantaShifterEditor::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (MantaPluginToolbar::preferredHeight));

    // 8.342：エンジンは**プリセットの右横**へ（本人の指定）。右端にはレイテンシー
    {
        auto trailing = toolbar.getTrailingArea();
        statusLabel.setBounds (trailing.removeFromRight (statusWidth).withTrimmedRight (2));

        auto engineRow = trailing.withWidth (juce::jmin (engineGroupWidth, trailing.getWidth()));
        engineCaption.setBounds (engineRow.removeFromLeft (engineCaptionWidth));
        engineSwitch.setBounds (engineRow);   // 8.339：「1」＋つまみ＋「2」はスイッチが自分で描く
    }

    area.reduce (10, 6);

    // 下の帯は先に取る（地を塗る矩形もこれ。左右は窓の端から 8 px）
    bandArea = getLocalBounds().withTrimmedBottom (6).removeFromBottom (bandHeight).reduced (8, 0);
    area.setBottom (bandArea.getY() - 10);

    //--------------------------------------------------------------------------
    // 上：つまみ2つとモード（左）、ピッチの表示（右）
    auto top = area;
    auto left = top.removeFromLeft (380);
    top.removeFromLeft (10);

    // 8.343：表示は左の列と同じ高さ（下端＝モードのボタンの下端。本人の指定「縦幅を統一」）。
    // 前は下に MIDI の案内の行（18 px）を取っていて、そのぶん短かった。案内は表示の中に描く
    pitchView.setBounds (top.withTrimmedBottom (2));

    auto modes = left.removeFromBottom (32);
    left.removeFromBottom (6);

    // Pitch ─ Link ─ Formant。エンジンの行が抜けたぶん（8.342）は、上下に等しく分けて真ん中に
    auto knobs = left.withSizeKeepingCentre (left.getWidth(), juce::jmin (left.getHeight(), 232));

    const int bigWidth = 150;
    auto pitchColumn = knobs.removeFromLeft (bigWidth);
    auto formantColumn = knobs.removeFromRight (bigWidth);

    pitchCaption.setBounds (pitchColumn.removeFromTop (20));
    pitchSlider.setBounds (pitchColumn.reduced (4, 0));
    formantCaption.setBounds (formantColumn.removeFromTop (20));
    formantSlider.setBounds (formantColumn.reduced (4, 0));

    linkButton.setBounds (knobs.withSizeKeepingCentre (juce::jmin (knobs.getWidth() - 8, 64), 26));

    const int modeWidth = modes.getWidth() / 4;

    for (auto& button : modeButtons)
        button.setBounds (modes.removeFromLeft (modeWidth).reduced (2, 2));

    //--------------------------------------------------------------------------
    // 下の帯（8.342：帯の内側に余白。前は見出しが帯の上端に貼り付いていた）
    auto bottom = bandArea.reduced (bandPaddingX, bandPaddingY);

    auto knobColumn = [&bottom] (ValueEntrySlider& slider, juce::Label& caption)
    {
        auto column = bottom.removeFromLeft (72);
        caption.setBounds (column.removeFromTop (14));
        slider.setBounds (column.reduced (4, 0));
        bottom.removeFromLeft (4);
    };

    auto comboColumn = [&bottom] (juce::ComboBox& box, juce::Label& caption, int width)
    {
        auto column = bottom.removeFromLeft (width);
        caption.setBounds (column.removeFromTop (14));
        box.setBounds (column.withSizeKeepingCentre (width - 6, 26));
        bottom.removeFromLeft (4);
    };

    auto buttonColumn = [&bottom] (juce::TextButton& button, int width)
    {
        auto column = bottom.removeFromLeft (width);
        button.setBounds (column.withTrimmedTop (14).withSizeKeepingCentre (width - 6, 26));
        bottom.removeFromLeft (4);
    };

    comboColumn (keyBox, keyCaption, 70);
    comboColumn (scaleBox, scaleCaption, 110);
    knobColumn (retuneSlider, retuneCaption);
    buttonColumn (midiHoldButton, 84);

    bottom.removeFromLeft (10);   // 補正の組と出力の組のあいだ

    buttonColumn (driveOnButton, 64);
    knobColumn (driveSlider, driveCaption);
    knobColumn (mixSlider, mixCaption);
    knobColumn (outputSlider, outputCaption);
    buttonColumn (bypassButton, 72);
}

void MantaShifterEditor::timerCallback()
{
    // テーマが変わったら作り直す（DAWのテーマ変更に即時追従）
    if (MantaTheme::accent() != lastMain || MantaTheme::curve() != lastSub || AppColours::getTheme() != lastDawTheme)
        applyTheme();

    ShifterEngine::DisplayFrame frame;
    bool any = false;

    while (processor.getEngine().popDisplay (frame))
    {
        pitchView.addFrame (frame);
        any = true;
    }

    if (any)
        pitchView.repaint();

    refreshControls();
}

//==============================================================================
// 8.339：エンジンのトグルスイッチ

void ShifterEngineSwitch::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused (down);

    const bool spectral = getToggleState();
    auto area = getLocalBounds();

    // 真ん中につまみの溝、左に「1」（PSOLA）、右に「2」（Spectral）
    const auto track = area.withSizeKeepingCentre (trackWidth, trackHeight).toFloat();
    const auto left = area.withRight ((int) track.getX() - 6);
    const auto right = area.withLeft ((int) track.getRight() + 6);

    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.setColour (spectral ? theme.textDim : theme.main);
    g.drawText ("1", left, juce::Justification::centredRight);   // 8.340：数字だけ（本人の指定。名前はツールチップ）
    g.setColour (spectral ? theme.main : theme.textDim);
    g.drawText ("2", right, juce::Justification::centredLeft);

    // 溝はどちらの側でもメインの色（どちらを選んでも「効いている」ので、オン／オフの色分けはしない）
    g.setColour (highlighted ? theme.mainStrong : theme.main);
    g.fillRoundedRectangle (track, AppColours::corner (track.getHeight() * 0.5f));

    const float diameter = track.getHeight() - 4.0f;
    const float x = spectral ? track.getRight() - 2.0f - diameter : track.getX() + 2.0f;
    g.setColour (juce::Colours::white);   // つまみは白（ふつうのトグルと同じ。`onMain`だと紫の上で黒になり、沈んで見えた）
    g.fillRoundedRectangle (x, track.getY() + 2.0f, diameter, diameter, AppColours::corner (diameter * 0.5f));   // 8.342：角ばらせているときは四角いつまみ（溝と揃える）

    if (hasKeyboardFocus (false))
    {
        g.setColour (theme.mainStrong);
        g.drawRoundedRectangle (track.expanded (2.0f), AppColours::corner (track.getHeight() * 0.5f + 2.0f), 1.0f);
    }
}
