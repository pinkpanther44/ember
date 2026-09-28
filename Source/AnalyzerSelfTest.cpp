#include "AnalyzerSelfTest.h"

#include "Branding.h"
#include "AppColours.h"
#include "Plugins/MantaPluginFormat.h"
#include "Plugins/MantaAnalyzer/MantaAnalyzerProcessor.h"
#include "Plugins/MantaAnalyzer/MantaAnalyzerEditor.h"
#include "Plugins/MantaAnalyzer/SpectrumAnalyzer.h"
#include "Plugins/MantaAnalyzer/TargetRange.h"
#include "Plugins/MantaAnalyzer/CurveBuilder.h"
#include "Plugins/MantaAnalyzer/AnalyzerThemeColors.h"
#include "Plugins/MantaAnalyzer/MonotoneCubic.h"
#include "Plugins/MantaTheme.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>
#include <random>
#include <thread>

namespace AnalyzerSelfTest
{
    namespace
    {
        int problems = 0;

        void say (const juce::String& line) { std::cout << line << std::endl; }

        void check (bool condition, const juce::String& what)
        {
            if (condition)
            {
                say ("  ok    " + what);
            }
            else
            {
                ++problems;
                say ("  FAIL  " + what);
            }
        }

        void info (const juce::String& what) { say ("  info  " + what); }

        juce::String db (double value) { return juce::String (value, 2) + " dB"; }

        /** 画面の列 x の周波数（`CurveBuilder::frequencyAtX()`と同じ式）。 */
        double settingsFrequencyAt (int x, int width, const AnalyzerFrame& frame)
        {
            return frame.frequencyMin * std::pow (frame.frequencyMax / frame.frequencyMin, (double) x / (double) (width - 1));
        }

        //======================================================================
        // 試験信号

        /** 周波数の決まった信号を、サンプルレートに合わせて作る。 */
        using Generator = std::function<float (double t)>;

        /** アナライザーへ`seconds`秒ぶん流す（L=R）。 */
        void feed (SpectrumAnalyzer& analyzer, double sampleRate, double seconds, const Generator& generator,
                   double& clock)
        {
            const int total = (int) std::lround (seconds * sampleRate);
            std::vector<float> block (4096);

            for (int done = 0; done < total;)
            {
                const int n = juce::jmin ((int) block.size(), total - done);

                for (int i = 0; i < n; ++i)
                {
                    block[(size_t) i] = generator (clock);
                    clock += 1.0 / sampleRate;
                }

                analyzer.pushSamples (block.data(), block.data(), n);
                done += n;
            }
        }

        /** Paul Kellet の「精度の高い」ピンクノイズ（9.2 Hz以上で ±0.05 dB。44.1 kHzで設計されたもの）。 */
        struct PinkNoise
        {
            std::mt19937 random { 12345 };
            std::normal_distribution<float> white { 0.0f, 0.1f };
            float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

            float next()
            {
                const float w = white (random);
                b0 = 0.99886f * b0 + w * 0.0555179f;
                b1 = 0.99332f * b1 + w * 0.0750759f;
                b2 = 0.96900f * b2 + w * 0.1538520f;
                b3 = 0.86650f * b3 + w * 0.3104856f;
                b4 = 0.55000f * b4 + w * 0.5329522f;
                b5 = -0.7616f * b5 - w * 0.0168980f;
                const float pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
                b6 = w * 0.115926f;
                return pink * 0.25f;
            }
        };

        /** そのビンの中から、[fLo, fHi] の範囲の最大と最小。 */
        std::pair<float, float> rangeOf (const SpectrumAnalyzer& analyzer, double fLo, double fHi)
        {
            float lo = 1000.0f, hi = -1000.0f;
            const auto& values = analyzer.getAveragedDb();

            for (int k = 1; k < analyzer.getNumBins(); ++k)
            {
                const double f = k * analyzer.getBinHz();

                if (f < fLo || f > fHi)
                    continue;

                lo = juce::jmin (lo, values[(size_t) k]);
                hi = juce::jmax (hi, values[(size_t) k]);
            }

            return { lo, hi };
        }

        AnalyzerSettings integrateSettings (float slope, float smoothing)
        {
            AnalyzerSettings s;
            s.response = AnalyzerSettings::Response::integrate;
            s.slopeDbPerOctave = slope;
            s.smoothingOctaves = smoothing;
            return s;
        }

        //======================================================================

