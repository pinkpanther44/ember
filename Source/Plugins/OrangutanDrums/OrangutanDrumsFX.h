#pragma once
// =============================================================================
//  8.288：**Orangutan Drums のマスター段**（Phase 281）。
//
//  `MAGAZINE`の`DrumFX.h`です。並びは
//
//      DRIVE ─▶ GLUE COMP ─▶ REVERB(send) ─▶ ソフトリミッタ
//
//  **リバーブは送りです**（パッドごとの`SEND`つまみで送ります）。
//
//  > **`Manta Reverb`があるのに、なぜ中にも入れるのか。**
//  > ドラムは**パッドごとに送り量を変える**もので、後段に1つ挿しても
//  > 「スネアだけ深く」ができません。ここのリバーブは、そのためのものです。
//
//  添字の扱いだけ、元のまま残してあります——**計算で範囲内が保証できても、
//  最後に必ず範囲へ収めること**（元の作者が踏んだところ）。
// =============================================================================
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

#include "OrangutanDrumsDSP.h"

namespace orangutan
{

// -----------------------------------------------------------------------------
//  整数ディレイのコムフィルタ（Freeverb系）
// -----------------------------------------------------------------------------
class Comb
{
public:
    void prepare (int lengthSamples)
    {
        len = std::max (4, lengthSamples);
        buf.assign ((size_t) len, 0.0f);
        idx = 0;
        store = 0.0f;
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        idx = 0; store = 0.0f;
    }

    inline float process (float in, float feedback, float damp) noexcept
    {
        if (buf.empty()) return 0.0f;
        if (idx < 0 || idx >= len) idx = 0;                 // 保険：必ず範囲内

        const float out = buf[(size_t) idx];
        store = out * (1.0f - damp) + store * damp;
        if (std::abs (store) < 1.0e-20f) store = 0.0f;

        float v = in + store * feedback;
        if (! std::isfinite (v)) v = 0.0f;                  // 発散した場合の遮断
        buf[(size_t) idx] = clampf (v, -4.0f, 4.0f);

        if (++idx >= len) idx = 0;
        return out;
    }

private:
    std::vector<float> buf;
    int   len = 4, idx = 0;
    float store = 0.0f;
};

// -----------------------------------------------------------------------------
//  オールパス
// -----------------------------------------------------------------------------
class Allpass
{
public:
    void prepare (int lengthSamples)
    {
        len = std::max (4, lengthSamples);
        buf.assign ((size_t) len, 0.0f);
        idx = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); idx = 0; }

    inline float process (float in) noexcept
    {
        if (buf.empty()) return in;
        if (idx < 0 || idx >= len) idx = 0;

        const float bufout = buf[(size_t) idx];
        const float out    = -in + bufout;
        float v = in + bufout * 0.5f;
        if (! std::isfinite (v)) v = 0.0f;
        buf[(size_t) idx] = clampf (v, -4.0f, 4.0f);

        if (++idx >= len) idx = 0;
        return out;
    }

private:
    std::vector<float> buf;
    int len = 4, idx = 0;
};

// -----------------------------------------------------------------------------
//  ステレオリバーブ（4コム + 2オールパス / ch）
// -----------------------------------------------------------------------------
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double k = sampleRate / 44100.0;
        static const int combLen[4] = { 1116, 1188, 1277, 1356 };
        static const int apLen[2]   = { 556, 441 };
        const int stereoSpread = (int) (23 * k);

        for (int i = 0; i < 4; ++i)
        {
            combL[i].prepare ((int) (combLen[i] * k));
            combR[i].prepare ((int) (combLen[i] * k) + stereoSpread);
        }
        for (int i = 0; i < 2; ++i)
        {
            apL[i].prepare ((int) (apLen[i] * k));
            apR[i].prepare ((int) (apLen[i] * k) + stereoSpread);
        }
        preL.set (180.0f, 0.7f, sr);
        preR.set (180.0f, 0.7f, sr);
        reset();
    }

    void reset()
    {
        for (auto& c : combL) c.reset();
        for (auto& c : combR) c.reset();
        for (auto& a : apL)   a.reset();
        for (auto& a : apR)   a.reset();
        preL.reset(); preR.reset();
    }

    // size 0..1 : 残響長 / damp 0..1 : 高域減衰
    void setParams (float sizeNorm, float dampNorm) noexcept
    {
        feedback = 0.72f + 0.22f * clampf (sizeNorm, 0.0f, 1.0f);   // RT60 約0.6〜3.2秒
        damp     = 0.15f + 0.65f * clampf (dampNorm, 0.0f, 1.0f);
        // コムの定常ゲインは 1/(1-feedback)。入力側で打ち消して
        // SIZEを回しても音量が変わらないようにする（前は fb=0.98 で約50倍になっていた）
        inScale  = 0.125f * (1.0f - feedback);
    }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        // 低域は残響に入れない（キックが濁るのを防ぐ）
        const float xl = preL.highpass (inL) * inScale;
        const float xr = preR.highpass (inR) * inScale;

        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            l += combL[i].process (xl, feedback, damp);
            r += combR[i].process (xr, feedback, damp);
        }
        for (int i = 0; i < 2; ++i)
        {
            l = apL[i].process (l);
            r = apR[i].process (r);
        }
        outL = sanitise (l);
        outR = sanitise (r);
    }

