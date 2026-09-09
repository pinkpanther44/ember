#include "MantaEQProcessor.h"

#include "MantaEQEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

#include <cmath>
#include <cstring>

using MantaEQParams::Channel;
using MantaEQParams::Shape;

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaEqIdentifier = "manta:eq";

    /** エンベロープ追従の係数。`ms`ミリ秒で1/eまで近づく。 */
    float timeConstant (float milliseconds, double sampleRate)
    {
        const double seconds = juce::jmax (0.0001, (double) milliseconds * 0.001);
        return (float) std::exp (-1.0 / (seconds * juce::jmax (1.0, sampleRate)));
    }

    /** float1つを署名へ混ぜる。**値が変わったかを見るだけ**なので、
        衝突しにくければ十分（FNV-1aの掛け算だけ借りている）。 */
    void mixIntoSignature (juce::uint64& signature, float value)
    {
        juce::uint32 bits = 0;
        std::memcpy (&bits, &value, sizeof (bits));

        signature ^= (juce::uint64) bits;
        signature *= 0x100000001b3ULL;
    }

    /** 定位・M/Sバランスに使う等パワーの組（真ん中で両方1.0）。 */
    void equalPowerPair (float position, float& firstOut, float& secondOut)
    {
        const float angle = (juce::jlimit (-1.0f, 1.0f, position) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

        firstOut  = std::cos (angle) * juce::MathConstants<float>::sqrt2;
        secondOut = std::sin (angle) * juce::MathConstants<float>::sqrt2;
    }
}

//==============================================================================

void MantaEQProcessor::BandState::resetFilters() noexcept
{
    for (auto& channelFilters : filters)
        for (auto& filter : channelFilters)
            filter.reset();

    for (auto& d : detector)
        d.reset();

    envelope = 0.0f;
}

//==============================================================================

MantaEQProcessor::MantaEQProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaEQ", MantaEQParams::createParameterLayout())
{
    // **文字列で引くのは、ここ1回だけ。** 32サンプルごとに引き直すと、
    // それだけで無視できない時間になります
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        auto& pointers = bandParameters[(size_t) band];

        pointers.enabled      = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandEnabled));
        pointers.active       = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandActive));
        pointers.frequency    = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandFreq));
        pointers.gain         = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandGain));
        pointers.q            = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandQ));
        pointers.shape        = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandShape));
        pointers.slope        = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandSlope));
        pointers.channel      = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandChannel));
        pointers.dynEnabled   = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandDynOn));
        pointers.dynThreshold = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandDynThresh));
        pointers.dynRange     = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandDynRange));
        pointers.dynAttack    = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandDynAttack));
        pointers.dynRelease   = apvts.getRawParameterValue (MantaEQParams::bandParamId (band, MantaEQParams::bandDynRelease));

        publishedDynamicOffsetDb[(size_t) band].store (0.0f);
    }

    outputGainParam        = apvts.getRawParameterValue (MantaEQParams::outputGain);
    outputPanParam         = apvts.getRawParameterValue (MantaEQParams::outputPan);
    outputMsBalanceParam   = apvts.getRawParameterValue (MantaEQParams::outputMsBalance);
    outputPhaseInvertParam = apvts.getRawParameterValue (MantaEQParams::outputPhaseInvert);
    outputAutoGainParam    = apvts.getRawParameterValue (MantaEQParams::outputAutoGain);

    processingModeParam       = apvts.getRawParameterValue (MantaEQParams::processingMode);
    processingResolutionParam = apvts.getRawParameterValue (MantaEQParams::processingResolution);
}

MantaEQProcessor::~MantaEQProcessor()
{
    // **溜まっている呼び出しを取り消してから壊すこと。**
    // 残したまま壊すと、すでに無いオブジェクトへ`handleAsyncUpdate()`が入ります
    cancelPendingUpdate();
}

//==============================================================================

void MantaEQProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (mantaEqIdentifier, description);
}

const juce::String MantaEQProcessor::getName() const
{
    return Branding::eqPluginName;
}

//==============================================================================

void MantaEQProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない。8.153の書き出し）。
    // **何度でも来ます**ので、そのたびに作り直します
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    displaySampleRate.store (currentSampleRate);

    for (auto& band : bands)
    {
        band.resetFilters();
        band.dynamicOffsetDb = 0.0f;
    }

    for (auto& channelFilters : soloFilters)
        for (auto& filter : channelFilters)
            filter.reset();

    preFifo.reset();
    postFifo.reset();

    const double rampSeconds = 0.02;
    for (auto* smoothed : { &smoothedOutputGain, &smoothedPanLeft, &smoothedPanRight,
                            &smoothedMidGain, &smoothedSideGain })
        smoothed->reset (currentSampleRate, rampSeconds);

    // **係数を作り直させる。** 署名を潰しておくと、次のブロックで必ず通ります
    autoGainSignature = 0;
    linearPhaseSignature = 0;

    linearPhase.prepare (currentSampleRate);

    updateSettingsFromParameters();

    for (int band = 0; band < MantaEQParams::numBands; ++band)
        refreshBandCoefficients (band);

    // 仕様書4.7：Zero Latencyは0、Linear Phaseは処理解像度そのもの
    // （`LinearPhaseEngine::getLatencySamples()`）。
    // **`prepareToPlay()`のここだけは、その場で申告してよい**
    // （音はまだ回っていないので、聞き手がいない）
    currentMode = (MantaEQParams::ProcessingMode) juce::jlimit (
        0, (int) MantaEQParams::ProcessingMode::numModes - 1, (int) processingModeParam->load());

    const int resolutionIndex = juce::jlimit (0, LinearPhaseEngine::numResolutionChoices - 1,
                                               (int) processingResolutionParam->load());
    linearPhase.setIrLength (LinearPhaseEngine::resolutionChoices[resolutionIndex]);

    const int latency = currentMode == MantaEQParams::ProcessingMode::linearPhase
                          ? linearPhase.getLatencySamples() : 0;

    wantedLatency.store (latency);
    publishedLatency.store (latency);
    setLatencySamples (latency);
}

void MantaEQProcessor::releaseResources()
{
    for (auto& band : bands)
        band.resetFilters();

    linearPhase.reset();
}

void MantaEQProcessor::updateLatency (int wantedLatencySamples)
{
    if (wantedLatency.exchange (wantedLatencySamples) == wantedLatencySamples)
        return;

    // **`setLatencySamples()`を音のスレッドから呼ばないこと。**
    // 中で`updateHostDisplay()`が走り、購読している側（`AudioProcessorGraph`や
    // 画面）へその場で通知が飛びます。メッセージスレッドへ回します
    triggerAsyncUpdate();
}

void MantaEQProcessor::handleAsyncUpdate()
{
    const int latency = wantedLatency.load();

    if (publishedLatency.exchange (latency) == latency)
        return;

    // 9.5：**レイテンシは正直に申告する。**
    // 呼ばないと、グラフが他の経路とずらしてくれません（＝Linear Phaseにした
    // トラックだけ遅れて聴こえます）
    setLatencySamples (latency);
}

bool MantaEQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput  = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    return mainInput == mainOutput;
}

//==============================================================================

void MantaEQProcessor::updateSettingsFromParameters()
{
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        const auto& pointers = bandParameters[(size_t) band];
        auto& stored = settings[(size_t) band];

        MantaEQParams::BandSettings fresh;

        fresh.enabled   = pointers.enabled->load() > 0.5f;
        fresh.active    = pointers.active->load() > 0.5f;
        fresh.frequency = pointers.frequency->load();
        fresh.gainDb    = pointers.gain->load();
        fresh.q         = pointers.q->load();
        fresh.shape     = (Shape) juce::jlimit (0, (int) Shape::numShapes - 1,
                                                 (int) pointers.shape->load());
        fresh.channel   = (Channel) juce::jlimit (0, (int) Channel::numChannels - 1,
                                                   (int) pointers.channel->load());

        const int slopeIndex = juce::jlimit (0, MantaEQParams::numSlopeChoices - 1,
                                              (int) pointers.slope->load());
        fresh.slopeDbPerOctave = MantaEQParams::slopeChoicesDbPerOctave[slopeIndex];

        fresh.dynamicEnabled     = pointers.dynEnabled->load() > 0.5f;
        fresh.dynamicThresholdDb = pointers.dynThreshold->load();
        fresh.dynamicRangeDb     = pointers.dynRange->load();
        fresh.dynamicAttackMs    = pointers.dynAttack->load();
        fresh.dynamicReleaseMs   = pointers.dynRelease->load();

        // ダイナミクスで動いているぶんは**引き継ぐ**（パラメータではないので）
        fresh.dynamicOffsetDb = stored.dynamicOffsetDb;

        const bool needsReset = fresh.enabled != stored.enabled
                                 || fresh.shape != stored.shape
                                 || fresh.channel != stored.channel
                                 || fresh.slopeDbPerOctave != stored.slopeDbPerOctave;

        const bool changed = needsReset
                              || fresh.frequency != stored.frequency
                              || fresh.gainDb != stored.gainDb
                              || fresh.q != stored.q;

        stored = fresh;

        if (needsReset)
        {
            // **形が変わったら状態を捨てる。** 段数が変わったのに前の値が残っていると、
            // 一瞬だけ大きな音が出ます（段の数が増えたときにいちばん出やすい）
            bands[(size_t) band].resetFilters();
        }

        if (changed)
        {
            refreshBandCoefficients (band);
            bands[(size_t) band].detectorCoeffs
                = EQFilterDesign::designDetector (fresh.frequency, fresh.q, currentSampleRate);
        }
    }
}

