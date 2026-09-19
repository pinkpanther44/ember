#include "JavaRhinoBassEditor.h"

#include "JavaRhinoBassPresets.h" // 8.259：工場プリセット（Phase 267）
#include "../../AppIcon.h"        // 8.229：埋め込みの絵を名前で引く（Phase 249）
#include "../../Branding.h"
#include "../../ChordModel.h"     // 8.121：音名の数え方（`midiNoteName()`。Phase 156）
#include "../../SegmentDisplay.h" // 8.177：Emberの7セグ（Phase 219）
#include "../../Utf8.h"

namespace
{
    /** 画像の名前。**CMakeが`Resources/Plugins/java_rhino_bass_field.jpg`を埋め込みます**。 */
    const char* const fieldImageName = "java_rhino_bass_field_jpg";
}

//==============================================================================

void BassFieldLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPosProportional,
                                              float rotaryStartAngle, float rotaryEndAngle,
                                              juce::Slider& slider)
{
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float diameter = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();

    const float arcRadius = diameter * 0.5f - arcThickness * 0.5f;
    const float bodyRadius = juce::jmax (4.0f, arcRadius - arcThickness * 0.5f - arcGap);

    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const auto fillColour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();
    const auto accent = enabled ? fillColour : fillColour.withMultipliedAlpha (0.45f);

    // ① 影。**濃い絵の上でも、円が置かれていることを見せる**
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f + 3.0f, bodyRadius * 2.0f + 3.0f)
                       .withCentre (centre.translated (0.0f, 1.5f)));

    // ② 弧の地（端から端まで）
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (JavaRhinoBassTheme::fieldKnobTrack());
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    // ③ いまの値ぶんの弧
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

    // ④ 本体。**塗ること**
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (JavaRhinoBassTheme::fieldKnobBody());
    g.fillEllipse (body);

    g.setColour (accent.withAlpha (enabled ? 0.6f : 0.25f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // ⑤ 指針
    juce::Path pointer;
    const float thickness = juce::jmax (1.6f, bodyRadius * 0.16f);

    pointer.addRoundedRectangle (-thickness * 0.5f, -bodyRadius * 0.88f,
                                  thickness, bodyRadius * 0.55f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (enabled ? JavaRhinoBassTheme::fieldInk() : JavaRhinoBassTheme::fieldInkDim());
    g.fillPath (pointer);
}

void BassFieldLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // **編集中とEmberの7セグは基底に任せる**（クラスの説明）
    if (label.isBeingEdited() || SevenSegment::canDraw (label.getText()))
    {
        MantaKnobLookAndFeel::drawLabel (g, label);
        return;
    }

    const float alpha = label.isEnabled() ? 1.0f : 0.5f;

    // 8.274：**縁は描きません**（Phase 273／本人の指定「縁は無し」）。
    //
    // Phase 272までは**暗い縁を4方向に置いてから白い字**を重ねていました（8.259）。
    // あれは**濃い絵の上に文字を浮かせる**ための手当てで、絵を淡いものへ替えたいま、
    // 白い縁だけが浮きます。**絵に合わせた手当ては、絵と一緒に畳むこと。**
    //
    // **太字だけは残します。** つまみの下の小さい数値は、絵の階調の上では
    // 細いと読みにくいままです（`Racco Guitar`の絵より階調の幅が広い）。
    g.setFont (juce::Font (juce::FontOptions (label.getFont().getHeight(), juce::Font::bold)));
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.drawFittedText (label.getText(), label.getLocalBounds(), label.getJustificationType(), 1, 1.0f);
}

//==============================================================================

BassFieldKnob::BassFieldKnob (const juce::String& name, juce::Colour arcColour)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, JavaRhinoBassTheme::fieldInk());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setColour (juce::Slider::rotarySliderFillColourId, arcColour);
    slider.setColour (juce::Slider::textBoxTextColourId, JavaRhinoBassTheme::fieldInk());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, arcColour.withAlpha (0.35f));

    // **読み取り専用の欄**（打ち込みは右クリック。`ValueEntrySlider`）
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, valueHeight);
    addAndMakeVisible (slider);
}

