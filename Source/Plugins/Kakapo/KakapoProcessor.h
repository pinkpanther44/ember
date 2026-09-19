#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "KakapoLeadVoice.h"
#include "KakapoNoteHistory.h"
#include "KakapoParameters.h"
#include "KakapoScale.h"

#include <array>
#include <atomic>

//==============================================================================
/**
    8.292：**Kakapo 本体**（Phase 285／本人の仕様書・設計書）。
    **内蔵プラグインの9つめ＝5つめの音源**（`../MantaPluginFormat.h`）。

    弾いた音から**いまのスケール**を当てて出します。メジャー1つ・マイナー1つ
    （仕様書1章）。**音も鳴ります**——外部音源へMIDIを回さずに確かめられるよう、
    単一音色のモノフォニックリードを積んであります（仕様書2章）。

    ```
    MIDI ─┬─▶ NoteHistory（直近N音／N秒）─▶ ヒストグラム ─▶ ScaleDetector ─┐
          │                                                                  ├─▶ 画面（Snapshot）
          ├─▶ LeadVoice（モノフォニック。last-note priority）─▶ 出力        ┘
          │
          └─▶ MIDI Thru（**この本体では受け手がいません**。`KakapoParameters.h`）
    ```

    ### 判定は音のスレッドで、画面へは写しで渡します

    設計書13章のとおりです。`AnalysisSnapshot`は**ただの値の束**なので、
    `juce::CriticalSection`で短く囲って写すだけ——音のスレッドが待つのは
    数十ナノ秒です。**画面側は20Hzで読みに来ます。**

    > **ロックフリーの三重バッファは、まだ要りません**（設計書13章も同じ判断）。
    > 24候補の採点は12×12回の足し算で、1ブロックあたり数マイクロ秒です。

    ### `Reset`はパラメータではありません

    押した瞬間に履歴を消すだけで、**覚えておく値がありません**（設計書12章）。
    画面から`requestReset()`を呼び、**音のスレッドが次のブロックの頭で**消します
    ——その場で`std::deque`を触ると、音のスレッドと殴り合います。
*/
class KakapoProcessor : public juce::AudioPluginInstance
{
public:
    //==========================================================================
    /** 画面へ渡す写し（設計書13章）。**値だけ**にしてあること。 */
    struct AnalysisSnapshot
    {
        std::array<bool, 128> active {};    ///< いま鳴っている音（鍵盤の濃いほう）
        std::array<bool, 128> recent {};    ///< 控えに残っている音（薄いほう）
        std::array<float, 12> histogram {}; ///< 12音の使用量（**そのままの重み**）

        kakapo::Result result;

        int noteCount = 0;
    };

    //==========================================================================
    KakapoProcessor();
    ~KakapoProcessor() override = default;

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

    /** **本物のMIDIスルーです**（`KakapoParameters.h`。この本体では受け手がいません）。 */
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }

    double getTailLengthSeconds() const override { return 0.2; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // 画面から使うもの

    juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }

    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    /** いまの判定。**画面はこれだけを見ます**（設計書13章）。 */
    AnalysisSnapshot readSnapshot() const;

    /** 履歴と判定を消す（画面の`RESET`）。**その場では消しません**（クラスの説明）。 */
    void requestReset() { resetRequested.store (true); }

private:
    //==========================================================================
    void handleMidiMessage (const juce::MidiMessage& message, int samplePosition);
    void renderSegment (float* left, float* right, int offset, int numSamples);
    void publishSnapshot();

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    double currentFs = 48000.0;
    std::int64_t sampleClock = 0;

    kakapo::NoteHistory history;
    kakapo::LeadVoice voice;

    juce::SmoothedValue<float> outputGain { 0.7f };

    /** 音を出さないときも、右chの実体が要ります（モノ出力のとき）。 */
    juce::AudioBuffer<float> monoRight;

    /** スルーするMIDIを組み立てる先（**入ってきた列をそのまま使い回さない**）。 */
    juce::MidiBuffer passThrough;

    //==========================================================================
    std::atomic<float>* holdModeParam   = nullptr;
    std::atomic<float>* holdLengthParam = nullptr;
    std::atomic<float>* volumeParam     = nullptr;
    std::atomic<float>* midiThruParam   = nullptr;

    std::atomic<bool> resetRequested { false };

    //==========================================================================
    mutable juce::CriticalSection snapshotLock;
    AnalysisSnapshot snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KakapoProcessor)
};
