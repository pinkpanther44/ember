#include "EQCurveComponent.h"

#include "../MantaTheme.h"
#include "../../Utf8.h"

#include <cmath>

using MantaEQParams::Channel;
using MantaEQParams::Shape;

namespace
{
    /** 目盛りを入れる周波数（数字を出すものだけ`true`）。 */
    struct GridFrequency { float hz; bool labelled; };

    const GridFrequency gridFrequencies[]
    {
        {    20.0f, true  }, {    30.0f, false }, {    40.0f, false }, {    50.0f, false },
        {    60.0f, false }, {    80.0f, false }, {   100.0f, true  }, {   150.0f, false },
        {   200.0f, false }, {   300.0f, false }, {   400.0f, false }, {   500.0f, false },
        {   600.0f, false }, {   800.0f, false }, {  1000.0f, true  }, {  1500.0f, false },
        {  2000.0f, false }, {  3000.0f, false }, {  4000.0f, false }, {  5000.0f, false },
        {  6000.0f, false }, {  8000.0f, false }, { 10000.0f, true  }, { 15000.0f, false },
        { 20000.0f, true  },
    };

    juce::String frequencyLabel (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 0) + "k";

        return juce::String ((int) hz);
    }

    /** MIDIノート番号 → 周波数。仕様書4.14の鍵盤表示で使う。 */
    float noteToFrequency (int midiNote)
    {
        return 440.0f * std::pow (2.0f, (float) (midiNote - 69) / 12.0f);
    }

    bool isBlackKey (int midiNote)
    {
        switch (midiNote % 12)
        {
            case 1: case 3: case 6: case 8: case 10: return true;
            default: return false;
        }
    }

    // 右クリックメニューの番号
    enum MenuIds
    {
        shapeBase = 1,        // +0〜8
        slopeBase = 20,       // +0〜7
        channelBase = 40,     // +0〜4
        toggleDynamic = 60,
        toggleSolo = 61,
        toggleBypass = 62,
        removeBand = 63,
        enterFrequency = 70,
        curveRangeBase = 100, // +0〜3
        toggleKeyboard = 110
    };

    const float curveRangeChoices[] { 6.0f, 12.0f, 18.0f, 30.0f };

    //==========================================================================
    /** Phase 207：**右クリックメニューの中で、そのまま打ち込める行**（本人の要望）。

        Phase 206では別のダイアログ（`NameEntry`）を開いていましたが、
        **メニューを出す → 項目を選ぶ → 窓が開く → 打つ → OK** と4手かかっていました。
        メニューの中に入れると**メニューを出す → 打つ → Enter**で済みます。

        ### `juce::PopupMenu`の中で入力欄が動く条件

        - `PopupMenu::CustomComponent (false)` ＝ **押しても項目として確定しない**
          （trueだと、欄を押した瞬間にメニューが閉じます）
        - **こちらから`grabKeyboardFocus()`すること。**
          メニューの窓は`setWantsKeyboardFocus(false)`なので、
          放っておくと誰も打ち込みを受け取りません
        - フォーカスは**開いた直後にはまだ取れない**（窓が出来ていない）ので、
          タイマーで1回だけ遅らせています

        メニューは`doesAnyJuceCompHaveFocus()`を見て閉じるかどうかを決めるので、
        **欄がフォーカスを持っているあいだは勝手に閉じません**。 */
    class ParameterEntryItem : public juce::PopupMenu::CustomComponent,
                                private juce::Timer
    {
    public:
        ParameterEntryItem (const juce::String& captionText,
                             juce::RangedAudioParameter& parameterToUse,
                             bool shouldGrabFocus,
                             std::function<void()> onCommitted)
            : juce::PopupMenu::CustomComponent (false),
              parameter (parameterToUse),
              committed (std::move (onCommitted))
        {
            caption.setText (captionText, juce::dontSendNotification);
            caption.setColour (juce::Label::textColourId, MantaTheme::textDim());
            caption.setFont (juce::Font (juce::FontOptions (12.0f)));
            addAndMakeVisible (caption);

            editor.setText (parameter.getCurrentValueAsText(), false);
            editor.setSelectAllWhenFocused (true);
            editor.setJustification (juce::Justification::centredRight);
            editor.setColour (juce::TextEditor::backgroundColourId, MantaTheme::graphBackground());
            editor.setColour (juce::TextEditor::textColourId, MantaTheme::text());
            editor.setColour (juce::TextEditor::outlineColourId, MantaTheme::border());
            editor.setColour (juce::TextEditor::focusedOutlineColourId, MantaTheme::accent());

            editor.onReturnKey = [this]
            {
                commit();

                // **Enterで閉じる。** 3つとも直したいときはTabで移れます
                triggerMenuItem();
            };

            editor.onEscapeKey = [this]
            {
                // **打ちかけを捨ててから閉じる。** そのまま閉じると、
                // フォーカスが外れた拍子に`commit()`が走って入ってしまいます
                editor.setText (parameter.getCurrentValueAsText(), false);
                triggerMenuItem();
            };

            editor.onFocusLost = [this] { commit(); };

            addAndMakeVisible (editor);

            if (shouldGrabFocus)
                startTimer (40);
        }

        void getIdealSize (int& idealWidth, int& idealHeight) override
        {
            idealWidth = 208;
            idealHeight = 26;
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4, 2);

            caption.setBounds (area.removeFromLeft (68));
            editor.setBounds (area);
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            editor.grabKeyboardFocus();
        }

        void commit()
        {
            const auto text = editor.getText();

            // **数字を含まない文字列は無視する。** `getValueForText()`は
            // 読めない文字列を0として扱うので、そのまま渡すと**下限へ飛びます**
            // （`ValueEntrySlider::applyTextValue()`と同じ決まり）
            if (! text.containsAnyOf ("0123456789"))
            {
                editor.setText (parameter.getCurrentValueAsText(), false);
                return;
            }

            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, parameter.getValueForText (text)));
            parameter.endChangeGesture();

            // 丸められた結果を出し直す（"1234"→"1.23 kHz"）
            editor.setText (parameter.getCurrentValueAsText(), false);

            if (committed != nullptr)
                committed();
        }

        juce::RangedAudioParameter& parameter;
        std::function<void()> committed;

        juce::Label caption;
        juce::TextEditor editor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterEntryItem)
    };
}

