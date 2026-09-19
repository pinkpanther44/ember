#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "OrangutanDrumsDSP.h"
#include "OrangutanDrumsFX.h"
#include "OrangutanDrumsParameters.h"

#include <array>
#include <atomic>

//==============================================================================
/**
    8.288：**Orangutan Drums 本体**（Phase 281）。
    **内蔵プラグインの8つめ＝4つめの音源**（`../MantaPluginFormat.h`）。

    中身は本人が別に作った`MAGAZINE`（16パッドのドラムシンセ）で、
    **シークエンサだけ持って来ていません**（本人の指定）。

    ```
    MIDI 36〜51 ─▶ パッド16個（エンジン別のボイス）─┬─▶ マスター段 ─▶ 出力（0番のバス）
                                                     │      ※ SENDのぶんはリバーブへ
                                                     └─▶ DIRECTのパッドは 1〜16番のバスへ
    ```

    ### 8.289：**ベースゾーンを廃止しました**（Phase 282／本人の指定）

    52〜96のノートで`SUB 808`のパッドを音程で弾ける仕掛けがありましたが、
    **本人の指定で外しました**。`SUB 808`は**パッドのエンジンとしては残ります**
    （36〜51で叩けます）。

    **36〜51の外のノートは、もう何も鳴らしません。**

    ### 8.289：**パラアウトを戻しました**（Phase 282／本人の要望）

    `MAGAZINE`にあった16本のパラアウトです。**本体が受け皿を持っています**
    ——**ドラムアウトトラック**（`TrackType::DrumOut`）が、音源の1番以降のバスを
    別トラックで受けます（8.144）。

    | | |
    |---|---|
    | バス0 | メイン。**マスター段を通ります** |
    | バス1〜16 | パッド1〜16の専用出力。**マスター段を通りません**（素のまま） |

    **1〜16番は既定で無効**です。本体は「受け皿のあるトラックだけ」バスを有効にして
    音源を作り直すので（8.144）、**普段は2ch出力のまま**動きます。

    > **無効なバスへ向けたパッドは、MAINへ落とします。** 受け皿を作る前に
    > `DIRECT`へ切り替えても音が消えないように——**消えるより分かりやすい**からです。

    **`DIRECT`のパッドは、リバーブへも送りません**（`SEND`のぶんは捨てます）。
    マスター段を通らないので、返りだけがメインに乗ると辻褄が合いません。

    ### パッドはそれぞれモノフォニック

    実機と同じで、**同じパッドを叩き直すと前の音は止まります**
    （`DrumVoice::trigger()`が状態を全部作り直します）。
    16パッドで最大16声。**声の取り合いは起きません。**

    ### チョークグループ

    `CL HAT`と`OP HAT`は同じ群（`engineChokeGroup()`）。クローズを叩くと
    オープンが**4ミリ秒で閉じます**。**どのパッドに割り当てても効きます**
    ——群はパッド番号ではなく**エンジン**で決まるためです。

    ### 画面へ渡すもの

    エディタは閉じると壊れるので、**表示に必要な状態はこちら側**に置きます
    （`getPadActivity()`・`getActiveVoiceCount()`）。
    選んでいるパッドと、開いている頁は`apvts.state`の`<UI>`へ入れます——
    **プロジェクトに保存され、開き直すと戻ります**（`MantaEQProcessor`と同じ道）。
*/
class OrangutanDrumsProcessor : public juce::AudioPluginInstance
{
public:
    OrangutanDrumsProcessor();
    ~OrangutanDrumsProcessor() override = default;

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** ドラムエディタにパッドの名前を出すためのもの（ホストが訊きに来ます）。

        **エンジンを変えても、訊き直さないホストがあります。** */
    std::optional<juce::String> getNameForMidiNoteNumber (int note, int midiChannel) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** `CYMBAL`と`SUB 808`のいちばん長い減衰が5秒、そこへリバーブが乗ります。 */
    double getTailLengthSeconds() const override { return 6.0; }

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

