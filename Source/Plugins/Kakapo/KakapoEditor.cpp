#include "KakapoEditor.h"

#include "../../AppIcon.h"        // 8.229：埋め込みの絵を名前で引く
#include "../../Branding.h"
#include "../../SegmentDisplay.h" // 8.177：Emberの7セグ
#include "../../Utf8.h"

namespace
{
    /** 画像の名前。**CMakeが`Resources/Plugins/kakapo_field.jpg`を埋め込みます**。 */
    const char* const fieldImageName = "kakapo_field_jpg";

    /** そのスケールの構成音（`"C D E F G A B"`）。

        **候補の名前だけでは、どの音が入っているのか分かりません**——
        鍵盤に手を置いたまま読めるように、7音をそのまま並べます。 */
    juce::String scaleNoteList (int root, kakapo::ScaleType type)
    {
        juce::String line;

        const int* steps = kakapo::scaleSteps (type);

        for (int i = 0; i < 7; ++i)
        {
            if (line.isNotEmpty())
                line << "  ";

            line << kakapo::pitchClassName (root + steps[i]);
        }

        return line;
    }
}

//==============================================================================

void KakapoFieldLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
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

    // ① 影。**暗い絵の上でも、円が置かれていることを見せる**
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillEllipse (juce::Rectangle<float> (bodyRadius * 2.0f + 3.0f, bodyRadius * 2.0f + 3.0f)
                       .withCentre (centre.translated (0.0f, 1.5f)));

    // ② 弧の地（端から端まで）
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (KakapoTheme::fieldKnobTrack());
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

    g.setColour (KakapoTheme::fieldKnobBody());
    g.fillEllipse (body);

    g.setColour (accent.withAlpha (enabled ? 0.6f : 0.25f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // ⑤ 指針
    juce::Path pointer;
    const float thickness = juce::jmax (1.6f, bodyRadius * 0.16f);

    pointer.addRoundedRectangle (-thickness * 0.5f, -bodyRadius * 0.88f,
                                  thickness, bodyRadius * 0.55f, thickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (enabled ? KakapoTheme::fieldInk() : KakapoTheme::fieldInkDim());
    g.fillPath (pointer);
}

void KakapoFieldLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
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

KakapoFieldKnob::KakapoFieldKnob (const juce::String& name, juce::Colour arcColour)
{
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    label.setColour (juce::Label::textColourId, KakapoTheme::fieldInk());
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    slider.setColour (juce::Slider::rotarySliderFillColourId, arcColour);
    slider.setColour (juce::Slider::textBoxTextColourId, KakapoTheme::fieldInk());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxHighlightColourId, arcColour.withAlpha (0.35f));

    // **読み取り専用の欄**（打ち込みは右クリック。`ValueEntrySlider`）
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 110, valueHeight);
    addAndMakeVisible (slider);
}

void KakapoFieldKnob::resized()
{
    auto area = getLocalBounds();

    label.setBounds (area.removeFromTop (labelHeight));
    slider.setBounds (area);
}

//==============================================================================

void KakapoPitchClassPanel::setState (const KakapoProcessor::AnalysisSnapshot& snapshot)
{
    const auto& favoured = snapshot.result.majorFavoured ? snapshot.result.major
                                                          : snapshot.result.minor;

    if (histogram == snapshot.histogram
         && candidate.root == favoured.root
         && candidate.type == favoured.type
         && hasEnoughNotes == snapshot.result.hasEnoughNotes)
        return;

    histogram = snapshot.histogram;
    candidate = favoured;
    hasEnoughNotes = snapshot.result.hasEnoughNotes;

    repaint();
}

void KakapoPitchClassPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (KakapoTheme::fieldPanel());
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (KakapoTheme::fieldPanelEdge());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    g.setColour (KakapoTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("PITCH CLASS", bounds.reduced (14.0f, 10.0f).removeFromTop (14.0f),
                 juce::Justification::centredLeft, false);

    // いちばん大きい重みで割って、**いつも同じ高さまで伸びる**ようにします
    // （生の重みで描くと、長く弾くほど全部が天井に張り付きます）
    float loudest = 0.0f;

    for (const float weight : histogram)
        loudest = juce::jmax (loudest, weight);

    const auto area = getLocalBounds().reduced (14, 8).withTrimmedTop (22);
    const auto namesArea = area.withTop (area.getBottom() - 20);
    const auto barsArea = area.withTrimmedBottom (24);

    constexpr int barGap = 4;
    const float barWidth = ((float) barsArea.getWidth() - barGap * 11.0f) / 12.0f;

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
    {
        const float x = (float) barsArea.getX() + (barWidth + barGap) * (float) pitchClass;

        const bool inScale = kakapo::isInScale (pitchClass - candidate.root, candidate.type);
        const bool isRoot = pitchClass == candidate.root;

        // 地（どこまで伸びるか）。**薄くすること**——濃いと、鳴っていない音まで
        // 「いっぱいまで鳴っている」ように見えます
        g.setColour (KakapoTheme::fieldInk().withAlpha (0.05f));
        g.fillRoundedRectangle (x, (float) barsArea.getY(), barWidth, (float) barsArea.getHeight(), 3.0f);

        if (loudest > 0.0f)
        {
            const float height = (float) barsArea.getHeight() * (histogram[(size_t) pitchClass] / loudest);

            if (height > 1.0f)
            {
                auto colour = KakapoTheme::fieldInk().withAlpha (0.35f);

                if (hasEnoughNotes && inScale)
                    colour = isRoot ? KakapoTheme::fieldHighlight() : KakapoTheme::fieldAccent();

                g.setColour (colour);
                g.fillRoundedRectangle (x, (float) barsArea.getBottom() - height,
                                         barWidth, height, 3.0f);
            }
        }

        // 音名。**スケールに入っている音だけ濃く**
        g.setColour (hasEnoughNotes && ! inScale ? KakapoTheme::fieldInkDim()
                                                 : KakapoTheme::fieldInk());
        g.setFont (juce::Font (juce::FontOptions (10.0f, isRoot ? juce::Font::bold : juce::Font::plain)));
        g.drawText (kakapo::pitchClassName (pitchClass),
                     juce::Rectangle<float> (x, (float) namesArea.getY(), barWidth, 16.0f),
                     juce::Justification::centred, false);
    }
}

//==============================================================================

void KakapoCandidatePanel::setState (const KakapoProcessor::AnalysisSnapshot& snapshot)
{
    if (result.major.root == snapshot.result.major.root
         && result.minor.root == snapshot.result.minor.root
         && std::abs (result.major.matchRatio - snapshot.result.major.matchRatio) < 0.002f
         && std::abs (result.minor.matchRatio - snapshot.result.minor.matchRatio) < 0.002f
         && result.majorFavoured == snapshot.result.majorFavoured
         && hasEnoughNotes == snapshot.result.hasEnoughNotes)
        return;

    result = snapshot.result;
    hasEnoughNotes = snapshot.result.hasEnoughNotes;

    repaint();
}