void BassFieldKnob::resized()
{
    auto area = getLocalBounds();

    label.setBounds (area.removeFromTop (labelHeight));
    slider.setBounds (area);
}

//==============================================================================

BassStyleChip::BassStyleChip (const juce::String& noteNameToUse, const juce::String& styleNameToUse,
                               juce::Colour onColourToUse)
    : juce::Button (styleNameToUse), noteName (noteNameToUse), onColour (onColourToUse)
{
    setButtonText (styleNameToUse);
}

void BassStyleChip::paintButton (juce::Graphics& g, bool isMouseOver, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 4.0f;

    const bool on = getToggleState();

    auto fill = on ? onColour : MantaTheme::graphBackground();

    if (isDown)
        fill = fill.darker (0.15f);
    else if (isMouseOver)
        fill = on ? fill.brighter (0.12f) : fill.brighter (0.25f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (on ? onColour.darker (0.25f) : MantaTheme::border());
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    const auto textColour = on ? juce::Colours::white : MantaTheme::text();

    g.setColour (textColour.withAlpha (on ? 0.8f : 0.55f));
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.drawText (noteName, bounds.reduced (6.0f, 3.0f).removeFromTop (11.0f),
                 juce::Justification::centredLeft, false);

    g.setColour (textColour);
    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText (getButtonText(), bounds.reduced (6.0f, 3.0f).withTrimmedTop (10.0f),
                 juce::Justification::centredLeft, false);
}

//==============================================================================

void BassStyleBadge::setState (BassStyle style, float sustainSeconds)
{
    if (current == style && std::abs (sustain - sustainSeconds) < 0.01f)
        return;

    current = style;
    sustain = sustainSeconds;
    repaint();
}

void BassStyleBadge::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    // **奏法の表をそのまま引くこと**（`getStyleTone()`。数字を2箇所に書かない。1.27）
    const auto& styleTone = getStyleTone (current);

    const float effective = styleTone.t60Fixed > 0.0f ? styleTone.t60Fixed
                                                       : sustain * styleTone.t60Mul;

    auto curveArea = bounds.reduced (7.0f, 4.0f).withTrimmedTop (13.0f);

    juce::Path curve;
    curve.startNewSubPath (curveArea.getX(), curveArea.getY());

    constexpr float windowSeconds = 15.0f;   // 横幅＝Sustainの最大値
    const int steps = juce::jmax (8, (int) curveArea.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps * windowSeconds;
        const float amplitude = std::pow (0.001f, t / juce::jmax (0.02f, effective));

        curve.lineTo (curveArea.getX() + curveArea.getWidth() * (float) i / (float) steps,
                       curveArea.getBottom() - curveArea.getHeight() * amplitude);
    }

    g.setColour (JavaRhinoBassTheme::accent());
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    auto top = bounds.reduced (7.0f, 3.0f).removeFromTop (12.0f);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText ("STYLE", top, juce::Justification::centredLeft, false);

    g.setColour (JavaRhinoBassTheme::highlight());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (JavaRhinoBassEditor::styleName (current), top,
                 juce::Justification::centredRight, false);
}

//==============================================================================

void JavaRhinoBassKeyboard::setStyleChoice (int choice)
{
    if (currentChoice == choice)
        return;

    currentChoice = choice;
    repaint();
}

bool JavaRhinoBassKeyboard::isOutOfRange (int midiNoteNumber)
{
    // **上側だけ**（下はB0で打ち止め＝鍵盤の左端）
    return midiNoteNumber > JavaRhinoBassProcessor::highestNote
        && ! JavaRhinoBassProcessor::isKeySwitchNote (midiNoteNumber);
}

void JavaRhinoBassKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                            juce::Rectangle<float> area, bool isDown, bool isOver,
                                            juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver,
                                                 lineColour, textColour);

    if (JavaRhinoBassProcessor::isKeySwitchNote (midiNoteNumber))
    {
        const bool selected =
            midiNoteNumber == JavaRhinoBassProcessor::keySwitchNoteForChoice (currentChoice);

        g.setColour ((selected ? JavaRhinoBassTheme::highlight() : JavaRhinoBassTheme::accent())
                         .withAlpha (isDown ? 0.9f : 0.62f));
        g.fillRect (area);

        g.setColour (lineColour);
        g.drawRect (area);
        return;
    }

    if (isOutOfRange (midiNoteNumber))
    {
        // **押しても鳴らない音**（G#4〜B4）
        g.setColour (MantaTheme::panelBackground().withAlpha (0.72f));
        g.fillRect (area);

        g.setColour (lineColour);
        g.drawRect (area);
    }
}

void JavaRhinoBassKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                                            juce::Rectangle<float> area, bool isDown, bool isOver,
                                            juce::Colour noteFillColour)
{
    if (JavaRhinoBassProcessor::isKeySwitchNote (midiNoteNumber))
    {
        const bool selected =
            midiNoteNumber == JavaRhinoBassProcessor::keySwitchNoteForChoice (currentChoice);

        auto fill = selected ? JavaRhinoBassTheme::highlight() : JavaRhinoBassTheme::accent();

        if (isDown)      fill = fill.darker (0.3f);
        else if (isOver) fill = fill.brighter (0.15f);

        g.setColour (fill);
        g.fillRect (area);

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRect (area);
        return;
    }

    juce::MidiKeyboardComponent::drawBlackNote (midiNoteNumber, g, area, isDown, isOver,
                                                 noteFillColour);

    if (isOutOfRange (midiNoteNumber))
    {
        g.setColour (MantaTheme::panelBackground().withAlpha (0.55f));
        g.fillRect (area);
    }
}

//==============================================================================

juce::String JavaRhinoBassEditor::styleName (BassStyle style)
{
    // **訳しません**（つまみの名前と同じ扱い。用語は英語）
    switch (style)
    {
        case BassStyle::Finger:   return "FINGER";
        case BassStyle::Pick:     return "PICK";
        case BassStyle::Slap:     return "SLAP";
        case BassStyle::Pop:      return "POP";
        case BassStyle::Mute:     return "MUTE";
        case BassStyle::Ghost:    return "GHOST";
        case BassStyle::Harmonic: return "HARM";
        case BassStyle::NumStyles:
        default:                  return "FINGER";
    }
}

juce::String JavaRhinoBassEditor::blendText (float blend)
{
    if (blend < 0.02f) return "NECK";
    if (blend > 0.98f) return "BRIDGE";
    if (std::abs (blend - 0.5f) < 0.02f) return "BOTH";

    return blend < 0.5f ? "NECK " + juce::String (juce::roundToInt ((0.5f - blend) * 200.0f)) + "%"
                        : "BRIDGE " + juce::String (juce::roundToInt ((blend - 0.5f) * 200.0f)) + "%";
}

//==============================================================================

