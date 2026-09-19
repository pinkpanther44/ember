#pragma once
// =============================================================================
//  8.288：**Orangutan Drums の音を作るところ**（Phase 281／本人の要望）。
//
//  本人が別に作った`MAGAZINE`（16パッドのドラムシンセ）の`DrumDSP.h`です。
//  **シークエンサは持って来ていません**（本人の指定）——本体にピアノロールと
//  ドラムエディタがあるので、プラグインの中にもう1つ置く理由がありません。
//
//  `Racco Guitar`・`Java Rhino Bass`と同じで、**JUCEに依存させていません**。
//  g++単体でコンパイルでき、そのまま数値で測れます。
//
//  ### 16のエンジン
//
//  TR-808を土台に、パッドごとに好きなエンジンを割り当てます。
//  `KICK` `SUB 808` `SNARE` `CLAP` `RIM` `TOM` `CONGA` `CL HAT` `OP HAT`
//  `CYMBAL` `COWBELL` `SHAKER` `SNAP` `ZAP` `NOISE FX` `STICK`。
//
//  **`CL HAT`と`OP HAT`は同じチョークグループ**（実機どおり、クローズが
//  オープンを止めます）。`engineChokeGroup()`が唯一の出どころです。
//
//  ### 乱数はトリガごとに戻します
//
//  `trigger()`が`rng.seed(baseSeed)`を呼びます。**同じ設定なら毎回同じ波形**に
//  なるので、書き出しが再現でき、前後比較にも意味が出ます
//  （8.269でベースの大きさを測り直したときの話と同じ）。
// =============================================================================
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace orangutan
{

static constexpr float kPi    = 3.14159265358979f;
static constexpr float kTwoPi = 6.28318530717959f;

// 数値の健全性ガード。NaN/Infが出たら0にする（発散を後段へ流さない）
inline float sanitise (float v) noexcept
{
    return (std::isfinite (v)) ? v : 0.0f;
}

inline float clampf (float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf (float a, float b, float t) noexcept { return a + (b - a) * t; }

// 0..1 を min..max へ指数マップ（時間・周波数はこれが自然）
inline float expMap (float t, float lo, float hi) noexcept
{
    return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f));
}

inline float semitoneRatio (float semis) noexcept { return std::pow (2.0f, semis * (1.0f / 12.0f)); }

// -----------------------------------------------------------------------------
//  乱数（xorshift32）
// -----------------------------------------------------------------------------
struct Rng
{
    uint32_t s = 0x9E3779B9u;

    inline void seed (uint32_t v) noexcept { s = (v == 0u ? 1u : v); }

    inline float white() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * (1.0f / 2147483648.0f);   // -1..1
    }
};

// -----------------------------------------------------------------------------
//  AD エンベロープ（短い線形アタック + 指数減衰）
//  decaySec は -60dB に達するまでの時間（T60）として正規化する。
// -----------------------------------------------------------------------------
struct AD
{
    float value = 0.0f, decayCoef = 0.0f, attackStep = 1.0f, attackVal = 0.0f;
    bool  active = false;

    void trigger (float decaySec, float attackSec, double sr) noexcept
    {
        value      = 1.0f;
        attackVal  = 0.0f;
        const float aSamples = (float) (attackSec * sr);
        attackStep = (aSamples < 1.0f) ? 1.0f : (1.0f / aSamples);
        const float dSamples = std::max (1.0f, (float) (decaySec * sr));
        decayCoef  = std::exp (-6.907755f / dSamples);          // ln(0.001)
        active     = true;
    }

    void reset() noexcept { value = 0.0f; attackVal = 0.0f; active = false; }

    inline float next() noexcept
    {
        if (! active) return 0.0f;
        if (attackVal < 1.0f) { attackVal += attackStep; if (attackVal > 1.0f) attackVal = 1.0f; }
        const float out = value * attackVal;
        value *= decayCoef;
        if (value < 1.0e-5f) active = false;
        return out;
    }
};

