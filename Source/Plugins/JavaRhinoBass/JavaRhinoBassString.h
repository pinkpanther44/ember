#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    8.257：**5弦ベースの弦1本**（Phase 265／拡張Karplus-Strong）。

    本人が別に作った`JBass5`（別リポジトリ）の`BassString`をそのまま移しました。
    **式は1文字も変えていません**——向こうで音とチューニング精度を追い込んだものです。

    ```
       励起 ─┬─▶[ 整数ディレイ ]─▶[ 1次オールパス(小数部) ]─┬─▶ 出力（ピックアップのコムへ）
             │                                              │
             └──[ ×decayGain ]◀─[ 衝突非線形 ]◀─[ ループLPF ]┘
    ```

    ### ギターのほうとの違い（**5弦ベースで初めて出た罠**）

    `RaccoGuitarString`（8.256）と作りは同じですが、**3つ違います**。
    どれも**音域比が約13倍**（B0 31Hz〜G4 392Hz）あることから出たものです。

    #### ① `brightness`は係数ではなく「**基音の何倍で切るか**」

    ループフィルタの係数を直に持つと、**低音は明るく、高音は完全に無音**になります
    （向こうの実測：`brightness = 0.05`で MIDI61 の rms が `4.7e-7`＝事実上の無音）。
    短いループ＝高音ほど、1秒あたりにフィルタを通る回数が多く、損失が積み上がるためです。

    ここでは`brightness`を**基音の6〜96倍のカットオフ**として持ちます。

    #### ② 減衰ゲインで、**基音におけるLPFの損失を打ち消す**

    ```
    decayGain = 0.001^(D / (T60·fs)) ÷ |H_lpf(f0)|
    ```

    **基音の減衰は常にT60どおり**になり、ループフィルタは
    「基音に対して倍音がどう減るか」だけを決めます。

    #### ③ オールパスの係数は**二分探索で解く**

    ギターでは`c = (1−frac)/(1+frac)`の近似で足りていましたが、
    ベースの低音域では残ります。`atan2`で厳密な位相遅延を出し、
    26回の二分探索で解くと**±0.25cent**に収まりました
    （制御レートは64サンプルごとなので、負荷は無視できます）。

    ### DCブロッカーは**ループの外**

    ループの中に置くと、ハイパスの位相がループ長に足されて
    **全音域で+0.3〜+4.2cent上ずります**（向こうの実測）。
    ループLPFのDCゲインは1、`decayGain`は1未満なので、輪の中に直流は溜まりません。

    JUCEに依存していないので、**そのままg++で測れます**。
*/
namespace bassdsp
{
    /** **MSVCは`<cmath>`に`M_PI`を定義しません**（`_USE_MATH_DEFINES`が要る）。
        GCC/Clangでは通ってしまうので、**Linuxの検証だけでは気づけません**。
        マクロに頼らず自前の定数を使います。 */
    inline constexpr double kPi  = 3.14159265358979323846;
    inline constexpr float  kPiF = 3.14159265358979323846f;
}

//==============================================================================
class JavaRhinoBassString
{
public:
    void prepare (double sampleRateIn)
    {
        fs = sampleRateIn;

        // 最低20Hzまで保持できる大きさ（2のべき乗）
        const int need = (int) std::ceil (fs / 20.0) + 16;
        int size = 16;

        while (size < need)
            size <<= 1;

        line.assign ((size_t) size, 0.0f);
        hist.assign ((size_t) size, 0.0f);
        mask = size - 1;

        reset();
        recalc();
    }

    void reset()
    {
        std::fill (line.begin(), line.end(), 0.0f);
        std::fill (hist.begin(), hist.end(), 0.0f);
        writeIdx = 0;
        histIdx  = 0;
        apX1 = apY1 = 0.0f;
        lpY1 = 0.0f;
        collisionEnv = 0.0f;
        dcX1 = dcY1  = 0.0f;
    }

    //==========================================================================
    /** 明るさ 0..1。**係数ではなく「基音の何倍で切るか」**（クラスの説明①）。 */
    void setBrightness (float b)
    {
        brightness = std::clamp (b, 0.0f, 1.0f);
        recalc();                 // **周波数も計算し直すこと**
    }

    /** 絶対上限カットオフ（Hz）。高音弦ほど倍音が少ない実機の傾向。 */
    void setCutoffCeiling (float hz)
    {
        cutoffCeiling = std::clamp (hz, 500.0f, 16000.0f);
        recalc();
    }

