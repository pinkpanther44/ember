#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "Transport.h"
#include "TempoMap.h"   // 仕様書5.1・5.9：テンポと拍子の変化点（Phase 141）

//==============================================================================
/**
    メトロノーム（クリック）を鳴らすプロセッサ（Phase 38）。

    **仕様書には項目が無い**（参考にした画面写真の下端にあったことから追加した）。
    録音のときに拍が分かるものが無く、テンポを決めても手がかりが無かったため。

    ### 位置の決め方

    再生位置は`Transport`から読む（設計書1.5。位置を持つのはTransportだけ）。

    **Phase 141で「掛け算1つ」をやめました。** それまでは

    ```
    1拍のサンプル数 = サンプルレート × 60 / テンポ
    n拍目の位置     = n × 1拍のサンプル数
    ```

    でしたが、**曲の途中でテンポや拍子が変わると、この式は成り立ちません。**
    いまは`TempoMap`（画面と同じ表）に「n拍目は何秒か」を訊きます——
    **ルーラーの小節線とクリックが同じ計算から出る**ので、食い違いようがありません。

    表は**メッセージスレッドが組み立て、一瞬だけロックして入れ替える**形にしてあります
    （`MidiPlayerProcessor::rebuildNoteList()`と同じやり方。HANDOVER 1.12
    「オーディオスレッドからValueTreeを読まない」）。
    オーディオスレッドは**取れなければその回は諦める**ので、待たされません。
    そのため**再生中にテンポを変えても、次のブロックから追従します。**

    ### 音

    プラグインもサンプルも使わず、その場で減衰する正弦波を作る。
    小節の頭（1拍目）だけ高い音にして、拍子の頭が分かるようにしてある。

    ### 書き出しには入れないこと

    このノードはマスターの手前に繋がっているので、**何もしなければ
    ミックスダウンにもクリックが入る**。書き出しの前後で`setEnabled(false)`して戻す
    （AudioEngine::renderMixdownToFile / renderStemsToFolder）。
*/
class MetronomeProcessor : public juce::AudioProcessor
{
public:
    explicit MetronomeProcessor (Transport& transportToUse);

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** クリックを鳴らすか。切ると即座に無音になる（鳴っている途中の音も打ち切らない）。 */
    void setEnabled (bool shouldBeEnabled) { enabled.store (shouldBeEnabled); }
    bool isEnabled() const                 { return enabled.load(); }

    /** テンポと拍子の表を入れ替える（Phase 141）。**メッセージスレッドから呼ぶこと。**

        **再生中に呼んで構いません**——一瞬だけロックして差し替えるだけです。
        オーディオスレッドはロックが取れなければその回を諦めるので、待たされません。 */
    void setTempoMap (const TempoMap& newMap);

    /** クリックの音量（0.0〜1.0）。 */
    void setGain (float newGain)           { gain.store (juce::jlimit (0.0f, 1.0f, newGain)); }
    float getGain() const                  { return gain.load(); }

    //==========================================================================
    // 仕様書5.4：録音のカウントイン（Phase 39）

    /** カウントインを始める。指定した小節ぶんクリックを鳴らし、**鳴らし終えた瞬間に
        トランスポートを開始する**。

        開始をオーディオスレッド側で行うのは、そこがサンプル単位で正確な唯一の場所だから。
        メッセージスレッドのタイマーで「そろそろ終わったはず」と判断すると、
        ブロック1つぶん（10ms前後）ずれる。演奏の頭がその量だけ遅れて録れることになる。

        **メトロノームが切ってあってもカウントインは鳴る**（鳴らないと意味が無い）。 */
    void startCountIn (int bars, juce::int64 transportStartPosition, juce::int64 transportEndPosition);

    /** カウントイン中か（UIの表示用）。 */
    bool isCountingIn() const { return countInBeatsRemaining.load() > 0; }

    /** カウントインを取り消す（録音を始める前に止めたとき）。 */
    void cancelCountIn() { countInBeatsRemaining.store (0); }

    // AudioProcessorの純粋仮想関数群（プラグインではないので簡易実装でよい）
    const juce::String getName() const override                     { return "Metronome"; }
    double getTailLengthSeconds() const override                    { return 0.0; }
    bool acceptsMidi() const override                               { return false; }
    bool producesMidi() const override                              { return false; }
    juce::AudioProcessorEditor* createEditor() override             { return nullptr; }
    bool hasEditor() const override                                 { return false; }
    int getNumPrograms() override                                   { return 1; }
    int getCurrentProgram() override                                { return 0; }
    void setCurrentProgram (int) override                           {}
    const juce::String getProgramName (int) override                { return {}; }
    void changeProgramName (int, const juce::String&) override      {}
    void getStateInformation (juce::MemoryBlock&) override          {}
    void setStateInformation (const void*, int) override            {}

private:
    /** クリック1回ぶんを鳴らし始める。 */
    void startClick (bool isAccent);

    Transport& transport;

    std::atomic<bool> enabled { false };

    // Phase 141：テンポと拍子は**表で持つ**（TempoMap.h）。
    // メッセージスレッドが組み立て、一瞬だけロックして入れ替える
    // （MidiPlayerProcessor::rebuildNoteList()と同じやり方）。
    // **オーディオスレッドは読むだけ**——ここで確保も解放もしないこと（1.12）
    TempoMap tempoMap;
    juce::SpinLock tempoMapLock;

    std::atomic<float> gain { 0.5f };

    double currentSampleRate = 44100.0;

    //==========================================================================
    // カウントイン（Phase 39）。
    // 残り拍数はUIからも読むのでatomic。カウンタはオーディオスレッドだけが触る。

    std::atomic<int> countInBeatsRemaining { 0 };
    std::atomic<juce::int64> countInTransportStart { 0 };
    std::atomic<juce::int64> countInTransportEnd { 0 };

    /** 次の拍までのサンプル数。 */
    double countInSamplesToNextBeat = 0.0;

    /** カウントインの何拍目か（小節の頭にアクセントを付けるため）。 */
    int countInBeatIndex = 0;

    /** Phase 141：カウントインの1拍のサンプル数と拍子。**始めるときに写し取ります。**

        数え終わった瞬間に演奏を始めるので（`processCountIn()`）、**数えている
        最中に間隔が変わると、演奏の頭がその量だけずれます。**
        テンポマップから引くのは**演奏を始める位置の値**です——
        カウントインは「これから始まる場所の拍」を数えるものなので。 */
    double countInSamplesPerBeat = 0.0;
    int countInBeatsPerBar = 4;

    /** カウントイン中のクリックを進める。鳴らし終えたらトランスポートを開始する。 */
    void processCountIn (int numSamples, int& clickOffsetOut, bool& isAccentOut);

    //==========================================================================
    // 鳴っている最中のクリック。オーディオスレッドだけが触るのでatomicは要らない。

    /** 残りサンプル数。0なら鳴っていない。 */
    int clickSamplesRemaining = 0;
    double clickPhase = 0.0;
    double clickPhaseIncrement = 0.0;
    float clickLevel = 0.0f;

    /** クリックの長さ（秒）。短すぎるとプツッとしか聞こえず、長いと拍に被る。 */
    static constexpr double clickLengthSeconds = 0.035;

    static constexpr double accentFrequency = 1600.0; // 小節の頭
    static constexpr double beatFrequency   = 1000.0; // それ以外の拍

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetronomeProcessor)
};