void MantaEQProcessor::refreshBandCoefficients (int bandIndex)
{
    auto& band = bands[(size_t) bandIndex];
    const auto& stored = settings[(size_t) bandIndex];

    const int previousSections = band.sections.numSections;

    band.sections = EQFilterDesign::designBand (stored, currentSampleRate);

    // 段が増えたぶんは、前の値が入っていない状態から始める
    for (int section = previousSections; section < band.sections.numSections; ++section)
        for (auto& channelFilters : band.filters)
            channelFilters[section].reset();
}

void MantaEQProcessor::updateDynamics (int bandIndex)
{
    auto& band = bands[(size_t) bandIndex];
    auto& stored = settings[(size_t) bandIndex];

    if (! stored.enabled || ! stored.dynamicEnabled)
    {
        if (band.dynamicOffsetDb != 0.0f)
        {
            band.dynamicOffsetDb = 0.0f;
            stored.dynamicOffsetDb = 0.0f;
            refreshBandCoefficients (bandIndex);
            publishedDynamicOffsetDb[(size_t) bandIndex].store (0.0f);
        }

        return;
    }

    const float levelDb = juce::Decibels::gainToDecibels (band.envelope, -100.0f);

    // 閾値からどれだけ上か。**12dB上で全開**にしている
    // （狭くすると切り替わりが「スイッチ」に、広くすると効きが分かりにくくなる）
    const float over = levelDb - stored.dynamicThresholdDb;
    const float amount = juce::jlimit (0.0f, 1.0f, over / 12.0f);

    const float target = stored.dynamicRangeDb * amount;

    // **0.05dB刻みで見る。** これを入れないと、無音でも毎回係数を作り直します
    if (std::abs (target - band.dynamicOffsetDb) > 0.05f)
    {
        band.dynamicOffsetDb = target;
        stored.dynamicOffsetDb = target;
        refreshBandCoefficients (bandIndex);
        publishedDynamicOffsetDb[(size_t) bandIndex].store (target);
    }
}

//==============================================================================

void MantaEQProcessor::toMidSide (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i];
        const float r = right[i];

        left[i]  = (l + r) * 0.5f;   // Mid
        right[i] = (l - r) * 0.5f;   // Side
    }
}

void MantaEQProcessor::toLeftRight (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float m = left[i];
        const float s = right[i];

        left[i]  = m + s;
        right[i] = m - s;
    }
}

void MantaEQProcessor::processChunk (float* left, float* right, int numSamples)
{
    const bool stereo = (right != nullptr);
    bool inMidSide = false;

    for (int bandIndex = 0; bandIndex < MantaEQParams::numBands; ++bandIndex)
    {
        const auto& stored = settings[(size_t) bandIndex];
        auto& band = bands[(size_t) bandIndex];

        // Phase 206：**バイパス中のバンドは通さない**（消えてはいない）
        if (! stored.enabled || ! stored.active || band.sections.numSections == 0)
            continue;

        const bool usesRightOnly = (stored.channel == Channel::right || stored.channel == Channel::side);

        // モノで来たときは、右だけ／Sideだけのバンドは何もしない
        if (! stereo && usesRightOnly)
            continue;

        const bool wantsMidSide = stereo
                                    && (stored.channel == Channel::mid || stored.channel == Channel::side);

        if (wantsMidSide != inMidSide)
        {
            if (wantsMidSide)
                toMidSide (left, right, numSamples);
            else
                toLeftRight (left, right, numSamples);

            inMidSide = wantsMidSide;
        }

        //----------------------------------------------------------------------
        // 仕様書4.5：検出（フィルタを通す前の音を見る）
        if (stored.dynamicEnabled)
        {
            const bool bothChannels = stereo && stored.channel == Channel::stereo;
            float envelope = band.envelope;

            for (int i = 0; i < numSamples; ++i)
            {
                float detected;

                if (bothChannels)
                {
                    detected = 0.5f * (std::abs (band.detector[0].process (left[i], band.detectorCoeffs))
                                        + std::abs (band.detector[1].process (right[i], band.detectorCoeffs)));
                }
                else if (usesRightOnly)
                {
                    detected = std::abs (band.detector[1].process (right[i], band.detectorCoeffs));
                }
                else
                {
                    detected = std::abs (band.detector[0].process (left[i], band.detectorCoeffs));
                }

                const float coefficient = detected > envelope ? band.attackCoefficient
                                                               : band.releaseCoefficient;
                envelope = coefficient * (envelope - detected) + detected;
            }

            band.envelope = envelope;
        }

        //----------------------------------------------------------------------
        auto filterInPlace = [&band] (int channel, float* data, int count)
        {
            for (int section = 0; section < band.sections.numSections; ++section)
            {
                const auto& coefficients = band.sections.sections[section];
                auto& filter = band.filters[channel][section];

                for (int i = 0; i < count; ++i)
                    data[i] = filter.process (data[i], coefficients);
            }
        };

        switch (stored.channel)
        {
            case Channel::stereo:
                filterInPlace (0, left, numSamples);

                if (stereo)
                    filterInPlace (1, right, numSamples);

                break;

            case Channel::left:
            case Channel::mid:
                filterInPlace (0, left, numSamples);
                break;

            case Channel::right:
            case Channel::side:
                filterInPlace (1, right, numSamples);
                break;

            case Channel::numChannels:
            default:
                break;
        }
    }

    // **必ず戻すこと。** ここを飛ばすと、後ろのバンドが無いときに
    // M/Sのまま出ていきます（左右が入れ替わったように聴こえます）
    if (inMidSide)
        toLeftRight (left, right, numSamples);
}

