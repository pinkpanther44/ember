#include "LimiterMeters.h"

#include <algorithm>

namespace
{
    constexpr double pi = 3.14159265358979323846;

    inline float linearToDb (float v) noexcept
    {
        return v > 1.0e-7f ? 20.0f * std::log10 (v) : LimiterMeters::silenceDb;
    }
}

//==============================================================================

void LimiterMeters::Histogram::reset()
{
    count.assign ((size_t) histogramBins, 0u);
    energy.assign ((size_t) histogramBins, 0.0);
}

void LimiterMeters::Histogram::add (double meanSquare)
{
    const float lufs = energyToLufs (meanSquare);

    // **絶対ゲート −70 LUFS より下は入れない**（ヒストグラムの範囲の外）
    if (lufs < -70.0f)
        return;

    const int bin = binFor (lufs);
    ++count[(size_t) bin];
    energy[(size_t) bin] += meanSquare;
}

float LimiterMeters::energyToLufs (double meanSquare) noexcept
{
    return meanSquare > 1.0e-20 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : silenceDb;
}

int LimiterMeters::binFor (float lufs) noexcept
{
    return std::clamp ((int) std::floor ((lufs + 70.0f) / 0.1f), 0, histogramBins - 1);
}

//==============================================================================

void LimiterMeters::prepare (double rate, int numChannels)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    channels = std::clamp (numChannels, 1, 2);

    //--------------------------------------------------------------------------
    // K特性（BS.1770-4）。**48 kHz以外でも同じ特性**になるよう、規格の係数を生んだ式から作る
    // （定数はlibebur128と同じもの。48 kHzで規格の表の値になることを自己検査で確かめています）
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / sampleRate);
        const double vh = std::pow (10.0, gainDb / 20.0);
        const double vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;

        for (auto& s : shelf)
        {
            s.b0 = (vh + vb * k / q + k * k) / a0;
            s.b1 = 2.0 * (k * k - vh) / a0;
            s.b2 = (vh - vb * k / q + k * k) / a0;
            s.a1 = 2.0 * (k * k - 1.0) / a0;
            s.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / sampleRate);
        const double a0 = 1.0 + k / q + k * k;

        for (auto& s : highpass)
        {
            s.b0 = 1.0;
            s.b1 = -2.0;
            s.b2 = 1.0;
            s.a1 = 2.0 * (k * k - 1.0) / a0;
            s.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    samplesPer100ms = std::max (1, (int) std::lround (0.1 * sampleRate));
    samplesPerFrame = std::max (1, (int) std::lround (sampleRate / 30.0));

    momentaryHistogram.reset();
    shortTermHistogram.reset();
    resetMeasurements();
}

std::array<double, 7> LimiterMeters::getKWeightingCoefficients() const noexcept
{
    return { shelf[0].b0, shelf[0].b1, shelf[0].b2, shelf[0].a1, shelf[0].a2, highpass[0].a1, highpass[0].a2 };
}

void LimiterMeters::resetMeasurements()
{
    for (auto& s : shelf)    s.z1 = s.z2 = 0.0;
    for (auto& s : highpass) s.z1 = s.z2 = 0.0;

    samplesInBlock = 0;
    blockSum = 0.0;
    recent.fill (0.0);
    recentPos = recentCount = 0;

    // ヒストグラムは**中身だけ**0に（確保し直さない）
    std::fill (momentaryHistogram.count.begin(), momentaryHistogram.count.end(), 0u);
    std::fill (momentaryHistogram.energy.begin(), momentaryHistogram.energy.end(), 0.0);
    std::fill (shortTermHistogram.count.begin(), shortTermHistogram.count.end(), 0u);
    std::fill (shortTermHistogram.energy.begin(), shortTermHistogram.energy.end(), 0.0);

    samplesInFrame = 0;
    pending = {};
    truePeakMaxDb = silenceDb;
    truePeakMaxLinear = 0.0f;
    gainReductionMaxDb = 0.0f;
    clippedSinceReset = false;

    column = {};
    samplesInColumn = 0;
}