    /** 画面の`<UI>`。**毎回引き直すこと**——`setStateInformation()`が木を差し替えると、
        前に取ったものは古い木を指します（`MantaEQProcessor::getUiState()`と同じ）。 */
    juce::ValueTree getUiState();

    /** パッドの発光（0〜1）。画面がタイマーで読みます。 */
    float getPadActivity (int pad) const noexcept;

    /** そのパッドに割り当ててあるエンジン（`orangutan::Engine`）。 */
    int getPadEngine (int pad) const noexcept;

    /** そのパッドが`DIRECT`か（**パラアウトのバスが有効かどうかは見ません**）。 */
    bool isPadDirectOut (int pad) const noexcept;

    /** 1〜16番のバスが有効か（＝本体にドラムアウトトラックがあるか）。
        画面が「いまは届いていません」と出すのに使います。 */
    bool areDirectOutsAvailable() const noexcept { return directOutsEnabled.load(); }

    /** いま鳴っている声の数。 */
    int getActiveVoiceCount() const noexcept { return activeVoiceCount.load(); }

    /** 画面のパッドを押したとき。**鍵盤と同じ道を通します**
        （`keyboardState`→`processNextMidiBuffer()`）——発音・チョーク・発光が
        MIDIで来たときと1本にまとまります。 */
    void triggerPadFromUI (int pad, float velocity);

    //==========================================================================
    // 鍵盤の決まり（**画面もここを見ます**。1.27）

    static constexpr int numPads = OrangutanDrumsParams::numPads;

    /** パッド1＝MIDI 36。**MPC／MASCHINEと同じ**です（C4＝60の流儀ではC2）。 */
    static constexpr int padBaseNote = 36;
    static constexpr int padHighNote = padBaseNote + numPads - 1;   // 51

    static bool isPadNote (int note) { return note >= padBaseNote && note <= padHighNote; }

private:
    //==========================================================================
    static BusesProperties makeBusesProperties();

    void cacheParameterPointers();

    orangutan::VoiceParams readPadParams (int pad) const noexcept;

    void handleMidiMessage (const juce::MidiMessage& message);
    void triggerPad (int pad, float velocity);
    void renderSegment (int offset, int numSamples);

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    double currentFs = 48000.0;

    orangutan::DrumVoice voices[numPads];
    orangutan::MasterFX masterFX;

    /** リバーブへの送り（0=L 1=R）と、**捨て先**（2=L 3=R。DIRECTのパッドの送り）。 */
    juce::AudioBuffer<float> scratch;

    /** モノ出力のときに右chの実体が要ります（同じポインタへ足すと左右が混ざります）。 */
    juce::AudioBuffer<float> monoRight;

    juce::SmoothedValue<float> masterGain { 0.8f };

    //==========================================================================
    // パッドごとの行き先。**ブロックの先頭で1回だけ決めます**

    float* padDestinationL[numPads] { nullptr };
    float* padDestinationR[numPads] { nullptr };
    float* padSendL[numPads] { nullptr };
    float* padSendR[numPads] { nullptr };

    float* mainL = nullptr;
    float* mainR = nullptr;

    //==========================================================================
    // パラメータのポインタ（`processBlock`で`getRawParameterValue()`を呼ばない）

    struct PadPointers
    {
        std::atomic<float>* engine = nullptr;
        std::atomic<float>* tune   = nullptr;
        std::atomic<float>* decay  = nullptr;
        std::atomic<float>* tone   = nullptr;
        std::atomic<float>* snap   = nullptr;
        std::atomic<float>* level  = nullptr;
        std::atomic<float>* pan    = nullptr;
        std::atomic<float>* send   = nullptr;
        std::atomic<float>* out    = nullptr;
    };

    std::array<PadPointers, (size_t) numPads> padPointers;

    std::atomic<float>* driveParam  = nullptr;
    std::atomic<float>* glueParam   = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* sizeParam   = nullptr;
    std::atomic<float>* dampParam   = nullptr;
    std::atomic<float>* volumeParam = nullptr;

    //==========================================================================
    std::array<std::atomic<float>, (size_t) numPads> padActivity;
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<bool> directOutsEnabled { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrangutanDrumsProcessor)
};
