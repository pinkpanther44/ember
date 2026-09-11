#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ProjectModel.h"
#include "Transport.h"

//==============================================================================
/**
    8.205：**プラグインへ「いま何拍目で、テンポはいくつか」を渡す**
    （Phase 238／ディレイのテンポシンクのため）。

    ─────────────────────────────────────────────────────────────────────────
    ここまで無かったものです
    ─────────────────────────────────────────────────────────────────────────

    JUCEの`AudioProcessorGraph`は、**各ノードの`process()`のたびに
    グラフのプレイヘッドを配ります**（`juce_AudioProcessorGraph.cpp`）：

    ```cpp
    void process (const Context& c) final
    {
        processor.setPlayHead (c.audioPlayHead);
    ```

    ところが**`graph.setPlayHead()`を誰も呼んでいませんでした。**
    つまり、挿してあるプラグインが`getPlayHead()`を呼ぶと`nullptr`が返ります。

    > **これはディレイのためだけの話ではありません。**
    > **テンポに同期する市販のVST3**（ディレイ・アルペジエーター・LFO）が
    > **全部、同期できていませんでした。** ホストとして足りていなかった部分です。

    ─────────────────────────────────────────────────────────────────────────
    何を渡すか
    ─────────────────────────────────────────────────────────────────────────

    | | 出どころ |
    |---|---|
    | 再生中か | `Transport::isPlaying()` |
    | 時刻（秒・サンプル） | `Transport` |
    | テンポ | `ProjectModel::getTempoAt()`（**曲の途中で変わります**。8.102） |
    | 拍子 | `ProjectModel::getTimeSignatureAt()` |
    | 拍の位置 | `ProjectModel::getBarBeatAt()` |

    **テンポと拍子は「その時刻の値」を引きます。** 1つの値を持ち回すと、
    テンポチェンジのある曲で**変化点を過ぎても古い値を配り続けます。**

    ─────────────────────────────────────────────────────────────────────────
    音のスレッドから呼ばれます
    ─────────────────────────────────────────────────────────────────────────

    `getPosition()`は**各プラグインの`processBlock()`の中**から呼ばれます。

    - **確保しないこと。** `juce::String`を作らない——
      拍子は`ProjectModel::getTimeSignatureAt()`が文字列を返すので、
      **ここでは使わず、数字で持っている`getBeatsPerBarAt()`のほうを使います**
    - `ProjectModel`の読み出しは`ValueTree`を辿るだけで、確保はありません
*/
class EnginePlayHead : public juce::AudioPlayHead
{
public:
    EnginePlayHead (ProjectModel& projectToUse, Transport& transportToUse)
        : project (projectToUse), transport (transportToUse)
    {
    }

    /** 音のスレッドから、ブロックの頭のサンプルレートを教えてもらいます。
        **`prepareToPlay`の側から呼ぶこと**（ここで問い合わせに行かない）。 */
    void setSampleRate (double newSampleRate) { sampleRate = newSampleRate; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;

        const double seconds = transport.getPositionSeconds();

        info.setTimeInSeconds (seconds);
        info.setTimeInSamples (transport.getPositionSamples());
        info.setIsPlaying (transport.isPlaying());

        // **録音中かどうかは渡していません。** `Transport`が持っていないので、
        // ここで別の場所から引くと**2箇所に真実ができます**（1.27）。
        // 要るようになったら`Transport`へ足してからここへ回すこと
        info.setIsRecording (false);
        info.setIsLooping (false);

        //----------------------------------------------------------------------
        // テンポと拍子。**その時刻の値**を引きます（曲の途中で変わります。8.102）

        const double bpm = project.getTempoAt (seconds);

        info.setBpm (bpm);

        TimeSignature signature;
        signature.numerator = juce::jmax (1, project.getBeatsPerBarAt (seconds));

        // **分母は`ProjectModel`が数字で持っていません**（"4/4"の文字列）。
        // 音のスレッドで文字列を切るのは避けたいので、
        // **4分音符を1拍とする前提**に揃えてあります——
        // `getTempoAt()`が返すBPMも4分音符あたりの値なので、辻褄は合います。
        //
        // 8/8や6/8を「8分音符が1拍」として扱いたくなったら、
        // **`ProjectModel`側に数字で分母を返すものを足してから**ここを直すこと
        signature.denominator = 4;

        info.setTimeSignature (signature);

        //----------------------------------------------------------------------
        // 拍の位置。**プラグインはここを見て小節の頭を知ります**

        const auto barBeat = project.getBarBeatAt (seconds);

        info.setBarCount (barBeat.bar);
        info.setPpqPositionOfLastBarStart ((double) (barBeat.bar * signature.numerator));
        info.setPpqPosition ((double) (barBeat.bar * signature.numerator)
                               + (double) barBeat.beat + barBeat.beatFraction);

        return info;
    }

private:
    ProjectModel& project;
    Transport& transport;

    double sampleRate = 44100.0;
};
