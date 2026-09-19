#include "RaccoGuitarEditor.h"

#include "RaccoGuitarPresets.h" // 8.259：工場プリセット（Phase 267）
#include "../../AppIcon.h"     // 8.229：埋め込みの絵を名前で引く（Phase 249）
#include "../../Branding.h"
#include "../../ChordModel.h"  // 8.121：音名の数え方（`midiNoteName()`。Phase 156）
#include "../../Utf8.h"

namespace
{
    /** 画像の名前。**CMakeが`Resources/Plugins/racco_guitar_field.jpg`を埋め込みます**。 */
    const char* const fieldImageName = "racco_guitar_field_jpg";

    /** 奏法の並び。**チップの並び順**で、`GuitarArticulation`の番号とは別です
        ——鍵盤の**キースイッチの並び**（A#1から右へ）に合わせてあります。 */
    constexpr GuitarArticulation chipOrder[numGuitarArticulations]
    {
        GuitarArticulation::brushing,        // A#1
        GuitarArticulation::pinchHarmonic,   // B1
        GuitarArticulation::normal,          // C2
        GuitarArticulation::slide,           // C#2
        GuitarArticulation::palmMute,        // D2
        GuitarArticulation::harmonic,        // D#2
    };
}

//==============================================================================

void RaccoFieldLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                               float sliderPosProportional,
                                               float rotaryStartAngle, float rotaryEndAngle,
                                               juce::Slider& slider)
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
    // ① 影。**画像の上なので必要です**——地と同じ明るさの円を置くと、
    //    絵に溶けて「浮いているもの」に見えません
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f + 3.0f, bodyRadius * 2.0f + 3.0f)
                       .withCentre (centre.translated (0.0f, 1.5f)));

    //--------------------------------------------------------------------------
    // ② 弧の地（端から端まで）。**薄く残すこと**——どこまで回せるのかが見えないと、
    //    端に当たったのか壊れたのか分からない
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (RaccoGuitarTheme::fieldKnobTrack());
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    //--------------------------------------------------------------------------
    // ③ いまの値ぶんの弧。真ん中が0のつまみは**12時から**伸ばす
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
    // ④ 本体。**塗ること**：画像が透けると指針が読めません
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (RaccoGuitarTheme::fieldKnobBody());
    g.fillEllipse (body);

    g.setColour (accent.withAlpha (enabled ? 0.55f : 0.25f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    //--------------------------------------------------------------------------
    // ⑤ 指針。**中心までは引かない**（真ん中が詰まると、小さいときに点に見える）
    juce::Path pointer;
    const float thickness = juce::jmax (1.6f, bodyRadius * 0.16f);

    pointer.addRoundedRectangle (-thickness * 0.5f, -bodyRadius * 0.88f,
                                  thickness, bodyRadius * 0.55f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (enabled ? RaccoGuitarTheme::fieldInk() : RaccoGuitarTheme::fieldInkDim());
    g.fillPath (pointer);
}

//==============================================================================

RaccoFieldKnob::RaccoFieldKnob (const juce::String& name, juce::Colour arcColour)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, RaccoGuitarTheme::fieldInk());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setColour (juce::Slider::rotarySliderFillColourId, arcColour);
    slider.setColour (juce::Slider::textBoxTextColourId, RaccoGuitarTheme::fieldInk());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, arcColour.withAlpha (0.35f));

    // **読み取り専用の欄**にしてあります（打ち込みは右クリック。`ValueEntrySlider`）
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, valueHeight);
    addAndMakeVisible (slider);
}

void RaccoFieldKnob::resized()
{
    auto area = getLocalBounds();

    label.setBounds (area.removeFromTop (labelHeight));
    slider.setBounds (area);
}

//==============================================================================

RaccoArticulationChip::RaccoArticulationChip (const juce::String& noteNameToUse,
                                               const juce::String& articulationName,
                                               juce::Colour onColourToUse)
    : juce::Button (articulationName), noteName (noteNameToUse), onColour (onColourToUse)
{
    setButtonText (articulationName);
}

void RaccoArticulationChip::paintButton (juce::Graphics& g, bool isMouseOver, bool isDown)
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

    // 上＝キースイッチのノート（小さく）／下＝奏法の名前
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

void RaccoArticulationBadge::setState (GuitarArticulation articulation, float sustainSeconds)
{
    if (current == articulation && std::abs (sustain - sustainSeconds) < 0.01f)
        return;

    current = articulation;
    sustain = sustainSeconds;
    repaint();
}

