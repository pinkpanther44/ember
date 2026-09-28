#pragma once

#include "AnalyzerSettings.h"
#include "AnalyzerFifo.h"

#include <memory>
#include <vector>

namespace juce { namespace dsp { class FFT; } }

//==============================================================================
/**
    8.329：スペクトルを求める（アナライザー設計書4章の`SpectrumAnalyzer`）。

    **画面のスレッドで回します**（音のスレッドは`AnalyzerFifo`へ書くだけ。仕様書4.4）。

    ### 順番は固定（設計書4章）

    1. 窓掛けとFFT（チャンネルに応じて1回か2回）
    2. ビンごとのパワーと正規化（0 dBFSの正弦波が0 dB）
    3. スロープ（ビンごとの利得）
    4. 周波数平滑化 → **ここの値がピークホールドとリアルタイム曲線の元**
    5. 時間平均（立上り・減衰。積算は累積平均）
    6. dBへ（下限 −120 dB）

    **すべてパワーで計算し、dBへは最後に1回だけ**直します。順番を入れ替えると結果が変わります。

    ### 高いサンプルレート（仕様書4.1）

    ビンの幅を 48 kHz・8192点（約5.9 Hz）の近くに保つため、**88.2 kHz以上でFFTを2倍、
    176.4 kHz以上で4倍**にします。仕様書は「96 kHz以上で倍」ですが、倍だけだと
    192 kHzでビンの幅が2倍になり、**同じ音源が3 dB違って見えます**
    （仕様書7.3「サンプルレートが違っても1 dB以内」を満たせない。自己検査で確かめました）。
*/
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer();

    /** 大きさの決まるものを作り直す（**メモリを確保するのはここだけ**）。 */
    void prepare (double sampleRate, const AnalyzerSettings& settings);

    /** `prepare()`をやり直す必要があるか（FFTサイズ・窓・オーバーラップ・レート）。 */
    bool needsPrepare (double sampleRate, const AnalyzerSettings& settings) const;

    /** 作り直しの要らない設定（チャンネル・平滑化・スロープ・応答速度）。
        チャンネルが変わったとき・積算に切り替えたときは平均をやり直します（設計書7.1）。 */
    void applySettings (const AnalyzerSettings& settings);

    /** FIFOを空になるまで読み、できたフレームの数を返す。 */
    int process (AnalyzerFifo& fifo);

    /** 標本を直接渡す（自己検査で使う。`process()`もここを通ります）。 */
    int pushSamples (const float* left, const float* right, int numSamples);

    /** 平均・ピーク・履歴を空にする（画面を開いたとき。設計書4.1）。 */
    void reset();

    //--------------------------------------------------------------------------
    int getNumBins() const noexcept { return numBins; }
    double getBinHz() const noexcept { return binHz; }
    int getEffectiveFftSize() const noexcept { return fftSize; }
    double getSampleRate() const noexcept { return sampleRate; }

    /** 1フレームぶんの時間（ホップ長 ÷ レート）。 */
    double getFrameSeconds() const noexcept;

    /** 時間平均したもの（スロープ・平滑化込み。ビンごと、dB）。 */
    const std::vector<float>& getAveragedDb() const noexcept { return averagedDb; }

    /** 平均する前のいちばん新しいフレーム（リアルタイム曲線とピークの元）。 */
    const std::vector<float>& getFrameDb() const noexcept { return frameDb; }

    /** 100 Hz〜10 kHzの平均（1/6オクターブずつまとめてから平均。設計書4.5）。
        まだ何も来ていなければ −120。 */
    float getReferenceBandDb() const;

    /** 同じものを、**時間平均する前のいちばん新しいフレーム**で。
        8.329：**無音かどうかはこちらで決めます**（`TargetRange::update()`）。平均のほうは、
        音が止まってから −90 dB を割るまでに数秒かかり、そのあいだレンジが下へ追いかけていました。 */
    float getLatestReferenceBandDb() const;

    /** これまでにできたフレームの数（`reset()`で0へ）。 */
    int getNumFrames() const noexcept { return framesSinceReset; }

    /** 窓がいちど**本物の音で埋まったか**（リセットしてから窓の長さぶん流れたか）。
        8.329：埋まる前のフレームは0が混ざって低く出ます。レンジをそこへ合わせると、
        **最初に13 dBほど低い位置へ置かれ、3秒の時定数でゆっくり戻る**ことになっていました。 */
    bool isWarmedUp() const noexcept { return (long long) framesSinceReset * hop >= fftSize; }

    //--------------------------------------------------------------------------
    // 平滑化（自己検査が素朴な計算と比べるので、外から呼べるようにしてあります）

    /** ビン k の平滑化で使う範囲 [lo, hi]。1ビンに満たないところは lo = hi = k。 */
    static void buildSmoothingRanges (double binHz, int numBins, double octaves,
                                      std::vector<int>& lo, std::vector<int>& hi);

    /** 累積和で平均を取る（全体で O(N)）。`prefix`は作業用。 */
    static void applySmoothing (const std::vector<double>& power, const std::vector<int>& lo,
                                const std::vector<int>& hi, std::vector<double>& prefix,
                                std::vector<double>& out);

    static constexpr float floorDb = -120.0f;

    /** 窓の長さの何倍でFFTするか（後ろは0で埋める。`prepare()`の説明）。 */
    static constexpr int zeroPadding = 2;

    static float powerToDb (double power) noexcept;

    /** 時間平均の1歩（設計書4.5）：上がるときは立上り、下がるときは減衰の係数。
        α = e^(−Δt/τ)。**解析と自己検査で同じものを使います。** */
    static double averageStep (double current, double input, double attackAlpha, double releaseAlpha) noexcept
    {
        const double a = input > current ? attackAlpha : releaseAlpha;
        return a * current + (1.0 - a) * input;
    }

