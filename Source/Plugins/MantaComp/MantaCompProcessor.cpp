#include "MantaCompProcessor.h"

#include "MantaCompEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaCompIdentifier = "manta:comp";
}

//==============================================================================

MantaCompProcessor::MantaCompProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   // **既定では無効**（`MantaCompProcessor.h`の説明）
                                   .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaComp", MantaCompParams::createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    parameters.threshold       = get (MantaCompParams::threshold);
    parameters.ratio           = get (MantaCompParams::ratio);
    parameters.knee            = get (MantaCompParams::knee);
    parameters.attack          = get (MantaCompParams::attack);
    parameters.release         = get (MantaCompParams::release);
    parameters.autoEnvelope    = get (MantaCompParams::autoEnvelope);
    parameters.adaptiveRelease = get (MantaCompParams::adaptiveRelease);
    parameters.inputGain       = get (MantaCompParams::inputGain);
    parameters.makeupGain      = get (MantaCompParams::makeupGain);
    parameters.autoGain        = get (MantaCompParams::autoGain);
    parameters.lookAhead       = get (MantaCompParams::lookAhead);
    parameters.stereoLink      = get (MantaCompParams::stereoLink);
    parameters.scFilterOn      = get (MantaCompParams::scFilterOn);
    parameters.scLowCut        = get (MantaCompParams::scLowCut);
    parameters.scHighCut       = get (MantaCompParams::scHighCut);
    parameters.scListen        = get (MantaCompParams::scListen);
    parameters.mix             = get (MantaCompParams::mix);
}

MantaCompProcessor::~MantaCompProcessor()
{
    // **溜まっている呼び出しを取り消してから壊すこと**（8.167）
    cancelPendingUpdate();
}

void MantaCompProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (mantaCompIdentifier, description);
}

const juce::String MantaCompProcessor::getName() const
{
    return Branding::compPluginName;
}

//==============================================================================

void MantaCompProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない。8.153の書き出し）
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    displaySampleRate.store (rate);
    engine.prepare (rate);

    // **ここだけは、その場で申告してよい**（音はまだ回っていない）
    const int latency = getSettings().lookAhead ? engine.getLookAheadSamples() : 0;

    wantedLatency.store (latency);
    publishedLatency.store (latency);
    setLatencySamples (latency);
}

void MantaCompProcessor::releaseResources()
{
    engine.reset();
}

bool MantaCompProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();
    const auto& mainInput = layouts.getMainInputChannelSet();

    if (mainOutput != juce::AudioChannelSet::mono() && mainOutput != juce::AudioChannelSet::stereo())
        return false;

    if (mainInput != mainOutput)
        return false;

    // サイドチェインは**無効・モノ・ステレオ**のどれでもよい。
    // ここで断ると`ensureSidechainBusEnabled()`が有効にできません（8.111）
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled()
             && sidechain != juce::AudioChannelSet::mono()
             && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================

CompressorEngine::Settings MantaCompProcessor::getSettings() const
{
    CompressorEngine::Settings settings;

    settings.thresholdDb = parameters.threshold->load();
    settings.ratio       = parameters.ratio->load();
    settings.kneeDb      = parameters.knee->load();

    settings.attackMs        = parameters.attack->load();
    settings.releaseMs       = parameters.release->load();
    settings.autoEnvelope    = parameters.autoEnvelope->load() > 0.5f;
    settings.adaptiveRelease = parameters.adaptiveRelease->load() > 0.5f;

    settings.inputGainDb = parameters.inputGain->load();
    settings.makeupDb    = parameters.makeupGain->load();
    settings.autoGain    = parameters.autoGain->load() > 0.5f;

    settings.lookAhead  = parameters.lookAhead->load() > 0.5f;
    settings.stereoLink = parameters.stereoLink->load() > 0.5f;

    settings.sidechainFilterOn = parameters.scFilterOn->load() > 0.5f;
    settings.lowCutHz          = parameters.scLowCut->load();
    settings.highCutHz         = parameters.scHighCut->load();
    settings.listen            = parameters.scListen->load() > 0.5f;

    settings.mix = juce::jlimit (0.0f, 1.0f, parameters.mix->load() * 0.01f);

    return settings;
}

void MantaCompProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    const auto settings = getSettings();

    // 外部サイドチェイン。**繋がっていなければ自分の音を見ます**（仕様書1章）
    const float* sidechainPointers[2] { nullptr, nullptr };
    int numSidechainChannels = 0;

    if (getBusCount (true) > 1)
    {
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sidechainBuffer = getBusBuffer (buffer, true, 1);

            numSidechainChannels = juce::jmin (2, sidechainBuffer.getNumChannels());

            for (int ch = 0; ch < numSidechainChannels; ++ch)
                sidechainPointers[ch] = sidechainBuffer.getReadPointer (ch);
        }
    }

    sidechainConnected.store (numSidechainChannels > 0);

    // **本線だけを渡すこと。** `buffer`にはサイドチェインのチャンネルも
    // 入っているので、そのまま渡すとサイドチェインまで圧縮して書き戻します
    auto mainBuffer = getBusBuffer (buffer, false, 0);

    engine.process (mainBuffer, numSidechainChannels > 0 ? sidechainPointers : nullptr,
                     numSidechainChannels, settings);

    // 仕様書2-3：Look Aheadを切り替えたらレイテンシーも変わる。
    // **申告はメッセージスレッドから**（8.167）
    const int latency = engine.getLatencySamples();

    if (wantedLatency.exchange (latency) != latency)
        triggerAsyncUpdate();
}

void MantaCompProcessor::handleAsyncUpdate()
{
    const int latency = wantedLatency.load();

    if (publishedLatency.exchange (latency) == latency)
        return;

    // 9.5：**レイテンシは正直に申告する**（呼ばないとグラフがずらしてくれません）
    setLatencySamples (latency);
}

//==============================================================================

void MantaCompProcessor::swapSidechainFrequencies()
{
    auto* lowCut = apvts.getParameter (MantaCompParams::scLowCut);
    auto* highCut = apvts.getParameter (MantaCompParams::scHighCut);

    if (lowCut == nullptr || highCut == nullptr)
        return;

    // **範囲が違うので、値をそのまま入れ替えると端で丸められます**
    // （Low Cutは20〜2000、High Cutは200〜20000）。
    // それで構いません——入れ替えた結果が範囲の外なら、
    // そもそもそこには置けない値です
    const float lowValue = lowCut->convertFrom0to1 (lowCut->getValue());
    const float highValue = highCut->convertFrom0to1 (highCut->getValue());

    lowCut->beginChangeGesture();
    lowCut->setValueNotifyingHost (lowCut->convertTo0to1 (highValue));
    lowCut->endChangeGesture();

    highCut->beginChangeGesture();
    highCut->setValueNotifyingHost (highCut->convertTo0to1 (lowValue));
    highCut->endChangeGesture();
}

juce::AudioProcessorEditor* MantaCompProcessor::createEditor()
{
    return new MantaCompEditor (*this);
}

void MantaCompProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaCompProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::ValueTree MantaCompProcessor::getUiState()
{
    // **毎回引き直すこと。** `replaceState()`を通ると、前に取った`ValueTree`は
    // 古い木を指したままになります
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}
