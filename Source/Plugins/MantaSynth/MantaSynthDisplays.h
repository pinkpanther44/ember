#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../MantaTheme.h"

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Manta Synthesizer の「見るだけ」の部品4つ（Phase 213）。

    EAGLE type0の`EnvelopeDisplay.h`・`WaveformDisplay.h`・`SegmentMeter.h`・
    `Oscilloscope.h`を1つにまとめたものです。**どれも数十行**で、
    ファイルを4つに分けると「どこにあるか」を探すほうが手間になります。

    色は`MantaTheme`（＝本体の`AppColours`）から引いています。
    向こうのシアンはここでは**パープル**です（本人の指定）。

    | 部品 | 何を出すか |
    |---|---|
    | `SynthEnvelopeDisplay` | A/D/S/Rつまみの形。**音と同じ指数カーブ**で描く |
    | `SynthWaveformDisplay` | 選んでいる波形（Saw/Square/Triangle） |
    | `SynthLevelMeter` | 出力レベル。段組みのLED風 |
    | `SynthOscilloscope` | 出ている波形。ゼロ交差で止めて見せる |

    **どれもマウスを通します**（`setInterceptsMouseClicks(false, false)`）。
    見るだけのものが操作を横取りすると、下にあるものが押せなくなります。
*/

//==============================================================================
/** ADSRの形。**APVTSを見張って、つまみが動いたら描き直します。** */
class SynthEnvelopeDisplay : public juce::Component,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    SynthEnvelopeDisplay (juce::AudioProcessorValueTreeState& state,
                           const juce::String& attackId, const juce::String& decayId,
                           const juce::String& sustainId, const juce::String& releaseId,
                           juce::Colour lineColour)
        : apvts (state), attackID (attackId), decayID (decayId),
          sustainID (sustainId), releaseID (releaseId), colour (lineColour)
    {
        for (const auto& id : { attackID, decayID, sustainID, releaseID })
            apvts.addParameterListener (id, this);

        setInterceptsMouseClicks (false, false);
    }

    ~SynthEnvelopeDisplay() override
    {
        for (const auto& id : { attackID, decayID, sustainID, releaseID })
            apvts.removeParameterListener (id, this);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (2.0f);

        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const float a = apvts.getRawParameterValue (attackID)->load();
        const float d = apvts.getRawParameterValue (decayID)->load();
        const float s = apvts.getRawParameterValue (sustainID)->load();
        const float r = apvts.getRawParameterValue (releaseID)->load();

        // 時間の配分。**平方根で潰してある**——秒のまま横幅に割ると、
        // 3秒のReleaseを入れた瞬間にAttackが1pxになって見えなくなります
        auto timeScale = [] (float t) { return std::sqrt (std::clamp (t, 0.0f, 5.0f)); };

        const float ta = timeScale (a), td = timeScale (d), tr = timeScale (r);
        const float sustainHold = 0.6f;                 // 押しっぱなしの区間（見た目だけ）
        float total = ta + td + sustainHold + tr;
        if (total < 0.001f) total = 1.0f;

        const float x0 = bounds.getX() + 4.0f;
        const float x1 = bounds.getRight() - 4.0f;
        const float y0 = bounds.getBottom() - 4.0f;     // レベル0
        const float y1 = bounds.getY() + 4.0f;          // レベル1
        const float width = x1 - x0;

        auto tx = [&] (float cumulative) { return x0 + (cumulative / total) * width; };
        auto ly = [&] (float level) { return y0 + (y1 - y0) * std::clamp (level, 0.0f, 1.0f); };

        const float xAttack  = tx (ta);
        const float xDecay   = tx (ta + td);
        const float xSustain = tx (ta + td + sustainHold);
        const float xRelease = tx (ta + td + sustainHold + tr);

        juce::Path path;
        path.startNewSubPath (x0, y0);
        path.lineTo (xAttack, y1);                      // アタックは直線

        // **音と同じ指数カーブ**で描くこと（`MantaSynthDSP::ADSR`のDecay/Release）。
        // 直線で描くと、聴こえている減り方と絵が食い違います
        const int steps = 24;

        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            const float curve = std::pow (1.0f - t, 2.2f);
            path.lineTo (xAttack + (xDecay - xAttack) * t, ly (s + (1.0f - s) * curve));
        }

        path.lineTo (xSustain, ly (s));

        for (int i = 1; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            const float curve = std::pow (1.0f - t, 2.2f);
            path.lineTo (xSustain + (xRelease - xSustain) * t, ly (s * curve));
        }

        juce::Path fill = path;
        fill.lineTo (xRelease, y0);
        fill.lineTo (x0, y0);
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.15f));
        g.fillPath (fill);

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (MantaTheme::grid());
        for (float gx : { xAttack, xDecay, xSustain })
            g.drawVerticalLine ((int) gx, y1, y0);

        // 折れ目に点を打つ。**どのつまみがどこを動かしているか**が読めます
        g.setColour (colour.brighter (0.3f));
        auto dot = [&] (float px, float py) { g.fillEllipse (px - 2.5f, py - 2.5f, 5.0f, 5.0f); };
        dot (xAttack, y1);
        dot (xDecay, ly (s));
        dot (xSustain, ly (s));
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        // **音のスレッドから呼ばれます。** 描き直しはメッセージスレッドへ渡すこと
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<SynthEnvelopeDisplay> (this)]
            {
                if (safe != nullptr) safe->repaint();
            });
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String attackID, decayID, sustainID, releaseID;
    juce::Colour colour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthEnvelopeDisplay)
};

