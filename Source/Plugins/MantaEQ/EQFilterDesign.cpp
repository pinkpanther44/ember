#include "EQFilterDesign.h"

#include <cmath>
#include <complex>

namespace EQFilterDesign
{
    using MantaEQParams::Shape;

    namespace
    {
        constexpr double pi = 3.14159265358979323846;

        /** a0で割って正規化する。**RBJの式はどれもa0が1ではない**ので、ここを通す。 */
        Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
        {
            const double inv = (std::abs (a0) > 1.0e-12) ? 1.0 / a0 : 1.0;

            Coeffs c;
            c.b0 = (float) (b0 * inv);
            c.b1 = (float) (b1 * inv);
            c.b2 = (float) (b2 * inv);
            c.a1 = (float) (a1 * inv);
            c.a2 = (float) (a2 * inv);

            return c;
        }

        /** ナイキストに寄りすぎた周波数を戻す。

            **これが無いと、高いほうへ寄せたときに係数が壊れます**
            （`tan(w0/2)`が発散する）。44.1kHzでは約21.4kHzが上限になります。 */
        double limitFrequency (double hz, double sampleRate)
        {
            return juce::jlimit (5.0, sampleRate * 0.4995, hz);
        }

        double limitQ (double q)
        {
            return juce::jlimit ((double) MantaEQParams::minQ, (double) MantaEQParams::maxQ, q);
        }

        Coeffs bell (double hz, double q, double gainDb, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));
            const double A = std::pow (10.0, gainDb / 40.0);