void LimiterMeters::finishBlock()
{
    const double meanSquare = blockSum / (double) samplesPer100ms;
    blockSum = 0.0;
    samplesInBlock = 0;

    recent[(size_t) recentPos] = meanSquare;
    recentPos = (recentPos + 1) % (int) recent.size();
    recentCount = std::min (recentCount + 1, (int) recent.size());

    // Integrated：**400 ms の窓がそろってから**、100 ms ごとに（75%重なり。BS.1770）
    if (recentCount >= 4)
    {
        double m = 0.0;
        for (int i = 1; i <= 4; ++i)
            m += recent[(size_t) ((recentPos - i + 30) % 30)];

        momentaryHistogram.add (m / 4.0);
    }

    // LRA：3 s の窓がそろってから
    if (recentCount >= 30)
    {
        double s = 0.0;
        for (double v : recent)
            s += v;

        shortTermHistogram.add (s / 30.0);
    }
}

float LimiterMeters::getMomentary() const noexcept
{
    if (recentCount == 0)
        return silenceDb;

    const int n = std::min (4, recentCount);
    double m = 0.0;

    for (int i = 1; i <= n; ++i)
        m += recent[(size_t) ((recentPos - i + 30) % 30)];

    return energyToLufs (m / 4.0);
}

float LimiterMeters::getShortTerm() const noexcept
{
    if (recentCount == 0)
        return silenceDb;

    double s = 0.0;
    for (int i = 1; i <= recentCount; ++i)
        s += recent[(size_t) ((recentPos - i + 30) % 30)];

    return energyToLufs (s / 30.0);
}

float LimiterMeters::integratedFrom (const Histogram& h, float relativeGate, double* gatedEnergy) const noexcept
{
    // 絶対ゲートを通ったもの（ヒストグラムに入っているもの全部）の平均
    double energy = 0.0;
    uint64_t count = 0;

    for (int b = 0; b < histogramBins; ++b)
    {
        energy += h.energy[(size_t) b];
        count += h.count[(size_t) b];
    }

    if (count == 0)
        return silenceDb;

    const float gate = energyToLufs (energy / (double) count) + relativeGate;

    // 相対ゲート：ビンの中央がゲート以上のものだけ
    double gated = 0.0;
    uint64_t gatedCount = 0;

    for (int b = 0; b < histogramBins; ++b)
    {
        const float centre = -70.0f + 0.1f * ((float) b + 0.5f);

        if (centre >= gate)
        {
            gated += h.energy[(size_t) b];
            gatedCount += h.count[(size_t) b];
        }
    }

    if (gatedEnergy != nullptr)
        *gatedEnergy = gate;

    return gatedCount > 0 ? energyToLufs (gated / (double) gatedCount) : silenceDb;
}

float LimiterMeters::getIntegrated() const noexcept
{
    return integratedFrom (momentaryHistogram, -10.0f);
}

float LimiterMeters::getLoudnessRange() const noexcept
{
    const auto& h = shortTermHistogram;

    // 相対ゲート −20 LU（Short-term の、絶対ゲートを通ったものの平均から）
    double gateLufs = 0.0;

    if (integratedFrom (h, -20.0f, &gateLufs) <= silenceDb)
        return 0.0f;

    uint64_t total = 0;
    int firstBin = histogramBins;

    for (int b = 0; b < histogramBins; ++b)
    {
        const float centre = -70.0f + 0.1f * ((float) b + 0.5f);

        if (centre >= (float) gateLufs)
        {
            firstBin = std::min (firstBin, b);
            total += h.count[(size_t) b];
        }
    }

    if (total == 0)
        return 0.0f;

    // 10 と 95 パーセンタイル（個数で数える）
    auto percentile = [&] (double fraction)
    {
        const double wanted = fraction * (double) (total - 1);
        uint64_t seen = 0;

        for (int b = firstBin; b < histogramBins; ++b)
        {
            seen += h.count[(size_t) b];

            if ((double) seen > wanted)
                return -70.0f + 0.1f * ((float) b + 0.5f);
        }

        return -70.0f + 0.1f * ((float) histogramBins - 0.5f);
    };

    return percentile (0.95) - percentile (0.10);
}

