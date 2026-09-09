#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "MantaSynthDSP.h"
#include "MantaSynthParameters.h"

#include <algorithm>

//==============================================================================
/**
    つまみの現在値を、鳴っている声すべてへ配るための入れ物。

    `processBlock()`のたびに全ボイスへ配ります。**ボイスは自分でAPVTSを引きません**
    ——8声が同時に文字列でパラメータを引くと、それだけで無視できない時間になります。
*/
struct MantaSynthSettings
{
    // OSC 1
    int   osc1Wave = 0;
    int   osc1Voices = 3;
    float osc1Detune = 15.0f;      // cent
    float osc1Level = 0.8f;
    float osc1Spread = 0.0f;       // ステレオ拡散 0..1

    // OSC 2
    int   osc2Wave = 0;
    int   osc2Voices = 1;
    float osc2Detune = 10.0f;
    float osc2Level = 0.0f;
    float osc2Spread = 0.0f;
    int   osc2Octave = 0;          // -2..+2

    // SUB
    int   subWave = 0;             // 0=Sine, 1=Square
    int   subOctave = -1;          // -1 or -2
    float subLevel = 0.0f;

    // NOISE
    int   noiseType = 0;           // 0=White, 1=Pink, 2=Brown, 3=Blue
    float noiseLevel = 0.0f;

    // FILTER
    float cutoff = 8000.0f;        // Hz
    float resonance = 0.3f;
    int   filterType = 0;          // 0=LP, 1=BP, 2=HP

    // ENV
    float ampA = 0.005f, ampD = 0.1f, ampS = 0.8f, ampR = 0.15f;
    float modA = 0.01f, modD = 0.2f, modS = 0.0f, modR = 0.2f;

    // LFO
    float lfoRate = 5.0f;
    float lfoDepth = 0.0f;
    int   lfoDest = 0;             // 0=Filter, 1=Pitch, 2=Amp
    float lfo2Rate = 3.0f;
    float lfo2Depth = 0.0f;
    int   lfo2Dest = 0;

    /** Mod Env → Cutoff（固定結線ぶん）。 */
    float modEnvToCutoff = 0.0f;

    float driveAmt = 1.0f;         // 1..20

    /** ポルタメント：直前の音程から新しい音程へ移る時間（秒）。0で無効。 */
    float glideTime = 0.0f;

    // セクションのON/OFF
    bool osc1On = true, osc2On = true, subOn = true, noiseOn = true;
    bool filterOn = true, lfoOn = true, modEnvOn = true, driveOn = true, lfo2On = true;

    //--------------------------------------------------------------------------
    /** モジュレーション・マトリクス。**4スロット**（本人の指定。EAGLE type0は8）。

        - ソース … 0=None, 1=LFO1, 2=ModEnv, 3=AmpEnv, 4=Velocity, 5=Note, 6=LFO2
        - デスト … 0=None, 1=Pitch, 2=Cutoff, 3=Amp, 4=OSC1Lv, 5=OSC2Lv, 6=OSC1Spread, 7=Detune

        番号は`MantaSynthParams::getMatrixSourceNames()`の並びと**対**です。
        片方だけ増やすと、画面の名前と効く先がずれます（1.27）。 */
    static constexpr int numSlots = MantaSynthParams::numMatrixSlots;

    int   matrixSource[numSlots] { };
    int   matrixDestination[numSlots] { };
    float matrixAmount[numSlots] { };   // -1..+1
};

//==============================================================================
/** このシンセが鳴らせる音（`juce::Synthesiser`の決まりごと）。 */
class MantaSynthSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

//==============================================================================
/**
    1声ぶん。**オシレータからアンプまで、1サンプルずつ通します。**

    ### なぜ1サンプルずつなのか

    マトリクスでピッチとカットオフを動かすからです。
    ブロック単位でまとめて処理すると、**LFOの1周期がブロック長で階段状**になり、
    速いLFOほど「ジリジリ」という別の音になります。

    ### 固定結線とマトリクスは足し合わせる

    LFO1/LFO2の`Dest`つまみ（固定結線）と、マトリクスの4スロットは
    **両方効きます**。片方を消すと、EAGLE type0で作った工場プリセットの
    音が変わってしまうためです。
*/
class MantaSynthVoice : public juce::SynthesiserVoice
{
public:
    MantaSynthVoice() { prepare (0.0); }

