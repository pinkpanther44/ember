#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LimiterEngine.h"
#include "LimiterMeters.h"
#include "MantaLimiterParameters.h"

#include <atomic>

//==============================================================================
/**
    8.333：Manta Limiter／Af Elephant Limiter の本体（リミッター設計書の`LimiterEffect`をJUCEで包んだもの）。

    **Manta Studio内部専用形式**です（`../MantaPluginFormat.h`）。挿す・保存する・開き直す・
    オートメーションは外のVST3と同じ道を通ります（9.5）。

    ### レイテンシー

    先読み（最大5 ms）＋トゥルーピーク補間＋オーバーサンプラーの往復。**変わるたびに申告し直します**
    （仕様書）。`setLatencySamples()`は**音のスレッドから呼ばない**こと——中で`updateHostDisplay()`が走るので、
    `juce::AsyncUpdater`で回します（Manta Compと同じ。8.167）。

    ### ラウドネスの目標

    仕様書：「パラメータに含めず、プラグインの状態として保存」。`apvts.state`の属性`loudnessTarget`に持ちます
    （0＝Off、それ以外はLUFS）。A/B・Undoには入りません（パラメータではないので）。
*/
class MantaLimiterProcessor : public juce::AudioPluginInstance,
                              private juce::AsyncUpdater
{
public:
    MantaLimiterProcessor();
    ~MantaLimiterProcessor() override;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    /** 今のパラメータ（**音のスレッドと画面の両方がこれを読みます**。1.27）。 */
    LimiterEngine::Parameters getLimiterParameters() const;

    LimiterMeters& getMeters() noexcept { return meters; }

    double getSampleRateForDisplay() const noexcept { return displaySampleRate.load(); }
    int getReportedLatencySamples() const noexcept { return publishedLatency.load(); }

    /** ラウドネスの目標（LUFS。0 は Off）。 */
    float getLoudnessTarget();
    void setLoudnessTarget (float lufs);

    /** スクロール表示の時間（5 か 10 秒。状態に保存）。 */
    int getDisplaySeconds();
    void setDisplaySeconds (int seconds);

    /** 仕様書「再生開始で自動リセット」（既定オフ。状態に保存）。 */
    bool getAutoResetOnPlay();
    void setAutoResetOnPlay (bool shouldReset);

private:
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    LimiterEngine engine;
    LimiterMeters meters;

    struct Pointers
    {
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* style = nullptr;
        std::atomic<float>* lookahead = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* autoRelease = nullptr;
        std::atomic<float>* link = nullptr;
        std::atomic<float>* oversampling = nullptr;
        std::atomic<float>* truePeak = nullptr;
        std::atomic<float>* unity = nullptr;
        std::atomic<float>* audition = nullptr;
        std::atomic<float>* dcFilter = nullptr;
        std::atomic<float>* dither = nullptr;
        std::atomic<float>* noiseShaping = nullptr;
    } parameters;

    std::atomic<double> displaySampleRate { 48000.0 };
    std::atomic<int> wantedLatency { 0 };
    std::atomic<int> publishedLatency { 0 };

    // 再生開始で自動リセット（**音のスレッドは`apvts.state`を読まない**ので、写しを atomic で持つ）
    std::atomic<bool> autoResetOnPlay { false };
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaLimiterProcessor)
};
