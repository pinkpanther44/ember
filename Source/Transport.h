#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
/**
    再生位置（プレイヘッド）を一箇所で管理するクラス。設計書1.5。

    Phase 12cのノード化リファクタで新設。それ以前は再生用プロセッサが自分で
    再生位置を持っていたが、トラックごとにプレイヤーを持つ構成になると、
    各自が別々に位置を進めることになり、ずれの温床になる。
    そのため「位置を持つのはTransportだけ、各プロセッサは読むだけ」に変えた。

    位置を進めるのはMasterChannelProcessorが1ブロックにつき1回だけ行う。
    マスターはグラフの最下流にあり、必ず全音源より後に処理されるため、
    「各音源が今ブロックの位置を読む → 最後にマスターが進める」という順序になる。

    値はオーディオスレッドとUIスレッドの両方から触るためstd::atomicで持つ。
*/
class Transport
{
public:
    Transport() = default;

    void prepare (double newSampleRate)
    {
        if (newSampleRate > 0.0)
            sampleRate.store (newSampleRate);
    }

    /** 再生を開始する。endPositionSamplesは自動停止する位置（0以下なら自動停止しない）。

        startPositionSamplesを指定すると途中から再生できる（仕様書5.9。Phase 18）。
        既に終端を過ぎた位置から始めようとした場合は、呼び出し側が判断すること
        （ここでは position >= end なら即座に自動停止する）。 */
    void start (juce::int64 endPositionSamplesToUse, juce::int64 startPositionSamples = 0)
    {
        positionSamples.store (juce::jmax ((juce::int64) 0, startPositionSamples));
        endPositionSamples.store (endPositionSamplesToUse);
        playing.store (true);
    }

    /** 停止中に再生位置だけを動かす（仕様書5.9のシーク）。
        再生中に呼んでも位置は飛ぶが、鳴っているMIDIノートを止めるのは呼び出し側の仕事
        （AudioEngine::setPlayheadSeconds参照）。 */
    void setPositionSamples (juce::int64 newPosition)
    {
        positionSamples.store (juce::jmax ((juce::int64) 0, newPosition));
    }

    double getSampleRate() const      { return sampleRate.load(); }

    void stop()                       { playing.store (false); }
    bool isPlaying() const            { return playing.load(); }
    juce::int64 getPositionSamples() const { return positionSamples.load(); }

    double getPositionSeconds() const
    {
        const double rate = sampleRate.load();
        return rate > 0.0 ? (double) positionSamples.load() / rate : 0.0;
    }

    //==========================================================================
    // 仕様書5.9：ループ再生（Phase 48）
    //
    // **折り返しはブロックの境目でしか起きない。** 各プロセッサはブロックの先頭で
    // 位置を1回読んで、そのブロックぶんをまとめて作るためで、
    // ループ点は最大1ブロックぶん（512サンプル・48kHzなら約11ms）遅れることがあります。
    // サンプル単位で正確に折り返すには、**全プロセッサがブロックを分割して処理する**
    // 必要があり、設計書1.5の作りを大きく変えることになります（6.3に記録）。

    void setLoop (bool shouldLoop, juce::int64 startSamples, juce::int64 endSamples)
    {
        loopStartSamples.store (juce::jmax ((juce::int64) 0, startSamples));
        loopEndSamples.store (juce::jmax ((juce::int64) 0, endSamples));
        loopEnabled.store (shouldLoop);
    }

    bool isLoopEnabled() const              { return loopEnabled.load(); }
    juce::int64 getLoopStartSamples() const { return loopStartSamples.load(); }
    juce::int64 getLoopEndSamples() const   { return loopEndSamples.load(); }

    /** オーディオスレッドから、1ブロックぶん位置を進める。
        終端に達したら自動的に停止する（仕様書5.9の「再生範囲を過ぎたら停止」）。 */
    void advance (int numSamples)
    {
        if (! playing.load())
            return;

        const auto newPosition = positionSamples.load() + (juce::int64) numSamples;
        positionSamples.store (newPosition);

        // 仕様書5.9：ループ中は終端での自動停止より折り返しが優先される（Phase 48）
        if (loopEnabled.load())
        {
            const auto loopStart = loopStartSamples.load();
            const auto loopEnd = loopEndSamples.load();
            const auto loopLength = loopEnd - loopStart;

            if (loopLength > 0 && newPosition >= loopEnd)
            {
                // 行き過ぎたぶんはループの先頭から数え直す。
                // 単純にloopStartへ戻すと、ブロックが長いときにテンポがわずかに狂う。
                positionSamples.store (loopStart + ((newPosition - loopStart) % loopLength));
                return;
            }
        }

        const auto endPosition = endPositionSamples.load();

        if (endPosition > 0 && newPosition >= endPosition)
            playing.store (false);
    }

private:
    std::atomic<bool> playing { false };
    std::atomic<juce::int64> positionSamples { 0 };
    std::atomic<juce::int64> endPositionSamples { 0 };
    std::atomic<double> sampleRate { 44100.0 };

    std::atomic<bool> loopEnabled { false };
    std::atomic<juce::int64> loopStartSamples { 0 };
    std::atomic<juce::int64> loopEndSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Transport)
};
