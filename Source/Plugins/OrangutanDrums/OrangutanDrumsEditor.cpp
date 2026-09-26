#include "OrangutanDrumsEditor.h"

#include "OrangutanDrumsPresets.h"   // 8.288：工場キット
#include "../../AppIcon.h"           // 8.229：埋め込みの絵を名前で引く
#include "../../Branding.h"
#include "../../ChordModel.h"        // 8.121：音名の数え方（`midiNoteName()`）
#include "../../SegmentDisplay.h"    // 8.177：Emberの7セグ
#include "../../Utf8.h"

namespace
{
    /** 画像の名前。**CMakeが`Resources/Plugins/orangutan_drums_field.jpg`を埋め込みます**。 */
    const char* const fieldImageName = "orangutan_drums_field_jpg";

    /** `<UI>`に入れるもの（`OrangutanDrumsEditor`の説明）。 */
    const juce::Identifier selectedPadProperty { "selectedPad" };
    const juce::Identifier pageProperty { "page" };
}

//==============================================================================

void DrumsFieldLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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

    // ① 影。**銅の上でも、円が置かれていることを見せる**
    g.setColour (juce::Colours::black.withAlpha (0.14f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f + 3.0f, bodyRadius * 2.0f + 3.0f)
                       .withCentre (centre.translated (0.0f, 1.5f)));

    // ② 弧の地（端から端まで）
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (OrangutanDrumsTheme::fieldKnobTrack());
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

    g.setColour (OrangutanDrumsTheme::fieldKnobBody());
    g.fillEllipse (body);

    g.setColour (accent.withAlpha (enabled ? 0.6f : 0.25f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // ⑤ 指針
    juce::Path pointer;
    const float thickness = juce::jmax (1.6f, bodyRadius * 0.16f);

    pointer.addRoundedRectangle (-thickness * 0.5f, -bodyRadius * 0.88f,
                                  thickness, bodyRadius * 0.55f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (enabled ? OrangutanDrumsTheme::fieldInk() : OrangutanDrumsTheme::fieldInkDim());
    g.fillPath (pointer);
}

void DrumsFieldLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // **編集中とEmberの7セグは基底に任せる**（8.274と同じ）
    if (label.isBeingEdited() || SevenSegment::canDraw (label.getText()))
    {
        MantaKnobLookAndFeel::drawLabel (g, label);
        return;
    }

    const float alpha = label.isEnabled() ? 1.0f : 0.5f;

    g.setFont (juce::Font (juce::FontOptions (label.getFont().getHeight(), juce::Font::bold)));
    g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
    g.drawFittedText (label.getText(), label.getLocalBounds(), label.getJustificationType(), 1, 1.0f);
}

//==============================================================================

DrumsFieldKnob::DrumsFieldKnob (const juce::String& name, juce::Colour arcColour,
                                 int labelHeightToUse, int valueHeightToUse,
                                 juce::Colour inkColour)
    : labelHeight (labelHeightToUse), valueHeight (valueHeightToUse)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions ((float) labelHeight - 4.5f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, inkColour);
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setColour (juce::Slider::rotarySliderFillColourId, arcColour);
    slider.setColour (juce::Slider::textBoxTextColourId, inkColour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, arcColour.withAlpha (0.35f));

    // **読み取り専用の欄**（打ち込みは右クリック。`ValueEntrySlider`）
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 78, valueHeight);
    addAndMakeVisible (slider);
}

void DrumsFieldKnob::resized()
{
    auto area = getLocalBounds();

    label.setBounds (area.removeFromTop (labelHeight));
    slider.setBounds (area);
}

void DrumsFieldKnob::setLabelText (const juce::String& text)
{
    if (label.getText() == text)
        return;

    label.setText (text, juce::dontSendNotification);
}

//==============================================================================

DrumPadButton::DrumPadButton (int padIndex)
    : index (padIndex),
      noteName (midiNoteName (OrangutanDrumsProcessor::padBaseNote + padIndex))
{
    setWantsKeyboardFocus (false);
}

void DrumPadButton::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void DrumPadButton::setEngineName (const juce::String& name)
{
    if (engineName == name)
        return;

    engineName = name;
    repaint();
}

void DrumPadButton::setDirectOut (bool isDirect)
{
    if (directOut == isDirect)
        return;

    directOut = isDirect;
    repaint();
}

void DrumPadButton::setGlow (float newGlow)
{
    // **変わったときだけ**（20Hzで16枚を描き直すと、閉じている窓でも回り続けます）
    if (std::abs (glow - newGlow) < 0.01f)
        return;

    glow = newGlow;
    repaint();
}

