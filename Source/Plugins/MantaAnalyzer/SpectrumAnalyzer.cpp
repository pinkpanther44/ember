#include "SpectrumAnalyzer.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <limits>

namespace
{
    /** レートに応じてFFTを何倍にするか（ビンの幅を 48 kHz・設定値のときの近くに保つ）。 */
    int sizeMultiplierFor (double sampleRate)
    {
        if (sampleRate >= 176400.0) return 4;
        if (sampleRate >= 88200.0)  return 2;
        return 1;
    }

    constexpr int popChunk = 4096;
}

SpectrumAnalyzer::SpectrumAnalyzer()
{
    popL.assign (popChunk, 0.0f);
    popR.assign (popChunk, 0.0f);
}

SpectrumAnalyzer::~SpectrumAnalyzer() = default;

float SpectrumAnalyzer::powerToDb (double p) noexcept
{
    if (! (p > 1.0e-12))
        return floorDb;

    return juce::jmax (floorDb, (float) (10.0 * std::log10 (p)));
}

double SpectrumAnalyzer::getFrameSeconds() const noexcept
{
    return (double) hop / sampleRate;
}

bool SpectrumAnalyzer::needsPrepare (double rate, const AnalyzerSettings& s) const
{
    return fft == nullptr
        || rate != sampleRate
        || s.fftSize != preparedWith.fftSize
        || s.window != preparedWith.window
        || s.overlap != preparedWith.overlap;
}

void SpectrumAnalyzer::prepare (double rate, const AnalyzerSettings& s)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    settings = s;
    preparedWith = s;

    fftSize = s.fftSize * sizeMultiplierFor (sampleRate);

    // **窓の長さの2倍でFFTする（後ろ半分は0）**。ビンの間隔が半分になり、
    // 正弦波がビンとビンのあいだに落ちたときの目減り（Hannで最大1.4 dB）が0.35 dB以内になります。
    // 1 kHzは48 kHz・8192点で**ビンの1/3ずれ**に当たり、そのままだと −0.6 dB に見えました
    // （仕様書7.3「0 dB ±0.5 dB」を満たせない）。雑音の高さは変わりません
    transformSize = fftSize * zeroPadding;

    int order = 0;
    while ((1 << order) < transformSize)
        ++order;

    fft = std::make_unique<juce::dsp::FFT> (order);

    numBins = transformSize / 2 + 1;
    binHz = sampleRate / (double) transformSize;
    hop = juce::jmax (1, (int) std::lround (fftSize * (1.0 - (double) s.overlap)));

    //--------------------------------------------------------------------------
    // 窓（周期形。オーバーラップ加算の都合ではなく、解析なので対称でも構わないが、
    // 端の0が2つ並ばないほうが同じ長さで分解能が少しよい）

    window.assign ((size_t) fftSize, 0.0f);
    windowSum = 0.0;

    for (int n = 0; n < fftSize; ++n)
    {
        const double phase = juce::MathConstants<double>::twoPi * (double) n / (double) fftSize;
        double w;

        if (s.window == AnalyzerSettings::Window::blackmanHarris)
            w = 0.35875 - 0.48829 * std::cos (phase) + 0.14128 * std::cos (2.0 * phase)
                        - 0.01168 * std::cos (3.0 * phase);
        else
            w = 0.5 - 0.5 * std::cos (phase);

        window[(size_t) n] = (float) w;
        windowSum += w;
    }

    historyL.assign ((size_t) fftSize, 0.0f);
    historyR.assign ((size_t) fftSize, 0.0f);
    mixed.assign ((size_t) fftSize, 0.0f);
    scratch.assign ((size_t) transformSize * 2, 0.0f);
    magnitudesL.assign ((size_t) numBins, 0.0f);
    magnitudesR.assign ((size_t) numBins, 0.0f);

    power.assign ((size_t) numBins, 0.0);
    smoothed.assign ((size_t) numBins, 0.0);
    prefix.assign ((size_t) numBins + 1, 0.0);
    averagedPower.assign ((size_t) numBins, 0.0);
    averagedDb.assign ((size_t) numBins, floorDb);
    frameDb.assign ((size_t) numBins, floorDb);

    //--------------------------------------------------------------------------
    // 基準帯域（100 Hz〜10 kHz を 1/6 オクターブずつ。設計書4.5）

    referenceBands.clear();

    for (int i = 0;; ++i)
    {
        const double fLo = 100.0 * std::pow (2.0, i / 6.0);

        if (fLo >= 10000.0)
            break;

        const double fHi = juce::jmin (10000.0, 100.0 * std::pow (2.0, (i + 1) / 6.0));
        const int lo = (int) std::ceil (fLo / binHz);
        const int hi = juce::jmin (numBins - 1, (int) std::ceil (fHi / binHz) - 1);

        if (hi >= lo)
            referenceBands.push_back ({ lo, hi });
    }

    rebuildGainTables();
    buildSmoothingRanges (binHz, numBins, s.smoothingOctaves, smoothLo, smoothHi);
    reset();
}

