#include "PunchRecording.h"

namespace PunchRecording
{

int clearAudioRange (Track track, juce::Range<double> range, juce::UndoManager* undoManager)
{
    if (range.isEmpty())
        return 0;

    constexpr double eps = 1.0e-6;
    const double a = range.getStart(), b = range.getEnd();
    int removed = 0;

    // 割ると並びが変わるので、**1つ手を入れるたびに頭から見直す**（回数の上限は念のため）
    for (int guard = 0; guard < 10000; ++guard)
    {
        bool changed = false;

        for (int i = 0; i < track.getNumClips(); ++i)
        {
            auto clip = track.getClip (i);
            const double s = clip.getStartTime();
            const double e = s + clip.getLength();

            if (e <= a + eps || s >= b - eps)
                continue;   // 範囲にかからない

            // 範囲の外へはみ出している側は、範囲の端で割って外を残す（割れた外側は次の周で「かからない」になる）
            if (s < a - eps && track.splitClipAt (clip.state, a, undoManager))
            {
                changed = true;
                break;
            }

            if (e > b + eps && track.splitClipAt (clip.state, b, undoManager))
            {
                changed = true;
                break;
            }

            track.removeClip (clip, undoManager);   // 範囲の中に収まった切れ端
            ++removed;
            changed = true;
            break;
        }

        if (! changed)
            break;
    }

    return removed;
}

AudioClip placeAudio (Track track, const juce::String& filePath, double recordStart, double recordedLength,
                      juce::Range<double> range, juce::UndoManager* undoManager)
{
    // ファイルは削らない。**見せる窓（開始・長さ・オフセット）だけを範囲に合わせる**
    const double start = juce::jmax (range.getStart(), recordStart);
    const double end = juce::jmin (range.getEnd(), recordStart + recordedLength);
    const double offset = start - recordStart;

    return track.addAudioClip (filePath, start, juce::jmax (0.0, end - start), undoManager, offset);
}

int clearNotesInRange (Track track, juce::Range<double> range, juce::UndoManager* undoManager)
{
    if (range.isEmpty())
        return 0;

    int removed = 0;

    for (int i = track.getNumNotes(); --i >= 0;)
    {
        auto note = track.getNote (i);
        const double s = note.getStartTime();
        const double e = s + note.getLength();

        if (s >= range.getStart() && s < range.getEnd())
        {
            track.removeNote (note, undoManager);
            ++removed;
        }
        else if (s < range.getStart() && e > range.getStart())
        {
            note.setLength (range.getStart() - s, undoManager);   // 手前から鳴り続けている音は、範囲の頭で切る
        }
    }

    return removed;
}

int clearCCInRange (Track track, juce::Range<double> range, const std::set<int>& controllers,
                    juce::UndoManager* undoManager)
{
    if (range.isEmpty() || controllers.empty())
        return 0;

    int removed = 0;

    for (int i = track.getNumCCEvents(); --i >= 0;)
    {
        auto event = track.getCCEvent (i);
        const double t = event.getTime();

        if (t >= range.getStart() && t < range.getEnd() && controllers.count (event.getControllerNumber()) > 0)
        {
            track.removeCCEvent (event, undoManager);
            ++removed;
        }
    }

    return removed;
}

int replaceAudio (Track track, const juce::String& filePath, double recordStart, double recordedLength,
                  juce::Range<double> keep, juce::UndoManager* undoManager)
{
    const int replaced = clearAudioRange (track, keep, undoManager);
    placeAudio (track, filePath, recordStart, recordedLength, keep, undoManager);
    return replaced;
}

MidiResult replaceMidi (ProjectModel& project, const RecordedMidiTake& take, double sampleRate, double stopSeconds,
                        juce::Range<double> keep, juce::UndoManager* undoManager)
{
    MidiResult result;

    if (keep.isEmpty())
        return result;   // 範囲の中で録れていない（空の範囲で`applyTake()`を呼ぶと全部入ってしまう）

    auto target = project.findTrackById (take.trackId);

    if (target.state.getParent().isValid())
    {
        result.replacedNotes = clearNotesInRange (target, keep, undoManager);
        clearCCInRange (target, keep, MidiRecording::controllersIn (take, sampleRate, keep), undoManager);
    }

    result.summary = MidiRecording::applyTake (project, take, sampleRate, stopSeconds, undoManager, keep);
    return result;
}

} // namespace PunchRecording