    /** **0を渡してよい**（作った直後は、まだレートが決まっていません）。
        そのままだと`freq / 0`で無限大になるので、ここで受け止めます。 */
    void prepare (double rate)
    {
        const double sampleRate = rate > 0.0 ? rate : 44100.0;

        osc1.setSampleRate (sampleRate);
        osc2.setSampleRate (sampleRate);
        subOsc.setSampleRate (sampleRate);
        filterL.setSampleRate (sampleRate);
        filterR.setSampleRate (sampleRate);
        ampEnv.setSampleRate (sampleRate);
        modEnv.setSampleRate (sampleRate);
        lfo.setSampleRate (sampleRate);
        lfo2.setSampleRate (sampleRate);
    }

    void setSettings (const MantaSynthSettings& newSettings) { settings = newSettings; }

    /** 直前に弾かれた音程を伝える（グライドの開始点になる）。 */
    void setGlideFrom (float note) { glideFrom = note; }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<MantaSynthSound*> (sound) != nullptr;
    }

    void startNote (int midiNote, float velocity,
                     juce::SynthesiserSound*, int currentPitchWheel) override
    {
        noteNumber = midiNote;

        // --- ポルタメント ---
        targetNote = (float) midiNote;

        if (settings.glideTime > 0.001f && glideFrom >= 0.0f && glideFrom != targetNote)
        {
            currentNote = glideFrom;

            // **音程差に関わらず、指定した時間で到達させる**（一定時間方式）。
            // 3度でも1オクターブでも同じ時間で移るので、フレーズの表情が揃います
            float samples = settings.glideTime * (float) getSampleRate();
            glideStep = (samples > 1.0f) ? (targetNote - currentNote) / samples
                                          : (targetNote - currentNote);
        }
        else
        {
            currentNote = targetNote;
            glideStep = 0.0f;
        }

        velocityValue = velocity;
        pitchBendSemis = (currentPitchWheel - 8192) / 8192.0f * 2.0f;   // ±2半音

        osc1.setWaveform ((MantaSynthDSP::Waveform) settings.osc1Wave);
        osc1.setNumVoices (settings.osc1Voices);
        osc1.setDetune (settings.osc1Detune);
        osc1.setSpread (settings.osc1Spread);
        osc1.reset();

        osc2.setWaveform ((MantaSynthDSP::Waveform) settings.osc2Wave);
        osc2.setNumVoices (settings.osc2Voices);
        osc2.setDetune (settings.osc2Detune);
        osc2.setSpread (settings.osc2Spread);
        osc2.reset();

        subOsc.setWaveform ((MantaSynthDSP::SubWave) settings.subWave);
        subOsc.setOctave (settings.subOctave);
        subOsc.reset();

        noise.setType ((MantaSynthDSP::NoiseType) settings.noiseType);
        noise.reset();

        updateFrequencies();

        filterL.reset(); filterR.reset();
        lfo.reset();
        lfo2.reset();

        ampEnv.setParameters (settings.ampA, settings.ampD, settings.ampS, settings.ampR);
        modEnv.setParameters (settings.modA, settings.modD, settings.modS, settings.modR);
        ampEnv.noteOn();
        modEnv.noteOn();

        lastPitchMod = 0.0f;
        lastSpread1 = settings.osc1Spread;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
            modEnv.noteOff();
        }
        else
        {
            clearCurrentNote();
            ampEnv.noteOff();
        }
    }

    void pitchWheelMoved (int newValue) override
    {
        pitchBendSemis = (newValue - 8192) / 8192.0f * 2.0f;
        updateFrequencies();
    }

    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outBuffer,
                           int startSample, int numSamples) override
    {
        if (! ampEnv.isActive()) return;

        // **毎ブロック反映すること。** これが無いとRATEつまみが効きません
        lfo.setRate (settings.lfoRate);
        lfo2.setRate (settings.lfo2Rate);

        auto* left = outBuffer.getWritePointer (0, startSample);
        auto* right = outBuffer.getNumChannels() > 1
                        ? outBuffer.getWritePointer (1, startSample) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            // --- モジュレーションのもとを1回ずつ計算 ---
            //
            // **切ってあっても回すこと。** 止めると位相が置いていかれ、
            // 入れ直した瞬間に別のところから始まります
            float lfoRaw = lfo.processSine();
            if (! settings.lfoOn) lfoRaw = 0.0f;

            float lfo2Raw = lfo2.processSine();
            if (! settings.lfo2On) lfo2Raw = 0.0f;

            float modEnvValue = modEnv.process();
            if (! settings.modEnvOn) modEnvValue = 0.0f;

            const float ampEnvValue = ampEnv.process();
            const float noteNorm = (noteNumber - 60) / 24.0f;   // C4基準で-1..+1程度

            const float sourceValue[7] { 0.0f, lfoRaw, modEnvValue, ampEnvValue,
                                          velocityValue, noteNorm, lfo2Raw };

            // --- 行き先ごとに足し合わせる ---
            float destination[8] { };

            for (int slot = 0; slot < MantaSynthSettings::numSlots; ++slot)
            {
                const int source = settings.matrixSource[slot];
                const int dest = settings.matrixDestination[slot];

                if (source <= 0 || dest <= 0) continue;         // Noneは飛ばす

                destination[dest] += sourceValue[source] * settings.matrixAmount[slot];
            }

            // --- 固定結線ぶん ---
            const float lfoValue = lfoRaw * settings.lfoDepth;
            const float lfo2Value = lfo2Raw * settings.lfo2Depth;

            float pitchModSemis = destination[1] * 12.0f;       // マトリクス：最大±1oct
            if (settings.lfoDest == 1)  pitchModSemis += lfoValue * 0.5f;
            if (settings.lfo2Dest == 1) pitchModSemis += lfo2Value * 0.5f;

            // --- グライドを1サンプル進める ---
            bool gliding = false;

            if (glideStep != 0.0f && currentNote != targetNote)
            {
                currentNote += glideStep;

                // **行き過ぎたら止める。** 通り越すと、着いたあとで揺り戻します
                if ((glideStep > 0.0f && currentNote >= targetNote)
                 || (glideStep < 0.0f && currentNote <= targetNote))
                {
                    currentNote = targetNote;
                    glideStep = 0.0f;
                }

                gliding = true;
            }

            if (gliding || pitchModSemis != 0.0f || lastPitchMod != 0.0f)
                updateFrequenciesWithMod (pitchModSemis);

            lastPitchMod = pitchModSemis;

            // --- レベルと広がりをマトリクスで動かす ---
            const float level1 = settings.osc1On
                                   ? std::clamp (settings.osc1Level + destination[4], 0.0f, 1.5f) : 0.0f;
            const float level2 = settings.osc2On
                                   ? std::clamp (settings.osc2Level + destination[5], 0.0f, 1.5f) : 0.0f;

            const float spread1 = std::clamp (settings.osc1Spread + destination[6], 0.0f, 1.0f);

            // **変わったときだけ入れ直す**（パン係数の計算が毎サンプル走らないように）
            if (spread1 != lastSpread1) { osc1.setSpread (spread1); lastSpread1 = spread1; }

            if (destination[7] != 0.0f)
            {
                osc1.setDetune (std::clamp (settings.osc1Detune + destination[7] * 50.0f, 0.0f, 50.0f));
                osc2.setDetune (std::clamp (settings.osc2Detune + destination[7] * 50.0f, 0.0f, 50.0f));
            }

            // --- オシレータ（ステレオ拡散） ---
            float o1L, o1R, o2L, o2R;
            osc1.processStereo (o1L, o1R);
            osc2.processStereo (o2L, o2R);

            float sampleL = o1L * level1 + o2L * level2;
            float sampleR = o1R * level1 + o2R * level2;

            // --- サブとノイズ（モノ。真ん中へ足す） ---
            const float subValue = subOsc.process() * (settings.subOn ? settings.subLevel : 0.0f);
            const float noiseValue = noise.process() * (settings.noiseOn ? settings.noiseLevel : 0.0f);
            const float centre = subValue + noiseValue;
            sampleL += centre;
            sampleR += centre;

            // --- フィルタ（左右は別々に通す。ステレオ幅を保つため） ---
            float cutoffMod = 0.0f;
            if (settings.lfoDest == 0)  cutoffMod += lfoValue * 3.0f;
            if (settings.lfo2Dest == 0) cutoffMod += lfo2Value * 3.0f;
            cutoffMod += modEnvValue * settings.modEnvToCutoff * 4.0f;
            cutoffMod += destination[2] * 5.0f;                        // マトリクス：±5oct

            const float cutoff = settings.cutoff * std::pow (2.0f, cutoffMod);
            const auto filterType = (MantaSynthDSP::FilterType) settings.filterType;

            filterL.setType (filterType); filterL.setParams (cutoff, settings.resonance);
            filterR.setType (filterType); filterR.setParams (cutoff, settings.resonance);

            if (settings.filterOn)
            {
                sampleL = filterL.process (sampleL);
                sampleR = filterR.process (sampleR);
            }

            // --- ドライブ ---
            if (settings.driveOn)
            {
                sampleL = MantaSynthDSP::drive (sampleL, settings.driveAmt);
                sampleR = MantaSynthDSP::drive (sampleR, settings.driveAmt);
            }

            // --- アンプ（ADSR × ベロシティ × マトリクス） ---
            float amp = ampEnvValue * velocityValue;
            if (settings.lfoDest == 2)  amp *= (1.0f + lfoValue * 0.5f);
            if (settings.lfo2Dest == 2) amp *= (1.0f + lfo2Value * 0.5f);
            amp *= (1.0f + destination[3]);
            amp = std::max (0.0f, amp) * 0.5f;

            sampleL *= amp;
            sampleR *= amp;

            if (right != nullptr)
            {
                left[i] += sampleL;
                right[i] += sampleR;
            }
            else if (left != nullptr)
            {
                left[i] += (sampleL + sampleR) * 0.5f;   // モノ出力は左右を平均
            }
        }

        if (! ampEnv.isActive()) clearCurrentNote();
    }