//==============================================================================
/** 選んでいる波形のかたち（2周期ぶん）。 */
class SynthWaveformDisplay : public juce::Component,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    SynthWaveformDisplay (juce::AudioProcessorValueTreeState& state, const juce::String& waveParamId)
        : apvts (state), paramID (waveParamId)
    {
        apvts.addParameterListener (paramID, this);
        setInterceptsMouseClicks (false, false);
    }

    ~SynthWaveformDisplay() override { apvts.removeParameterListener (paramID, this); }

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds();
        auto labelArea = full.removeFromTop (14);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText ("WAVEFORM", labelArea, juce::Justification::centred);

        auto box = full.toFloat().reduced (1.0f);
        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        const float midY = box.getCentreY();
        g.setColour (MantaTheme::grid());
        g.drawHorizontalLine ((int) midY, box.getX() + 4.0f, box.getRight() - 4.0f);

        const int wave = (int) apvts.getRawParameterValue (paramID)->load();
        const float x0 = box.getX() + 6.0f;
        const float x1 = box.getRight() - 6.0f;
        const float amplitude = box.getHeight() * 0.32f;
        const float width = x1 - x0;

        juce::Path path;
        const int steps = 160;
        const float cycles = 2.0f;

        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            const float phase = std::fmod (t * cycles, 1.0f);
            float value = 0.0f;

            switch (wave)
            {
                case 0: value = 2.0f * phase - 1.0f; break;                        // Saw
                case 1: value = (phase < 0.5f) ? 1.0f : -1.0f; break;              // Square
                case 2: value = (phase < 0.5f) ? (4.0f * phase - 1.0f)
                                                : (3.0f - 4.0f * phase); break;    // Triangle
                default: break;
            }

            const float px = x0 + width * t;
            const float py = midY - value * amplitude;

            if (i == 0) path.startNewSubPath (px, py);
            else        path.lineTo (px, py);
        }

        g.setColour (MantaTheme::accent());
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

private:
    void parameterChanged (const juce::String&, float) override
    {
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<SynthWaveformDisplay> (this)]
            {
                if (safe != nullptr) safe->repaint();
            });
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String paramID;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthWaveformDisplay)
};

//==============================================================================
/**
    段組みのステレオ・レベルメーター。

    **色は3段階**です——普段はパープル、-6dBあたりからオレンジ、
    上の2段は赤。「もう上げられない」が色で分かります。
*/
class SynthLevelMeter : public juce::Component,
                         private juce::Timer
{
public:
    /** `getLevel(channel)`は0..1のピークを返すもの。 */
    explicit SynthLevelMeter (std::function<float (int)> levelSource)
        : getLevel (std::move (levelSource))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~SynthLevelMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto scaleRow = area.removeFromBottom (11);

        auto rowL = area.removeFromTop (area.getHeight() / 2 - 2);
        area.removeFromTop (4);
        auto rowR = area;

        auto labelL = rowL.removeFromLeft (12);
        auto labelR = rowR.removeFromLeft (12);

        drawBar (g, rowL, dbL);
        drawBar (g, rowR, dbR);

        g.setColour (MantaTheme::textDim());
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText ("L", labelL, juce::Justification::centredLeft);
        g.drawText ("R", labelR, juce::Justification::centredLeft);

        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        const int marks[] { -48, -24, -12, -6, 0 };

        for (int mark : marks)
        {
            const float t = dbToNormalised ((float) mark);
            const int x = rowL.getX() + juce::roundToInt (t * rowL.getWidth());

            g.setColour (MantaTheme::textDim().withAlpha (0.75f));
            g.drawText (juce::String (mark), x - 14, scaleRow.getY(), 28, 11,
                         juce::Justification::centred);
        }
    }