JavaRhinoBassEditor::JavaRhinoBassEditor (JavaRhinoBassProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::bassPresetFolder),
      keyboard (processorToUse.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (toolbar);

    toolbar.onStateRestored = [this] { repaint(); };

    // 8.259：**出来合いの音**（Phase 267／本人の要望。8.173と同じ理由）
    toolbar.setFactoryPresets (
        MantaFactoryPresets::makeToolbarPresets (processorToUse.getValueTreeState(),
                                                  JavaRhinoBassPresets::all()));

    // 8.186：**名前を直に書かないこと**（`Branding.h`が唯一の出どころ）
    titleLabel.setText (Branding::bassPluginName, juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    titleLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredRight);
    toolbar.addAndMakeVisible (titleLabel);

    fieldImage = AppIcon::loadEmbedded (fieldImageName);

    //--------------------------------------------------------------------------
    // 案内の帯
    defaultHint = utf8 ("コントロールにカーソルを合わせると、ここに説明が出ます");

    hintLabel.setText (defaultHint, juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    hintLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (badge);
    registerHint (badge, utf8 ("いまの奏法と、その減衰のかたち。MuteとGhostはSustainに関係なく短く切れます"));

    //--------------------------------------------------------------------------
    // 出力
    auto& state = processor.getValueTreeState();

    outputSlider.setLookAndFeel (&fieldLookAndFeel.get());
    outputSlider.setColour (juce::Slider::trackColourId, JavaRhinoBassTheme::accent());
    outputSlider.setColour (juce::Slider::thumbColourId, JavaRhinoBassTheme::accent());
    outputSlider.setColour (juce::Slider::backgroundColourId, MantaTheme::graphBackground());
    addAndMakeVisible (outputSlider);
    styledSliders.push_back (&outputSlider);

    sliderAttachments.add (new SliderAttachment (state, JavaRhinoBassParams::gain, outputSlider));

    outputValue.setColour (juce::Label::textColourId, MantaTheme::text());
    outputValue.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    outputValue.setJustificationType (juce::Justification::centredLeft);
    outputValue.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (outputValue);

    outputSlider.onValueChange = [this]
    {
        outputValue.setText (juce::String (outputSlider.getValue(), 2), juce::dontSendNotification);
    };
    outputSlider.onValueChange();

    registerHint (outputSlider, utf8 ("このプラグインから出る音量"));

    //--------------------------------------------------------------------------
    // BLEND（**`Racco Guitar`のPICKUPと同じ場所**）
    blendSlider.setLookAndFeel (&fieldLookAndFeel.get());
    blendSlider.setColour (juce::Slider::trackColourId, JavaRhinoBassTheme::accent());
    blendSlider.setColour (juce::Slider::thumbColourId, JavaRhinoBassTheme::accent());
    blendSlider.setColour (juce::Slider::backgroundColourId, MantaTheme::graphBackground());
    addAndMakeVisible (blendSlider);
    styledSliders.push_back (&blendSlider);

    sliderAttachments.add (new SliderAttachment (state, JavaRhinoBassParams::blend, blendSlider));

    blendValue.setColour (juce::Label::textColourId, MantaTheme::text());
    blendValue.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    blendValue.setJustificationType (juce::Justification::centredLeft);
    blendValue.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (blendValue);

    blendSlider.onValueChange = [this]
    {
        blendValue.setText (blendText ((float) blendSlider.getValue()), juce::dontSendNotification);
    };
    blendSlider.onValueChange();

    registerHint (blendSlider, utf8 ("2つのピックアップの混ぜ具合。真ん中で両方フル（ジャズベの定番。中域がへこみます）"));

    //--------------------------------------------------------------------------
    // RESET / LEGATO
    MantaPluginToolbar::styleButton (resetButton, "RESET");
    addAndMakeVisible (resetButton);

    resetButton.onClick = [this]
    {
        for (auto* parameter : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
    };

    registerHint (resetButton, utf8 ("つまみと奏法を、最初の状態へ戻します"));

    MantaPluginToolbar::styleButton (legatoButton, "LEGATO");
    legatoButton.setClickingTogglesState (true);
    legatoButton.setColour (juce::TextButton::buttonOnColourId, JavaRhinoBassTheme::accent());
    legatoButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (legatoButton);

    buttonAttachments.add (new ButtonAttachment (state, JavaRhinoBassParams::legato, legatoButton));

    registerHint (legatoButton, utf8 ("押さえたまま次を弾いたときに、弾き直さず音程だけ変えます（切ると全部弾き直しになります）"));

    //--------------------------------------------------------------------------
    // 奏法のチップ
    const auto styleNames = JavaRhinoBassParams::getStyleNames();

    for (size_t i = 0; i < chips.size(); ++i)
    {
        const int choice = (int) i;

        auto chip = std::make_unique<BassStyleChip> (
            // 8.121：**音名の数え方は`midiNoteName()`に1本化**（C4＝60）
            midiNoteName (JavaRhinoBassProcessor::keySwitchNoteForChoice (choice)),
            styleNames[choice].toUpperCase(),
            JavaRhinoBassTheme::highlight());

        chip->onClick = [this, choice] { setStyleChoice (choice); };

        addAndMakeVisible (*chip);
        registerHint (*chip, utf8 ("鍵盤のキースイッチと同じ切り替えです（音は鳴りません）"));

        chips[i] = std::move (chip);
    }

    //--------------------------------------------------------------------------
    // 絵の上のつまみ（**グリーン＝弦そのもの／ブルー＝弾き方**）
    const auto green = JavaRhinoBassTheme::fieldAccent();
    const auto blue  = JavaRhinoBassTheme::fieldHighlight();

    addKnob (brightness, "BRIGHT",    JavaRhinoBassParams::brightness, green,
              utf8 ("音色の明るさ。基音の6〜96倍のところで切ります（どの音程でも倍音の構成がそろいます）"));
    addKnob (sustain,    "SUSTAIN",   JavaRhinoBassParams::sustain,    green,
              utf8 ("弦が鳴り続ける秒数。どの音程でも同じ秒数だけ鳴ります"));
    addKnob (pluckPos,   "PLUCK POS", JavaRhinoBassParams::pluckPos,   green,
              utf8 ("弾く位置。ブリッジ寄り（小さい値）ほど硬い音になります"));
    addKnob (tone,       "TONE",      JavaRhinoBassParams::tone,       green,
              utf8 ("パッシブトーン。出口で高い側を削ります（800Hz〜12kHz）"));
    addKnob (hardness,   "HARDNESS",  JavaRhinoBassParams::hardness,   blue,
              utf8 ("指やピックの硬さ。励起の明るさの基準になります"));
    addKnob (attack,     "ATTACK",    JavaRhinoBassParams::attack,     blue,
              utf8 ("弾いた瞬間のノイズの量。レガートには付きません"));
    addKnob (clank,      "CLANK",     JavaRhinoBassParams::clank,      blue,
              utf8 ("スラップのときの、弦がフレットに当たる音。ほかの奏法では効きません"));

    // 秒とパーセントだけ単位を添えます
    sustain->slider.setTextValueSuffix (" s");

    // 8.291：**数値は0.0〜10.0**（Phase 284／本人の指定。`Orangutan Drums`と揃えました）。
    //
    // **`SUSTAIN`だけ秒のまま**です（本人の指定）——「9秒」は**そのものが読みたい値**で、
    // 0〜10にすると位置しか分からなくなります。
    //
    // > `TONE`は元から0〜10でした（`JavaRhinoBassParameters.cpp`）。**数字は変わりません**が、
    // > ここを通しておくと、範囲を後で変えたときに**表示だけは0〜10のまま**でいられます。
    //
    // 繋いだ後に入れ直す必要はありません（`getTextFromValue()`の側なので。8.290）
    for (auto* knob : { brightness.get(), pluckPos.get(), tone.get(),
                         hardness.get(), attack.get(), clank.get() })
        knob->slider.setDisplayUnit (ValueEntrySlider::DisplayUnit::zeroToTen);

    //--------------------------------------------------------------------------
    // 鍵盤
    keyboard.setAvailableRange (JavaRhinoBassProcessor::lowestNote,
                                 JavaRhinoBassProcessor::keyboardHighNote);
    keyboard.setScrollButtonsVisible (false);

    // **C4＝60の流儀**（本体のピアノロールと同じ。8.121）
    keyboard.setOctaveForMiddleC (4);

    int whiteKeys = 0;

    for (int note = JavaRhinoBassProcessor::lowestNote;
         note <= JavaRhinoBassProcessor::keyboardHighNote; ++note)
        if (! juce::MidiMessage::isMidiNoteBlack (note))
            ++whiteKeys;

    keyboard.setKeyWidth ((float) contentW / (float) juce::jmax (1, whiteKeys));

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, AppColours::pianoWhiteKey);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, AppColours::pianoBlackKey);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, MantaTheme::border());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                         JavaRhinoBassTheme::accent());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                         JavaRhinoBassTheme::accent().withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    registerHint (keyboard, utf8 ("B0からG4までが鳴ります。右端の色つきの6鍵は奏法の切り替えです"));

    //--------------------------------------------------------------------------
    addMouseListener (this, true);

    startTimerHz (12);
    timerCallback();

    // 8.172：**大きさは固定**
    setSize (fixedWidth, fixedHeight);
}

