#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "RaccoGuitarParameters.h"
#include "RaccoGuitarVoice.h"

#include <algorithm>
#include <atomic>
#include <vector>

//==============================================================================
/**
    8.256：**Racco Guitar 本体**（Phase 264）。**内蔵プラグインの6つめ＝2つめの音源**
    （`../MantaPluginFormat.h`）。

    中身は本人が別に作った`KSGuitar`（Karplus-Strong 拡張版）で、
    **アンプ段だけ持って来ていません**（本人の指定。アンプ／キャビネットは
    外のプラグインでやる）。

    ```
    MIDI ─▶[ キースイッチ／音域／レガート ]─▶[ 8声 ]─▶[ 出力ゲイン ]─▶ 出力
              filterMidi()                    RaccoGuitarVoice
    ```

    ### 鳴らない音がある、のが正しい

    実機の22フレットに合わせて**E2（40）〜D6（86）だけ**鳴ります。
    その下の6つ（A#1〜D#2）は**奏法を切り替えるキースイッチ**で、音は出ません。

    | ノート | 奏法 |
    |---|---|
    | A#1 (34) | ブラッシング |
    | B1 (35) | ピッキングハーモニクス |
    | C2 (36) | ノーマルへ戻す |
    | C#2 (37) | スライド |
    | D2 (38) | パームミュート |
    | D#2 (39) | ハーモニクス |

    **音域外を黙って捨てるのは`filterMidi()`**です。ここを通す前に
    画面の鍵盤を混ぜること——後から混ぜると、鍵盤で押した音だけ素通りします。

    ### レガートはキースイッチ不要

    **弾き方だけで判定**します：前のキーを押さえたまま次を弾くとハンマリング
    （7半音以内／前のノートから60ms以上）、離すとプリング。
    スライド奏法のときは、同じ弾き方で**音程が時間をかけて動きます**。

    ### 画面へ渡すもの

    - `currentArticulation` … いまの奏法（**キースイッチで変わる＝音のスレッドが書く**）
    - `activeVoiceCount` … 鳴っている声の数（画面右上の`n VOICES`）

    どちらも**書きっぱなしの入れ物**です（音のスレッドは書くだけ、画面は読むだけ）。
*/
class RaccoGuitarProcessor : public juce::AudioPluginInstance
{
public:
    RaccoGuitarProcessor();
    ~RaccoGuitarProcessor() override = default;

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

    /** Sustainの上限が10秒なので、**離した後も伸びるぶん**を申告しておく。 */
    double getTailLengthSeconds() const override { return 2.0; }

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

    /** 画面の鍵盤。**プロセッサ側が持ちます**——押しっぱなしで画面を閉じても
        音が残らないように（`MantaSynthProcessor`と同じ）。 */
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    GuitarArticulation getArticulation() const
    {
        return (GuitarArticulation) currentArticulation.load();
    }

    /** 画面のチップから切り替える。**プロパティにも書くので、保存されます。** */
    void setArticulationFromUI (GuitarArticulation articulation);

    int getActiveVoiceCount() const { return activeVoiceCount.load(); }

    //==========================================================================
    // 鍵盤の決まり（**画面もここを見ます**。数字を2箇所に書かない。1.27）

    static constexpr int ksBrushing  = 34;   // A#1
    static constexpr int ksPinchHarm = 35;   // B1
    static constexpr int ksNormal    = 36;   // C2
    static constexpr int ksSlide     = 37;   // C#2
    static constexpr int ksPalmMute  = 38;   // D2
    static constexpr int ksHarmonic  = 39;   // D#2

    static constexpr int lowestNote  = 40;   // E2（6弦開放）
    static constexpr int highestNote = 86;   // D6（1弦22フレット）

    /** 画面の鍵盤の左端。**A#1は黒鍵**なので、1つ下の白鍵から出します
        （左端が黒鍵で始まると鍵盤に見えません）。 */
    static constexpr int keyboardLowNote = 33;   // A1

    static bool isKeySwitchNote (int note)
    {
        return note >= ksBrushing && note <= ksHarmonic;
    }

    /** キースイッチのノート番号（奏法の順）。**画面のチップが引きます**。 */
    static int keySwitchNoteFor (GuitarArticulation articulation);

    static constexpr int numVoices = 8;

private:
    void filterMidi (juce::MidiBuffer& midiMessages);
    RaccoGuitarVoice* findActiveVoice (int note);
    float glideTimeFor (int interval) const;

    //==========================================================================
    /** レガートの「いま押さえている連なり」。 */
    struct LegatoChain
    {
        bool active = false;
        int  voiceNote = -1;
        std::vector<int> held;

        void reset() { active = false; voiceNote = -1; held.clear(); }

        bool contains (int note) const
        {
            return std::find (held.begin(), held.end(), note) != held.end();
        }
    };

    static constexpr int    maxLegatoInterval  = 7;
    static constexpr double legatoGuardMs      = 60.0;
    static constexpr float  hammerGlideMs      = 5.0f;
    static constexpr float  slideMsPerSemitone = 40.0f;
    static constexpr float  slideGlideMaxMs    = 280.0f;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    juce::Synthesiser synth;

    LegatoChain legato;
    juce::int64 sampleClock     = 0;
    juce::int64 lastNoteOnClock = -1000000;

    juce::MidiBuffer filteredMidi;

    std::atomic<float>* brightnessParam = nullptr;
    std::atomic<float>* sustainParam    = nullptr;
    std::atomic<float>* pickPosParam    = nullptr;
    std::atomic<float>* pickupSelParam  = nullptr;
    std::atomic<float>* hardnessParam   = nullptr;
    std::atomic<float>* attackParam     = nullptr;
    std::atomic<float>* gainParam       = nullptr;

    juce::SmoothedValue<float> gainSmoothed;

    std::atomic<int> currentArticulation { (int) GuitarArticulation::normal };
    std::atomic<int> activeVoiceCount { 0 };

    /** `apvts.state`に書く奏法のプロパティ名。**画面も同じ名前で読みます**。 */
    static const juce::Identifier& articulationProperty();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RaccoGuitarProcessor)
};