//==============================================================================

EQCurveComponent::EQCurveComponent (MantaEQProcessor& processorToUse)
    : processor (processorToUse)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::NormalCursor);

    updateBandSnapshots();

    // 毎秒30回。**アナライザーの取り込みと描き直しは同じ間隔**にしてある
    // （別々にすると、取り込んだのに描かれないフレームができる）
    startTimerHz (30);
}

EQCurveComponent::~EQCurveComponent()
{
    stopTimer();
}

void EQCurveComponent::timerCallback()
{
    const double sampleRate = processor.getSampleRateForDisplay();

    if (sampleRate != lastSpectrumSampleRate)
    {
        lastSpectrumSampleRate = sampleRate;
        preSpectrum.prepare (sampleRate);
        postSpectrum.prepare (sampleRate);
    }

    const auto uiState = processor.getUiState();
    const int mode = MantaEQUiState::getInt (uiState, MantaEQUiState::analyserMode, 3);
    const bool frozen = MantaEQUiState::getBool (uiState, MantaEQUiState::analyserFrozen, false);
    const float speed = MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserSpeed, 0.5f);

    if ((mode & 1) != 0)
        preSpectrum.update (processor.getPreAnalyserFifo(), speed, frozen);

    if ((mode & 2) != 0)
        postSpectrum.update (processor.getPostAnalyserFifo(), speed, frozen);

    // **描く前に取り直す。** マウスの判定（`findBandAt()`）も同じ写しを見るので、
    // ここで1回取っておけば、描画と操作でずれません
    updateBandSnapshots();

    repaint();
}

//==============================================================================

juce::Rectangle<float> EQCurveComponent::getGraphArea() const
{
    auto area = getLocalBounds().toFloat();

    area.removeFromBottom ((float) labelStripHeight);

    if (MantaEQUiState::getBool (processor.getUiState(), MantaEQUiState::showKeyboard, false))
        area.removeFromBottom ((float) keyboardHeight);

    return area;
}

float EQCurveComponent::getCurveRangeDb() const
{
    return MantaEQUiState::getFloat (processor.getUiState(), MantaEQUiState::curveRangeDb, 18.0f);
}

float EQCurveComponent::frequencyToX (float frequencyHz) const
{
    const auto area = getGraphArea();

    return area.getX() + MantaEQParams::frequencyToProportion (frequencyHz) * area.getWidth();
}

float EQCurveComponent::xToFrequency (float x) const
{
    const auto area = getGraphArea();

    if (area.getWidth() <= 0.0f)
        return 1000.0f;

    return MantaEQParams::proportionToFrequency ((x - area.getX()) / area.getWidth());
}

float EQCurveComponent::gainToY (float gainDb) const
{
    const auto area = getGraphArea();
    const float range = getCurveRangeDb();

    const float proportion = juce::jlimit (-0.2f, 1.2f, 0.5f - (gainDb / (range * 2.0f)));

    return area.getY() + proportion * area.getHeight();
}

float EQCurveComponent::yToGain (float y) const
{
    const auto area = getGraphArea();
    const float range = getCurveRangeDb();

    if (area.getHeight() <= 0.0f)
        return 0.0f;

    const float proportion = (y - area.getY()) / area.getHeight();

    return juce::jlimit (-MantaEQParams::maxGainDb, MantaEQParams::maxGainDb,
                          (0.5f - proportion) * range * 2.0f);
}

float EQCurveComponent::analyserDbToY (float levelDb, float frequencyHz) const
{
    const auto area = getGraphArea();
    const auto uiState = processor.getUiState();

    // 仕様書4.8のTilt。**低いほうが必ず大きく出る**ので、
    // そのまま描くと右下がりの三角形にしか見えません
    const float tilt = MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserTilt, 4.5f);
    const float floorDb = MantaEQUiState::getFloat (uiState, MantaEQUiState::analyserFloorDb, -90.0f);

    const float tilted = levelDb + tilt * std::log2 (juce::jmax (1.0f, frequencyHz) / 1000.0f);
    const float proportion = juce::jlimit (-0.2f, 1.2f, (tilted - floorDb) / (0.0f - floorDb));

    return area.getBottom() - proportion * area.getHeight();
}

//==============================================================================

