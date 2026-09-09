#include "LinearPhaseEngine.h"

#include <cmath>

namespace
{
    /** 2のべき乗の指数。`fftSize`から`ffts`の添字を出すのに使う。 */
    int orderOf (int powerOfTwo) noexcept
    {
        int order = 0;

        while ((1 << order) < powerOfTwo)
            ++order;

        return order;
    }
}

//==============================================================================

LinearPhaseEngine::LinearPhaseEngine()
{
    // **FFTは先に全部作る。** 解像度を変えるたびに作り直すと、
    // そこで確保が走ります（`processBlock()`から呼ばれ得る）
    for (int i = 0; i < numResolutionChoices; ++i)
        ffts.push_back (std::make_unique<juce::dsp::FFT> (orderOf (resolutionChoices[i] * 2)));

    const size_t maxBins = (size_t) (maxFftSize / 2 + 1);

    for (auto& kernel : kernels)
        kernel.assign ((size_t) maxFftSize, {});

    for (auto& magnitude : magnitudes)
        magnitude.assign (maxBins, 1.0f);

    for (int channel = 0; channel < 2; ++channel)
    {
        inputFifo[(size_t) channel].assign ((size_t) (maxIrLength / 2), 0.0f);
        outputFifo[(size_t) channel].assign ((size_t) (maxIrLength / 2), 0.0f);
        overlapTail[(size_t) channel].assign ((size_t) maxFftSize, 0.0f);
        channelSpectrum[(size_t) channel].assign ((size_t) maxFftSize, {});
    }

    scratchA.assign ((size_t) maxFftSize, {});
    scratchB.assign ((size_t) maxFftSize, {});

    setIrLength (resolutionChoices[2]);
}

LinearPhaseEngine::~LinearPhaseEngine() = default;

const juce::dsp::FFT& LinearPhaseEngine::getFft() const noexcept
{
    for (int i = 0; i < numResolutionChoices; ++i)
        if (resolutionChoices[i] == irLength)
            return *ffts[(size_t) i];

    return *ffts[0];
}

void LinearPhaseEngine::prepare (double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;

    reset();
    kernelsDirty = true;
}

void LinearPhaseEngine::reset()
{
    for (int channel = 0; channel < 2; ++channel)
    {
        std::fill (inputFifo[(size_t) channel].begin(), inputFifo[(size_t) channel].end(), 0.0f);
        std::fill (outputFifo[(size_t) channel].begin(), outputFifo[(size_t) channel].end(), 0.0f);
        std::fill (overlapTail[(size_t) channel].begin(), overlapTail[(size_t) channel].end(), 0.0f);
    }

    fifoPosition = 0;
}

void LinearPhaseEngine::setIrLength (int irLengthSamples)
{
    int wanted = resolutionChoices[0];

    for (int i = 0; i < numResolutionChoices; ++i)
        if (resolutionChoices[i] == irLengthSamples)
            wanted = irLengthSamples;

    if (wanted == irLength)
        return;

    irLength = wanted;
    fftSize = irLength * 2;
    hopSize = irLength / 2;

    reset();
    kernelsDirty = true;
}

double LinearPhaseEngine::getBinFrequency (int bin) const noexcept
{
    return (double) bin * sampleRate / (double) fftSize;
}

void LinearPhaseEngine::resetMagnitudes() noexcept
{
    const int numBins = getNumBins();

    for (auto& magnitude : magnitudes)
        std::fill (magnitude.begin(), magnitude.begin() + numBins, 1.0f);
}

//==============================================================================