    /** 減衰時間（-60dBに達するまでの秒数）。 */
    void setDecayT60 (float seconds)
    {
        t60 = std::max (0.02f, seconds);
        recalcDecay();
    }

    void setFrequency (double freqHz)
    {
        freq = std::clamp (freqHz, 20.0, 6000.0);
        recalc();
    }

    /** 2つのピックアップ位置。**実効弦長に対する「ブリッジからの距離」比**。 */
    void setPickupRatios (float neckRatio, float bridgeRatio)
    {
        pickupNeck   = std::clamp (neckRatio,   0.01f, 0.49f);
        pickupBridge = std::clamp (bridgeRatio, 0.01f, 0.49f);
    }

    /** 0＝ネックPUのみ／**0.5＝両方フル**（ジャズベの定番）／1＝ブリッジPUのみ。

        真ん中で両方フルにすると、**和を取ることで中域スクープが自動的に生まれます**
        （実機のVol/Vol全開と同じ）。 */
    void setBlend (float blend01)
    {
        const float b = std::clamp (blend01, 0.0f, 1.0f);

        gainNeck   = std::min (1.0f, 2.0f - 2.0f * b);
        gainBridge = std::min (1.0f, 2.0f * b);
    }

    /** 弦がフレット／指板に当たる非線形（スラップのクランク）。 */
    void triggerCollision (float amount01, float threshold, float decaySeconds)
    {
        collisionEnv       = std::clamp (amount01, 0.0f, 1.0f);
        collisionThreshold = std::max (0.02f, threshold);
        collisionDecay     = (float) std::exp (-1.0 / (std::max (0.005, (double) decaySeconds) * fs));
    }

    //==========================================================================
    /** 1サンプル処理。`excitationIn`は弦への注入（不要なら0）。 */
    inline float processSample (float excitationIn) noexcept
    {
        // ディレイライン（整数部）
        const int rIdx = (writeIdx - intDelay) & mask;
        const float x = line[(size_t) rIdx];

        // 1次オールパス（小数部）
        const float ap = apCoef * x + apX1 - apCoef * apY1;
        apX1 = x;
        apY1 = ap;

        // ループフィルタ（1次LPF）
        lpY1 = lpA * ap + (1.0f - lpA) * lpY1;
        float fb = lpY1;

        // 衝突非線形（クランク）
        if (collisionEnv > 1.0e-4f)
        {
            const float thr = collisionThreshold;
            const float clipped = thr * std::tanh (fb / thr);
            fb += collisionEnv * (clipped - fb);
            collisionEnv *= collisionDecay;
        }

        // 減衰（T60正規化済み）
        fb *= decayGain;

        // **DCブロッカーはループ内に置かないこと**（クラスの説明）。
        // 出力段の`dcBlock()`で処理します

        line[(size_t) writeIdx] = fb + excitationIn;
        writeIdx = (writeIdx + 1) & mask;

        // 出力履歴（ピックアップのコム用）
        hist[(size_t) histIdx] = ap;
        histIdx = (histIdx + 1) & mask;

        return ap;
    }

    /** ピックアップ位置のコムフィルタを掛けた出力。`processSample()`の直後に呼ぶ。 */
    inline float pickupOutput (float v) const noexcept
    {
        const double D = loopDelay;
        const float dn = readHist (pickupNeck   * D);
        const float db = readHist (pickupBridge * D);

        return gainNeck * (v - dn) + gainBridge * (v - db);
    }

    /** 出力段のDCブロッカー（**ループの外**）。 */
    inline float dcBlock (float x) noexcept
    {
        const float y = x - dcX1 + 0.9995f * dcY1;
        dcX1 = x;
        dcY1 = y;

        return y;
    }

    double getLoopDelay() const noexcept { return loopDelay; }
    double getFrequency() const noexcept { return freq; }

private:
    //==========================================================================
    inline float readHist (double delaySamples) const noexcept
    {
        const double d = std::clamp (delaySamples, 1.0, (double) mask - 2.0);
        const int    i = (int) d;
        const float  f = (float) (d - i);
        const int    a = (histIdx - 1 - i) & mask;
        const int    b = (a - 1) & mask;

        return hist[(size_t) a] + f * (hist[(size_t) b] - hist[(size_t) a]);
    }

