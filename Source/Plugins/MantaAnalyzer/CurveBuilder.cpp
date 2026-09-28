#include "CurveBuilder.h"

#include <juce_core/juce_core.h>

#include <cmath>

double CurveBuilder::frequencyAtX (double x) const
{
    if (width <= 1)
        return minHz;

    return minHz * std::pow (maxHz / minHz, x / (double) (width - 1));
}

double CurveBuilder::xForFrequency (double hz) const
{
    if (width <= 1 || hz <= 0.0)
        return 0.0;

    return (double) (width - 1) * std::log (hz / minHz) / std::log (maxHz / minHz);
}

void CurveBuilder::setAxis (double fMin, double fMax, int widthPx, double binHz, int numBins)
{
    if (fMin == minHz && fMax == maxHz && widthPx == width && binHz == bin && numBins == bins)
        return;

    minHz = fMin;
    maxHz = fMax;
    width = juce::jmax (0, widthPx);
    bin = binHz;
    bins = numBins;

    columns.assign ((size_t) width, {});
    interpolationTop = 1;

    for (int x = 0; x < width; ++x)
    {
        auto& column = columns[(size_t) x];

        const double fLo = frequencyAtX (x - 0.5);
        const double fHi = frequencyAtX (x + 0.5);

        // 受け持つ幅に入るビン（k·binHz が [fLo, fHi) にあるもの）
        const int lo = juce::jmax (1, (int) std::ceil (fLo / binHz));
        const int hi = juce::jmin (numBins - 1, (int) std::ceil (fHi / binHz) - 1);

        column.logF = std::log2 (frequencyAtX (x));

        if (hi - lo + 1 >= 2)
        {
            column.averaged = true;
            column.lo = lo;
            column.hi = hi;
        }
        else
        {
            // 補間：前後のビンが要る。ここまでのビンで単調三次を作る
            const int above = juce::jmin (numBins - 1, (int) std::ceil (frequencyAtX (x) / binHz) + 1);
            interpolationTop = juce::jmax (interpolationTop, above);
        }
    }

    knotX.assign ((size_t) interpolationTop, 0.0);
    knotY.assign ((size_t) interpolationTop, 0.0);

    for (int k = 1; k <= interpolationTop && k < numBins; ++k)
        knotX[(size_t) k - 1] = std::log2 (k * binHz);
}

void CurveBuilder::build (const std::vector<float>& binDb, std::vector<float>& out)
{
    out.resize ((size_t) width);

    if (width == 0 || (int) binDb.size() < bins)
        return;

    // 補間に使う部分だけで単調三次を作る（DCのビン0は使わない）
    const int knots = juce::jmin (interpolationTop, bins - 1);

    for (int k = 1; k <= knots; ++k)
        knotY[(size_t) k - 1] = binDb[(size_t) k];

    spline.set (knotX.data(), knotY.data(), knots);

    for (int x = 0; x < width; ++x)
    {
        const auto& column = columns[(size_t) x];

        if (column.averaged)
        {
            // **パワーで平均する**（dBのまま平均すると、谷が深く見える）
            double sum = 0.0;

            for (int k = column.lo; k <= column.hi; ++k)
                sum += std::pow (10.0, binDb[(size_t) k] / 10.0);

            const double mean = sum / (double) (column.hi - column.lo + 1);
            out[(size_t) x] = mean > 1.0e-12 ? (float) (10.0 * std::log10 (mean)) : -120.0f;
        }
        else
        {
            out[(size_t) x] = (float) spline (column.logF);
        }
    }
}
