#include "PitchDetector.h"

// 8.336：実数FFTは Signalsmith Linear（変換エンジンと同じライブラリ。MIT）
#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>

struct PitchDetector::Fft
{
    signalsmith::linear::RealFFT<float> transform;
    std::vector<float> windowTime, segmentTime, correlation;
    std::vector<std::complex<float>> windowSpectrum, segmentSpectrum;
};

PitchDetector::PitchDetector() : fft (std::make_unique<Fft>()) {}
PitchDetector::~PitchDetector() = default;

namespace
{
    float noteFromFrequency (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    float frequencyFromNote (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
}

void PitchDetector::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    // 時間で決める（仕様書5章）。48 kHz で 1536／48／600
    windowLength = juce::roundToInt (rate * 0.032);
    minLag = juce::jmax (2, (int) std::floor (rate / maxFrequency));
    maxLag = (int) std::ceil (rate / minFrequency);

    // 相関の回り込みが τ≥0 側へ入らない長さ（N ≥ W＋τmax）
    // 2のべき乗に限らない（2・3・5の積）。48 kHz で 4096 ではなく 2160 前後になり、**検出の CPU が半分近く**になる（8.336）
    fftSize = (int) signalsmith::linear::RealFFT<float>::fastSizeAbove ((size_t) (windowLength + maxLag));
    fftSize += fftSize % 2;
    fft->transform.resize ((size_t) fftSize);
    fft->windowTime.assign ((size_t) fftSize, 0.0f);
    fft->segmentTime.assign ((size_t) fftSize, 0.0f);
    fft->correlation.assign ((size_t) fftSize, 0.0f);
    fft->windowSpectrum.assign ((size_t) fftSize / 2, {});
    fft->segmentSpectrum.assign ((size_t) fftSize / 2, {});
    prefixEnergy.assign ((size_t) (windowLength + maxLag + 1), 0.0f);
    difference.assign ((size_t) (maxLag + 2), 0.0f);

    reset();
}

void PitchDetector::reset()
{
    recent.fill (0.0f);
    recentCount = recentWrite = 0;
    acceptedNote = -1.0f;
    octaveJumpRun = 0;
    lastFrequency = 0.0f;
}

PitchDetector::Result PitchDetector::analyseRaw (const float* segment)
{
    Result result;
    const int length = windowLength + maxLag;

    // 二乗の累積和（e(τ)＝区間 [τ, τ+W) の二乗和）と、全体のRMS
    double running = 0.0;
    prefixEnergy[0] = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        running += (double) segment[i] * segment[i];
        prefixEnergy[(size_t) i + 1] = (float) running;
    }

    const double meanSquare = running / length;
    result.rmsDb = meanSquare > 1.0e-20 ? (float) (10.0 * std::log10 (meanSquare)) : -200.0f;

    if (result.rmsDb < silenceDb)
        return result;   // 無声（小さすぎる）

    // r(τ)：窓（先頭 W）と区間（W＋τmax）の相互相関を FFT で
    auto& f = *fft;
    std::fill (f.windowTime.begin(), f.windowTime.end(), 0.0f);
    std::fill (f.segmentTime.begin(), f.segmentTime.end(), 0.0f);
    std::copy (segment, segment + windowLength, f.windowTime.begin());
    std::copy (segment, segment + length, f.segmentTime.begin());

    f.transform.fft (f.windowTime.data(), f.windowSpectrum.data());
    f.transform.fft (f.segmentTime.data(), f.segmentSpectrum.data());

    // conj(A)·B。0番は直流とナイキストの実数が2つ詰めてあるので、別々に掛ける
    const auto a0 = f.windowSpectrum[0], b0 = f.segmentSpectrum[0];
    f.segmentSpectrum[0] = { a0.real() * b0.real(), a0.imag() * b0.imag() };

    for (int k = 1; k < fftSize / 2; ++k)
        f.segmentSpectrum[(size_t) k] = std::conj (f.windowSpectrum[(size_t) k]) * f.segmentSpectrum[(size_t) k];

