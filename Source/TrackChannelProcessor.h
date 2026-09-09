#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Transport.h"

//==============================================================================
/**
    仕様書5.7・設計書1.5：1トラックぶんのチャンネル（フェーダー段）。

    Phase 12cのノード化リファクタで新設。トラックの信号経路は

        ClipPlayerProcessor（そのトラックのクリップ再生）
            → （インサートプラグイン列。Phase 12c-2で追加）
            → TrackChannelProcessor（音量・パン・ミュート／ソロ・メーター）
            → マスター

    となり、設計書1.5の「各Trackをノードとしてラップし、インサートは直列のノード列」
    という構成に対応する。

    値の受け渡しはUIスレッド→オーディオスレッドの一方向で、std::atomicのみを使う
    （ValueTreeはスレッドセーフではないため、必要な値だけを写し取る）。
*/
class TrackChannelProcessor : public juce::AudioProcessor
{
public:
    explicit TrackChannelProcessor (Transport& transportToUse);
    ~TrackChannelProcessor() override;

    //==========================================================================
    // 仕様書5.6：オートメーション（Phase 19）

    /** オートメーションの1点。再生位置（サンプル）と正規化値（0〜1）の組。
        curveは「この点から次の点まで」の繋ぎ方（設計書1.3のcurveType）。
        int型で持っているのは、この段階でProjectModelのenumへ依存させないため。 */
    struct AutomationSample
    {
        juce::int64 positionSamples = 0;
        float value = 0.0f;
        int curve = 0; // AutomationCurveをintへ写したもの

        /** 8.37：曲がり具合（Phase 77）。**写し忘れると、画面では曲がっているのに
            直線で鳴る**（見た目と音を合わせるためにここまで運ぶ）。 */
        float curveAmount = 0.0f;
    };

    /** 再生開始時に、モデルのオートメーションを平坦な配列として渡す。

        オーディオスレッドからValueTreeを読むわけにはいかないため、
        ClipPlayerProcessorがクリップを読み込み直すのと同じ考え方で、
        **再生を始める前にメッセージスレッドが写し取っておく**。
        空の配列を渡すと、そのパラメータはオートメーションなし（フェーダーの値のまま）になる。 */
    void setAutomation (std::vector<AutomationSample> volumePoints,
                         std::vector<AutomationSample> panPoints);

    /** オートメーション配列から、指定位置の値をカーブに従って補間する。
        マスターチャンネル（MasterChannelProcessor）も同じ計算を使うため公開している。 */
    static float getAutomationValueAt (const std::vector<AutomationSample>& points,
                                        juce::int64 positionSamples, float fallback);

    /** 仕様書5.6：記録中はオートメーションを無視してフェーダーの値を使う（Phase 20）。

        これが無いと、既にオートメーションが書かれているトラックをWrite/Touch/Latchで
        上書きしようとしたとき、**動かしているフェーダーの音が聞こえない**
        （古いオートメーションが優先されてしまう）。書いている値がそのまま聞こえるのが正しい。 */
    void setAutomationBypassed (bool shouldBypass) { automationBypassed.store (shouldBypass); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** 音量・パン・可聴状態をまとめて設定する（メッセージスレッドから呼ぶ）。

        vcaOffsetDbは仕様書5.2.4のVCAトラックによる**相対オフセット**（設計書1.3）。
        フェーダー値へdBで加算されるだけで、オートメーション（Phase 19）が
        フェーダーより優先される場合でも同じように加算される
        （VCAは「今出ている音量に対する補正」であって、フェーダーの代わりではないため）。
        リンクされていないトラックでは0を渡す。 */
    void setMixSettings (float volumeDb, float pan, bool audible, float vcaOffsetDb = 0.0f);

    /** メーター表示用のレベル（0.0〜1.0）。フェーダー適用後の値。 */
    float getLevel (int channel) const;

    // AudioProcessorの純粋仮想関数群
    const juce::String getName() const override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** これ以下の音量は無音として扱う（フェーダー下端をきちんと0にするため）。 */
    static constexpr float silenceThresholdDb = -60.0f;

private:
    static constexpr int numMeterChannels = 2;
    static constexpr float meterDecayPerBlock = 0.75f;

    /** 音量（dB）・VCAオフセット（dB）・パンから、L/Rそれぞれのゲインを求める。
        setMixSettings()とオートメーション適用の両方から使う（計算を1箇所にまとめる）。 */
    static void calculateGains (float volumeDb, float vcaOffsetDb, float pan, float& leftOut, float& rightOut);

    Transport& transport;

    std::atomic<float> gainLeft { 1.0f };
    std::atomic<float> gainRight { 1.0f };
    std::atomic<bool> audible { true };
    std::atomic<float> levels[numMeterChannels] { { 0.0f }, { 0.0f } };

    // 仕様書5.6：再生開始時に写し取ったオートメーション（Phase 19）。
    // 差し替え（メッセージスレッド）と読み出し（オーディオスレッド）をロックで保護する。
    std::vector<AutomationSample> volumeAutomation;
    std::vector<AutomationSample> panAutomation;
    juce::SpinLock automationLock;

    // フェーダーの現在値。オートメーションが無いパラメータはこちらを使う
    std::atomic<float> currentVolumeDb { 0.0f };
    std::atomic<float> currentPan { 0.0f };

    // 仕様書5.2.4：リンク先VCAによるオフセット（dB。リンクなしなら0）
    std::atomic<float> currentVcaOffsetDb { 0.0f };

    // 記録中はオートメーションを迂回する（setAutomationBypassed参照）
    std::atomic<bool> automationBypassed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackChannelProcessor)
};
