#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EQAnalyser.h"
#include "EQFilterDesign.h"
#include "LinearPhaseEngine.h"
#include "MantaEQParameters.h"

#include <array>
#include <atomic>

//==============================================================================
/**
    GUIの見せ方（アナライザーの設定など）。**オートメーションの対象ではありません。**

    パラメータにすると、オートメーションの選択メニューに「アナライザーの速さ」が
    並ぶことになります（仕様書5.6）。音に関係しないものは、
    `apvts.state`の`<UI>`へプロパティとして置いて保存だけしています。

    `ProjectIds.h`と同じ書き方（ヘッダで`extern`、実体は`.cpp`）にしてあります。
*/
namespace MantaEQUiState
{
    extern const juce::Identifier analyserMode;     ///< 0=切／1=Pre／2=Post／3=両方
    extern const juce::Identifier analyserFrozen;   ///< 仕様書4.8のFreeze
    extern const juce::Identifier analyserSpeed;    ///< 0（ゆっくり）〜1（速い）
    extern const juce::Identifier analyserTilt;     ///< dB/oct（既定4.5）
    extern const juce::Identifier analyserFloorDb;  ///< 表示の下限（-60／-90／-120）
    extern const juce::Identifier curveRangeDb;     ///< 縦軸の幅（±6／±12／±18／±30）
    extern const juce::Identifier showKeyboard;     ///< 仕様書4.14の鍵盤表示
    extern const juce::Identifier selectedBand;     ///< つまみが指しているバンド

    /** プロパティを読む（無ければ既定値）。 */
    float getFloat (const juce::ValueTree& uiState, const juce::Identifier& id, float defaultValue);
    int getInt (const juce::ValueTree& uiState, const juce::Identifier& id, int defaultValue);
    bool getBool (const juce::ValueTree& uiState, const juce::Identifier& id, bool defaultValue);
}