void RaccoArticulationBadge::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    // 奏法によって実際に掛かるT60（`RaccoGuitarVoice::applyEffectiveTone()`と同じ考え）。
    // **ベロシティは1.0として描いています**——押す前に見るものなので
    float effective = sustain;

    switch (current)
    {
        case GuitarArticulation::palmMute: effective = juce::jmin (sustain, 0.5f); break;
        case GuitarArticulation::brushing: effective = 0.08f;                      break;
        default: break;
    }

    auto curveArea = bounds.reduced (7.0f, 4.0f).withTrimmedTop (13.0f);

    juce::Path curve;
    curve.startNewSubPath (curveArea.getX(), curveArea.getY());

    constexpr float windowSeconds = 10.0f;   // 横幅＝Sustainの最大値
    const int steps = juce::jmax (8, (int) curveArea.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps * windowSeconds;
        const float amplitude = std::pow (0.001f, t / juce::jmax (0.02f, effective));

        curve.lineTo (curveArea.getX() + curveArea.getWidth() * (float) i / (float) steps,
                       curveArea.getBottom() - curveArea.getHeight() * amplitude);
    }

    g.setColour (RaccoGuitarTheme::accent());
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    // 見出しと、いまの奏法
    auto top = bounds.reduced (7.0f, 3.0f).removeFromTop (12.0f);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText ("ARTIC", top, juce::Justification::centredLeft, false);

    g.setColour (RaccoGuitarTheme::highlight());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (RaccoGuitarEditor::articulationName (current), top,
                 juce::Justification::centredRight, false);
}

//==============================================================================

void RaccoGuitarKeyboard::setArticulation (GuitarArticulation articulation)
{
    if (current == articulation)
        return;

    current = articulation;
    repaint();
}

bool RaccoGuitarKeyboard::isOutOfRange (int midiNoteNumber) const
{
    return midiNoteNumber < RaccoGuitarProcessor::lowestNote
        && ! RaccoGuitarProcessor::isKeySwitchNote (midiNoteNumber);
}

void RaccoGuitarKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                          juce::Rectangle<float> area, bool isDown, bool isOver,
                                          juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver,
                                                 lineColour, textColour);

    if (RaccoGuitarProcessor::isKeySwitchNote (midiNoteNumber))
    {
        const bool selected =
            midiNoteNumber == RaccoGuitarProcessor::keySwitchNoteFor (current);

        g.setColour ((selected ? RaccoGuitarTheme::highlight() : RaccoGuitarTheme::accent())
                         .withAlpha (isDown ? 0.85f : 0.55f));
        g.fillRect (area);

        g.setColour (lineColour);
        g.drawRect (area);
        return;
    }

    if (isOutOfRange (midiNoteNumber))
    {
        // **押しても鳴らない音**。地に寄せて、鳴る音と見分けられるようにする
        g.setColour (MantaTheme::panelBackground().withAlpha (0.72f));
        g.fillRect (area);

        g.setColour (lineColour);
        g.drawRect (area);
    }
}

void RaccoGuitarKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                                          juce::Rectangle<float> area, bool isDown, bool isOver,
                                          juce::Colour noteFillColour)
{
    if (RaccoGuitarProcessor::isKeySwitchNote (midiNoteNumber))
    {
        const bool selected =
            midiNoteNumber == RaccoGuitarProcessor::keySwitchNoteFor (current);

        auto fill = selected ? RaccoGuitarTheme::highlight() : RaccoGuitarTheme::accent();

        if (isDown)      fill = fill.darker (0.3f);
        else if (isOver) fill = fill.brighter (0.15f);
        else             fill = fill.darker (0.12f);

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

juce::String RaccoGuitarEditor::articulationName (GuitarArticulation articulation)
{
    // **訳しません**（ほかの内蔵プラグインのつまみの名前と同じ扱い。用語は英語）
    switch (articulation)
    {
        case GuitarArticulation::normal:        return "NORMAL";
        case GuitarArticulation::palmMute:      return "PALM";
        case GuitarArticulation::harmonic:      return "HARM";
        case GuitarArticulation::slide:         return "SLIDE";
        case GuitarArticulation::pinchHarmonic: return "PINCH";
        case GuitarArticulation::brushing:      return "BRUSH";
    }

    return "NORMAL";
}

//==============================================================================

RaccoGuitarEditor::RaccoGuitarEditor (RaccoGuitarProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::guitarPresetFolder),
      keyboard (processorToUse.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (toolbar);

    // Undo・A/B・プリセットで値が入れ替わったとき。
    // **つまみは繋いであるので勝手に追いつきます**——描き直すだけ
    toolbar.onStateRestored = [this] { repaint(); };

    // 8.259：**出来合いの音**（Phase 267／本人の要望）。
    // 8.173と同じ理由——音源は「まず何か鳴らしてみたい」ものなので、
    // **保存済みが1つも無い状態で開くこと自体が壁**になります
    toolbar.setFactoryPresets (
        MantaFactoryPresets::makeToolbarPresets (processorToUse.getValueTreeState(),
                                                  RaccoGuitarPresets::all()));

    // 8.186：**名前を直に書かないこと**（`Branding.h`が唯一の出どころ）
    titleLabel.setText (Branding::guitarPluginName, juce::dontSendNotification);
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
    registerHint (badge, utf8 ("いまの奏法と、その減衰のかたち。奏法によってSustainに上限が掛かります"));

    //--------------------------------------------------------------------------
    // 出力
    auto& state = processor.getValueTreeState();

    outputSlider.setLookAndFeel (&fieldLookAndFeel.get());
    outputSlider.setColour (juce::Slider::trackColourId, RaccoGuitarTheme::accent());
    outputSlider.setColour (juce::Slider::thumbColourId, RaccoGuitarTheme::accent());
    outputSlider.setColour (juce::Slider::backgroundColourId, MantaTheme::graphBackground());
    addAndMakeVisible (outputSlider);
    styledSliders.push_back (&outputSlider);

    sliderAttachments.add (new SliderAttachment (state, RaccoGuitarParams::gain, outputSlider));

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
    // ピックアップ（**VINEの`LOOP KEY`にあたる場所**）
    const auto pickupNames = RaccoGuitarParams::getPickupNames();

    for (int i = 0; i < pickupNames.size(); ++i)
        pickupBox.addItem (pickupNames[i], i + 1);

    pickupBox.setColour (juce::ComboBox::backgroundColourId, MantaTheme::graphBackground());
    pickupBox.setColour (juce::ComboBox::textColourId, MantaTheme::text());
    pickupBox.setColour (juce::ComboBox::outlineColourId, MantaTheme::border());
    pickupBox.setColour (juce::ComboBox::arrowColourId, RaccoGuitarTheme::accent());
    addAndMakeVisible (pickupBox);

    comboAttachments.add (new ComboAttachment (state, RaccoGuitarParams::pickupSel, pickupBox));

    registerHint (pickupBox, utf8 ("ピックアップの位置。ブリッジ寄り（Rear）ほど硬い音になります"));

    //--------------------------------------------------------------------------
    // RESET（つまみを既定値へ。奏法もノーマルへ戻します）
    MantaPluginToolbar::styleButton (resetButton, "RESET");
    addAndMakeVisible (resetButton);

    resetButton.onClick = [this]
    {
        for (auto* parameter : processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (parameter))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

        processor.setArticulationFromUI (GuitarArticulation::normal);
    };

    registerHint (resetButton, utf8 ("つまみと奏法を、最初の状態へ戻します"));

    //--------------------------------------------------------------------------
    // 奏法のチップ（**キースイッチと同じことをします**）
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const auto articulation = chipOrder[i];

        auto chip = std::make_unique<RaccoArticulationChip> (
            // 8.121：**音名の数え方は`midiNoteName()`に1本化**（C4＝60。Phase 156）
            midiNoteName (RaccoGuitarProcessor::keySwitchNoteFor (articulation)),
            articulationName (articulation),
            RaccoGuitarTheme::highlight());

        chip->onClick = [this, articulation] { processor.setArticulationFromUI (articulation); };

        addAndMakeVisible (*chip);
        registerHint (*chip, utf8 ("鍵盤のキースイッチと同じ切り替えです（音は鳴りません）"));

        chips[i] = std::move (chip);
    }

    //--------------------------------------------------------------------------
    // 画像の上のつまみ（**水色＝弦そのもの／ピンク＝弾き方**。`RaccoGuitarTheme.h`）
    const auto blue = RaccoGuitarTheme::fieldAccent();
    const auto pink = RaccoGuitarTheme::fieldHighlight();

    addKnob (sustain,    "SUSTAIN",  RaccoGuitarParams::sustain,    blue,
              utf8 ("弦が鳴り続ける秒数。どの音程でも同じ秒数だけ鳴ります"));

    // **秒だけ単位を添えます**（ほかの4つは0〜1の割合で、単位がありません）
    sustain->slider.setTextValueSuffix (" s");
    addKnob (hardness,   "HARDNESS", RaccoGuitarParams::hardness,   pink,
              utf8 ("ピックの硬さ。0でフェルトや指、1で硬いピックの鋭い音"));
    addKnob (brightness, "BRIGHT",   RaccoGuitarParams::brightness, blue,
              utf8 ("音色の明るさ。下げると倍音が速く減って丸いトーンになります"));
    addKnob (pickPos,    "PICK POS", RaccoGuitarParams::pickPos,    blue,
              utf8 ("弾く位置。ブリッジ寄り（小さい値）で硬い音。ピッキングハーモニクスの次数もここで決まります"));
    addKnob (attack,     "ATTACK",   RaccoGuitarParams::attack,     pink,
              utf8 ("弾いた瞬間のピックノイズの量。ハンマリングやプリングには付きません"));

    // 8.291：**数値は0.0〜10.0**（Phase 284／本人の指定。`Orangutan Drums`と揃えました）。
    //
    // **`SUSTAIN`だけ秒のまま**です（本人の指定）——「9秒」は**そのものが読みたい値**で、
    // 0〜10にすると位置しか分からなくなります。
    // `PICK POS`のように0.05〜0.45という半端な範囲のものは、**むしろ位置のほうが読めます**。
    //
    // 繋いだ後に入れ直す必要はありません（`getTextFromValue()`の側なので。8.290）
    for (auto* knob : { hardness.get(), brightness.get(), pickPos.get(), attack.get() })
        knob->slider.setDisplayUnit (ValueEntrySlider::DisplayUnit::zeroToTen);

    //--------------------------------------------------------------------------
    // 鍵盤
    keyboard.setAvailableRange (RaccoGuitarProcessor::keyboardLowNote,
                                 RaccoGuitarProcessor::highestNote);
    keyboard.setScrollButtonsVisible (false);

    // **C4＝60の流儀**（本体のピアノロールと同じ。8.121）。
    // 既定のままだと、鍵盤のCの文字だけ1オクターブずれて見えます
    keyboard.setOctaveForMiddleC (4);

    // **白鍵の数でちょうど埋める**（余りが出ると右端に隙間が空きます）
    int whiteKeys = 0;

    for (int note = RaccoGuitarProcessor::keyboardLowNote;
         note <= RaccoGuitarProcessor::highestNote; ++note)
        if (! juce::MidiMessage::isMidiNoteBlack (note))
            ++whiteKeys;

    keyboard.setKeyWidth ((float) contentW / (float) juce::jmax (1, whiteKeys));

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, AppColours::pianoWhiteKey);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, AppColours::pianoBlackKey);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, MantaTheme::border());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, RaccoGuitarTheme::accent());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                         RaccoGuitarTheme::accent().withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    registerHint (keyboard, utf8 ("E2からD6までが鳴ります。左端の色つきの6鍵は奏法の切り替えです"));

    //--------------------------------------------------------------------------
    // **子の全部の出入りがここへ来ます**（つまみを足しても配線は増えません）
    addMouseListener (this, true);

    // 奏法（キースイッチで変わる）と声の数を見に行く。**12回／秒で足ります**
    startTimerHz (12);
    timerCallback();

    // 8.172：**大きさは固定**（ほかの内蔵プラグインと同じ方針）
    setSize (fixedWidth, fixedHeight);
}