void DrumPadButton::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const float corner = 6.0f;

    // 地。**塗ること**——透けると文字が読めません（`fieldKnobBody`と同じ理由）
    auto fill = OrangutanDrumsTheme::fieldKnobBody().withAlpha (0.92f);

    if (glow > 0.01f)
        fill = fill.interpolatedWith (OrangutanDrumsTheme::fieldAccent(),
                                       juce::jlimit (0.0f, 0.8f, glow * 0.8f));
    else if (highlighted)
        fill = fill.brighter (0.15f);

    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.5f), corner);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (selected ? OrangutanDrumsTheme::fieldHighlight()
                          : OrangutanDrumsTheme::fieldInk().withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, selected ? 2.0f : 1.0f);

    // 明るく光っているときは、文字を白へ寄せます（読めなくならないように）
    const auto ink = glow > 0.45f ? juce::Colours::white : OrangutanDrumsTheme::fieldInk();

    auto top = bounds.reduced (6.0f, 4.0f).removeFromTop (11.0f);

    g.setColour (ink.withAlpha (0.7f));
    g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), top,
                 juce::Justification::centredLeft, false);

    // 8.289：**DIRECTの目印**（Phase 282）。パッドの上では1文字しか置けません
    if (directOut)
    {
        g.setColour (glow > 0.45f ? juce::Colours::white
                                  : OrangutanDrumsTheme::fieldHighlight());
        g.setFont (juce::Font (juce::FontOptions (8.5f, juce::Font::bold)));
        g.drawText ("D", top, juce::Justification::centredRight, false);
    }

    g.setColour (ink);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawFittedText (engineName, bounds.reduced (4.0f, 16.0f).toNearestInt(),
                       juce::Justification::centred, 1, 0.8f);

    g.setColour (ink.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText (noteName, bounds.reduced (6.0f, 4.0f).removeFromBottom (11.0f),
                 juce::Justification::centred, false);
}

void DrumPadButton::mouseDown (const juce::MouseEvent& event)
{
    if (onSelect != nullptr)
        onSelect (index);

    // **上ほど強く**（`MAGAZINE`と同じ式。クラスの説明）
    const float height = juce::jmax (1.0f, (float) getHeight());
    const float position = juce::jlimit (0.0f, 1.0f, event.position.y / height);
    const float velocity = juce::jlimit (0.30f, 1.0f, 1.0f - position * 0.62f);

    if (onHit != nullptr)
        onHit (index, velocity);
}

void DrumPadButton::mouseEnter (const juce::MouseEvent&)
{
    highlighted = true;
    repaint();
}

void DrumPadButton::mouseExit (const juce::MouseEvent&)
{
    highlighted = false;
    repaint();
}

//==============================================================================

void DrumEngineBadge::setState (int padIndex, int engine, float decayNormalised)
{
    if (pad == padIndex && currentEngine == engine && std::abs (decay - decayNormalised) < 0.002f)
        return;

    pad = padIndex;
    currentEngine = engine;
    decay = decayNormalised;
    repaint();
}

void DrumEngineBadge::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    // 8.290：**絵の上の色で**（Phase 283）。テーマの地（`graphBackground()`）だと、
    // ダークで**明るい絵の上に黒い箱**が浮きます
    g.setColour (OrangutanDrumsTheme::fieldKnobBody().withAlpha (0.88f));
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (OrangutanDrumsTheme::fieldInk().withAlpha (0.35f));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    // **DSPの表をそのまま引くこと**（数字を2箇所に書かない。1.27）
    const auto range = orangutan::engineDecayRange (currentEngine);
    const float seconds = orangutan::expMap (decay, range.lo, range.hi);

    auto curveArea = bounds.reduced (8.0f, 5.0f).withTrimmedLeft (96.0f);

    juce::Path curve;
    curve.startNewSubPath (curveArea.getX(), curveArea.getY());

    // 8.289：**横は、そのエンジンでいちばん長いところまで**（Phase 282）。
    //
    // Phase 281は5秒で固定でした（どのエンジンでも同じ物差し）。**短いエンジンでは
    // 何も見えません**——`STICK`の上限は0.18秒なので、5秒の箱では
    // **左端で落ちる縦線**にしかなりませんでした（実際にそう写りました）。
    //
    // つまみを回したときに**曲線が伸び縮みして見える**ほうが、ここでは役に立ちます。
    const float windowSeconds = juce::jmax (0.05f, range.hi);
    const int steps = juce::jmax (8, (int) curveArea.getWidth());

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps * windowSeconds;
        const float amplitude = std::pow (0.001f, t / juce::jmax (0.006f, seconds));

        curve.lineTo (curveArea.getX() + curveArea.getWidth() * (float) i / (float) steps,
                       curveArea.getBottom() - curveArea.getHeight() * amplitude);
    }

    g.setColour (OrangutanDrumsTheme::fieldAccent());
    g.strokePath (curve, juce::PathStrokeType (1.6f));

    auto left = bounds.reduced (8.0f, 4.0f).removeFromLeft (90.0f);

    g.setColour (OrangutanDrumsTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText ("DECAY", left.removeFromTop (11.0f), juce::Justification::centredLeft, false);

    g.setColour (OrangutanDrumsTheme::fieldInk());
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (juce::String (seconds, 2) + " s", left, juce::Justification::centredLeft, false);
}

//==============================================================================

void OrangutanDrumsKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                             juce::Rectangle<float> area, bool isDown, bool isOver,
                                             juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver,
                                                 lineColour, textColour);

    // 8.290：**鳴らない鍵は灰色**（Phase 283／本人の指定。クラスの説明）
    if (midiNoteNumber <= OrangutanDrumsProcessor::padHighNote)
        return;

    g.setColour (MantaTheme::panelBackground().withAlpha (0.78f));
    g.fillRect (area);

    g.setColour (lineColour);
    g.drawRect (area);
}

//==============================================================================

juce::String OrangutanDrumsEditor::snapLabelFor (int engine)
{
    // **`SNAP`はエンジンごとに意味が変わります**（`OrangutanDrumsDSP.h`）。
    // つまみの名前をそのまま出すより、**いま何を回しているか**を出すほうが早い
    switch (engine)
    {
        case orangutan::ENG_KICK:
        case orangutan::ENG_SUB:
        case orangutan::ENG_TOM:
        case orangutan::ENG_CONGA:   return "CLICK";
        case orangutan::ENG_SNARE:   return "SNAPPY";
        case orangutan::ENG_CLAP:    return "SPREAD";
        case orangutan::ENG_SHAKER:  return "ATTACK";
        case orangutan::ENG_ZAP:     return "SWEEP";
        case orangutan::ENG_NOISE:   return "UP/DOWN";
        default:                     return "SNAP";
    }
}

juce::String OrangutanDrumsEditor::toneLabelFor (int engine)
{
    // KICK と SUB の TONE は**倍音（サチュレーション）の量**で、明るさではありません。
    //
    // **`DRIVE`と書かないこと。** マスターにも`DRIVE`があり、同じ画面に2つ出ます
    // （1度そう書いて、絵を撮って気づきました）
    if (engine == orangutan::ENG_KICK || engine == orangutan::ENG_SUB)
        return "HARMONICS";

    return "TONE";
}

//==============================================================================