//==============================================================================
/**
    Manta EQ 本体（設計書1章「全体アーキテクチャ」の`EQAudioProcessor`）。

    **Manta Studio内部専用形式**です（`MantaPluginFormat.h`）。
    VST3としては書き出しません——ホスト側の作法（バス構成の交渉、
    パラメータの上限、GUIのスケーリング）に合わせるぶんの手間が丸ごと不要で、
    そのぶんを中身に使えます。

    ### いま入っているもの（仕様書のフェーズ1〜5）

    | | 中身 |
    |---|---|
    | Phase 1・2 | 12バンドのパラメトリックEQ、9つの形状（4.2） |
    | Phase 3 | ステレオ配置 Stereo/L/R/M/S（4.4） |
    | Phase 4 | ダイナミックEQ（4.5）、バンドのソロ試聴（4.1） |
    | Phase 5 | スペクトラムアナライザー Pre/Post/Freeze（4.8） |
    | Phase 6 | **Linear Phaseモード**（4.7・設計書4.2。`LinearPhaseEngine`） |
    | 4.16 | 出力セクション（Gain/Pan/M-Sバランス/位相反転/オートゲイン） |

    **まだ入っていないもの**：スペクトラムグラブ（4.9）、コリジョン検出（4.10）、
    EQ Match（4.11）、キャラクターモード（4.13）。
    仕様書8章で見送りが決まっているものは含みません。

    ### Linear Phaseではダイナミクスが効きません

    ダイナミックEQは**入力に合わせて振幅を動かす**もので、Linear Phaseは
    **その振幅からインパルス応答を作り直す**方式です。動かすたびに作り直すと、
    フレームの間隔（数十ms）でしか追えず、**動いているのに間に合っていない**
    という分かりにくい鳴り方になります。

    そこで**Linear Phaseのあいだは、ダイナミクスを止めています**
    （バンドの静かなほうのGainだけが効きます）。
    GUIの帯に注意書きを出すので、黙って効かなくなることはありません。

    ### 32サンプルずつ処理する理由

    ダイナミックEQは**入力レベルでゲインが動く**ので、係数もそれに合わせて
    作り直す必要があります。ブロック（512サンプル＝約10ms）ごとだと反応が粗く、
    **1サンプルごとだと係数計算が重すぎます。**

    そこで`chunkSize`（32サンプル＝48kHzで0.67ms）ずつ進め、
    その頭で必要なバンドだけ係数を作り直しています。
    **ダイナミクスを使っていないバンドは、作り直しません。**

    ### `processBlock()`で確保しない

    HANDOVER 9.5：**内蔵プラグインが落ちるとアプリが落ちます**（サンドボックスの外）。
    段の配列（`maxSectionsPerBand`）も検出器も**固定長**にしてあり、
    `processBlock()`の中で`new`が走る場所はありません。

    ### レートは`prepareToPlay()`で受けた値を使う

    8.153：書き出しは**デバイスと違うレート**でグラフを回します。
    デバイスへ訊きに行くと、書き出した音だけEQのカーブがずれます。
*/
class MantaEQProcessor : public juce::AudioPluginInstance,
                          private juce::AsyncUpdater
{
public:
    MantaEQProcessor();
    ~MantaEQProcessor() override;

    //==========================================================================
    // juce::AudioPluginInstance

    void fillInPluginDescription (juce::PluginDescription& description) const override;

    //==========================================================================
    // juce::AudioProcessor

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

    /** カーブを描くためのレート。**`prepareToPlay()`で受けた値**です。 */
    double getSampleRateForDisplay() const { return displaySampleRate.load(); }

    /** いまのバンドの設定（ダイナミクスで動いているぶんも入っています）。

        **カーブはこれを描くこと。** パラメータから組み直すと、
        ダイナミクスで動いているぶんが線に出ません。 */
    MantaEQParams::BandSettings getBandSettings (int bandIndex) const;

    MantaEQParams::OutputSettings getOutputSettings() const;

    EQAnalyserFifo& getPreAnalyserFifo()  { return preFifo; }
    EQAnalyserFifo& getPostAnalyserFifo() { return postFifo; }

    /** 仕様書4.1：バンドのソロ試聴。**-1で解除。**

        保存しません（聴き比べのための一時的なものなので、
        プロジェクトを開き直したらソロは解けているべきです）。 */
    void setSoloedBand (int bandIndex);
    int getSoloedBand() const { return soloedBand.load(); }

    /** 出力メーター用（dBFS）。 */
    float getOutputLevelDb (int channel) const;

    /** オートゲインで実際に引かれている量（dB）。GUIの表示用。 */
    float getAutoGainDb() const { return publishedAutoGainDb.load(); }

    /** いまホストへ申告している遅れ（サンプル）。GUIの表示用。 */
    int getReportedLatencySamples() const { return publishedLatency.load(); }

    /** ダイナミクスをオンにしているバンドが1本でもあるか（GUIの注意書き用）。 */
    bool hasAnyDynamicBand() const;

    /** 空いているバンドを1つ探してオンにする（カーブのダブルクリックから）。
        戻り値はバンド番号。空きが無ければ-1。 */
    int enableFreeBand (float frequencyHz, float gainDb);

    //==========================================================================
    /** GUIの設定（アナライザーの見せ方など）。**オートメーションの対象ではない**ので、
        パラメータではなく`apvts.state`のプロパティとして保存します。

        こうしておくと、プロジェクトの保存・復元（設計書3.8）にそのまま乗ります。 */
    juce::ValueTree getUiState();

private:
    //==========================================================================
    /** 1バンドぶんの、音を出す側の持ち物。 */
    struct BandState
    {
        EQFilterDesign::SectionList sections;
        EQFilterDesign::Biquad filters[2][MantaEQParams::maxSectionsPerBand];

        // 仕様書4.5：ダイナミクスの検出器
        EQFilterDesign::Coeffs detectorCoeffs;
        EQFilterDesign::Biquad detector[2];
        float envelope = 0.0f;
        float dynamicOffsetDb = 0.0f;

        /** アタック／リリースの追従係数。**32サンプルごとに入れ直します**
            （つまみを動かしている最中でも、そのブロックから効かせるため）。 */
        float attackCoefficient = 0.0f;
        float releaseCoefficient = 0.0f;

        void resetFilters() noexcept;
    };

    void updateSettingsFromParameters();
    void refreshBandCoefficients (int bandIndex);
    void updateDynamics (int bandIndex);

    /** バンドの静かなほうの値だけから作った署名。**ダイナミクスは入れない**
        （入れると毎ブロック作り直すことになる）。 */
    juce::uint64 makeStaticSettingsSignature() const;

    /** 設計書4.2：Linear Phaseの核を作り直す（`LinearPhaseEngine`へ振幅を渡す）。 */
    void updateLinearPhaseMagnitudes (int soloedBandIndex);

    /** ホストへ申告する遅れを合わせる。**申告そのものはメッセージスレッドから**
        （`handleAsyncUpdate()`）。 */
    void updateLatency (int wantedLatencySamples);
    void handleAsyncUpdate() override;

    void processChunk (float* left, float* right, int numSamples);
    void processSoloChunk (float* left, float* right, int numSamples, int bandIndex);
    void applyOutputSection (juce::AudioBuffer<float>& buffer);
    void recalculateAutoGain();

    /** L/R → M/S。**行列演算そのもの**（設計書4.3）。 */
    static void toMidSide (float* left, float* right, int numSamples) noexcept;
    static void toLeftRight (float* left, float* right, int numSamples) noexcept;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** パラメータの読み出し口を覚えておく。

        **`getRawParameterValue()`は文字列で引きます。**
        32サンプルごとに引き直すと、そこだけで無視できない時間になります。 */
    struct BandParameterPointers
    {
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* active = nullptr;   ///< バイパスしていないか（Phase 206）
        std::atomic<float>* frequency = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* channel = nullptr;
        std::atomic<float>* dynEnabled = nullptr;
        std::atomic<float>* dynThreshold = nullptr;
        std::atomic<float>* dynRange = nullptr;
        std::atomic<float>* dynAttack = nullptr;
        std::atomic<float>* dynRelease = nullptr;
    };

    std::array<BandParameterPointers, (size_t) MantaEQParams::numBands> bandParameters;

    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* outputPanParam = nullptr;
    std::atomic<float>* outputMsBalanceParam = nullptr;
    std::atomic<float>* outputPhaseInvertParam = nullptr;
    std::atomic<float>* outputAutoGainParam = nullptr;
    std::atomic<float>* processingModeParam = nullptr;
    std::atomic<float>* processingResolutionParam = nullptr;

    //==========================================================================
    std::array<MantaEQParams::BandSettings, (size_t) MantaEQParams::numBands> settings;
    std::array<BandState, (size_t) MantaEQParams::numBands> bands;

    /** GUIが読む用の写し。**音のスレッドが書き、画面のスレッドが読みます。** */
    std::array<std::atomic<float>, (size_t) MantaEQParams::numBands> publishedDynamicOffsetDb;

    // 仕様書4.1：ソロ試聴（バンドの効く範囲だけを鳴らす）
    std::atomic<int> soloedBand { -1 };
    EQFilterDesign::Coeffs soloCoeffs;
    EQFilterDesign::Biquad soloFilters[2][2];

    EQAnalyserFifo preFifo, postFifo;

    // 設計書4.2：Linear Phaseパス（仕様書4.7）
    LinearPhaseEngine linearPhase;
    MantaEQParams::ProcessingMode currentMode = MantaEQParams::ProcessingMode::zeroLatency;
    juce::uint64 linearPhaseSignature = 0;

    std::atomic<int> wantedLatency { 0 };
    std::atomic<int> publishedLatency { 0 };

    std::atomic<double> displaySampleRate { 44100.0 };
    double currentSampleRate = 44100.0;

    // オートゲイン（仕様書4.16）。**設定が変わったときだけ計算し直す**
    juce::uint64 autoGainSignature = 0;
    float autoGainDb = 0.0f;
    std::atomic<float> publishedAutoGainDb { 0.0f };

    std::atomic<float> outputLevelDb[2] { { -100.0f }, { -100.0f } };

    /** 出力段のゲインを滑らかにする（つまみを動かしたときのプツッ音よけ）。 */
    juce::SmoothedValue<float> smoothedOutputGain, smoothedPanLeft, smoothedPanRight;
    juce::SmoothedValue<float> smoothedMidGain, smoothedSideGain;

    /** ダイナミクスの係数を作り直す刻み。 */
    static constexpr int chunkSize = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaEQProcessor)
};