void KakapoCandidatePanel::drawCandidate (juce::Graphics& g, juce::Rectangle<int> area,
                                           const kakapo::ScaleCandidate& scaleCandidate,
                                           bool favoured) const
{
    auto bounds = area.toFloat();

    if (favoured)
    {
        g.setColour (KakapoTheme::fieldAccent().withAlpha (0.12f));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (KakapoTheme::fieldAccent().withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);
    }

    auto inner = area.reduced (14, 10);
    auto top = inner.removeFromTop (16);

    g.setColour (KakapoTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (kakapo::scaleTypeName (scaleCandidate.type), top,
                 juce::Justification::centredLeft, false);

    if (favoured)
    {
        g.setColour (KakapoTheme::fieldHighlight());
        g.drawText ("FAVOURED", top, juce::Justification::centredRight, false);
    }

    auto nameArea = inner.removeFromTop (34);

    g.setColour (hasEnoughNotes ? KakapoTheme::fieldInk() : KakapoTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (25.0f, juce::Font::bold)));
    g.drawText (hasEnoughNotes ? juce::String (kakapo::pitchClassName (scaleCandidate.root)) + " "
                                   + kakapo::scaleTypeName (scaleCandidate.type)
                               : juce::String ("--"),
                 nameArea, juce::Justification::centredLeft, false);

    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.setColour (hasEnoughNotes ? KakapoTheme::fieldAccent() : KakapoTheme::fieldInkDim());
    g.drawText (hasEnoughNotes
                   ? juce::String (juce::roundToInt (scaleCandidate.matchRatio * 100.0f)) + "%"
                   : juce::String(),
                 nameArea, juce::Justification::centredRight, false);

    // 一致率の帯
    auto barArea = inner.removeFromTop (12).toFloat();

    g.setColour (KakapoTheme::fieldInk().withAlpha (0.10f));
    g.fillRoundedRectangle (barArea, 3.0f);

    if (hasEnoughNotes && scaleCandidate.matchRatio > 0.0f)
    {
        g.setColour (favoured ? KakapoTheme::fieldAccent()
                              : KakapoTheme::fieldAccent().withAlpha (0.55f));
        g.fillRoundedRectangle (barArea.withWidth (barArea.getWidth() * scaleCandidate.matchRatio),
                                 3.0f);
    }

    // 構成音（**どの音が入っているのか**）
    inner.removeFromTop (8);

    g.setColour (KakapoTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (hasEnoughNotes ? scaleNoteList (scaleCandidate.root, scaleCandidate.type)
                               : juce::String(),
                 inner.removeFromTop (16), juce::Justification::centredLeft, false);
}

void KakapoCandidatePanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (KakapoTheme::fieldPanel());
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (KakapoTheme::fieldPanelEdge());
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    auto area = getLocalBounds().reduced (14, 10);

    g.setColour (KakapoTheme::fieldInkDim());
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText ("SCALE", area.removeFromTop (14), juce::Justification::centredLeft, false);

    if (! hasEnoughNotes)
    {
        g.setColour (KakapoTheme::fieldInkDim());
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("LISTENING", area.removeFromTop (14), juce::Justification::centredRight, false);
    }

    // **高さは決め打ちにします**（余りを2つに配ると、中身より枠が大きくなって
    // 「何も入っていない箱」に見えます）
    constexpr int blockHeight = 132;
    constexpr int blockGap = 18;

    area.removeFromTop (juce::jmax (0, (area.getHeight() - blockHeight * 2 - blockGap) / 2));

    drawCandidate (g, area.removeFromTop (blockHeight), result.major,
                    hasEnoughNotes && result.majorFavoured);

    area.removeFromTop (blockGap);

    drawCandidate (g, area.removeFromTop (blockHeight), result.minor,
                    hasEnoughNotes && ! result.majorFavoured);
}

//==============================================================================

void KakapoCentreBadge::setState (int pitchClass, bool favoured, bool hasEnoughNotes)
{
    if (centre == pitchClass && majorFavoured == favoured && enough == hasEnoughNotes)
        return;

    centre = pitchClass;
    majorFavoured = favoured;
    enough = hasEnoughNotes;

    repaint();
}

void KakapoCentreBadge::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (MantaTheme::graphBackground());
    g.fillRoundedRectangle (bounds, 5.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    auto area = bounds.reduced (8.0f, 5.0f);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (8.5f)));
    g.drawText ("CENTRE", area.removeFromTop (11.0f), juce::Justification::centredLeft, false);

    g.setColour (enough ? KakapoTheme::accent() : MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    g.drawText (enough && centre >= 0 ? kakapo::pitchClassName (centre) : "--",
                 area, juce::Justification::centredLeft, false);

    if (enough)
    {
        g.setColour (KakapoTheme::highlight());
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (majorFavoured ? "MAJOR" : "MINOR", area,
                     juce::Justification::centredRight, false);
    }
}

//==============================================================================

void KakapoKeyboard::setMasks (const std::array<bool, 128>& activeMask,
                                const std::array<bool, 128>& recentMask)
{
    if (active == activeMask && recent == recentMask)
        return;

    active = activeMask;
    recent = recentMask;

    repaint();
}

juce::Colour KakapoKeyboard::overlayFor (int midiNoteNumber) const
{
    if (midiNoteNumber < 0 || midiNoteNumber > 127)
        return juce::Colours::transparentBlack;

    if (active[(size_t) midiNoteNumber])
        return KakapoTheme::accent().withAlpha (0.75f);

    if (recent[(size_t) midiNoteNumber])
        return KakapoTheme::accent().withAlpha (0.28f);

    return juce::Colours::transparentBlack;
}

void KakapoKeyboard::drawWhiteNote (int midiNoteNumber, juce::Graphics& g,
                                     juce::Rectangle<float> area, bool isDown, bool isOver,
                                     juce::Colour lineColour, juce::Colour textColour)
{
    juce::MidiKeyboardComponent::drawWhiteNote (midiNoteNumber, g, area, isDown, isOver,
                                                 lineColour, textColour);

    const auto overlay = overlayFor (midiNoteNumber);

    if (overlay.isTransparent())
        return;

    g.setColour (overlay);
    g.fillRect (area);

    g.setColour (lineColour);
    g.drawRect (area);
}