        void testPlumbing()
        {
            say ("--- plumbing (9.5 step 3)");

            const MantaPlugins::Entry* entry = nullptr;

            for (const auto& e : MantaPlugins::getEntries())
                if (juce::String (e.identifier) == "manta:analyzer")
                    entry = &e;

            check (entry != nullptr, "the analyzer is in the built-in table as manta:analyzer");

            if (entry == nullptr)
                return;

            juce::PluginDescription description;
            const bool found = MantaPlugins::findDescription ("manta:analyzer", description);

            check (found
                     && description.name == Branding::analyzerPluginName
                     && description.category == "Fx|Analyzer"
                     && ! description.isInstrument
                     && description.numInputChannels == 2 && description.numOutputChannels == 2,
                    "its description: " + description.name + ", " + description.category + ", 2 in / 2 out");

            auto instance = entry->create();
            auto* processor = dynamic_cast<MantaAnalyzerProcessor*> (instance.get());

            check (processor != nullptr, "the table creates a MantaAnalyzerProcessor");

            if (processor == nullptr)
                return;

            processor->prepareToPlay (48000.0, 512);
            check (processor->getLatencySamples() == 0, "latency is 0 samples");

            // **音を1ビットも変えない**（仕様書1.2）——窓が閉じていても開いていても
            for (const bool open : { false, true })
            {
                processor->setEditorOpen (open);

                juce::AudioBuffer<float> buffer (2, 512);
                juce::Random random (7);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

                juce::AudioBuffer<float> original (buffer);
                juce::MidiBuffer midi;
                processor->processBlock (buffer, midi);

                bool identical = true;

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        identical = identical && buffer.getSample (ch, i) == original.getSample (ch, i);

                check (identical, juce::String ("the output is bit-identical to the input (window ")
                                    + (open ? "open)" : "closed)"));
            }

            // 仕様書4.4：**閉じているあいだは書き写さない**
            processor->setEditorOpen (false);
            processor->getFifo().discardAll();
            {
                juce::AudioBuffer<float> buffer (2, 256);
                buffer.clear();
                juce::MidiBuffer midi;
                processor->processBlock (buffer, midi);
            }
            check (processor->getFifo().getNumReady() == 0, "nothing is copied while the window is closed");

            processor->setEditorOpen (true);
            {
                juce::AudioBuffer<float> buffer (2, 256);
                buffer.clear();
                juce::MidiBuffer midi;
                processor->processBlock (buffer, midi);
            }
            check (processor->getFifo().getNumReady() == 256, "256 samples are copied while it is open");
            processor->setEditorOpen (false);

            //------------------------------------------------------------------
            // 保存して読み直す

            AnalyzerSettings changed;
            changed.lowPhon = 60.0f;
            changed.highPhon = 75.0f;
            changed.align = AnalyzerSettings::Align::calibrated;
            changed.calibrationSpl = 85.0f;
            changed.response = AnalyzerSettings::Response::slow;
            changed.smoothingOctaves = 1.0f / 6.0f;
            changed.slopeDbPerOctave = 3.0f;
            changed.fftSize = 16384;
            changed.window = AnalyzerSettings::Window::blackmanHarris;
            changed.channel = AnalyzerSettings::Channel::side;
            changed.levelRangeDb = 60.0f;
            changed.peakHoldInfinite = true;
            processor->setSettings (changed);

            juce::MemoryBlock state;
            processor->getStateInformation (state);

            auto reopened = entry->create();
            reopened->setStateInformation (state.getData(), (int) state.getSize());

            check (dynamic_cast<MantaAnalyzerProcessor&> (*reopened).getSettings() == processor->getSettings(),
                    "every setting comes back after saving and loading");

            // 設計書7.2：**知らないキーは無視、欠けたキーは初期値、範囲の外は寄せる**
            juce::ValueTree tree (AnalyzerSettings::treeType);
            tree.setProperty ("schema", 1, nullptr);
            tree.setProperty ("lowPhon", 65.0, nullptr);
            tree.setProperty ("somethingFromTheFuture", "hello", nullptr);
            tree.setProperty ("fftSize", 12345, nullptr);
            tree.setProperty ("channel", 99, nullptr);

            const auto read = AnalyzerSettings::fromTree (tree);
            const AnalyzerSettings defaults;

            check (read.lowPhon == 65.0f, "a key that is there is read (lowPhon 65)");
            check (read.highPhon == defaults.highPhon && read.slopeDbPerOctave == defaults.slopeDbPerOctave,
                    "keys that are missing keep their initial values");
            check (read.fftSize == 8192 || read.fftSize == 16384, "an FFT size that is not a choice goes to the nearest one ("
                                                                    + juce::String (read.fftSize) + ")");
            check (read.channel == defaults.channel, "a channel number it does not know stays at the initial value");
        }

        //======================================================================