RaccoGuitarEditor::~RaccoGuitarEditor()
{
    removeMouseListener (this);

    // **被せたLookAndFeelは、必ず外してから壊すこと**（1.5）
    for (auto* slider : styledSliders)
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void RaccoGuitarEditor::addKnob (std::unique_ptr<RaccoFieldKnob>& knob, const juce::String& name,
                                  const juce::String& parameterId, juce::Colour colour,
                                  const juce::String& hint)
{
    knob = std::make_unique<RaccoFieldKnob> (name, colour);

    // `ValueEntrySlider`が被せている`MixerLookAndFeel`を上書きする
    knob->slider.setLookAndFeel (&fieldLookAndFeel.get());
    knob->slider.setPopupDisplayEnabled (false, false, this);   // 数値は出しっぱなし

    addAndMakeVisible (*knob);
    styledSliders.push_back (&knob->slider);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  parameterId, knob->slider));

    // **桁をそろえる。** パラメータの刻みのままだと、Brightnessだけ`0.5500`と4桁で出て、
    // 5つ並べたときに数字の長さがばらつきます。
    //
    // **`setNumDecimalPlacesToDisplay()`では変わりません**——`SliderAttachment`が
    // `textFromValueFunction`を（パラメータの`getText()`に）差し替えており、
    // そちらが先に効くためです。**繋いだ後に、こちらを上書きすること**
    knob->slider.textFromValueFunction = [] (double value) { return juce::String (value, 2); };
    knob->slider.updateText();

    registerHint (*knob, hint);
}