void KakapoKeyboard::drawBlackNote (int midiNoteNumber, juce::Graphics& g,
                                     juce::Rectangle<float> area, bool isDown, bool isOver,
                                     juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (midiNoteNumber, g, area, isDown, isOver,
                                                 noteFillColour);

    const auto overlay = overlayFor (midiNoteNumber);

    if (overlay.isTransparent())
        return;

    // **黒鍵はもともと暗い**ので、白鍵と同じ濃さだと何も見えません
    g.setColour (overlay.withMultipliedAlpha (1.35f));
    g.fillRect (area);
}

//==============================================================================

KakapoEditor::KakapoEditor (KakapoProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::scalePresetFolder),
      keyboard (processorToUse.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    addAndMakeVisible (toolbar);

    toolbar.onStateRestored = [this]
    {
        updateHoldSuffix();
        repaint();
    };

    // **工場プリセットは置いていません**（`KakapoParameters.h`）。
    // 動かせるのは3つだけで、「出来合いの音」に当たるものがありません

    // 8.186：**名前を直に書かないこと**（`Branding.h`が唯一の出どころ）
    titleLabel.setText (Branding::scalePluginName, juce::dontSendNotification);
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
    registerHint (badge, utf8 ("いちばん多く鳴っている音（トーナルセンター）と、そこから決めた優勢なほう"));

    auto& state = processor.getValueTreeState();

    //--------------------------------------------------------------------------
    // 出力
    outputSlider.setLookAndFeel (&fieldLookAndFeel.get());
    outputSlider.setColour (juce::Slider::trackColourId, KakapoTheme::accent());
    outputSlider.setColour (juce::Slider::thumbColourId, KakapoTheme::accent());
    outputSlider.setColour (juce::Slider::backgroundColourId, MantaTheme::graphBackground());
    addAndMakeVisible (outputSlider);
    styledSliders.push_back (&outputSlider);

    sliderAttachments.add (new SliderAttachment (state, KakapoParams::volume, outputSlider));

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

    registerHint (outputSlider, utf8 ("内蔵リードの音量。音色は固定で、触れるのはここだけです"));

    //--------------------------------------------------------------------------
    // MIDIスルー
    MantaPluginToolbar::styleButton (thruButton, "MIDI THRU");
    thruButton.setClickingTogglesState (true);
    thruButton.setColour (juce::TextButton::buttonOnColourId, KakapoTheme::highlight());
    thruButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (thruButton);

    buttonAttachments.add (new ButtonAttachment (state, KakapoParams::midiThru, thruButton));

    registerHint (thruButton, utf8 ("受け取った音符を、そのまま出口へ流します（このアプリでは受け手がいないので、いまは効きません）"));

    //--------------------------------------------------------------------------
    // 絵の上のつまみ（1つだけ）
    holdKnob = std::make_unique<KakapoFieldKnob> ("HOLD", KakapoTheme::fieldHighlight());

    holdKnob->setLookAndFeel (&fieldLookAndFeel.get());
    styledComponents.push_back (holdKnob.get());

    holdKnob->slider.setLookAndFeel (&fieldLookAndFeel.get());
    holdKnob->slider.setPopupDisplayEnabled (false, false, this);

    // **整数で出します**（音数に小数はありません。8.291の`setDisplayDecimals`）
    holdKnob->slider.setDisplayDecimals (0);
    holdKnob->slider.setValueEntryDecimals (0);

    addAndMakeVisible (*holdKnob);
    styledSliders.push_back (&holdKnob->slider);

    sliderAttachments.add (new SliderAttachment (state, KakapoParams::holdLength, holdKnob->slider));

    registerHint (*holdKnob, utf8 ("判定に使う音の長さ。直近の何音か、または直近の何秒かを決めます"));

    //--------------------------------------------------------------------------
    // 間引き方（NOTES / SECONDS）
    const auto modeNames = KakapoParams::getHoldModeNames();

    for (size_t i = 0; i < holdModeButtons.size(); ++i)
    {
        const int mode = (int) i;

        MantaPluginToolbar::styleButton (holdModeButtons[i], modeNames[mode]);

        holdModeButtons[i].setColour (juce::TextButton::buttonColourId,
                                       KakapoTheme::fieldPanel());
        holdModeButtons[i].setColour (juce::TextButton::textColourOffId, KakapoTheme::fieldInk());
        holdModeButtons[i].setColour (juce::TextButton::buttonOnColourId,
                                       KakapoTheme::fieldHighlight());
        holdModeButtons[i].setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        holdModeButtons[i].onClick = [this, mode] { setHoldMode (mode); };

        addAndMakeVisible (holdModeButtons[i]);
        registerHint (holdModeButtons[i], utf8 ("HOLDの単位。音数で切るか、秒で切るかを選びます"));
    }

    //--------------------------------------------------------------------------
    // リセット（**パラメータではありません**。設計書12章）
    MantaPluginToolbar::styleButton (resetButton, "RESET");
    resetButton.setColour (juce::TextButton::buttonColourId, KakapoTheme::fieldPanel());
    resetButton.setColour (juce::TextButton::textColourOffId, KakapoTheme::fieldInk());
    addAndMakeVisible (resetButton);

    resetButton.onClick = [this] { processor.requestReset(); };

    registerHint (resetButton, utf8 ("控えている音と判定を消します。弾き直すところから数え直したいときに"));

    //--------------------------------------------------------------------------
    addAndMakeVisible (pitchClassPanel);
    registerHint (pitchClassPanel, utf8 ("12音それぞれの使われ方。色が付いているのが、優勢なスケールに入っている音です"));

    addAndMakeVisible (candidatePanel);
    registerHint (candidatePanel, utf8 ("メジャー1つとマイナー1つ。帯は一致率で、FAVOUREDが付いているほうが優勢です"));

    //--------------------------------------------------------------------------
    // 鍵盤
    keyboard.setAvailableRange (36, 84);   // C2〜C6
    keyboard.setScrollButtonsVisible (false);

    // **C4＝60の流儀**（本体のピアノロールと同じ。8.121）
    keyboard.setOctaveForMiddleC (4);

    int whiteKeys = 0;

    for (int note = 36; note <= 84; ++note)
        if (! juce::MidiMessage::isMidiNoteBlack (note))
            ++whiteKeys;

    keyboard.setKeyWidth ((float) contentW / (float) juce::jmax (1, whiteKeys));

    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, AppColours::pianoWhiteKey);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, AppColours::pianoBlackKey);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, MantaTheme::border());
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, KakapoTheme::accent());
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                         KakapoTheme::accent().withAlpha (0.4f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (keyboard);

    registerHint (keyboard, utf8 ("鳴っている音は濃く、控えに残っている音は薄く出ます。ここから弾くこともできます"));

    //--------------------------------------------------------------------------
    addMouseListener (this, true);

    startTimerHz (20);
    timerCallback();

    // 8.172：**大きさは固定**
    setSize (fixedWidth, fixedHeight);
}