        void testFifo()
        {
            say ("--- sample FIFO (design 4.1)");

            AnalyzerFifo fifo (1024);
            std::vector<float> l (700), r (700), outL (700), outR (700);

            bool ordered = true;
            float next = 0.0f, expected = 0.0f;

            // 3回に分けて書き、読み、**折り返しをまたいでも順番が保たれる**こと
            for (int round = 0; round < 5; ++round)
            {
                for (int i = 0; i < 700; ++i) { l[(size_t) i] = next; r[(size_t) i] = -next; next += 1.0f; }
                fifo.push (l.data(), r.data(), 700);

                const int got = fifo.pop (outL.data(), outR.data(), 700);

                for (int i = 0; i < got; ++i)
                {
                    ordered = ordered && outL[(size_t) i] == expected && outR[(size_t) i] == -expected;
                    expected += 1.0f;
                }
            }

            check (ordered && expected == next, "samples come out in order across the wrap-around");

            // あふれ
            fifo.push (l.data(), r.data(), 700);
            fifo.push (l.data(), r.data(), 700);   // 1024 に 1400 は入らない
            check (fifo.takeOverflow(), "writing more than it holds raises the overflow flag");
            check (! fifo.takeOverflow(), "...and taking it lowers it again");
            check (fifo.getNumReady() == 700, "the block that did not fit was dropped whole");

            fifo.discardAll();
            check (fifo.getNumReady() == 0, "discarding leaves it empty");

            // **2つのスレッドで**：書き手は連番を書き、読み手は欠けも入れ替わりも無いこと
            AnalyzerFifo shared (8192);
            std::atomic<bool> done { false };
            constexpr int total = 2000000;

            std::thread writer ([&]
            {
                std::vector<float> a (256), b (256);
                int value = 0;

                while (value < total)
                {
                    const int n = juce::jmin (256, total - value);

                    for (int i = 0; i < n; ++i) { a[(size_t) i] = (float) ((value + i) % 1000000); b[(size_t) i] = a[(size_t) i]; }

                    // 読み手が追いつくまで待つ（**本物の音のスレッドは待ちません**。ここは試しの都合）
                    while (shared.getCapacity() - shared.getNumReady() < n)
                        std::this_thread::yield();

                    shared.push (a.data(), b.data(), n);
                    value += n;
                }

                done = true;
            });

            std::vector<float> a (1000), b (1000);
            int received = 0;
            bool continuous = true;

            while (! done || shared.getNumReady() > 0)
            {
                const int n = shared.pop (a.data(), b.data(), 1000);

                for (int i = 0; i < n; ++i)
                {
                    continuous = continuous && a[(size_t) i] == (float) (received % 1000000) && b[(size_t) i] == a[(size_t) i];
                    ++received;
                }

                if (n == 0)
                    std::this_thread::yield();
            }

            writer.join();

            check (continuous && received == total && ! shared.takeOverflow(),
                    "2,000,000 samples across two threads arrive complete and in order");
        }

        //======================================================================

        void testMonotoneCubic()
        {
            say ("--- monotone cubic interpolation (design 6.3)");

            std::mt19937 random (99);
            std::uniform_real_distribution<double> step (0.1, 2.0), value (-40.0, 0.0);

            bool inside = true;
            double worst = 0.0;

            for (int trial = 0; trial < 200; ++trial)
            {
                std::vector<double> xs (12), ys (12);
                double x = 0.0;

                for (int i = 0; i < 12; ++i)
                {
                    x += step (random);
                    xs[(size_t) i] = x;
                    ys[(size_t) i] = value (random);
                }

                MonotoneCubic spline (xs.data(), ys.data(), 12);

                for (int k = 0; k < 11; ++k)
                    for (int j = 1; j < 20; ++j)
                    {
                        const double at = xs[(size_t) k] + (xs[(size_t) k + 1] - xs[(size_t) k]) * j / 20.0;
                        const double v = spline (at);
                        const double lo = juce::jmin (ys[(size_t) k], ys[(size_t) k + 1]);
                        const double hi = juce::jmax (ys[(size_t) k], ys[(size_t) k + 1]);
                        const double over = juce::jmax (lo - v, v - hi);

                        worst = juce::jmax (worst, over);
                        inside = inside && over <= 1.0e-9;
                    }
            }

            check (inside, "on 200 random point sets, it never leaves the range of the two neighbours (worst "
                             + juce::String (worst, 12) + ")");
        }

        //======================================================================

        void testSmoothingAndAveraging()
        {
            say ("--- smoothing, time averaging, peak hold (design 4.4-4.6)");

            // 累積和で求めたものが、**周波数から数え直した素朴な平均**と一致すること
            const double binHz = 48000.0 / 16384.0;
            const int bins = 8193;
            std::vector<int> lo, hi;
            SpectrumAnalyzer::buildSmoothingRanges (binHz, bins, 1.0 / 3.0, lo, hi);

            std::mt19937 random (5);
            std::uniform_real_distribution<double> value (0.0, 1.0);
            std::vector<double> power ((size_t) bins), prefix, fast;

            for (auto& p : power)
                p = value (random);

            SpectrumAnalyzer::applySmoothing (power, lo, hi, prefix, fast);

            double worst = 0.0;

            for (int k = 1; k < bins; ++k)
            {
                const double f = k * binHz;
                const double fLo = f * std::pow (2.0, -1.0 / 6.0);
                const double fHi = f * std::pow (2.0, 1.0 / 6.0);

                double sum = 0.0;
                int count = 0;

                for (int j = 1; j < bins; ++j)
                    if (j * binHz >= fLo - 1.0e-9 && j * binHz <= fHi + 1.0e-9) { sum += power[(size_t) j]; ++count; }

                const double naive = count >= 2 ? sum / count : power[(size_t) k];
                worst = juce::jmax (worst, std::abs (naive - fast[(size_t) k]));
            }

            check (worst < 1.0e-9, "1/3-oct smoothing by prefix sums equals the naive mean ("
                                     + juce::String (worst, 12) + ")");

            // 時定数：τ たったら、目標との差が約37%（設計書8.1）
            auto remainingAfter = [] (double tau, bool rising)
            {
                const double dt = 2048.0 / 48000.0;
                const double alpha = std::exp (-dt / tau);
                double y = rising ? 0.0 : 1.0;
                const double target = rising ? 1.0 : 0.0;
                const int steps = (int) std::lround (tau / dt);

                for (int i = 0; i < steps; ++i)
                    y = SpectrumAnalyzer::averageStep (y, target, alpha, alpha);

                return std::abs (target - y) * std::exp (-(tau - steps * dt) / tau);
            };

            const double up = remainingAfter (0.05, true);
            const double down = remainingAfter (1.0, false);

            check (std::abs (up - std::exp (-1.0)) < 0.02 && std::abs (down - std::exp (-1.0)) < 0.02,
                    "after one time constant ~37% is left (attack " + juce::String (up * 100.0, 1)
                      + "%, release " + juce::String (down * 100.0, 1) + "%)");

            // ピーク：2秒保持、そのあと 12 dB/s
            PeakHold peaks;
            peaks.reset (1);
            const double dt = 0.05;
            std::vector<float> loud { -10.0f }, quiet { -120.0f };

            peaks.update (loud, dt, 2.0f, false);
            float atHold = 0.0f, afterFall = 0.0f;

            for (int i = 1; i <= 60; ++i)
            {
                peaks.update (quiet, dt, 2.0f, false);

                if (i == 38) atHold = peaks.getPeakDb()[0];      // 1.9 s
                if (i == 60) afterFall = peaks.getPeakDb()[0];   // 3.0 s
            }

            check (atHold == -10.0f, "the peak holds for 2 s (" + db (atHold) + " at 1.9 s)");
            check (std::abs (afterFall - (-10.0f - 12.0f * 1.0f)) < 0.7f,
                    "then falls at 12 dB/s (" + db (afterFall) + " at 3.0 s, expected about -22)");

            PeakHold forever;
            forever.reset (1);
            forever.update (loud, dt, 2.0f, true);
            for (int i = 0; i < 200; ++i)
                forever.update (quiet, dt, 2.0f, true);

            check (forever.getPeakDb()[0] == -10.0f, "held forever, it does not fall");
        }