JavaRhinoBassEditor::~JavaRhinoBassEditor()
{
    removeMouseListener (this);

    for (auto* slider : styledSliders)
        slider->setLookAndFeel (nullptr);

    for (auto* component : styledComponents)
        component->setLookAndFeel (nullptr);
}

//==============================================================================

void JavaRhinoBassEditor::addKnob (std::unique_ptr<BassFieldKnob>& knob, const juce::String& name,
                                    const juce::String& parameterId, juce::Colour colour,
                                    const juce::String& hint)
{
    knob = std::make_unique<BassFieldKnob> (name, colour);

    // 箱ごと被せる（**見出しのラベルにも縁が届く**。`styledComponents`の説明）
    knob->setLookAndFeel (&fieldLookAndFeel.get());
    styledComponents.push_back (knob.get());

    knob->slider.setLookAndFeel (&fieldLookAndFeel.get());
    knob->slider.setPopupDisplayEnabled (false, false, this);   // 数値は出しっぱなし

    addAndMakeVisible (*knob);
    styledSliders.push_back (&knob->slider);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  parameterId, knob->slider));

    // **桁をそろえる。** `setNumDecimalPlacesToDisplay()`では変わりません
    // ——`SliderAttachment`が`textFromValueFunction`を差し替えており、
    // そちらが先に効くためです（8.256で1度踏みました）
    knob->slider.textFromValueFunction = [] (double value) { return juce::String (value, 2); };
    knob->slider.updateText();

    registerHint (*knob, hint);
}

