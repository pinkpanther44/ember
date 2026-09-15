#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RaccoGuitarString.h"

//==============================================================================
/** 奏法。**番号は`RaccoGuitarProcessor`の`currentArticulation`に入ります**ので、
    並びを変えないこと（保存した状態と、画面のチップの並びが対です）。 */
enum class GuitarArticulation
{
    normal        = 0,
    palmMute      = 1,
    harmonic      = 2,
    slide         = 3,
    pinchHarmonic = 4,
    brushing      = 5    ///< ブラッシング（左手ミュートのカッティング。音程感なし）
};

/** 奏法の数（画面のチップと、キースイッチの表が両方これを見ます。1.27）。 */
inline constexpr int numGuitarArticulations = 6;

//==============================================================================
class RaccoGuitarSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
/**
    8.256：**1ボイス＝1本の弦**（Phase 264）。

    `KSGuitar`（別リポジトリ）の`StringVoice`を移したものです。
    **DSPは変えていません**——アンプ段だけ持って来ませんでした（本人の指定）。

    ### ピッキングのモデリング

    | | 何を表しているか |
    |---|---|
    | **Hardness** | 柔らかいピックは接触時間が長く初期変形が滑らか＝**丸い音**。硬いピックは瞬間解放で高次倍音が豊富＝**鋭い音**。励起ローパスの「基準カットオフ」で、**ベロシティはその周りを変調**する（2層） |
    | **Attack** | 弦振動の前に入る約2msの広帯域な「カチッ」。**「弾いた感」の正体**で、硬いピック・強いピッキングほど目立つ |

    **レガート（ハンマリング／プリング）にはピックノイズを付けません**——
    ピックを使っていないので。

    ### ビブラートは上にしか揺れない

    ギターの指ビブラートは**弦を曲げる**動作なので、音程は元の音から
    **上方向にしか動きません**。LFOを`(1 + sin) / 2`の形にしてあります
    （シンセの上下対称ビブラートとの違い）。
*/
class RaccoGuitarVoice : public juce::SynthesiserVoice
{
public:
    RaccoGuitarVoice()
    {
        excitation.resize (maxExcitationLength, 0.0f);
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<RaccoGuitarSound*> (sound) != nullptr;
    }

    bool isReleasing() const { return releasing; }

    //==========================================================================
    /** つまみの値。**ブロックの頭で毎回渡されます**。 */
    void setParameters (float newBrightness, float newSustainSeconds,
                        float newPickPos, float newPickupPos,
                        float newPickHardness, float newPickAttack,
                        GuitarArticulation newArticulation)
    {
        brightness     = newBrightness;
        sustainSeconds = newSustainSeconds;
        pickPos        = newPickPos;
        pickHardness   = newPickHardness;
        pickAttack     = newPickAttack;
        articulation   = newArticulation;

        // つまみの値（0.02〜0.5）は、ブリッジからのmm距離として解釈する。
        // スケール長648mmに対する比率なので、たとえば 0.115 ≒ 75mm（リアPU）
        string.setPickupPositionMm (newPickupPos * scaleLengthMm);
        string.setPickPositionMm   (newPickPos   * scaleLengthMm);

        if (! releasing && isVoiceActive())
            applyEffectiveTone();
    }

    //==========================================================================
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override
    {
        currentNoteSemis = targetNoteSemis = (float) midiNoteNumber;
        pitchBendSemitones = pitchWheelToSemitones (currentPitchWheelPosition);
        glideCoef = 1.0f;

        articulationAtStart = articulation;
        velocityAtStart     = juce::jlimit (0.0f, 1.0f, velocity);

        const float sr = (float) getSampleRate();
        vibratoPhaseInc   = juce::MathConstants<float>::twoPi * vibratoRateHz / sr;
        vibratoSmoothCoef = 1.0f - std::exp (-1.0f / (0.03f * sr));
        vibratoPhase = 0.0f;

        string.prepare (getSampleRate());
        string.setScaleLengthMm (scaleLengthMm);

        // **どの弦で押さえた音か**を決める（実効弦長の基準になる）
        string.setOpenStringFrequency (estimateOpenStringFreq (midiNoteNumber));

        applyEffectiveTone();
        updateFrequency();

        generateExcitation();

        releasing   = false;
        releaseGain = 1.0f;
        envelope    = 1.0f;
    }

