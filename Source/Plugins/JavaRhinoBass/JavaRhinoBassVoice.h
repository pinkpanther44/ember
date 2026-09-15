#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "JavaRhinoBassExcitation.h"
#include "JavaRhinoBassString.h"

//==============================================================================
/**
    8.257：**5弦ジャズベース（34インチ）の諸元**（Phase 265）。

    弦長をmmで持ち、**実効弦長＝スケール長 ×（開放弦周波数 / 実周波数）**として、
    ピックアップと撥弦位置をその比に直しています。
    ハイポジションほどPUが相対的に前寄りになる、という実機の音色変化が出ます。
*/
namespace BassSpec
{
    /** Fender Jazz Bass V 相当。**34インチ固定**。 */
    inline constexpr float scaleLengthMm  = 863.6f;
    inline constexpr float pickupNeckMm   = 205.0f;   ///< ブリッジからの距離（フロントPU）
    inline constexpr float pickupBridgeMm = 100.0f;   ///< ブリッジからの距離（リアPU）

    inline constexpr int numStrings = 5;

    /** B0, E1, A1, D2, G2（5弦レギュラー＝BEADG）。 */
    inline constexpr int openNotes[numStrings] = { 23, 28, 33, 38, 43 };

    inline constexpr int lowestNote  = 23;   ///< B0
    inline constexpr int highestNote = 67;   ///< G4（G弦24フレット）

    /** 弦ごとの個性：brightness係数／T60係数／出力レベル／絶対カットオフ上限。 */
    struct StringSpec { float brightMul, t60Mul, levelMul, ceilingHz; };

    inline const StringSpec& spec (int idx)
    {
        static const StringSpec s[numStrings] =
        {
            { 0.88f, 1.12f, 1.06f, 4200.0f },   // .130 B弦：太く暗く、長く鳴る
            { 0.93f, 1.07f, 1.02f, 5200.0f },   // .105 E弦
            { 1.00f, 1.02f, 1.00f, 6200.0f },   // .085 A弦
            { 1.06f, 0.97f, 0.98f, 7200.0f },   // .065 D弦
            { 1.12f, 0.92f, 0.96f, 8200.0f },   // .045 G弦：細く明るく、減衰は速い
        };

        return s[juce::jlimit (0, numStrings - 1, idx)];
    }

    /** 押さえる弦の推定：**音程以下でいちばん高い開放弦**。 */
    inline int chooseString (int midiNote)
    {
        int best = 0;

        for (int i = 0; i < numStrings; ++i)
            if (openNotes[i] <= midiNote)
                best = i;

        return best;
    }
}

//==============================================================================
struct BassVoiceParams
{
    float brightness = 0.62f;
    float sustain    = 9.0f;
    float pluckPos   = 0.22f;
    float hardness   = 0.60f;
    float attack     = 0.70f;
    float clank      = 0.50f;
    float blend      = 0.50f;
    int   style      = 0;      ///< `BassStyle`
};

//==============================================================================
class JavaRhinoBassSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int)    override { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
/**
    8.257：**1ボイス＝1本の弦**（Phase 265）。`JBass5`の`BassVoice`を移したものです。

    ### スラップのときだけ、弦で奏法が変わる

    `Slap`を選んでいても、**D弦・G弦は自動的に`Pop`（プル）**になります——
    実際の奏法どおり「低音弦は親指、高音弦は人差し指で引っ張る」を再現したものです。
    Popのほうが衝突非線形が強く出ます。

    ### 音程の更新は**64サンプルに1回**

    `updateFrequency()`はオールパス係数の二分探索（26反復）を含むので、
    毎サンプルは重すぎます。制御レートを落としても、
    グライドとビブラートの滑らかさには足ります。

    ### 鍵を離したら、**手でミュートする**

    T60を0.10秒へ落として150msでフェードします。実機で指を離すのと同じ挙動です。
*/
class JavaRhinoBassVoice : public juce::SynthesiserVoice
{
public:
    JavaRhinoBassVoice()
        : rng ((unsigned) juce::Random::getSystemRandom().nextInt())
    {
    }

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<JavaRhinoBassSound*> (s) != nullptr;
    }