void EQCurveComponent::paint (juce::Graphics& g)
{
    const auto area = getGraphArea();

    g.fillAll (MantaTheme::graphBackground());

    drawGrid (g);

    const auto uiState = processor.getUiState();
    const int mode = MantaEQUiState::getInt (uiState, MantaEQUiState::analyserMode, 3);

    if ((mode & 1) != 0)
        drawSpectrum (g, preSpectrum, MantaTheme::spectrumPre(), true);

    if ((mode & 2) != 0)
        drawSpectrum (g, postSpectrum, MantaTheme::spectrumPost(), false);

    drawCurves (g);
    drawHandles (g);

    if (MantaEQUiState::getBool (uiState, MantaEQUiState::showKeyboard, false))
        drawKeyboard (g);

    // 周波数の数字（いちばん下の帯）
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.setColour (MantaTheme::textDim());

    const auto labelStrip = getLocalBounds().toFloat().removeFromBottom ((float) labelStripHeight);

    for (const auto& entry : gridFrequencies)
    {
        if (! entry.labelled)
            continue;

        const float x = frequencyToX (entry.hz);

        // **両端は内側へ寄せる。** そのまま中央揃えで置くと、20Hzと20kHzが
        // 画面の外へはみ出して「0」「20」に見えます
        auto textArea = juce::Rectangle<float> (x - 20.0f, labelStrip.getY(), 40.0f, labelStrip.getHeight());
        textArea.setX (juce::jlimit (0.0f, (float) getWidth() - textArea.getWidth(), textArea.getX()));

        g.drawText (frequencyLabel (entry.hz), textArea, juce::Justification::centred, false);
    }

    g.setColour (MantaTheme::border());
    g.drawRect (area, 1.0f);
}

void EQCurveComponent::drawGrid (juce::Graphics& g) const
{
    const auto area = getGraphArea();

    for (const auto& entry : gridFrequencies)
    {
        const float x = frequencyToX (entry.hz);

        g.setColour (entry.labelled ? MantaTheme::gridStrong() : MantaTheme::grid());
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    // 横線は6dBごと。0dBだけ濃くする
    const float range = getCurveRangeDb();
    const float step = range > 20.0f ? 12.0f : 6.0f;

    for (float db = -range; db <= range + 0.01f; db += step)
    {
        const float y = gainToY (db);

        g.setColour (std::abs (db) < 0.01f ? MantaTheme::gridStrong() : MantaTheme::grid());
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());

        // **いちばん上と下には数字を出さない**（枠にかかって半分しか読めない）
        if (std::abs (db) > 0.01f && std::abs (db) < range - 0.01f)
        {
            g.setColour (MantaTheme::textDim().withAlpha (0.7f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (juce::String (db > 0.0f ? "+" : "") + juce::String ((int) db),
                         juce::Rectangle<float> (area.getRight() - 34.0f, y - 7.0f, 30.0f, 14.0f),
                         juce::Justification::centredRight, false);
        }
    }
}

void EQCurveComponent::drawSpectrum (juce::Graphics& g, const EQSpectrum& spectrum,
                                      juce::Colour colour, bool filled) const
{
    const auto area = getGraphArea();

    juce::Path path;
    bool started = false;

    for (int point = 0; point < EQSpectrum::numPoints; ++point)
    {
        const float frequency = spectrum.getFrequency (point);
        const float x = frequencyToX (frequency);
        const float y = juce::jlimit (area.getY() - 4.0f, area.getBottom() + 4.0f,
                                       analyserDbToY (spectrum.getMagnitudeDb (point), frequency));

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

    if (filled)
    {
        auto closed = path;
        closed.lineTo (area.getRight(), area.getBottom());
        closed.lineTo (area.getX(), area.getBottom());
        closed.closeSubPath();

        g.setColour (colour);
        g.fillPath (closed);
    }
    else
    {
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }
}

void EQCurveComponent::updateBandSnapshots()
{
    const double sampleRate = processor.getSampleRateForDisplay();

    // **音を出している側と同じ関数で係数を作る**（1.27）。
    // `getBandSettings()`はダイナミクスで動いているぶんも入っている
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        bandSettings[band] = processor.getBandSettings (band);
        bandSections[band] = EQFilterDesign::designBand (bandSettings[band], sampleRate);
    }
}

void EQCurveComponent::drawCurves (juce::Graphics& g)
{
    const auto area = getGraphArea();
    const double sampleRate = processor.getSampleRateForDisplay();

    juce::Path bandPaths[MantaEQParams::numBands];
    bool bandStarted[MantaEQParams::numBands] {};

    // Phase 207：**合計のカーブは2本**（本人の要望）。
    //
    // Stereo/Left/Right と Mid/Side を混ぜて1本にすると、
    // **M/Sで動かしたぶんが、そこに掛かっていないL/Rの線にも乗って見えます**
    // ——「Midだけ持ち上げたのに、全体が持ち上がったように見える」。
    //
    // 前に出すのはL/R側（パープル）。多くの場合こちらが主で、
    // M/Sは補助として使うためです。
    juce::Path stereoPath, midSidePath;
    bool stereoStarted = false, midSideStarted = false;

    bool hasStereoBand = false, hasMidSideBand = false;

    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        if (bandSections[band].numSections == 0 || ! bandSettings[band].active)
            continue;

        if (bandSettings[band].channel == Channel::mid || bandSettings[band].channel == Channel::side)
            hasMidSideBand = true;
        else
            hasStereoBand = true;
    }

    const int left = (int) area.getX();
    const int right = (int) area.getRight();

    // **2ピクセルおき**（このファイルの冒頭の「重さ」）
    for (int x = left; x <= right; x += 2)
    {
        const float frequency = xToFrequency ((float) x);
        double stereoDb = 0.0;
        double midSideDb = 0.0;

        for (int band = 0; band < MantaEQParams::numBands; ++band)
        {
            if (bandSections[band].numSections == 0)
                continue;

            const double magnitude = EQFilterDesign::magnitudeAt (bandSections[band], frequency, sampleRate);
            const double db = juce::Decibels::gainToDecibels (magnitude, -60.0);

            // **バイパスしたバンドは合計に入れない**（線は点線で残す）
            if (bandSettings[band].active)
            {
                const auto channel = bandSettings[band].channel;

                if (channel == Channel::mid || channel == Channel::side)
                    midSideDb += db;
                else
                    stereoDb += db;
            }

            const float y = gainToY ((float) db);

            if (! bandStarted[band])
            {
                bandPaths[band].startNewSubPath ((float) x, y);
                bandStarted[band] = true;
            }
            else
            {
                bandPaths[band].lineTo ((float) x, y);
            }
        }

        auto addPoint = [x] (juce::Path& path, bool& started, float y)
        {
            if (! started)
            {
                path.startNewSubPath ((float) x, y);
                started = true;
            }
            else
            {
                path.lineTo ((float) x, y);
            }
        };

        if (hasStereoBand)
            addPoint (stereoPath, stereoStarted, gainToY ((float) stereoDb));

        if (hasMidSideBand)
            addPoint (midSidePath, midSideStarted, gainToY ((float) midSideDb));
    }

    // バンドごとの線（細く、**ステレオ配置の色**で。Phase 206）
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        if (! bandStarted[band])
            continue;

        const auto colour = MantaTheme::bandColour ((int) bandSettings[band].channel);
        const bool isSelected = (band == selectedBand);

        if (! bandSettings[band].active)
        {
            // **バイパス中は点線。** 消さずに残すのは、「元がどうだったか」を
            // 見ながら聴き比べるためです（消したら周波数もQも戻せません）
            const float dashes[] { 4.0f, 4.0f };

            juce::Path dashed;
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, bandPaths[band], dashes,
                                                             juce::numElementsInArray (dashes));

            g.setColour (colour.withAlpha (isSelected ? 0.6f : 0.3f));
            g.fillPath (dashed);
            continue;
        }

        g.setColour (colour.withAlpha (isSelected ? 0.9f : 0.45f));
        g.strokePath (bandPaths[band], juce::PathStrokeType (isSelected ? 1.8f : 1.1f));
    }

    // 合計のカーブ。**0dBの線との間を薄く塗る**と、上げているのか下げているのかが
    // 一目で分かる。
    //
    // **描く順に意味があります**——後に描いたほうが前に出るので、
    // M/S（オレンジ）を先、L/R（パープル）を後
    auto drawTotal = [&] (const juce::Path& path, bool started, juce::Colour colour)
    {
        if (! started)
            return;

        auto filled = path;
        filled.lineTo (area.getRight(), gainToY (0.0f));
        filled.lineTo (area.getX(), gainToY (0.0f));
        filled.closeSubPath();

        g.setColour (colour.withAlpha (0.16f));
        g.fillPath (filled);

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.2f));
    };

    // **1本も無いほうは描きません。** 描くと0dBのところに
    // まっすぐな線が1本増えるだけで、目盛りと紛れます
    drawTotal (midSidePath, midSideStarted, MantaTheme::bandColour ((int) Channel::mid));
    drawTotal (stereoPath, stereoStarted, MantaTheme::bandColour ((int) Channel::stereo));
}