OrangutanDrumsEditor::OrangutanDrumsEditor (OrangutanDrumsProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::drumsPresetFolder),
      keyboard (processorToUse.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    lastEngines.fill (-1);
    lastDirectOuts.fill (false);

    addAndMakeVisible (toolbar);

    // **プリセットを当てたら、繋ぎ直して描き直すこと**——パッドのつまみは
    // 選んでいるパッドへ繋がっているので、値が外から入れ替わると数値だけ古くなります
    toolbar.onStateRestored = [this]
    {
        rebuildPadAttachments();
        updatePadLabels();
        updateOutButton();
        repaint();
    };

    toolbar.setFactoryPresets (
        MantaFactoryPresets::makeToolbarPresets (processorToUse.getValueTreeState(),
                                                  OrangutanDrumsPresets::all()));

    // 8.318：**いまのキットの名前を出す**（Phase 311／`MantaPluginToolbar.h`）。
    // **この帯には38pxしか空きがありません**（ヘッダーの「ツールバーに題名が入りません」）。
    // 上限を広く渡しても、帯の残りいっぱい（約110px）で止まります——
    // キットの名前（`FUTURE BASS`など）は収まり、長ければ「…」で詰めます
    toolbar.setShowsCurrentPreset (true);

    fieldImage = AppIcon::loadEmbedded (fieldImageName);

    //--------------------------------------------------------------------------
    // 案内の帯
    defaultHint = utf8 ("カーソルを合わせると、ここに説明が出ます");

    hintLabel.setText (defaultHint, juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, MantaTheme::textDim());
    hintLabel.setFont (juce::Font (juce::FontOptions (10.5f)));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    hintLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintLabel);

    auto& state = processor.getValueTreeState();

    //--------------------------------------------------------------------------
    // 出力
    outputSlider.setLookAndFeel (&fieldLookAndFeel.get());
    outputSlider.setColour (juce::Slider::trackColourId, OrangutanDrumsTheme::accent());
    outputSlider.setColour (juce::Slider::thumbColourId, OrangutanDrumsTheme::accent());
    outputSlider.setColour (juce::Slider::backgroundColourId, MantaTheme::graphBackground());
    addAndMakeVisible (outputSlider);
    styledSliders.push_back (&outputSlider);

    sliderAttachments.add (new SliderAttachment (state, OrangutanDrumsParams::volume, outputSlider));

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

    registerHint (outputSlider, utf8 ("このプラグインから出る音量。出口にソフトリミッタがあるので、上げ切っても0dBFSは越えません"));

    //--------------------------------------------------------------------------
    // エンジンの選択（**選んでいるパッドのもの**）
    engineBox.addItemList (OrangutanDrumsParams::getEngineNames(), 1);
    engineBox.setColour (juce::ComboBox::backgroundColourId, MantaTheme::graphBackground());
    engineBox.setColour (juce::ComboBox::textColourId, MantaTheme::text());
    engineBox.setColour (juce::ComboBox::outlineColourId, MantaTheme::border());
    engineBox.setColour (juce::ComboBox::arrowColourId, OrangutanDrumsTheme::accent());
    addAndMakeVisible (engineBox);

    registerHint (engineBox, utf8 ("このパッドの音源。16種類あり、どのパッドにも自由に割り当てられます"));

    //--------------------------------------------------------------------------
    // 8.289：**このパッドの出口**（Phase 282）
    MantaPluginToolbar::styleButton (outButton, "OUT  MAIN");
    outButton.setColour (juce::TextButton::buttonOnColourId, OrangutanDrumsTheme::highlight());
    outButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (outButton);

    outButton.onClick = [this]
    {
        if (auto* parameter = processor.getValueTreeState().getParameter (
                                  OrangutanDrumsParams::padId (selectedPad,
                                                                OrangutanDrumsParams::padOut)))
        {
            const float wanted = processor.isPadDirectOut (selectedPad) ? 0.0f : 1.0f;

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (wanted));
            parameter->endChangeGesture();
        }
    };

    registerHint (outButton, utf8 ("このパッドの出口。DIRECTにすると、マスターを通らずにパラアウトへ出ます（本体でドラムアウトトラックを作って受けてください）"));

    //--------------------------------------------------------------------------
    // 頁のタブ
    MantaPluginToolbar::styleButton (padsTabButton, "PADS");
    MantaPluginToolbar::styleButton (knobsTabButton, "PAD 01");

    // 8.290：**タブも絵の上に載りました**（Phase 283）。テーマの地のままだと、
    // ダークで**明るい絵の上に黒いボタン**が並びます
    for (auto* button : { &padsTabButton, &knobsTabButton })
    {
        button->setColour (juce::TextButton::buttonColourId,
                            OrangutanDrumsTheme::fieldKnobBody().withAlpha (0.88f));
        button->setColour (juce::TextButton::textColourOffId, OrangutanDrumsTheme::fieldInk());
        button->setColour (juce::TextButton::buttonOnColourId, OrangutanDrumsTheme::fieldAccent());
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        addAndMakeVisible (*button);
    }

    padsTabButton.onClick = [this] { setPage (0); };
    knobsTabButton.onClick = [this] { setPage (1); };

    registerHint (padsTabButton, utf8 ("16個のパッド。叩いて選び、そのまま鳴らせます"));
    registerHint (knobsTabButton, utf8 ("選んでいるパッドのつまみ7つ"));

    //--------------------------------------------------------------------------
    // パッド16個
    for (size_t i = 0; i < pads.size(); ++i)
    {
        auto pad = std::make_unique<DrumPadButton> ((int) i);

        pad->onSelect = [this] (int index) { setSelectedPad (index); };
        pad->onHit = [this] (int index, float velocity)
        {
            processor.triggerPadFromUI (index, velocity);
        };

        addAndMakeVisible (*pad);
        registerHint (*pad, utf8 ("叩くと選んで鳴ります。上のほうを押すほど強く鳴ります"));

        pads[i] = std::move (pad);
    }

    //--------------------------------------------------------------------------
    // 絵の上のつまみ（**インディゴ＝音そのもの／プラム＝出口**）
    const auto indigo = OrangutanDrumsTheme::fieldAccent();
    const auto plum   = OrangutanDrumsTheme::fieldHighlight();

    addPadKnob (tune,  "TUNE",  indigo,
                 utf8 ("このパッドの音程。ノイズ系のエンジンでは、フィルタの位置が動きます（±24半音）"));
    addPadKnob (decay, "DECAY", indigo,
                 utf8 ("音が消えるまでの長さ。範囲はエンジンごとに違います（下の箱に形が出ます）"));
    addPadKnob (tone,  "TONE",  indigo,
                 utf8 ("明るさ。KICKとSUB 808だけは意味が変わり、倍音（サチュレーション）の量になります"));
    addPadKnob (snap,  "SNAP",  indigo,
                 utf8 ("アタックの成分。エンジンで意味が変わるので、見出しのほうが変わります"));
    addPadKnob (level, "LEVEL", plum,
                 utf8 ("このパッドの音量"));
    addPadKnob (pan,   "PAN",   plum,
                 utf8 ("左右の位置（等パワー）"));
    addPadKnob (send,  "SEND",  plum,
                 utf8 ("マスターのリバーブへ送る量。DIRECTのパッドは送りません"));

    // **真ん中が0のつまみ**は、弧を12時から塗ります（`MantaKnobLookAndFeel`）
    tune->slider.getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);
    pan->slider.getProperties().set (MantaKnobLookAndFeel::bipolarProperty(), true);

    // 8.291：**この2つだけ元の単位へ戻します**（Phase 284／本人の指定）。
    //
    // ほかのつまみは0.0〜10.0ですが（8.290）、**半音と左右は「真ん中が0」に意味があります**
    // ——0〜10にすると真ん中が5.0になり、**+2半音なのか-2半音なのか**が読めません。
    //
    // > **位置が知りたいつまみと、値が知りたいつまみがある。**
    // > 目盛りを揃えるのは前者だけにすること。
    for (auto* knob : { tune.get(), pan.get() })
    {
        knob->slider.setDisplayUnit (ValueEntrySlider::DisplayUnit::plain);
        knob->slider.setDisplayDecimals (2);
    }

    addAndMakeVisible (badge);
    registerHint (badge, utf8 ("選んでいるパッドの減衰のかたち。エンジンごとに時間の範囲が違います"));

    //--------------------------------------------------------------------------
    // マスター（**頁で隠れません**）
    addMasterKnob (drive,  "DRIVE",  OrangutanDrumsParams::drive,  plum,
                    utf8 ("出口のサチュレーション。上げるほど太く、潰れます"));
    addMasterKnob (glue,   "GLUE",   OrangutanDrumsParams::glue,   plum,
                    utf8 ("全体をまとめるコンプ。ドラム全体が1つの塊に聞こえるようになります"));
    addMasterKnob (reverb, "REVERB", OrangutanDrumsParams::reverb, plum,
                    utf8 ("リバーブの量。送るのはパッドごとのSENDで、ここはその返りの大きさです"));
    addMasterKnob (size,   "SIZE",   OrangutanDrumsParams::size,   plum,
                    utf8 ("リバーブの広さ"));
    addMasterKnob (damp,   "DAMP",   OrangutanDrumsParams::damp,   plum,
                    utf8 ("リバーブの高い側の減り方。上げるほど暗い響きになります"));

    //--------------------------------------------------------------------------
    // 鍵盤。8.289：**パッドの音域だけになり、色も塗りません**（Phase 282／本人の指定）。
    //
    // 8.290：**E3（52）まで出します**（Phase 283／本人の指定）。51は黒鍵なので、
    // そこで止めると**右端が白鍵の途中で切れて**見えます。52は灰色に塗って、
    // 「ここから先は鳴らない」と分かる形に（`OrangutanDrumsKeyboard`）
    keyboard.setAvailableRange (OrangutanDrumsProcessor::padBaseNote,
                                 OrangutanDrumsKeyboard::highestShownNote);
    keyboard.setScrollButtonsVisible (false);

    // **C4＝60の流儀**（本体のピアノロールと同じ。8.121）
    keyboard.setOctaveForMiddleC (4);

    int whiteKeys = 0;

    for (int note = OrangutanDrumsProcessor::padBaseNote;
         note <= OrangutanDrumsKeyboard::highestShownNote; ++note)
        if (! juce::MidiMessage::isMidiNoteBlack (note))
            ++whiteKeys;

    keyboard.setKeyWidth ((float) contentW / (float) juce::jmax (1, whiteKeys));

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, AppColours::pianoWhiteKey);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, AppColours::pianoBlackKey);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, MantaTheme::border());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId,
                         OrangutanDrumsTheme::accent());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                         OrangutanDrumsTheme::accent().withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    registerHint (keyboard, utf8 ("左の16鍵がパッド1〜16（MIDI 36〜51）です。右端のE3は鳴りません"));

    //--------------------------------------------------------------------------
    // 開いたときは、前に選んでいたパッドと頁へ戻します（`<UI>`）
    setSelectedPad ((int) processor.getUiState().getProperty (selectedPadProperty, 0));
    setPage ((int) processor.getUiState().getProperty (pageProperty, 0));

    addMouseListener (this, true);

    startTimerHz (20);
    timerCallback();

    // 8.172：**大きさは固定**
    setSize (fixedWidth, fixedHeight);
}

