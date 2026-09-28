#pragma once

#include "LimiterCore.h"
#include "LimiterDsp.h"

#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
    8.333：リミッター全体（リミッター設計書「処理ブロックの流れ」の`LimiterEffect`）。

    **ホストに依存しません**（`MantaLimiterProcessor`が包みます。自己検査は直にこれを使います）。

    ```
    1 値を読む（構造が変われば組み直しの印）
    2 Gain（20 msで滑らかに）→ DCフィルタ
    3 オーバーサンプリング（上げる）
    4 LimiterCore
    5 オーバーサンプリング（下げる）
    6 Unity Gain → ディザー＋ノイズシェーピング
    7 メーター
    ```

    ### レイテンシーは整数に揃える

    遅れ＝先読み＋補間＋オーバーサンプラーの往復。オーバーサンプラーの遅れは基準のレートで端数になり得るので、
    **端数ぶんをリミッターの遅延に足して**、全体を基準のレートの整数にしています（`LimiterCore`の`padding`）。

    ### 組み直し（先読み・オーバーサンプリング・トゥルーピーク・レート）

    設計書：**5 msで消してから組み直し、5 msで戻す**（不連続な音を出さない）。組み直した瞬間にレイテンシーが変わるので、
    `latencyChangedFlag`を立てます（プロセッサが音のスレッドの外で申告し直す）。
*/
class LimiterEngine
{
public:
    struct Parameters
    {
        float gainDb = 0.0f;
        float outputDb = -1.0f;
        int style = 0;              ///< Transparent / Punchy / Aggressive / Safe
        float lookaheadMs = 1.0f;
        float attackPercent = 100.0f;
        float releaseMs = 100.0f;
        bool autoRelease = true;
        float linkPercent = 100.0f;
        int oversampling = 0;       ///< Off / 2x / 4x / 8x
        bool truePeak = true;
        bool unity = false;
        bool audition = false;
        bool dcFilter = false;
        int dither = 0;             ///< Off / 16 / 20 / 24 bit
        int noiseShaping = 0;       ///< Off / Light
    };

    /** スタイルの定数（設計書「スタイル定数」）。 */
    struct StyleConstants
    {
        float attackRatio;
        bool triangular;
        float releaseScale;
        bool forceMaxLookahead;
    };

    static const StyleConstants& styleConstants (int style);

    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** 今のパラメータで組み直す（`prepare()`の直後と、自己検査から。フェードは掛けない）。 */
    void reconfigureNow (const Parameters& parameters);

    void process (float* const* channels, int numChannels, int numSamples, const Parameters& parameters);

    /** 基準のレートでの遅れ（先読み＋補間＋オーバーサンプラー）。 */
    int getLatencySamples() const noexcept { return latencySamples.load(); }

    /** 組み直してレイテンシーが変わったら立つ（読んだら下ろす）。 */
    bool takeLatencyChanged() noexcept { return latencyChangedFlag.exchange (false); }

    //--------------------------------------------------------------------------
    // メーター（設計書「メーター設計」の一部。ラウドネスは`LimiterMeters`）

    /** 直前のブロックの値（音のスレッドが書き、画面が読む。**1ブロックぶんの最大**）。 */
    struct BlockMeters
    {
        std::array<float, 2> inputPeak {};       ///< Gainのあと（リニア）
        std::array<float, 2> outputPeak {};
        std::array<float, 2> outputTruePeak {};
        float gainReductionDb = 0.0f;            ///< ブロックでいちばん深いGR（0以下）
    };

    const BlockMeters& getBlockMeters() const noexcept { return meters; }

    /** 直前のブロックの、基準のレートのサンプルごとのGR（dB）。 */
    const float* getGainReductionTrace() const noexcept { return grPerSample.data(); }

    /** 直前のブロックの、Gain（とDCフィルタ）のあと・リミットの前の音（入力のメーターとスクロール表示）。 */
    const float* getInputTrace (int channel) const noexcept { return inputTrace[(size_t) channel].data(); }

    /** 直前のブロックの、出力のトゥルーピーク（サンプルごと。出力TPの計測に使う）。 */
    const float* getOutputTruePeakTrace (int channel) const noexcept { return tpTrace[(size_t) channel].data(); }

    static constexpr double fadeSeconds = 0.005;

private:
    struct Structure
    {
        int lookaheadCore = 0;
        int stages = 0;
        bool truePeakDetection = false;

        bool operator!= (const Structure& o) const
        {
            return lookaheadCore != o.lookaheadCore || stages != o.stages || truePeakDetection != o.truePeakDetection;
        }
    };

    Structure structureFor (const Parameters& p) const;
    void applyStructure (const Structure& s);
    LimiterCore::Dynamics dynamicsFor (const Parameters& p) const;

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int channelsPrepared = 2;

    LimiterCore core;
    LimiterDsp::Oversampler oversampler;
    std::array<LimiterDsp::DcFilter, 2> dcFilters;
    std::array<LimiterDsp::Ditherer, 2> ditherers;
    std::array<LimiterDsp::TruePeakMeter, 2> truePeakMeters;

    Structure current;
    bool haveStructure = false;

    // 組み直しのフェード（基準のレート）
    enum class Fade { none, out, in };
    Fade fade = Fade::none;
    int fadePosition = 0, fadeLength = 1;
    Structure pending;

    // Gain（とUnity）の滑らかさ：20 msの直線
    float gainCurrent = 1.0f, gainTarget = 1.0f;
    int gainStepsLeft = 0;
    float gainStep = 0.0f;
    float ceilingDbCurrent = -1.0f;

    std::array<float*, 2> coreChannels {};
    std::vector<float> grPerSample;
    std::array<std::vector<float>, 2> tpTrace, inputTrace;
    std::vector<float> gainRamp;

    BlockMeters meters;
    std::atomic<int> latencySamples { 0 };
    std::atomic<bool> latencyChangedFlag { false };
};
