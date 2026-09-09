#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <vector>

class ProjectModel;

//==============================================================================
/**
    8.146：**弾いたMIDIをノートにする**（Phase 184／改善案⑬a。仕様書5.4）。

    ### なぜ独立した置き場所なのか

    録ったものは3人の手を渡ります：

    | 誰 | 何をするか |
    |---|---|
    | `MidiPlayerProcessor` | オーディオスレッドで**拾って溜める** |
    | `AudioEngine` | アームしたトラックぶんを**集める** |
    | `ArrangeView` | ノートとCCに**直してモデルへ入れる** |

    3人が同じ入れ物を知っている必要があるので、**入れ物だけをここに置きます**。
    `MidiPlayerProcessor.h`に置くと`ArrangeView`が再生器を、
    `AudioEngine.h`に置くと再生器がエンジンを知ることになります。

    ### 直す規則も1箇所に置く

    `applyTake()`が**唯一の変換**です（1.27）。ノートオンとノートオフの
    突き合わせ、鳴りっぱなしの始末、CCの間引き——**どれも判断が要る**ので、
    呼ぶ側ごとに書くと必ずずれます。
*/
struct RecordedMidiEvent
{
    /** **`juce::MidiMessage`では持ちません。**

        あちらは3バイトを超えると`malloc`します。
        溜めるのは**オーディオスレッド**なので、そこで確保が走ると音が途切れます
        （1.15）。ノートオン・ノートオフ・CC・ピッチベンドは
        **どれも2バイトまで**なので、生のバイトで足ります。 */
    juce::uint8 status = 0;
    juce::uint8 data1 = 0;
    juce::uint8 data2 = 0;

    /** 曲の頭から数えたサンプル位置。**ブロックの先頭の値**です
        （オーディオスレッドが`Transport::getPositionSamples()`から取る）。 */
    juce::int64 samplePosition = 0;
};

/** 1トラックぶんの録り分。 */
struct RecordedMidiTake
{
    juce::String trackId;
    std::vector<RecordedMidiEvent> events;

    /** 入れ物が満杯になって、途中から捨てたか（`maxRecordedMidiEvents`）。
        **黙って落とさないこと**——録れていないのに録れたと見えるのが
        いちばん困ります。 */
    bool overflowed = false;
};

/** 溜められるイベントの数の上限（1トラックあたり）。

    **オーディオスレッドで確保しないため**に、始める時点で確保しておきます。
    1件24バイトなので、これで1.5MBほど。16分音符を毎秒16個
    （＝ノートオンとオフで32件）弾き続けても**30分以上**入ります。 */
inline constexpr int maxRecordedMidiEvents = 65536;

namespace MidiRecording
{
    /** `applyTake()`が何を入れたか（画面に出す用）。 */
    struct Summary
    {
        int numNotes = 0;
        int numCCs = 0;

        /** 録音を止めた時点でまだ押されていた鍵の数。
            **止めた位置で切って入れます**（捨てません）。 */
        int numHeldNotes = 0;
    };

    /** 録り分を、そのトラックのノートとCCにして入れる。

        - `sampleRate`はサンプル位置を秒へ直すのに使う（0以下なら何もしない）
        - `stopSeconds`は**録音を止めた曲の時刻**。
          まだ押されていた鍵は、ここで切ります

        **`undoManager`は呼ぶ側が区切ってから渡すこと**（3.1）。
        1回の録音がUndo1回で消えてほしいので、`beginAction()`は外側です。

        ### 入れないもの

        - **同じ値が続くCC**——モジュレーションホイールは毎秒100件近く来ます。
          値が変わった時だけ入れます
        - **ポリフォニックアフタータッチ**——モデルに置き場所がありません
          （`MidiControllers`が持つのは**チャンネル**アフタータッチまで） */
    Summary applyTake (ProjectModel& project,
                        const RecordedMidiTake& take,
                        double sampleRate,
                        double stopSeconds,
                        juce::UndoManager* undoManager);
}