    /** 1次LPF `y = a·x + (1-a)·y1` の位相遅延（サンプル）。 */
    static double lowpassPhaseDelay (double a, double w) noexcept
    {
        if (w < 1.0e-9) return (1.0 - a) / a;      // DC極限

        const double p = 1.0 - a;
        const double im = p * std::sin (w);
        const double re = 1.0 - p * std::cos (w);

        return std::atan2 (im, re) / w;
    }

    /** 1次オールパス `H(z) = (c + z^-1)/(1 + c·z^-1)` の位相遅延（サンプル）。 */
    static double allpassPhaseDelay (double c, double w) noexcept
    {
        if (w < 1.0e-9) return (1.0 - c) / (1.0 + c);

        const double pn = std::atan2 (-std::sin (w), c + std::cos (w));
        const double pd = std::atan2 (-c * std::sin (w), 1.0 + c * std::cos (w));

        return -(pn - pd) / w;
    }

    /** 目標の位相遅延を与えるオールパス係数を**二分探索で解く**（クラスの説明③）。 */
    static double solveAllpassCoef (double target, double w) noexcept
    {
        // cが大きいほど位相遅延は小さくなる（単調）
        double lo = -0.98, hi = 0.98;

        for (int i = 0; i < 26; ++i)
        {
            const double mid = 0.5 * (lo + hi);

            if (allpassPhaseDelay (mid, w) > target) lo = mid; else hi = mid;
        }

        return 0.5 * (lo + hi);
    }

    void recalc()
    {
        if (line.empty())
            return;

        // brightness → 基音相対カットオフ → 1次LPF係数
        {
            const double k  = 6.0 + std::pow ((double) brightness, 1.5) * 90.0;  // 基音の6〜96倍
            const double fc = std::clamp (freq * k, 40.0,
                                           std::min ((double) cutoffCeiling, fs * 0.45));

            lpA = (float) (1.0 - std::exp (-2.0 * bassdsp::kPi * fc / fs));
            lpA = std::clamp (lpA, 0.001f, 0.999f);
        }

        const double D = fs / freq;                       // 必要な総ループ遅延
        const double w = 2.0 * bassdsp::kPi * freq / fs;
        const double lpPD = lowpassPhaseDelay (lpA, w);   // **実計算**（固定近似しない）

        const double rest = D - lpPD;
        int    id     = (int) std::floor (rest - 0.5);
        double target = rest - id;                        // オールパスに担わせる遅延

        // 0付近を避ける
        while (target < 0.4)  { id -= 1; target += 1.0; }
        while (target >= 1.4) { id += 1; target -= 1.0; }

        if (id < 2)        { id = 2; target = std::max (0.4, rest - id); }
        if (id > mask - 4) { id = mask - 4; }

        intDelay  = id;
        apCoef    = (float) solveAllpassCoef (target, w);   // **厳密解**
        loopDelay = D;

        recalcDecay();
    }

    void recalcDecay()
    {
        // T60正規化 その1：1ループあたりの減衰量を弦長から求める
        const double loops = loopDelay / (t60 * fs);
        double g = std::pow (0.001, loops);

        // T60正規化 その2：**ループLPFが基音を減衰させるぶんを打ち消す**
        // （クラスの説明②。これが無いと、brightnessを下げたとき高音弦が即無音になる）
        const double w = 2.0 * bassdsp::kPi * freq / fs;
        const double p = 1.0 - lpA;
        const double re = 1.0 - p * std::cos (w);
        const double im = p * std::sin (w);
        const double mag = lpA / std::sqrt (re * re + im * im);

        if (mag > 1.0e-6)
            g /= mag;

        decayGain = (float) std::min (g, 0.99999);
    }

    //==========================================================================
    double fs = 48000.0;
    double freq = 100.0;
    double loopDelay = 480.0;

    std::vector<float> line, hist;
    int mask = 0, writeIdx = 0, histIdx = 0;

    int   intDelay = 100;
    float apCoef = 0.0f, apX1 = 0.0f, apY1 = 0.0f;

    float brightness = 0.62f, lpA = 0.62f, lpY1 = 0.0f;
    float cutoffCeiling = 8000.0f;
    float t60 = 8.0f, decayGain = 0.999f;

    float pickupNeck = 0.24f, pickupBridge = 0.12f;
    float gainNeck = 1.0f, gainBridge = 1.0f;

    float collisionEnv = 0.0f, collisionThreshold = 0.3f, collisionDecay = 0.999f;
    float dcX1 = 0.0f, dcY1 = 0.0f;
};