KakapoEditor::~KakapoEditor()
{
    removeMouseListener (this);

    for (auto* slider : styledSliders)
        slider->setLookAndFeel (nullptr);

    for (auto* component : styledComponents)
        component->setLookAndFeel (nullptr);
}

//==============================================================================

void KakapoEditor::setHoldMode (int mode)
{
    if (auto* parameter = processor.getValueTreeState().getParameter (KakapoParams::holdMode))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) mode));
        parameter->endChangeGesture();
    }
}

void KakapoEditor::updateHoldSuffix()
{
    const int mode = (int) processor.getValueTreeState()
                                      .getRawParameterValue (KakapoParams::holdMode)->load();

    // **単位も一緒に入れ替えます**（つまみ1本で意味が変わるので。`KakapoParameters.h`）
    holdKnob->slider.setTextValueSuffix (mode == 0 ? " NOTES" : " SEC");
    holdKnob->slider.updateText();

    for (size_t i = 0; i < holdModeButtons.size(); ++i)
        holdModeButtons[i].setToggleState ((int) i == mode, juce::dontSendNotification);
}

void KakapoEditor::registerHint (juce::Component& component, const juce::String& text)
{
    hints[&component] = text;
}

void KakapoEditor::showHint (const juce::String& text)
{
    if (hintLabel.getText() == text)
        return;

    hintLabel.setText (text, juce::dontSendNotification);
}

