#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginSandboxHost.h"

//==============================================================================
/**
    設計書1.2・3.3の`PluginHostProxy`に相当するクラス。

    AudioProcessorGraphから見ると普通のAudioProcessorだが、実際の音声処理は
    別プロセス（サンドボックス）のプラグインが行う。処理は共有メモリ経由で
    受け渡される（設計書3.4）。

    上位のグラフはこのクラスが「ネイティブロードされたプラグイン」なのか
    「サンドボックス経由」なのかを意識しなくてよい、というのが設計上の狙い。

    子プロセスが応答しない／クラッシュした場合は、そのブロックを無加工のまま
    通す（バイパス動作）。音は途切れるかもしれないが、DAW本体は動き続ける。
*/
class SandboxedPluginProcessor : public juce::AudioProcessor
{
public:
    explicit SandboxedPluginProcessor (PluginSandboxHost& hostToUse);

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    const juce::String getName() const override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    PluginSandboxHost& sandboxHost;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxedPluginProcessor)
};