void SpectrumAnalyzer::applySettings (const AnalyzerSettings& s)
{
    const auto previous = settings;
    settings = s;

    if (numBins == 0)
        return;

    if (s.slopeDbPerOctave != previous.slopeDbPerOctave)
        rebuildGainTables();

    if (s.smoothingOctaves != previous.smoothingOctaves)
        buildSmoothingRanges (binHz, numBins, s.smoothingOctaves, smoothLo, smoothHi);

    // 設計書7.1：チャンネルは**次のフレームから計算を切り替え、平均はやり直す**
    // （Lの平均にRを混ぜ始めると、数秒のあいだ「どちらでもない」曲線になる）。
    // 積算へ切り替えたときも、そこから数え直す
    const bool becameIntegrate = s.response == AnalyzerSettings::Response::integrate
                                  && previous.response != AnalyzerSettings::Response::integrate;

    if (s.channel != previous.channel || becameIntegrate)
    {
        std::fill (averagedPower.begin(), averagedPower.end(), 0.0);
        std::fill (averagedDb.begin(), averagedDb.end(), floorDb);
        integrateCount = 0;
    }
}

void SpectrumAnalyzer::rebuildGainTables()
{
    // 設計書4.4：g_k = 10^(S·log2(f_k/1000)/10)。**1 kHzで0 dB**
    slopeGain.assign ((size_t) numBins, 1.0);

    const double slope = settings.slopeDbPerOctave;

    for (int k = 1; k < numBins; ++k)
    {
        const double f = k * binHz;
        slopeGain[(size_t) k] = std::pow (10.0, slope * std::log2 (f / 1000.0) / 10.0);
    }

    if (numBins > 1)
        slopeGain[0] = slopeGain[1];
}

void SpectrumAnalyzer::buildSmoothingRanges (double binHzIn, int bins, double octaves,
                                             std::vector<int>& lo, std::vector<int>& hi)
{
    lo.resize ((size_t) bins);
    hi.resize ((size_t) bins);

    for (int k = 0; k < bins; ++k)
    {
        lo[(size_t) k] = k;
        hi[(size_t) k] = k;

        if (octaves <= 0.0 || k == 0)
            continue;

        // ビン k を中心に、f_k·2^(−b/2) 〜 f_k·2^(b/2)（設計書4.4）
        const double f = k * binHzIn;
        const double fLo = f * std::pow (2.0, -octaves / 2.0);
        const double fHi = f * std::pow (2.0, octaves / 2.0);

        const int a = juce::jmax (1, (int) std::ceil (fLo / binHzIn - 1.0e-9));
        const int b = juce::jmin (bins - 1, (int) std::floor (fHi / binHzIn + 1.0e-9));

        // **1ビンに満たないところは、そのままの値**（低域）
        if (b > a)
        {
            lo[(size_t) k] = a;
            hi[(size_t) k] = b;
        }
    }
}

