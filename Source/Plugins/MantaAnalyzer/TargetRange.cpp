#include "TargetRange.h"

#include <cmath>

namespace Iso226
{
    const std::array<double, numBands> frequencies
    {
        20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
        1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500
    };

    namespace
    {
        // ⚠️ **ISO 226:2003 の係数**（ヘッダーの注意書き）。2023年版に替えるときは、ここの4つ
        const std::array<double, numBands> alphaF
        {
            0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330, 0.315,
            0.301, 0.288, 0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244, 0.243, 0.243,
            0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301
        };

        const std::array<double, numBands> lU
        {
            -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1,
            -2.0,  -1.1,  -0.4,   0.0,   0.3,   0.5,   0.0, -2.7, -4.1, -1.0,  1.7,
             2.5,   1.2,  -2.1,  -7.1, -11.2, -10.7,  -3.1
        };

        const std::array<double, numBands> tF
        {
            78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4,
            11.4,  8.6,  6.2,  4.4,  3.0,  2.2,  2.4,  3.5,  1.7, -1.3, -4.2,
            -6.0, -5.4, -1.5,  6.0, 12.6, 13.9, 12.3
        };

        /** 式の定数 c（2003年版は 1.15。2023年版では変わっています）。 */
        constexpr double kConstant = 1.15;
    }

    double splAt (int band, double phon)
    {
        const double af = alphaF[(size_t) band];
        const double lu = lU[(size_t) band];
        const double tf = tF[(size_t) band];

        // 仕様書5.2
        const double af1 = 4.47e-3 * (std::pow (10.0, 0.025 * phon) - kConstant)
                         + std::pow (0.4 * std::pow (10.0, (tf + lu) / 10.0 - 9.0), af);

        return 10.0 / af * std::log10 (af1) - lu + 94.0;
    }

    double maxPhonAt (double hz)
    {
        // 2003年版の有効範囲：20〜4000 Hz は 20〜90 phon、5000〜12500 Hz は 20〜80 phon
        return hz <= 4000.0 ? 90.0 : 80.0;
    }
}

//==============================================================================

TargetRange::TargetRange()
{
    configure (AnalyzerSettings());
}

void TargetRange::configure (const AnalyzerSettings& s)
{
    settings = s;

    if (s.lowPhon == configuredLow && s.highPhon == configuredHigh)
        return;

    configuredLow = s.lowPhon;
    configuredHigh = s.highPhon;

    std::array<double, Iso226::numBands> logF {};

    for (int b = 0; b < Iso226::numBands; ++b)
    {
        logF[(size_t) b] = std::log2 (Iso226::frequencies[(size_t) b]);
        lowSpl[(size_t) b] = Iso226::splAt (b, s.lowPhon);
        highSpl[(size_t) b] = Iso226::splAt (b, s.highPhon);
    }

    lowCurve.set (logF.data(), lowSpl.data(), Iso226::numBands);
    highCurve.set (logF.data(), highSpl.data(), Iso226::numBands);

    midpoint1k = 0.5 * (lowSpl[Iso226::index1k] + highSpl[Iso226::index1k]);
}

float TargetRange::targetOffset (float referenceDb) const
{
    // 設計書5.3
    switch (settings.align)
    {
        case AnalyzerSettings::Align::calibrated:
            return settings.calibrationDbfs - settings.calibrationSpl;

        case AnalyzerSettings::Align::manualOffset:
            return (float) (referenceDb - midpoint1k) + settings.manualOffsetDb;

        case AnalyzerSettings::Align::autoFollow:
        default:
            return (float) (referenceDb - midpoint1k);
    }
}

void TargetRange::update (float referenceDb, float latestReferenceDb, double dt)
{
    // 校正は入力を見ないので、いつでも目標そのもの
    if (settings.align == AnalyzerSettings::Align::calibrated)
    {
        offset = targetOffset (referenceDb);
        return;
    }

    // **無音のあいだは動かさない**（仕様書5.3：直前の位置で帯を保持）。
    //
    // 8.329：**無音かどうかは、いちばん新しいフレームで決める**。時間平均のほうで決めると、
    // 音が止まってから −90 dB を割るまでの数秒（応答「速」でも約5秒）、減っていく平均を
    // レンジが追いかけて**10 dBほど下がっていました**（自己検査で見つけたもの）
    if (latestReferenceDb < silenceDb || referenceDb < silenceDb)
        return;

    const float target = targetOffset (referenceDb);

    // 設計書5.3：**最初の有効な入力ではすぐに合わせる**（0 dB から帯がせり上がる見え方を避ける）
    if (waitingForFirstInput)
    {
        offset = target;
        waitingForFirstInput = false;
        return;
    }

    const double k = 1.0 - std::exp (-dt / followSeconds);
    offset += (float) (k * (target - offset));
}

float TargetRange::shapeDb (const MonotoneCubic& curve, const std::array<double, Iso226::numBands>& spl,
                            double hz) const
{
    const double x = std::log2 (juce::jmax (1.0, hz));
    const double x0 = std::log2 (Iso226::minFrequency);
    const double x1 = std::log2 (Iso226::maxFrequency);

    double value;

    // 仕様書5.2：**20 Hz 未満と 12.5 kHz 超は、端の2点から log 周波数の上で直線外挿**
    if (x < x0)
    {
        const double slope = (spl[1] - spl[0]) / (std::log2 (Iso226::frequencies[1]) - x0);
        value = spl[0] + slope * (x - x0);
    }
    else if (x > x1)
    {
        const int n = Iso226::numBands;
        const double slope = (spl[(size_t) n - 1] - spl[(size_t) n - 2])
                             / (x1 - std::log2 (Iso226::frequencies[(size_t) n - 2]));
        value = spl[(size_t) n - 1] + slope * (x - x1);
    }
    else
    {
        value = curve (x);
    }

    // スペクトラムと同じスロープを掛ける（仕様書5.3）
    return (float) (value + settings.slopeDbPerOctave * std::log2 (hz / 1000.0) + offset);
}

float TargetRange::lowerDb (double hz) const { return shapeDb (lowCurve, lowSpl, hz); }
float TargetRange::upperDb (double hz) const { return shapeDb (highCurve, highSpl, hz); }

bool TargetRange::outsideStandard (double hz) const
{
    if (hz < Iso226::minFrequency || hz > Iso226::maxFrequency)
        return true;

    return settings.highPhon > Iso226::maxPhonAt (hz);
}
