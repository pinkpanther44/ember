#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../MantaBiquad.h"

#include <array>
#include <vector>

//==============================================================================
/**
    設計書3章「DSPモジュール設計」を1つにまとめたもの（Manta Comp）。

    設計書は`LevelDetector`／`GainComputer`／`EnvelopeFollower`／`LookAheadDelay`……と
    クラスを分けていましたが、**1つにしてあります。**

    | | 理由 |
    |---|---|
    | どれも「1サンプルぶんの数行」 | クラスにすると、行数より`prepare()`と`reset()`の写しのほうが多くなります |
    | 順番が固定 | 差し替える先がありません（EQの形状のように選べるものではない） |
    | 状態が絡み合う | Adaptiveはエンベロープと平均リダクションの**両方**を見ます |

    分けたほうがよい単位（Biquad）は`../MantaBiquad.h`へ出してあり、
    **Manta EQと共有**しています。

    ### 処理の順番（設計書3章）

    ```
    Input Gain → 検出信号を作る（外部SC or 自分／Stereo Link／SCフィルタ）
      → レベル(dB) → ゲインリダクション量 → Attack/Release追従
      → Dryを先読みぶん遅らせる → リダクション＋Makeup(+Auto Gain) → Mix
    ```

    ### 音と絵は同じ式から出す

    伝達特性のグラフ（`CompressorDisplay`）は`computeOutputDb()`を呼びます——
    **音を出しているのとまったく同じ関数**です（1.27）。
    別々に書くと、聴こえている効きと描いてある折れ線が食い違います。

    ### `process()`で確保しない

    HANDOVER 9.5：**内蔵プラグインが落ちるとアプリが落ちます**（サンドボックスの外）。
    遅延バッファも検出器も`prepare()`で確保し、`process()`の中に`new`はありません。
*/
class CompressorEngine
{
public:
    CompressorEngine() = default;

    /** つまみの値をひとまとめにしたもの。**毎ブロック丸ごと渡します。** */
    struct Settings
    {
        float thresholdDb = -20.0f;
        float ratio = 4.0f;
        float kneeDb = 6.0f;

        float attackMs = 10.0f;
        float releaseMs = 100.0f;
        bool autoEnvelope = false;
        bool adaptiveRelease = false;

        float inputGainDb = 0.0f;
        float makeupDb = 0.0f;
        bool autoGain = false;

        bool lookAhead = true;
        bool stereoLink = true;

        bool sidechainFilterOn = false;
        float lowCutHz = 20.0f;
        float highCutHz = 20000.0f;
        bool listen = false;

        float mix = 1.0f;   ///< 0〜1（画面は0〜100%）
    };

    void prepare (double sampleRateToUse);
    void reset();

    /** 仕様書2-3：Look AheadがONのときだけ遅れます。**その値をそのまま申告**します。 */
    int getLatencySamples() const noexcept { return latencySamples; }

    /** 先読みの長さ（サンプル）。ONかOFFかに関わらず、いまのレートでの値。 */
    int getLookAheadSamples() const noexcept { return lookAheadSamples; }

    /** 音を通す。`sidechain`は外部サイドチェイン（無ければnullptr）。 */
    void process (juce::AudioBuffer<float>& buffer,
                   const float* const* sidechain, int numSidechainChannels,
                   const Settings& settings);

    //==========================================================================
    // 画面から読むもの（`process()`が書き、画面のスレッドが読む）

    float getReductionDb() const noexcept { return publishedReductionDb.load(); }
    float getInputLevelDb (int channel) const noexcept { return inputLevelDb[(size_t) juce::jlimit (0, 1, channel)].load(); }
    float getOutputLevelDb (int channel) const noexcept { return outputLevelDb[(size_t) juce::jlimit (0, 1, channel)].load(); }
    float getAutoGainDb() const noexcept { return publishedAutoGainDb.load(); }

    /** いま実際に使っているAttack／Release（Autoで動くので、画面に出す）。 */
    float getEffectiveAttackMs() const noexcept { return publishedAttackMs.load(); }
    float getEffectiveReleaseMs() const noexcept { return publishedReleaseMs.load(); }