void SpectrumAnalyzer::applySmoothing (const std::vector<double>& in, const std::vector<int>& lo,
                                       const std::vector<int>& hi, std::vector<double>& prefixSum,
                                       std::vector<double>& out)
{
    const size_t n = in.size();

    prefixSum.resize (n + 1);
    out.resize (n);
    prefixSum[0] = 0.0;

    for (size_t i = 0; i < n; ++i)
        prefixSum[i + 1] = prefixSum[i] + in[i];

    for (size_t k = 0; k < n; ++k)
    {
        const int a = lo[k];
        const int b = hi[k];
        out[k] = (prefixSum[(size_t) b + 1] - prefixSum[(size_t) a]) / (double) (b - a + 1);
    }
}

void SpectrumAnalyzer::reset()
{
    std::fill (historyL.begin(), historyL.end(), 0.0f);
    std::fill (historyR.begin(), historyR.end(), 0.0f);
    std::fill (averagedPower.begin(), averagedPower.end(), 0.0);
    std::fill (averagedDb.begin(), averagedDb.end(), floorDb);
    std::fill (frameDb.begin(), frameDb.end(), floorDb);

    historyPos = 0;
    samplesUntilFrame = hop;
    framesSinceReset = 0;
    integrateCount = 0;
}

int SpectrumAnalyzer::process (AnalyzerFifo& fifo)
{
    // **あふれていたら、残りも捨てる**（途中が欠けた列を1本として解析しない。設計書4.1）
    if (fifo.takeOverflow())
        fifo.discardAll();

    int frames = 0;

    for (;;)
    {
        const int n = fifo.pop (popL.data(), popR.data(), popChunk);

        if (n <= 0)
            break;

        frames += pushSamples (popL.data(), popR.data(), n);
    }

    return frames;
}

int SpectrumAnalyzer::pushSamples (const float* left, const float* right, int numSamples)
{
    if (fft == nullptr)
        return 0;

    int frames = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        historyL[(size_t) historyPos] = left[i];
        historyR[(size_t) historyPos] = right != nullptr ? right[i] : left[i];
        historyPos = (historyPos + 1) % fftSize;

        // 設計書4.2：**ホップ長ぶん溜まるごとに1フレーム**。1回に複数できたら全部回す
        // （時間平均を正しく進めるため）
        if (--samplesUntilFrame <= 0)
        {
            analyseFrame();
            samplesUntilFrame = hop;
            ++frames;
        }
    }

    return frames;
}

void SpectrumAnalyzer::transform (const float* history, float* magnitudes)
{
    // 古い順に並べ直しながら窓を掛ける
    for (int n = 0; n < fftSize; ++n)
        scratch[(size_t) n] = history[(size_t) ((historyPos + n) % fftSize)] * window[(size_t) n];

    std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);

    fft->performFrequencyOnlyForwardTransform (scratch.data(), true);

    for (int k = 0; k < numBins; ++k)
        magnitudes[k] = scratch[(size_t) k];
}

