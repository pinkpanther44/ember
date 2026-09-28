#include "MantaLimiterProcessor.h"

#include "MantaLimiterEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaLimiterIdentifier = "manta:limiter";

    const juce::Identifier loudnessTargetId { "loudnessTarget" };
    const juce::Identifier displaySecondsId { "displaySeconds" };
    const juce::Identifier autoResetId { "autoResetOnPlay" };
}

MantaLimiterProcessor::MantaLimiterProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaLimiter", MantaLimiterParams::createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    parameters.gain         = get (MantaLimiterParams::gain);
    parameters.output       = get (MantaLimiterParams::output);
    parameters.style        = get (MantaLimiterParams::style);
    parameters.lookahead    = get (MantaLimiterParams::lookahead);
    parameters.attack       = get (MantaLimiterParams::attack);
    parameters.release      = get (MantaLimiterParams::release);
    parameters.autoRelease  = get (MantaLimiterParams::autoRelease);
    parameters.link         = get (MantaLimiterParams::link);
    parameters.oversampling = get (MantaLimiterParams::oversampling);
    parameters.truePeak     = get (MantaLimiterParams::truePeak);
    parameters.unity        = get (MantaLimiterParams::unity);
    parameters.audition     = get (MantaLimiterParams::audition);
    parameters.dcFilter     = get (MantaLimiterParams::dcFilter);
    parameters.dither       = get (MantaLimiterParams::dither);
    parameters.noiseShaping = get (MantaLimiterParams::noiseShaping);
}

MantaLimiterProcessor::~MantaLimiterProcessor()
{
    cancelPendingUpdate();
}

void MantaLimiterProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わります（1.27）
    MantaPlugins::findDescription (mantaLimiterIdentifier, description);
}

const juce::String MantaLimiterProcessor::getName() const
{
    return Branding::limiterPluginName;
}

LimiterEngine::Parameters MantaLimiterProcessor::getLimiterParameters() const
{
    LimiterEngine::Parameters p;

    p.gainDb        = parameters.gain->load();
    p.outputDb      = parameters.output->load();
    p.style         = juce::roundToInt (parameters.style->load());
    p.lookaheadMs   = parameters.lookahead->load();
    p.attackPercent = parameters.attack->load();
    p.releaseMs     = parameters.release->load();
    p.autoRelease   = parameters.autoRelease->load() > 0.5f;
    p.linkPercent   = parameters.link->load();
    p.oversampling  = juce::roundToInt (parameters.oversampling->load());
    p.truePeak      = parameters.truePeak->load() > 0.5f;
    p.unity         = parameters.unity->load() > 0.5f;
    p.audition      = parameters.audition->load() > 0.5f;
    p.dcFilter      = parameters.dcFilter->load() > 0.5f;
    p.dither        = juce::roundToInt (parameters.dither->load());
    p.noiseShaping  = juce::roundToInt (parameters.noiseShaping->load());
    return p;
}

void MantaLimiterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 9.5：**渡された値を使う**（デバイスへ訊きに行かない）。何度でも来るので、毎回作り直す
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int channels = juce::jmax (1, getMainBusNumInputChannels());

    displaySampleRate.store (rate);

    engine.prepare (rate, juce::jmax (1, samplesPerBlock), channels);
    engine.reconfigureNow (getLimiterParameters());
    meters.prepare (rate, channels);

    // ここはメッセージスレッドなので、その場で申告してよい
    const int latency = engine.getLatencySamples();
    wantedLatency.store (latency);
    publishedLatency.store (latency);
    setLatencySamples (latency);
}

bool MantaLimiterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 仕様書：モノラル／ステレオ。入出力は同じ形
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void MantaLimiterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int total = buffer.getNumSamples();

    if (numChannels <= 0 || total <= 0)
        return;

    // 仕様書「再生開始で自動リセット」：止まっていた再生が始まった瞬間に計測をやり直す
    if (autoResetOnPlay.load (std::memory_order_relaxed))
    {
        bool playing = false;

        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                playing = position->getIsPlaying();

        if (playing && ! wasPlaying)
            meters.requestReset();

        wasPlaying = playing;
    }

    const auto p = getLimiterParameters();
    float* const* channels = buffer.getArrayOfWritePointers();

    // **ブロックが準備より長く来ても、分けて通す**（書き出しなどで起こり得る）
    const int chunk = juce::jmax (1, getBlockSize());

    for (int start = 0; start < total; start += chunk)
    {
        const int n = juce::jmin (chunk, total - start);
        float* pointers[2] { channels[0] + start, numChannels > 1 ? channels[1] + start : nullptr };

        engine.process (pointers, numChannels, n, p);

        const float* input[2] { engine.getInputTrace (0), engine.getInputTrace (numChannels > 1 ? 1 : 0) };
        const float* output[2] { pointers[0], numChannels > 1 ? pointers[1] : pointers[0] };
        const float* truePeak[2] { engine.getOutputTruePeakTrace (0), engine.getOutputTruePeakTrace (numChannels > 1 ? 1 : 0) };

        meters.push (input, output, engine.getGainReductionTrace(), truePeak, numChannels, n, p.outputDb);
    }

    // 組み直しでレイテンシーが変わったら、**メッセージスレッドで**申告し直す（8.167）
    if (engine.takeLatencyChanged())
    {
        wantedLatency.store (engine.getLatencySamples());
        triggerAsyncUpdate();
    }
}

void MantaLimiterProcessor::handleAsyncUpdate()
{
    const int latency = wantedLatency.load();

    if (publishedLatency.exchange (latency) == latency)
        return;

    // 9.5：**レイテンシーは正直に申告する**（呼ばないとグラフがずらしてくれません）
    setLatencySamples (latency);
}

float MantaLimiterProcessor::getLoudnessTarget()
{
    return (float) (double) apvts.state.getProperty (loudnessTargetId, 0.0);
}

void MantaLimiterProcessor::setLoudnessTarget (float lufs)
{
    apvts.state.setProperty (loudnessTargetId, lufs, nullptr);
}

int MantaLimiterProcessor::getDisplaySeconds()
{
    return (int) apvts.state.getProperty (displaySecondsId, 5) == 10 ? 10 : 5;
}

void MantaLimiterProcessor::setDisplaySeconds (int seconds)
{
    apvts.state.setProperty (displaySecondsId, seconds == 10 ? 10 : 5, nullptr);
}

bool MantaLimiterProcessor::getAutoResetOnPlay()
{
    return (bool) apvts.state.getProperty (autoResetId, false);
}

void MantaLimiterProcessor::setAutoResetOnPlay (bool shouldReset)
{
    apvts.state.setProperty (autoResetId, shouldReset, nullptr);
    autoResetOnPlay.store (shouldReset);
}

juce::AudioProcessorEditor* MantaLimiterProcessor::createEditor()
{
    return new MantaLimiterEditor (*this);
}

void MantaLimiterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaLimiterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // 設計書：**知らないIDは無視、欠けたIDは既定値**。`replaceState()`がそうします
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    autoResetOnPlay.store (getAutoResetOnPlay());
}