    /** ハンマリング／プリング／スライド。**ピッキングし直さずに音程だけ変える**。 */
    void legatoTo (int newMidiNote, float strength, float glideTimeMs)
    {
        targetNoteSemis = (float) newMidiNote;

        const float tau = juce::jmax (1.0f, glideTimeMs) * 0.001f * 0.35f;
        glideCoef = 1.0f - std::exp (-1.0f / (tau * (float) getSampleRate()));

        injectLegatoExcitation (juce::jlimit (0.0f, 1.0f, strength));

        envelope = juce::jmax (envelope, 0.5f);
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            // 「押弦の指を離してミュートした」質感。**音程によらず一定時間**
            releasing = true;
            string.setDecaySeconds (0.25f);
            string.setBrightness (juce::jmin (brightness, 0.2f));
        }
        else
        {
            clearCurrentNote();
            string.reset();
        }
    }

    void pitchWheelMoved (int newPitchWheelValue) override
    {
        pitchBendSemitones = pitchWheelToSemitones (newPitchWheelValue);
        updateFrequency();
    }

    void controllerMoved (int controllerNumber, int newValue) override
    {
        if (controllerNumber == 1)   // CC1 ＝ モジュレーションホイール
            vibratoTarget = ((float) newValue / 127.0f) * maxVibratoSemis;
    }

    //==========================================================================
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override
    {
        if (! isVoiceActive())
            return;

        const float releaseCoef = getReleaseCoef();

        for (int i = 0; i < numSamples; ++i)
        {
            //--- ピッチ（グライド＋ビブラート） ------------------------------
            bool needFreqUpdate = false;

            if (currentNoteSemis != targetNoteSemis)
            {
                currentNoteSemis += (targetNoteSemis - currentNoteSemis) * glideCoef;

                if (std::abs (targetNoteSemis - currentNoteSemis) < 0.001f)
                    currentNoteSemis = targetNoteSemis;

                needFreqUpdate = true;
            }

            if (vibratoDepth > 0.0005f || vibratoTarget > 0.0005f)
            {
                vibratoDepth += (vibratoTarget - vibratoDepth) * vibratoSmoothCoef;

                vibratoPhase += vibratoPhaseInc;
                if (vibratoPhase > juce::MathConstants<float>::twoPi)
                    vibratoPhase -= juce::MathConstants<float>::twoPi;

                // **上にしか揺れない**（クラスの説明）
                vibratoOffset = vibratoDepth * 0.5f * (1.0f + std::sin (vibratoPhase));
                needFreqUpdate = true;
            }
            else if (vibratoOffset != 0.0f)
            {
                vibratoOffset = 0.0f;
                vibratoPhase  = 0.0f;
                needFreqUpdate = true;
            }

            if (needFreqUpdate)
                updateFrequency();

            //--- 弦 ------------------------------------------------------------
            float exc = 0.0f;

            if (excitationPos < excitationLength)
                exc = excitation[(size_t) excitationPos++];

            float out = string.process (exc);

            if (releasing)
            {
                releaseGain *= releaseCoef;
                out *= releaseGain;
            }

            envelope = juce::jmax (std::abs (out), envelope * 0.9999f);

            for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
                outputBuffer.addSample (ch, startSample + i, out);
        }

        if (releasing && (releaseGain < 1.0e-4f || envelope < 1.0e-4f))
        {
            clearCurrentNote();
            string.reset();
            releasing = false;
        }
    }

