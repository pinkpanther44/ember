#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaDelayEngine.h"
#include "MantaDelayParameters.h"

#include <atomic>

//==============================================================================
/**
    Manta Delay 本体（ディレイ設計書2章の`PluginProcessor`）。

    **Manta Studio内部専用形式**です（`../MantaPluginFormat.h`）。
    EQ・コンプ・シンセと同じく`juce::AudioPluginFormat`に載せてあるので、
    挿す・保存する・開き直す・オートメーションが**外のVST3と同じ道**を通ります。

    ### 入っているもの（ディレイ設計書8章の段階表）

    | | 中身 |
    |---|---|
    | Phase 1 | Single Echo、Time／Feedback／Mix／テンポシンク |
    | Phase 2 | キャラクター（Digital Clean・Analog BBD・Tape Echo・Lo-Fi） |
    | **Phase 3（いまここ）** | フィードバック内フィルター、LFOモジュレーション、ダッキング |

    **先の段階のものは入っていません。** マルチタップ、デュアルエンジン、
    リバース、ディフュージョン——ここからです。

    ### テンポシンク

    `getPlayHead()`から引きます。**Phase 238でホスト側に足したもの**です（8.205）——
    それまで`graph.setPlayHead()`を誰も呼んでおらず、
    **プラグインからは`nullptr`しか見えませんでした。**

    プレイヘッドが取れないとき（ホストが渡さない／同期OFF）は、
    **`Time`のつまみの値をそのまま使います。** 黙って無音にはしません。

    ### レイテンシー

    **報告しません。** ディレイ自体は意図した遅れなので補正の対象外です
    （設計書7章）。オーバーサンプリングはまだ使っていません。
*/
class MantaDelayProcessor : public juce::AudioPluginInstance
{
public:
    MantaDelayProcessor();
    ~MantaDelayProcessor() override;

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

    /** フィードバックが残っているあいだは鳴り続けます。

        **上限で答えておきます**（設計書7章の最大ディレイタイム×数回ぶん）。
        0を返すと、ホストが「もう何も出ない」と判断して切ってよいことになります。 */
    double getTailLengthSeconds() const override { return MantaDelayParams::maxDelayMs / 1000.0 * 4.0; }

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

    /** いま鳴っているディレイタイム（秒）。**ディスプレイはこれを描きます。**

        つまみの値ではなく**寄せている最中の値**です（`MantaDelayEngine`）——
        つまみと違う値が出ているあいだは、実際にそう鳴っています。 */
    double getCurrentDelaySeconds() const { return engine.getDisplayDelaySeconds(); }

    /** 同期の基準にしているテンポ（BPM）。**取れていないときは0**。

        画面が「ホストからテンポが来ていない」ことを出せるようにしてあります——
        **黙って別の値で鳴らすより、出ていないと言うほうがよい**（8.161と同じ考え）。 */
    double getSyncBpm() const { return syncBpm.load(); }

    /** 8.212：ダッキングがいまどれだけ絞っているか（0〜1。Phase 242）。

        画面の細い帯がこれを出します——**絞られたぶんは「音が小さい」だけ**なので、
        AttackとReleaseを回しても、見えないと何が起きているか読めません。 */
    float getDuckReduction() const { return engine.getDisplayDuckReduction(); }

    juce::ValueTree getUiState();

private:
    /** そのブロックで使うディレイタイム（秒）を決める。

        **換算は`MantaDelayParams::getQuarterNotes()`ただ1つ**を通します（1.27）——
        画面も同じ関数を使うので、**描いている位置と鳴っている位置がずれません。** */
    double resolveDelaySeconds();

    juce::AudioProcessorValueTreeState apvts;
    MantaDelayEngine engine;

    /** パラメータの読み出し口。**文字列で引くのは作るときの1回だけ**
        （毎ブロック引き直すと、それだけで無視できない時間になります）。 */
    struct Pointers
    {
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* syncDivision = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;

        // 8.208：Phase 2（Phase 240）
        std::atomic<float>* character = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* tone = nullptr;
        std::atomic<float>* wowRate = nullptr;
        std::atomic<float>* wowDepth = nullptr;
        std::atomic<float>* flutterRate = nullptr;
        std::atomic<float>* flutterDepth = nullptr;

        // 8.210〜8.212：Phase 3（Phase 242）
        std::atomic<float>* filterType = nullptr;
        std::atomic<float>* filterFreq = nullptr;
        std::atomic<float>* filterQ = nullptr;
        std::atomic<float>* filterGain = nullptr;
        std::atomic<float>* filterPost = nullptr;

        std::atomic<float>* lfoShape = nullptr;
        std::atomic<float>* lfoRate = nullptr;
        std::atomic<float>* lfoDepth = nullptr;

        std::atomic<float>* duckAmount = nullptr;
        std::atomic<float>* duckAttack = nullptr;
        std::atomic<float>* duckRelease = nullptr;
    };

    Pointers parameters;

    std::atomic<double> syncBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayProcessor)
};
