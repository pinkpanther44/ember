#pragma once

#include "MantaSynthDSP.h"

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Manta Synthesizer の出口に置くエフェクトとEQ（Phase 213）。

    EAGLE type0の`Source/Effects.h`を写したものです（`MantaSynthDSP.h`と同じ理由）。

    | | 中身 |
    |---|---|
    | `MultiFX` | Chorus／Flanger／Phaser／Tremolo／Delay／Reverb を1スロットで切り替え |
    | `ThreeBandEQ` | ローシェルフ(200Hz)＋ミッドピーク(可変)＋ハイシェルフ(4kHz) |

    ### なぜ`MantaBiquad`を使っていないか

    本体には`Source/Plugins/MantaBiquad.h`（Manta EQと共有のバイクアッド）が
    ありますが、そちらは**ローパス／ハイパスしか持っていません**。
    ここで要るのはシェルフとピークです。

    足すことも考えましたが、**Manta EQの係数はカーブの描画と対になっています**
    （`EQFilterDesign::magnitudeAt()`）。片方だけ増やすと、
    「EQに載っている形」と「描ける形」がずれます。
    ここは音を出すだけなので、**この中に閉じたまま**にしてあります。

    ### 4つのつまみで全部のタイプを賄う

    `Rate` / `Depth` / `Mix` / `Feedback` の4つを**すべて0..1で受け取り**、
    タイプごとに中で読み替えます。

    | タイプ | Rate | Depth | Feedback |
    |---|---|---|---|
    | Chorus | 揺れの速さ | 揺れ幅 | （使わない） |
    | Flanger | 揺れの速さ | 揺れ幅 | うねりの強さ |
    | Phaser | 掃引の速さ | ノッチの深さ | レゾナンス |
    | Tremolo | 揺れの速さ | 音量の振れ幅 | （使わない） |
    | Delay | **短いほど速い繰り返し** | 左右の時間差 | 繰り返す回数 |
    | Reverb | （使わない） | 高域の減り方 | 残響の長さ |

    つまみの数を増やさなかったのは、**タイプを変えるたびに画面が変わると
    どこを触っていたのか分からなくなる**ためです（元のEAGLE type0の判断）。