private:
    void analyseFrame();
    void transform (const float* history, float* magnitudes);
    void rebuildGainTables();
    float bandMeanDb (const std::vector<double>& source) const;

    double sampleRate = 48000.0;
    AnalyzerSettings settings;
    AnalyzerSettings preparedWith;

    int fftSize = 0;                         ///< 窓の長さ（設定 × レートの倍率）
    int transformSize = 0;                   ///< FFTの長さ（窓の長さ × `zeroPadding`）
    int numBins = 0;
    double binHz = 1.0;
    int hop = 1;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    double windowSum = 1.0;

    std::vector<float> historyL, historyR;   ///< 円環（長さ fftSize）
    int historyPos = 0;
    int samplesUntilFrame = 0;

    std::vector<float> scratch;              ///< FFTの作業場（2 × transformSize）
    std::vector<float> magnitudesL, magnitudesR;
    std::vector<float> mixed;                ///< Mid／Sideの波形

    std::vector<double> slopeGain;
    std::vector<int> smoothLo, smoothHi;
    std::vector<double> power, smoothed, prefix;

    std::vector<double> averagedPower;
    std::vector<float> averagedDb, frameDb;
    int framesSinceReset = 0;
    int integrateCount = 0;

    struct Band { int lo, hi; };
    std::vector<Band> referenceBands;

    std::vector<float> popL, popR;           ///< FIFOから読む受け皿
};

//==============================================================================
/**
    8.329：ピークホールド（設計書4.6）。**ビンごと**に持ちます。

    - 入力がピーク以上 → ピーク＝入力、保持の残り＝保持時間
    - 保持の残りがある → 残りを減らす
    - それ以外 → 12 dB/s で落とす（入力より下には落とさない）
    - 保持が「無限」→ 落とさない（クリックで`reset()`）
*/
class PeakHold
{
public:
    void reset (int numBins);

    void update (const std::vector<float>& inputDb, double dtSeconds,
                 float holdSeconds, bool holdForever);

    const std::vector<float>& getPeakDb() const noexcept { return peakDb; }

    static constexpr float fallDbPerSecond = 12.0f;

private:
    std::vector<float> peakDb;
    std::vector<float> holdLeft;
};
