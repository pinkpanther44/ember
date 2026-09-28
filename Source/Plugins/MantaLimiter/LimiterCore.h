#pragma once

#include "LimiterDsp.h"

#include <array>
#include <cstdint>
#include <vector>

//==============================================================================
/**
    8.333：リミッターの心臓部（リミッター設計書「リミッターコア詳細設計」）。

    **ホストにも画面にも依存しません**（標準ライブラリだけ）。オーバーサンプリング後のレートで動きます。

    ### 無オーバーシュートの仕組み（設計書のとおり）

    ```
    検出 → 必要ゲイン r（リンク込み） → 区間最小値ホールド（長さ S）
         → 移動平均（矩形：長さ S ／ 三角：長さ B の矩形2段、S = 2B − 1）
         → ゲイン経路の追加遅延 E = N − (S − 1) → リリース（戻る向きだけ遅く）
    音声 → 遅延 N
    ```

    平均に使う全サンプルのホールド窓がピーク位置を含むので、**ピークの時点で必ず必要ゲイン以下**になります。
    リリースは下げる向きには即時、戻る向きだけ遅いので、この保証を崩しません。

    ### トゥルーピーク検出

    トゥルーピーク検出のとき、検出は**8倍補間**（各相16タップ。`LimiterDsp::truePeakFactor`）の値で行います。
    補間フィルタの遅れ（`truePeakDelay`＝8サンプル）のぶん、音声側の遅延も延ばします。
    （設計書は4倍・12タップ。点の間の山を見落として天井を超えたので8倍にしました。`LimiterDsp.h`）
    サンプル i の検出値は「i の前後の区間（i−1〜i と i〜i+1）の補間値の最大」——
    サンプル間のピークを、**両側のサンプルのゲインに効かせる**ためです。

    ⚠️ 設計書は「BS.1770-4付属書2の4相×12タップFIR」ですが、付属書の係数表は手元に無いため、
    **同じ形（4倍・48〜49タップ）のKaiser窓sincを`prepare()`で作って**います（`LimiterDsp.h`）。
    規格の係数は「例」として載っているもので、合否は試験（出力TP ≤ 天井＋0.1 dB）で見ています。

    ### 窓の長さが変わるとき（スタイル・Attack）

    設計書は「累積和をリングバッファから再計算し、出力ゲインを10 msかけて補間」。ここでは
    **必要ゲインの履歴（`history`）を新しい長さの経路へ流し直して**、最初からその長さで動いていたのと
    同じ状態を作り、**旧い経路と10 msかけて混ぜ替え**ます。**どちらの経路も天井を超えないので、
    混ぜた途中も超えません**（ゲインの凸結合）。

    ### 確保しない

    `prepare()`で**いちばん長い先読み・いちばん高いレート**ぶんを確保し、`process()`では確保しません。
*/
class LimiterCore
{
public:
    static constexpr int maxChannels = 2;

    /** 大きさを決める（**確保はここだけ**）。`maxDelaySamples`は先読み＋補間＋補正のいちばん長い値。 */
    void prepare (double coreSampleRate, int maxLookaheadSamples, int maxBlockSamples);

    /** コアのレートだけ変える（オーバーサンプリングの倍率が変わったとき。**確保し直さない**）。
        このあと`configure()`と`setDynamics()`を呼ぶこと（時定数がレートで変わる）。 */
    void setSampleRate (double coreSampleRate) noexcept { sampleRate = coreSampleRate > 0.0 ? coreSampleRate : sampleRate; }

    /** 構造を決め直して空にする（先読み・TP・補正の遅延が変わったとき）。**出力は1から始まります。** */
    void configure (int lookaheadSamples, bool truePeakDetection, int paddingSamples);

    /** 構造を変えない設定（毎ブロック呼んで構いません。窓の長さが変われば混ぜ替えを始めます）。 */
    struct Dynamics
    {
        float attackRatio = 1.0f;     ///< 0〜1（Attack％ × スタイルの係数）
        bool triangular = true;       ///< 三角窓か（false で矩形）
        float releaseMs = 100.0f;     ///< スタイルの倍率を掛けたあと
        bool autoRelease = true;
        float link = 1.0f;            ///< 0〜1
        float ceilingDb = -1.0f;
        bool audition = false;
    };