    void setCurrentPlaybackSampleRate (double newRate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

        if (newRate > 0.0)
        {
            fs = newRate;
            str.prepare (fs);
            noteSmooth.reset (fs, 0.005);
        }
    }

    void setParams (const BassVoiceParams& p) { params = p; }

    int getCurrentNote() const noexcept { return currentNote; }

    //==========================================================================
    void startNote (int midiNote, float velocity,
                     juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        currentNote = midiNote;
        noteSmooth.setCurrentAndTargetValue ((double) midiNote);
        bendSemitones = (currentPitchWheelPosition - 8192) / 8192.0 * 2.0;

        str.reset();
        stringIdx = BassSpec::chooseString (midiNote);

        applyEffectiveTone (velocity);
        updateFrequency (true);

        buildExcitation (velocity, false);

        releasing = false;
        releaseGain = 1.0f;
        vibPhase = 0.0f;
    }

    /** レガート（ハンマリング／スライド）。**ディレイラインをリセットせず**音程だけ変える。 */
    void legatoTo (int newNote, double glideMs, float velocity)
    {
        currentNote = newNote;
        stringIdx = BassSpec::chooseString (newNote);

        noteSmooth.reset (fs, juce::jmax (0.001, glideMs / 1000.0));
        noteSmooth.setTargetValue ((double) newNote);

        applyEffectiveTone (velocity);
        buildExcitation (velocity * 0.35f, true);   // 小さな励起だけ注入

        releasing = false;
        releaseGain = 1.0f;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            // **指を離す＝弦を手でミュートする**（クラスの説明）
            releasing = true;
            str.setDecayT60 (0.10f);
            releaseStep = (float) (1.0 / (0.15 * fs));
        }
        else
        {
            clearCurrentNote();
            str.reset();
            currentNote = -1;
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        bendSemitones = (newValue - 8192) / 8192.0 * 2.0;
    }

    void controllerMoved (int number, int value) override
    {
        if (number == 1)                          // CC1＝ビブラート
            vibratoDepth = value / 127.0f;
        else if (number == 11 || number == 7)
            expression = 0.2f + 0.8f * (value / 127.0f);
    }

    //==========================================================================
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (currentNote < 0)
            return;

        auto* l = out.getWritePointer (0, startSample);
        auto* r = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            if (--controlCounter <= 0)
            {
                controlCounter = controlRate;
                updateFrequency (false);
            }

            const float e = (excIdx < exc.stringLength) ? exc.string[excIdx] : 0.0f;
            const float d = (excIdx < exc.directLength) ? exc.direct[excIdx] : 0.0f;
            ++excIdx;

            const float v = str.processSample (e);
            float y = str.dcBlock (str.pickupOutput (v)) + d;

            y *= levelMul * expression;

            if (releasing)
            {
                releaseGain -= releaseStep;

                if (releaseGain <= 0.0f)
                {
                    clearCurrentNote();
                    str.reset();
                    currentNote = -1;
                    return;
                }

                y *= releaseGain * releaseGain;
            }

            l[i] += y;

            if (r != nullptr)
                r[i] += y;
        }
    }