private:
    //==========================================================================
    /** 押さえる弦の推定。

        レギュラーチューニング（E2 A2 D3 G3 B3 E4）のうち、**その音程以下で
        いちばん高い開放弦**を選びます＝いちばん自然に押さえられる弦。 */
    static float estimateOpenStringFreq (int midiNote)
    {
        static constexpr int openNotes[6] = { 40, 45, 50, 55, 59, 64 };

        int chosen = openNotes[0];

        for (int n : openNotes)
            if (n <= midiNote && n > chosen)
                chosen = n;

        return (float) juce::MidiMessage::getMidiNoteInHertz (chosen);
    }

    float getReleaseCoef() const
    {
        constexpr float releaseTimeMs = 80.0f;
        const float samples = releaseTimeMs * 0.001f * (float) getSampleRate();

        return std::pow (10.0f, -3.0f / samples);
    }

    /** ピッキングハーモニクスの次数は**Pick Posと連動**します
        （実機と同じく、ピックを当てる位置でスクイールの音程が変わる）。 */
    int pinchHarmonicOrder() const
    {
        const float p = juce::jmax (0.05f, pickPos);

        return juce::jlimit (3, 5, (int) std::round (1.0f / p));
    }

    /** ピックの硬さ＋ベロシティから、励起ローパスの基準カットオフを出す。 */
    float hardnessCutoff() const
    {
        const float base = juce::jmap (pickHardness, 0.06f, 0.85f);

        return base * (0.55f + 0.45f * velocityAtStart);
    }

    //==========================================================================
    /** 奏法ごとの「実効の音色」。**つまみの値に上限が掛かります**。 */
    void applyEffectiveTone()
    {
        switch (articulationAtStart)
        {
            case GuitarArticulation::palmMute:
            {
                // **ベロシティでミュートの深さが変わる**（実機の挙動）。
                // 弱く弾く＝深いミュート、強く弾く＝浅いミュート
                const float v = velocityAtStart;
                string.setBrightness   (juce::jmin (brightness,     juce::jmap (v, 0.20f, 0.36f)));
                string.setDecaySeconds (juce::jmin (sustainSeconds, juce::jmap (v, 0.15f, 0.5f)));
                break;
            }

            case GuitarArticulation::harmonic:
                string.setBrightness (juce::jmin (brightness, 0.45f));
                string.setDecaySeconds (sustainSeconds);
                break;

            case GuitarArticulation::pinchHarmonic:
                string.setBrightness (juce::jlimit (0.05f, 0.95f, brightness * 1.3f));
                string.setDecaySeconds (sustainSeconds);
                break;

            case GuitarArticulation::brushing:
            {
                // 左手で弦に触れたまま弾くので、振動がほぼ即座に殺されて音程感が消える。
                // 減衰を極端に短くし、「ジャッ」という高域は残すため brightness は中程度
                const float v = velocityAtStart;
                string.setBrightness   (juce::jlimit (0.05f, 0.9f, 0.45f + 0.25f * v));
                string.setDecaySeconds (0.05f + 0.03f * v);
                break;
            }

            case GuitarArticulation::slide:
            case GuitarArticulation::normal:
            default:
                string.setBrightness (brightness);
                string.setDecaySeconds (sustainSeconds);
                break;
        }
    }

    //==========================================================================
    void generateExcitation()
    {
        const float v = velocityAtStart;

        //--- ハーモニクス系：1/k周期のバーストをk回並べて周期化 ----------------
        if (articulationAtStart == GuitarArticulation::harmonic
         || articulationAtStart == GuitarArticulation::pinchHarmonic)
        {
            const bool pinch = (articulationAtStart == GuitarArticulation::pinchHarmonic);
            const int  k     = pinch ? pinchHarmonicOrder() : 2;

            const int period = juce::jlimit (4, maxExcitationLength / k,
                                              (int) (string.getTotalDelay() / (float) k));

            // ナチュラルは触れる指が支配的なので、硬さの影響を弱めに
            const float cutoff    = pinch
                ? juce::jmin (0.95f, hardnessCutoff() * 1.4f + 0.15f)
                : hardnessCutoff() * 0.7f;
            const float amplitude = pinch ? v * 0.8f : v * 0.5f;

            buildBurst (scratchBurst, period, cutoff, amplitude);

            excitationLength = period * k;
            excitationPos = 0;

            for (int i = 0; i < excitationLength; ++i)
                excitation[(size_t) i] = scratchBurst[i % period];

            addPickNoise (excitation.data(), excitationLength, pinch ? 1.0f : 0.4f);
            return;
        }

        //--- ブラッシング：弦を擦る広帯域ノイズ --------------------------------
        if (articulationAtStart == GuitarArticulation::brushing)
        {
            // 弦自体はほぼ即座に減衰するので、**この励起音が主な音色**になる
            excitationLength = juce::jlimit (8, maxExcitationLength,
                                              (int) (string.getTotalDelay() * 1.6f));
            excitationPos = 0;

            buildBurst (excitation.data(), excitationLength,
                         0.55f + 0.35f * v,     // 広帯域（高域まで残す）
                         v * 0.55f);

            addPickNoise (excitation.data(), excitationLength, 1.2f);
            return;
        }

        //--- 通常系（ノーマル／スライド／パームミュート） ----------------------
        const bool muted = (articulationAtStart == GuitarArticulation::palmMute);

        float lengthScale = 1.0f;

        if (muted)
            lengthScale = juce::jmap (v, 0.35f, 0.65f);

        excitationLength = juce::jlimit (8, maxExcitationLength,
                                          (int) (string.getTotalDelay() * lengthScale));
        excitationPos = 0;

        float cutoff = hardnessCutoff();

        if (muted)
            cutoff *= juce::jmap (v, 0.30f, 0.55f);

        buildBurst (excitation.data(), excitationLength, cutoff, v * 0.7f);

        // ミュート時も手のひら越しにカチッと鳴る
        addPickNoise (excitation.data(), excitationLength, muted ? 0.8f : 1.0f);
    }

    //==========================================================================
    /** ノイズバースト＋ピック位置のコムフィルタ＋ローパス。

        **`length`は`maxExcitationLength`以下であること**（呼ぶ側で切ってあります）。

        下ごしらえの雑音は**メンバの置き場**（`scratchNoise`）に書きます——
        元の`KSGuitar`は`std::vector`をここで作っていましたが、
        **音のスレッドで確保しない**のがこのアプリの決まりです（9.5）。 */
    void buildBurst (float* dest, int length, float cutoff, float amplitude)
    {
        jassert (length <= maxExcitationLength);

        // ピック位置は mm 指定の絶対位置から、いまの実効弦長に対する比率で出す
        const int pickDelay = juce::jlimit (1, juce::jmax (1, length - 1),
                                             (int) (string.getTotalDelay()
                                                    * string.getPickPositionRatio()));

        auto* noise = scratchNoise;

        for (int i = 0; i < length; ++i)
            noise[i] = random.nextFloat() * 2.0f - 1.0f;

        cutoff = juce::jlimit (0.01f, 0.99f, cutoff);

        float lp = 0.0f;

        for (int i = 0; i < length; ++i)
        {
            const float comb = noise[i] - (i >= pickDelay ? noise[i - pickDelay] : 0.0f);

            lp = cutoff * comb + (1.0f - cutoff) * lp;
            dest[i] = lp * amplitude;
        }
    }

    /** ピックノイズ：励起の先頭に加える約2msの高域寄りクリック。 */
    void addPickNoise (float* dest, int excLength, float scale)
    {
        if (pickAttack <= 0.001f)
            return;

        const int clickLen = juce::jlimit (4, excLength, (int) (getSampleRate() * 0.002));

        const float amount = pickAttack * scale
                             * (0.15f + 0.45f * velocityAtStart)
                             * (0.5f  + 0.5f  * pickHardness);

        float prev = 0.0f;

        for (int i = 0; i < clickLen; ++i)
        {
            const float n  = random.nextFloat() * 2.0f - 1.0f;
            const float hp = n - prev;                       // 1次差分＝ハイパス
            prev = n;

            const float env = 1.0f - (float) i / (float) clickLen;   // 減衰窓
            dest[i] += hp * env * env * amount;
        }
    }

    void injectLegatoExcitation (float strength)
    {
        const int length = juce::jlimit (8, maxExcitationLength,
                                          (int) (string.getTotalDelay() * 0.25f));

        excitationLength = length;
        excitationPos = 0;

        buildBurst (excitation.data(), length,
                     0.12f + 0.15f * strength,
                     0.10f + 0.20f * strength);

        // **ピックノイズは加えない**（レガートはピックを使わない）
    }

    void updateFrequency()
    {
        const float semis = currentNoteSemis + pitchBendSemitones + vibratoOffset;
        const float freq  = 440.0f * std::pow (2.0f, (semis - 69.0f) / 12.0f);

        string.setFrequency (freq);
    }

    static float pitchWheelToSemitones (int pitchWheelValue)
    {
        return ((float) pitchWheelValue - 8192.0f) / 8192.0f * 2.0f;
    }

    static constexpr int maxExcitationLength = 4096;

    RaccoGuitarString string;

    /** **ボイスが1つずつ持ちます**——`juce::Random`を関数の中で作ると、
        同じブロックで複数のボイスが同じ種から始まることがあります。 */
    juce::Random random;

    std::vector<float> excitation;
    int excitationLength = 0;
    int excitationPos    = 0;

    /** 下ごしらえの置き場。**`startNote()`のたびに確保しないため**に持っています。 */
    float scratchNoise[maxExcitationLength] { };
    float scratchBurst[maxExcitationLength] { };

    float currentNoteSemis = 69.0f;
    float targetNoteSemis  = 69.0f;
    float glideCoef        = 1.0f;
    float pitchBendSemitones = 0.0f;

    // ビブラート（CC1）
    static constexpr float vibratoRateHz   = 5.5f;
    static constexpr float maxVibratoSemis = 0.5f;
    float vibratoTarget     = 0.0f;
    float vibratoDepth      = 0.0f;
    float vibratoPhase      = 0.0f;
    float vibratoPhaseInc   = 0.0f;
    float vibratoSmoothCoef = 0.001f;
    float vibratoOffset     = 0.0f;

    float brightness     = 0.5f;
    float sustainSeconds = 3.0f;
    float pickPos        = 0.15f;

    /** スケール長。**648mm＝25.5インチ相当のロングスケール**（固定）。 */
    static constexpr float scaleLengthMm = 648.0f;

    float pickHardness   = 0.5f;   // 0＝フェルト／指 〜 1＝硬いピック
    float pickAttack     = 0.35f;  // ピックノイズの量

    GuitarArticulation articulation        = GuitarArticulation::normal;
    GuitarArticulation articulationAtStart = GuitarArticulation::normal;
    float              velocityAtStart     = 1.0f;

    float envelope    = 0.0f;
    bool  releasing   = false;
    float releaseGain = 1.0f;
};
