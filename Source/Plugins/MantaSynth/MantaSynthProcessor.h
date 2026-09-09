#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "MantaSynthEffects.h"
#include "MantaSynthParameters.h"
#include "MantaSynthVoice.h"

#include <atomic>

//==============================================================================
/**
    Manta Synthesizer 本体（Phase 213）。

    **Manta Studio内部専用形式の3つめ**で、**最初の音源**です
    （`../MantaPluginFormat.h`）。Manta EQ・Manta Compと同じ道を通るので、
    トラックの音源スロットへ挿す・保存する・開き直す・オートメーションは
    **本体側に足すものが1つもありません**。

    ### 音源として名乗るところ

    `MantaPlugins::getEntries()`の`isInstrument`を`true`にしてあります。
    そこから先は本体が面倒を見ます。

    | どこ | 何を見ているか |
    |---|---|
    | ブラウザの「INS」表示 | `PluginDescription::isInstrument`（`BrowserPanel.cpp`） |
    | トラックの音源スロット | 同上（`TrackRackComponent.cpp`。エフェクトは弾かれる） |
    | 入力を繋がない | `numInputChannels = 0`（`MantaPluginFormat.cpp`） |

    ### MIDIの入口は2つ

    1. **トラックから来るMIDI**（ピアノロールに書いた音）
    2. **画面の鍵盤**（`keyboardState`）

    `processBlock()`で合流させています。**鍵盤のほうを先に混ぜること**——
    後から混ぜると、同じブロックの中で順番が入れ替わります。

    ### 出口

    ```
    ボイス8声 → FX1 → FX2 → EQ → 出力ゲイン
    ```

    FX・EQは**声ごとではなくマスター段**です。リバーブを声ごとに掛けると、
    和音を弾いたときに残響が声の数だけ重なります。

    ### 画面へ渡すもの

    メーターの値とオシロスコープの波形は、**書きっぱなしの入れ物**で渡します
    （音のスレッドは書くだけ、画面は読むだけ。ロックを取りません。8.170と同じ）。
*/
class MantaSynthProcessor : public juce::AudioPluginInstance
{
public:
    MantaSynthProcessor();
    ~MantaSynthProcessor() override = default;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    //==========================================================================
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

    //==========================================================================
    // GUIから使うもの

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    /** 画面の鍵盤。**プロセッサ側が持ちます**——エディタは閉じると壊れますが、
        押しっぱなしのまま閉じても音が残らないように、状態はこちらに置きます。 */
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    /** 出力レベル（0..1のピーク）。メーターが読みます。 */
    float getOutputLevel (int channel) const
    {
        return outputLevel[juce::jlimit (0, 1, channel)].load();
    }

    /** オシロスコープが読む波形（モノにまとめたもの）。 */
    static constexpr int scopeSize = 1024;

    const float* getScopeBuffer() const { return scopeBuffer; }
    int getScopeWritePosition() const { return scopeWritePos.load(); }

private:
    void updateVoiceSettings();

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    /** 8声。**増やす前に測ること**——声を増やすとそのぶん重くなります
        （1声が1サンプルずつ回っているので、ここは素直に比例します）。 */
    static constexpr int numVoices = 8;

    MantaGlideSynthesiser synth;

    juce::SmoothedValue<float> outputGain;

    MantaSynthDSP::MultiFX fx1, fx2;
    MantaSynthDSP::ThreeBandEQ eq;

    std::atomic<float> outputLevel[2] { { 0.0f }, { 0.0f } };

    float scopeBuffer[scopeSize] { };
    std::atomic<int> scopeWritePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaSynthProcessor)
};