void MantaEQProcessor::processSoloChunk (float* left, float* right, int numSamples, int bandIndex)
{
    juce::ignoreUnused (bandIndex);

    for (int section = 0; section < 2; ++section)
    {
        for (int i = 0; i < numSamples; ++i)
            left[i] = soloFilters[0][section].process (left[i], soloCoeffs);

        if (right != nullptr)
            for (int i = 0; i < numSamples; ++i)
                right[i] = soloFilters[1][section].process (right[i], soloCoeffs);
    }
}

//==============================================================================

juce::uint64 MantaEQProcessor::makeStaticSettingsSignature() const
{
    juce::uint64 signature = 0xcbf29ce484222325ULL;

    for (const auto& stored : settings)
    {
        mixIntoSignature (signature, stored.enabled ? 1.0f : 0.0f);
        mixIntoSignature (signature, stored.active ? 1.0f : 0.0f);
        mixIntoSignature (signature, stored.frequency);
        mixIntoSignature (signature, stored.gainDb);
        mixIntoSignature (signature, stored.q);
        mixIntoSignature (signature, (float) (int) stored.shape);
        mixIntoSignature (signature, (float) stored.slopeDbPerOctave);
        mixIntoSignature (signature, (float) (int) stored.channel);
    }

    return signature;
}

void MantaEQProcessor::recalculateAutoGain()
{
    // **ダイナミクスのぶんは入れない。** 入れると毎ブロック計算し直すことになり、
    // しかもオートゲインが音に合わせて揺れます（それは別の機能です）
    const juce::uint64 signature = makeStaticSettingsSignature();

    if (signature == autoGainSignature)
        return;

    autoGainSignature = signature;

    EQFilterDesign::SectionList staticSections[MantaEQParams::numBands];

    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        auto copy = settings[(size_t) band];
        copy.dynamicOffsetDb = 0.0f;

        // **バイパス中のバンドは数えない**（音を通していないので、補正する量にも入らない）
        copy.enabled = copy.enabled && copy.active;

        staticSections[band] = EQFilterDesign::designBand (copy, currentSampleRate);
    }

    // 20Hz〜20kHzを対数で64点。**1kHzのまわりを重く**見ている
    // （耳が敏感なところ。20Hzの上げ下げと1kHzの上げ下げを同じ重みにすると、
    //   低域を切っただけで大きく持ち上がってしまう）
    constexpr int numPoints = 64;
    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (int point = 0; point < numPoints; ++point)
    {
        const float proportion = (float) point / (float) (numPoints - 1);
        const double frequency = MantaEQParams::proportionToFrequency (proportion);

        double magnitude = 1.0;

        for (int band = 0; band < MantaEQParams::numBands; ++band)
            magnitude *= EQFilterDesign::magnitudeAt (staticSections[band], frequency, currentSampleRate);

        const double octaves = std::log2 (frequency / 1000.0) / 2.5;
        const double weight = std::exp (-0.5 * octaves * octaves);

        weightedSum += weight * juce::Decibels::gainToDecibels (magnitude, -60.0);
        totalWeight += weight;
    }

    autoGainDb = totalWeight > 0.0 ? (float) (-weightedSum / totalWeight) : 0.0f;
    autoGainDb = juce::jlimit (-24.0f, 24.0f, autoGainDb);

    publishedAutoGainDb.store (autoGainDb);
}

