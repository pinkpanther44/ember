#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CompressorEngine.h"
#include "MantaCompParameters.h"

#include <array>
#include <atomic>

//==============================================================================
/**
    Manta Comp 本体（コンプ設計書1章の`PluginProcessor`）。

    **Manta Studio内部専用形式**です（`../MantaPluginFormat.h`）。
    Manta EQと同じく`juce::AudioPluginFormat`に載せてあるので、
    挿す・保存する・開き直す・オートメーションが**外のVST3と同じ道**を通ります。

    ### 入っているもの（コンプ仕様書のフェーズ1〜6）

    | | 中身 |
    |---|---|
    | Phase 1・2 | Threshold / Ratio / Knee / Attack / Release / Input Gain / Makeup、GRメーター |
    | Phase 3 | Stereo Link、In/Outメーター、伝達特性グラフ |
    | Phase 4 | Look Ahead、Mix（パラレルコンプ） |
    | Phase 5 | サイドチェイン（Low Cut / High Cut / Filter / Listen / Swap）＋**外部入力** |
    | Phase 6 | Auto（Attack/Release自動）、Adaptive、Auto Gain |

    ### 外部サイドチェイン

    **2つめの入力バスを持っています。** Manta Studioは
    `AudioEngine::insertSupportsSidechain()`が`getBusCount(true) > 1`を見るだけなので、
    **本体側に足すものはありません**——Consoleのスロットを右クリックすると
    「サイドチェインのソース」が出ます。

    バスは**既定で無効**にしてあります（8.111：作った直後に一律で有効にすると、
    バス構成の交渉で落ちるプラグインが実在した）。
    `AudioEngine::ensureSidechainBusEnabled()`が、
    **ソースを実際に割り当てたときだけ**有効にします。

    ### レイテンシー

    仕様書2-3：**Look AheadがONのときだけ**遅れます（5ms）。
    `setLatencySamples()`は**音のスレッドから呼ばない**こと——
    中で`updateHostDisplay()`が走るので、`juce::AsyncUpdater`で回します（8.167）。
*/
class MantaCompProcessor : public juce::AudioPluginInstance,
                            private juce::AsyncUpdater
{
public:
    MantaCompProcessor();
    ~MantaCompProcessor() override;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    //==========================================================================
    // GUIから使うもの

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    /** つまみの値をひとまとめにして返す（**グラフもこれを描きます**。1.27）。 */
    CompressorEngine::Settings getSettings() const;

    const CompressorEngine& getEngine() const { return engine; }

    /** 外部サイドチェインが実際に繋がっているか（GUIの表示用）。 */
    bool isSidechainConnected() const { return sidechainConnected.load(); }

    double getSampleRateForDisplay() const { return displaySampleRate.load(); }
    int getReportedLatencySamples() const { return publishedLatency.load(); }

    /** 仕様書2-6：SwapはAPVTSのパラメータではなく、**2つの値を入れ替えるだけ**。
        「入れ替えた状態」という別の値を持たないので、保存するものが増えません。 */
    void swapSidechainFrequencies();

    juce::ValueTree getUiState();

private:
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    CompressorEngine engine;

    /** パラメータの読み出し口。**文字列で引くのは作るときの1回だけ**
        （毎ブロック引き直すと、それだけで無視できない時間になります）。 */
    struct Pointers
    {
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* ratio = nullptr;
        std::atomic<float>* knee = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* autoEnvelope = nullptr;
        std::atomic<float>* adaptiveRelease = nullptr;
        std::atomic<float>* inputGain = nullptr;
        std::atomic<float>* makeupGain = nullptr;
        std::atomic<float>* autoGain = nullptr;
        std::atomic<float>* lookAhead = nullptr;
        std::atomic<float>* stereoLink = nullptr;
        std::atomic<float>* scFilterOn = nullptr;
        std::atomic<float>* scLowCut = nullptr;
        std::atomic<float>* scHighCut = nullptr;
        std::atomic<float>* scListen = nullptr;
        std::atomic<float>* mix = nullptr;
    };

    Pointers parameters;

    std::atomic<double> displaySampleRate { 44100.0 };
    std::atomic<bool> sidechainConnected { false };
    std::atomic<int> wantedLatency { 0 };
    std::atomic<int> publishedLatency { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaCompProcessor)
};
