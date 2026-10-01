#include "MantaShifterProcessor.h"

#include "MantaShifterEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaShifterIdentifier = "manta:shifter";

    const juce::Identifier stateVersionId { "stateVersion" };
    const juce::Identifier engineId { "engine" };   // 8.337
}

MantaShifterProcessor::MantaShifterProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaShifter", ShifterParams::createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    parameters.pitch    = get (ShifterParams::pitch);
    parameters.formant  = get (ShifterParams::formant);
    parameters.link     = get (ShifterParams::link);
    parameters.mode     = get (ShifterParams::mode);
    parameters.key      = get (ShifterParams::key);
    parameters.scale    = get (ShifterParams::scale);
    parameters.retune   = get (ShifterParams::retune);
    parameters.driveOn  = get (ShifterParams::driveOn);
    parameters.drive    = get (ShifterParams::drive);
    parameters.mix      = get (ShifterParams::mix);
    parameters.output   = get (ShifterParams::output);
    parameters.bypass   = get (ShifterParams::bypass);
    parameters.midiHold = get (ShifterParams::midiHold);

    apvts.state.setProperty (stateVersionId, stateVersion, nullptr);
}

void MantaShifterProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わります（1.27）
    MantaPlugins::findDescription (mantaShifterIdentifier, description);
}

const juce::String MantaShifterProcessor::getName() const
{
    return Branding::shifterPluginName;
}

ShifterEngine::Parameters MantaShifterProcessor::getShifterParameters() const
{
    ShifterEngine::Parameters p;

    p.pitch    = parameters.pitch->load();
    p.formant  = parameters.formant->load();
    p.link     = parameters.link->load() > 0.5f;
    p.mode     = juce::roundToInt (parameters.mode->load());
    p.key      = juce::roundToInt (parameters.key->load());
    p.scale    = juce::roundToInt (parameters.scale->load());
    p.retuneMs = parameters.retune->load();
    p.driveOn  = parameters.driveOn->load() > 0.5f;
    p.drive    = parameters.drive->load();
    p.mix      = parameters.mix->load();
    p.outputDb = parameters.output->load();
    p.bypass   = parameters.bypass->load() > 0.5f;
    p.midiHold = parameters.midiHold->load() > 0.5f;
    p.engine   = engineType.load (std::memory_order_relaxed);
    return p;
}

void MantaShifterProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 9.5：**渡された値を使う**（デバイスへ訊きに行かない）。何度でも来るので、毎回作り直す
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const int channels = juce::jmax (1, getMainBusNumInputChannels());

    displaySampleRate.store (rate);
    engine.prepare (rate, juce::jmax (1, samplesPerBlock), channels);

    // レイテンシーは**ここで決まって、もう変わらない**（設計書5章）。メッセージスレッドなのでその場で申告
    setLatencySamples (engine.getLatencySamples());
}

bool MantaShifterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 仕様書5章：モノラル入出力（ステレオ入力なら L+R で検出して、両chへ同じシフト）
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void MantaShifterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    int numEvents = 0;

    for (const auto metadata : midiMessages)
    {
        if (numEvents >= (int) midiScratch.size())
            break;   // 溢れたぶんは捨てる（確保しない）

        if (metadata.numBytes < 2)
            continue;

        auto& e = midiScratch[(size_t) numEvents++];
        e.sampleOffset = juce::jlimit (0, numSamples - 1, metadata.samplePosition);
        e.status = metadata.data[0];
        e.data1 = metadata.data[1];
        e.data2 = metadata.numBytes > 2 ? metadata.data[2] : 0;
    }

    engine.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, getShifterParameters(),
                    midiScratch.data(), numEvents);
}

juce::AudioProcessorEditor* MantaShifterProcessor::createEditor()
{
    return new MantaShifterEditor (*this);
}

void MantaShifterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (stateVersionId, stateVersion, nullptr);

    // 8.339：エンジンは**いつも書く**（既定を変えても、保存したプロジェクトの音が変わらないように）
    state.setProperty (engineId, getEngineType(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaShifterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    // 設計書4章：**知らないIDは無視、欠けたIDは既定値**。`replaceState()`がそうします
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    apvts.state.setProperty (stateVersionId, stateVersion, nullptr);
    engineType.store (getEngineType());
}

int MantaShifterProcessor::getEngineType()
{
    // 8.339：**既定は PSOLA**（本人の指定）。属性が無い状態（初めて挿したとき）は PSOLA
    return juce::jlimit (0, 1, (int) apvts.state.getProperty (engineId, (int) PitchEngineType::psola));
}

void MantaShifterProcessor::setEngineType (int type)
{
    type = juce::jlimit (0, 1, type);
    apvts.state.setProperty (engineId, type, nullptr);
    engineType.store (type);
}
