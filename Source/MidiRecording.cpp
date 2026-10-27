#include "MidiRecording.h"

#include "ProjectModel.h"
#include "MidiEditModel.h"

#include <array>

namespace MidiRecording
{

namespace
{
    /** 押しっぱなしのまま止められた鍵に与える最短の長さ（秒）。

        **0にしないこと。** 長さ0のノートはピアノロールで掴めず、
        再生でも鳴りません（ノートオンとオフが同じサンプルに来る）。 */
    constexpr double minimumNoteSeconds = 0.02;

    struct HeldNote
    {
        bool held = false;
        int velocity = 100;
        double startSeconds = 0.0;
    };
}

Summary applyTake (ProjectModel& project,
                    const RecordedMidiTake& take,
                    double sampleRate,
                    double stopSeconds,
                    juce::UndoManager* undoManager,
                    juce::Range<double> keepRange)
{
    Summary summary;

    if (sampleRate <= 0.0 || take.events.empty())
        return summary;

    auto track = project.findTrackById (take.trackId);

    // 1.32：**IDで引いて、見つからなければ何もしない。** 録っている最中に
    // トラックを消されることは普通にあります（切り離されたツリーは異常ではない）
    if (! track.state.getParent().isValid())
        return summary;

    // 押されている鍵は**音程ごとに1つ**だけ覚えます。同じ音程を離さずに
    // 二度押すことはできないので、これで足ります
    std::array<HeldNote, 128> held {};

    // **値が変わった時だけ入れる**ための直前の値（`MidiControllers`の特殊値ぶんも入る）。
    // -1は「まだ何も来ていない」
    std::array<int, 130> lastCCValue {};
    lastCCValue.fill (-1);

    const auto toSeconds = [sampleRate] (juce::int64 samples)
    {
        return juce::jmax (0.0, (double) samples / sampleRate);
    };

    const auto addNote = [&] (int pitch, const HeldNote& note, double endSeconds) -> bool
    {
        // 8.360：パンチの範囲があるときは、**範囲の中で押し始めた音だけ**。範囲の終わりで切る（Phase 349）
        if (! keepRange.isEmpty())
        {
            if (note.startSeconds < keepRange.getStart() || note.startSeconds >= keepRange.getEnd())
                return false;

            endSeconds = juce::jmin (endSeconds, keepRange.getEnd());
        }

        // ループの折り返しをまたぐと、終わりが始まりより前に来ます
        // （`Transport::advance()`が位置を戻すため）。**捨てずに最短で入れる**——
        // 弾いた音が消えるより、短くても残っているほうがましです
        const double length = juce::jmax (minimumNoteSeconds, endSeconds - note.startSeconds);

        track.addNote (pitch, note.velocity, note.startSeconds, length, undoManager);
        ++summary.numNotes;
        return true;
    };

    const auto addCC = [&] (int controllerNumber, int value, double timeSeconds)
    {
        if (! keepRange.isEmpty() && (timeSeconds < keepRange.getStart() || timeSeconds >= keepRange.getEnd()))
            return;   // 8.360：範囲の外の CC は入れない

        if (! juce::isPositiveAndBelow (controllerNumber, (int) lastCCValue.size()))
            return;

        if (lastCCValue[(size_t) controllerNumber] == value)
            return;   // 同じ値が続いている（モジュレーションホイールは毎秒100件近く来る）

        lastCCValue[(size_t) controllerNumber] = value;
        track.addCCEvent (controllerNumber, value, timeSeconds, undoManager);
        ++summary.numCCs;
    };

    for (const auto& event : take.events)
    {
        const int type = event.status & 0xf0;
        const double timeSeconds = toSeconds (event.samplePosition);
        const int pitch = event.data1 & 0x7f;

        // **ベロシティ0のノートオンはノートオフ**。ランニングステータスで
        // 送ってくる鍵盤があるので、必ず両方を見ること
        const bool isNoteOff = (type == 0x80) || (type == 0x90 && event.data2 == 0);

        if (isNoteOff)
        {
            auto& note = held[(size_t) pitch];

            if (note.held)
            {
                addNote (pitch, note, timeSeconds);
                note.held = false;
            }

            continue;
        }

        if (type == 0x90)
        {
            auto& note = held[(size_t) pitch];

            // 同じ音程のノートオフが来ないまま次のノートオンが来た（取りこぼし）。
            // **今の位置で切ってから**新しく始める
            if (note.held)
                addNote (pitch, note, timeSeconds);

            note.held = true;
            note.velocity = juce::jmax (1, (int) event.data2);
            note.startSeconds = timeSeconds;
            continue;
        }

        if (type == 0xb0)
        {
            addCC (pitch, event.data2 & 0x7f, timeSeconds);
            continue;
        }

        if (type == 0xe0)
        {
            // ピッチベンドは14ビット（下位7ビットが先）
            const int value = (event.data1 & 0x7f) | ((event.data2 & 0x7f) << 7);
            addCC (MidiControllers::pitchBend, value, timeSeconds);
            continue;
        }

        if (type == 0xd0)
        {
            addCC (MidiControllers::channelPressure, pitch, timeSeconds);
            continue;
        }

        // 0xa0（ポリフォニックアフタータッチ）と0xc0（プログラムチェンジ）は
        // モデルに置き場所がないので捨てる
    }

    // 止めた時点でまだ押されていた鍵。**そこで切って入れる**——
    // 押しっぱなしのまま止めるのは、最後の音を伸ばしたときに普通に起きます
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        auto& note = held[(size_t) pitch];

        if (! note.held)
            continue;

        if (addNote (pitch, note, juce::jmax (stopSeconds, note.startSeconds + minimumNoteSeconds)))
            ++summary.numHeldNotes;   // 8.360：範囲の外で捨てた音は数えない

        note.held = false;
    }

    return summary;
}

std::set<int> controllersIn (const RecordedMidiTake& take, double sampleRate, juce::Range<double> range)
{
    std::set<int> numbers;

    if (sampleRate <= 0.0)
        return numbers;

    for (const auto& event : take.events)
    {
        const double t = juce::jmax (0.0, (double) event.samplePosition / sampleRate);

        if (! range.isEmpty() && (t < range.getStart() || t >= range.getEnd()))
            continue;

        // 番号の振り方は`applyTake()`と同じ
        switch (event.status & 0xf0)
        {
            case 0xb0: numbers.insert (event.data1 & 0x7f); break;
            case 0xe0: numbers.insert (MidiControllers::pitchBend); break;
            case 0xd0: numbers.insert (MidiControllers::channelPressure); break;
            default: break;
        }
    }

    return numbers;
}

} // namespace MidiRecording
