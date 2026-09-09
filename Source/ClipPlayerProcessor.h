#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "ProjectModel.h"
#include "Transport.h"

//==============================================================================
/**
    設計書1.5「オーディオエンジンアーキテクチャ」に対応する、クリップ再生用のAudioProcessor。

    Phase 4dで新設。**Phase 12cのノード化リファクタで「1トラックにつき1つ」**になった。
    それ以前は1つのプロセッサが全トラックのクリップを内部でミックスしていたが、
    それではトラックごとにインサートプラグインを挟めない（設計書1.5が要求する
    「各Trackをノードとしてラップする」構成にできない）ため、分割した。

    音量・パン・ミュート／ソロはここでは扱わない。後段のTrackChannelProcessorの担当。
    再生位置も持たず、共有のTransportから読む。

    現時点での既知の簡略化（今後の課題）：
    - Play開始時にその時点のクリップ一覧を読み込み直す方式のため、再生中にクリップを
      追加・削除しても次にPlayを押すまでは反映されない
    - ファイル読み込み（AudioFormatReaderSource::getNextAudioBlock）自体を
      オーディオコールバック内で行っており、先読みバッファリングは未実装

    8.152：**サンプルレートの違いは、ここで吸収します**（Phase 190／8.1のD9aの前提）。

    Phase 189まで、ファイルのサンプルレートを見ていませんでした。
    **48kのプロジェクトへ44.1kのファイルを取り込むと、8.8%速く鳴っていました**
    （読む位置をプロジェクトのサンプル数で数えていたため）。

    ### 位置から引き直す（状態を持たない）

    **出力のn番目が、ファイルのどこに当たるかを毎回計算します。**

    ```
    ファイルの位置 = 頭の位置 + クリップの中の位置 × (ファイルのレート ÷ 出力のレート)
    ```

    足し込んでいく形（リサンプラを1つ持って回す）にしないのは、
    **このクラスが位置を持たない**からです——`processBlock()`は毎ブロック
    `setNextReadPosition()`で飛び直しますし、書き出しでは別のレートで
    同じグラフを回します。**状態を持つと、飛んだ先で必ず食い違います。**

    ### レートが同じときは何もしない

    **倍率がちょうど1のときは整数で読みます**（Phase 189までと同じ道）。
    ほとんどのファイルはプロジェクトと同じレートなので、
    そこを補間で通すと、**直す前より音が悪くなります**。
*/
class ClipPlayerProcessor : public juce::AudioProcessor
{
public:
    /** trackIdで指定したトラックのクリップだけを再生する。 */
    ClipPlayerProcessor (ProjectModel& projectToUse, Transport& transportToUse, juce::String trackIdToPlay);
    ~ClipPlayerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** 現在のProjectModelの内容でクリップ一覧を読み込み直す（再生開始時に呼ぶ）。 */
    void prepareClipsForPlayback();

    /** このトラックのクリップが終わる位置（サンプル）。自動停止位置の計算に使う。 */
    juce::int64 getEndPositionSamples() const;

    // AudioProcessorの純粋仮想関数群（プラグインではないので簡易実装でよい）
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

private:
    struct ActiveClip
    {
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

        // **これらは「出力の」サンプル数**（プロジェクトのレート）
        juce::int64 startSample = 0;
        juce::int64 lengthSamples = 0;
        juce::int64 fadeInSamples = 0;
        juce::int64 fadeOutSamples = 0;

        /** 8.152：読み始めるところ。**「ファイルの」サンプル数**（Phase 190）。
            **出力のサンプル数と混ぜないこと**——レートが違うと別の場所を指します。 */
        double sourceStartSamples = 0.0;

        /** 8.152：出力1サンプルが、ファイルの何サンプルぶんか
            （＝ファイルのレート ÷ 出力のレート）。**1.0なら補間しません**。 */
        double sourceSamplesPerOutput = 1.0;

        /** 仕様書5.5：クリップゲイン（Phase 80/8.40）。**掛けられる倍率**で持つ。
            オーディオスレッドでdBから直さないための写し（AutomationSampleと同じ考え方）。 */
        float gainLinear = 1.0f;

        /** 仕様書5.5：逆再生（Phase 86/8.46）。**ファイルは触らず、読む向きを変える**。 */
        bool reversed = false;
    };

    ProjectModel& project;
    Transport& transport;
    const juce::String trackId;

    juce::AudioFormatManager formatManager;

    std::vector<ActiveClip> clipSources;
    mutable juce::SpinLock clipSourcesLock; // prepareClipsForPlayback()（メッセージスレッド）とprocessBlock（オーディオスレッド）間の保護用

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // 毎ブロックのヒープ確保を避けるため、prepareToPlayで一度だけ確保する。
    // 8.152：**レートが違うクリップがあると、1ブロックぶんより多く要ります**
    // （48kの512サンプルを鳴らすのに、96kのファイルからは1024サンプル読む）。
    // 広げるのは`prepareClipsForPlayback()`（メッセージスレッド）だけ
    juce::AudioBuffer<float> scratchBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipPlayerProcessor)
};