// -----------------------------------------------------------------------------
//  State Variable Filter (TPT / Cytomic 系)。安定でモジュレーション耐性が高い。
// -----------------------------------------------------------------------------
struct SVF
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void set (float freqHz, float q, double sr) noexcept
    {
        const float nyq = (float) (sr * 0.49);
        const float f   = clampf (freqHz, 10.0f, nyq);
        g  = std::tan (kPi * f / (float) sr);
        k  = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // lp / bp / hp を同時に得る
    inline void process (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        // デノーマル対策（Linuxテスト時にCPUを食わないように）
        if (std::abs (ic1) < 1.0e-20f) ic1 = 0.0f;
        if (std::abs (ic2) < 1.0e-20f) ic2 = 0.0f;
        lp = v2;
        bp = v1;
        hp = v0 - k * v1 - v2;
    }

    inline float lowpass  (float x) noexcept { float l, b, h; process (x, l, b, h); return l; }
    inline float bandpass (float x) noexcept { float l, b, h; process (x, l, b, h); return b; }
    inline float highpass (float x) noexcept { float l, b, h; process (x, l, b, h); return h; }

    // 共振点のゲインはQに比例する。k(=1/Q)を掛けてユニティゲイン化する。
    // これをやらないとQを上げたときだけ音量が跳ね上がる。
    inline float bandpassUnity (float x) noexcept { return bandpass (x) * k; }
};

// -----------------------------------------------------------------------------
//  DCブロッカ（キックのピッチエンベロープはDCを生みやすい）
// -----------------------------------------------------------------------------
struct DCBlock
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.9985f;
    void prepare (double sr) noexcept { r = 1.0f - (kTwoPi * 12.0f / (float) sr); }
    void reset() noexcept { x1 = y1 = 0.0f; }
    inline float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = y;
        if (std::abs (y1) < 1.0e-20f) y1 = 0.0f;
        return y;
    }
};

// -----------------------------------------------------------------------------
//  エンジン定義
// -----------------------------------------------------------------------------
enum Engine
{
    ENG_KICK = 0, ENG_SUB, ENG_SNARE, ENG_CLAP, ENG_RIM, ENG_TOM, ENG_CONGA,
    ENG_CHH, ENG_OHH, ENG_CYMBAL, ENG_COWBELL, ENG_SHAKER, ENG_SNAP,
    ENG_ZAP, ENG_NOISE, ENG_STICK,
    ENG_COUNT
};

// UIに出す文字列はASCIIのみ（手引き 3-7）
inline const char* engineName (int e) noexcept
{
    static const char* names[ENG_COUNT] = {
        "KICK", "SUB 808", "SNARE", "CLAP", "RIM", "TOM", "CONGA",
        "CL HAT", "OP HAT", "CYMBAL", "COWBELL", "SHAKER", "SNAP",
        "ZAP", "NOISE FX", "STICK"
    };
    return (e >= 0 && e < ENG_COUNT) ? names[e] : "----";
}

// チョークグループ。同じ番号(>0)の音は互いを止める。0はチョークしない。
inline int engineChokeGroup (int e) noexcept
{
    if (e == ENG_CHH || e == ENG_OHH) return 1;   // 実機どおりクローズがオープンを止める
    return 0;
}

// 各エンジンのディケイ範囲（秒, T60）
struct DecayRange { float lo, hi; };
inline DecayRange engineDecayRange (int e) noexcept
{
    switch (e)
    {
        case ENG_KICK:    return { 0.060f, 2.20f };
        case ENG_SUB:     return { 0.080f, 5.00f };
        case ENG_SNARE:   return { 0.040f, 1.20f };
        case ENG_CLAP:    return { 0.060f, 1.60f };
        case ENG_RIM:     return { 0.015f, 0.35f };
        case ENG_TOM:     return { 0.060f, 2.00f };
        case ENG_CONGA:   return { 0.040f, 1.20f };
        case ENG_CHH:     return { 0.015f, 0.40f };
        case ENG_OHH:     return { 0.060f, 2.00f };
        case ENG_CYMBAL:  return { 0.200f, 5.00f };
        case ENG_COWBELL: return { 0.060f, 1.50f };
        case ENG_SHAKER:  return { 0.015f, 0.45f };
        case ENG_SNAP:    return { 0.040f, 0.90f };
        case ENG_ZAP:     return { 0.030f, 1.20f };
        case ENG_NOISE:   return { 0.150f, 4.00f };
        case ENG_STICK:   return { 0.006f, 0.18f };
        default:          return { 0.050f, 1.00f };
    }
}