private:
    double sr = 48000.0;
    Comb    combL[4], combR[4];
    Allpass apL[2], apR[2];
    SVF     preL, preR;
    float   feedback = 0.84f, damp = 0.4f, inScale = 0.02f;
};

// -----------------------------------------------------------------------------
//  グルーコンプレッサ（ピーク検出 / 固定レシオ4:1 / オートメイクアップ）
// -----------------------------------------------------------------------------
class GlueComp
{
public:
    void prepare (double sampleRate)
    {
        sr  = sampleRate;
        atk = std::exp (-1.0f / (float) (0.006 * sr));   // 6 ms
        rel = std::exp (-1.0f / (float) (0.120 * sr));   // 120 ms
        env = 0.0f;
    }

    void reset() noexcept { env = 0.0f; }

    // amount 0..1 : 0で無効、上げるほど深く掛かる
    void setAmount (float amount) noexcept
    {
        amt       = clampf (amount, 0.0f, 1.0f);
        threshold = std::pow (10.0f, (-1.0f - 17.0f * amt) / 20.0f);  // -1dB .. -18dB
        makeup    = std::pow (10.0f, (6.0f * amt) / 20.0f);
    }

    inline void process (float& l, float& r) noexcept
    {
        if (amt <= 0.0001f) return;

        const float det = std::max (std::abs (l), std::abs (r));
        const float c   = (det > env) ? atk : rel;
        env = det + (env - det) * c;
        if (env < 1.0e-20f) env = 0.0f;

        float gain = 1.0f;
        if (env > threshold)
        {
            const float over = env / threshold;
            gain = std::pow (over, -0.75f);              // ratio 4:1
        }
        gain *= makeup;
        l = sanitise (l * gain);
        r = sanitise (r * gain);
    }

private:
    double sr = 48000.0;
    float  atk = 0.0f, rel = 0.0f, env = 0.0f;
    float  amt = 0.0f, threshold = 1.0f, makeup = 1.0f;
};

// -----------------------------------------------------------------------------
//  マスターチェイン
// -----------------------------------------------------------------------------
class MasterFX
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reverb.prepare (sampleRate);
        comp.prepare (sampleRate);
        toneL.set (12000.0f, 0.7f, sr);
        toneR.set (12000.0f, 0.7f, sr);
        dcL.prepare (sampleRate);
        dcR.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        reverb.reset(); comp.reset();
        toneL.reset(); toneR.reset();
        dcL.reset(); dcR.reset();
    }

    void setParams (float driveNorm, float compNorm,
                    float reverbMix, float reverbSize, float reverbDamp) noexcept
    {
        drive     = 1.0f + 11.0f * clampf (driveNorm, 0.0f, 1.0f);
        driveNorm_= clampf (driveNorm, 0.0f, 1.0f);
        driveComp = 1.0f / std::tanh (drive);
        comp.setAmount (compNorm);
        revMix = clampf (reverbMix, 0.0f, 1.0f);
        reverb.setParams (reverbSize, reverbDamp);
    }

    // dry: 本線 / send: リバーブ送り
    inline void process (float& l, float& r, float sendL, float sendR) noexcept
    {
        // --- ドライブ（テープ風の非対称サチュレーション） ---
        if (driveNorm_ > 0.0001f)
        {
            const float dl = std::tanh (l * drive) * driveComp;
            const float dr = std::tanh (r * drive) * driveComp;
            l = lerpf (l, dl, driveNorm_);
            r = lerpf (r, dr, driveNorm_);
            l = toneL.lowpass (l);
            r = toneR.lowpass (r);
        }

        // --- リバーブ ---
        if (revMix > 0.0001f)
        {
            float wl = 0.0f, wr = 0.0f;
            reverb.process (sendL, sendR, wl, wr);
            l += wl * revMix * 1.4f;
            r += wr * revMix * 1.4f;
        }

        // --- グルーコンプ ---
        comp.process (l, r);

        // --- DC除去 + ソフトリミッタ ---
        l = dcL.process (l);
        r = dcR.process (r);
        l = softClip (l);
        r = softClip (r);
    }

private:
    static inline float softClip (float x) noexcept
    {
        if (! std::isfinite (x)) return 0.0f;
        if (x >  1.6f) x =  1.6f;
        if (x < -1.6f) x = -1.6f;
        return x - (x * x * x) * (1.0f / 6.75f);   // 約±0.99でなめらかに頭打ち
    }

    double  sr = 48000.0;
    Reverb  reverb;
    GlueComp comp;
    SVF     toneL, toneR;
    DCBlock dcL, dcR;
    float   drive = 1.0f, driveComp = 1.0f, driveNorm_ = 0.0f, revMix = 0.0f;
};

} // namespace orangutan
