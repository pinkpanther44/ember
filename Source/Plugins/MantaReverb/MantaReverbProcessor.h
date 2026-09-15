#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaReverbEngine.h"
#include "MantaReverbParameters.h"
#include "MantaReverbRouting.h"

#include <array>
#include <atomic>

//==============================================================================
/** 8.249：画面だけが覚えていること（Phase 259）。

    **音には関係しません**ので、パラメータにはしません——
    オートメーションや A/B の対象になるべきものではないからです。
    `apvts.state`の`UI`の子として、**プロジェクトと一緒に残ります。**

    ディレイの`MantaDelayUiState`と同じ作りです（8.217）。 */
namespace MantaReverbUiState
{
    /** つまみが映しているエンジン（0＝A、1＝B）。 */
    extern const juce::Identifier selectedEngine;
}

//==============================================================================
/**
    Manta Reverb 本体（リバーブ設計書2章の`PluginProcessor`）。

    **Manta Studio内部専用形式**です（`../MantaPluginFormat.h`）。
    EQ・コンプ・シンセ・ディレイと同じく`juce::AudioPluginFormat`に載せてあるので、
    挿す・保存する・開き直す・オートメーションが**外のVST3と同じ道**を通ります。

    ### 入っているもの（リバーブ設計書8章の段階表）

    | | 中身 |
    |---|---|
    | Phase 1 | Room 1系統（初期反射＋4×4 FDN） |
    | Phase 2 | Plate（Dattorro型）、Hall |
    | **Phase 3（いまここ）** | **Ambience、Shape／Spread** |
    | Phase 4 | Twin Delays、Panorama |
    | Phase 5 | デュアルエンジンとルーティング |
    | Phase 6 | Random Hall、Saturation |

    **`Random Hall HD`は非採用**（設計書4-4：ライト版でも★4相当が下限）。

    ### 1サンプルずつ回します

    FDNは**左右が1本の輪でつながっている**ので、チャンネルごとにブロックを
    回す形では書けません（`MantaReverbEngine`の説明）。
    ディレイがPhase 5bで同じ形にしています（8.218）。

    ### テンポシンクはありません

    リバーブの減衰時間は**部屋の性質**であって拍ではないので、
    同期する相手がありません（ディレイとはここが違います）。

    ### レイテンシー

    **報告しません。** プリディレイは意図した遅れなので補正の対象外です。
    オーバーサンプリングはまだ使っていません（仕様書7章：サチュレーション使用時のみ検討）。
*/
class MantaReverbProcessor : public juce::AudioPluginInstance
{
public:
    MantaReverbProcessor();
    ~MantaReverbProcessor() override;

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

    /** 響きが消えるまで鳴り続けます。**上限で答えておきます**——
        0を返すと、ホストが「もう何も出ない」と判断して切ってよいことになります。 */
    double getTailLengthSeconds() const override
    {
        return MantaReverbParams::maxDecaySeconds + MantaReverbParams::maxPredelayMs / 1000.0;
    }

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

    /** 8.240：**画面が描くための設定**（Phase 254）。

        `processBlock()`が使うのと**同じ`buildEngineSettings()`**を通します（1.27）——
        別々に組み立てると、**描いている響きと鳴っている響きがずれます**
        （ディレイの`getTapPattern()`と同じ形。8.214）。 */
    MantaReverbEngine::Settings getDisplaySettings (int engine) const;

    /** 8.249：いまのルーティング（Phase 259）。**画面のグレーアウトが見ます。** */
    MantaReverbRouting::Mode getRoutingMode() const;

    juce::ValueTree getUiState();

private:
    /** 1エンジンぶんの設定を組み立てる。**音の側と画面の側で同じものを使います**（1.27）。 */
    MantaReverbEngine::Settings buildEngineSettings (int engine) const;

    juce::AudioProcessorValueTreeState apvts;

    /** 8.249：**2つになりました**（Phase 259）。

        **`Single`のときも両方`prepare()`します**——途中でモードを変えたときに、
        **Bだけ用意されていない**状態を作らないため（ディレイと同じ。8.217）。 */
    std::array<MantaReverbEngine, (size_t) MantaReverbParams::numEngines> engines;

    /** パラメータの読み出し口。**文字列で引くのは作るときの1回だけ**
        （毎ブロック引き直すと、それだけで無視できない時間になります）。 */
    struct EnginePointers
    {
        std::atomic<float>* predelay = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* size = nullptr;
        std::atomic<float>* diffusion = nullptr;
        std::atomic<float>* highDampFreq = nullptr;
        std::atomic<float>* highDampAmount = nullptr;
        std::atomic<float>* lowDampFreq = nullptr;
        std::atomic<float>* lowDampAmount = nullptr;
        std::atomic<float>* earlyLevel = nullptr;
        std::atomic<float>* width = nullptr;

        std::atomic<float>* algorithm = nullptr;   // 8.243（Phase 255）

        // 8.246：Phase 3（Phase 256）
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* spread = nullptr;

        // 8.247：Phase 4a（Phase 257）
        std::atomic<float>* twinTime = nullptr;
        std::atomic<float>* twinFeedback = nullptr;
        std::atomic<float>* twinCross = nullptr;

        // 8.248：Phase 4b（Phase 258）
        std::atomic<float>* panMonoSum = nullptr;
        std::atomic<float>* panInvertRight = nullptr;
        std::atomic<float>* panSwap = nullptr;

        std::atomic<float>* engineLevel = nullptr;   // 8.249（Phase 259）

        // 8.250〜8.251：Phase 6（Phase 260）
        std::atomic<float>* modulation = nullptr;
        std::atomic<float>* saturation = nullptr;
    };

    struct GlobalPointers
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* routingMode = nullptr;   // 8.249（Phase 259）
    };

    GlobalPointers parameters;
    std::array<EnginePointers, (size_t) MantaReverbParams::numEngines> engineParameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaReverbProcessor)
};