    void setDynamics (const Dynamics& dynamics);

    /** その場で書き換える（チャンネルは1か2）。 */
    void process (float* const* channels, int numChannels, int numSamples);

    /** 音声の遅れ（コアのレートのサンプル数）＝ 先読み ＋ 補間の遅れ ＋ 補正。 */
    int getDelaySamples() const noexcept { return lookahead + tpDelay + padding; }

    /** 直前の`process()`の、サンプルごとに掛けたゲイン（**リニア**、1以下。チャンネルの小さいほう）。
        dBにするのは表示のためだけなので、呼ぶ側が元のレートで1回だけ直します（8倍で毎サンプルの log を避ける）。 */
    const float* getGainReductionTrace() const noexcept { return grTrace.data(); }

    int getLookaheadSamples() const noexcept { return lookahead; }
    int getWindowLength() const noexcept { return windowS; }

    /** 補間の遅れ（トゥルーピーク検出のとき）。 */
    static constexpr int truePeakDelay = LimiterDsp::truePeakDelay;

    /** 混ぜ替えにかける時間（設計書：10 ms）。 */
    static constexpr double switchSeconds = 0.010;

private:
    //--------------------------------------------------------------------------
    /** 1チャンネルぶんのゲイン経路（ホールド → 平均 → 追加遅延）。
        **中の配列は`allocate()`で1回だけ確保**し、`copyFrom()`は中身を写すだけです。 */
    struct GainPath
    {
        void allocate (int capacity);
        void reset (int windowLength, bool triangular, int extraDelay);
        void copyFrom (const GainPath& other);

        /** 必要ゲイン r を1つ入れて、遅延を通した平均ゲインを返す。 */
        float push (float r);

        // ホールド（単調デック：値が増える順）
        std::vector<float> dequeValue;
        std::vector<int64_t> dequeIndex;
        int dequeFirst = 0, dequeCount = 0, dequeMask = 0;
        int64_t index = 0;
        int holdLength = 1;

        // 平均（1段目と、三角なら2段目）
        std::vector<float> ring1, ring2;
        int length1 = 1, length2 = 1, pos1 = 0, pos2 = 0;
        double sum1 = 0.0, sum2 = 0.0;
        bool triangular = false;

        // 追加遅延
        std::vector<float> delay;
        int delayLength = 0, delayPos = 0;

        int capacity = 0;
    };

    /** リリース（dBで持つ。戻る向きだけ遅く）。 */
    struct Release
    {
        double db = 0.0, fastDb = 0.0, slowDb = 0.0;
        double reductionSeconds = 0.0;   ///< 0.5 dB以上のGRが続いている時間（自動リリース）
    };

    float releaseStep (Release& state, float attackGain);

    void rebuildActivePaths();
    void computeWindow (int& S, int& B) const;

    double sampleRate = 48000.0;
    int lookahead = 1, tpDelay = 0, padding = 0;
    bool truePeak = false;
    int capacity = 0;

    Dynamics dynamics;
    int windowS = 1, windowB = 1;
    bool windowTriangular = true;

    // チャンネルごと
    std::array<GainPath, maxChannels> active, fading;
    std::array<Release, maxChannels> release;
    std::array<std::vector<float>, maxChannels> audioDelay;
    std::array<std::vector<float>, maxChannels> history;       ///< 必要ゲイン r の履歴（流し直し用）
    std::array<LimiterDsp::TruePeakDetector, maxChannels> detectors;   ///< トゥルーピークの見張り（出力のTP計と同じもの）
    std::array<float, maxChannels> previousIntervalPeak {};
    int audioPos = 0, historyPos = 0;
    int historyLength = 0, audioLength = 0;

    int fadeSamplesLeft = 0, fadeSamplesTotal = 1;

    std::vector<float> grTrace;

    // リリースの係数（`setDynamics()`で作る）
    double alphaManual = 0.0, alphaFast = 0.0, alphaSlow = 0.0;
};