void MantaEQProcessor::updateLinearPhaseMagnitudes (int soloedBandIndex)
{
    linearPhase.resetMagnitudes();

    const int numBins = linearPhase.getNumBins();

    float* magnitudeFor[4]
    {
        linearPhase.getMagnitudeBuffer (0),   // L
        linearPhase.getMagnitudeBuffer (1),   // R
        linearPhase.getMagnitudeBuffer (2),   // Mid
        linearPhase.getMagnitudeBuffer (3),   // Side
    };

    // 仕様書4.1：**ソロは核へ焼き込む。**
    // Zero Latency側と同じく「そのバンドが触っている範囲だけ」を残しますが、
    // **こちらは同じエンジンを通す**ので、ソロにしても遅れが変わりません
    if (juce::isPositiveAndBelow (soloedBandIndex, MantaEQParams::numBands))
    {
        auto copy = settings[(size_t) soloedBandIndex];
        copy.dynamicOffsetDb = 0.0f;

        switch (copy.shape)
        {
            case Shape::lowCut:
            case Shape::lowShelf:   copy.shape = Shape::highCut; copy.q = 0.707f; copy.slopeDbPerOctave = 24; break;
            case Shape::highCut:
            case Shape::highShelf:  copy.shape = Shape::lowCut;  copy.q = 0.707f; copy.slopeDbPerOctave = 24; break;
            default:                copy.shape = Shape::bandPass; copy.q = juce::jmax (0.5f, copy.q); break;
        }

        const auto sections = EQFilterDesign::designBand (copy, currentSampleRate);

        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto magnitude = (float) EQFilterDesign::magnitudeAt (sections,
                                                                        linearPhase.getBinFrequency (bin),
                                                                        currentSampleRate);
            magnitudeFor[0][bin] = magnitude;
            magnitudeFor[1][bin] = magnitude;
        }

        linearPhase.markKernelsDirty();
        return;
    }

    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        auto copy = settings[(size_t) band];

        if (! copy.enabled || ! copy.active)
            continue;

        // **ダイナミクスのぶんは入れない**（このファイルの冒頭の説明）
        copy.dynamicOffsetDb = 0.0f;

        const auto sections = EQFilterDesign::designBand (copy, currentSampleRate);

        if (sections.numSections == 0)
            continue;

        // このバンドが掛かる先。Stereoだけが2つ（LとR）へ掛かる
        float* first = nullptr;
        float* second = nullptr;

        switch (copy.channel)
        {
            case Channel::stereo: first = magnitudeFor[0]; second = magnitudeFor[1]; break;
            case Channel::left:   first = magnitudeFor[0]; break;
            case Channel::right:  first = magnitudeFor[1]; break;
            case Channel::mid:    first = magnitudeFor[2]; break;
            case Channel::side:   first = magnitudeFor[3]; break;
            default: break;
        }

        if (first == nullptr)
            continue;

        // **振幅は1本ぶんだけ計算して、掛ける先へ配る**（Stereoで2回計算しない）
        for (int bin = 0; bin < numBins; ++bin)
        {
            const auto magnitude = (float) EQFilterDesign::magnitudeAt (sections,
                                                                        linearPhase.getBinFrequency (bin),
                                                                        currentSampleRate);
            first[bin] *= magnitude;

            if (second != nullptr)
                second[bin] *= magnitude;
        }
    }

    linearPhase.markKernelsDirty();
}

void MantaEQProcessor::applyOutputSection (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    const bool useAutoGain = outputAutoGainParam->load() > 0.5f;
    const float totalGainDb = outputGainParam->load() + (useAutoGain ? autoGainDb : 0.0f);

    smoothedOutputGain.setTargetValue (juce::Decibels::decibelsToGain (totalGainDb));

    float panLeft = 1.0f, panRight = 1.0f;
    equalPowerPair (outputPanParam->load(), panLeft, panRight);
    smoothedPanLeft.setTargetValue (panLeft);
    smoothedPanRight.setTargetValue (panRight);

    float midGain = 1.0f, sideGain = 1.0f;
    equalPowerPair (outputMsBalanceParam->load(), midGain, sideGain);
    smoothedMidGain.setTargetValue (midGain);
    smoothedSideGain.setTargetValue (sideGain);

    const float phase = outputPhaseInvertParam->load() > 0.5f ? -1.0f : 1.0f;

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = smoothedOutputGain.getNextValue() * phase;
        const float thisPanLeft = smoothedPanLeft.getNextValue();
        const float thisPanRight = smoothedPanRight.getNextValue();
        const float thisMid = smoothedMidGain.getNextValue();
        const float thisSide = smoothedSideGain.getNextValue();

        if (right != nullptr)
        {
            // 仕様書4.16：M/Sバランス（真ん中で素通り）
            const float mid = (left[i] + right[i]) * 0.5f * thisMid;
            const float side = (left[i] - right[i]) * 0.5f * thisSide;

            left[i]  = (mid + side) * gain * thisPanLeft;
            right[i] = (mid - side) * gain * thisPanRight;
        }
        else
        {
            left[i] *= gain;
        }
    }
}

