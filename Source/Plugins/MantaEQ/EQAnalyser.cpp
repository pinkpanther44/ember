#include "EQAnalyser.h"

#include "MantaEQParameters.h"

#include <cmath>

//==============================================================================

EQAnalyserFifo::EQAnalyserFifo()
    : buffer ((size_t) bufferSize, 0.0f)
{
}

void EQAnalyserFifo::reset() noexcept
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePosition.store (0, std::memory_order_release);
}

void EQAnalyserFifo::push (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    int position = writePosition.load (std::memory_order_relaxed);

    const float scale = 1.0f / (float) numChannels;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            sum += channels[ch][i];

        buffer[(size_t) position] = sum * scale;
        position = (position + 1) & (bufferSize - 1);
    }

    writePosition.store (position, std::memory_order_release);
}

void EQAnalyserFifo::readLatest (float* destination, int numSamples) const noexcept
{
    if (destination == nullptr || numSamples <= 0)
        return;

    const int limited = juce::jmin (numSamples, bufferSize);
    const int end = writePosition.load (std::memory_order_acquire);

    int position = (end - limited + bufferSize) & (bufferSize - 1);

    for (int i = 0; i < limited; ++i)
    {
        destination[i] = buffer[(size_t) position];
        position = (position + 1) & (bufferSize - 1);
    }
}

//==============================================================================

EQSpectrum::EQSpectrum()
    : fftBuffer ((size_t) (fftSize * 2), 0.0f)
{
    for (int i = 0; i < numPoints; ++i)
    {
        const float proportion = (float) i / (float) (numPoints - 1);
        frequencies[(size_t) i] = MantaEQParams::proportionToFrequency (proportion);
    }

    clear();
}

void EQSpectrum::prepare (double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
    clear();
}

void EQSpectrum::clear()
{
    magnitudesDb.fill (floorDb);
}

void EQSpectrum::update (const EQAnalyserFifo& fifo, float speed, bool frozen)
{
    if (frozen)
        return;

    // **毎回ゼロで埋め直すこと。** FFTは後半（虚部の置き場）まで使うので、
    // 前のフレームの値が残っていると窓の外に音があることになります
    std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);

    fifo.readLatest (fftBuffer.data(), fftSize);

    window.multiplyWithWindowingTable (fftBuffer.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data());

    // Hann窓を掛けたぶんの目減り（平均0.5）と、FFTの点数で割って戻す。
    // **絶対値としての正しさより、0dBFSの正弦波が0dB付近に出ること**を合わせている
    const float normalise = 2.0f / ((float) fftSize * 0.5f);

    const int numBins = fftSize / 2;
    const double binWidth = sampleRate / (double) fftSize;

    // 落ちる速さ。`speed`は0（ゆっくり）〜1（速い）
    const float decayDb = juce::jmap (juce::jlimit (0.0f, 1.0f, speed), 1.0f, 14.0f);

    for (int point = 0; point < numPoints; ++point)
    {
        const double centreHz = frequencies[(size_t) point];

        // この点が受け持つ周波数の幅（隣の点との中間まで）
        const double lowHz  = point > 0
                                ? std::sqrt (centreHz * (double) frequencies[(size_t) (point - 1)])
                                : centreHz * 0.97;
        const double highHz = point < numPoints - 1
                                ? std::sqrt (centreHz * (double) frequencies[(size_t) (point + 1)])
                                : centreHz * 1.03;

        const int firstBin = (int) std::ceil (lowHz / binWidth);
        const int lastBin  = (int) std::floor (highHz / binWidth);

        float magnitude = 0.0f;

        if (lastBin >= firstBin && firstBin >= 0)
        {
            // **幅にビンが入っているときは、その中の最大**（細いピークを残す）
            for (int bin = firstBin; bin <= juce::jmin (lastBin, numBins - 1); ++bin)
                magnitude = juce::jmax (magnitude, fftBuffer[(size_t) bin]);
        }
        else
        {
            // **1つも入らないときは補間**（低いほうはビンより点のほうが細かい）
            const double exact = centreHz / binWidth;
            const int bin = juce::jlimit (0, numBins - 2, (int) exact);
            const float fraction = (float) (exact - (double) bin);

            magnitude = fftBuffer[(size_t) bin] * (1.0f - fraction)
                          + fftBuffer[(size_t) (bin + 1)] * fraction;
        }

        const float levelDb = juce::Decibels::gainToDecibels (magnitude * normalise, floorDb);
        auto& stored = magnitudesDb[(size_t) point];

        // **上がるのは即座、下がるのはゆっくり。**
        // 上がりも遅くすると、短い音がまったく見えません
        stored = levelDb > stored ? levelDb
                                   : juce::jmax (levelDb, stored - decayDb);
    }
}