void LimiterMeters::publishFrame()
{
    Frame f = pending;

    for (int c = 0; c < 2; ++c)
    {
        f.inputPeak[c] = linearToDb (pending.inputPeak[c]);
        f.outputPeak[c] = linearToDb (pending.outputPeak[c]);
        f.outputTruePeak[c] = linearToDb (pending.outputTruePeak[c]);
    }

    f.outputTruePeakMax = truePeakMaxDb;
    f.gainReductionMax = gainReductionMaxDb;
    f.momentary = getMomentary();
    f.shortTerm = getShortTerm();
    f.integrated = getIntegrated();
    f.loudnessRange = getLoudnessRange();
    f.peakToLoudness = (f.integrated > silenceDb && truePeakMaxDb > silenceDb) ? truePeakMaxDb - f.integrated : 0.0f;
    f.clipped = clippedSinceReset;

    frames.push (f);

    // 次の33 msぶん（ピークはリニアで溜める）
    pending = {};
    for (int c = 0; c < 2; ++c)
        pending.inputPeak[c] = pending.outputPeak[c] = pending.outputTruePeak[c] = 0.0f;
    pending.gainReductionNow = 0.0f;
}

void LimiterMeters::push (const float* const* input, const float* const* output, const float* gainReductionDb,
                          const float* const* outputTruePeak, int numChannels, int numSamples, float ceilingDb)
{
    if (resetRequested.exchange (false))
    {
        resetMeasurements();

        for (int c = 0; c < 2; ++c)
            pending.inputPeak[c] = pending.outputPeak[c] = pending.outputTruePeak[c] = 0.0f;
    }

    numChannels = std::clamp (numChannels, 1, 2);

    const float clipLevel = std::pow (10.0f, (ceilingDb + 0.1f) / 20.0f);
    const int columnLength = samplesPerColumn.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        float inMin = 0.0f, inMax = 0.0f, outAbs = 0.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            const float in = input[c][i];
            const float out = output[c][i];
            const float tp = outputTruePeak[c][i];

            // K特性の二乗和（**チャンネルの重みは1**。L・R）
            const double k = highpass[(size_t) c].process (shelf[(size_t) c].process ((double) out));
            blockSum += k * k;

            pending.inputPeak[c] = std::max (pending.inputPeak[c], std::abs (in));
            pending.outputPeak[c] = std::max (pending.outputPeak[c], std::abs (out));
            pending.outputTruePeak[c] = std::max (pending.outputTruePeak[c], tp);

            if (tp > clipLevel)
                clippedSinceReset = true;

            inMin = std::min (inMin, in);
            inMax = std::max (inMax, in);
            outAbs = std::max (outAbs, std::abs (out));

            // 最大が更新されたときだけ dB に直す（毎サンプルの log を避ける）
            if (tp > truePeakMaxLinear)
            {
                truePeakMaxLinear = tp;
                truePeakMaxDb = linearToDb (tp);
            }
        }

        const float gr = gainReductionDb[i];
        pending.gainReductionNow = std::min (pending.gainReductionNow, gr);
        gainReductionMaxDb = std::min (gainReductionMaxDb, gr);

        if (++samplesInBlock >= samplesPer100ms)
            finishBlock();

        if (++samplesInFrame >= samplesPerFrame)
        {
            samplesInFrame = 0;
            publishFrame();
        }

        // スクロール表示の列
        column.inMin = std::min (column.inMin, inMin);
        column.inMax = std::max (column.inMax, inMax);
        column.outAbsMax = std::max (column.outAbsMax, outAbs);
        column.grMax = std::min (column.grMax, gr);

        if (++samplesInColumn >= columnLength)
        {
            columns.push (column);
            column = {};
            samplesInColumn = 0;
        }
    }
}