void RaccoGuitarEditor::registerHint (juce::Component& component, const juce::String& text)
{
    hints[&component] = text;
}

void RaccoGuitarEditor::showHint (const juce::String& text)
{
    if (hintLabel.getText() == text)
        return;

    hintLabel.setText (text, juce::dontSendNotification);
}

void RaccoGuitarEditor::mouseEnter (const juce::MouseEvent& event)
{
    // **親をたどること**：つまみの中の数値欄やラベルからも来ます
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

void RaccoGuitarEditor::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    showHint (defaultHint);
}

//==============================================================================

void RaccoGuitarEditor::timerCallback()
{
    const auto articulation = processor.getArticulation();
    const int voices = processor.getActiveVoiceCount();

    if (articulation != lastArticulation)
    {
        lastArticulation = articulation;

        for (size_t i = 0; i < chips.size(); ++i)
            chips[i]->setToggleState (chipOrder[i] == articulation, juce::dontSendNotification);

        keyboard.setArticulation (articulation);
    }

    badge.setState (articulation,
                     processor.getValueTreeState()
                              .getRawParameterValue (RaccoGuitarParams::sustain)->load());

    if (voices != lastVoiceCount)
    {
        lastVoiceCount = voices;

        // 声の数だけを描き直す（帯ぜんぶを描き直さない）
        repaint (voicesX, headerY, contentX + contentW - voicesX, headerH);
    }
}

//==============================================================================