private:
    void updateFrequencies() { updateFrequenciesWithMod (0.0f); }

    void updateFrequenciesWithMod (float extraSemis)
    {
        const float baseNote = currentNote + pitchBendSemis + extraSemis;
        const float f1 = 440.0f * std::pow (2.0f, (baseNote - 69.0f) / 12.0f);
        const float f2 = f1 * std::pow (2.0f, (float) settings.osc2Octave);

        osc1.setBaseFrequency (f1);
        osc2.setBaseFrequency (f2);
        subOsc.setBaseFrequency (f1);   // サブはメイン音程を追ってオクターブ下げる
    }

    MantaSynthDSP::UnisonOscillator osc1, osc2;
    MantaSynthDSP::SubOscillator subOsc;
    MantaSynthDSP::NoiseGenerator noise;
    MantaSynthDSP::SVF filterL, filterR;
    MantaSynthDSP::ADSR ampEnv, modEnv;
    MantaSynthDSP::LFO lfo, lfo2;

    MantaSynthSettings settings;

    int noteNumber = 60;
    float velocityValue = 0.8f, pitchBendSemis = 0.0f;

    /** 前回の値（毎サンプルの入れ直しを避けるため）。 */
    float lastPitchMod = 0.0f, lastSpread1 = -1.0f;

    /** ポルタメント。`glideFrom`が-1なら「直前の音が無い」。 */
    float currentNote = 60.0f, targetNote = 60.0f, glideStep = 0.0f;
    float glideFrom = -1.0f;
};

//==============================================================================
/**
    直前に弾かれた音程を各ボイスへ伝える`juce::Synthesiser`。

    **ポルタメントの開始点はボイスの外にあります**——
    いま鳴らそうとしている声は、前に鳴っていた声を知らないためです。
*/
class MantaGlideSynthesiser : public juce::Synthesiser
{
public:
    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override
    {
        for (int i = 0; i < getNumVoices(); ++i)
            if (auto* voice = dynamic_cast<MantaSynthVoice*> (getVoice (i)))
                voice->setGlideFrom (lastNote);

        juce::Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
        lastNote = (float) midiNoteNumber;
    }

    void allNotesOff (int midiChannel, bool allowTailOff) override
    {
        juce::Synthesiser::allNotesOff (midiChannel, allowTailOff);
        lastNote = -1.0f;   // 全部離したらグライド元をリセット
    }

private:
    float lastNote = -1.0f;
};