*/
namespace MantaSynthDSP
{
    //==========================================================================
    /** 補間付きディレイライン（小数サンプル遅延が読める）。

        コーラス／フランジャー／ディレイの土台です。 */
    class DelayLine
    {
    public:
        void prepare (double sr, float maxDelaySec)
        {
            sampleRate = (float) sr;
            int len = (int) (maxDelaySec * sampleRate) + 4;
            buf.assign ((size_t) len, 0.0f);
            writePos = 0;
        }

        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); writePos = 0; }

        void write (float x)
        {
            if (buf.empty()) return;
            buf[(size_t) writePos] = x;
            if (++writePos >= (int) buf.size()) writePos = 0;
        }

        float read (float delaySamples) const
        {
            if (buf.empty()) return 0.0f;

            int len = (int) buf.size();
            delaySamples = std::clamp (delaySamples, 1.0f, (float) (len - 2));
            float rp = (float) writePos - delaySamples;
            while (rp < 0.0f)         rp += (float) len;
            while (rp >= (float) len) rp -= (float) len;

            int i0 = (int) rp;

            // **浮動小数の丸めで`rp`がちょうど`len`になることがあります。**
            // 添字は必ず 0..len-1 に収めること（これが無いと稀に範囲外を読む）
            if (i0 >= len) i0 = 0;
            if (i0 < 0)    i0 = 0;

            float frac = rp - (float) i0;
            if (frac < 0.0f || frac > 1.0f) frac = 0.0f;

            int i1 = i0 + 1; if (i1 >= len) i1 -= len;
            return buf[(size_t) i0] * (1.0f - frac) + buf[(size_t) i1] * frac;
        }

    private:
        std::vector<float> buf;
        int writePos = 0;
        float sampleRate = 44100.0f;
    };

    //==========================================================================
    /** 1次オールパス（フェイザー用）。 */
    class AllpassStage
    {
    public:
        void setCoeff (float a) { a1 = a; }
        void reset() { z = 0.0f; }

        float process (float x)
        {
            float y = -a1 * x + z;
            z = x + a1 * y;
            return y;
        }

    private:
        float a1 = 0.0f, z = 0.0f;
    };

    //==========================================================================
    /** リバーブ用コムフィルタ（ダンピング付き）。 */
    class CombFilter
    {
    public:
        void prepare (int len) { buf.assign ((size_t) std::max (1, len), 0.0f); pos = 0; lp = 0.0f; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; lp = 0.0f; }

        float process (float x, float fb, float damp)
        {
            if (buf.empty()) return 0.0f;                             // prepare前の保険
            if (pos < 0 || pos >= (int) buf.size()) pos = 0;

            float y = buf[(size_t) pos];
            lp = y * (1.0f - damp) + lp * damp;                        // 高域を減衰させる
            buf[(size_t) pos] = x + lp * fb;
            if (++pos >= (int) buf.size()) pos = 0;
            return y;
        }

    private:
        std::vector<float> buf; int pos = 0; float lp = 0.0f;
    };

    //==========================================================================
    /** リバーブ用オールパス（拡散）。 */
    class AllpassDiffuser
    {
    public:
        void prepare (int len) { buf.assign ((size_t) std::max (1, len), 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

        float process (float x)
        {
            if (buf.empty()) return x;
            if (pos < 0 || pos >= (int) buf.size()) pos = 0;

            float y = buf[(size_t) pos];
            float out = -x + y;
            buf[(size_t) pos] = x + y * 0.5f;
            if (++pos >= (int) buf.size()) pos = 0;
            return out;
        }

    private:
        std::vector<float> buf; int pos = 0;
    };

    //==========================================================================
    /** バイクアッド（RBJクックブック）。EQのシェルフとピークに使う。 */
    class Biquad
    {
    public:
        void reset() { z1 = 0.0f; z2 = 0.0f; }

        float process (float x)
        {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void makeLowShelf (float sr, float freq, float gainDb)
        {
            float A = std::pow (10.0f, gainDb / 40.0f);
            float w0 = kTwoPi * std::clamp (freq, 20.0f, sr * 0.45f) / sr;
            float cw = std::cos (w0), sw = std::sin (w0);
            float alpha = sw / 2.0f * std::sqrt ((A + 1.0f / A) * (1.0f / 0.9f - 1.0f) + 2.0f);
            float sq = 2.0f * std::sqrt (A) * alpha;
            float a0 = (A + 1.0f) + (A - 1.0f) * cw + sq;

            setAll (A * ((A + 1.0f) - (A - 1.0f) * cw + sq) / a0,
                     2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw) / a0,
                     A * ((A + 1.0f) - (A - 1.0f) * cw - sq) / a0,
                     (-2.0f * ((A - 1.0f) + (A + 1.0f) * cw)) / a0,
                     ((A + 1.0f) + (A - 1.0f) * cw - sq) / a0);
        }

        void makeHighShelf (float sr, float freq, float gainDb)
        {
            float A = std::pow (10.0f, gainDb / 40.0f);
            float w0 = kTwoPi * std::clamp (freq, 20.0f, sr * 0.45f) / sr;
            float cw = std::cos (w0), sw = std::sin (w0);
            float alpha = sw / 2.0f * std::sqrt ((A + 1.0f / A) * (1.0f / 0.9f - 1.0f) + 2.0f);
            float sq = 2.0f * std::sqrt (A) * alpha;
            float a0 = (A + 1.0f) - (A - 1.0f) * cw + sq;

            setAll (A * ((A + 1.0f) + (A - 1.0f) * cw + sq) / a0,
                     -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw) / a0,
                     A * ((A + 1.0f) + (A - 1.0f) * cw - sq) / a0,
                     (2.0f * ((A - 1.0f) - (A + 1.0f) * cw)) / a0,
                     ((A + 1.0f) - (A - 1.0f) * cw - sq) / a0);
        }

        void makePeak (float sr, float freq, float gainDb, float Q = 0.9f)
        {
            float A = std::pow (10.0f, gainDb / 40.0f);
            float w0 = kTwoPi * std::clamp (freq, 20.0f, sr * 0.45f) / sr;
            float cw = std::cos (w0), sw = std::sin (w0);
            float alpha = sw / (2.0f * std::max (0.1f, Q));
            float a0 = 1.0f + alpha / A;

            setAll ((1.0f + alpha * A) / a0,
                     (-2.0f * cw) / a0,
                     (1.0f - alpha * A) / a0,
                     (-2.0f * cw) / a0,
                     (1.0f - alpha / A) / a0);
        }

    private:
        void setAll (float B0, float B1, float B2, float A1, float A2)
        { b0 = B0; b1 = B1; b2 = B2; a1 = A1; a2 = A2; }

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    //==========================================================================
    /** 3バンドEQ（ステレオ）。 */
    class ThreeBandEQ
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto* b : { &lowL, &lowR, &midL, &midR, &highL, &highR }) b->reset();
            update (0.0f, 0.0f, 1000.0f, 0.0f);
        }

        void update (float lowDb, float midDb, float midFreq, float highDb)
        {
            lowL.makeLowShelf (sampleRate, 200.0f, lowDb);
            lowR.makeLowShelf (sampleRate, 200.0f, lowDb);
            midL.makePeak (sampleRate, midFreq, midDb);
            midR.makePeak (sampleRate, midFreq, midDb);
            highL.makeHighShelf (sampleRate, 4000.0f, highDb);
            highR.makeHighShelf (sampleRate, 4000.0f, highDb);
        }

        void process (float& l, float& r)
        {
            l = highL.process (midL.process (lowL.process (l)));
            r = highR.process (midR.process (lowR.process (r)));
        }

    private:
        float sampleRate = 44100.0f;
        Biquad lowL, lowR, midL, midR, highL, highR;
    };

    //==========================================================================
    /** モジュレーション系（Chorus/Flanger/Phaser/Tremolo）と空間系（Delay/Reverb）。 */
    enum class FXType { Off = 0, Chorus, Flanger, Phaser, Tremolo, Delay, Reverb };

    /** マルチエフェクト（1スロットぶん）。 */
    class MultiFX
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            dlL.prepare (sr, 1.2f);   // 最大1.2秒（ディレイ用）
            dlR.prepare (sr, 1.2f);
            lfoPhase = 0.0f;
            for (auto& a : apL) a.reset();
            for (auto& a : apR) a.reset();

            // リバーブ用のコム／オールパス長（Freeverb系の素数長をレート比で調整）
            const int combLen[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
            const int apLen[4]   = { 556, 441, 341, 225 };
            float scale = (float) sr / 44100.0f;

            for (int i = 0; i < 8; ++i)
            {
                combsL[i].prepare ((int) (combLen[i] * scale));
                combsR[i].prepare ((int) ((combLen[i] + 23) * scale));   // 左右をずらしてステレオ感
            }

            for (int i = 0; i < 4; ++i)
            {
                diffL[i].prepare ((int) (apLen[i] * scale));
                diffR[i].prepare ((int) ((apLen[i] + 11) * scale));
            }

            clear();
        }

        void clear()
        {
            dlL.clear(); dlR.clear();
            for (auto& a : apL) a.reset();
            for (auto& a : apR) a.reset();
            for (auto& c : combsL) c.clear();
            for (auto& c : combsR) c.clear();
            for (auto& d : diffL) d.clear();
            for (auto& d : diffR) d.clear();
            fbL = fbR = 0.0f;
        }

        void setType (FXType t) { type = t; }

        /** 4つの共通つまみ。**すべて0..1**で受け取り、タイプごとに読み替える。 */
        void setParams (float rate, float depth, float mixAmount, float feedback)
        {
            p1 = std::clamp (rate, 0.0f, 1.0f);
            p2 = std::clamp (depth, 0.0f, 1.0f);
            mix = std::clamp (mixAmount, 0.0f, 1.0f);
            fbAmt = std::clamp (feedback, 0.0f, 1.0f);
        }

        void process (float& l, float& r)
        {
            if (type == FXType::Off || mix <= 0.0001f) return;

            float dryL = l, dryR = r;
            float wetL = l, wetR = r;

            switch (type)
            {
                case FXType::Chorus:   processChorus (wetL, wetR);   break;
                case FXType::Flanger:  processFlanger (wetL, wetR);  break;
                case FXType::Phaser:   processPhaser (wetL, wetR);   break;
                case FXType::Tremolo:  processTremolo (wetL, wetR);  break;
                case FXType::Delay:    processDelay (wetL, wetR);    break;
                case FXType::Reverb:   processReverb (wetL, wetR);   break;
                default: break;
            }

            l = dryL * (1.0f - mix) + wetL * mix;
            r = dryR * (1.0f - mix) + wetR * mix;
        }

    private:
        float lfoTick (float rateHz)
        {
            lfoPhase += rateHz / sampleRate;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
            return std::sin (kTwoPi * lfoPhase);
        }

        /** Chorus：基準15ms付近をLFOで揺らす。左右で逆位相にして広げる。 */
        void processChorus (float& l, float& r)
        {
            float rate = 0.05f + p1 * 4.0f;              // 0.05〜4Hz
            float depthMs = 1.0f + p2 * 8.0f;            // 1〜9ms
            float m = lfoTick (rate);
            float baseMs = 15.0f;
            float dL = (baseMs + depthMs * m)    * sampleRate * 0.001f;
            float dR = (baseMs + depthMs * (-m)) * sampleRate * 0.001f;

            dlL.write (l); dlR.write (r);
            l = dlL.read (dL);
            r = dlR.read (dR);
        }

        /** Tremolo：音量をLFOで揺らす。 */
        void processTremolo (float& l, float& r)
        {
            float rate = 0.1f + p1 * 16.0f;              // 0.1〜16Hz
            float m = lfoTick (rate) * 0.5f + 0.5f;      // 0..1
            float g = 1.0f - p2 * m;
            l *= g; r *= g;
        }

        /** Flanger：基準3ms付近。フィードバックで強いうねりを作る。 */
        void processFlanger (float& l, float& r)
        {
            float rate = 0.05f + p1 * 3.0f;
            float fb = fbAmt * 0.85f;
            float m = lfoTick (rate);
            float baseMs = 3.0f, depthMs = 1.0f + p2 * 3.0f;
            float dL = (baseMs + depthMs * m)    * sampleRate * 0.001f;
            float dR = (baseMs + depthMs * (-m)) * sampleRate * 0.001f;

            dlL.write (l + fbL * fb);
            dlR.write (r + fbR * fb);
            l = dlL.read (dL); r = dlR.read (dR);
            fbL = l; fbR = r;
        }

        /** Phaser：4段オールパスをLFOで掃引する。 */
        void processPhaser (float& l, float& r)
        {
            float rate = 0.05f + p1 * 4.0f;
            float m = lfoTick (rate) * 0.5f + 0.5f;      // 0..1
            float depth = 0.2f + p2 * 0.75f;
            float freq = 200.0f * std::pow (2.0f, m * 5.0f);   // 200Hz〜6.4kHz
            float w = kPi * std::clamp (freq, 20.0f, sampleRate * 0.45f) / sampleRate;
            float a = (1.0f - std::tan (w)) / (1.0f + std::tan (w));
            float fb = fbAmt * 0.7f;
            float outL = l + fbL * fb, outR = r + fbR * fb;

            for (int i = 0; i < 4; ++i) { apL[i].setCoeff (a); outL = apL[i].process (outL); }
            for (int i = 0; i < 4; ++i) { apR[i].setCoeff (a); outR = apR[i].process (outR); }

            fbL = outL; fbR = outR;
            l = l + outL * depth;   // 原音と混ぜてノッチを作る
            r = r + outR * depth;
        }

        /** Delay：フィードバック付き。**Rateが高いほど短い間隔**（速い繰り返し）。 */
        void processDelay (float& l, float& r)
        {
            float timeMs = 30.0f + (1.0f - p1) * 970.0f;   // 1000〜30ms
            float fb = fbAmt * 0.85f;
            float dS = timeMs * sampleRate * 0.001f;
            float spread = 1.0f + p2 * 0.08f;              // Depthで左右の時間差
            float outL = dlL.read (dS);
            float outR = dlR.read (dS * spread);

            dlL.write (l + outL * fb);
            dlR.write (r + outR * fb);
            l = outL; r = outR;
        }

        /** Reverb：コム8＋オールパス4のシュレーダー型。 */
        void processReverb (float& l, float& r)
        {
            float size = 0.7f + fbAmt * 0.28f;    // Feedbackで残響の長さ
            float damp = 0.1f + p2 * 0.8f;        // Depthで高域減衰
            float inL = l * 0.35f, inR = r * 0.35f;
            float accL = 0.0f, accR = 0.0f;

            for (int i = 0; i < 8; ++i)
            {
                accL += combsL[i].process (inL, size, damp);
                accR += combsR[i].process (inR, size, damp);
            }

            accL *= 0.125f; accR *= 0.125f;

            for (int i = 0; i < 4; ++i)
            {
                accL = diffL[i].process (accL);
                accR = diffR[i].process (accR);
            }

            l = accL; r = accR;
        }

        float sampleRate = 44100.0f;
        FXType type = FXType::Off;
        float p1 = 0.5f, p2 = 0.5f, mix = 0.0f, fbAmt = 0.3f;
        float lfoPhase = 0.0f;
        float fbL = 0.0f, fbR = 0.0f;

        DelayLine dlL, dlR;
        AllpassStage apL[4], apR[4];
        CombFilter combsL[8], combsR[8];
        AllpassDiffuser diffL[4], diffR[4];
    };
}