void SpectrumAnalyzer::analyseFrame()
{
    using Channel = AnalyzerSettings::Channel;

    //--------------------------------------------------------------------------
    // 1〜2：FFTとパワー。P_k = (2|X_k| / Σw)²（0 dBFSの正弦波が0 dB。設計書4.3）

    const double norm = 2.0 / windowSum;

    auto toPower = [norm] (float magnitude)
    {
        const double a = norm * (double) magnitude;
        return a * a;
    };

    switch (settings.channel)
    {
        case Channel::sum:
        {
            // **LとRを別々にFFTしてパワーを平均する**（波形で足すと、逆相が消える）
            transform (historyL.data(), magnitudesL.data());
            transform (historyR.data(), magnitudesR.data());

            for (int k = 0; k < numBins; ++k)
                power[(size_t) k] = 0.5 * (toPower (magnitudesL[(size_t) k]) + toPower (magnitudesR[(size_t) k]));
            break;
        }

        case Channel::left:
        case Channel::right:
        {
            transform (settings.channel == Channel::left ? historyL.data() : historyR.data(), magnitudesL.data());

            for (int k = 0; k < numBins; ++k)
                power[(size_t) k] = toPower (magnitudesL[(size_t) k]);
            break;
        }

        case Channel::mid:
        case Channel::side:
        {
            const float sign = settings.channel == Channel::mid ? 1.0f : -1.0f;

            for (int n = 0; n < fftSize; ++n)
                mixed[(size_t) n] = 0.5f * (historyL[(size_t) n] + sign * historyR[(size_t) n]);

            transform (mixed.data(), magnitudesL.data());

            for (int k = 0; k < numBins; ++k)
                power[(size_t) k] = toPower (magnitudesL[(size_t) k]);
            break;
        }
    }

    //--------------------------------------------------------------------------
    // 3：スロープ

    for (int k = 0; k < numBins; ++k)
        power[(size_t) k] *= slopeGain[(size_t) k];

    //--------------------------------------------------------------------------
    // 4：周波数平滑化（ここの値がピークとリアルタイム曲線の元）

    applySmoothing (power, smoothLo, smoothHi, prefix, smoothed);

    for (int k = 0; k < numBins; ++k)
        frameDb[(size_t) k] = powerToDb (smoothed[(size_t) k]);

    //--------------------------------------------------------------------------
    // 5：時間平均（パワーで）

    const double dt = getFrameSeconds();

    if (settings.response == AnalyzerSettings::Response::integrate)
    {
        // 積算：Y ← Y + (X − Y)/n（設計書4.5）
        ++integrateCount;
        const double w = 1.0 / (double) integrateCount;

        for (int k = 0; k < numBins; ++k)
            averagedPower[(size_t) k] += (smoothed[(size_t) k] - averagedPower[(size_t) k]) * w;
    }
    else
    {
        const double attack = std::exp (-dt / (double) AnalyzerSettings::attackSeconds);
        const double release = std::exp (-dt / (double) settings.releaseSeconds());

        for (int k = 0; k < numBins; ++k)
            averagedPower[(size_t) k] = averageStep (averagedPower[(size_t) k], smoothed[(size_t) k], attack, release);
    }

    //--------------------------------------------------------------------------
    // 6：dBへ

    for (int k = 0; k < numBins; ++k)
        averagedDb[(size_t) k] = powerToDb (averagedPower[(size_t) k]);

    ++framesSinceReset;
}

float SpectrumAnalyzer::getReferenceBandDb() const
{
    return bandMeanDb (averagedPower);
}

float SpectrumAnalyzer::getLatestReferenceBandDb() const
{
    return bandMeanDb (smoothed);
}

float SpectrumAnalyzer::bandMeanDb (const std::vector<double>& source) const
{
    if (referenceBands.empty() || framesSinceReset == 0)
        return floorDb;

    // 設計書4.5：**1/6 オクターブずつまとめてから平均**。ビンのまま平均すると、
    // ビンの多い高域の重みが大きくなりすぎる
    double sum = 0.0;

    for (const auto& band : referenceBands)
    {
        double bandSum = 0.0;

        for (int k = band.lo; k <= band.hi; ++k)
            bandSum += source[(size_t) k];

        sum += bandSum / (double) (band.hi - band.lo + 1);
    }

    return powerToDb (sum / (double) referenceBands.size());
}

//==============================================================================

void PeakHold::reset (int numBins)
{
    peakDb.assign ((size_t) numBins, SpectrumAnalyzer::floorDb);
    holdLeft.assign ((size_t) numBins, 0.0f);
}

void PeakHold::update (const std::vector<float>& inputDb, double dt, float holdSeconds, bool holdForever)
{
    if (peakDb.size() != inputDb.size())
        reset ((int) inputDb.size());

    const float fall = (float) (fallDbPerSecond * dt);

    for (size_t k = 0; k < inputDb.size(); ++k)
    {
        const float x = inputDb[k];
        float& peak = peakDb[k];
        float& hold = holdLeft[k];

        if (x >= peak)
        {
            peak = x;
            hold = holdSeconds;
        }
        else if (holdForever)
        {
            // 落とさない
        }
        else if (hold > 0.0f)
        {
            hold -= (float) dt;
        }
        else
        {
            peak = juce::jmax (x, peak - fall);
        }
    }
}
