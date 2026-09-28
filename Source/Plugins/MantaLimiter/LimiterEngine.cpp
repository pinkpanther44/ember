#include "LimiterEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    constexpr double gainRampSeconds = 0.020;   // 設計書：Gain・Output・Unityは20 msの直線
}

const LimiterEngine::StyleConstants& LimiterEngine::styleConstants (int style)
{
    // 設計書「スタイル定数」（attackRatio, 三角窓か, releaseScale, 先読みを5 msに固定するか）
    static const StyleConstants styles[]
    {
        { 1.00f, true,  1.0f, false },   // Transparent
        { 0.50f, true,  1.0f, false },   // Punchy
        { 0.25f, false, 0.5f, false },   // Aggressive
        { 1.00f, true,  2.0f, true  },   // Safe（Lookahead 5 ms固定）
    };

    return styles[std::clamp (style, 0, 3)];
}

void LimiterEngine::prepare (double rate, int maxBlockSize, int numChannels)
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    maxBlock = std::max (1, maxBlockSize);
    channelsPrepared = std::clamp (numChannels, 1, 2);

    oversampler.prepare (2, maxBlock);

    // **いちばん高いレート（8倍）・いちばん長い先読み（5 ms）**ぶんを確保（設計書：確保は prepare だけ）
    const double maxCoreRate = sampleRate * 8.0;
    core.prepare (maxCoreRate, (int) std::ceil (0.005 * maxCoreRate) + 2, maxBlock * 8);

    for (int c = 0; c < 2; ++c)
    {
        dcFilters[(size_t) c].prepare (sampleRate);
        ditherers[(size_t) c].reset (0x12345u + 977u * (uint32_t) c);
        truePeakMeters[(size_t) c].prepare();
        tpTrace[(size_t) c].assign ((size_t) maxBlock, 0.0f);
        inputTrace[(size_t) c].assign ((size_t) maxBlock, 0.0f);
    }

    grPerSample.assign ((size_t) maxBlock, 0.0f);
    gainRamp.assign ((size_t) maxBlock, 1.0f);

    haveStructure = false;
    fade = Fade::none;
}

LimiterEngine::Structure LimiterEngine::structureFor (const Parameters& p) const
{
    Structure s;
    s.stages = std::clamp (p.oversampling, 0, 3);

    const int factor = 1 << s.stages;
    const double lookaheadMs = styleConstants (p.style).forceMaxLookahead ? 5.0 : (double) p.lookaheadMs;

    s.lookaheadCore = std::max (1, (int) std::lround (lookaheadMs / 1000.0 * sampleRate * factor));

    // 設計書は「オーバーサンプリングが4倍以上なら、その信号をそのまま使う」。
    // **4倍でも点の間の山を見落とす**（20 kHzの音で最大0.47 dB。自己検査で天井を0.22 dB超えた）ので、
    // **そのまま使うのは8倍のときだけ**にしています（8倍なら見落としは0.12 dB以下）
    s.truePeakDetection = p.truePeak && factor < 8;
    return s;
}

void LimiterEngine::applyStructure (const Structure& s)
{
    current = s;
    haveStructure = true;

    oversampler.setStages (s.stages);

    const int factor = 1 << s.stages;
    core.setSampleRate (sampleRate * factor);

    // **遅れを基準のレートの整数に揃える**（ヘッダーの説明）
    const int tp = s.truePeakDetection ? LimiterCore::truePeakDelay : 0;
    const int total = s.lookaheadCore + tp + oversampler.getDelayInCoreSamples();
    const int padding = (factor - total % factor) % factor;

    core.configure (s.lookaheadCore, s.truePeakDetection, padding);

    const int latency = (total + padding) / factor;

    if (latencySamples.exchange (latency) != latency)
        latencyChangedFlag.store (true);

    for (auto& f : dcFilters)
        f.reset();

    for (auto& m : truePeakMeters)
        m.reset();
}

LimiterCore::Dynamics LimiterEngine::dynamicsFor (const Parameters& p) const
{
    const auto& style = styleConstants (p.style);

    LimiterCore::Dynamics d;
    d.attackRatio = std::clamp (p.attackPercent / 100.0f, 0.0f, 1.0f) * style.attackRatio;
    d.triangular = style.triangular;
    d.releaseMs = p.releaseMs * style.releaseScale;
    d.autoRelease = p.autoRelease;
    d.link = std::clamp (p.linkPercent / 100.0f, 0.0f, 1.0f);
    d.ceilingDb = ceilingDbCurrent;
    d.audition = p.audition;
    return d;
}

void LimiterEngine::reconfigureNow (const Parameters& p)
{
    ceilingDbCurrent = p.outputDb;
    applyStructure (structureFor (p));
    core.setDynamics (dynamicsFor (p));

    gainCurrent = gainTarget = dbToGain (p.gainDb);
    gainStepsLeft = 0;
    fade = Fade::none;
}

