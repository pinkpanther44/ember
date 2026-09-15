#pragma once

#include "JavaRhinoBassString.h"   // bassdsp::kPi

#include <algorithm>
#include <cmath>
#include <random>

//==============================================================================
/**
    8.257：**奏法ごとの励起信号**（Phase 265）。`JBass5`の`Excitation.h`を移したものです。

    ### 3層の役割分担

    | | 何を決めるか |
    |---|---|
    | **Hardness** | 励起ローパスの**基準**カットオフ |
    | **Velocity** | その**周り**を変調 |
    | **Attack** | 高域ノイズ（ピック音・爪）の量 |

    ### 出口は2系統

    - `string` … **弦へ注入**する信号
    - `direct` … 弦を通さず出力へ直接足す**打撃音**（スラップのクランク、ポップのスナップ）

    ### `JBass5`から変えたところ：**確保しない**

    向こうは`std::vector`を音のたびに3本作っていました（`applyPluckComb`が
    もう1本コピーを作るので、実際は4本）。**このアプリは`processBlock()`で確保しません**（9.5）
    ので、**ボイスが持つ置き場**（`JavaRhinoBassExcitationBuffers`）へ書く形に変えました。

    **式と定数は1つも変えていません。**
*/
enum class BassStyle
{
    Finger = 0,   ///< 指弾き
    Pick,         ///< ピック弾き
    Slap,         ///< スラップ（サムピング）
    Pop,          ///< プル／ポップ。**Slapのとき高音弦で自動的に選ばれます**
    Mute,         ///< ブリッジミュート
    Ghost,        ///< ゴーストノート
    Harmonic,     ///< ハーモニクス
    NumStyles
};

/** 画面に出す奏法（`Pop`は自動で選ばれるものなので、**選択肢には出しません**）。 */
inline constexpr int numBassStyles = 6;

//==============================================================================
struct StyleTone
{
    float brightMul;    ///< brightnessに掛ける係数
    float t60Mul;       ///< sustainに掛ける係数
    float t60Fixed;     ///< >0ならT60を固定（ミュート系）
    float pluckPos;     ///< 撥弦位置（実効弦長比）。<0ならPluck Posのつまみを使う
    float levelMul;     ///< 出力レベル
};

inline const StyleTone& getStyleTone (BassStyle s)
{
    // brightMul, t60Mul, t60Fixed, pluckPos, levelMul
    static const StyleTone table[(int) BassStyle::NumStyles] =
    {
        { 1.00f, 1.00f,  0.0f,  -1.0f, 1.00f },   // Finger
        { 1.06f, 0.95f,  0.0f,  -1.0f, 1.00f },   // Pick
        { 1.10f, 0.85f,  0.0f,  0.38f, 1.10f },   // Slap
        { 1.13f, 0.80f,  0.0f,  0.30f, 1.15f },   // Pop
        { 0.80f, 1.00f,  0.35f, 0.11f, 0.95f },   // Mute
        { 0.90f, 1.00f,  0.08f, -1.0f, 0.85f },   // Ghost
        { 1.05f, 1.20f,  0.0f,  -1.0f, 0.80f },   // Harmonic
    };

    return table[(int) s];
}

//==============================================================================
struct ExcitationParams
{
    BassStyle style   = BassStyle::Finger;
    float velocity    = 0.8f;     ///< 0..1
    float hardness    = 0.6f;     ///< 0..1
    float attack      = 0.7f;     ///< 0..1
    float clank       = 0.5f;     ///< 0..1
    double periodSamples = 480.0;
    double sampleRate    = 48000.0;
    int   harmonicOrder  = 2;
    float pluckRatio     = 0.22f; ///< 撥弦位置（実効弦長比）
};

//==============================================================================
/**
    励起の置き場。**ボイスが1つずつ持ちます**（音のスレッドで確保しないため）。

    `maxLength`は**192kHzでB0（30.9Hz）を弾いたときの1周期**（6220サンプル）より
    大きく取ってあります。ここに収まらない設定では励起が少し短くなるだけで、
    音が壊れることはありません。
*/
struct JavaRhinoBassExcitationBuffers
{
    static constexpr int maxLength = 8192;
    static constexpr int maxDirect = 2048;

