#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaDelayEngine.h"
#include "MantaDelayParameters.h"
#include "MantaDelayRouting.h"
#include "MantaDelayTaps.h"

#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/** 8.214：画面だけが覚えていること（Phase 243）。

    **音には関係しません**ので、パラメータにはしません——
    オートメーションや A/B の対象になるべきものではないからです。
    `apvts.state`の`UI`の子として、**プロジェクトと一緒に残ります。**

    Manta EQの`MantaEQUiState`と同じ作りです（あちらは選んでいるバンド）。 */
namespace MantaDelayUiState
{
    /** つまみが指しているタップ（0起点）。 */
    extern const juce::Identifier selectedTap;

    /** 8.217：つまみが映しているエンジン（0＝A、1＝B。Phase 244）。 */
    extern const juce::Identifier selectedEngine;
}

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
    | Phase 3 | フィードバック内フィルター、LFOモジュレーション、ダッキング |
    | Phase 4 | マルチタップ（最大8本。`MantaDelayTaps.h`） |
    | Phase 5a | デュアルエンジンとルーティング（`MantaDelayRouting.h`） |
    | **Phase 5b（いまここ）** | **Ping-Pongとクロスフィードバック** |

    **残っているのはPhase 6だけ**です（リバース、ディフュージョン、Freeze、UIの仕上げ）。

    ### 8.218：1サンプルずつ回します（Phase 245）

    クロスフィードバックは**Aの戻りがBの線へ入る**ので、
    **ブロックごとにエンジンを回す形では間に合いません。**
    `processBlock()`が`beginSample()`／`readChannel()`／`writeChannel()`を
    交互に呼びます（`MantaDelayEngine`）。

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
    double getCurrentDelaySeconds (int engine) const
    {
        return engines[(size_t) juce::jlimit (0, MantaDelayParams::numEngines - 1, engine)]
                   .getDisplayDelaySeconds();
    }

    /** 同期の基準にしているテンポ（BPM）。**取れていないときは0**。

        画面が「ホストからテンポが来ていない」ことを出せるようにしてあります——
        **黙って別の値で鳴らすより、出ていないと言うほうがよい**（8.161と同じ考え）。 */
    double getSyncBpm() const { return syncBpm.load(); }

    /** 8.217：いまのルーティング（Phase 244）。画面のグレーアウトが見ます。 */
    MantaDelayRouting::Mode getRoutingMode() const
    {
        return (MantaDelayRouting::Mode)
                   juce::jlimit (0, MantaDelayRouting::getModeCount() - 1,
                                  juce::roundToInt (parameters.routingMode->load()));
    }

    /** 8.212：ダッキングがいまどれだけ絞っているか（0〜1。Phase 242）。

        画面の細い帯がこれを出します——**絞られたぶんは「音が小さい」だけ**なので、
        AttackとReleaseを回しても、見えないと何が起きているか読めません。 */
    float getDuckReduction (int engine) const
    {
        return engines[(size_t) juce::jlimit (0, MantaDelayParams::numEngines - 1, engine)]
                   .getDisplayDuckReduction();
    }

    /** 8.214：いまのタップの並び（Phase 243）。ディスプレイとタップ帯が描きます。

        **`processBlock()`が使うのと同じ`buildTapPattern()`**を通します（1.27）。 */
    MantaDelayTaps::Pattern getTapPattern (int engine) const { return buildTapPattern (engine); }

    juce::ValueTree getUiState();

private:
    /** そのブロックで使うディレイタイム（秒）を決める。

        **換算は`MantaDelayParams::getQuarterNotes()`ただ1つ**を通します（1.27）——
        画面も同じ関数を使うので、**描いている位置と鳴っている位置がずれません。** */
    double resolveDelaySeconds (int engine);

    /** 8.217：1エンジンぶんの設定を組み立てる（Phase 244）。 */
    MantaDelayEngine::Settings buildEngineSettings (int engine);

    juce::AudioProcessorValueTreeState apvts;

    /** 8.217：エンジンは2つ（Phase 244）。**Singleのときも両方`prepare()`します**——
        途中でモードを変えたときに、**Bだけ用意されていない**状態を作らないため。 */
    std::array<MantaDelayEngine, (size_t) MantaDelayParams::numEngines> engines;

    /** パラメータの読み出し口。**文字列で引くのは作るときの1回だけ**
        （毎ブロック引き直すと、それだけで無視できない時間になります）。 */
    struct EnginePointers
    {
        std::atomic<float>* timeMs = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* syncDivision = nullptr;
        std::atomic<float>* feedback = nullptr;

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

        // 8.214：Phase 4（Phase 243）
        std::atomic<float>* tapCount = nullptr;

        struct Tap
        {
            std::atomic<float>* step = nullptr;
            std::atomic<float>* level = nullptr;
            std::atomic<float>* pan = nullptr;
        };

        std::array<Tap, (size_t) MantaDelayTaps::maxTaps> taps;

        // 8.217：Phase 5（Phase 244）
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
    };

    /** エンジン共通のもの。**`mix`と`outputGain`はここ**（8.217）。 */
    struct GlobalPointers
    {
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* outputGain = nullptr;
        std::atomic<float>* routingMode = nullptr;
        std::atomic<float>* crossFeedback = nullptr;   // 8.218（Phase 245）
    };

    GlobalPointers parameters;
    std::array<EnginePointers, (size_t) MantaDelayParams::numEngines> engineParameters;

    /** 8.214：つまみの値からタップの並びを組み立てる（Phase 243）。

        **音の側と画面の側で同じものを使います**（1.27）——
        別々に組み立てると、**描いている並びと鳴っている並びがずれます。** */
    MantaDelayTaps::Pattern buildTapPattern (int engine) const;

    //==========================================================================
    /** 8.217：ルーティングで使う場所（Phase 244）。

        **`prepareToPlay()`で確保します**——`processBlock()`で`setSize()`を呼ぶと、
        そこで確保が起きます（9.4）。`dryMono`はダッキングが見る原音です。 */
    /** 8.218：**入口の写し**（Phase 245）。`buffer`は出口として書き換えるので、
        入口の音を別に取っておきます。`dryMono`はダッキングが見る原音。 */
    juce::AudioBuffer<float> inputCopy;
    std::vector<float> dryMono;

    std::atomic<double> syncBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayProcessor)
};