void LinearPhaseEngine::rebuildKernels() noexcept
{
    kernelsDirty = false;

    const auto& fft = getFft();
    const int numBins = getNumBins();
    const int halfIr = irLength / 2;

    for (int which = 0; which < 4; ++which)
    {
        auto& kernel = kernels[(size_t) which];

        // ---- ① 2×2の行列の1マスぶんの、周波数ごとの実数を作る -------------
        //
        // Y_L = H_L・[ (H_M+H_S)/2・L + (H_M−H_S)/2・R ]
        // Y_R = H_R・[ (H_M−H_S)/2・L + (H_M+H_S)/2・R ]
        //
        // **符号が負になることがあります**（SideのほうがMidより大きいとき）。
        // 位相が0かπかというだけで、**インパルス応答は左右対称のまま**です
        for (int bin = 0; bin < numBins; ++bin)
        {
            const float hl = magnitudes[0][(size_t) bin];
            const float hr = magnitudes[1][(size_t) bin];
            const float hm = magnitudes[2][(size_t) bin];
            const float hs = magnitudes[3][(size_t) bin];

            const float sum = 0.5f * (hm + hs);
            const float difference = 0.5f * (hm - hs);

            float value = 0.0f;

            switch (which)
            {
                case 0: value = hl * sum;        break;   // A11
                case 1: value = hl * difference; break;   // A12
                case 2: value = hr * difference; break;   // A21
                default: value = hr * sum;       break;   // A22
            }

            scratchA[(size_t) bin] = { value, 0.0f };

            // 実数の信号にするため、**折り返しにも同じ値を入れる**（エルミート対称）
            if (bin > 0 && bin < fftSize - bin)
                scratchA[(size_t) (fftSize - bin)] = { value, 0.0f };
        }

        // ---- ② 逆FFT → 位相ゼロのインパルス応答（0を中心に左右対称） -------
        fft.perform (scratchA.data(), scratchB.data(), true);

        // ---- ③ 中心をM/2へずらし、窓を掛けて長さMで打ち切る ----------------
        //
        // **窓が要ります。** そのまま切ると、切り口の段差が
        // 周波数特性のうねり（ギブス現象）になって出ます。
        // Hann窓は「なまり方」と「うねりの少なさ」の間で素直な選び方です
        std::fill (scratchA.begin(), scratchA.begin() + fftSize, juce::dsp::Complex<float> {});

        for (int i = 0; i < irLength; ++i)
        {
            const int source = ((i - halfIr) % fftSize + fftSize) % fftSize;

            const float window = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                          * (float) i / (float) (irLength - 1));

            scratchA[(size_t) i] = { scratchB[(size_t) source].real() * window, 0.0f };
        }

        // ---- ④ 畳み込みに使う形（周波数領域）へ -----------------------------
        fft.perform (scratchA.data(), kernel.data(), false);
    }
}

//==============================================================================

void LinearPhaseEngine::transformFrame() noexcept
{
    if (kernelsDirty)
        rebuildKernels();

    const auto& fft = getFft();

    // ---- 入力（hopサンプル）をゼロ詰めして周波数へ ---------------------------
    for (int channel = 0; channel < 2; ++channel)
    {
        std::fill (scratchA.begin(), scratchA.begin() + fftSize, juce::dsp::Complex<float> {});

        for (int i = 0; i < hopSize; ++i)
            scratchA[(size_t) i] = { inputFifo[(size_t) channel][(size_t) i], 0.0f };

        fft.perform (scratchA.data(), channelSpectrum[(size_t) channel].data(), false);
    }

    // ---- 2×2の行列を掛ける ---------------------------------------------------
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& a = kernels[(size_t) (channel * 2)];       // A11 / A21
        const auto& b = kernels[(size_t) (channel * 2 + 1)];   // A12 / A22

        // **どちらの出力も、掛ける相手は入力のLとR**（行列の1行ぶん）
        const auto& inputLeft  = channelSpectrum[0];
        const auto& inputRight = channelSpectrum[1];

        for (int bin = 0; bin < fftSize; ++bin)
            scratchA[(size_t) bin] = a[(size_t) bin] * inputLeft[(size_t) bin]
                                       + b[(size_t) bin] * inputRight[(size_t) bin];

        fft.perform (scratchA.data(), scratchB.data(), true);

        // ---- オーバーラップアド ---------------------------------------------
        //
        // 畳み込みの結果は `hop + M - 1` サンプル。前半のhopぶんが出力、
        // 残りは次のフレームへ持ち越す
        auto& tail = overlapTail[(size_t) channel];
        auto& out = outputFifo[(size_t) channel];

        for (int i = 0; i < hopSize; ++i)
            out[(size_t) i] = scratchB[(size_t) i].real() + tail[(size_t) i];

        const int tailLength = irLength - 1;

        // **前から順に書くこと。** 読むのは`hopSize + j`（いま書く`j`より必ず後ろ）
        // なので、控えを取らずに詰められる
        for (int j = 0; j < tailLength; ++j)
        {
            const float carried = (hopSize + j) < tailLength ? tail[(size_t) (hopSize + j)] : 0.0f;

            tail[(size_t) j] = scratchB[(size_t) (hopSize + j)].real() + carried;
        }
    }
}

void LinearPhaseEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || numSamples <= 0)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        inputFifo[0][(size_t) fifoPosition] = left[n];
        inputFifo[1][(size_t) fifoPosition] = right != nullptr ? right[n] : left[n];

        left[n] = outputFifo[0][(size_t) fifoPosition];

        if (right != nullptr)
            right[n] = outputFifo[1][(size_t) fifoPosition];

        if (++fifoPosition >= hopSize)
        {
            fifoPosition = 0;
            transformFrame();
        }
    }
}