// エンジン既定の基準周波数（Hz）。TUNEはこれに対する半音オフセット。
inline float engineBaseHz (int e) noexcept
{
    switch (e)
    {
        case ENG_KICK:    return 50.0f;
        case ENG_SUB:     return 41.20f;   // E1
        case ENG_SNARE:   return 185.0f;
        case ENG_TOM:     return 110.0f;
        case ENG_CONGA:   return 220.0f;
        case ENG_RIM:     return 1700.0f;
        case ENG_COWBELL: return 540.0f;
        case ENG_ZAP:     return 220.0f;
        default:          return 1.0f;     // 比率として使うエンジン
    }
}

// TR-808 ハイハット/シンバルの6基のスクエア発振器（実機由来の周波数）
static const float kMetalHz[6] = { 205.3f, 369.6f, 304.4f, 522.7f, 540.0f, 800.0f };

// -----------------------------------------------------------------------------
//  ボイスのパラメータ束（トリガ時にスナップショットする）
// -----------------------------------------------------------------------------
struct VoiceParams
{
    int   engine = ENG_KICK;
    float tune   = 0.0f;    // -24..+24 semitone
    float decay  = 0.5f;    // 0..1
    float tone   = 0.5f;    // 0..1
    float snap   = 0.5f;    // 0..1
    float level  = 0.80f;   // 0..1.5
    float pan    = 0.0f;    // -1..1
    float send   = 0.0f;    // 0..1  (リバーブへ)
};

// -----------------------------------------------------------------------------
//  ドラムボイス本体
// -----------------------------------------------------------------------------
class DrumVoice
{
public:
    void prepare (double sampleRate, uint32_t seed) noexcept
    {
        sr = sampleRate;
        baseSeed = (seed == 0u ? 1u : seed);
        rng.seed (baseSeed);
        dc.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        ampEnv.reset(); pitchEnv.reset(); subEnv.reset(); clickEnv.reset();
        for (auto& b : burstEnv) b.reset();
        for (auto& p : phase) p = 0.0f;
        f1.reset(); f2.reset(); dc.reset();
        active = false; chokeGain = 1.0f; chokeStep = 0.0f;
        elapsed = 0;
    }

    bool  isActive()   const noexcept { return active; }
    int   chokeGroup() const noexcept { return engineChokeGroup (prm.engine); }
    float sendAmount() const noexcept { return prm.send; }

    // pitchHz > 0 のときはその周波数で鳴らす（SUB 808 のメロディック演奏用）
    void trigger (const VoiceParams& p, float velocity, float pitchHz) noexcept
    {
        prm      = p;
        vel      = clampf (velocity, 0.0f, 1.0f);
        const float velCurve = 0.15f + 0.85f * vel * vel;   // 弱打をしっかり弱く
        amp      = velCurve * clampf (p.level, 0.0f, 1.5f);
        elapsed  = 0;
        chokeGain = 1.0f; chokeStep = 0.0f;
        glideActive = false;
        // 乱数をトリガごとに戻す。同じ設定なら毎回同じ波形になり、
        // DAWでのバウンスが再現可能になる（テストの前後比較も意味を持つ）
        rng.seed (baseSeed);

        const DecayRange dr = engineDecayRange (p.engine);
        decaySec = expMap (p.decay, dr.lo, dr.hi);

        const float ratio = semitoneRatio (clampf (p.tune, -24.0f, 24.0f));
        baseHz     = (pitchHz > 0.0f ? pitchHz : engineBaseHz (p.engine)) * ratio;
        metalScale = ratio;
        tuneRatio  = ratio;

        for (auto& ph : phase) ph = 0.0f;
        f1.reset(); f2.reset(); dc.reset();
        initEngine();
        active = true;
    }