            return normalise (1.0 + alpha * A, -2.0 * cosw, 1.0 - alpha * A,
                               1.0 + alpha / A, -2.0 * cosw, 1.0 - alpha / A);
        }

        Coeffs lowShelf (double hz, double q, double gainDb, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));
            const double A = std::pow (10.0, gainDb / 40.0);
            const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

            return normalise (A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAalpha),
                               2.0 * A * ((A - 1.0) - (A + 1.0) * cosw),
                               A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAalpha),
                               (A + 1.0) + (A - 1.0) * cosw + twoSqrtAalpha,
                               -2.0 * ((A - 1.0) + (A + 1.0) * cosw),
                               (A + 1.0) + (A - 1.0) * cosw - twoSqrtAalpha);
        }

        Coeffs highShelf (double hz, double q, double gainDb, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));
            const double A = std::pow (10.0, gainDb / 40.0);
            const double twoSqrtAalpha = 2.0 * std::sqrt (A) * alpha;

            return normalise (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAalpha),
                               -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw),
                               A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAalpha),
                               (A + 1.0) - (A - 1.0) * cosw + twoSqrtAalpha,
                               2.0 * ((A - 1.0) - (A + 1.0) * cosw),
                               (A + 1.0) - (A - 1.0) * cosw - twoSqrtAalpha);
        }

        Coeffs notch (double hz, double q, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));

            return normalise (1.0, -2.0 * cosw, 1.0,
                               1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
        }

        /** ピークが0dBのバンドパス（RBJの"constant 0 dB peak gain"）。 */
        Coeffs bandPass (double hz, double q, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));

            return normalise (alpha, 0.0, -alpha,
                               1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
        }

        Coeffs allPass (double hz, double q, double sampleRate)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double cosw = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * limitQ (q));

            return normalise (1.0 - alpha, -2.0 * cosw, 1.0 + alpha,
                               1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
        }

        Coeffs lowPass2 (double hz, double q, double sampleRate)
        {
            // 8.170：**式は共有のものを使う**（Phase 208。`../MantaBiquad.h`）
            return MantaBiquad::designLowPass (hz, limitQ (q), sampleRate);
        }

        Coeffs highPass2 (double hz, double q, double sampleRate)
        {
            return MantaBiquad::designHighPass (hz, limitQ (q), sampleRate);
        }

        /** 1次のローパス／ハイパス（6 dB/oct）。双一次変換。 */
        Coeffs onePole (double hz, double sampleRate, bool highPassMode)
        {
            const double w0 = 2.0 * pi * limitFrequency (hz, sampleRate) / sampleRate;
            const double k = std::tan (w0 * 0.5);
            const double norm = 1.0 / (k + 1.0);

            Coeffs c;

            if (highPassMode)
            {
                c.b0 = (float) norm;
                c.b1 = (float) -norm;
            }
            else
            {
                c.b0 = (float) (k * norm);
                c.b1 = (float) (k * norm);
            }

            c.b2 = 0.0f;
            c.a1 = (float) ((k - 1.0) * norm);
            c.a2 = 0.0f;

            return c;
        }

        /** N次Butterworthを段に分けたときの、k番目のBiquadのQ。

            k は 0 から `N/2 - 1`。**最後がいちばん高い**（＝肩の共振を決める段）。 */
        double butterworthQ (int order, int sectionIndex)
        {
            const double theta = pi * (2.0 * sectionIndex + 1.0) / (2.0 * order);
            return 1.0 / (2.0 * std::cos (theta));
        }

        /** カット系を組む。`highPassMode`がtrueならLow Cut（＝ハイパス）。 */
        void buildCascade (SectionList& list, double hz, double q, int slopeDbPerOctave,
                            double sampleRate, bool highPassMode)
        {
            const int order = juce::jlimit (1, 16, slopeDbPerOctave / 6);
            const int numBiquads = order / 2;
            const bool hasFirstOrder = (order % 2) != 0;

            // つまみのQは**いちばん最後の段（肩）**へ掛ける。
            // 12 dB/octのときButterworthのQは0.7071なので、つまみの値がそのままQになる
            const double resonance = limitQ (q) / 0.70710678118654752;

            for (int i = 0; i < numBiquads; ++i)
            {
                double sectionQ = butterworthQ (order, i);

                if (i == numBiquads - 1)
                    sectionQ = limitQ (sectionQ * resonance);

                list.add (highPassMode ? highPass2 (hz, sectionQ, sampleRate)
                                        : lowPass2 (hz, sectionQ, sampleRate));
            }

            if (hasFirstOrder)
                list.add (onePole (hz, sampleRate, highPassMode));
        }
    }

    //==========================================================================

    SectionList designBand (const MantaEQParams::BandSettings& settings, double sampleRate)
    {
        SectionList list;

        if (! settings.enabled || sampleRate <= 0.0)
            return list;

        const double hz = settings.frequency;
        const double q = settings.q;
        const double gainDb = settings.effectiveGainDb();

        switch (settings.shape)
        {
            case Shape::bell:
                list.add (bell (hz, q, gainDb, sampleRate));
                break;

            case Shape::lowShelf:
                list.add (lowShelf (hz, q, gainDb, sampleRate));
                break;

            case Shape::highShelf:
                list.add (highShelf (hz, q, gainDb, sampleRate));
                break;

            case Shape::lowCut:
                buildCascade (list, hz, q, settings.slopeDbPerOctave, sampleRate, true);
                break;

            case Shape::highCut:
                buildCascade (list, hz, q, settings.slopeDbPerOctave, sampleRate, false);
                break;

            case Shape::notch:
                list.add (notch (hz, q, sampleRate));
                break;

            case Shape::bandPass:
                list.add (bandPass (hz, q, sampleRate));
                break;

            case Shape::tiltShelf:
                // **1本の傾き**を、低域を下げるシェルフと高域を上げるシェルフの
                // 組で作る。それぞれ半分ずつなので、合計の傾きはGainのぶんになる
                list.add (lowShelf (hz, q, -gainDb * 0.5, sampleRate));
                list.add (highShelf (hz, q, gainDb * 0.5, sampleRate));
                break;

            case Shape::allPass:
                list.add (allPass (hz, q, sampleRate));
                break;

            case Shape::numShapes:
            default:
                break;
        }

        return list;
    }

    double magnitudeAt (const SectionList& sections, double frequencyHz, double sampleRate)
    {
        if (sections.numSections <= 0 || sampleRate <= 0.0)
            return 1.0;

        const double w = 2.0 * pi * frequencyHz / sampleRate;
        const std::complex<double> z { std::cos (-w), std::sin (-w) };   // e^{-jw}
        const std::complex<double> z2 = z * z;

        double magnitude = 1.0;

        for (int i = 0; i < sections.numSections; ++i)
        {
            const auto& c = sections.sections[i];

            const std::complex<double> numerator   = (double) c.b0 + (double) c.b1 * z + (double) c.b2 * z2;
            const std::complex<double> denominator = 1.0            + (double) c.a1 * z + (double) c.a2 * z2;

            const double denominatorMagnitude = std::abs (denominator);

            if (denominatorMagnitude < 1.0e-12)
                continue;

            magnitude *= std::abs (numerator) / denominatorMagnitude;
        }

        return magnitude;
    }

    Coeffs designDetector (float frequencyHz, float q, double sampleRate)
    {
        // 検出はバンドの中心のまわりだけを見る。**つまみのQが極端でも、
        // 検出の幅は落ち着かせておく**（Qを40にした瞬間に反応しなくなるのを防ぐ）
        const double detectorQ = juce::jlimit (0.3, 4.0, (double) q);

        return bandPass (frequencyHz, detectorQ, sampleRate);
    }
}