    f.transform.ifft (f.segmentSpectrum.data(), f.correlation.data());

    // 逆変換の倍率はライブラリの流儀に依らないよう、**τ=0 の相関＝窓の二乗和**から出す
    const float e0 = prefixEnergy[(size_t) windowLength];
    const float scale = std::abs (f.correlation[0]) > 1.0e-30f ? e0 / f.correlation[0] : 0.0f;

    for (int tau = 0; tau <= maxLag; ++tau)
        f.correlation[(size_t) tau] *= scale;

    const float* correlation = f.correlation.data();   // 相関 r(τ)

    // d(τ) と、累積平均で正規化したもの（CMNDF）
    difference[0] = 1.0f;
    double cumulative = 0.0;

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        const float eTau = prefixEnergy[(size_t) (tau + windowLength)] - prefixEnergy[(size_t) tau];
        const float d = juce::jmax (0.0f, e0 + eTau - 2.0f * correlation[tau]);
        cumulative += d;
        difference[(size_t) tau] = cumulative > 0.0 ? (float) (d * tau / cumulative) : 1.0f;
    }

    // 閾値を最初に下回った谷（下り切ったところ）。無ければ範囲の最小
    int best = -1;

    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (difference[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxLag && difference[(size_t) tau + 1] < difference[(size_t) tau])
                ++tau;

            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = minLag;

        for (int tau = minLag + 1; tau <= maxLag; ++tau)
            if (difference[(size_t) tau] < difference[(size_t) best])
                best = tau;
    }

    // 放物線で小数の周期へ
    double period = best;

    if (best > 1 && best < maxLag)
    {
        const double a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
        const double denominator = a - 2.0 * b + c;

        if (denominator > 1.0e-12)
            period = best + juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / denominator);
    }

    result.aperiodicity = difference[(size_t) best];
    result.voiced = result.aperiodicity < voicedLimit;
    result.rawFrequency = result.voiced ? (float) (rate / period) : 0.0f;
    result.frequency = result.rawFrequency;
    return result;
}

PitchDetector::Result PitchDetector::analyse (const float* segment)
{
    auto result = analyseRaw (segment);

    if (! result.voiced)
    {
        // 無声が挟まったら履歴を捨てる（次の音は前の音と関係ない）
        recentCount = recentWrite = 0;
        octaveJumpRun = 0;
        acceptedNote = -1.0f;
        result.frequency = lastFrequency;
        return result;
    }

    // 直近5回の中央値（音高で）
    const float rawNote = noteFromFrequency (result.rawFrequency);
    recent[(size_t) recentWrite] = rawNote;
    recentWrite = (recentWrite + 1) % (int) recent.size();
    recentCount = juce::jmin (recentCount + 1, (int) recent.size());

    std::array<float, 5> sorted {};
    std::copy (recent.begin(), recent.begin() + recentCount, sorted.begin());
    std::sort (sorted.begin(), sorted.begin() + recentCount);
    const float median = sorted[(size_t) recentCount / 2];

    // ±1オクターブの跳びは3回続くまで採らない。**数えるのは生の値**（中央値で数えると、
    // 中央値が入れ替わるのに3回＋跳びの3回で、本物の跳躍を採るまで5回＝27 ms かかった）
    auto isOctaveJump = [] (float a, float b) { return std::abs (std::abs (a - b) - 12.0f) < 1.0f; };

    octaveJumpRun = (acceptedNote >= 0.0f && isOctaveJump (rawNote, acceptedNote)) ? octaveJumpRun + 1 : 0;

    if (acceptedNote >= 0.0f && isOctaveJump (median, acceptedNote) && octaveJumpRun < 3)
    {
        result.frequency = frequencyFromNote (acceptedNote);
        lastFrequency = result.frequency;
        return result;
    }

    acceptedNote = median;
    result.frequency = frequencyFromNote (median);
    lastFrequency = result.frequency;
    return result;
}