    // 808ベースのスライド（ポルタメント）。エンベロープを再トリガせず音程だけ動かす。
    void glideTo (float hz, float glideMs) noexcept
    {
        if (hz <= 0.0f) return;
        glideTarget = hz;
        if (glideMs < 0.5f || baseHz <= 0.0f)
        {
            baseHz = hz;
            glideActive = false;
            return;
        }
        const float n = std::max (1.0f, (float) (glideMs * 0.001 * sr));
        glideRate   = std::pow (glideTarget / baseHz, 1.0f / n);   // 指数グライド（音楽的）
        glideActive = (std::abs (glideRate - 1.0f) > 1.0e-7f);
        if (! glideActive) baseHz = hz;
    }

    // 即座に止める（チョークグループ用）。クリック防止に短いフェード。
    void choke (float fadeMs = 4.0f) noexcept
    {
        if (! active) return;
        const float n = std::max (1.0f, (float) (fadeMs * 0.001 * sr));
        chokeStep = 1.0f / n;
    }

    // 加算レンダリング。sendL/R には prm.send を掛けた成分を足す。
    void renderAdd (float* outL, float* outR, float* sendL, float* sendR, int numSamples) noexcept
    {
        if (! active) return;

        // 等パワーパン
        const float p    = clampf (prm.pan, -1.0f, 1.0f) * 0.5f + 0.5f;
        const float gL   = std::cos (p * kPi * 0.5f);
        const float gR   = std::sin (p * kPi * 0.5f);
        const float sAmt = clampf (prm.send, 0.0f, 1.0f);

        for (int i = 0; i < numSamples; ++i)
        {
            float s = sanitise (tick()) * amp;

            if (chokeStep > 0.0f)
            {
                chokeGain -= chokeStep;
                if (chokeGain <= 0.0f) { chokeGain = 0.0f; active = false; }
                s *= chokeGain;
            }

            outL[i] += s * gL;
            outR[i] += s * gR;
            if (sAmt > 0.0f) { sendL[i] += s * gL * sAmt; sendR[i] += s * gR * sAmt; }

            ++elapsed;
            if (! active) break;
        }
    }

private:
    // ---- 内部状態 ----
    double sr = 48000.0;
    Rng    rng;
    VoiceParams prm;
    bool   active = false;
    float  amp = 0.0f, vel = 1.0f;
    float  decaySec = 0.2f, baseHz = 50.0f, metalScale = 1.0f, tuneRatio = 1.0f;
    uint32_t baseSeed = 1u;
    float  chokeGain = 1.0f, chokeStep = 0.0f;
    float  glideTarget = 0.0f, glideRate = 1.0f;
    bool   glideActive = false;
    int64_t elapsed = 0;

    AD ampEnv, pitchEnv, subEnv, clickEnv, burstEnv[3];
    SVF f1, f2;
    DCBlock dc;
    float phase[6] = { 0, 0, 0, 0, 0, 0 };

    // エンジン別の派生値
    float sweepStart = 0.0f, sweepEnd = 0.0f, sweepCoef = 0.0f, sweepVal = 0.0f;
    int   burstAt[3] = { 0, 0, 0 };
    int   burstFired = 0;
    float toneMixA = 0.0f, toneMixB = 0.0f;

    inline float phaseInc (float hz) const noexcept { return hz / (float) sr; }

    // フィルタ周波数にもTUNEを反映させる。
    // これが無いとノイズ系エンジン(CLAP/SHAKER/SNAP/STICK/NOISE FX)でTUNEノブが無反応になる。
    // 極端なTUNEで可聴帯域外へ飛ばないよう上下限で挟む。
    inline float ftune (float hz) const noexcept { return clampf (hz * tuneRatio, 25.0f, 15000.0f); }