private:
    //==========================================================================
    static double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

    /** 奏法＋弦＋つまみから、ループフィルタと減衰を決める。 */
    void applyEffectiveTone (float velocity)
    {
        auto style = (BassStyle) juce::jlimit (0, (int) BassStyle::NumStyles - 1, params.style);

        // **スラップのとき、D/G弦は自動でポップ（プル）**（クラスの説明）
        if (style == BassStyle::Slap && stringIdx >= 3)
            style = BassStyle::Pop;

        effectiveStyle = style;

        const auto& st = getStyleTone (style);
        const auto& sp = BassSpec::spec (stringIdx);

        float b = params.brightness * st.brightMul * sp.brightMul;
        b += 0.10f * (velocity - 0.5f);              // 強く弾くほど明るい

        str.setBrightness (juce::jlimit (0.0f, 1.0f, b));
        str.setCutoffCeiling (sp.ceilingHz);

        const float t60 = (st.t60Fixed > 0.0f)
                            ? st.t60Fixed
                            : params.sustain * st.t60Mul * sp.t60Mul;

        str.setDecayT60 (t60);

        levelMul = st.levelMul * sp.levelMul;
    }

    void buildExcitation (float velocity, bool legato)
    {
        const auto& st = getStyleTone (effectiveStyle);

        ExcitationParams ep;
        ep.style         = effectiveStyle;
        ep.velocity      = juce::jlimit (0.05f, 1.0f, velocity);
        ep.hardness      = params.hardness;
        ep.attack        = legato ? 0.0f : params.attack;   // レガートにピック音は付けない
        ep.clank         = params.clank;
        ep.sampleRate    = fs;
        ep.periodSamples = str.getLoopDelay();
        ep.harmonicOrder = 2;
        ep.pluckRatio    = (st.pluckPos > 0.0f) ? st.pluckPos : params.pluckPos;

        generateExcitation (ep, rng, exc);
        excIdx = 0;

        if (! legato && exc.collisionAmount > 0.0f)
            str.triggerCollision (exc.collisionAmount, exc.collisionThreshold, exc.collisionDecay);
    }

    void updateFrequency (bool force)
    {
        double note = force ? noteSmooth.getTargetValue()
                            : noteSmooth.skip (controlRate);

        // ビブラート（**上方向バイアス**。ベースなので浅め）
        if (vibratoDepth > 0.001f)
        {
            vibPhase += (float) (2.0 * bassdsp::kPi * 5.0 * controlRate / fs);

            if (vibPhase > 2.0f * bassdsp::kPiF)
                vibPhase -= 2.0f * bassdsp::kPiF;

            const float lfo = 0.5f * (1.0f - std::cos (vibPhase));   // 0..1
            note += vibratoDepth * 0.35 * lfo;
        }

        note += bendSemitones;

        const double f = midiToHz (note);
        str.setFrequency (f);

        // **実効弦長**からPU／撥弦位置の比を出す
        const double openF = midiToHz ((double) BassSpec::openNotes[stringIdx]);
        const double effLenMm = BassSpec::scaleLengthMm * (openF / juce::jmax (1.0, f));

        const float rn = (float) juce::jlimit (0.02, 0.49, BassSpec::pickupNeckMm   / effLenMm);
        const float rb = (float) juce::jlimit (0.02, 0.49, BassSpec::pickupBridgeMm / effLenMm);

        str.setPickupRatios (rn, rb);
        str.setBlend (params.blend);
    }

    //==========================================================================
    JavaRhinoBassString str;
    JavaRhinoBassExcitationBuffers exc;

    /** **ボイスが1つずつ持ちます**（同じブロックで複数の声が同じ種から始まらないように）。 */
    std::mt19937 rng;

    BassVoiceParams params;
    BassStyle effectiveStyle = BassStyle::Finger;

    double fs = 48000.0;
    int currentNote = -1;
    int stringIdx = 0;
    int excIdx = 0;

    juce::SmoothedValue<double> noteSmooth { 0.0 };
    double bendSemitones = 0.0;

    float vibratoDepth = 0.0f, vibPhase = 0.0f;
    float expression = 1.0f;
    float levelMul = 1.0f;

    bool  releasing = false;
    float releaseGain = 1.0f, releaseStep = 0.0001f;

    /** 音程の更新は**64サンプルに1回**（クラスの説明）。 */
    static constexpr int controlRate = 64;
    int controlCounter = 1;
};
