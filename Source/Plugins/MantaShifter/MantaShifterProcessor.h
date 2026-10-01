#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ShifterEngine.h"
#include "ShifterParameters.h"

#include <array>
#include <atomic>

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice の本体（本人の仕様書・設計書（ライト版）。`ShifterEngine`をJUCEで包んだもの）。

    **Manta Studio内部専用形式**です（`../MantaPluginFormat.h`）。挿す・保存する・開き直す・
    オートメーションは外のVST3と同じ道を通ります（9.5）。設計書4章の`IAudioProcessor`／`DawAdapter`は、
    **このクラスがそのまま`DawAdapter`の役**です（DAWのAPIは JUCE の`AudioProcessor`に決まっているため）。

    ### MIDIを受けるエフェクト

    `acceptsMidi()`が真。MIDIモードの音は**インサートの右クリック →「MIDI入力」で選んだMIDIトラック**から来ます
    （8.336で本体に足した配線。`AudioEngine::setInsertMidiSource()`）。

    ### レイテンシー

    `prepareToPlay()`で決まり、**処理中は変わりません**（設計書5章）。48 kHz で 2464 サンプル（51.3 ms）。
*/
class MantaShifterProcessor : public juce::AudioPluginInstance
{
public:
    MantaShifterProcessor();
    ~MantaShifterProcessor() override = default;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
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
    ShifterEngine::Parameters getShifterParameters() const;

    ShifterEngine& getEngine() noexcept { return engine; }
    double getSampleRateForDisplay() const noexcept { return displaySampleRate.load(); }

    /** 8.337：エンジン（0 Spectral／1 PSOLA。`PitchEngineType`。**8.339 から既定は PSOLA**）。**パラメータではなく状態**（`apvts.state`の属性）——
        工場プリセットは全パラメータを既定へ戻すので、パラメータにするとプリセットを選ぶたびにエンジンまで戻ってしまう。
        A/B・Undo には入らない。切り替えると、音が途切れないよう往復の遅れぶん慣らしてから 15 ms で混ぜ替える。 */
    int getEngineType();
    void setEngineType (int type);

    /** 状態の形式（設計書4章「先頭に形式バージョン」。`apvts.state`の属性に持つ）。 */
    static constexpr int stateVersion = 1;

private:
    juce::AudioProcessorValueTreeState apvts;
    ShifterEngine engine;

    struct Pointers
    {
        std::atomic<float>* pitch = nullptr;
        std::atomic<float>* formant = nullptr;
        std::atomic<float>* link = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* key = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* retune = nullptr;
        std::atomic<float>* driveOn = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* midiHold = nullptr;
    } parameters;

    /** このブロックのMIDI（**確保しないよう、固定の長さ**。溢れたぶんは捨てる）。 */
    std::array<ShifterEngine::MidiEvent, 1024> midiScratch {};

    std::atomic<double> displaySampleRate { 48000.0 };

    // エンジン（**音のスレッドは`apvts.state`を読まない**ので、写しを atomic で持つ）
    std::atomic<int> engineType { (int) PitchEngineType::psola };   // 8.339：既定は PSOLA

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaShifterProcessor)
};