    inline float sineAt (int idx, float hz) noexcept
    {
        phase[idx] += phaseInc (hz);
        if (phase[idx] >= 1.0f) phase[idx] -= 1.0f;
        return std::sin (kTwoPi * phase[idx]);
    }

    inline float squareAt (int idx, float hz) noexcept
    {
        phase[idx] += phaseInc (hz);
        if (phase[idx] >= 1.0f) phase[idx] -= 1.0f;
        return (phase[idx] < 0.5f) ? 1.0f : -1.0f;
    }

    // 6基のメタリック発振器の和（ハット/シンバル/カウベル共用）
    inline float metallic (int count) noexcept
    {
        float sum = 0.0f;
        for (int k = 0; k < count; ++k)
            sum += squareAt (k, kMetalHz[k] * metalScale);
        return sum * (1.0f / (float) count);
    }

    // -------------------------------------------------------------------------
    void initEngine() noexcept
    {
        const float tone = clampf (prm.tone, 0.0f, 1.0f);
        const float snap = clampf (prm.snap, 0.0f, 1.0f);
        burstFired = 0;

        switch (prm.engine)
        {
            case ENG_KICK:
                ampEnv  .trigger (decaySec, 0.0015f, sr);
                pitchEnv.trigger (0.018f + 0.030f * (1.0f - snap), 0.0f, sr);
                clickEnv.trigger (0.0035f, 0.0002f, sr);
                f1.set (ftune (expMap (tone, 1200.0f, 7000.0f)), 0.8f, sr);   // クリック用BPF
                break;

            case ENG_SUB:
                ampEnv  .trigger (decaySec, 0.0030f, sr);
                pitchEnv.trigger (0.020f, 0.0f, sr);
                clickEnv.trigger (0.0030f, 0.0002f, sr);
                f1.set (ftune (expMap (tone, 900.0f, 4500.0f)), 0.9f, sr);
                break;

            case ENG_SNARE:
                ampEnv  .trigger (decaySec, 0.0010f, sr);                    // ノイズ側
                subEnv  .trigger (std::min (decaySec, 0.16f), 0.0008f, sr);  // トーン側
                f1.set (ftune (expMap (tone, 700.0f, 6500.0f)), 0.9f, sr);
                f2.set (ftune (expMap (tone, 250.0f, 900.0f)), 0.7f, sr);
                break;

            case ENG_CLAP:
            {
                const float spread = 0.0055f + 0.0130f * (1.0f - snap);
                for (int k = 0; k < 3; ++k)
                    burstAt[k] = (int) (spread * k * sr);
                burstEnv[0].trigger (0.0045f, 0.0003f, sr);
                burstFired = 1;
                ampEnv.reset();                                   // ボディは3発目の後に起動
                f1.set (ftune (expMap (tone, 550.0f, 2600.0f)), 1.4f, sr);
                f2.set (ftune (expMap (tone, 1400.0f, 5000.0f)), 0.8f, sr);
                break;
            }

            case ENG_RIM:
                ampEnv  .trigger (decaySec, 0.0002f, sr);
                clickEnv.trigger (0.0012f, 0.0001f, sr);
                f1.set (baseHz, 2.0f + 8.0f * tone, sr);
                f2.set (baseHz * 0.31f, 1.5f + 4.0f * tone, sr);
                break;

            case ENG_TOM:
            case ENG_CONGA:
                ampEnv  .trigger (decaySec, 0.0012f, sr);
                pitchEnv.trigger (prm.engine == ENG_TOM ? 0.070f : 0.030f, 0.0f, sr);
                clickEnv.trigger (0.0050f, 0.0003f, sr);
                f1.set (ftune (expMap (tone, 900.0f, 6000.0f)), 0.8f, sr);
                break;

            case ENG_CHH:
            case ENG_OHH:
                ampEnv  .trigger (decaySec, 0.0006f, sr);
                clickEnv.trigger (0.0025f, 0.0001f, sr);
                f1.set (ftune (expMap (tone, 3800.0f, 11000.0f)), 0.7f, sr);   // HPF
                f2.set (ftune (expMap (tone, 7000.0f, 13000.0f)), 0.8f, sr);   // 上の共振
                break;

            case ENG_CYMBAL:
                ampEnv  .trigger (decaySec, 0.0020f, sr);
                clickEnv.trigger (0.0060f, 0.0002f, sr);
                f1.set (ftune (expMap (tone, 1800.0f, 7000.0f)), 0.7f, sr);
                f2.set (ftune (expMap (tone, 6000.0f, 12000.0f)), 0.6f, sr);
                break;

            case ENG_COWBELL:
                ampEnv  .trigger (decaySec, 0.0008f, sr);
                clickEnv.trigger (0.0030f, 0.0001f, sr);   // SNAP用のアタック
                f1.set (ftune (expMap (tone, 1200.0f, 4200.0f)), 1.1f, sr);
                break;

            case ENG_SHAKER:
                ampEnv  .trigger (decaySec, 0.0010f + 0.0060f * (1.0f - snap), sr);
                f1.set (ftune (expMap (tone, 3500.0f, 11000.0f)), 0.8f, sr);
                break;

            case ENG_SNAP:
                ampEnv  .trigger (decaySec, 0.0012f, sr);
                clickEnv.trigger (0.0035f, 0.0002f, sr);
                f1.set (ftune (expMap (tone, 1100.0f, 4200.0f)), 1.6f, sr);
                f2.set (ftune (expMap (tone, 2600.0f, 8000.0f)), 0.9f, sr);
                break;

            case ENG_ZAP:
            {
                ampEnv.trigger (decaySec, 0.0010f, sr);
                const float octaves = 1.0f + 5.0f * snap;
                sweepStart = baseHz * expMap (tone, 3.0f, 14.0f);
                sweepEnd   = std::max (25.0f, sweepStart / std::pow (2.0f, octaves));
                sweepVal   = sweepStart;
                sweepCoef  = std::exp (-1.0f / std::max (1.0f, (float) (decaySec * 0.35 * sr)));
                f1.set (ftune (expMap (tone, 2000.0f, 9000.0f)), 1.2f, sr);
                break;
            }

            case ENG_NOISE:
            {
                ampEnv.trigger (decaySec, 0.004f + 0.10f * (1.0f - snap), sr);
                const float dir = (snap - 0.5f) * 2.0f;             // -1:下降  +1:上昇
                const float depth = std::pow (24.0f, std::abs (dir));
                const float centre = ftune (expMap (tone, 400.0f, 3000.0f));
                if (dir >= 0.0f) { sweepStart = centre; sweepEnd = std::min (16000.0f, centre * depth); }
                else             { sweepStart = std::min (16000.0f, centre * depth); sweepEnd = centre; }
                sweepVal  = sweepStart;
                sweepCoef = std::exp (-1.0f / std::max (1.0f, (float) (decaySec * 0.45 * sr)));
                f1.set (sweepStart, 1.0f + 3.5f * tone, sr);
                break;
            }

            case ENG_STICK:
                ampEnv  .trigger (decaySec, 0.0002f, sr);
                clickEnv.trigger (0.0008f, 0.0001f, sr);
                f1.set (ftune (expMap (tone, 900.0f, 5200.0f)), 2.2f, sr);
                break;

            default:
                ampEnv.trigger (decaySec, 0.001f, sr);
                break;
        }

        toneMixA = tone;
        toneMixB = snap;
    }