juce::Point<float> EQCurveComponent::getHandlePosition (int bandIndex) const
{
    const auto& settings = bandSettings[bandIndex];

    // **つまみは「つまみの値」の位置に置く。**
    // ダイナミクスで動いているぶんまで入れると、掴んでいる最中に
    // つまみが指の下から逃げます（線のほうは動いて構いません）
    const float gainDb = MantaEQParams::shapeUsesGain (settings.shape) ? settings.gainDb : 0.0f;

    return { frequencyToX (settings.frequency), gainToY (gainDb) };
}

juce::String EQCurveComponent::getChannelFlagText (Channel channel)
{
    switch (channel)
    {
        case Channel::left:  return "L";
        case Channel::right: return "R";
        case Channel::mid:   return "M";
        case Channel::side:  return "S";
        default: break;
    }

    return {};
}

bool EQCurveComponent::isChannelFlagOnLeft (Channel channel)
{
    // **耳の位置と同じ並び。** LeftとMidが左、RightとSideが右
    return channel == Channel::left || channel == Channel::mid;
}

juce::Point<float> EQCurveComponent::getHandleButtonCentre (int bandIndex, HandleButton button) const
{
    const auto centre = getHandlePosition (bandIndex);
    const float radius = MantaTheme::bandHandleRadius + 2.0f;

    // Phase 207：つまみの**上**に、**Sをつまみの真上**にして右へ並べる（本人の指定）。
    //
    // ```
    //  S D ×
    // ①
    // ```
    //
    // 真ん中をDにしていたPhase 206までは、**いちばんよく押すS**が
    // つまみの中心から左へずれていました
    const float y = centre.y - radius - handleButtonRadius - 3.0f;

    switch (button)
    {
        case HandleButton::solo:    return { centre.x, y };
        case HandleButton::dynamic: return { centre.x + handleButtonSpacing, y };
        case HandleButton::remove:  return { centre.x + handleButtonSpacing * 2.0f, y };
        default: break;
    }

    return centre;
}