void KakapoEditor::mouseEnter (const juce::MouseEvent& event)
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

void KakapoEditor::mouseExit (const juce::MouseEvent& event)
{
    juce::ignoreUnused (event);

    showHint (defaultHint);
}

//==============================================================================

void KakapoEditor::timerCallback()
{
    const auto snapshot = processor.readSnapshot();

    pitchClassPanel.setState (snapshot);
    candidatePanel.setState (snapshot);

    badge.setState (snapshot.result.tonalCentre, snapshot.result.majorFavoured,
                     snapshot.result.hasEnoughNotes);

    keyboard.setMasks (snapshot.active, snapshot.recent);

    const int mode = (int) processor.getValueTreeState()
                                      .getRawParameterValue (KakapoParams::holdMode)->load();

    if (mode != lastHoldMode)
    {
        lastHoldMode = mode;
        updateHoldSuffix();
        repaint (rightBoxX, boxY, rightBoxW, boxH);
    }

    // 右の箱（いま何を根拠にしているか）と、控えている音の数
    const bool statusChanged = snapshot.result.hasEnoughNotes != lastSnapshot.result.hasEnoughNotes
                                 || snapshot.result.tonalCentre != lastSnapshot.result.tonalCentre
                                 || snapshot.result.majorFavoured != lastSnapshot.result.majorFavoured
                                 || snapshot.result.major.root != lastSnapshot.result.major.root
                                 || snapshot.result.minor.root != lastSnapshot.result.minor.root;

    lastSnapshot = snapshot;

    if (statusChanged)
        repaint (rightBoxX, boxY, rightBoxW, boxH);

    if (snapshot.noteCount != lastNoteCount)
    {
        lastNoteCount = snapshot.noteCount;
        repaint (notesX, headerY, contentX + contentW - notesX, headerH);
    }
}

//==============================================================================

juce::String KakapoEditor::statusText() const
{
    if (! lastSnapshot.result.hasEnoughNotes)
        return "PLAY AT LEAST THREE DIFFERENT NOTES";

    const auto& favoured = lastSnapshot.result.majorFavoured ? lastSnapshot.result.major
                                                              : lastSnapshot.result.minor;

    juce::String line;

    line << kakapo::pitchClassName (lastSnapshot.result.tonalCentre) << " IS THE STRONGEST NOTE  /  "
         << kakapo::pitchClassName (favoured.root) << " " << kakapo::scaleTypeName (favoured.type)
         << " IS FAVOURED";

    if (kakapo::isRelativeKey (lastSnapshot.result.major, lastSnapshot.result.minor))
        line << "  /  RELATIVE KEYS";

    return line;
}

//==============================================================================

void KakapoEditor::drawTracked (juce::Graphics& g, const juce::String& text,
                                 juce::Point<float> origin, const juce::Font& font, float tracking)
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

void KakapoEditor::drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour (MantaTheme::panelBackground());
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    g.setColour (MantaTheme::border());
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 8.0f, 1.0f);
}

void KakapoEditor::drawBox (juce::Graphics& g, juce::Rectangle<int> area,
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

void KakapoEditor::drawField (juce::Graphics& g) const
{
    const juce::Rectangle<int> area (contentX, fieldY, contentW, fieldH);

    juce::Path rounded;
    rounded.addRoundedRectangle (area.toFloat(), 10.0f);

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rounded);

        if (fieldImage.isValid())
        {
            g.drawImage (fieldImage, area.toFloat(), juce::RectanglePlacement::fillDestination);

            // **薄いベール**（絵を暗いまま見せる。読ませるものは板の上。`KakapoTheme`）
            g.setColour (KakapoTheme::fieldVeil());
            g.fillRect (area);
        }
        else
        {
            g.setColour (KakapoTheme::fieldKnobBody());
            g.fillRect (area);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);

    // **影を先に置くこと。** 苔の絵は場所で明るさが変わるので、白い字だけでは
    // 明るいところで消えます（1度そう写りました）
    const juce::Font caption (juce::FontOptions (9.5f, juce::Font::bold));

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    drawTracked (g, "KAKAPO / SCALE FIELD",
                  { (float) area.getX() + 25.0f, (float) area.getY() + 24.0f }, caption, 1.6f);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    drawTracked (g, "KAKAPO / SCALE FIELD",
                  { (float) area.getX() + 24.0f, (float) area.getY() + 23.0f }, caption, 1.6f);
}

