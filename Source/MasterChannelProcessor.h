#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Transport.h"
#include "TrackChannelProcessor.h" // AutomationSample（オートメーションの点。Phase 20）

//==============================================================================
/**
    仕様書5.7「マスターチャンネル」・設計書1.3のmasterBusに対応するプロセッサ。

    Phase 12bで新設。すべての音（クリップ再生・音源・テストトーン・入力モニター）は
    このノードを通ってから出力へ向かう。ここでマスター音量を適用し、
    最終的な出力レベルを計測する。

    レベル値はオーディオスレッドが書き、UIスレッドが読むためstd::atomicで受け渡す
    （RecorderProcessorの入力メーターと同じ考え方）。
*/
class MasterChannelProcessor : public juce::AudioProcessor
{
public:
    explicit MasterChannelProcessor (Transport& transportToUse);
    ~MasterChannelProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** マスター音量（dB）。-60dB以下は無音として扱う。 */
    void setVolumeDb (float newVolumeDb);

    /** 仕様書5.6：マスター音量のオートメーション（Phase 20）。
        トラックと同じく、再生前にメッセージスレッドが平坦な配列として渡す
        （HANDOVER 1.12「オーディオスレッドからValueTreeを読まない」）。 */
    void setVolumeAutomation (std::vector<TrackChannelProcessor::AutomationSample> points);

    /** 仕様書5.6：記録中はオートメーションを迂回する（TrackChannelProcessorと同じ理由。Phase 20）。 */
    void setAutomationBypassed (bool shouldBypass) { automationBypassed.store (shouldBypass); }

    /** 出力レベル（0.0〜1.0）。UIのメーター表示用。 */
    float getOutputLevel (int channel) const;

    // AudioProcessorの純粋仮想関数群（プラグインではないので簡易実装でよい）
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
    static constexpr int numMeterChannels = 2;
    static constexpr float meterDecayPerBlock = 0.75f;

    Transport& transport;
    std::atomic<float> gain { 1.0f };
    std::atomic<float> outputLevels[numMeterChannels] { { 0.0f }, { 0.0f } };

    // 仕様書5.6：マスター音量のオートメーション（Phase 20）
    std::vector<TrackChannelProcessor::AutomationSample> volumeAutomation;
    juce::SpinLock automationLock;
    std::atomic<float> currentVolumeDb { 0.0f }; // オートメーションが無いときの基準値
    std::atomic<bool> automationBypassed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterChannelProcessor)
};