    float string[maxLength] { };
    float direct[maxDirect] { };
    float scratch[maxLength] { };   ///< 撥弦位置のコム用（元は`std::vector`のコピー）

    int stringLength = 0;
    int directLength = 0;

    float collisionAmount    = 0.0f;
    float collisionThreshold = 1.0f;
    float collisionDecay     = 0.12f;   ///< 秒
};

//==============================================================================
namespace exc_detail
{
    inline float onePoleCoef (double cutoffHz, double fs)
    {
        return (float) (1.0 - std::exp (-2.0 * bassdsp::kPi
                                          * std::clamp (cutoffHz, 10.0, fs * 0.45) / fs));
    }

    inline void normalise (float* v, int length, float target)
    {
        float peak = 0.0f;

        for (int i = 0; i < length; ++i)
            peak = std::max (peak, std::abs (v[i]));

        if (peak > 1.0e-6f)
        {
            const float g = target / peak;

            for (int i = 0; i < length; ++i)
                v[i] *= g;
        }
    }

    /** 撥弦位置のコムフィルタ `y[n] = x[n] - x[n - d]`。 */
    inline void applyPluckComb (float* v, int length, int d, float* scratch)
    {
        if (d < 1 || d >= length)
            return;

        std::copy (v, v + length, scratch);

        for (int i = length - 1; i >= 0; --i)
            v[i] = scratch[i] - (i >= d ? scratch[i - d] : 0.0f);
    }
}