        //======================================================================

        void testIsoAndRange()
        {
            say (juce::String ("--- equal-loudness contours and the target range (") + Iso226::edition + ")");

            double worst = 0.0;
            for (int phon = 20; phon <= 90; phon += 10)
                worst = juce::jmax (worst, std::abs (Iso226::splAt (Iso226::index1k, phon) - phon));

            check (worst < 0.1, "at 1 kHz the contour equals its own loudness level (worst " + db (worst) + ")");

            const double low40 = Iso226::splAt (0, 40.0);
            const double dip40 = Iso226::splAt (22, 40.0);   // 3.15 kHz
            check (low40 > 90.0 && dip40 < 40.0,
                    "40 phon: 20 Hz needs far more level (" + db (low40) + "), 3.15 kHz a little less (" + db (dip40) + ")");

            check (Iso226::maxPhonAt (4000.0) == 90.0 && Iso226::maxPhonAt (5000.0) == 80.0,
                    "the valid range is 90 phon up to 4 kHz and 80 phon above");

            // 自動追従：最初はすぐ、あとは 3 s でゆっくり、無音では止まる
            AnalyzerSettings s;
            TargetRange range;
            range.configure (s);
            range.snapOnNextInput();

            range.update (-20.0f, -20.0f, 1.0 / 60.0);
            const float first = range.getOffsetDb();
            const float expectedFirst = (float) (-20.0 - range.getMidpointAt1k());
            check (std::abs (first - expectedFirst) < 0.01f, "the first real input places the range at once");

            for (int i = 0; i < 9 * 60; ++i)
                range.update (-30.0f, -30.0f, 1.0 / 60.0);

            const float moved = range.getOffsetDb() - first;
            check (std::abs (moved - (-10.0f)) < 0.5f, "after 9 s (3 time constants) it has followed to within 5% ("
                                                        + juce::String (moved, 2) + " of -10 dB)");

            const float before = range.getOffsetDb();
            for (int i = 0; i < 600; ++i)
                range.update (-100.0f, -100.0f, 1.0 / 60.0);

            check (range.getOffsetDb() == before, "during silence the range does not move");

            // 校正：−20 dBFS = 83 dB SPL → 80 phon の 1 kHz は −23 dB（設計書5.3の例）
            s.align = AnalyzerSettings::Align::calibrated;
            s.slopeDbPerOctave = 4.5f;
            range.configure (s);
            range.update (-100.0f, -100.0f, 1.0 / 60.0);

            check (std::abs (range.upperDb (1000.0) - (-23.0f)) < 0.1f,
                    "calibrated: 80 phon at 1 kHz is drawn at -23 dB (" + db (range.upperDb (1000.0)) + ")");

            check (range.outsideStandard (15000.0) && range.outsideStandard (15.0) && ! range.outsideStandard (1000.0),
                    "below 20 Hz and above 12.5 kHz is outside the standard");

            s.highPhon = 90.0f;
            s.lowPhon = 80.0f;
            range.configure (s);
            check (range.outsideStandard (8000.0) && ! range.outsideStandard (2000.0),
                    "90 phon is outside the standard at 8 kHz but not at 2 kHz");
        }

        //======================================================================