OrangutanDrumsEditor::~OrangutanDrumsEditor()
{
    removeMouseListener (this);

    // **繋ぎを先に外すこと**（つまみより後ろで壊れると、壊れたものを触ります）
    padAttachments.clear();
    engineAttachment.reset();

    for (auto* slider : styledSliders)
        slider->setLookAndFeel (nullptr);

    for (auto* component : styledComponents)
        component->setLookAndFeel (nullptr);
}

//==============================================================================

void OrangutanDrumsEditor::addPadKnob (std::unique_ptr<DrumsFieldKnob>& knob,
                                        const juce::String& name, juce::Colour colour,
                                        const juce::String& hint)
{
    knob = std::make_unique<DrumsFieldKnob> (name, colour);

    knob->setLookAndFeel (&fieldLookAndFeel.get());
    styledComponents.push_back (knob.get());

    knob->slider.setLookAndFeel (&fieldLookAndFeel.get());
    knob->slider.setPopupDisplayEnabled (false, false, this);

    addAndMakeVisible (*knob);
    styledSliders.push_back (&knob->slider);

    // 8.290：**数値は0.0〜10.0**（Phase 283／本人の要望。`ValueEntrySlider`）
    knob->slider.setDisplayUnit (ValueEntrySlider::DisplayUnit::zeroToTen);

    // **ここでは繋ぎません**（選んでいるパッドへ`rebuildPadAttachments()`が繋ぎます）
    registerHint (*knob, hint);
}

