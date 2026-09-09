#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

//==============================================================================
/**
    仕様書5.2.2「センドトラック」：送り1本ぶんの音量を適用するだけのノード。

    `AudioProcessorGraph`の接続そのものには音量の概念が無く、繋いだ信号は
    そのまま合流する。「トラックごとにセンドレベルを個別設定する」ためには、
    送り元と送り先の間にゲインを1枚挟む必要があるため、この小さなノードを置いている。

        送り元トラック → SendGainProcessor（送り量）→ センドトラック

    パンは持たない（送り元の定位をそのまま保つのが一般的なため）。
*/
class SendGainProcessor : public juce::AudioProcessor
{
public:
    SendGainProcessor();
    ~SendGainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** 送り量（dB）。-60dB以下は無音として扱う。 */
    void setLevelDb (float newLevelDb);

    // AudioProcessorの純粋仮想関数群
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

    static constexpr float silenceThresholdDb = -60.0f;

private:
    std::atomic<float> gain { 0.5f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SendGainProcessor)
};
