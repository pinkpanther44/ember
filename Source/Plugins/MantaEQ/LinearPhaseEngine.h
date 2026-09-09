#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

//==============================================================================
/**
    設計書4.2「Linear Phaseパス」（EQ仕様書4.7・フェーズ6）。

    ### 何をしているか

    Zero Latencyパス（`EQFilterDesign`）と**同じ振幅特性**を、位相を一切ずらさずに
    掛けます。手順は設計書のとおりです。

    ```
    バンドの係数 → 各周波数での振幅 → 位相ゼロのスペクトル
        → 逆FFT → 窓を掛けて有限長のインパルス応答
        → オーバーラップアドで畳み込み
    ```

    ### レイテンシーは「処理解像度」そのもの

    | 処理解像度（IRの長さ M） | レイテンシー | 48kHzで |
    |---|---|---|
    | 1024 | 1024 サンプル | 21 ms |
    | 2048 | 2048 | 43 ms |
    | 4096 | 4096 | 85 ms |
    | 8192 | 8192 | 171 ms |

    内訳は**溜めるぶん（H＝M/2）＋インパルス応答の中心（M/2）**で、合わせてMです。
    ホストへは`setLatencySamples()`で申告します（HANDOVER 9.5）。

    低いほうまで正確に効かせるにはIRを長くする必要があり、そのぶん遅れます——
    **これがPro-Qの「処理解像度」と同じトレードオフ**です。

    ### M/S を含めても、畳み込みは2チャンネルぶんで済む

    位相ゼロのフィルタは**周波数ごとにただの実数倍**なので、
    L/R向けのフィルタとM/S向けのフィルタが**掛け算として交換できます**。
    そのため、全体を **2×2 の行列** 1枚にまとめられます。

    ```
    Y_L = H_L ・[ (H_M + H_S)/2 ・L + (H_M − H_S)/2 ・R ]
    Y_R = H_R ・[ (H_M − H_S)/2 ・L + (H_M + H_S)/2 ・R ]
    ```

    掛け算が4回に増えるだけで、**FFTは前後2回ずつのまま**です
    （M/Sのために畳み込みを2段重ねると、レイテンシーが2倍になります）。

    M/Sのバンドが1本も無ければ`H_M = H_S = 1`となり、
    行列は自動的に対角（＝L/Rを別々に掛けるだけ）になります。

    ### 核の作り直しは、音のスレッドでフレームの頭に

    作り直しはFFT8回ぶん（＝1フレームの処理2回ぶん）で、
    **フレームの間隔（H サンプル）に対して常に同じ割合**です。
    解像度を上げると重くなりますが、そのぶんフレームの間隔も伸びます。

    つまみを回しっぱなしでも**FFTの仕事が3倍になるだけ**なので、
    別スレッドと二重バッファを持ち込むより、こちらのほうが確かです。

    ### 確保はしない

    バッファもFFTも**いちばん大きい解像度に合わせて先に用意**してあります。
    解像度を変えても、変わるのは使う長さと`juce::dsp::FFT`の選び先だけです。
*/
class LinearPhaseEngine
{
public:
    LinearPhaseEngine();
    ~LinearPhaseEngine();

    /** 選べる処理解像度（＝インパルス応答の長さ）。**短いほうから並べること。** */
    static constexpr int resolutionChoices[] { 1024, 2048, 4096, 8192 };
    static constexpr int numResolutionChoices = 4;
    static constexpr int maxIrLength = 8192;
    static constexpr int maxFftSize = maxIrLength * 2;

    /** レートが変わったとき（`prepareToPlay()`）に呼ぶ。**中身も消えます。** */
    void prepare (double sampleRateToUse);

    /** 溜めているものを捨てる（モードを切り替えたとき、再生位置が飛んだとき）。 */
    void reset();

    /** 処理解像度を変える。**変わったときだけ**中身を作り直します。 */
    void setIrLength (int irLengthSamples);

    int getIrLength() const noexcept { return irLength; }

    /** ホストへ申告する遅れ（サンプル）。 */
    int getLatencySamples() const noexcept { return irLength; }

    //==========================================================================
    // 核を作るための入口。**呼ぶ側が振幅を書き込んでから`markKernelsDirty()`**。

    int getNumBins() const noexcept { return fftSize / 2 + 1; }

    /** `bin`番目の周波数（Hz）。 */
    double getBinFrequency (int bin) const noexcept;

    /** 振幅を書き込む先。`which`は 0=L / 1=R / 2=Mid / 3=Side。
        **長さは`getNumBins()`**。1.0が0dB。 */
    float* getMagnitudeBuffer (int which) noexcept { return magnitudes[(size_t) which].data(); }

    /** すべて1.0（＝素通り）に戻す。書き込む前に呼ぶこと。 */
    void resetMagnitudes() noexcept;

    /** 次のフレームの頭で核を作り直させる。 */
    void markKernelsDirty() noexcept { kernelsDirty = true; }

    //==========================================================================
    /** 音を通す。**`right`はnullptrでもよい**（モノ）。 */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    void rebuildKernels() noexcept;
    void transformFrame() noexcept;

    /** いまの解像度に対応する`juce::dsp::FFT`。 */
    const juce::dsp::FFT& getFft() const noexcept;

    int irLength = resolutionChoices[2];
    int fftSize = irLength * 2;
    int hopSize = irLength / 2;

    double sampleRate = 44100.0;
    bool kernelsDirty = true;

    /** 解像度ごとのFFT（作り直さないよう先に全部持っておく）。 */
    std::vector<std::unique_ptr<juce::dsp::FFT>> ffts;

    // 2×2の行列（A11, A12, A21, A22）を周波数ごとに持ったもの
    std::array<std::vector<juce::dsp::Complex<float>>, 4> kernels;

    std::array<std::vector<float>, 4> magnitudes;

    std::array<std::vector<float>, 2> inputFifo, outputFifo, overlapTail;
    std::vector<juce::dsp::Complex<float>> scratchA, scratchB;
    std::array<std::vector<juce::dsp::Complex<float>>, 2> channelSpectrum;

    int fifoPosition = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinearPhaseEngine)
};