void JavaRhinoBassEditor::registerHint (juce::Component& component, const juce::String& text)
{
    hints[&component] = text;
}

void JavaRhinoBassEditor::showHint (const juce::String& text)
{
    if (hintLabel.getText() == text)
        return;

    hintLabel.setText (text, juce::dontSendNotification);
}

void JavaRhinoBassEditor::setStyleChoice (int choice)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (JavaRhinoBassParams::style))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) choice));
        parameter->endChangeGesture();
    }
}

void JavaRhinoBassEditor::mouseEnter (const juce::MouseEvent& event)
{
    // **親をたどること**（つまみの中の数値欄やラベルからも来ます）
    for (auto* component = event.eventComponent; component != nullptr && component != this;
         component = component->getParentComponent())
    {
        const auto found = hints.find (component);

        if (found != hints.end())
        {
            showHint (found->second);
            return;
        }
    }

    showHint (defaultHint);
}

void JavaRhinoBassEditor::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    showHint (defaultHint);
}

//==============================================================================

void JavaRhinoBassEditor::timerCallback()
{
    auto& state = processor.getValueTreeState();

    const int choice = (int) state.getRawParameterValue (JavaRhinoBassParams::style)->load();
    const int voices = processor.getActiveVoiceCount();

    if (choice != lastStyleChoice)
    {
        lastStyleChoice = choice;

        for (size_t i = 0; i < chips.size(); ++i)
            chips[i]->setToggleState ((int) i == choice, juce::dontSendNotification);

        keyboard.setStyleChoice (choice);
    }

    badge.setState (JavaRhinoBassParams::styleFromChoice (choice),
                     state.getRawParameterValue (JavaRhinoBassParams::sustain)->load());

    if (voices != lastVoiceCount)
    {
        lastVoiceCount = voices;
        repaint (voicesX, headerY, contentX + contentW - voicesX, headerH);
    }
}