EQCurveComponent::HandleButton EQCurveComponent::findHandleButtonAt (juce::Point<float> position) const
{
    // **選んでいるバンドのものだけ。** 出ていないボタンは押せてはいけない
    if (! juce::isPositiveAndBelow (selectedBand, MantaEQParams::numBands)
         || ! bandSettings[selectedBand].enabled)
        return HandleButton::none;

    for (auto button : { HandleButton::solo, HandleButton::dynamic, HandleButton::remove })
        if (position.getDistanceFrom (getHandleButtonCentre (selectedBand, button)) <= handleButtonRadius + 2.0f)
            return button;

    return HandleButton::none;
}

void EQCurveComponent::drawHandles (juce::Graphics& g) const
{
    const int soloed = processor.getSoloedBand();

    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        const auto& settings = bandSettings[band];

        if (! settings.enabled)
            continue;

        const auto centre = getHandlePosition (band);
        const auto colour = MantaTheme::bandColour ((int) settings.channel);

        const bool isSelected = (band == selectedBand);
        const float radius = MantaTheme::bandHandleRadius + (isSelected ? 2.0f : 0.0f);

        // バイパス中は**中身を抜く**（枠だけ残す）。番号は読めるままにしておく
        const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

        if (settings.active)
        {
            g.setColour (colour.withAlpha (0.9f));
            g.fillEllipse (bounds);

            g.setColour (MantaTheme::graphBackground().withAlpha (0.85f));
            g.drawEllipse (bounds, 1.5f);

            g.setColour (MantaTheme::graphBackground());
        }
        else
        {
            g.setColour (MantaTheme::graphBackground().withAlpha (0.8f));
            g.fillEllipse (bounds);

            g.setColour (colour.withAlpha (0.65f));
            g.drawEllipse (bounds.reduced (0.75f), 1.5f);
        }

        g.setFont (juce::Font (juce::FontOptions (isSelected ? 11.0f : 10.0f, juce::Font::bold)));
        g.drawText (juce::String (band + 1), bounds, juce::Justification::centred, false);

        if (band == soloed)
        {
            g.setColour (MantaTheme::accent());
            g.drawEllipse (bounds.expanded (3.0f), 2.0f);
        }

        //----------------------------------------------------------------------
        // ダイナミクスが動いているぶんは、つまみから伸びる短い線で見せる
        if (settings.active && settings.dynamicEnabled
             && std::abs (settings.dynamicOffsetDb) > 0.1f
             && MantaEQParams::shapeUsesGain (settings.shape))
        {
            const float dynamicY = gainToY (settings.effectiveGainDb());

            g.setColour (colour.withAlpha (0.8f));
            g.drawLine (centre.x, centre.y, centre.x, dynamicY, 2.0f);
            g.fillEllipse (centre.x - 3.0f, dynamicY - 3.0f, 6.0f, 6.0f);
        }

        if (! isSelected)
            continue;

        //----------------------------------------------------------------------
        // Phase 206：旗（L/R/M/S）。**つまみに接して**置く
        const auto flag = getChannelFlagText (settings.channel);

        if (flag.isNotEmpty())
        {
            const float flagWidth = 13.0f;
            const float flagHeight = 14.0f;

            const float flagX = isChannelFlagOnLeft (settings.channel)
                                  ? centre.x - radius - flagWidth
                                  : centre.x + radius;

            const juce::Rectangle<float> flagArea (flagX, centre.y - flagHeight * 0.5f, flagWidth, flagHeight);

            g.setColour (colour);
            g.fillRoundedRectangle (flagArea, 2.0f);

            g.setColour (MantaTheme::graphBackground());
            g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
            g.drawText (flag, flagArea, juce::Justification::centred, false);
        }

        //----------------------------------------------------------------------
        // Phase 206：つまみの上の小さなボタン（ソロ／ダイナミクス／削除）
        struct ButtonLook { HandleButton button; juce::String text; bool isOn; };

        // **×は`charToString()`で作ること。** 非ASCIIのリテラルを
        // `juce::String`へ直に渡すと止まります（`Utf8.h`）。
        // 画面に出る文字ですが記号なので、訳の表には載せません
        const auto crossMark = juce::String::charToString ((juce::juce_wchar) 0x00d7);

        const ButtonLook buttons[]
        {
            { HandleButton::solo,    "S",       band == soloed },
            { HandleButton::dynamic, "D",       settings.dynamicEnabled },
            { HandleButton::remove,  crossMark, false },
        };

        for (const auto& look : buttons)
        {
            const auto buttonCentre = getHandleButtonCentre (band, look.button);
            const auto buttonBounds = juce::Rectangle<float> (handleButtonRadius * 2.0f,
                                                               handleButtonRadius * 2.0f)
                                        .withCentre (buttonCentre);

            // 削除だけはオレンジ（**押すと戻せない**ので、他と同じ色にしない）
            const auto buttonColour = look.button == HandleButton::remove ? AppColours::orange : colour;

            g.setColour (look.isOn ? buttonColour : MantaTheme::graphBackground().withAlpha (0.92f));
            g.fillEllipse (buttonBounds);

            g.setColour (buttonColour.withAlpha (look.isOn ? 1.0f : 0.8f));
            g.drawEllipse (buttonBounds.reduced (0.5f), 1.2f);

            g.setColour (look.isOn ? MantaTheme::graphBackground() : buttonColour);
            g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
            g.drawText (look.text, buttonBounds, juce::Justification::centred, false);
        }
    }
}