//==============================================================================

void MantaEQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // 入力の無い出力チャンネルは消しておく（JUCEの決まり）
    for (int channel = getTotalNumInputChannels(); channel < numChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // 仕様書4.8：Pre側（EQを通す前）
    preFifo.push (buffer.getArrayOfReadPointers(), juce::jmin (2, numChannels), numSamples);

    updateSettingsFromParameters();
    recalculateAutoGain();

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    const int solo = soloedBand.load();
    const bool soloing = juce::isPositiveAndBelow (solo, MantaEQParams::numBands)
                           && settings[(size_t) solo].enabled && settings[(size_t) solo].active;

    //--------------------------------------------------------------------------
    // 仕様書4.7：処理モード
    const auto wantedMode = (MantaEQParams::ProcessingMode) juce::jlimit (
        0, (int) MantaEQParams::ProcessingMode::numModes - 1, (int) processingModeParam->load());

    const int resolutionIndex = juce::jlimit (0, LinearPhaseEngine::numResolutionChoices - 1,
                                               (int) processingResolutionParam->load());
    const int wantedIrLength = LinearPhaseEngine::resolutionChoices[resolutionIndex];

    if (wantedMode != currentMode)
    {
        currentMode = wantedMode;

        // **どちらの経路も溜めているものを捨てる。**
        // 切り替えた瞬間に、前のモードの残りが出ます
        for (auto& band : bands)
            band.resetFilters();

        linearPhase.reset();
        linearPhaseSignature = 0;
    }

    if (wantedIrLength != linearPhase.getIrLength())
    {
        linearPhase.setIrLength (wantedIrLength);
        linearPhaseSignature = 0;
    }

    updateLatency (currentMode == MantaEQParams::ProcessingMode::linearPhase
                     ? linearPhase.getLatencySamples() : 0);

    if (currentMode == MantaEQParams::ProcessingMode::linearPhase)
    {
        // ダイナミクスは効かせない（このファイルの冒頭の説明）。
        // **止めたことを画面へも伝える**——カーブに古い動きが残らないように
        for (int band = 0; band < MantaEQParams::numBands; ++band)
        {
            if (bands[(size_t) band].dynamicOffsetDb != 0.0f)
            {
                bands[(size_t) band].dynamicOffsetDb = 0.0f;
                settings[(size_t) band].dynamicOffsetDb = 0.0f;
                publishedDynamicOffsetDb[(size_t) band].store (0.0f);
            }
        }

        juce::uint64 signature = makeStaticSettingsSignature();
        mixIntoSignature (signature, (float) (soloing ? solo + 1 : 0));
        mixIntoSignature (signature, (float) wantedIrLength);

        if (signature != linearPhaseSignature)
        {
            linearPhaseSignature = signature;

            // **振幅を書き込むだけ。** 核の作り直しは、次のフレームの頭で
            // `LinearPhaseEngine`が自分でやります（重い処理をここで抱えない）
            updateLinearPhaseMagnitudes (soloing ? solo : -1);
        }

        linearPhase.process (left, right, numSamples);

        applyOutputSection (buffer);

        postFifo.push (buffer.getArrayOfReadPointers(), juce::jmin (2, numChannels), numSamples);

        for (int channel = 0; channel < 2; ++channel)
        {
            const int source = juce::jmin (channel, numChannels - 1);
            const float peak = buffer.getMagnitude (source, 0, numSamples);

            outputLevelDb[channel].store (juce::Decibels::gainToDecibels (peak, -100.0f));
        }

        return;
    }

    //--------------------------------------------------------------------------
    // 設計書4.1：Zero Latencyパス
    if (soloing)
    {
        // 仕様書4.1：**そのバンドが触っている範囲だけを鳴らす。**
        // 「そのバンドのフィルタだけを掛ける」だと、上げ下げしていないバンドで
        // 何も変わらず、聴き分けの役に立ちません
        const auto& stored = settings[(size_t) solo];

        switch (stored.shape)
        {
            case Shape::lowCut:
            case Shape::lowShelf:
                soloCoeffs = EQFilterDesign::designBand (
                    [&stored]
                    {
                        MantaEQParams::BandSettings copy = stored;
                        copy.shape = Shape::highCut;
                        copy.q = 0.707f;
                        copy.slopeDbPerOctave = 12;
                        return copy;
                    }(), currentSampleRate).sections[0];
                break;

            case Shape::highCut:
            case Shape::highShelf:
                soloCoeffs = EQFilterDesign::designBand (
                    [&stored]
                    {
                        MantaEQParams::BandSettings copy = stored;
                        copy.shape = Shape::lowCut;
                        copy.q = 0.707f;
                        copy.slopeDbPerOctave = 12;
                        return copy;
                    }(), currentSampleRate).sections[0];
                break;

            default:
                soloCoeffs = EQFilterDesign::designBand (
                    [&stored]
                    {
                        MantaEQParams::BandSettings copy = stored;
                        copy.shape = Shape::bandPass;
                        copy.q = juce::jmax (0.5f, stored.q);
                        return copy;
                    }(), currentSampleRate).sections[0];
                break;
        }
    }

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const int count = juce::jmin (chunkSize, numSamples - start);

        if (soloing)
        {
            processSoloChunk (left + start, right != nullptr ? right + start : nullptr, count, solo);
            continue;
        }

        // 32サンプルごとにダイナミクスを見直す（このファイルの冒頭の説明）
        for (int band = 0; band < MantaEQParams::numBands; ++band)
        {
            auto& state = bands[(size_t) band];
            const auto& stored = settings[(size_t) band];

            state.attackCoefficient  = timeConstant (stored.dynamicAttackMs, currentSampleRate);
            state.releaseCoefficient = timeConstant (stored.dynamicReleaseMs, currentSampleRate);

            updateDynamics (band);
        }

        processChunk (left + start, right != nullptr ? right + start : nullptr, count);
    }

    applyOutputSection (buffer);

    // 仕様書4.8：Post側（EQを通した後）
    postFifo.push (buffer.getArrayOfReadPointers(), juce::jmin (2, numChannels), numSamples);

    for (int channel = 0; channel < 2; ++channel)
    {
        const int source = juce::jmin (channel, numChannels - 1);
        const float peak = buffer.getMagnitude (source, 0, numSamples);

        outputLevelDb[channel].store (juce::Decibels::gainToDecibels (peak, -100.0f));
    }
}