        void testCurveBuilder()
        {
            say ("--- one point per pixel (design 6.2)");

            const double binHz = 48000.0 / 16384.0;
            const int bins = 8193;
            std::vector<float> binDb ((size_t) bins);

            // なめらかな形（−6 dB/oct）
            for (int k = 0; k < bins; ++k)
                binDb[(size_t) k] = (float) (-30.0 - 6.0 * std::log2 (juce::jmax (1.0, k * binHz) / 1000.0));

            CurveBuilder builder;
            builder.setAxis (20.0, 20000.0, 896, binHz, bins);

            std::vector<float> out;
            builder.build (binDb, out);

            int switchAt = -1;
            for (int x = 1; x < 896; ++x)
                if (builder.isAveragedColumn (x) && ! builder.isAveragedColumn (x - 1))
                    switchAt = x;

            double worstJump = 0.0, worstError = 0.0;

            for (int x = 1; x < 896; ++x)
            {
                const double expectedStep = std::abs (-6.0 * std::log2 (builder.frequencyAtX (x) / builder.frequencyAtX (x - 1)));
                worstJump = juce::jmax (worstJump, std::abs (out[(size_t) x] - out[(size_t) x - 1]) - expectedStep);
            }

            for (int x = 0; x < 896; ++x)
            {
                const double truth = -30.0 - 6.0 * std::log2 (builder.frequencyAtX (x) / 1000.0);
                worstError = juce::jmax (worstError, std::abs (out[(size_t) x] - truth));
            }

            check (switchAt > 0, "there is a column where interpolation hands over to averaging (x = "
                                   + juce::String (switchAt) + ", " + juce::String (builder.frequencyAtX (switchAt), 0) + " Hz)");
            check (worstJump < 0.05, "no column jumps more than the slope itself (worst extra " + db (worstJump) + ")");
            check (worstError < 0.3, "every column is within 0.3 dB of the true shape (worst " + db (worstError) + ")");
        }

        //======================================================================

        void testColours()
        {
            say ("--- colours from the accent (design 6.5)");

            const juce::Colour accents[]
            {
                juce::Colour (0xff05040a), juce::Colour (0xfffafaff), juce::Colour (0xff7c5cff),
                juce::Colour (0xffff8a00), juce::Colour (0xff00c0ff), juce::Colour (0xffffff40)
            };

            double worstGap = 1.0;

            {
                const auto p = AnalyzerThemeColors::fromAccent (AppColours::purple);
                info ("accent " + AppColours::purple.toDisplayString (false) + " L=" + juce::String (AnalyzerThemeColors::toOklch (AppColours::purple).l, 3)
                        + " -> below " + p.belowLower.toDisplayString (false) + ", range " + p.rangeBottom.toDisplayString (false)
                        + ".." + p.rangeTop.toDisplayString (false) + ", excess " + p.excessFar.toDisplayString (false)
                        + ", curve " + p.curve.toDisplayString (false));
            }

            for (const auto accent : accents)
            {
                const auto c = AnalyzerThemeColors::fromAccent (accent);
                const float steps[]
                {
                    AnalyzerThemeColors::toOklch (c.belowLower).l, AnalyzerThemeColors::toOklch (c.rangeBottom).l,
                    AnalyzerThemeColors::toOklch (c.rangeTop).l, AnalyzerThemeColors::toOklch (c.excessFar).l
                };

                for (int i = 1; i < 4; ++i)
                    worstGap = juce::jmin (worstGap, (double) (steps[i] - steps[i - 1]));
            }

            check (worstGap >= AnalyzerThemeColors::minimumStep - 0.005,
                    "even for near-black and near-white accents, each fill step is at least 0.08 lighter (worst "
                      + juce::String (worstGap, 3) + ")");

            // 8.330：**どちらのテーマでも、曲線が地から浮いて見える**こと。
            // 白い地に明るい線を引くと沈むので、ライトでは線を暗くしてあります（`fromAccent()`）
            for (const bool dark : { true, false })
            {
                double worstCurve = 1.0, worstFill = 1.0;

                for (const auto accent : accents)
                {
                    const auto c = AnalyzerThemeColors::fromAccent (accent, dark);
                    const float background = AnalyzerThemeColors::toOklch (c.background).l;

                    worstCurve = juce::jmin (worstCurve, (double) std::abs (AnalyzerThemeColors::toOklch (c.curve).l - background));
                    worstFill = juce::jmin (worstFill, (double) std::abs (AnalyzerThemeColors::toOklch (c.fillBottom).l - background));
                }

                const juce::String theme = dark ? "dark" : "light";

                check (worstCurve >= 0.35, theme + ": the curve stands out from the background (lightness gap at least "
                                             + juce::String (worstCurve, 3) + ")");
                check (worstFill >= 0.03, theme + ": the bottom of the fill is not the background colour (gap at least "
                                            + juce::String (worstFill, 3) + ")");
            }
        }

        //======================================================================

