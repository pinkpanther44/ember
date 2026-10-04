#include "LimiterSelfTest.h"

#include "Branding.h"
#include "Plugins/MantaPluginFormat.h"
#include "Plugins/MantaLimiter/MantaLimiterProcessor.h"
#include "Plugins/MantaLimiter/MantaLimiterEditor.h"
#include "AppColours.h"
#include "Plugins/MantaLimiter/LimiterEngine.h"
#include "Plugins/MantaLimiter/LimiterMeters.h"
#include "Plugins/MantaLimiter/LimiterDsp.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <functional>
#include <iostream>
#include <random>

namespace LimiterSelfTest
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

        juce::String db (double value, int places = 2) { return juce::String (value, places) + " dB"; }

        double toDb (double linear) { return linear > 1.0e-12 ? 20.0 * std::log10 (linear) : -240.0; }

        constexpr double twoPi = 6.283185307179586;

        /** 8.335：**どの処理系でも同じ値を出す正規乱数**（Box–Muller）。

            `std::normal_distribution`は**作り方が処理系まかせ**で、同じ`mt19937`の種からでも
            MSVCとGCCで**別の信号**になります（`mt19937`の出す整数そのものは規格で決まっている）。
            1.2.0の準備で、Linuxだけトゥルーピークの検査が落ちて気づきました。
            `std::normal_distribution<float>`と同じ呼び方（`gauss (random)`）にしてあります。 */
        class PortableGauss
        {
        public:
            PortableGauss (float meanToUse, float sigmaToUse) : mean (meanToUse), sigma (sigmaToUse) {}

            float operator() (std::mt19937& engine)
            {
                if (hasSpare)
                {
                    hasSpare = false;
                    return mean + sigma * spare;
                }

                const double u1 = ((double) engine() + 0.5) / 4294967296.0;
                const double u2 = ((double) engine() + 0.5) / 4294967296.0;
                const double radius = std::sqrt (-2.0 * std::log (u1));

                spare = (float) (radius * std::sin (twoPi * u2));
                hasSpare = true;
                return mean + sigma * (float) (radius * std::cos (twoPi * u2));
            }

        private:
            float mean, sigma, spare = 0.0f;
            bool hasSpare = false;
        };

        //======================================================================
        using Parameters = LimiterEngine::Parameters;

        /** サンプル番号 → (L, R)。 */
        using Generator = std::function<std::pair<float, float> (int)>;

        struct Run
        {
            std::vector<float> left, right;
            int latency = 0;
        };

        /** エンジンに直に流す。`changer`はブロックごとにパラメータを変える（途中で変える試験）。 */
        Run runEngine (Parameters p, double rate, int totalSamples, const Generator& generator,
                       int channels = 2, int block = 512,
                       const std::function<void (int, Parameters&)>& changer = nullptr)
        {
            LimiterEngine engine;
            engine.prepare (rate, block, channels);
            engine.reconfigureNow (p);

            Run run;
            run.left.resize ((size_t) totalSamples);
            run.right.resize ((size_t) totalSamples);
            run.latency = engine.getLatencySamples();

            std::vector<float> l ((size_t) block), r ((size_t) block);

            for (int start = 0, blockIndex = 0; start < totalSamples; start += block, ++blockIndex)
            {
                const int n = std::min (block, totalSamples - start);

                for (int i = 0; i < n; ++i)
                {
                    const auto [a, b] = generator (start + i);
                    l[(size_t) i] = a;
                    r[(size_t) i] = b;
                }

                if (changer != nullptr)
                    changer (blockIndex, p);

                float* pointers[2] { l.data(), r.data() };
                engine.process (pointers, channels, n, p);

                std::copy (l.begin(), l.begin() + n, run.left.begin() + start);
                std::copy (r.begin(), r.begin() + n, run.right.begin() + start);
            }

            return run;
        }

        float samplePeak (const Run& run, int channels = 2)
        {
            float peak = 0.0f;

            for (size_t i = 0; i < run.left.size(); ++i)
            {
                peak = std::max (peak, std::abs (run.left[i]));

                if (channels > 1)
                    peak = std::max (peak, std::abs (run.right[i]));
            }

            return peak;
        }

        /** **別の方法で測るトゥルーピーク**：16倍、片側32タップのKaiser窓sinc（リミッターの補間より長い）。 */
        double referenceTruePeak (const std::vector<float>& x)
        {
            constexpr int factor = 16, half = 32;
            std::vector<double> kernel ((size_t) (2 * half * factor + 1));

            for (int i = 0; i < (int) kernel.size(); ++i)
            {
                const double t = (double) (i - half * factor) / factor;
                kernel[(size_t) i] = LimiterDsp::sinc (t) * LimiterDsp::kaiser (i, (int) kernel.size(), 9.0);
            }

            double peak = 0.0;

            for (int n = half; n + half < (int) x.size(); ++n)
            {
                peak = std::max (peak, (double) std::abs (x[(size_t) n]));

                for (int k = 1; k < factor; ++k)
                {
                    double y = 0.0;

                    for (int j = -half + 1; j <= half; ++j)
                        y += x[(size_t) (n + j)] * kernel[(size_t) (half * factor + k - j * factor)];

                    peak = std::max (peak, std::abs (y));
                }
            }

            return peak;
        }

        //======================================================================

        void testPlumbing()
        {
            say ("--- plumbing (9.5 step 3)");

            const MantaPlugins::Entry* entry = nullptr;

            for (const auto& e : MantaPlugins::getEntries())
                if (juce::String (e.identifier) == "manta:limiter")
                    entry = &e;

            check (entry != nullptr, "the limiter is in the built-in table as manta:limiter");

            if (entry == nullptr)
                return;

            juce::PluginDescription description;
            const bool found = MantaPlugins::findDescription ("manta:limiter", description);

            check (found && description.name == Branding::limiterPluginName && description.category == "Fx|Dynamics"
                     && description.numInputChannels == 2 && description.numOutputChannels == 2,
                    "its description: " + description.name + ", " + description.category + ", 2 in / 2 out");

            auto instance = entry->create();
            auto* processor = dynamic_cast<MantaLimiterProcessor*> (instance.get());
            check (processor != nullptr, "the table creates a MantaLimiterProcessor");

            if (processor == nullptr)
                return;

            processor->prepareToPlay (48000.0, 512);

            // 既定：Lookahead 1 ms（48サンプル）＋ TP補間 6 ＝ 54
            check (processor->getLatencySamples() == 56,
                    "the default latency is reported (" + juce::String (processor->getLatencySamples()) + " samples = 1 ms lookahead + 8 for true peak)");

            // 仕様書：自動化しない7つ
            const juce::StringArray notAutomated { "lookahead", "oversampling", "truePeak", "unity", "audition", "dither", "noiseShaping" };
            bool flagsRight = true;

            for (auto* parameter : processor->getParameters())
            {
                auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
                const bool shouldAutomate = withId != nullptr && ! notAutomated.contains (withId->paramID);
                flagsRight = flagsRight && parameter->isAutomatable() == shouldAutomate;
            }

            check (processor->getParameters().size() == 15 && flagsRight,
                    "15 parameters; lookahead, oversampling and the listening switches are not automatable");

            // 保存して読み直す（パラメータとラウドネスの目標）
            auto& apvts = processor->getValueTreeState();
            apvts.getParameter ("gain")->setValueNotifyingHost (apvts.getParameter ("gain")->convertTo0to1 (6.0f));
            apvts.getParameter ("style")->setValueNotifyingHost (apvts.getParameter ("style")->convertTo0to1 (2.0f));
            apvts.getParameter ("oversampling")->setValueNotifyingHost (apvts.getParameter ("oversampling")->convertTo0to1 (2.0f));
            processor->setLoudnessTarget (-14.0f);

            juce::MemoryBlock state;
            processor->getStateInformation (state);

            auto reopened = entry->create();
            reopened->setStateInformation (state.getData(), (int) state.getSize());
            auto& other = dynamic_cast<MantaLimiterProcessor&> (*reopened);

            const auto a = processor->getLimiterParameters();
            const auto b = other.getLimiterParameters();

            check (b.gainDb == a.gainDb && b.style == 2 && b.oversampling == 2 && other.getLoudnessTarget() == -14.0f,
                    "parameters and the loudness target come back after saving and loading");
        }

        //======================================================================

        void testNoOvershoot()
        {
            say ("--- gate 1: no overshoot (sample peak, oversampling off, true peak off)");

            const double rate = 48000.0;
            const float ceilingDb = -1.0f;
            const float ceiling = std::pow (10.0f, ceilingDb / 20.0f);
            const float allowed = ceiling * (1.0f + 1.0e-6f);

            std::mt19937 random (1);
            PortableGauss gauss (0.0f, 1.0f);

            // 入力3種（+24 dB のホワイトノイズ・掃引サイン・単発インパルス）
            std::vector<float> noiseL (24000), noiseR (24000);
            for (size_t i = 0; i < noiseL.size(); ++i)
            {
                noiseL[i] = 16.0f * 0.3f * gauss (random);
                noiseR[i] = 16.0f * 0.3f * gauss (random);
            }

            const Generator noise = [&] (int i) { return std::make_pair (noiseL[(size_t) i], noiseR[(size_t) i]); };

            const Generator sweep = [rate] (int i)
            {
                const double t = i / rate;
                const double phase = twoPi * 20.0 * 0.5 / std::log (1000.0) * (std::exp (std::log (1000.0) * t / 0.5) - 1.0);
                const float v = (float) (16.0 * std::sin (phase));
                return std::make_pair (v, -0.7f * v);
            };

            const Generator impulse = [] (int i)
            {
                const float v = (i % 4000 == 1000) ? 16.0f : 0.0f;
                return std::make_pair (v, i % 4000 == 2500 ? -12.0f : 0.0f);
            };

            float worst = 0.0f;
            int combinations = 0;

            for (int style = 0; style < 4; ++style)
                for (float lookahead : { 0.1f, 1.0f, 5.0f })
                    for (float attack : { 0.0f, 50.0f, 100.0f })
                        for (float link : { 0.0f, 100.0f })
                            for (const auto* generator : { &noise, &sweep, &impulse })
                            {
                                Parameters p;
                                p.style = style;
                                p.lookaheadMs = lookahead;
                                p.attackPercent = attack;
                                p.linkPercent = link;
                                p.truePeak = false;
                                p.outputDb = ceilingDb;

                                const auto run = runEngine (p, rate, 24000, *generator);
                                worst = std::max (worst, samplePeak (run));
                                ++combinations;
                            }

            check (worst <= allowed, "no output sample exceeds the ceiling over " + juce::String (combinations)
                                       + " combinations of style, lookahead, attack, link and signal (worst "
                                       + db (toDb (worst) - ceilingDb, 7) + " against the ceiling)");

            // **途中で窓の長さを変えても**（スタイルとAttackをブロックごとに入れ替える。
            // Safe に入ると先読みが変わって組み直し＝フェードも起きる）
            {
                std::mt19937 dice (7);
                Parameters p;
                p.truePeak = false;
                p.outputDb = ceilingDb;

                const auto run = runEngine (p, rate, 24000, noise, 2, 256, [&dice] (int, Parameters& q)
                {
                    q.style = (int) (dice() % 4);
                    q.attackPercent = (float) (dice() % 101);
                    q.releaseMs = 1.0f + (float) (dice() % 500);
                    q.autoRelease = (dice() & 1) != 0;
                });

                const float peak = samplePeak (run);
                check (peak <= allowed, "switching style and attack every block still never exceeds it (worst "
                                          + db (toDb (peak) - ceilingDb, 7) + ")");
            }

            // モノラル
            {
                Parameters p;
                p.truePeak = false;
                const auto run = runEngine (p, rate, 24000, noise, 1);
                check (samplePeak (run, 1) <= std::pow (10.0f, -1.0f / 20.0f) * (1.0f + 1.0e-6f), "mono never exceeds it either");
            }
        }

        //======================================================================

        void testLatencyAndNull()
        {
            say ("--- gate 1: latency and null test");

            const double rate = 48000.0;

            // レイテンシー：小さなインパルス（リミットしない）の出る位置 ＝ 報告値
            bool allMatch = true;
            juce::String detail;

            for (int oversampling = 0; oversampling < 4; ++oversampling)
                for (bool truePeak : { false, true })
                    for (float lookahead : { 0.1f, 1.0f, 2.7f })
                    {
                        Parameters p;
                        p.oversampling = oversampling;
                        p.truePeak = truePeak;
                        p.lookaheadMs = lookahead;

                        const auto run = runEngine (p, rate, 4000, [] (int i)
                        {
                            const float v = i == 1000 ? 0.1f : 0.0f;
                            return std::make_pair (v, v);
                        });

                        int loudest = 0;
                        for (int i = 0; i < (int) run.left.size(); ++i)
                            if (std::abs (run.left[(size_t) i]) > std::abs (run.left[(size_t) loudest]))
                                loudest = i;

                        const bool match = loudest - 1000 == run.latency;
                        allMatch = allMatch && match;

                        if (! match || (oversampling == 3 && truePeak && lookahead == 1.0f))
                            detail << " [OS " << (1 << oversampling) << "x, TP " << (truePeak ? "on" : "off")
                                   << ", " << lookahead << " ms: reported " << run.latency << ", measured " << (loudest - 1000) << "]";
                    }

            check (allMatch, "the impulse comes out exactly at the reported latency (24 settings)" + detail);

            // ヌル：Gain 0 dB・リミットしない音量（−20 dBFS前後のノイズ）→ 遅れを直すと原音と一致
            for (bool truePeak : { false, true })
            {
                std::mt19937 random (3);
                PortableGauss gauss (0.0f, 0.05f);
                std::vector<float> input (48000);
                for (auto& v : input)
                    v = std::clamp (gauss (random), -0.5f, 0.5f);

                Parameters p;
                p.truePeak = truePeak;

                const auto run = runEngine (p, rate, (int) input.size(), [&input] (int i)
                {
                    return std::make_pair (input[(size_t) i], -input[(size_t) i]);
                });

                double worst = 0.0;
                for (int i = run.latency; i < (int) input.size(); ++i)
                    worst = std::max (worst, (double) std::abs (run.left[(size_t) i] - input[(size_t) (i - run.latency)]));

                check (toDb (worst) <= -120.0, juce::String ("null test with true peak ") + (truePeak ? "on" : "off")
                                                  + ": the difference from the delayed input is " + db (toDb (worst), 1));
            }
        }

        //======================================================================

        void testTruePeakAndOversampling()
        {
            say ("--- gate 2: true peak, oversampler, audition");

            const double rate = 48000.0;
            const float ceilingDb = -1.0f;

            // 出力TP ≤ 天井 +0.1 dB。**高域の多い信号**（+24 dBのノイズを高域寄りに＋11 kHz付近のサイン）。
            // 8.335：**種を8通り**にして最悪で見る（1つの種ではたまたま通ることがあった。Linuxで別の信号になって −0.88 dB）
            auto makeSignals = [rate] (unsigned int seed, std::vector<float>& signal, std::vector<float>& bandLimited)
            {
                std::mt19937 random (seed);
                PortableGauss gauss (0.0f, 1.0f);
                signal.assign (48000, 0.0f);
                float previous = 0.0f;

                for (size_t i = 0; i < signal.size(); ++i)
                {
                    const float white = gauss (random);
                    const float bright = white - 0.8f * previous;   // 高域寄り
                    previous = white;
                    signal[i] = 4.0f * bright + 6.0f * (float) std::sin (twoPi * 11025.0 * (double) i / rate + 0.7);
                }

                // **同じ信号を20 kHzで帯域制限したもの**（実際の曲はマスターの時点で20 kHz付近までしか無い）
                bandLimited.assign (signal.size(), 0.0f);
                constexpr int half = 127;
                const double cutoff = 20000.0 / rate;   // 0.4167（ナイキストの 0.83）
                std::vector<double> lowpass ((size_t) (2 * half + 1));

                for (int i = 0; i <= 2 * half; ++i)
                    lowpass[(size_t) i] = 2.0 * cutoff * LimiterDsp::sinc (2.0 * cutoff * (i - half))
                                          * LimiterDsp::kaiser (i, 2 * half + 1, 9.0);

                for (int n = half; n + half < (int) signal.size(); ++n)
                {
                    double y = 0.0;
                    for (int i = -half; i <= half; ++i)
                        y += lowpass[(size_t) (i + half)] * signal[(size_t) (n - i)];
                    bandLimited[(size_t) n] = (float) y;
                }
            };

            auto measure = [&] (const std::vector<float>& source, int oversampling, double& meterReading)
            {
                Parameters p;
                p.oversampling = oversampling;
                p.truePeak = true;
                p.outputDb = ceilingDb;

                const auto run = runEngine (p, rate, (int) source.size(), [&source] (int i)
                {
                    return std::make_pair (source[(size_t) i], source[(size_t) i] * 0.8f);
                });

                // 組み立てが落ち着いた後ろ半分で測る
                std::vector<float> tail (run.left.begin() + 24000, run.left.end() - 200);

                LimiterDsp::TruePeakMeter meter;
                meter.prepare();
                float reading = 0.0f;
                for (float v : tail)
                    reading = std::max (reading, meter.push (v));

                meterReading = toDb (reading);
                return toDb (referenceTruePeak (tail));
            };

            auto osName = [] (int oversampling) { return oversampling == 0 ? juce::String ("off") : juce::String (1 << oversampling) + "x"; };

            std::vector<float> signal, bandLimited;
            double worst[4] { -240.0, -240.0, -240.0, -240.0 }, worstMeter[4] {};
            unsigned int worstSeed[4] {};

            for (unsigned int seed = 5; seed < 13; ++seed)
            {
                makeSignals (seed, signal, bandLimited);

                for (int oversampling = 0; oversampling < 4; ++oversampling)
                {
                    double meterReading = 0.0;
                    const double tp = measure (bandLimited, oversampling, meterReading);

                    if (tp > worst[oversampling])
                    {
                        worst[oversampling] = tp;
                        worstMeter[oversampling] = meterReading;
                        worstSeed[oversampling] = seed;
                    }
                }
            }

            for (int oversampling = 0; oversampling < 4; ++oversampling)
                check (worst[oversampling] <= ceilingDb + 0.1,
                       "output true peak with oversampling " + osName (oversampling) + ", worst of 8 signals: " + db (worst[oversampling])
                         + " (ceiling " + db (ceilingDb, 1) + ", allowed +0.1 dB; seed " + juce::String (worstSeed[oversampling])
                         + ", the plug-in's own meter reads " + db (worstMeter[oversampling]) + ")");

            // 見張りの補間が、ナイキスト寄りの正弦波をどれだけ低く読むか（相をずらして最小を取る。合否は付けない）
            {
                juce::StringArray readings;

                for (double f : { 10000.0, 15000.0, 18000.0, 20000.0, 22000.0 })
                {
                    double lowest = 1.0;

                    for (int phaseStep = 0; phaseStep < 16; ++phaseStep)
                    {
                        LimiterDsp::TruePeakDetector detector;
                        detector.prepare();
                        float reading = 0.0f;

                        for (int i = 0; i < 4800; ++i)
                        {
                            const float v = (float) std::sin (twoPi * f * i / rate + twoPi * phaseStep / 16.0);
                            const float r = detector.push (v);
                            if (i > 200)
                                reading = std::max (reading, r);
                        }

                        lowest = std::min (lowest, (double) reading);
                    }

                    readings.add (juce::String ((int) (f / 1000.0)) + " kHz " + db (toDb (lowest)));
                }

                info ("the true-peak detector reads a full-scale sine at: " + readings.joinIntoString (", "));
            }

            // **ナイキストの間際まで持ち上げたまま**の信号（実際の曲より厳しい）。合否は付けず、値だけ出す
            makeSignals (5, signal, bandLimited);

            for (int oversampling : { 0, 1, 2 })
            {
                double meterReading = 0.0;
                const double tp = measure (signal, oversampling, meterReading);

                info ("with content right up to Nyquist, oversampling " + osName (oversampling)
                        + ": output true peak " + db (tp) + ", the plug-in's meter reads " + db (meterReading));
            }

            // オーバーサンプラーの通過域：リミットしない正弦波の振幅（遅れを除いて比べる）
            for (int oversampling = 1; oversampling < 4; ++oversampling)
            {
                double worstRipple = 0.0;

                for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
                {
                    Parameters p;
                    p.oversampling = oversampling;
                    p.truePeak = false;
                    p.outputDb = 0.0f;

                    const auto run = runEngine (p, rate, 24000, [f, rate] (int i)
                    {
                        const float v = (float) (0.25 * std::sin (twoPi * f * i / rate));
                        return std::make_pair (v, v);
                    });

                    double rmsOut = 0.0;
                    for (int i = 12000; i < 24000; ++i)
                        rmsOut += (double) run.left[(size_t) i] * run.left[(size_t) i];

                    const double gain = toDb (std::sqrt (rmsOut / 12000.0) / (0.25 / std::sqrt (2.0)));

                    if (f <= 18000.0)
                        worstRipple = std::max (worstRipple, std::abs (gain));
                }

                check (worstRipple < 0.1, "oversampling " + juce::String (1 << oversampling)
                                             + "x passes 100 Hz-18 kHz within 0.1 dB (worst " + db (worstRipple, 3) + ")");
            }

            // Audition：出力 ＋ 削った成分 ＝ 遅れた入力
            {
                Parameters p;
                p.truePeak = true;

                const Generator loud = [&signal] (int i) { return std::make_pair (signal[(size_t) i], signal[(size_t) i]); };

                const auto normal = runEngine (p, rate, 24000, loud);
                p.audition = true;
                const auto removed = runEngine (p, rate, 24000, loud);

                double worst = 0.0;
                for (int i = normal.latency; i < 24000; ++i)
                {
                    const double sum = (double) normal.left[(size_t) i] + removed.left[(size_t) i];
                    worst = std::max (worst, std::abs (sum - signal[(size_t) (i - normal.latency)]));
                }

                check (toDb (worst) <= -100.0, "output + audition = the delayed input (difference " + db (toDb (worst), 1) + ")");
            }
        }

        //======================================================================

        struct LoudnessRun
        {
            float momentary, shortTerm, integrated, range;
        };

        /** 秒と dBFS の組の並び（ステレオの 1 kHz 正弦波）を流して、ラウドネスを返す。 */
        LoudnessRun measureSegments (const std::vector<std::pair<double, double>>& segments, double rate = 48000.0)
        {
            LimiterMeters meters;
            meters.prepare (rate, 2);

            const int block = 480;
            std::vector<float> l ((size_t) block), zeros ((size_t) block, 0.0f);
            double phase = 0.0;

            for (const auto& [seconds, levelDb] : segments)
            {
                const double amplitude = std::pow (10.0, levelDb / 20.0);
                const int total = (int) std::lround (seconds * rate);

                for (int start = 0; start < total; start += block)
                {
                    const int n = std::min (block, total - start);

                    for (int i = 0; i < n; ++i)
                    {
                        l[(size_t) i] = (float) (amplitude * std::sin (phase));
                        phase += twoPi * 1000.0 / rate;
                    }

                    const float* channels[2] { l.data(), l.data() };
                    const float* tp[2] { zeros.data(), zeros.data() };
                    meters.push (channels, channels, zeros.data(), tp, 2, n, 0.0f);
                }
            }

            return { meters.getMomentary(), meters.getShortTerm(), meters.getIntegrated(), meters.getLoudnessRange() };
        }

        void testLoudness()
        {
            say ("--- gate 3: loudness (ITU-R BS.1770-4, EBU Tech 3341 / 3342)");

            // K特性：48 kHz で規格の表の値になること
            {
                LimiterMeters meters;
                meters.prepare (48000.0, 2);
                const auto k = meters.getKWeightingCoefficients();
                const double table[7] { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                                        -1.69065929318241, 0.73248077421585, -1.99004745483398, 0.99007225036621 };

                double worst = 0.0;
                for (int i = 0; i < 7; ++i)
                    worst = std::max (worst, std::abs (k[(size_t) i] - table[i]));

                check (worst < 1.0e-8, "K-weighting at 48 kHz matches the table in BS.1770-4 (worst difference "
                                         + juce::String (worst, 12) + ")");
            }

            auto near = [] (float value, float expected, float tolerance) { return std::abs (value - expected) <= tolerance; };

            // Tech 3341 の1〜4（ステレオ 1 kHz）
            {
                const auto r = measureSegments ({ { 20.0, -23.0 } });
                check (near (r.momentary, -23.0f, 0.1f) && near (r.shortTerm, -23.0f, 0.1f) && near (r.integrated, -23.0f, 0.1f),
                        "3341 case 1 (-23 dBFS, 20 s): M " + juce::String (r.momentary, 2) + ", S " + juce::String (r.shortTerm, 2)
                          + ", I " + juce::String (r.integrated, 2) + " LUFS (-23 +-0.1)");
            }
            {
                const auto r = measureSegments ({ { 20.0, -33.0 } });
                check (near (r.integrated, -33.0f, 0.1f), "3341 case 2 (-33 dBFS): I " + juce::String (r.integrated, 2) + " LUFS");
            }
            {
                const auto r = measureSegments ({ { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } });
                check (near (r.integrated, -23.0f, 0.1f), "3341 case 3 (relative gate): I " + juce::String (r.integrated, 2) + " LUFS");
            }
            {
                const auto r = measureSegments ({ { 10.0, -72.0 }, { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 }, { 10.0, -72.0 } });
                check (near (r.integrated, -23.0f, 0.1f), "3341 case 4 (absolute and relative gate): I " + juce::String (r.integrated, 2) + " LUFS");
            }

            // Tech 3342 の1〜4（LRA ±1 LU）
            const struct { std::vector<std::pair<double, double>> segments; float lra; const char* name; } cases[]
            {
                { { { 20.0, -20.0 }, { 20.0, -30.0 } }, 10.0f, "case 1 (-20/-30)" },
                { { { 20.0, -20.0 }, { 20.0, -15.0 } }, 5.0f, "case 2 (-20/-15)" },
                { { { 20.0, -40.0 }, { 20.0, -20.0 } }, 20.0f, "case 3 (-40/-20)" },
                { { { 20.0, -50.0 }, { 20.0, -35.0 }, { 20.0, -20.0 }, { 20.0, -35.0 }, { 20.0, -50.0 } }, 15.0f, "case 4 (-50/-35/-20/-35/-50)" },
            };

            for (const auto& c : cases)
            {
                const auto r = measureSegments (c.segments);
                check (near (r.range, c.lra, 1.0f), juce::String ("3342 ") + c.name + ": LRA " + juce::String (r.range, 2)
                                                      + " LU (" + juce::String (c.lra, 0) + " +-1)");
            }

            // TPメーター：fs/4 の正弦波を 45° ずらす → サンプルは振幅の 1/√2、真のピークは振幅そのもの
            {
                LimiterDsp::TruePeakMeter meter;
                meter.prepare();
                float peak = 0.0f, samplePeakValue = 0.0f;

                for (int i = 0; i < 4800; ++i)
                {
                    const float v = (float) (0.5 * std::sin (twoPi * 0.25 * i + 0.25 * 3.141592653589793));
                    samplePeakValue = std::max (samplePeakValue, std::abs (v));
                    peak = std::max (peak, meter.push (v));
                }

                check (std::abs (toDb (peak) - toDb (0.5)) < 0.2, "a 45-degree fs/4 sine: sample peak " + db (toDb (samplePeakValue))
                                                                     + ", true peak " + db (toDb (peak)) + " (the sine is -6.02 dB)");
            }
        }

        //======================================================================

        void testDitherAndCpu()
        {
            say ("--- gate 4: dither, CPU");

            const double rate = 48000.0;

            for (int bits : { 16, 24 })
            {
                Parameters p;
                p.dither = bits == 16 ? 1 : 3;
                p.truePeak = false;
                const float lsb = std::pow (2.0f, (float) -(bits - 1));

                const auto run = runEngine (p, rate, 48000, [] (int) { return std::make_pair (0.0f, 0.0f); });

                bool onGrid = true;
                float largest = 0.0f;
                double mean = 0.0;

                for (float v : run.left)
                {
                    const float steps = v / lsb;
                    onGrid = onGrid && std::abs (steps - std::round (steps)) < 1.0e-3f;
                    largest = std::max (largest, std::abs (v));
                    mean += v;
                }

                mean /= (double) run.left.size();

                check (onGrid && largest <= 1.0f * lsb + 1.0e-9f && std::abs (mean) < 0.05 * lsb,
                        juce::String (bits) + " bit dither on silence: every sample sits on the grid, at most 1 LSB, no DC ("
                          + juce::String (largest / lsb, 2) + " LSB max)");
            }

            // CPU（参考）：48 kHz、代表設定。**入力は先に作っておき、エンジンの処理だけを測る**
            // （乱数を作る時間まで入れると、1サンプルごとの関数呼び出しと乱数で数倍に見えます）
            {
                std::mt19937 random (9);
                PortableGauss gauss (0.0f, 0.3f);   // 数 dB ずっとリミットがかかる音量
                std::vector<float> source (48000);
                for (auto& v : source)
                    v = gauss (random);

                for (int oversampling : { 0, 1, 2, 3 })
                {
                    Parameters p;
                    p.oversampling = oversampling;
                    p.truePeak = true;

                    LimiterEngine engine;
                    engine.prepare (rate, 512, 2);
                    engine.reconfigureNow (p);

                    std::vector<float> l (512), r (512);
                    double spent = 0.0;

                    for (int block = 0; block < 480000 / 512; ++block)
                    {
                        const int offset = (block * 512) % (48000 - 512);
                        std::copy (source.begin() + offset, source.begin() + offset + 512, l.begin());
                        std::copy (source.begin() + offset, source.begin() + offset + 512, r.begin());

                        float* pointers[2] { l.data(), r.data() };
                        const auto start = juce::Time::getMillisecondCounterHiRes();
                        engine.process (pointers, 2, 512, p);
                        spent += juce::Time::getMillisecondCounterHiRes() - start;
                    }

                    info ("CPU: stereo at 48 kHz, oversampling " + juce::String (oversampling == 0 ? "off" : juce::String (1 << oversampling) + "x")
                            + ", true peak on, limiting all the time: " + juce::String (spent / 100.0, 2)
                            + " % of one core (target " + (oversampling == 0 ? "1 %" : oversampling == 3 ? "5 %" : "-") + ")");
                }
            }
        }

        //======================================================================
        /** 曲らしい音（ピンクノイズ＋キックのような低い音の打ち込み）を、画面の更新を挟んで流す。 */
        void feedMusic (MantaLimiterProcessor& processor, MantaLimiterEditor* editor, double seconds)
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            std::mt19937 random (21);
            PortableGauss gauss (0.0f, 0.12f);
            float b0 = 0, b1 = 0, b2 = 0;
            const double rate = 48000.0;
            const int blocks = (int) (seconds * rate / 512);
            int sample = 0;

            for (int block = 0; block < blocks; ++block)
            {
                for (int i = 0; i < 512; ++i, ++sample)
                {
                    const float w = gauss (random);
                    b0 = 0.997f * b0 + 0.029f * w;
                    b1 = 0.985f * b1 + 0.032f * w;
                    b2 = 0.950f * b2 + 0.048f * w;
                    const float pink = (b0 + b1 + b2 + 0.1f * w) * 2.5f;

                    // 0.5秒ごとのキック（減衰する 55 Hz）
                    const double t = std::fmod (sample / rate, 0.5);
                    const float kick = (float) (0.9 * std::exp (-t * 18.0) * std::sin (twoPi * 55.0 * t));

                    buffer.setSample (0, i, pink + kick);
                    buffer.setSample (1, i, 0.9f * pink + kick);
                }

                processor.processBlock (buffer, midi);

                if (editor != nullptr && block % 3 == 0)
                    editor->tickForTesting();
            }
        }

        void testEditor (const juce::String& commandLine)
        {
            say ("--- the window (drawn off-screen)");

            MantaLimiterProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            auto& apvts = processor.getValueTreeState();
            apvts.getParameter ("gain")->setValueNotifyingHost (apvts.getParameter ("gain")->convertTo0to1 (8.0f));
            processor.setLoudnessTarget (-14.0f);

            std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
            auto* editor = dynamic_cast<MantaLimiterEditor*> (base.get());

            check (editor != nullptr, "the processor opens a MantaLimiterEditor");

            if (editor == nullptr)
                return;

            check (editor->getWidth() == MantaLimiterEditor::fixedWidth && editor->getHeight() == MantaLimiterEditor::fixedHeight
                     && ! editor->isResizable(),
                    "the window is " + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight())
                      + " and cannot be resized (like the other built-in plug-ins)");

            // 8.334：窓を詰めた（960 → 800）。**部品が窓からはみ出さず、互いに重ならず、
            // ボタンの文字が切れない**こと（隠れている Custom の値の欄も含めて）
            {
                juce::StringArray layoutProblems;
                const auto children = editor->getChildren();

                for (int i = 0; i < children.size(); ++i)
                {
                    auto* child = children[i];
                    const auto name = child->getTitle().isNotEmpty() ? child->getTitle()
                                    : dynamic_cast<juce::Button*> (child) != nullptr ? static_cast<juce::Button*> (child)->getButtonText()
                                    : juce::String ("child ") + juce::String (i);

                    if (child->getBounds().isEmpty() || ! editor->getLocalBounds().contains (child->getBounds()))
                        layoutProblems.add (name + " is outside the window " + child->getBounds().toString());

                    if (auto* button = dynamic_cast<juce::TextButton*> (child))
                        if (juce::GlyphArrangement::getStringWidthInt (button->getLookAndFeel().getTextButtonFont (*button, button->getHeight()),
                                                                       button->getButtonText()) + 8 > button->getWidth())
                            layoutProblems.add ("\"" + button->getButtonText() + "\" does not fit its button");

                    for (int j = i + 1; j < children.size(); ++j)
                        if (child->getBounds().intersects (children[j]->getBounds()))
                            layoutProblems.add (name + " overlaps child " + juce::String (j));
                }

                check (layoutProblems.isEmpty(), "every control is inside the window, none overlap, button labels fit"
                                             + (layoutProblems.isEmpty() ? juce::String() : ": " + layoutProblems.joinIntoString ("; ")));
            }

            // 8.342：**下の帯の中の部品は、帯の端から 6 px 以上離す**（本人の指定「余白をとって見た目を整える」）。
            // 前は帯を塗る位置と部品を置く位置を別々に計算していて、見出しやボタンが帯の上端に貼り付いていた。
            // 帯の外の部品も、帯に 6 px より近づかない
            {
                juce::StringArray bandProblems;
                const auto band = editor->getBandAreaForTesting();
                const auto inner = band.reduced (6);

                for (auto* child : editor->getChildren())
                {
                    const auto bounds = child->getBounds();

                    if (! bounds.intersects (band.expanded (6)) || inner.contains (bounds))
                        continue;

                    const auto name = dynamic_cast<juce::Button*> (child) != nullptr ? static_cast<juce::Button*> (child)->getButtonText()
                                    : dynamic_cast<juce::Label*> (child) != nullptr ? static_cast<juce::Label*> (child)->getText()
                                    : juce::String (typeid (*child).name());

                    bandProblems.add ("\"" + name + "\" " + bounds.toString());
                }

                check (! band.isEmpty() && bandProblems.isEmpty(),
                       "every control in the bottom band keeps 6 px from its edges (band " + band.toString() + ")"
                         + (bandProblems.isEmpty() ? juce::String() : ": too close: " + bandProblems.joinIntoString ("; ")));
            }


            feedMusic (processor, editor, 6.0);

            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            double total = 0.0;

            for (int i = 0; i < 10; ++i)
            {
                juce::Graphics g (image);
                const auto start = juce::Time::getMillisecondCounterHiRes();
                editor->paintEntireComponent (g, false);
                total += juce::Time::getMillisecondCounterHiRes() - start;
            }

            info ("drawing the whole window takes " + juce::String (total / 10.0, 2) + " ms (software renderer)");

            // `--png=<ファイル>`：自分の目で見るための絵（ダーク・ライト）
            if (commandLine.contains ("--png="))
            {
                const juce::File file (commandLine.fromFirstOccurrenceOf ("--png=", false, false).upToFirstOccurrenceOf (" ", false, false).unquoted());

                for (const bool light : { false, true })
                {
                    AppColours::setTheme (light ? AppColours::Theme::Light : AppColours::Theme::Dark);
                    feedMusic (processor, editor, 0.5);

                    juce::Image picture (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
                    {
                        juce::Graphics g (picture);
                        editor->paintEntireComponent (g, false);
                    }

                    const auto target = light ? file.getSiblingFile (file.getFileNameWithoutExtension() + "-light.png") : file;
                    target.deleteFile();
                    juce::FileOutputStream out (target);
                    juce::PNGImageFormat png;

                    if (out.openedOk() && png.writeImageToStream (picture, out))
                        info ("wrote " + target.getFullPathName());
                }

                AppColours::setTheme (AppColours::Theme::Dark);
            }
        }
    }

    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--limiter-selftest"))
            return false;

        problems = 0;
        say ("limiter self-test (8.333)");

        // 本体の色を読んでおく（設定ファイルには何も書かない）
        AppColours::setTheme (AppColours::Theme::Dark);

        testPlumbing();
        testNoOvershoot();
        testLatencyAndNull();
        testTruePeakAndOversampling();
        testLoudness();
        testDitherAndCpu();
        testEditor (commandLine);

        say ("--- " + juce::String (problems) + " problem(s) ---");
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);
        return true;
    }
}