//==============================================================================

juce::AudioProcessorEditor* MantaEQProcessor::createEditor()
{
    return new MantaEQEditor (*this);
}

void MantaEQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 設計書6章：APVTSの状態をそのままXMLにする。
    // **`<UI>`の子（アナライザーの見せ方）も一緒に入ります**
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaEQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // **読み込んだら必ず作り直すこと。** 署名を潰しておかないと、
    // オートゲインとLinear Phaseの核が前の設定のままになります
    autoGainSignature = 0;
    linearPhaseSignature = 0;
}

//==============================================================================

MantaEQParams::BandSettings MantaEQProcessor::getBandSettings (int bandIndex) const
{
    MantaEQParams::BandSettings result;

    if (! juce::isPositiveAndBelow (bandIndex, MantaEQParams::numBands))
        return result;

    // **パラメータから組み直します。** 音のスレッドが持っている`settings`を
    // 画面のスレッドから読むと、書いている最中を読むことになります
    const auto& pointers = bandParameters[(size_t) bandIndex];

    result.enabled   = pointers.enabled->load() > 0.5f;
    result.active    = pointers.active->load() > 0.5f;
    result.frequency = pointers.frequency->load();
    result.gainDb    = pointers.gain->load();
    result.q         = pointers.q->load();
    result.shape     = (Shape) juce::jlimit (0, (int) Shape::numShapes - 1, (int) pointers.shape->load());
    result.channel   = (Channel) juce::jlimit (0, (int) Channel::numChannels - 1, (int) pointers.channel->load());

    const int slopeIndex = juce::jlimit (0, MantaEQParams::numSlopeChoices - 1, (int) pointers.slope->load());
    result.slopeDbPerOctave = MantaEQParams::slopeChoicesDbPerOctave[slopeIndex];

    result.dynamicEnabled     = pointers.dynEnabled->load() > 0.5f;
    result.dynamicThresholdDb = pointers.dynThreshold->load();
    result.dynamicRangeDb     = pointers.dynRange->load();
    result.dynamicAttackMs    = pointers.dynAttack->load();
    result.dynamicReleaseMs   = pointers.dynRelease->load();

    // ダイナミクスで動いているぶんだけは、音のスレッドが置いた写しを読む
    result.dynamicOffsetDb = publishedDynamicOffsetDb[(size_t) bandIndex].load();

    return result;
}