    //==========================================================================
    /** 時間の流れを見せるための履歴（Phase 209／本人の要望）。

        1コマぶんは「その区間のいちばん大きかったところ」です。
        **平均ではなく最大**にしてあります——コンプが反応するのはピークなので、
        平均で描くと「効いている理由」が絵から消えます。 */
    struct HistoryFrame
    {
        float inputDb = -100.0f;
        float outputDb = -100.0f;
        float reductionDb = 0.0f;
    };

    /** 1秒あたりのコマ数。**画面の横1ピクセル＝1コマ**で描く前提です
        （600px幅なら4秒ぶん見えます）。 */
    static constexpr int historyRateHz = 150;

    /** 覚えておくコマ数（約13秒ぶん）。**2のべき乗**にしてある（円環の巻き戻しに使う）。 */
    static constexpr int historyCapacity = 2048;

    /** いちばん新しい`count`コマを、古い順に写す。**画面のスレッドから呼ぶこと。** */
    void readHistory (HistoryFrame* destination, int count) const noexcept;

    //==========================================================================
    /** 仕様書3-2：入力レベル(dB) → 出力レベル(dB)。

        **音を出す側とグラフの両方がこれを呼びます**（1.27）。
        `knee`が0ならThreshold前後の2状態、0より大きければ間を2次曲線でつなぎます。 */
    static float computeOutputDb (float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept;

private:
    /** 1チャンネルぶんの、検出とエンベロープの持ち物。 */
    struct ChannelState
    {
        MantaBiquad::Biquad lowCut, highCut;
        float envelopeDb = 0.0f;      ///< いま掛かっているリダクション量（dB）

        void reset() noexcept
        {
            lowCut.reset();
            highCut.reset();
            envelopeDb = 0.0f;
        }
    };

    void updateSidechainCoefficients (const Settings& settings);
    void updateAutoEnvelope (const Settings& settings, float& attackMsOut, float& releaseMsOut);

    double sampleRate = 44100.0;
    int lookAheadSamples = 0;
    int latencySamples = 0;

    /** 先読み用の遅延（円環バッファ）。**Dry側だけ遅らせます**——
        検出は遅らせていない信号を見るので、ピークが出る前に効き始めます。 */
    std::array<std::vector<float>, 2> delayBuffer;
    int delayWritePosition = 0;

    std::array<ChannelState, 2> channels;

    MantaBiquad::Coeffs lowCutCoeffs, highCutCoeffs;
    float lastLowCutHz = -1.0f, lastHighCutHz = -1.0f;

    // Auto（クレストファクターから決める）とAdaptive（平均リダクション）用
    float fastPeak = 0.0f;
    float slowMeanSquare = 0.0f;
    float averageReductionDb = 0.0f;

    /** 履歴（円環バッファ）。**書くのは音のスレッド、読むのは画面のスレッド**。

        `EQAnalyserFifo`と同じ考えで、**書きっぱなし**にしてあります——
        画面が止まっているあいだのコマは捨てて構わないので、
        「読み手が追いつかない」を音のスレッドが気にする必要がありません。 */
    std::vector<HistoryFrame> history;
    std::atomic<int> historyWritePosition { 0 };

    int historyIntervalSamples = 1;
    int historyCounter = 0;

    /** 溜めている1コマぶん。**dBにするのはコマを書くときだけ**
        （1サンプルごとに`log10`を2回呼ぶと、絵のために音のスレッドを使いすぎます）。 */
    float pendingInputPeak = 0.0f;
    float pendingOutputPeak = 0.0f;
    float pendingReductionDb = 0.0f;

    std::atomic<float> publishedReductionDb { 0.0f };
    std::atomic<float> publishedAutoGainDb { 0.0f };
    std::atomic<float> publishedAttackMs { 10.0f };
    std::atomic<float> publishedReleaseMs { 100.0f };
    std::array<std::atomic<float>, 2> inputLevelDb { { { -100.0f }, { -100.0f } } };
    std::array<std::atomic<float>, 2> outputLevelDb { { { -100.0f }, { -100.0f } } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorEngine)
};
