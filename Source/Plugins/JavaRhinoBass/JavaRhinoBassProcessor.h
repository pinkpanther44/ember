#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "JavaRhinoBassParameters.h"
#include "JavaRhinoBassVoice.h"

#include <atomic>

//==============================================================================
/**
    8.257：**Java Rhino Bass 本体**（Phase 265）。
    **内蔵プラグインの7つめ＝3つめの音源**（`../MantaPluginFormat.h`）。

    中身は本人が別に作った`JBass5`（5弦ジャズベースの物理モデリング）で、
    **アンプ段だけ持って来ていません**（本人の指定。`Racco Guitar`と同じ切り分け）。

    ```
    MIDI ─▶[ キースイッチ／音域／レガート ]─▶[ 8声 ]─▶[ パッシブトーン ]─▶[ 出力 ]
              filterMidi()                JavaRhinoBassVoice
    ```

    ### キースイッチは**音域の上**にあります

    5弦ベースは**低音側を使い切る**ので、ギターのように下へ置けません。

    | ノート | 奏法 |
    |---|---|
    | C5 (72) | Finger（指弾き） |
    | C#5 (73) | Pick（ピック） |
    | D5 (74) | Slap（サムピング） |
    | D#5 (75) | Mute（ブリッジミュート） |
    | E5 (76) | Ghost（ゴーストノート） |
    | F5 (77) | Harmonic |

    **演奏できるのはB0(23)〜G4(67)**。あいだのG#4〜B4（68〜71）は**鳴りません**
    （実機に無い音域で、鍵盤では薄く出しています）。

    ### 奏法の書き戻しは、**メッセージスレッドから**

    キースイッチは音のスレッドで拾いますが、`style`は**パラメータ**なので、
    そこで`setValueNotifyingHost()`を呼ぶとホストへの通知が音のスレッドから出ます。

    `juce::AsyncUpdater`にして、**音のスレッドは番号を置くだけ**にしてあります
    （元の`JBass5`はその場で書いていました）。

    ### パッシブトーンは**出口の1次ローパス**

    800Hz〜12kHz。アンプ段ではなく**楽器側のトーンつまみ**なので、
    アンプを持って来ていないこちらにも残してあります。
*/
class JavaRhinoBassProcessor : public juce::AudioPluginInstance,
                                private juce::AsyncUpdater
{
public:
    JavaRhinoBassProcessor();
    ~JavaRhinoBassProcessor() override;

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

    /** Sustainの上限が15秒なので、離した後に伸びるぶんを申告しておく。 */
    double getTailLengthSeconds() const override { return 3.0; }

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

    int getActiveVoiceCount() const { return activeVoiceCount.load(); }

    //==========================================================================
    // 鍵盤の決まり（**画面もここを見ます**。1.27）

    static constexpr int ksFinger   = 72;   // C5
    static constexpr int ksPick     = 73;   // C#5
    static constexpr int ksSlap     = 74;   // D5
    static constexpr int ksMute     = 75;   // D#5
    static constexpr int ksGhost    = 76;   // E5
    static constexpr int ksHarmonic = 77;   // F5

    static constexpr int lowestNote  = BassSpec::lowestNote;    // B0
    static constexpr int highestNote = BassSpec::highestNote;   // G4

    /** 画面の鍵盤に出す右端。**キースイッチまで出します**。 */
    static constexpr int keyboardHighNote = ksHarmonic;

    static bool isKeySwitchNote (int note)
    {
        return note >= ksFinger && note <= ksHarmonic;
    }

    /** 選択肢の番号（`getStyleNames()`の並び）→ キースイッチのノート。 */
    static int keySwitchNoteForChoice (int choice) { return ksFinger + choice; }

    static constexpr int numVoices = 8;

private:
    void handleAsyncUpdate() override;

    void filterMidi (juce::MidiBuffer& midiMessages);
    JavaRhinoBassVoice* findVoicePlaying (int note);

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    juce::Synthesiser synth;

    /** レガートの「いま押さえている連なり」。 */
    struct LegatoChain
    {
        int voiceNote = -1;
        juce::Array<int> held;
    };

    LegatoChain chain;

    juce::int64 sampleClock = 0;
    juce::int64 lastNoteOnClock = -1000000;

    juce::MidiBuffer filteredMidi;

    std::atomic<float>* styleParamValue = nullptr;
    std::atomic<float>* brightnessParam = nullptr;
    std::atomic<float>* sustainParam    = nullptr;
    std::atomic<float>* pluckPosParam   = nullptr;
    std::atomic<float>* hardnessParam   = nullptr;
    std::atomic<float>* attackParam     = nullptr;
    std::atomic<float>* clankParam      = nullptr;
    std::atomic<float>* blendParam      = nullptr;
    std::atomic<float>* toneParam       = nullptr;
    std::atomic<float>* gainParam       = nullptr;
    std::atomic<float>* legatoParam     = nullptr;

    juce::AudioParameterChoice* styleParam = nullptr;   ///< キースイッチからの書き戻し先

    /** キースイッチが置いていく番号。**-1なら何も来ていません**。 */
    std::atomic<int> pendingStyle { -1 };

    // パッシブトーン（出口の1次LPF）
    float toneZ[2] { 0.0f, 0.0f };

    juce::SmoothedValue<float> masterGain { 0.8f };

    double currentFs = 48000.0;

    std::atomic<int> activeVoiceCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JavaRhinoBassProcessor)
};
