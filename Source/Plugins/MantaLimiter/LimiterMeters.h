#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
/**
    8.333：書き手1つ・読み手1つのロックフリーの待ち行列（リミッター設計書「スレッド間の受け渡し」）。
    **満杯なら新しいほうを捨てます**（表示が一瞬欠けても、音は止めない）。
*/
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        const uint32_t w = writePosition.load (std::memory_order_relaxed);

        if (w - readPosition.load (std::memory_order_acquire) >= (uint32_t) Capacity)
            return false;

        items[w % Capacity] = item;
        writePosition.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const uint32_t r = readPosition.load (std::memory_order_relaxed);

        if (r == writePosition.load (std::memory_order_acquire))
            return false;

        item = items[r % Capacity];
        readPosition.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> items {};
    std::atomic<uint32_t> writePosition { 0 }, readPosition { 0 };
};

//==============================================================================
/**
    8.333：メーターとラウドネス（リミッター仕様書「メーター・ラウドネス仕様」・設計書「メーター設計」）。

    **音のスレッドで集計し、画面へは待ち行列で渡します**（約33 msごとに1件）。

    ### ラウドネス（ITU-R BS.1770-4／EBU R128）

    1. K特性（高域シェルフ＋ハイパスの2段バイカッド。**係数は規格の式からレートごとに**作る）
    2. **100 msごとに**全チャンネルの二乗平均の和を double で確定
    3. Momentary＝直近4区間、Short-term＝直近30区間の平均
    4. Integrated：Momentaryを100 msごとに**0.1 LU刻みのヒストグラム**へ（−70〜+5 LUFS、751ビン）。
       各ビンは「個数」と「エネルギーの合計」を持ち、絶対ゲート −70 LUFS・相対ゲート −10 LU をビンの走査で
    5. LRA：Short-termを同じ形のヒストグラムへ。相対ゲート −20 LU のあと、10〜95パーセンタイルの差
    6. PLR＝出力TPの最大 − Integrated

    **何時間計っても、メモリと計算量が一定**です（ヒストグラムなので）。ゲートの位置がビンの中のどこかによる
    誤差は最大0.05 LU（EBU Tech 3341の許容差 ±0.1 LU の内）。

    ### 確保しない

    ヒストグラムも区間の輪も`prepare()`で確保します。
*/
class LimiterMeters
{
public:
    /** 約33 msごとに画面へ送るもの（設計書`MeterFrame`）。dBで入れます（無音は −inf の代わりに −150）。 */
    struct Frame
    {
        float inputPeak[2] { -150.0f, -150.0f };
        float outputPeak[2] { -150.0f, -150.0f };
        float outputTruePeak[2] { -150.0f, -150.0f };
        float outputTruePeakMax = -150.0f;      ///< 計測を始めてからの最大（dBTP）
        float gainReductionNow = 0.0f;          ///< 直近33 msで最も深いGR
        float gainReductionMax = 0.0f;          ///< 計測を始めてからの最大
        float momentary = -150.0f, shortTerm = -150.0f, integrated = -150.0f;
        float loudnessRange = 0.0f, peakToLoudness = 0.0f;
        bool clipped = false;
    };

    /** スクロール表示の1列（設計書`DisplayColumn`）。リニアの値。 */
    struct Column
    {
        float inMin = 0.0f, inMax = 0.0f;
        float outAbsMax = 0.0f;
        float grMax = 0.0f;    ///< その列で最も深いGR（dB、0以下）
    };

    void prepare (double sampleRate, int numChannels);

    /** **音のスレッドから**、1ブロックぶん。`input`はGainのあと（リミットの前）、`output`は最終の出力。 */
    void push (const float* const* input, const float* const* output, const float* gainReductionDb,
               const float* const* outputTruePeak, int numChannels, int numSamples, float ceilingDb);

    //--------------------------------------------------------------------------
    // 画面から

    bool popFrame (Frame& frame) noexcept { return frames.pop (frame); }
    bool popColumn (Column& into) noexcept { return columns.pop (into); }

    /** 計測をやり直す（次のブロックの頭で音のスレッドが空にする）。 */
    void requestReset() noexcept { resetRequested.store (true); }

    /** スクロール表示の1列が何サンプルか（表示時間 × レート ÷ 表示幅）。 */
    void setSamplesPerColumn (int samples) noexcept { samplesPerColumn.store (samples < 1 ? 1 : samples); }

    //--------------------------------------------------------------------------
    // 自己検査のため（音のスレッドと同じスレッドで呼ぶこと）

    float getMomentary() const noexcept;
    float getShortTerm() const noexcept;
    float getIntegrated() const noexcept;
    float getLoudnessRange() const noexcept;

    /** K特性の係数（1段目 b0 b1 b2 a1 a2、2段目 a1 a2）。48 kHzで規格の表と比べるため。 */
    std::array<double, 7> getKWeightingCoefficients() const noexcept;

    static constexpr float silenceDb = -150.0f;
    static constexpr int histogramBins = 751;          ///< −70〜+5 LUFS、0.1 LU刻み

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Histogram
    {
        std::vector<uint32_t> count;
        std::vector<double> energy;
        void reset();
        void add (double meanSquare);
    };

    void resetMeasurements();
    void finishBlock();
    void publishFrame();

    static float energyToLufs (double meanSquare) noexcept;
    static int binFor (float lufs) noexcept;
    float integratedFrom (const Histogram& h, float relativeGate, double* gatedEnergy = nullptr) const noexcept;

    double sampleRate = 48000.0;
    int channels = 2;

    std::array<Biquad, 2> shelf, highpass;

    // 100 ms 区間
    int samplesPer100ms = 4800, samplesInBlock = 0;
    double blockSum = 0.0;
    std::array<double, 30> recent {};    ///< 直近30区間の二乗平均（輪）
    int recentPos = 0, recentCount = 0;

    Histogram momentaryHistogram, shortTermHistogram;

    // ピーク・GR（33 msの窓と、計測を始めてから）
    int samplesPerFrame = 1600, samplesInFrame = 0;
    Frame pending;
    float truePeakMaxDb = silenceDb, truePeakMaxLinear = 0.0f, gainReductionMaxDb = 0.0f;
    bool clippedSinceReset = false;

    // スクロール表示
    std::atomic<int> samplesPerColumn { 480 };
    Column column;
    int samplesInColumn = 0;

    std::atomic<bool> resetRequested { false };

    SpscQueue<Frame, 64> frames;
    SpscQueue<Column, 4096> columns;
};