void OrangutanDrumsEditor::addMasterKnob (std::unique_ptr<DrumsFieldKnob>& knob,
                                           const juce::String& name,
                                           const juce::String& parameterId, juce::Colour colour,
                                           const juce::String& hint)
{
    // **小さい枠に入るので、見出しと数値を詰めます**（`DrumsFieldKnob`の説明）。
    // 8.290：**文字は`fieldInk()`へ戻しました**——絵がここまで広がったためです
    knob = std::make_unique<DrumsFieldKnob> (name, colour, 13, 15);

    knob->setLookAndFeel (&fieldLookAndFeel.get());
    styledComponents.push_back (knob.get());

    knob->slider.setLookAndFeel (&fieldLookAndFeel.get());
    knob->slider.setPopupDisplayEnabled (false, false, this);

    addAndMakeVisible (*knob);
    styledSliders.push_back (&knob->slider);

    // 8.290：**数値は0.0〜10.0**（Phase 283）。**繋ぐ前でも後でも構いません**
    // ——`getTextFromValue()`の側なので、`SliderAttachment`に差し替えられません
    knob->slider.setDisplayUnit (ValueEntrySlider::DisplayUnit::zeroToTen);

    sliderAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  parameterId, knob->slider));

    registerHint (*knob, hint);
}

void OrangutanDrumsEditor::rebuildPadAttachments()
{
    using namespace OrangutanDrumsParams;

    // **先に壊すこと**（クラスの説明。残したまま作ると2つ繋がります）
    padAttachments.clear();
    engineAttachment.reset();

    auto& state = processor.getValueTreeState();

    struct Entry { DrumsFieldKnob* knob; const char* suffix; };

    const Entry entries[]
    {
        { tune.get(),  padTune },
        { decay.get(), padDecay },
        { tone.get(),  padTone },
        { snap.get(),  padSnap },
        { level.get(), padLevel },
        { pan.get(),   padPan },
        { send.get(),  padSend },
    };

    for (const auto& entry : entries)
    {
        if (entry.knob == nullptr)
            continue;

        padAttachments.add (new SliderAttachment (state, padId (selectedPad, entry.suffix),
                                                   entry.knob->slider));

        // 8.290：**桁を入れ直す必要はもうありません**（Phase 283）。
        // `DisplayUnit::zeroToTen`は`getTextFromValue()`の側なので、
        // `SliderAttachment`が`textFromValueFunction`を差し替えても効いたままです
    }

    engineAttachment = std::make_unique<ComboBoxAttachment> (
        state, padId (selectedPad, padEngine), engineBox);
}

void OrangutanDrumsEditor::updatePadLabels()
{
    const int engine = processor.getPadEngine (selectedPad);

    if (snap != nullptr) snap->setLabelText (snapLabelFor (engine));
    if (tone != nullptr) tone->setLabelText (toneLabelFor (engine));

    badge.setState (selectedPad, engine,
                     processor.getValueTreeState()
                                .getRawParameterValue (OrangutanDrumsParams::padId (
                                    selectedPad, OrangutanDrumsParams::padDecay))->load());

    knobsTabButton.setButtonText ("PAD " + juce::String (selectedPad + 1).paddedLeft ('0', 2));
}

void OrangutanDrumsEditor::updateOutButton()
{
    const bool direct = processor.isPadDirectOut (selectedPad);

    outButton.setToggleState (direct, juce::dontSendNotification);
    outButton.setButtonText (direct ? "OUT  DIRECT" : "OUT  MAIN");
}

void OrangutanDrumsEditor::setSelectedPad (int pad)
{
    selectedPad = juce::jlimit (0, OrangutanDrumsProcessor::numPads - 1, pad);

    // **プロジェクトに残ります**（クラスの説明）
    processor.getUiState().setProperty (selectedPadProperty, selectedPad, nullptr);

    for (size_t i = 0; i < pads.size(); ++i)
        if (pads[i] != nullptr)
            pads[i]->setSelected ((int) i == selectedPad);

    rebuildPadAttachments();
    updatePadLabels();
    updateOutButton();

    repaint (contentX, controlY, contentW, controlH);
}

void OrangutanDrumsEditor::setPage (int page)
{
    currentPage = juce::jlimit (0, 1, page);

    processor.getUiState().setProperty (pageProperty, currentPage, nullptr);

    padsTabButton.setToggleState (currentPage == 0, juce::dontSendNotification);
    knobsTabButton.setToggleState (currentPage == 1, juce::dontSendNotification);

    for (auto& pad : pads)
        if (pad != nullptr)
            pad->setVisible (currentPage == 0);

    DrumsFieldKnob* const padKnobs[7]
    {
        tune.get(), decay.get(), tone.get(), snap.get(), level.get(), pan.get(), send.get()
    };

    for (auto* knob : padKnobs)
        if (knob != nullptr)
            knob->setVisible (currentPage == 1);

    badge.setVisible (currentPage == 1);
}

void OrangutanDrumsEditor::registerHint (juce::Component& component, const juce::String& text)
{
    hints[&component] = text;
}

void OrangutanDrumsEditor::showHint (const juce::String& text)
{
    if (hintLabel.getText() == text)
        return;

    hintLabel.setText (text, juce::dontSendNotification);
}

void OrangutanDrumsEditor::mouseEnter (const juce::MouseEvent& event)
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