MantaEQParams::OutputSettings MantaEQProcessor::getOutputSettings() const
{
    MantaEQParams::OutputSettings result;

    result.gainDb      = outputGainParam->load();
    result.pan         = outputPanParam->load();
    result.msBalance   = outputMsBalanceParam->load();
    result.phaseInvert = outputPhaseInvertParam->load() > 0.5f;
    result.autoGain    = outputAutoGainParam->load() > 0.5f;

    result.mode = (MantaEQParams::ProcessingMode) juce::jlimit (
        0, (int) MantaEQParams::ProcessingMode::numModes - 1, (int) processingModeParam->load());

    const int resolutionIndex = juce::jlimit (0, LinearPhaseEngine::numResolutionChoices - 1,
                                               (int) processingResolutionParam->load());
    result.linearPhaseIrLength = LinearPhaseEngine::resolutionChoices[resolutionIndex];

    return result;
}

bool MantaEQProcessor::hasAnyDynamicBand() const
{
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        const auto& pointers = bandParameters[(size_t) band];

        if (pointers.enabled->load() > 0.5f && pointers.active->load() > 0.5f
             && pointers.dynEnabled->load() > 0.5f)
            return true;
    }

    return false;
}

void MantaEQProcessor::setSoloedBand (int bandIndex)
{
    const int limited = juce::isPositiveAndBelow (bandIndex, MantaEQParams::numBands) ? bandIndex : -1;

    if (soloedBand.exchange (limited) == limited)
        return;

    // **切り替えたら、ソロ用のフィルタは空にしておく。**
    // 前のバンドの状態が残っていると、切り替えた瞬間に短い音が出ます
    for (auto& channelFilters : soloFilters)
        for (auto& filter : channelFilters)
            filter.reset();
}

float MantaEQProcessor::getOutputLevelDb (int channel) const
{
    return outputLevelDb[juce::jlimit (0, 1, channel)].load();
}

int MantaEQProcessor::enableFreeBand (float frequencyHz, float gainDb)
{
    for (int band = 0; band < MantaEQParams::numBands; ++band)
    {
        if (bandParameters[(size_t) band].enabled->load() > 0.5f)
            continue;

        auto setValue = [this] (const juce::String& parameterId, float value)
        {
            if (auto* parameter = apvts.getParameter (parameterId))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        };

        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandFreq), frequencyHz);
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandGain), gainDb);
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandQ), 0.707f);
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandShape), (float) (int) Shape::bell);
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandChannel), (float) (int) Channel::stereo);
        // **バイパスも解いておくこと。** 前に使ったスロットが
        // バイパスのまま残っていると、作った瞬間に効かないバンドができます
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandActive), 1.0f);
        setValue (MantaEQParams::bandParamId (band, MantaEQParams::bandEnabled), 1.0f);

        return band;
    }

    return -1;
}

juce::ValueTree MantaEQProcessor::getUiState()
{
    // **毎回引き直すこと。** `setStateInformation()`が`replaceState()`を通ると、
    // 前に取った`ValueTree`は古い木を指したままになります
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}

//==============================================================================

namespace MantaEQUiState
{
    const juce::Identifier analyserMode    { "analyserMode" };
    const juce::Identifier analyserFrozen  { "analyserFrozen" };
    const juce::Identifier analyserSpeed   { "analyserSpeed" };
    const juce::Identifier analyserTilt    { "analyserTilt" };
    const juce::Identifier analyserFloorDb { "analyserFloorDb" };
    const juce::Identifier curveRangeDb    { "curveRangeDb" };
    const juce::Identifier showKeyboard    { "showKeyboard" };
    const juce::Identifier selectedBand    { "selectedBand" };

    float getFloat (const juce::ValueTree& uiState, const juce::Identifier& id, float defaultValue)
    {
        return uiState.hasProperty (id) ? (float) uiState.getProperty (id) : defaultValue;
    }

    int getInt (const juce::ValueTree& uiState, const juce::Identifier& id, int defaultValue)
    {
        return uiState.hasProperty (id) ? (int) uiState.getProperty (id) : defaultValue;
    }

    bool getBool (const juce::ValueTree& uiState, const juce::Identifier& id, bool defaultValue)
    {
        return uiState.hasProperty (id) ? (bool) uiState.getProperty (id) : defaultValue;
    }
}