void KakapoEditor::drawWordmark (juce::Graphics& g) const
{
    g.setColour (MantaTheme::text());
    drawTracked (g, "KAKAPO", { (float) wordmarkX, (float) headerY + 34.0f },
                  juce::Font (juce::FontOptions (21.0f, juce::Font::bold)), 2.0f);

    g.setColour (KakapoTheme::accent());
    drawTracked (g, "SCALE SUGGESTER", { (float) wordmarkX + 2.0f, (float) headerY + 52.0f },
                  juce::Font (juce::FontOptions (9.0f, juce::Font::bold)), 2.2f);
}

void KakapoEditor::paint (juce::Graphics& g)
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

    g.setColour (KakapoTheme::highlight());
    g.fillEllipse ((float) hintArea.getX() + 11.0f, (float) hintArea.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "OUTPUT", { (float) outputLabelX, (float) headerY + 41.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.2f);

    // 控えている音の数
    {
        const juce::Rectangle<int> area (notesX, headerY, contentX + contentW - notesX - 20, headerH);

        g.setColour (KakapoTheme::accent());
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (lastNoteCount), area.withTrimmedRight (40),
                     juce::Justification::centredRight, false);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText ("NOTES", area, juce::Justification::centredRight, false);
    }

    //--------------------------------------------------------------------------
    // 2つめの帯
    drawPanel (g, { contentX, controlY, contentW, controlH });

    g.setColour (MantaTheme::textDim());
    drawTracked (g, "A SCALE SUGGESTER WITH A LEAD VOICE",
                  { (float) leftBoxX, (float) rowY + 19.0f },
                  juce::Font (juce::FontOptions (9.5f, juce::Font::bold)), 1.4f);

    drawBox (g, { leftBoxX, boxY, leftBoxW, boxH }, "DETECTION");
    drawBox (g, { rightBoxX, boxY, rightBoxW, boxH }, "STATUS");

    g.setColour (MantaTheme::text());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText ("24 CANDIDATES / 12 ROOTS x MAJOR + NATURAL MINOR",
                 leftBoxX + 10, boxY + 24, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("SCORE = NOTES IN THE SCALE - NOTES OUTSIDE IT",
                 leftBoxX + 10, boxY + 40, leftBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    g.setColour (lastSnapshot.result.hasEnoughNotes ? MantaTheme::text() : MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    g.drawText (statusText(), rightBoxX + 10, boxY + 24, rightBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    g.setColour (MantaTheme::textDim());
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (lastHoldMode == 0 ? "HOLDING THE LAST NOTES" : "HOLDING THE LAST SECONDS",
                 rightBoxX + 10, boxY + 40, rightBoxW - 20, 14,
                 juce::Justification::centredLeft, false);

    //--------------------------------------------------------------------------
    drawField (g);
}

//==============================================================================

void KakapoEditor::resized()
{
    toolbar.setBounds (contentX, toolbarY, contentW, MantaPluginToolbar::preferredHeight);
    titleLabel.setBounds (toolbar.getTrailingArea().withTrimmedRight (6));

    hintLabel.setBounds (hintX + 24, headerY + 14, hintW - 34, headerH - 28);
    badge.setBounds (badgeX, headerY + 10, badgeW, headerH - 20);

    outputSlider.setBounds (outputSliderX, headerY + 24, outputSliderW, 20);
    outputValue.setBounds (outputValueX, headerY + 26, 44, 16);

    thruButton.setBounds (thruX, rowY, thruW, rowH);

    pitchClassPanel.setBounds (pitchPanelX, panelY, pitchPanelW, panelH);
    candidatePanel.setBounds (candidatePanelX, panelY, candidatePanelW, panelH);

    if (holdKnob != nullptr)
        holdKnob->setBounds (columnX + (columnW - holdKnobW) / 2, holdKnobY, holdKnobW, holdKnobH);

    for (size_t i = 0; i < holdModeButtons.size(); ++i)
        holdModeButtons[i].setBounds (columnX + (int) i * (modeButtonW + modeButtonGap),
                                       modeButtonY, modeButtonW, modeButtonH);

    resetButton.setBounds (columnX, resetButtonY, columnW, resetButtonH);

    keyboard.setBounds (contentX, keyboardY, contentW, keyboardH);
}