//==============================================================================
inline void generateExcitation (const ExcitationParams& p, std::mt19937& rng,
                                 JavaRhinoBassExcitationBuffers& out)
{
    using namespace exc_detail;

    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    const double fs = p.sampleRate;
    const double period = std::max (8.0, p.periodSamples);
    const float  vel = std::clamp (p.velocity, 0.02f, 1.0f);
    const float  hard = std::clamp (p.hardness, 0.0f, 1.0f);

    out.directLength = 0;
    out.collisionAmount = 0.0f;

    //--------------------------------------------------------------------------
    // 奏法別の基本設定
    double cutoff = 800.0;   // 励起LPFカットオフ
    float  lenF   = 1.0f;    // 励起長／周期
    float  noiseAmt = 0.0f;  // 高域ノイズ量
    float  body   = 1.0f;    // 変位（基音）成分の重み
    bool   collision = false;

    switch (p.style)
    {
        case BassStyle::Finger:
            cutoff = 380.0 + hard * 900.0 + vel * 700.0;
            lenF = 1.00f; noiseAmt = 0.12f * p.attack; body = 1.00f;
            break;

        case BassStyle::Pick:
            cutoff = 1100.0 + hard * 2400.0 + vel * 1600.0;
            lenF = 0.72f; noiseAmt = 0.55f * p.attack; body = 0.80f;
            break;

        case BassStyle::Slap:
            cutoff = 850.0 + hard * 1700.0 + vel * 2000.0;
            lenF = 0.55f; noiseAmt = 0.40f * p.attack; body = 1.10f;
            collision = true;
            break;

        case BassStyle::Pop:
            cutoff = 1700.0 + hard * 2800.0 + vel * 2600.0;
            lenF = 0.42f; noiseAmt = 0.60f * p.attack; body = 0.70f;
            collision = true;
            break;

        case BassStyle::Mute:
            cutoff = 260.0 + hard * 500.0 + vel * 300.0;
            lenF = 0.90f; noiseAmt = 0.10f * p.attack; body = 1.05f;
            break;

        case BassStyle::Ghost:
            cutoff = 500.0 + hard * 900.0 + vel * 900.0;
            lenF = 0.80f; noiseAmt = 0.45f * p.attack; body = 0.25f;
            break;

        case BassStyle::Harmonic:
            cutoff = 700.0 + hard * 1200.0 + vel * 900.0;
            lenF = 1.00f; noiseAmt = 0.10f * p.attack; body = 0.60f;
            break;

        case BassStyle::NumStyles:
        default:
            break;
    }

    //--------------------------------------------------------------------------
    // 1) 基本波形＝ローパスノイズ＋変位の山
    int N = (int) (period * lenF);
    N = std::clamp (N, 6, JavaRhinoBassExcitationBuffers::maxLength);

    out.stringLength = N;

    const float k = onePoleCoef (cutoff, fs);
    float lp = 0.0f;

    for (int i = 0; i < N; ++i)
    {
        lp += k * (uni (rng) - lp);

        // 「弦が指／ピックから離れる」変位の形。**基音を確実に立ち上げる**
        const float shape = 0.5f * (1.0f - std::cos (2.0f * bassdsp::kPiF
                                                       * (float) (i + 1) / (float) (N + 1)));

        // 末尾を落とすエンベロープ
        const float env = 0.5f * (1.0f + std::cos (bassdsp::kPiF * (float) i / (float) N));

        out.string[i] = body * shape * 0.9f + lp * env * 1.0f;
    }

    //--------------------------------------------------------------------------
    // 2) ハーモニクス：1/k周期のバーストをk回タイルする
    if (p.style == BassStyle::Harmonic)
    {
        const int kOrd = std::max (2, p.harmonicOrder);
        const int sub  = std::max (4, N / kOrd);

        std::copy (out.string, out.string + N, out.scratch);

        for (int i = 0; i < N; ++i)
            out.string[i] = out.scratch[i % sub];
    }

    //--------------------------------------------------------------------------
    // 3) 撥弦位置のコム（位置は**実効弦長基準**）
    {
        const int d = (int) (std::clamp (p.pluckRatio, 0.02f, 0.48f) * period);
        applyPluckComb (out.string, N, d, out.scratch);
    }

    normalise (out.string, N, 1.0f);

    //--------------------------------------------------------------------------
    // 4) ピック／爪のノイズ（先頭2msの1次差分ハイパスノイズ）
    if (noiseAmt > 1.0e-4f)
    {
        const int noiseLen = std::min (N, (int) (0.002 * fs) + 1);
        float prev = 0.0f;

        for (int i = 0; i < noiseLen; ++i)
        {
            const float n = uni (rng);
            const float hp = n - prev;
            prev = n;

            const float env = 1.0f - (float) i / (float) noiseLen;
            out.string[i] += noiseAmt * hp * env * env;
        }
    }

    //--------------------------------------------------------------------------
    // 5) ベロシティ
    {
        const float g = std::pow (vel, 1.25f);

        for (int i = 0; i < N; ++i)
            out.string[i] *= g;
    }

    //--------------------------------------------------------------------------
    // 6) スラップ／ポップの打撃音（**弦を通さない直接音**）＋衝突非線形
    if (collision)
    {
        const float amt = std::clamp (p.clank, 0.0f, 1.0f) * vel;

        const int dLen = std::clamp ((int) (0.006 * fs), 8,
                                      JavaRhinoBassExcitationBuffers::maxDirect);
        out.directLength = dLen;

        const bool isPop = (p.style == BassStyle::Pop);
        const double bpLow = isPop ? 2600.0 : 1500.0;     // バンドパス下限相当
        const float kLp = onePoleCoef (isPop ? 7000.0 : 4500.0, fs);
        const float kHp = onePoleCoef (bpLow, fs);

        float l = 0.0f, h = 0.0f;

        for (int i = 0; i < dLen; ++i)
        {
            const float n = uni (rng);
            l += kLp * (n - l);
            h += kHp * (l - h);

            const float bp = l - h;                       // バンドパス
            const float env = std::exp (-4.5f * (float) i / (float) dLen);

            out.direct[i] = bp * env;
        }

        normalise (out.direct, dLen, amt * (isPop ? 0.45f : 0.38f));

        out.collisionAmount    = amt;
        out.collisionThreshold = 0.55f * (1.05f - std::clamp (p.clank, 0.0f, 1.0f))
                                        * (1.2f - 0.5f * vel);
        out.collisionDecay     = isPop ? 0.08f : 0.13f;
    }
}