void OrangutanDrumsEditor::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    showHint (defaultHint);
}

//==============================================================================

void OrangutanDrumsEditor::timerCallback()
{
    auto& state = processor.getValueTreeState();

    // パッドの光り方（**変わったときだけ描き直します**。`DrumPadButton::setGlow`）
    for (size_t i = 0; i < pads.size(); ++i)
        if (pads[i] != nullptr)
            pads[i]->setGlow (processor.getPadActivity ((int) i));

    // エンジンと出口の割り当てが変わったら、パッドの字と見出しを直す
    bool selectionChanged = false;

    for (int pad = 0; pad < OrangutanDrumsProcessor::numPads; ++pad)
    {
        const int engine = processor.getPadEngine (pad);
        const bool direct = processor.isPadDirectOut (pad);

        if (engine != lastEngines[(size_t) pad])
        {
            lastEngines[(size_t) pad] = engine;

            if (pads[(size_t) pad] != nullptr)
                pads[(size_t) pad]->setEngineName (orangutan::engineName (engine));

            if (pad == selectedPad)
                selectionChanged = true;
        }

        if (direct != lastDirectOuts[(size_t) pad])
        {
            lastDirectOuts[(size_t) pad] = direct;

            if (pads[(size_t) pad] != nullptr)
                pads[(size_t) pad]->setDirectOut (direct);

            if (pad == selectedPad)
                selectionChanged = true;
        }
    }

    if (selectionChanged)
    {
        updatePadLabels();
        updateOutButton();
        repaint (contentX, controlY, contentW, controlH);
    }

    // 下の箱（DECAYはオートメーションでも動きます）
    badge.setState (selectedPad, processor.getPadEngine (selectedPad),
                     state.getRawParameterValue (OrangutanDrumsParams::padId (
                         selectedPad, OrangutanDrumsParams::padDecay))->load());

    // **パラアウトが届く先があるか**（本体のドラムアウトトラック。8.144）
    const bool available = processor.areDirectOutsAvailable();

    if (available != lastDirectAvailable)
    {
        lastDirectAvailable = available;
        repaint (contentX, controlY, contentW, controlH);
    }

    const int voices = processor.getActiveVoiceCount();

    if (voices != lastVoiceCount)
    {
        lastVoiceCount = voices;
        repaint (contentX, headerY, contentW, 40);
    }
}

//==============================================================================

void OrangutanDrumsEditor::drawTracked (juce::Graphics& g, const juce::String& text,
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

void OrangutanDrumsEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void OrangutanDrumsEditor::drawField (juce::Graphics& g) const
{
    // 8.290：**タブからマスターの下まで1枚**（Phase 283／本人の要望）
    const juce::Rectangle<int> area (contentX, imageY, contentW, imageH);

    juce::Path rounded;
    rounded.addRoundedRectangle (area.toFloat(), 10.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rounded);

        if (fieldImage.isValid())
        {
            // 8.289：**`fillDestination`で敷きます**（Phase 282）。
            // 絵はほぼ正方形に切り直してあるので、伸びは見えません
            // ——`stretchToFit`のままだと、縦長の枠で銅の流れが潰れます
            g.drawImage (fieldImage, area.toFloat(), juce::RectanglePlacement::fillDestination);

            g.setColour (OrangutanDrumsTheme::fieldVeil());
            g.fillRect (area);
        }
        else
        {
            g.setColour (OrangutanDrumsTheme::fieldKnobBody());
            g.fillRect (area);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);

    // 頁とマスターの区切り。**枠にはしません**——絵が切れて見えます
    g.setColour (OrangutanDrumsTheme::fieldInk().withAlpha (0.18f));
    g.fillRect ((float) contentX + 16.0f, (float) masterDividerY, (float) contentW - 32.0f, 1.0f);

    g.setColour (OrangutanDrumsTheme::fieldInkDim());
    drawTracked (g, "MASTER", { (float) contentX + 20.0f, (float) masterY + 12.0f },
                  juce::Font (juce::FontOptions (9.0f, juce::Font::bold)), 1.2f);
}

void OrangutanDrumsEditor::drawWordmark (juce::Graphics& g) const
{
    g.setColour (MantaTheme::text());
    drawTracked (g, "ORANGUTAN", { (float) wordmarkX, (float) headerY + 26.0f },
                  juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), 1.6f);

    g.setColour (OrangutanDrumsTheme::accent());
    drawTracked (g, "DRUMS", { (float) wordmarkX + 2.0f, (float) headerY + 42.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 3.6f);
}

void OrangutanDrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    //--------------------------------------------------------------------------
    // 上の帯
    drawPanel (g, { contentX, headerY, contentW, headerH });
    drawWordmark (g);

    {
        const juce::Rectangle<int> area (contentX + contentW - 96, headerY + 12, 80, 26);

        g.setColour (OrangutanDrumsTheme::accent());
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (lastVoiceCount), area.withTrimmedRight (46),
                     juce::Justification::centredRight, false);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("VOICES", area, juce::Justification::centredRight, false);
    }

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "OUTPUT", { (float) outputLabelX, (float) headerY + 66.0f },
                  juce::Font (juce::FontOptions (9.0f, juce::Font::bold)), 1.2f);

    // 案内の帯
    {
        const juce::Rectangle<int> area (contentX + hintInset, headerY + 76,
                                          contentW - hintInset * 2, 32);

        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (area.toFloat(), 5.0f);

        g.setColour (MantaTheme::border().withAlpha (0.6f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 5.0f, 1.0f);
    }

    //--------------------------------------------------------------------------
    // 選んでいるパッドの帯
    drawPanel (g, { contentX, controlY, contentW, controlH });

    {
        const int note = OrangutanDrumsProcessor::padBaseNote + selectedPad;

        g.setColour (MantaTheme::textDim());
        drawTracked (g, "PAD " + juce::String (selectedPad + 1).paddedLeft ('0', 2),
                      { (float) contentX + 12.0f, (float) controlY + 17.0f },
                      juce::Font (juce::FontOptions (9.0f, juce::Font::bold)), 1.2f);

        g.setColour (MantaTheme::text());
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText (midiNoteName (note) + " (" + juce::String (note) + ")",
                     contentX + 72, controlY + 6, 90, 14,
                     juce::Justification::centredLeft, false);

        // **DIRECTなのに受け皿が無いときは、そう出します**（8.144）
        if (processor.isPadDirectOut (selectedPad) && ! lastDirectAvailable)
        {
            g.setColour (OrangutanDrumsTheme::highlight());
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText ("NO DRUM OUT TRACK", contentX + 160, controlY + 6, contentW - 172, 14,
                         juce::Justification::centredRight, false);
        }
        else if (orangutan::engineChokeGroup (processor.getPadEngine (selectedPad)) > 0)
        {
            g.setColour (MantaTheme::textDim());
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawText ("CHOKE GROUP 1", contentX + 160, controlY + 6, contentW - 172, 14,
                         juce::Justification::centredRight, false);
        }
    }

    //--------------------------------------------------------------------------
    // 8.290：**タブも頁もマスターも、この1枚の上**（Phase 283）
    drawField (g);
}

