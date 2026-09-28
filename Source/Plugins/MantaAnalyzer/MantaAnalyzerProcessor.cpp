#include "MantaAnalyzerProcessor.h"

#include "MantaAnalyzerEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaAnalyzerIdentifier = "manta:analyzer";
}

MantaAnalyzerProcessor::MantaAnalyzerProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MantaAnalyzerProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わります（1.27）
    MantaPlugins::findDescription (mantaAnalyzerIdentifier, description);
}

const juce::String MantaAnalyzerProcessor::getName() const
{
    return Branding::analyzerPluginName;
}

void MantaAnalyzerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使う**。画面は次のタイマーで変わったことに気づき、解析を作り直す（設計書7.3）
    currentSampleRate.store (sampleRate > 0.0 ? sampleRate : 48000.0);
    monoInput.store (getMainBusNumInputChannels() == 1);
}

bool MantaAnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 仕様書7.1：モノとステレオ。入出力は同じ形
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == output;
}

void MantaAnalyzerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    // **音には何もしない**（入力と出力は同じバッファ。ヘッダーの説明）。
    // 画面が閉じていれば、書き写すこともしない
    if (! editorOpen.load (std::memory_order_relaxed))
        return;

    const int channels = buffer.getNumChannels();

    if (channels <= 0)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = channels > 1 ? buffer.getReadPointer (1) : nullptr;

    fifo.push (left, right, buffer.getNumSamples());
}

juce::AudioProcessorEditor* MantaAnalyzerProcessor::createEditor()
{
    return new MantaAnalyzerEditor (*this);
}

void MantaAnalyzerProcessor::setSettings (const AnalyzerSettings& newSettings)
{
    settings = newSettings;
    settings.constrain();
}

void MantaAnalyzerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = settings.toTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaAnalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    // **知らないキーは無視、欠けたキーは初期値**（設計書7.2。`fromTree()`がそうする）
    settings = AnalyzerSettings::fromTree (juce::ValueTree::fromXml (*xml));

    if (onSettingsLoaded != nullptr)
        onSettingsLoaded();
}