void EQCurveComponent::drawKeyboard (juce::Graphics& g) const
{
    auto strip = getLocalBounds().toFloat();
    strip.removeFromBottom ((float) labelStripHeight);
    strip = strip.removeFromBottom ((float) keyboardHeight);

    g.setColour (AppColours::pianoWhiteKey);
    g.fillRect (strip);

    // 半音1つぶんの幅は場所によって変わる（横は対数目盛りなので）
    for (int note = 12; note <= 127; ++note)
    {
        const float frequency = noteToFrequency (note);

        if (frequency < MantaEQParams::minFrequency || frequency > MantaEQParams::maxFrequency)
            continue;

        const float lowX  = frequencyToX (frequency * 0.97153f);   // -半音の半分
        const float highX = frequencyToX (frequency * 1.02930f);   // +半音の半分

        if (isBlackKey (note))
        {
            g.setColour (AppColours::pianoBlackKey);
            g.fillRect (lowX, strip.getY(), juce::jmax (1.0f, highX - lowX), strip.getHeight());
        }
        else if (note % 12 == 0)
        {
            // Cのところに区切りと音名。**狭いところでは出さない**（重なって読めない）
            g.setColour (AppColours::pianoBlackKey.withAlpha (0.35f));
            g.drawVerticalLine ((int) lowX, strip.getY(), strip.getBottom());

            if (highX - lowX > 16.0f)
            {
                g.setColour (AppColours::pianoBlackKey);
                g.setFont (juce::Font (juce::FontOptions (9.0f)));
                g.drawText ("C" + juce::String (note / 12 - 1),
                             juce::Rectangle<float> (lowX, strip.getY(), highX - lowX, strip.getHeight()),
                             juce::Justification::centred, false);
            }
        }
    }

    g.setColour (MantaTheme::border());
    g.drawRect (strip, 1.0f);
}

//==============================================================================

int EQCurveComponent::findBandAt (juce::Point<float> position) const
{
    int found = -1;
    float bestDistance = MantaTheme::bandHandleRadius + 6.0f;

    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        if (! bandSettings[band].enabled)
            continue;

        const float distance = position.getDistanceFrom (getHandlePosition (band));

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            found = band;
        }
    }

    return found;
}

void EQCurveComponent::setSelectedBand (int bandIndex)
{
    const int limited = juce::isPositiveAndBelow (bandIndex, MantaEQParams::numBands) ? bandIndex : -1;

    if (limited == selectedBand)
        return;

    selectedBand = limited;

    auto uiState = processor.getUiState();
    uiState.setProperty (MantaEQUiState::selectedBand, selectedBand, nullptr);

    if (onBandSelected != nullptr)
        onBandSelected (selectedBand);

    repaint();
}

//==============================================================================

juce::RangedAudioParameter* EQCurveComponent::getParameter (int bandIndex, const char* suffix) const
{
    return processor.getValueTreeState().getParameter (MantaEQParams::bandParamId (bandIndex, suffix));
}

void EQCurveComponent::setParameter (int bandIndex, const char* suffix, float value)
{
    if (auto* parameter = getParameter (bandIndex, suffix))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void EQCurveComponent::beginGesture (int bandIndex, const char* suffix)
{
    if (auto* parameter = getParameter (bandIndex, suffix))
        parameter->beginChangeGesture();
}

void EQCurveComponent::endGesture (int bandIndex, const char* suffix)
{
    if (auto* parameter = getParameter (bandIndex, suffix))
        parameter->endChangeGesture();
}

//==============================================================================

void EQCurveComponent::applyHandleButton (int bandIndex, HandleButton button)
{
    switch (button)
    {
        case HandleButton::solo:
            processor.setSoloedBand (processor.getSoloedBand() == bandIndex ? -1 : bandIndex);
            break;

        case HandleButton::dynamic:
            setParameter (bandIndex, MantaEQParams::bandDynOn,
                           processor.getBandSettings (bandIndex).dynamicEnabled ? 0.0f : 1.0f);
            break;

        case HandleButton::remove:
            setParameter (bandIndex, MantaEQParams::bandEnabled, 0.0f);
            setSelectedBand (-1);
            break;

        default:
            return;
    }

    if (onBandsChanged != nullptr)
        onBandsChanged();

    updateBandSnapshots();
    repaint();
}

void EQCurveComponent::mouseDown (const juce::MouseEvent& event)
{
    // **ボタンの判定が先。** つまみの当たり判定と重なることがあり、
    // 後にすると「ボタンを押したのにドラッグが始まる」ことになります
    const auto handleButton = findHandleButtonAt (event.position);

    if (handleButton != HandleButton::none && ! event.mods.isPopupMenu())
    {
        applyHandleButton (selectedBand, handleButton);
        return;
    }

    const int band = findBandAt (event.position);

    if (event.mods.isPopupMenu())
    {
        if (band >= 0)
        {
            setSelectedBand (band);
            showBandMenu (band);
        }
        else
        {
            showBackgroundMenu();
        }

        return;
    }

    if (band < 0)
        return;

    setSelectedBand (band);

    draggingBand = band;
    draggingFrequencyOnly = false;
    draggingGainOnly = false;

    beginGesture (band, MantaEQParams::bandFreq);
    beginGesture (band, MantaEQParams::bandGain);
}

void EQCurveComponent::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingBand < 0)
        return;

    // Shiftを押しながらのときは**動きの大きいほうだけ**にする。
    // 「周波数はそのままでGainだけ」が、いちばんよく要る操作
    if (event.mods.isShiftDown() && ! draggingFrequencyOnly && ! draggingGainOnly)
    {
        const auto offset = event.getOffsetFromDragStart();

        if (std::abs (offset.x) > 4 || std::abs (offset.y) > 4)
        {
            draggingFrequencyOnly = std::abs (offset.x) > std::abs (offset.y);
            draggingGainOnly = ! draggingFrequencyOnly;
        }
    }

    if (! draggingGainOnly)
        setParameter (draggingBand, MantaEQParams::bandFreq,
                       juce::jlimit (MantaEQParams::minFrequency, MantaEQParams::maxFrequency,
                                      xToFrequency (event.position.x)));

    if (! draggingFrequencyOnly
         && MantaEQParams::shapeUsesGain (bandSettings[draggingBand].shape))
    {
        setParameter (draggingBand, MantaEQParams::bandGain, yToGain (event.position.y));
    }
}