//==============================================================================

void OrangutanDrumsEditor::resized()
{
    toolbar.setBounds (contentX, toolbarY, contentW, MantaPluginToolbar::preferredHeight);

    outputSlider.setBounds (outputSliderX, headerY + 50, outputSliderW, 20);
    outputValue.setBounds (outputValueX, headerY + 52, 48, 16);

    hintLabel.setBounds (contentX + hintInset + 8, headerY + 80, contentW - hintInset * 2 - 16, 26);

    engineBox.setBounds (engineBoxX, controlY + 24, engineBoxW, engineBoxH);
    outButton.setBounds (outButtonX, controlY + 24, outButtonW, engineBoxH);

    padsTabButton.setBounds (contentX + tabInset, tabsY, tabW, tabsH);
    knobsTabButton.setBounds (contentX + tabInset + tabW + tabGap, tabsY, tabW, tabsH);

    //--------------------------------------------------------------------------
    // 頁1：パッド（4×4。**下の段がパッド1**＝MPCの並び）
    for (size_t i = 0; i < pads.size(); ++i)
    {
        if (pads[i] == nullptr)
            continue;

        const int column = (int) i % 4;
        const int row = 3 - (int) i / 4;

        pads[i]->setBounds (padGridX + column * (padCell + padGap),
                             padGridY + row * (padCell + padGap),
                             padCell, padCell);
    }

    //--------------------------------------------------------------------------
    // 頁2：つまみ（4つ＋3つ）
    auto place = [] (DrumsFieldKnob* knob, int centreX, int centreY, int cellW, int cellH)
    {
        if (knob != nullptr)
            knob->setBounds (juce::Rectangle<int> (cellW, cellH)
                                 .withCentre ({ centreX, centreY }));
    };

    DrumsFieldKnob* const row1[4] { tune.get(), decay.get(), tone.get(), snap.get() };
    DrumsFieldKnob* const row2[3] { level.get(), pan.get(), send.get() };

    for (int i = 0; i < 4; ++i)
        place (row1[i], knobRow1X + i * knobCellW, knobRow1Y, knobCellW, knobCellH);

    for (int i = 0; i < 3; ++i)
        place (row2[i], knobRow2X + i * knobCellW, knobRow2Y, knobCellW, knobCellH);

    badge.setBounds (contentX + badgeInset, badgeY, contentW - badgeInset * 2, badgeH);

    //--------------------------------------------------------------------------
    // マスター（5つを1行に）
    DrumsFieldKnob* const masterKnobs[5]
    {
        drive.get(), glue.get(), reverb.get(), size.get(), damp.get()
    };

    for (int i = 0; i < 5; ++i)
        place (masterKnobs[i], masterX + i * masterCell, masterKnobY + masterCell / 2,
                masterCell, masterCell);

    keyboard.setBounds (contentX, keyboardY, contentW, keyboardH);
}