        void testAccuracy()
        {
            say ("--- accuracy (spec 7.3)");

            // 1. 0 dBFS の正弦波 → 0 dB ±0.5（スロープ0・平滑化オフ）。
            //    **1 kHz だけでなく、ビンとビンのあいだに落ちる周波数も**（最悪のずれを見る）
            for (const double rate : { 44100.0, 48000.0 })
            {
                float worst = 0.0f, atOneK = 0.0f;

                for (int i = 0; i <= 20; ++i)
                {
                    const double f = 990.0 + i;   // 990〜1010 Hz（1 Hz 刻みでビンのあいだを総なめ）

                    // **積算ではなく「速」で、落ち着いたところを読む**。積算は最初の数フレーム
                    // （窓がまだ0で埋まっている）まで平均に入れるので、0.2 dB ほど低く出ます
                    auto s = integrateSettings (0.0f, 0.0f);
                    s.response = AnalyzerSettings::Response::fast;

                    SpectrumAnalyzer analyzer;
                    analyzer.prepare (rate, s);

                    double clock = 0.0;
                    feed (analyzer, rate, 2.0, [f] (double t) { return (float) std::sin (juce::MathConstants<double>::twoPi * f * t); }, clock);

                    const float peak = rangeOf (analyzer, f - 20.0, f + 20.0).second;
                    worst = juce::jmax (worst, std::abs (peak));

                    if (f == 1000.0)
                        atOneK = peak;
                }

                check (worst <= 0.5f, "a 0 dBFS sine reads 0 dB +-0.5 at " + juce::String (rate / 1000.0, 1)
                                        + " kHz (1 kHz: " + db (atOneK) + ", worst of 990-1010 Hz: " + db (worst) + ")");
            }

            // 2. ピンクノイズ、スロープ 3 dB/oct、積算 → 50 Hz〜15 kHz で平ら ±1 dB
            {
                SpectrumAnalyzer analyzer;
                analyzer.prepare (44100.0, integrateSettings (3.0f, 1.0f / 3.0f));

                PinkNoise pink;
                double clock = 0.0;
                feed (analyzer, 44100.0, 40.0, [&pink] (double) { return pink.next(); }, clock);

                const auto [lo, hi] = rangeOf (analyzer, 50.0, 15000.0);
                check (hi - lo <= 2.0f, "pink noise with a 3 dB/oct slope is flat within +-1 dB from 50 Hz to 15 kHz (spread "
                                         + db (hi - lo) + ")");
            }

            // 3. ホワイトノイズ、スロープ 0 → 平ら ±1 dB
            {
                SpectrumAnalyzer analyzer;
                analyzer.prepare (48000.0, integrateSettings (0.0f, 1.0f / 3.0f));

                std::mt19937 random (3);
                std::normal_distribution<float> white (0.0f, 0.1f);
                double clock = 0.0;
                feed (analyzer, 48000.0, 20.0, [&] (double) { return white (random); }, clock);

                const auto [lo, hi] = rangeOf (analyzer, 50.0, 15000.0);
                check (hi - lo <= 2.0f, "white noise with no slope is flat within +-1 dB (spread " + db (hi - lo) + ")");
            }

            // 4. スイープ（初期設定）→ 曲線が値の範囲をはみ出さない・NaNが出ない
            {
                const double rate = 48000.0;
                AnalyzerSettings s;

                SpectrumAnalyzer analyzer;
                analyzer.prepare (rate, s);

                const double seconds = 10.0;
                double clock = 0.0;
                feed (analyzer, rate, seconds, [seconds] (double t)
                {
                    // 対数スイープ 20 Hz → 20 kHz の位相
                    const double k = std::log (1000.0);
                    const double phase = juce::MathConstants<double>::twoPi * 20.0 * seconds / k * (std::exp (k * t / seconds) - 1.0);
                    return (float) (0.5 * std::sin (phase));
                }, clock);

                CurveBuilder builder;
                builder.setAxis (20.0, 20000.0, 896, analyzer.getBinHz(), analyzer.getNumBins());
                std::vector<float> out;
                builder.build (analyzer.getAveragedDb(), out);

                float binMax = -1000.0f;
                for (int k = 1; k < analyzer.getNumBins(); ++k)
                    binMax = juce::jmax (binMax, analyzer.getAveragedDb()[(size_t) k]);

                bool finite = true;
                float curveMax = -1000.0f;
                for (float v : out) { finite = finite && std::isfinite (v); curveMax = juce::jmax (curveMax, v); }

                check (finite && curveMax <= binMax + 0.01f,
                        "a 20 Hz-20 kHz sweep draws no overshoot above the analysed values (curve max "
                          + db (curveMax) + ", bins max " + db (binMax) + ")");
            }

            // 5. 無音 → 曲線は下端へ落ち、レンジは直前の位置で止まる
            {
                const double rate = 48000.0;
                AnalyzerSettings s;
                s.response = AnalyzerSettings::Response::fast;

                SpectrumAnalyzer analyzer;
                analyzer.prepare (rate, s);
                TargetRange range;
                range.configure (s);
                range.snapOnNextInput();

                PinkNoise pink;
                double clock = 0.0;
                const double tick = 1.0 / 60.0;

                for (int i = 0; i < 180; ++i)
                {
                    feed (analyzer, rate, tick, [&pink] (double) { return pink.next(); }, clock);
                    range.update (analyzer.getReferenceBandDb(), analyzer.getLatestReferenceBandDb(), tick);
                }

                const float offsetBefore = range.getOffsetDb();

                for (int i = 0; i < 600; ++i)
                {
                    feed (analyzer, rate, tick, [] (double) { return 0.0f; }, clock);
                    range.update (analyzer.getReferenceBandDb(), analyzer.getLatestReferenceBandDb(), tick);
                }

                const auto [lo, hi] = rangeOf (analyzer, 20.0, 20000.0);
                juce::ignoreUnused (lo);

                check (hi < -80.0f, "after 10 s of silence the curve has fallen below the bottom of the scale (max " + db (hi) + ")");
                // 窓の長さぶん（8192点＝0.17 s）は、音の尻尾がまだ入っているので少しだけ追う
                check (std::abs (range.getOffsetDb() - offsetBefore) < 0.5f,
                        "...and the range stays where it was (moved " + db (range.getOffsetDb() - offsetBefore) + ")");
            }

            // 6. 同じ音源を 44.1 / 48 / 96 / 192 kHz で → 曲線の差が 1 dB 以内（初期設定）
            {
                // **周波数の決まった音源**（1/24 オクターブおきの正弦波。位相はばらばら）
                std::vector<double> frequencies, phases;
                std::mt19937 random (11);
                std::uniform_real_distribution<double> phase (0.0, juce::MathConstants<double>::twoPi);

                for (double f = 30.0; f < 18000.0; f *= std::pow (2.0, 1.0 / 24.0))
                {
                    frequencies.push_back (f);
                    phases.push_back (phase (random));
                }

                const double amplitude = 0.02;
                std::vector<std::vector<float>> curves;

                for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
                {
                    AnalyzerSettings s;
                    s.response = AnalyzerSettings::Response::integrate;

                    SpectrumAnalyzer analyzer;
                    analyzer.prepare (rate, s);

                    double clock = 0.0;
                    feed (analyzer, rate, 3.0, [&] (double t)
                    {
                        double sum = 0.0;
                        for (size_t i = 0; i < frequencies.size(); ++i)
                            sum += std::sin (juce::MathConstants<double>::twoPi * frequencies[i] * t + phases[i]);
                        return (float) (amplitude * sum);
                    }, clock);

                    CurveBuilder builder;
                    builder.setAxis (50.0, 15000.0, 896, analyzer.getBinHz(), analyzer.getNumBins());
                    std::vector<float> out;
                    builder.build (analyzer.getAveragedDb(), out);
                    curves.push_back (out);
                }

                double worst = 0.0;
                for (size_t c = 1; c < curves.size(); ++c)
                    for (size_t x = 0; x < curves[0].size(); ++x)
                        worst = juce::jmax (worst, (double) std::abs (curves[c][x] - curves[0][x]));

                check (worst <= 1.0, "the same source at 44.1/48/96/192 kHz draws curves within 1 dB of each other (worst "
                                       + db (worst) + ")");
            }
        }