void EQCurveComponent::mouseUp (const juce::MouseEvent&)
{
    if (draggingBand < 0)
        return;

    endGesture (draggingBand, MantaEQParams::bandFreq);
    endGesture (draggingBand, MantaEQParams::bandGain);

    draggingBand = -1;
}

void EQCurveComponent::mouseDoubleClick (const juce::MouseEvent& event)
{
    const int band = findBandAt (event.position);

    if (findHandleButtonAt (event.position) != HandleButton::none)
        return;   // ボタンの上での2回目のクリック。バンドを消さない

    if (band >= 0)
    {
        // 仕様書4.1：**つまみのダブルクリックで消す**
        setParameter (band, MantaEQParams::bandEnabled, 0.0f);
        setSelectedBand (-1);
    }
    else
    {
        const float frequency = juce::jlimit (MantaEQParams::minFrequency, MantaEQParams::maxFrequency,
                                               xToFrequency (event.position.x));
        const int created = processor.enableFreeBand (frequency, yToGain (event.position.y));

        if (created >= 0)
            setSelectedBand (created);
    }

    if (onBandsChanged != nullptr)
        onBandsChanged();

    repaint();
}

void EQCurveComponent::mouseMove (const juce::MouseEvent& event)
{
    const int band = findBandAt (event.position);

    if (band == hoveredBand)
        return;

    hoveredBand = band;
    setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor
                               : juce::MouseCursor::NormalCursor);
}

void EQCurveComponent::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
}

void EQCurveComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    // 仕様書4.1：**ホイールでQ。** 指しているバンドが優先、無ければ選択中のもの
    const int band = hoveredBand >= 0 ? hoveredBand : selectedBand;

    if (! juce::isPositiveAndBelow (band, MantaEQParams::numBands))
        return;

    const auto settings = processor.getBandSettings (band);

    if (! settings.enabled)
        return;

    const float factor = std::pow (2.0f, wheel.deltaY * 2.0f);

    setParameter (band, MantaEQParams::bandQ,
                   juce::jlimit (MantaEQParams::minQ, MantaEQParams::maxQ, settings.q * factor));
}

//==============================================================================

