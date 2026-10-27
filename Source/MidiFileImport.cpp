#include "MidiFileImport.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace MidiFileImport
{

Contents read (const juce::File& file)
{
    Contents contents;

    juce::FileInputStream stream (file);

    if (! stream.openedOk())
        return contents;

    juce::MidiFile midi;

    if (! midi.readFrom (stream))
        return contents;

    const short timeFormat = midi.getTimeFormat();
    const bool ticksPerQuarter = timeFormat > 0;

    // SMPTE 形式は秒にしてから、120BPM（1秒＝2拍）として拍に直す
    if (! ticksPerQuarter)
        midi.convertTimestampTicksToSeconds();

    const auto toBeats = [&] (double timeStamp)
    {
        return ticksPerQuarter ? timeStamp / (double) timeFormat : timeStamp * 2.0;
    };

    double lastBeat = 0.0;

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence sequence (*midi.getTrack (t));
        sequence.updateMatchedPairs();

        for (int i = 0; i < sequence.getNumEvents(); ++i)
        {
            const auto* event = sequence.getEventPointer (i);
            const double beat = toBeats (event->message.getTimeStamp());
            lastBeat = juce::jmax (lastBeat, beat);

            if (! event->message.isNoteOn())
                continue;

            const double endBeat = event->noteOffObject != nullptr ? toBeats (event->noteOffObject->message.getTimeStamp())
                                                                    : beat + 0.25;

            ImportedNote note;
            note.pitch = event->message.getNoteNumber();
            note.velocity = juce::jlimit (1, 127, (int) event->message.getVelocity());
            note.startBeats = beat;
            note.lengthBeats = juce::jmax (1.0 / 64.0, endBeat - beat);   // 長さ0のノートは掴めず鳴らない（8.146）
            contents.notes.push_back (note);

            lastBeat = juce::jmax (lastBeat, note.startBeats + note.lengthBeats);
        }
    }

    contents.lengthBeats = lastBeat;
    contents.ok = true;
    return contents;
}

int addToTrack (Track track, const Contents& contents, double startBeats, juce::UndoManager* undoManager)
{
    int added = 0;

    for (const auto& note : contents.notes)
    {
        track.addNoteBeats (note.pitch, note.velocity, startBeats + note.startBeats, note.lengthBeats, undoManager);
        ++added;
    }

    return added;
}

} // namespace MidiFileImport