        //======================================================================

        void testEditor()
        {
            say ("--- the window (drawn off-screen)");

            MantaAnalyzerProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
            auto* editor = dynamic_cast<MantaAnalyzerEditor*> (base.get());

            check (editor != nullptr, "the processor opens a MantaAnalyzerEditor");

            if (editor == nullptr)
                return;

            check (editor->getWidth() == 900 && editor->getHeight() == 500, "the window opens at 900 x 500");
            check (! editor->isResizable(), "...and its size is fixed, like the other built-in plugins (8.330)");
            check (processor.isEditorOpen(), "opening the window starts the copying");

            // ピンクノイズ＋1 kHz を3秒、画面の更新を挟みながら流す（本物と同じく、およそ60回／秒）
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double clock = 0.0;
            PinkNoise pink;

            for (int block = 0; block < 282; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.5f * pink.next()
                                  + (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * clock));
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                    clock += 1.0 / 48000.0;
                }

                processor.processBlock (buffer, midi);

                if (block % 2 == 1)
                    editor->tickForTesting();
            }

            const auto& frame = editor->getFrameForTesting();
            const int width = editor->getViewForTesting().getPlotBounds().getWidth();

            check (frame.hasData && (int) frame.average.size() == width,
                    "one frame gives one point per pixel (" + juce::String ((int) frame.average.size()) + ")");

            int loudest = 0;
            for (int x = 1; x < (int) frame.average.size(); ++x)
                if (frame.average[(size_t) x] > frame.average[(size_t) loudest])
                    loudest = x;

            const auto readout = editor->getViewForTesting().getReadoutText (loudest);
            check (readout.contains ("kHz") || readout.contains ("Hz"), "the loudest point reads: "
                                                                          + readout.replace ("\n", " | "));

            // 自動追従：**1 kHz での下限と上限の中点が、入力の基準帯域平均に来ている**こと
            // （8.330：レンジを出さないあいだは見ない。`MantaAnalyzerEditor::showsTargetRange`）
            if (MantaAnalyzerEditor::showsTargetRange)
            // （仕様書5.3）。窓が埋まる前のフレームへ合わせると、ここが13 dBほど下にずれていました
            {
                int x1k = 0;
                const auto plot = editor->getViewForTesting().getPlotBounds();

                for (int x = 0; x < width; ++x)
                {
                    const double hz = settingsFrequencyAt (x, width, frame);
                    if (std::abs (std::log2 (hz / 1000.0)) < std::abs (std::log2 (settingsFrequencyAt (x1k, width, frame) / 1000.0)))
                        x1k = x;
                }

                juce::ignoreUnused (plot);
                const float midpoint = 0.5f * (frame.lower[(size_t) x1k] + frame.upper[(size_t) x1k]);
                const float reference = editor->getReferenceDbForTesting();

                check (std::abs (midpoint - reference) < 1.5f,
                        "auto follow puts the range's midpoint at 1 kHz on the input level ("
                          + db (midpoint) + " vs " + db (reference) + ")");
            }

            // 描けること（時間も出す）
            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            double total = 0.0;

