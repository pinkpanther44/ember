#pragma once

#include <algorithm>
#include <array>
#include <cmath>

//==============================================================================
/**
    Manta Synthesizer の音を作る部品（Phase 213）。

    ### どこから来たか

    本人が以前に作った **EAGLE type0**（別リポジトリ）の
    `Source/SynthDSP.h`を写したものです。**動いていたものを土台にする**という
    本人の指示なので、式そのものは変えていません。

    変えたのは名前だけです（`rs`→`MantaSynthDSP`）。
    本体には既に`MantaBiquad`という別の`rs`相当があり、
    **同じ短い名前が2つあると、どちらを読んでいるのか分からなくなります**。

    ### JUCEに依存させていない

    元のファイルと同じく、**JUCEのヘッダを1つも取り込んでいません**。
    そのためg++単体でコンパイルでき、数値の確かめだけを回せます
    （HANDOVER 9.5「DSPは切り離して測る」）。

    | 部品 | 何をするもの |
    |---|---|
    | `Oscillator` | PolyBLEPでエイリアスを抑えたノコギリ／矩形／三角 |
    | `UnisonOscillator` | 同じ波を7本までデチューンして重ねる（太さの正体） |
    | `SubOscillator` | 1〜2オクターブ下を足して低音を支える |
    | `NoiseGenerator` | White／Pink／Brown／Blue |
    | `ADSR` | 音量と変調のエンベロープ |
    | `LFO` | 揺らすもの |
    | `SVF` | レゾナンス付きの状態変数フィルタ（LP／BP／HP） |
    | `drive()` | tanhの飽和 |
*/
namespace MantaSynthDSP
{
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    //==========================================================================
    /** ポリブレップ（PolyBLEP）：ノコギリ・矩形のエイリアスを抑える定番手法。

        波形の不連続点の近くだけを補正します。素のnaive波形は、
        **不連続点が持つ無限の倍音がそのまま折り返して**きます。 */
    inline float polyBlep (float t, float dt)
    {
        if (t < dt)                 { t /= dt; return t + t - t * t - 1.0f; }
        else if (t > 1.0f - dt)     { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    enum class Waveform { Saw = 0, Square = 1, Triangle = 2 };

    //==========================================================================
    /** 1本ぶんのアンチエイリアス・オシレータ。 */
    class Oscillator
    {
    public:
        void setSampleRate (double sr) { sampleRate = (float) sr; }
        void setFrequency (float hz)   { freq = hz; phaseInc = freq / sampleRate; }
        void setWaveform (Waveform w)  { wave = w; }
        void reset (float startPhase = 0.0f) { phase = startPhase; triState = 0.0f; }

        float process()
        {
            float dt = phaseInc;
            float value = 0.0f;

            switch (wave)
            {
                case Waveform::Saw:
                {
                    value = 2.0f * phase - 1.0f;          // naive saw (-1..1)
                    value -= polyBlep (phase, dt);        // アンチエイリアス補正
                    break;
                }
                case Waveform::Square:
                {
                    value = (phase < 0.5f) ? 1.0f : -1.0f;
                    value += polyBlep (phase, dt);
                    float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                    value -= polyBlep (t2, dt);
                    break;
                }
                case Waveform::Triangle:
                {
                    // 矩形を積分して三角波を得る（leaky integrator）
                    float sq = (phase < 0.5f) ? 1.0f : -1.0f;
                    sq += polyBlep (phase, dt);
                    float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                    sq -= polyBlep (t2, dt);
                    triState += 4.0f * dt * sq;           // 積分（振幅を約±1に正規化）
                    triState *= 0.999f;                   // DCドリフト防止のリーク
                    value = triState;
                    break;
                }
            }

            phase += phaseInc;
            if (phase >= 1.0f) phase -= 1.0f;
            return value;
        }

    private:
        float sampleRate = 44100.0f;
        float freq = 440.0f, phaseInc = 0.0f, phase = 0.0f, triState = 0.0f;
        Waveform wave = Waveform::Saw;
    };

    //==========================================================================
    /** サブオシレーター：メイン音より1〜2オクターブ低い音を足して低音を強化する。

        ベース／リードの土台を分厚くする定番。サイン波か矩形波を選べます。 */
    enum class SubWave { Sine = 0, Square = 1 };

    class SubOscillator
    {
    public:
        void setSampleRate (double sr) { sampleRate = (float) sr; }
        void setWaveform (SubWave w) { wave = w; }
        void setOctave (int oct) { octave = std::clamp (oct, -2, -1); }
        void reset() { phase = 0.0f; }

        /** メインの周波数を渡すと、オクターブ下げて内部で保持する。 */
        void setBaseFrequency (float mainHz)
        {
            freq = mainHz * std::pow (2.0f, (float) octave);
            phaseInc = freq / sampleRate;
        }

        float process()
        {
            float value = 0.0f;

            switch (wave)
            {
                case SubWave::Sine:
                    value = std::sin (kTwoPi * phase);
                    break;

                case SubWave::Square:
                {
                    // サブは低音なのでエイリアスの心配は小さいが、一応整える
                    value = (phase < 0.5f) ? 1.0f : -1.0f;
                    value += polyBlep (phase, phaseInc);
                    float t2 = phase + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                    value -= polyBlep (t2, phaseInc);
                    break;
                }
            }

            phase += phaseInc;
            if (phase >= 1.0f) phase -= 1.0f;
            return value;
        }

    private:
        float sampleRate = 44100.0f;
        float freq = 110.0f, phaseInc = 0.0f, phase = 0.0f;
        int octave = -1;
        SubWave wave = SubWave::Sine;
    };

    //==========================================================================
    /** ノイズ：音程を持たない「ザー」を混ぜる。

        どれもホワイトノイズに1段フィルタを掛けて作る軽い実装です。 */
    enum class NoiseType { White = 0, Pink = 1, Brown = 2, Blue = 3 };

    class NoiseGenerator
    {
    public:
        void setType (NoiseType t) { type = t; }
        void reset() { pink = 0.0f; brown = 0.0f; lastWhite = 0.0f; }

        float process()
        {
            float white = nextWhite();

            switch (type)
            {
                case NoiseType::White:
                    return white;

                case NoiseType::Pink:
                    // 一極ローパスで低音を持ち上げる簡易ピンク
                    pink = 0.98f * pink + 0.02f * white;
                    return pink * 3.5f;   // 音量を合わせる

                case NoiseType::Brown:
                    // 積分に近い強めのローパス。低音がゴロゴロする
                    brown = 0.995f * brown + 0.005f * white;
                    return std::clamp (brown * 8.0f, -1.0f, 1.0f);

                case NoiseType::Blue:
                    // ハイパス的に差分をとって高音寄りに
                    { float b = white - lastWhite; lastWhite = white; return b * 0.7f; }
            }

            return white;
        }

    private:
        /** 高速な擬似乱数（xorshift）で -1..1 のホワイトノイズ。 */
        float nextWhite()
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            return ((seed & 0xFFFFFF) / 8388608.0f) - 1.0f;
        }

        NoiseType type = NoiseType::White;
        unsigned int seed = 0x1234567u;
        float pink = 0.0f, brown = 0.0f, lastWhite = 0.0f;
    };

    //==========================================================================
    /** ユニゾン：複数のオシレータをデチューンして重ね、太さと広がりを作る。

        **1本ずつ左右へ振る**ので、デチューンした声部が空間的にも離れます。 */
    class UnisonOscillator
    {
    public:
        static constexpr int kMaxVoices = 7;

        UnisonOscillator() { updatePans(); }   // 初期パン係数を確定させる

        void setSampleRate (double sr) { for (auto& o : oscs) o.setSampleRate (sr); }
        void setWaveform (Waveform w) { for (auto& o : oscs) o.setWaveform (w); }
        void setNumVoices (int n) { numVoices = std::clamp (n, 1, kMaxVoices); updatePans(); }
        void setDetune (float cents) { detuneCents = cents; }
        void setSpread (float s) { spread = std::clamp (s, 0.0f, 1.0f); updatePans(); }

        void reset()
        {
            // 位相をばらすと重なりが自然になる（全部0だと初期に音が硬くなる）
            static const float ph[kMaxVoices] = { 0.0f, 0.13f, 0.29f, 0.47f, 0.61f, 0.79f, 0.91f };
            for (int i = 0; i < kMaxVoices; ++i) oscs[i].reset (ph[i]);
        }

        void setBaseFrequency (float hz) { baseFreq = hz; updateFreqs(); }

        /** 等パワーパンで左右へ振り分けて加算する。 */
        void processStereo (float& outL, float& outR)
        {
            float norm = 1.0f / std::sqrt ((float) numVoices);
            float l = 0.0f, r = 0.0f;

            for (int i = 0; i < numVoices; ++i)
            {
                float v = oscs[i].process();
                l += v * panL[i];
                r += v * panR[i];
            }

            outL = l * norm;
            outR = r * norm;
        }

    private:
        void updateFreqs()
        {
            if (numVoices == 1) { oscs[0].setFrequency (baseFreq); return; }

            for (int i = 0; i < numVoices; ++i)
            {
                // -1..+1 に均等配置してデチューン
                float pos = (float) i / (float) (numVoices - 1) * 2.0f - 1.0f;
                float ratio = std::pow (2.0f, (pos * detuneCents) / 1200.0f);
                oscs[i].setFrequency (baseFreq * ratio);
            }
        }

        void updatePans()
        {
            for (int i = 0; i < kMaxVoices; ++i)
            {
                float pan = 0.0f;   // -1(左)..+1(右)

                if (numVoices > 1)
                {
                    float pos = (float) i / (float) (numVoices - 1) * 2.0f - 1.0f;
                    pan = pos * spread;
                }

                // pan(-1..1) を角度(0..pi/2)にマップして等パワー化
                float angle = (pan * 0.5f + 0.5f) * (kPi * 0.5f);
                panL[i] = std::cos (angle);
                panR[i] = std::sin (angle);
            }
        }

        std::array<Oscillator, kMaxVoices> oscs;
        std::array<float, kMaxVoices> panL { }, panR { };
        int numVoices = 1;
        float detuneCents = 15.0f, baseFreq = 440.0f, spread = 0.0f;
    };

    //==========================================================================
    /** ADSRエンベロープ（サンプル単位。DecayとReleaseは指数的）。 */
    class ADSR
    {
    public:
        void setSampleRate (double sr) { sampleRate = (float) sr; }

        /** 時間は秒、`s`は0..1のレベル。 */
        void setParameters (float a, float d, float s, float r)
        {
            atkRate = (a <= 0.0f) ? 1.0f : 1.0f / (a * sampleRate);
            decRate = (d <= 0.0f) ? 1.0f : 1.0f / (d * sampleRate);
            susLevel = std::clamp (s, 0.0f, 1.0f);
            relRate = (r <= 0.0f) ? 1.0f : 1.0f / (r * sampleRate);
        }

        void noteOn()  { stage = Stage::Attack; }
        void noteOff() { if (stage != Stage::Idle) stage = Stage::Release; }
        bool isActive() const { return stage != Stage::Idle; }

        float process()
        {
            switch (stage)
            {
                case Stage::Idle:    level = 0.0f; break;

                case Stage::Attack:
                    level += atkRate;
                    if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                    break;

                case Stage::Decay:
                    level -= decRate * (level - susLevel + 0.0001f) * 4.0f;
                    if (level <= susLevel + 0.001f) { level = susLevel; stage = Stage::Sustain; }
                    break;

                case Stage::Sustain: level = susLevel; break;

                case Stage::Release:
                    level -= relRate * (level + 0.0001f) * 4.0f;
                    if (level <= 0.0005f) { level = 0.0f; stage = Stage::Idle; }
                    break;
            }

            return level;
        }

    private:
        enum class Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Stage::Idle;
        float sampleRate = 44100.0f, level = 0.0f;
        float atkRate = 0.01f, decRate = 0.01f, susLevel = 0.7f, relRate = 0.01f;
    };

    //==========================================================================
    /** LFO：音を「動かす」主役。 */
    class LFO
    {
    public:
        void setSampleRate (double sr) { sampleRate = (float) sr; }
        void setRate (float hz) { rate = hz; }
        void reset() { phase = 0.0f; }

        /** -1..1。**サイン固定**（元のEAGLE type0と同じ）。 */
        float processSine()
        {
            float v = std::sin (kTwoPi * phase);
            phase += rate / sampleRate;
            if (phase >= 1.0f) phase -= 1.0f;
            return v;
        }

    private:
        float sampleRate = 44100.0f, rate = 5.0f, phase = 0.0f;
    };

    //==========================================================================
    /** 状態変数フィルタ（SVF。Andrew Simper / Cytomic トポロジ）。

        **カットオフをLFOやエンベロープで揺らしても安定なのが利点**です。
        毎サンプル係数を作り直しても壊れません（それがこのシンセの使い方）。 */
    enum class FilterType { LowPass = 0, BandPass = 1, HighPass = 2 };

    class SVF
    {
    public:
        void setSampleRate (double sr) { sampleRate = (float) sr; }
        void reset() { ic1eq = 0.0f; ic2eq = 0.0f; }

        void setType (FilterType t) { type = t; }

        /** `cutoffHz`はHz、`resonance`は0..1（1で自己発振寸前）。 */
        void setParams (float cutoffHz, float resonance)
        {
            cutoffHz = std::clamp (cutoffHz, 20.0f, sampleRate * 0.45f);
            float g = std::tan (kPi * cutoffHz / sampleRate);
            k = 2.0f - 1.98f * std::clamp (resonance, 0.0f, 1.0f);   // 減衰係数(=1/Q)
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        /** 1回の計算からLP/BP/HPを同時に取り出せる。 */
        float process (float x)
        {
            float v3 = x - ic2eq;
            float v1 = a1 * ic1eq + a2 * v3;             // bandpass
            float v2 = ic2eq + a2 * ic1eq + a3 * v3;     // lowpass
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;

            switch (type)
            {
                case FilterType::LowPass:  return v2;
                case FilterType::BandPass: return v1;
                case FilterType::HighPass: return x - k * v1 - v2;
            }

            return v2;
        }

    private:
        FilterType type = FilterType::LowPass;
        float sampleRate = 44100.0f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f, ic1eq = 0.0f, ic2eq = 0.0f;
    };

    //==========================================================================
    /** ドライブ：tanhの飽和。

        出力は必ず(-1, 1)に収まり、入力に対して単調です
        （**上げすぎても暴れない**ので、ここだけは安心して振り切れる）。 */
    inline float drive (float x, float amount /*1..20*/)
    {
        float g = std::max (1.0f, amount);
        return std::tanh (x * g);
    }
}
