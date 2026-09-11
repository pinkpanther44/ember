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

    ### 入っているもの（ディレイ設計書8章のPhase 1）

    | | 中身 |
    |---|---|
    | **Phase 1（いまここ）** | Single Echo、Character＝Digital Cleanのみ、Time／Feedback／Mix／テンポシンク |

    **先の段階のものは入っていません。** キャラクター、フィルター、
    モジュレーション、ダッキング、マルチタップ、デュアルエンジン、
    リバース、ディフュージョン——全部これからです。

    ### テンポシンク

    `getPlayHead()`から引きます。**Phase 238でホスト側に足したもの**です（8.205）——
    それまで`graph.setPlayHead()`を誰も呼んでおらず、
    **プラグインからは`nullptr`しか見えませんでした。**

    プレイヘッドが取れないとき（ホストが渡さない／同期OFF）は、
    **`Time`のつまみの値をそのまま使います。** 黙って無音にはしません。

    ### レイテンシー

    **報告しません。** ディレイ自体は意図した遅れなので補正の対象外です
    （設計書7章）。オーバーサンプリングはPhase 1では使っていません。
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
    };

    Pointers parameters;

    std::atomic<double> syncBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayProcessor)
};