            for (int i = 0; i < 20; ++i)
            {
                juce::Graphics g (image);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                editor->paintEntireComponent (g, false);
                total += juce::Time::getMillisecondCounterHiRes() - start;
            }

            info ("drawing the whole window takes " + juce::String (total / 20.0, 2) + " ms (software renderer, 900 x 500)");

            // **塗りが本当の濃さで出ていること**。下限線より下（グラフの下端近く）は
            // 「下限より下の塗り」の色そのものになるはず。JUCEは画像を「いまの色の不透明度」で
            // 描くので、直前にレンジ帯（25%）を塗ったまま描くと、ここが4分の1の濃さになっていました
            {
                const auto plot = editor->getViewForTesting().getPlotBounds()
                                    + editor->getViewForTesting().getPosition();
                const auto pixel = image.getPixelAt (plot.getX() + 20, plot.getBottom() - 4);
                const auto theme = AnalyzerThemeColors::fromAccent (MantaTheme::accent(), AppColours::getTheme() == AppColours::Theme::Dark);
                const auto wanted = MantaAnalyzerEditor::showsTargetRange ? theme.belowLower : theme.fillBottom;

                const int difference = std::abs (pixel.getRed() - wanted.getRed())
                                     + std::abs (pixel.getGreen() - wanted.getGreen())
                                     + std::abs (pixel.getBlue() - wanted.getBlue());

                check (difference <= 6, "the fill below the lower line is drawn at full strength ("
                                          + pixel.toDisplayString (false) + ", wanted " + wanted.toDisplayString (false) + ")");
            }

            base.reset();
            check (! processor.isEditorOpen(), "closing the window stops the copying");
        }

        //======================================================================
        /** `--png=<path>`：曲らしい音（ピンクノイズ＋低い音＋3 kHz）を4秒流した画面を絵にする。
            **自分の目で見るため**のもので、合否は付けません。 */
        void writePicture (const juce::File& file)
        {
            MantaAnalyzerProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
            auto* editor = dynamic_cast<MantaAnalyzerEditor*> (base.get());

            if (editor == nullptr)
                return;

            PinkNoise pink;
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double clock = 0.0;

            for (int block = 0; block < 400; ++block)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const double t = clock;
                    const float v = 0.6f * pink.next()
                                  + (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 55.0 * t))
                                  + (float) (0.03 * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * t));
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                    clock += 1.0 / 48000.0;
                }

                processor.processBlock (buffer, midi);

                if (block % 2 == 1)
                    editor->tickForTesting();
            }

            auto& view = editor->getViewForTesting();
            const auto plot = view.getPlotBounds();
            view.setCursorForTesting (plot.getX() + (int) (plot.getWidth() * 0.78));

            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            {
                juce::Graphics g (image);
                editor->paintEntireComponent (g, false);
            }

            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat png;

            if (out.openedOk() && png.writeImageToStream (image, out))
                info ("wrote " + file.getFullPathName());

            // 設定のポップオーバー（並びを見る）
            {
                auto panel = MantaAnalyzerEditor::createSettingsPanel (editor->getSettings(), false, [] (const AnalyzerSettings&) {});
                juce::Image panelImage (juce::Image::ARGB, panel->getWidth(), panel->getHeight(), true);
                {
                    juce::Graphics g (panelImage);
                    panel->paintEntireComponent (g, false);
                }

                const auto panelFile = file.getSiblingFile (file.getFileNameWithoutExtension() + "-settings.png");
                panelFile.deleteFile();
                juce::FileOutputStream panelOut (panelFile);

                if (panelOut.openedOk() && png.writeImageToStream (panelImage, panelOut))
                    info ("wrote " + panelFile.getFullPathName());
            }

            // ライトテーマ（グラフは暗いまま、フッターだけ本体に従う）
            {
                AppColours::setTheme (AppColours::Theme::Light);
                editor->tickForTesting();
                editor->sendLookAndFeelChange();

                juce::Image lightImage (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                {
                    juce::Graphics g (lightImage);
                    editor->paintEntireComponent (g, false);
                }

                const auto lightFile = file.getSiblingFile (file.getFileNameWithoutExtension() + "-light.png");
                lightFile.deleteFile();
                juce::FileOutputStream lightOut (lightFile);

                if (lightOut.openedOk() && png.writeImageToStream (lightImage, lightOut))
                    info ("wrote " + lightFile.getFullPathName());

                AppColours::setTheme (AppColours::Theme::Dark);
            }
        }
    }

    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--analyzer-selftest"))
            return false;

        problems = 0;
        say ("analyzer self-test (8.329)");

        // **本体の色を読んでおく**（アクセントは`AppColours`から引く。読まないと透明な既定値のまま。
        // 設定ファイルには何も書きません）
        AppColours::setTheme (AppColours::Theme::Dark);

        testPlumbing();
        testFifo();
        testMonotoneCubic();
        testSmoothingAndAveraging();
        testIsoAndRange();
        testCurveBuilder();
        testColours();
        testAccuracy();
        testEditor();

        if (commandLine.contains ("--png="))
            writePicture (juce::File (commandLine.fromFirstOccurrenceOf ("--png=", false, false).upToFirstOccurrenceOf (" ", false, false).unquoted()));

        say ("--- " + juce::String (problems) + " problem(s) ---");
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);
        return true;
    }
}