//==============================================================================

void JavaRhinoBassEditor::drawTracked (juce::Graphics& g, const juce::String& text,
                                        juce::Point<float> origin, const juce::Font& font,
                                        float tracking)
{
    g.setFont (font);

    float x = origin.x;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto character = text.substring (i, i + 1);

        g.drawSingleLineText (character, juce::roundToInt (x), juce::roundToInt (origin.y));

        // **`Font::getStringWidthFloat()`はJUCE 9で無くなっています**（2章）
        x += juce::GlyphArrangement::getStringWidth (font, character) + tracking;
    }
}

void JavaRhinoBassEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void JavaRhinoBassEditor::drawBox (juce::Graphics& g, juce::Rectangle<int> area,
                                    const juce::String& title) const
{
    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (area.toFloat(), 6.0f);

    g.setColour (MantaTheme::border().withAlpha (0.6f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (MantaTheme::textDim());
    drawTracked (g, title, { (float) area.getX() + 10.0f, (float) area.getY() + 17.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);
}

void JavaRhinoBassEditor::drawField (juce::Graphics& g) const
{
    const juce::Rectangle<int> area (contentX, fieldY, contentW, fieldH);

    juce::Path rounded;
    rounded.addRoundedRectangle (area.toFloat(), 10.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rounded);

        if (fieldImage.isValid())
        {
            // **切り取らずに伸ばします**（絵は16:9、置き場所は約3:1。8.256）
            g.drawImage (fieldImage, area.toFloat(), juce::RectanglePlacement::stretchToFit);

            // **白を重ねる**。8.274で絵を淡いものへ替えたので、厚さもギターと同じ
            // 0.22になりました（`JavaRhinoBassTheme::fieldVeil()`）
            g.setColour (JavaRhinoBassTheme::fieldVeil());
            g.fillRect (area);
        }
        else
        {
            g.setColour (JavaRhinoBassTheme::fieldKnobBody());
            g.fillRect (area);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);

    g.setColour (JavaRhinoBassTheme::fieldInkDim());
    drawTracked (g, "JAVA RHINO / STRING FIELD",
                  { (float) area.getX() + 24.0f, (float) area.getY() + 30.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.6f);
}

void JavaRhinoBassEditor::drawWordmark (juce::Graphics& g) const
{
    g.setColour (MantaTheme::text());
    drawTracked (g, "JAVA RHINO", { (float) wordmarkX, (float) headerY + 34.0f },
                  juce::Font (juce::FontOptions (21.0f, juce::Font::bold)), 2.0f);

    g.setColour (JavaRhinoBassTheme::accent());
    drawTracked (g, "BASS", { (float) wordmarkX + 2.0f, (float) headerY + 52.0f },
                  juce::Font (juce::FontOptions (10.0f, juce::Font::bold)), 4.2f);
}

void JavaRhinoBassEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    //--------------------------------------------------------------------------
    // 上の帯
    drawPanel (g, { contentX, headerY, contentW, headerH });
    drawWordmark (g);

    const juce::Rectangle<int> hintArea (hintX, headerY + 14, hintW, headerH - 28);

    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (hintArea.toFloat(), 6.0f);

    g.setColour (MantaTheme::border().withAlpha (0.6f));
    g.drawRoundedRectangle (hintArea.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (JavaRhinoBassTheme::highlight());
    g.fillEllipse ((float) hintArea.getX() + 11.0f, (float) hintArea.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "OUTPUT", { (float) outputLabelX, (float) headerY + 41.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);

    // 鳴っている声の数
    {
        const juce::Rectangle<int> area (voicesX, headerY, contentX + contentW - voicesX - 20, headerH);

        g.setColour (JavaRhinoBassTheme::accent());
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (lastVoiceCount), area.withTrimmedRight (46),
                     juce::Justification::centredRight, false);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText ("VOICES", area, juce::Justification::centredRight, false);
    }

    //--------------------------------------------------------------------------
    // 2つめの帯
    drawPanel (g, { contentX, controlY, contentW, controlH });

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "A PHYSICAL MODELING BASS",
                  { (float) leftBoxX, (float) rowY + 19.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.4f);

    drawTracked (g, "BLEND", { (float) blendLabelX, (float) rowY + 19.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);

    drawBox (g, { leftBoxX, boxY, leftBoxW, boxH }, "STRING MODEL");
    drawBox (g, { rightBoxX, boxY, rightBoxW, boxH }, "ARTICULATION");

    g.setColour (MantaTheme::text());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText ("863.6 mm SCALE / 5 STRINGS B-E-A-D-G / KARPLUS-STRONG",
                 leftBoxX + 10, boxY + 24, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("B0 - G4 PLAYABLE / C5 - F5 KEY SWITCHES / CC1 VIBRATO",
                 leftBoxX + 10, boxY + 40, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    //--------------------------------------------------------------------------
    drawField (g);
}

//==============================================================================

void JavaRhinoBassEditor::resized()
{
    toolbar.setBounds (contentX, toolbarY, contentW, MantaPluginToolbar::preferredHeight);
    titleLabel.setBounds (toolbar.getTrailingArea().withTrimmedRight (6));

    hintLabel.setBounds (hintX + 24, headerY + 14, hintW - 34, headerH - 28);
    badge.setBounds (badgeX, headerY + 10, badgeW, headerH - 20);

    outputSlider.setBounds (outputSliderX, headerY + 24, outputSliderW, 20);
    outputValue.setBounds (outputValueX, headerY + 26, 44, 16);

    resetButton.setBounds (resetX, rowY, resetW, rowH);
    legatoButton.setBounds (legatoX, rowY, legatoW, rowH);

    blendSlider.setBounds (blendSliderX, rowY + 4, blendSliderW, 20);
    blendValue.setBounds (blendValueX, rowY + 6, blendValueW, 16);

    //--------------------------------------------------------------------------
    // 奏法のチップ（**余りを最後の1つに寄せない**）
    const float chipsX = (float) (rightBoxX + chipInset);
    const float chipsW = (float) (rightBoxW - chipInset * 2);
    const float chipW = (chipsW - (float) chipGap * (float) (numBassStyles - 1))
                         / (float) numBassStyles;

    for (size_t i = 0; i < chips.size(); ++i)
    {
        const float x = chipsX + (chipW + (float) chipGap) * (float) i;

        chips[i]->setBounds (juce::Rectangle<float> (x, (float) (boxY + 26), chipW,
                                                      (float) chipHeight).toNearestInt());
    }

    //--------------------------------------------------------------------------
    // 絵の上のつまみ（**割合で置く**）
    BassFieldKnob* const fieldKnobs[7]
    {
        brightness.get(), sustain.get(), pluckPos.get(), tone.get(),
        hardness.get(), attack.get(), clank.get()
    };

    for (int i = 0; i < 7; ++i)
    {
        if (fieldKnobs[i] == nullptr)
            continue;

        const auto centreX = (float) contentX + (float) contentW * knobPlacement[i].x;
        const auto centreY = (float) fieldY + (float) fieldH * knobPlacement[i].y;

        fieldKnobs[i]->setBounds (juce::Rectangle<float> ((float) knobCellW, (float) knobCellH)
                                       .withCentre ({ centreX, centreY })
                                       .toNearestInt());
    }

    keyboard.setBounds (contentX, keyboardY, contentW, keyboardH);
}