void LimiterEngine::process (float* const* channels, int numChannels, int numSamples, const Parameters& p)
{
    numChannels = std::clamp (numChannels, 1, 2);
    numSamples = std::min (numSamples, maxBlock);

    if (! haveStructure)
        reconfigureNow (p);

    //--------------------------------------------------------------------------
    // 1：構造が変わったら、消してから組み直す（5 ms で消す → 組み直し → 5 ms で戻す）
    {
        const auto wanted = structureFor (p);

        if (fade == Fade::out)
        {
            pending = wanted;
        }
        else if (wanted != current)
        {
            pending = wanted;
            fadeLength = std::max (1, (int) std::lround (fadeSeconds * sampleRate));

            // 戻している途中なら、**いまの音量から**消し始める
            fadePosition = fade == Fade::in ? fadeLength - fadePosition : 0;
            fade = Fade::out;
        }
    }

    //--------------------------------------------------------------------------
    // 2：Gain（20 ms の直線で滑らかに）→ DCフィルタ

    if (const float target = dbToGain (p.gainDb); target != gainTarget)
    {
        gainTarget = target;
        gainStepsLeft = std::max (1, (int) std::lround (gainRampSeconds * sampleRate));
        gainStep = (gainTarget - gainCurrent) / (float) gainStepsLeft;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (gainStepsLeft > 0)
        {
            gainCurrent += gainStep;

            if (--gainStepsLeft == 0)
                gainCurrent = gainTarget;
        }

        gainRamp[(size_t) i] = gainCurrent;
    }

    meters = {};

    for (int c = 0; c < numChannels; ++c)
    {
        float* x = channels[c];

        for (int i = 0; i < numSamples; ++i)
        {
            float v = x[i] * gainRamp[(size_t) i];

            if (p.dcFilter)
                v = dcFilters[(size_t) c].process (v);

            x[i] = v;
            inputTrace[(size_t) c][(size_t) i] = v;
            meters.inputPeak[(size_t) c] = std::max (meters.inputPeak[(size_t) c], std::abs (v));
        }
    }

    // 天井は20 msでブロックごとに寄せる（ブロックの中は一定。天井が途中で動かないので保証が崩れない）
    {
        const float k = std::min (1.0f, (float) numSamples / (float) (gainRampSeconds * sampleRate));
        ceilingDbCurrent += (p.outputDb - ceilingDbCurrent) * k;

        if (std::abs (p.outputDb - ceilingDbCurrent) < 0.01f)
            ceilingDbCurrent = p.outputDb;
    }

    core.setDynamics (dynamicsFor (p));

    //--------------------------------------------------------------------------
    // 3〜5：上げる → コア → 下げる

    const int factor = oversampler.getFactor();

    for (int c = 0; c < numChannels; ++c)
        coreChannels[(size_t) c] = oversampler.up (c, channels[c], numSamples);

    core.process (coreChannels.data(), numChannels, numSamples * factor);

    for (int c = 0; c < numChannels; ++c)
        oversampler.down (c, coreChannels[(size_t) c], channels[c], numSamples);

    {
        const float* trace = core.getGainReductionTrace();

        for (int i = 0; i < numSamples; ++i)
        {
            float worst = 1.0f;
            for (int k = 0; k < factor; ++k)
                worst = std::min (worst, trace[i * factor + k]);

            grPerSample[(size_t) i] = worst >= 1.0f ? 0.0f : LimiterDsp::fastGainToDb (std::max (worst, 1.0e-9f));
            meters.gainReductionDb = std::min (meters.gainReductionDb, grPerSample[(size_t) i]);
        }
    }

    //--------------------------------------------------------------------------
    // 6：Unity Gain → 組み直しのフェード → ディザー

    const bool dither = p.dither > 0;
    const int bits = p.dither == 1 ? 16 : p.dither == 2 ? 20 : 24;
    const float lsb = std::pow (2.0f, (float) -(bits - 1));
    const bool shaping = dither && p.noiseShaping > 0;

    bool fadeFinished = false;

    for (int i = 0; i < numSamples; ++i)
    {
        float fadeGain = 1.0f;

        if (fade == Fade::out)
        {
            fadeGain = std::max (0.0f, 1.0f - (float) fadePosition / (float) fadeLength);

            if (fadePosition < fadeLength)
                ++fadePosition;
            else
                fadeFinished = true;
        }
        else if (fade == Fade::in)
        {
            fadeGain = std::min (1.0f, (float) fadePosition / (float) fadeLength);

            if (++fadePosition >= fadeLength)
                fade = Fade::none;
        }

        for (int c = 0; c < numChannels; ++c)
        {
            float v = channels[c][i];

            // 設計書：**Gainと同じ滑らかな値で割る**（ずれると音量が揺れる）
            if (p.unity)
                v /= gainRamp[(size_t) i];

            v *= fadeGain;

            if (dither)
                v = ditherers[(size_t) c].process (v, lsb, shaping);

            channels[c][i] = v;

            //------------------------------------------------------------------
            // 7：出力のメーター
            meters.outputPeak[(size_t) c] = std::max (meters.outputPeak[(size_t) c], std::abs (v));

            const float tp = truePeakMeters[(size_t) c].push (v);
            tpTrace[(size_t) c][(size_t) i] = tp;
            meters.outputTruePeak[(size_t) c] = std::max (meters.outputTruePeak[(size_t) c], tp);
        }
    }

    // 消し終わったら組み直して、次のブロックから戻す
    if (fade == Fade::out && (fadeFinished || fadePosition >= fadeLength))
    {
        applyStructure (pending);
        core.setDynamics (dynamicsFor (p));
        fade = Fade::in;
        fadePosition = 0;
    }

    (void) factor;
}