    // -------------------------------------------------------------------------
    inline float tick() noexcept
    {
        const float tone = toneMixA;
        const float snap = toneMixB;
        float out = 0.0f;

        // グライド（808スライド用）
        if (glideActive)
        {
            baseHz *= glideRate;
            if ((glideRate > 1.0f && baseHz >= glideTarget)
             || (glideRate < 1.0f && baseHz <= glideTarget))
            {
                baseHz = glideTarget;
                glideActive = false;
            }
        }

        switch (prm.engine)
        {
            // ---- 808 バスドラム ---------------------------------------------
            case ENG_KICK:
            {
                const float pe = pitchEnv.next();
                const float f  = baseHz * (1.0f + (3.2f + 2.0f * snap) * pe);
                const float e  = ampEnv.next();
                float body = sineAt (0, f) * e;
                // TONE で倍音を足す（スマホでも聞こえる808にするため）
                const float drv = 1.0f + 7.0f * tone;
                body = std::tanh (body * drv) / std::tanh (drv);
                // アタックのクリック
                const float ck = f1.bandpass (rng.white()) * clickEnv.next() * snap * 1.4f;
                out = body * 1.05f + ck;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- 808 サブベース（音程演奏可） --------------------------------
            case ENG_SUB:
            {
                const float pe = pitchEnv.next();
                const float f  = baseHz * (1.0f + 0.9f * snap * pe);
                const float e  = ampEnv.next();
                float body = sineAt (0, f) * e;
                const float drv = 1.0f + 9.0f * tone;
                body = std::tanh (body * drv) / std::tanh (drv);
                const float ck = f1.bandpass (rng.white()) * clickEnv.next() * snap * 0.9f;
                out = body + ck;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- 808 スネア --------------------------------------------------
            case ENG_SNARE:
            {
                const float te = subEnv.next();
                const float ne = ampEnv.next();
                const float t1 = sineAt (0, baseHz)          * te;
                const float t2 = sineAt (1, baseHz * 1.78f)  * te * 0.6f;
                float noise = f1.bandpass (rng.white());
                float l, b, h;
                f2.process (noise, l, b, h);
                noise = h * 0.7f + noise * 0.6f;
                const float tonePart  = (t1 + t2) * (1.0f - 0.65f * snap);
                const float noisePart = noise * ne * (0.35f + 1.15f * snap);
                out = (tonePart + noisePart) * 0.9f;
                if (! ampEnv.active && ! subEnv.active) active = false;
                break;
            }

            // ---- クラップ ----------------------------------------------------
            case ENG_CLAP:
            {
                // 3連バーストを所定サンプルで順に起動する
                if (burstFired < 3 && elapsed >= burstAt[burstFired])
                {
                    burstEnv[burstFired].trigger (0.0045f, 0.0003f, sr);
                    ++burstFired;
                    if (burstFired == 3) ampEnv.trigger (decaySec, 0.0015f, sr);
                }
                float e = 0.0f;
                for (int k = 0; k < 3; ++k) e += burstEnv[k].next();
                e = e * 1.0f + ampEnv.next() * 0.85f;
                float noise = rng.white();
                const float b1 = f1.bandpassUnity (noise);
                float l, b2, h;
                f2.process (noise, l, b2, h);
                out = (b1 * 1.3f + b2 * 0.5f * tone) * e * 2.6f;
                if (burstFired == 3 && ! ampEnv.active
                    && ! burstEnv[0].active && ! burstEnv[1].active && ! burstEnv[2].active)
                    active = false;
                break;
            }

            // ---- リムショット ------------------------------------------------
            case ENG_RIM:
            {
                const float e  = ampEnv.next();
                // SNAP でノイズ成分とクリック成分の配合を変える
                const float ex = rng.white() * (0.10f + 0.32f * snap)
                               + clickEnv.next() * (1.35f - 0.60f * snap);
                const float r1 = f1.bandpassUnity (ex);
                const float r2 = f2.bandpassUnity (ex);
                out = (r1 * 1.2f + r2 * 0.55f) * e * 7.5f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- タム / コンガ -----------------------------------------------
            case ENG_TOM:
            case ENG_CONGA:
            {
                const float pe = pitchEnv.next();
                const float f  = baseHz * (1.0f + (prm.engine == ENG_TOM ? 0.85f : 0.35f) * pe);
                const float e  = ampEnv.next();
                float body = sineAt (0, f) * e;
                body += sineAt (1, f * 1.5f) * e * 0.18f * tone;
                const float ck = f1.bandpass (rng.white()) * clickEnv.next() * snap * 0.9f;
                out = body * 1.0f + ck;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- ハイハット（クローズ / オープン） ---------------------------
            case ENG_CHH:
            case ENG_OHH:
            {
                const float e   = ampEnv.next();
                const float src = metallic (6);
                const float hp  = f1.highpass (src);
                const float bp  = f2.bandpass (src);
                float sig = hp * 1.0f + bp * 0.5f;
                sig += rng.white() * 0.05f * tone;
                const float at = clickEnv.next() * snap * 0.6f;
                out = sig * (e + at) * 2.9f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- シンバル ----------------------------------------------------
            case ENG_CYMBAL:
            {
                const float e   = ampEnv.next();
                const float src = metallic (6) * 0.85f + rng.white() * 0.25f;
                const float hp  = f1.highpass (src);
                const float bp  = f2.bandpass (src);
                float sig = hp * 0.9f + bp * 0.6f;
                const float at = clickEnv.next() * snap * 0.5f;
                out = sig * (e + at) * 2.1f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- カウベル ----------------------------------------------------
            case ENG_COWBELL:
            {
                const float e  = ampEnv.next();
                const float s1 = squareAt (0, baseHz);
                const float s2 = squareAt (1, baseHz * 1.4815f);   // 540 : 800
                const float bp = f1.bandpass ((s1 + s2) * 0.5f);
                // SNAP はアタックの強さ。これを入れ忘れていて SNAP ノブが無反応だった
                const float at = clickEnv.next() * snap * 0.9f;
                out = (bp * 1.0f + (s1 + s2) * 0.10f * (1.0f - tone)) * (e + at) * 0.72f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- シェイカー --------------------------------------------------
            case ENG_SHAKER:
            {
                const float e  = ampEnv.next();
                const float hp = f1.highpass (rng.white());
                out = hp * e * 1.35f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- フィンガースナップ ------------------------------------------
            case ENG_SNAP:
            {
                const float e  = ampEnv.next() * 0.75f + clickEnv.next() * (0.6f + 0.8f * snap);
                const float n  = rng.white();
                const float b1 = f1.bandpassUnity (n);
                const float b2 = f2.bandpassUnity (n);
                out = (b1 * 1.3f + b2 * 0.7f) * e * 1.7f;
                if (! ampEnv.active && ! clickEnv.active) active = false;
                break;
            }

            // ---- ザップ（下降スイープ） --------------------------------------
            case ENG_ZAP:
            {
                sweepVal = sweepEnd + (sweepVal - sweepEnd) * sweepCoef;
                const float e = ampEnv.next();
                float body = sineAt (0, sweepVal);
                body = std::tanh (body * (1.0f + 4.0f * tone));
                const float n = f1.bandpass (rng.white()) * 0.25f * snap;
                out = (body + n) * e * 0.9f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- ノイズFX（ライザー / ダウンリフター） -------------------------
            case ENG_NOISE:
            {
                sweepVal = sweepEnd + (sweepVal - sweepEnd) * sweepCoef;
                // フィルタ係数の更新は32サンプルに1回で十分（tanが重いため）
                if ((elapsed & 31) == 0) f1.set (sweepVal, 1.0f + 3.5f * tone, sr);
                const float e = ampEnv.next();
                const float b = f1.bandpassUnity (rng.white());
                out = b * e * 4.0f;
                if (! ampEnv.active) active = false;
                break;
            }

            // ---- スティック / クリック ---------------------------------------
            case ENG_STICK:
            {
                const float e  = ampEnv.next() + clickEnv.next() * 1.2f;
                // SNAP でノイズ寄り/クリック寄りを切り替える
                const float bp = f1.bandpassUnity (rng.white() * (0.25f + 0.70f * snap)
                                                 + clickEnv.value * (1.10f - 0.50f * snap));
                out = bp * e * 3.0f;
                if (! ampEnv.active && ! clickEnv.active) active = false;
                break;
            }

            default:
                active = false;
                break;
        }

        return dc.process (clampf (out, -8.0f, 8.0f));
    }
};

} // namespace orangutan
