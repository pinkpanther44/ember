#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Transport.h"

//==============================================================================
/**
    仕様書5.4「オーディオ録音機能」・設計書1.5に対応する、録音用のAudioProcessor。

    Phase 7aで新設。オーディオデバイスの入力（audioInputNode）から流れてきた信号を受け取り、
    - 入力レベルの計測（UIのレベルメーター用）
    - 入力モニタリング（ソフトウェアモニタリング。仕様書5.4）
    を行う。実際のファイルへの書き出しはPhase 7bで追加する。

    設計上の位置づけ：録音を独立したAudioIODeviceCallbackとして実装する方法もあるが、
    それだとAudioProcessorGraph（設計書1.5）の外側に音の流れが増えてしまう。
    入力もグラフ上のノードとして扱うことで、将来の「入力にプラグインを挿してから録る」
    「トラックごとの入力ルーティング」といった拡張が、既存のノード接続の延長で書ける。

    レベル値はオーディオスレッドが書き、UIスレッドが読むためstd::atomicで受け渡す
    （ロックを取るとオーディオスレッドが待たされる可能性があるため）。
*/
class RecorderProcessor : public juce::AudioProcessor
{
public:
    explicit RecorderProcessor (Transport& transportToUse);
    ~RecorderProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** 入力モニタリング（入力音をそのまま出力へ返す）のON/OFF。
        スピーカー使用時のハウリングを避けるため、デフォルトはOFF。 */
    void setInputMonitoringEnabled (bool shouldMonitor);
    bool isInputMonitoringEnabled() const { return monitoring.load(); }

    /** 直近の入力ピークレベル（0.0〜1.0）。UIのレベルメーター表示用。
        表示が点滅しないよう、ピーク値から緩やかに減衰させた値を返す。 */
    float getInputLevel (int channel) const;

    /** 仕様書5.4：指定ファイルへの録音を開始する。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。
        メッセージスレッドから呼ぶこと。 */
    juce::String startRecording (const juce::File& file);

    /** 録音を停止し、ファイルを閉じる（未録音なら何もしない）。 */
    void stopRecording();

    bool isRecording() const { return activeWriter.load() != nullptr; }

    /** 現在の録音の長さ（秒）。UIの録音時間表示用。 */
    double getRecordedSeconds() const;

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
    /** 再生位置。**「トランスポートが動いている間だけ録る」ため**に見る（Phase 39）。

        Phase 38まではstartRecording()を呼んだ瞬間から録っていたが、
        カウントイン中（クリックは鳴るがトランスポートは止まっている）に
        その時間まで録り込んでしまう。トランスポートを基準にすると、
        **録音の開始位置がサンプル単位で正確**になる（メッセージスレッドの
        呼び出しタイミングに左右されない）。 */
    Transport& transport;

    static constexpr int numMeterChannels = 2;
    static constexpr float meterDecayPerBlock = 0.75f; // 1ブロックごとに残るレベルの割合

    std::atomic<bool> monitoring { false };
    std::atomic<float> inputLevels[numMeterChannels] { { 0.0f }, { 0.0f } };

    // 仕様書5.4：ファイルへの書き出し。
    // オーディオスレッドから直接ディスクへ書くとドロップアウトの原因になるため、
    // JUCEのThreadedWriter（バックグラウンドスレッドで書き出す仕組み）を使う。
    // activeWriterは「録音中か」をオーディオスレッドへ伝えるための生ポインタで、
    // 実体の所有権はthreadedWriterが持つ。両者の入れ替えはwriterLockで保護する。
    double currentSampleRate = 44100.0;
    juce::TimeSliceThread backgroundThread { "PersonalDAW Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<juce::int64> recordedSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RecorderProcessor)
};
