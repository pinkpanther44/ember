#include "SandboxedPluginProcessor.h"

#include "SandboxedPluginEditor.h"

//==============================================================================

SandboxedPluginProcessor::SandboxedParameter::SandboxedParameter (
        PluginSandboxHost& hostToUse, int indexToUse,
        PluginSandboxHost::LoadedPluginInfo::Parameter info)
    : host (hostToUse), index (indexToUse), details (std::move (info)),
      value (details.currentValue)
{
}

void SandboxedPluginProcessor::SandboxedParameter::setValue (float newValue)
{
    value.store (newValue);

    // **次のブロックで子へ渡します**（ここでIPCを送ると、
    // オートメーションのたびにメッセージが飛びます）
    host.queueParameterChange (index, newValue);
}

juce::String SandboxedPluginProcessor::SandboxedParameter::getName (int maximumStringLength) const
{
    return details.name.substring (0, maximumStringLength);
}

int SandboxedPluginProcessor::SandboxedParameter::getNumSteps() const
{
    return details.numSteps > 0 ? details.numSteps
                                 : juce::AudioProcessorParameter::getNumSteps();
}

juce::String SandboxedPluginProcessor::SandboxedParameter::getText (float normalised,
                                                                     int maximumStringLength) const
{
    // **子へ訊きに行きません。** 表示のために毎回プロセスをまたぐと、
    // つまみを回すたびに往復が発生します。数字で出します
    return juce::String (normalised, 3).substring (0, maximumStringLength);
}

float SandboxedPluginProcessor::SandboxedParameter::getValueForText (const juce::String& text) const
{
    return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
}

//==============================================================================

std::unique_ptr<SandboxedPluginProcessor>
    SandboxedPluginProcessor::create (const juce::PluginDescription& description,
                                       double sampleRate, int blockSize,
                                       juce::String& errorMessage)
{
    PluginSandboxHost::LoadedPluginInfo info;

    // **先に中身を読んでから**プロキシを作ります（読めなかったら何も作らない）。
    // `host`はプロキシが持つので、いったんここで動かして結果を受け取ります
    auto processor = std::unique_ptr<SandboxedPluginProcessor> (
        new SandboxedPluginProcessor (description, {}));

    if (! processor->host.loadPlugin (description, sampleRate, blockSize, info, errorMessage))
        return nullptr;

    processor->pluginInfo = std::move (info);

    // 子と同じ数・同じ名前のパラメータを立てる（クラスの説明）
    for (int i = 0; i < (int) processor->pluginInfo.parameters.size(); ++i)
    {
        auto parameter = std::make_unique<SandboxedParameter> (
            processor->host, i, processor->pluginInfo.parameters[(size_t) i]);

        processor->sandboxedParameters.push_back (parameter.get());
        processor->addHostedParameter (std::move (parameter));
    }

    processor->incomingChanges.resize (SandboxIPC::maxParamChanges);

    processor->host.onConnectionLost = [proxy = processor.get()]
    {
        proxy->crashed.store (true);
        proxy->triggerAsyncUpdate();
    };

    return processor;
}

SandboxedPluginProcessor::SandboxedPluginProcessor (const juce::PluginDescription& description,
                                                     PluginSandboxHost::LoadedPluginInfo info)
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      pluginDescription (description),
      pluginInfo (std::move (info))
{
}

SandboxedPluginProcessor::~SandboxedPluginProcessor()
{
    cancelPendingUpdate();

    // **繋ぎ先より先に消えないこと**（1.5）。子を止めてから、パラメータが消えます
    host.onConnectionLost = nullptr;
    host.shutdownWorker();
}

void SandboxedPluginProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **本物と同じ説明を返します**（クラスの説明）。
    // ここで別のものを返すと、保存したプロジェクトが開き直せません
    description = pluginDescription;
}

//==============================================================================

void SandboxedPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    host.prepare (sampleRate, samplesPerBlock);

    // **1ブロックぶんの遅れを足して申告すること**（`SandboxIPC.h`）。
    // これを忘れると、サンドボックスへ回したトラックだけ1ブロック遅れます
    setLatencySamples (host.getPluginLatencySamples() + samplesPerBlock);
}

void SandboxedPluginProcessor::releaseResources()
{
    host.release();
}

bool SandboxedPluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // **いまはステレオだけ**（共有メモリの`maxChannels`が2）。
    // サイドチェインやマルチ出力はPhase 268の範囲外です
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
             || layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled());
}

void SandboxedPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    juce::AudioPlayHead::PositionInfo position;
    bool havePosition = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto current = playHead->getPosition())
        {
            position = *current;
            havePosition = true;
        }
    }

    if (! host.processBlock (buffer, midiMessages, havePosition ? &position : nullptr,
                              pluginInfo.producesMidi))
    {
        // **子が居ない／落ちた：入力をそのまま通す**（クラスの説明）。
        // 無音にするより、少なくとも元の音が残るほうが作業への影響が小さい
        return;
    }

    // 子のGUIで動いたパラメータを取り込む（画面へ反映するのはメッセージスレッド）
    if (host.readParameterChanges (incomingChanges.data(), (int) incomingChanges.size()) > 0)
        triggerAsyncUpdate();
}

void SandboxedPluginProcessor::handleAsyncUpdate()
{
    if (crashed.exchange (false))
    {
        // **画面が先**。はめ込んだ窓はもう消えているので、
        // そのままにすると「固まった」ように見えます（8.262）
        if (activeSandboxEditor != nullptr)
            activeSandboxEditor->sandboxCrashed();

        if (onSandboxCrashed != nullptr)
            onSandboxCrashed();

        return;
    }

    // 子から来た値を、こちら側のパラメータへ写して画面へ知らせる
    for (const auto& change : incomingChanges)
    {
        if (! juce::isPositiveAndBelow (change.index, (int) sandboxedParameters.size()))
            continue;

        auto* parameter = sandboxedParameters[(size_t) change.index];

        if (std::abs (parameter->getValue() - change.value) < 1.0e-6f)
            continue;

        parameter->setValueFromWorker (change.value);
        parameter->sendValueChangedMessageToListeners (change.value);
    }
}

//==============================================================================

const juce::String SandboxedPluginProcessor::getName() const
{
    return pluginInfo.name.isNotEmpty() ? pluginInfo.name : pluginDescription.name;
}

double SandboxedPluginProcessor::getTailLengthSeconds() const { return pluginInfo.tailSeconds; }
bool SandboxedPluginProcessor::acceptsMidi() const            { return pluginInfo.acceptsMidi; }
bool SandboxedPluginProcessor::producesMidi() const           { return pluginInfo.producesMidi; }

//==============================================================================

void SandboxedPluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    destData = host.getPluginState();
}

void SandboxedPluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    host.setPluginState (juce::MemoryBlock (data, (size_t) sizeInBytes));
}

//==============================================================================
// 8.262：本来のGUI（Phase 269）

juce::AudioProcessorEditor* SandboxedPluginProcessor::createEditor()
{
    if (! pluginInfo.hasEditor || crashed.load() || ! host.isAlive())
        return nullptr;   // **本体が`GenericAudioProcessorEditor`へ落としてくれます**

    return new SandboxedPluginEditor (*this, host);
}

void SandboxedPluginProcessor::setActiveSandboxEditor (SandboxedPluginEditor* editor, bool embedded)
{
    activeSandboxEditor = editor;
    editorIsEmbedded = editor != nullptr && embedded;
}