void EQCurveComponent::showBandMenu (int bandIndex)
{
    const auto settings = processor.getBandSettings (bandIndex);

    juce::PopupMenu shapeMenu;
    const auto shapeNames = MantaEQParams::getShapeNames();

    for (int i = 0; i < shapeNames.size(); ++i)
        shapeMenu.addItem (shapeBase + i, shapeNames[i], true, (int) settings.shape == i);

    juce::PopupMenu slopeMenu;
    const auto slopeNames = MantaEQParams::getSlopeNames();

    for (int i = 0; i < slopeNames.size(); ++i)
        slopeMenu.addItem (slopeBase + i, slopeNames[i], true,
                            MantaEQParams::slopeChoicesDbPerOctave[i] == settings.slopeDbPerOctave);

    juce::PopupMenu channelMenu;
    const auto channelNames = MantaEQParams::getChannelNames();

    for (int i = 0; i < channelNames.size(); ++i)
        channelMenu.addItem (channelBase + i, channelNames[i], true, (int) settings.channel == i);

    juce::PopupMenu menu;
    menu.addSectionHeader ("Band " + juce::String (bandIndex + 1));

    // Phase 207：**メニューの中でそのまま打ち込む**（本人の要望。`ParameterEntryItem`）。
    // つまみを目で合わせなくても、「1000Hzちょうど」が入れられます
    juce::Component::SafePointer<EQCurveComponent> safeThis (this);

    auto onCommitted = [safeThis]
    {
        if (safeThis == nullptr)
            return;

        safeThis->updateBandSnapshots();
        safeThis->repaint();
    };

    // **いちばん上（Freq）にだけフォーカスを渡す。** 3つとも取りに行くと、
    // あとから取ったものが勝って、下の欄が選ばれた状態で開きます
    bool grabFocus = true;

    auto addEntry = [&] (const char* suffix, const juce::String& captionText, bool shouldShow)
    {
        if (! shouldShow)
            return;

        if (auto* parameter = getParameter (bandIndex, suffix))
        {
            menu.addCustomItem (enterFrequency,
                                 std::make_unique<ParameterEntryItem> (captionText, *parameter,
                                                                        grabFocus, onCommitted));
            grabFocus = false;
        }
    };

    addEntry (MantaEQParams::bandFreq, "Freq", true);
    addEntry (MantaEQParams::bandGain, "Gain", MantaEQParams::shapeUsesGain (settings.shape));
    addEntry (MantaEQParams::bandQ, "Q", true);

    menu.addSeparator();
    menu.addSubMenu (utf8 ("形状"), shapeMenu);
    menu.addSubMenu (utf8 ("スロープ"), slopeMenu, MantaEQParams::shapeUsesSlope (settings.shape));
    menu.addSubMenu (utf8 ("ステレオ配置"), channelMenu);
    menu.addSeparator();
    menu.addItem (toggleDynamic, utf8 ("ダイナミックEQ"), true, settings.dynamicEnabled);
    menu.addItem (toggleSolo, utf8 ("このバンドだけ聴く"), true, processor.getSoloedBand() == bandIndex);
    menu.addSeparator();

    // Phase 206：**バイパスと削除は別のもの**（`MantaEQParams::bandEnabled`の表）。
    // Phase 205までは「オフにする」＝削除だったので、聴き比べができませんでした
    menu.addItem (toggleBypass, utf8 ("バイパス"), true, ! settings.active);
    menu.addItem (removeBand, utf8 ("このバンドを削除"));

    // Phase 206：**つまみのすぐ横に出す**（本人の要望）。
    // 部品の真ん中に出ると、12本目のバンドを触っているのに
    // メニューは画面の中央、ということになります
    const auto handle = getHandlePosition (bandIndex);
    const auto screenPoint = localPointToGlobal (handle.toInt());

    // **`withDeletionCheck()`を付けること**（Phase 207）。
    // メニューの中に入力欄を置いたので、開いたまま**プラグインの窓が閉じられ得ます**
    // ——付けておけば、消えた後のコールバックは走りません（1.32）
    menu.showMenuAsync (juce::PopupMenu::Options()
                           .withTargetComponent (this)
                           .withTargetScreenArea ({ screenPoint.x, screenPoint.y, 1, 1 })
                           .withDeletionCheck (*this),
                         [this, bandIndex] (int result)
                         {
                             if (result == 0)
                                 return;

                             if (result >= shapeBase && result < shapeBase + (int) Shape::numShapes)
                             {
                                 setParameter (bandIndex, MantaEQParams::bandShape,
                                                (float) (result - shapeBase));
                             }
                             else if (result >= slopeBase && result < slopeBase + MantaEQParams::numSlopeChoices)
                             {
                                 setParameter (bandIndex, MantaEQParams::bandSlope,
                                                (float) (result - slopeBase));
                             }
                             else if (result >= channelBase && result < channelBase + (int) Channel::numChannels)
                             {
                                 setParameter (bandIndex, MantaEQParams::bandChannel,
                                                (float) (result - channelBase));
                             }
                             else if (result == toggleDynamic)
                             {
                                 const auto current = processor.getBandSettings (bandIndex);
                                 setParameter (bandIndex, MantaEQParams::bandDynOn,
                                                current.dynamicEnabled ? 0.0f : 1.0f);
                             }
                             else if (result == toggleSolo)
                             {
                                 processor.setSoloedBand (processor.getSoloedBand() == bandIndex
                                                            ? -1 : bandIndex);
                             }
                             else if (result == toggleBypass)
                             {
                                 setParameter (bandIndex, MantaEQParams::bandActive,
                                                processor.getBandSettings (bandIndex).active ? 0.0f : 1.0f);
                             }
                             else if (result == removeBand)
                             {
                                 setParameter (bandIndex, MantaEQParams::bandEnabled, 0.0f);
                                 setSelectedBand (-1);
                             }
                             else if (result == enterFrequency)
                             {
                                 // 打ち込みは`ParameterEntryItem`の中で済んでいます。
                                 // ここへ来るのはEnterで閉じたときだけ
                                 updateBandSnapshots();
                             }

                             if (onBandsChanged != nullptr)
                                 onBandsChanged();

                             repaint();
                         });
}


void EQCurveComponent::showBackgroundMenu()
{
    const auto uiState = processor.getUiState();
    const float currentRange = getCurveRangeDb();

    juce::PopupMenu rangeMenu;

    for (int i = 0; i < (int) (sizeof (curveRangeChoices) / sizeof (float)); ++i)
        rangeMenu.addItem (curveRangeBase + i,
                            juce::String::fromUTF8 ("\xc2\xb1") + juce::String ((int) curveRangeChoices[i]) + " dB",
                            true, std::abs (currentRange - curveRangeChoices[i]) < 0.01f);

    juce::PopupMenu menu;
    menu.addSubMenu (utf8 ("縦軸の幅"), rangeMenu);
    menu.addItem (toggleKeyboard, utf8 ("鍵盤を表示"), true,
                   MantaEQUiState::getBool (uiState, MantaEQUiState::showKeyboard, false));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this] (int result)
                         {
                             if (result == 0)
                                 return;

                             auto state = processor.getUiState();

                             if (result >= curveRangeBase
                                  && result < curveRangeBase + (int) (sizeof (curveRangeChoices) / sizeof (float)))
                             {
                                 state.setProperty (MantaEQUiState::curveRangeDb,
                                                     curveRangeChoices[result - curveRangeBase], nullptr);
                             }
                             else if (result == toggleKeyboard)
                             {
                                 const bool shown = MantaEQUiState::getBool (state, MantaEQUiState::showKeyboard, false);
                                 state.setProperty (MantaEQUiState::showKeyboard, ! shown, nullptr);
                             }

                             repaint();
                         });
}