void RaccoGuitarEditor::drawTracked (juce::Graphics& g, const juce::String& text,
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

void RaccoGuitarEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void RaccoGuitarEditor::drawBox (juce::Graphics& g, juce::Rectangle<int> area,
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

void RaccoGuitarEditor::drawField (juce::Graphics& g) const
{
    const juce::Rectangle<int> area (contentX, fieldY, contentW, fieldH);

    juce::Path rounded;
    rounded.addRoundedRectangle (area.toFloat(), 10.0f);

    {
        // **`ScopedSaveState`が要ります**——`reduceClipRegion()`は戻らないので、
        // これが無いとこの下の縁まで切り取られます（8.151と同じ話）
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rounded);

        if (fieldImage.isValid())
        {
            // **切り取らずに伸ばします**（`stretchToFit`）。絵は16:9、置き場所は約3:1なので、
            // `fillDestination`だと**真ん中の帯しか出ません**——いちばん色の出ている
            // 左右が落ちてしまいます。滲んだ絵なので、伸びても形が崩れて見えません
            g.drawImage (fieldImage, area.toFloat(), juce::RectanglePlacement::stretchToFit);

            // **薄い白を重ねる**：絵は場所によって明るさが違うので、
            // そのままだと濃いところで文字が読めません
            g.setColour (RaccoGuitarTheme::fieldVeil());
            g.fillRect (area);
        }
        else
        {
            // 絵が入っていないビルドでも、**面としては成立させる**
            g.setColour (RaccoGuitarTheme::fieldKnobBody());
            g.fillRect (area);
        }

        // **6本の弦を描いていたのをやめました**（本人の指定）。
        // 絵の上に横線を引くと、**絵のほうが背景に落ちて**、
        // もらった1枚がただの下地に見えます。空いているように見えても、
        // ここは**絵を見せる場所**です
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);

    g.setColour (RaccoGuitarTheme::fieldInkDim());
    drawTracked (g, "RACCO / STRING FIELD",
                  { (float) area.getX() + 24.0f, (float) area.getY() + 30.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.6f);
}

void RaccoGuitarEditor::drawWordmark (juce::Graphics& g) const
{
    g.setColour (MantaTheme::text());
    drawTracked (g, "RACCO", { (float) wordmarkX, (float) headerY + 34.0f },
                  juce::Font (juce::FontOptions (21.0f, juce::Font::bold)), 2.0f);

    g.setColour (RaccoGuitarTheme::accent());
    drawTracked (g, "GUITAR", { (float) wordmarkX + 2.0f, (float) headerY + 52.0f },
                  juce::Font (juce::FontOptions (10.0f, juce::Font::bold)), 4.2f);
}

void RaccoGuitarEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    //--------------------------------------------------------------------------
    // 上の帯
    drawPanel (g, { contentX, headerY, contentW, headerH });
    drawWordmark (g);

    // 案内の帯（中身は`hintLabel`）
    const juce::Rectangle<int> hintArea (hintX, headerY + 14, hintW, headerH - 28);

    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (hintArea.toFloat(), 6.0f);

    g.setColour (MantaTheme::border().withAlpha (0.6f));
    g.drawRoundedRectangle (hintArea.toFloat().reduced (0.5f), 6.0f, 1.0f);

    g.setColour (RaccoGuitarTheme::highlight());
    g.fillEllipse ((float) hintArea.getX() + 11.0f, (float) hintArea.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "OUTPUT", { (float) outputLabelX, (float) headerY + 41.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);

    // 鳴っている声の数
    {
        const juce::Rectangle<int> area (voicesX, headerY, contentX + contentW - voicesX - 20, headerH);

        g.setColour (RaccoGuitarTheme::accent());
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
    drawTracked (g, "A PHYSICAL MODELING GUITAR",
                  { (float) leftBoxX, (float) rowY + 19.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.4f);

    drawBox (g, { leftBoxX, boxY, leftBoxW, boxH }, "STRING MODEL");
    drawBox (g, { rightBoxX, boxY, rightBoxW, boxH }, "ARTICULATION");

    g.setColour (MantaTheme::text());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText ("648 mm SCALE / 6 STRINGS / KARPLUS-STRONG",
                 leftBoxX + 10, boxY + 24, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("E2 - D6 PLAYABLE / A#1 - D#2 KEY SWITCHES / CC1 VIBRATO",
                 leftBoxX + 10, boxY + 40, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    // ピックアップの見出し（選ぶところは`pickupBox`）
    g.setColour (MantaTheme::textDim());
    drawTracked (g, "PICKUP", { (float) pickupX + 10.0f, (float) rowY + 19.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);

    //--------------------------------------------------------------------------
    drawField (g);
}

//==============================================================================

void RaccoGuitarEditor::resized()
{
    toolbar.setBounds (contentX, toolbarY, contentW, MantaPluginToolbar::preferredHeight);
    titleLabel.setBounds (toolbar.getTrailingArea().withTrimmedRight (6));

    hintLabel.setBounds (hintX + 24, headerY + 14, hintW - 34, headerH - 28);
    badge.setBounds (badgeX, headerY + 10, badgeW, headerH - 20);

    outputSlider.setBounds (outputSliderX, headerY + 24, outputSliderW, 20);
    outputValue.setBounds (outputValueX, headerY + 26, 44, 16);

    resetButton.setBounds (resetX, rowY, resetW, rowH);
    pickupBox.setBounds (pickupX + 58, rowY, pickupW - 58, rowH);

    //--------------------------------------------------------------------------
    // 奏法のチップ。**余りを最後の1つに寄せない**ように、割り算は浮動小数で持つ
    const float chipsX = (float) (rightBoxX + chipInset);
    const float chipsW = (float) (rightBoxW - chipInset * 2);
    const float chipW = (chipsW - (float) chipGap * (float) (numGuitarArticulations - 1))
                         / (float) numGuitarArticulations;

    for (size_t i = 0; i < chips.size(); ++i)
    {
        const float x = chipsX + (chipW + (float) chipGap) * (float) i;

        chips[i]->setBounds (juce::Rectangle<float> (x, (float) (boxY + 26), chipW,
                                                      (float) chipHeight).toNearestInt());
    }

    //--------------------------------------------------------------------------
    // 画像の上のつまみ（**割合で置く**。`knobPlacement[]`）
    RaccoFieldKnob* const fieldKnobs[5]
    {
        sustain.get(), hardness.get(), brightness.get(), pickPos.get(), attack.get()
    };

    for (int i = 0; i < 5; ++i)
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