private:
    static constexpr int numSegments = 22;

    /** -54dB〜+6dB を 0..1 にする。 */
    static float dbToNormalised (float db) { return juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 60.0f); }

    void drawBar (juce::Graphics& g, juce::Rectangle<int> row, float db) const
    {
        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (row.toFloat(), 3.0f);

        const float lit = dbToNormalised (db) * numSegments;
        const float segmentWidth = (float) row.getWidth() / numSegments;

        for (int i = 0; i < numSegments; ++i)
        {
            const float x = row.getX() + i * segmentWidth;
            const juce::Rectangle<float> segment (x + 1.0f, row.getY() + 2.0f,
                                                   segmentWidth - 2.0f, row.getHeight() - 4.0f);

            juce::Colour on;

            if (i >= numSegments - 2)      on = juce::Colour (0xffe5484d);   // 赤（超えている）
            else if (i >= numSegments - 6) on = MantaTheme::curve();          // オレンジ（そろそろ）
            else                           on = MantaTheme::accent();         // パープル（普段）

            g.setColour ((float) i < lit ? on : MantaTheme::grid());
            g.fillRoundedRectangle (segment, 1.0f);
        }
    }

    void timerCallback() override
    {
        auto toDb = [] (float linear)
        {
            return linear > 1.0e-5f ? juce::Decibels::gainToDecibels (linear) : -100.0f;
        };

        const float newL = toDb (getLevel (0));
        const float newR = toDb (getLevel (1));

        // **変わっていなければ描き直さない**（止まっているときにCPUを使わないため）
        if (std::abs (newL - dbL) > 0.1f || std::abs (newR - dbR) > 0.1f)
        {
            dbL = newL; dbR = newR;
            repaint();
        }
    }

    std::function<float (int)> getLevel;
    float dbL = -100.0f, dbR = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthLevelMeter)
};

//==============================================================================
/**
    オシロスコープ。

    **立ち上がりのゼロ交差でトリガを掛けています。** 掛けないと、
    描くたびに波形が横へ流れて、形がまったく読めません。
*/
class SynthOscilloscope : public juce::Component,
                           private juce::Timer
{
public:
    SynthOscilloscope (const float* buffer, std::function<int()> writePositionSource, int size)
        : source (buffer), getWritePosition (std::move (writePositionSource)), bufferSize (size)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ~SynthOscilloscope() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto box = getLocalBounds().toFloat().reduced (1.0f);

        g.setColour (MantaTheme::graphBackground());
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        const float midY = box.getCentreY();
        g.setColour (MantaTheme::grid());
        g.drawHorizontalLine ((int) midY, box.getX() + 3.0f, box.getRight() - 3.0f);

        if (source == nullptr || bufferSize <= 4) return;

        // 古い順に並べ替えて取り出す
        samples.resize ((size_t) bufferSize);
        const int writePosition = getWritePosition();

        for (int i = 0; i < bufferSize; ++i)
            samples[(size_t) i] = source[(writePosition + i) % bufferSize];

        // 立ち上がりのゼロ交差を探す（**前の4分の1の中だけ**）
        int start = 0;
        const int searchTo = bufferSize / 4;

        for (int i = 1; i < searchTo; ++i)
        {
            if (samples[(size_t) (i - 1)] < 0.0f && samples[(size_t) i] >= 0.0f) { start = i; break; }
        }

        // **トリガ位置に関係なく同じ長さを描く**（見える幅が変わらないように）
        const int drawLength = bufferSize - searchTo;
        const float x0 = box.getX() + 3.0f;
        const float x1 = box.getRight() - 3.0f;
        const float amplitude = box.getHeight() * 0.42f;

        juce::Path path;
        const int points = 220;

        for (int i = 0; i <= points; ++i)
        {
            int index = start + (int) ((float) i / points * (drawLength - 1));
            if (index >= bufferSize) index = bufferSize - 1;

            const float value = juce::jlimit (-1.0f, 1.0f, samples[(size_t) index]);
            const float px = x0 + (x1 - x0) * ((float) i / points);
            const float py = midY - value * amplitude;

            if (i == 0) path.startNewSubPath (px, py);
            else        path.lineTo (px, py);
        }

        g.setColour (MantaTheme::accent());
        g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

private:
    void timerCallback() override { repaint(); }

    const float* source;
    std::function<int()> getWritePosition;
    int bufferSize;

    /** `paint()`のたびに確保しないための入れ物。 */
    mutable std::vector<float> samples;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthOscilloscope)
};
