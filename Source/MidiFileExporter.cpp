#include "MidiFileExporter.h"
#include "MidiCCMessage.h" // 仕様書5.3.3：CC番号→MIDIメッセージの対応（再生側と共有）
#include "Utf8.h"

juce::String MidiFileExporter::exportToFile (const ProjectModel& project, const juce::File& file)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (ticksPerQuarterNote);

    // 8.134：**テンポと拍子の変化点を書き出す**（Phase 171／8.105の宿題1）。
    //
    // Phase 170までは**曲の頭のテンポ1つだけ**でした。テンポマップ（8.102）を
    // 入れたのに書き出しが追いついておらず、**途中でテンポを変えた曲が、
    // 書き出すと最初のテンポのまま**になっていました。
    //
    // 拍子に至っては**1つも書いていません**でした（読み込んだ側は4/4と解釈します）。
    juce::MidiMessageSequence tempoTrack;

    const auto& tempoMap = project.getTempoMap();

    // **ティックは「拍の位置 × 960」。** SMFのティックは4分音符あたりで数えるので、
    // **拍の位置がそのまま使えます**——このアプリのBPMは4分音符あたりの数だからです
    // （拍子の分母は表示のためだけのもの。`TempoMap`は分母で拍の長さを変えていない）
    const auto beatsToTicks = [] (double beats)
    {
        return juce::jmax (0.0, beats) * (double) ticksPerQuarterNote;
    };

    // 曲の頭。**必ず置くこと**：無いと読み込んだ側は120BPM・4/4と解釈します
    tempoTrack.addEvent (juce::MidiMessage::tempoMetaEvent (microsecondsPerQuarterNote (tempoMap.initialTempo)), 0.0);
    tempoTrack.addEvent (juce::MidiMessage::timeSignatureMetaEvent (tempoMap.initialBeatsPerBar,
                                                                     tempoMap.initialDenominator), 0.0);

    for (const auto& change : tempoMap.tempoChanges)
        tempoTrack.addEvent (juce::MidiMessage::tempoMetaEvent (microsecondsPerQuarterNote (change.bpm)),
                              beatsToTicks (change.beatPosition));

    // **拍子は小節で決まる**ので、小節の頭の拍位置へ置く（8.102の3つの座標系）
    for (const auto& change : tempoMap.meterChanges)
        tempoTrack.addEvent (juce::MidiMessage::timeSignatureMetaEvent (change.beatsPerBar, change.denominator),
                              beatsToTicks (tempoMap.getBeatForBarStart (change.bar)));

    // **並べ直すこと。** 変化点は時刻順に入っているとは限らず（拍子とテンポを
    // 別々に足しているので必ず混ざる）、順不同のままだと読めないファイルになります
    tempoTrack.sort();

    midiFile.addTrack (tempoTrack);

    int numExportedTracks = 0;

    // ミュート／ソロで落としたトラックの数。**0本になった理由を分けて伝えるため**に数える
    // （ノートが無いのか、全部ミュートしているのかは、メッセージからしか分からない。1.9）。
    int numSilencedTracks = 0;

    // 仕様書5.2.1：ミュートされたトラックは書き出さない。ソロが立っていれば
    // ソロのトラックだけを書き出す（Phase 53／8.1のA4）。
    // **判定は`ProjectModel::isTrackAudible()`に集約してある**：VCAのミュート／ソロも
    // 効くうえ、ミキサー（`AudioEngine::updateMixerSettings`）と同じものを通さないと、
    // 「鳴っている内容」と「書き出した内容」が食い違う（HANDOVER 8.2）。
    // ソロが立っているかは全トラック共通なので、ここで1回だけ数える。
    const bool anySoloActive = project.isAnySoloActive();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Midi || track.getNumNotes() == 0)
            continue;

        if (! project.isTrackAudible (track, anySoloActive))
        {
            ++numSilencedTracks;
            continue;
        }

        juce::MidiMessageSequence sequence;
        sequence.addEvent (juce::MidiMessage::textMetaEvent (3, track.getName())); // 3 = トラック名

        // 仕様書5.3.2：ドラムマップの行ミュートとチョークグループは、書き出しにも効かせる（Phase 25）。
        // 再生と条件を揃えないと「鳴っている内容」と「書き出した内容」が食い違う（HANDOVER 1.14）。
        auto drumMap = project.getDrumMapForTrack (track);
        const bool hasDrumMap = drumMap.state.isValid();

        // チョークは全ノートが揃ってからでないと適用できないため、
        // いったんノートオン／オフの時刻を控えておき、最後にまとめて積む
        struct PendingNote { int pitch; int velocity; double startSeconds; double endSeconds; };
        std::vector<PendingNote> pendingNotes;
        std::vector<ChokeNote> chokeNotes;
        std::vector<size_t> chokeTargets;

        // 8.91：**ノートはトラックが直接持つ**（Phase 131）。窓の判定も換算も無くなった
        {
            for (int n = 0; n < track.getNumNotes(); ++n)
            {
                auto note = track.getNote (n);

                // 仕様書5.3.2：ミュートされている行は書き出さない（Phase 25）
                if (hasDrumMap && drumMap.isNoteMuted (note.getPitch()))
                    continue;

                const double startSeconds = note.getStartTime();
                const double endSeconds = startSeconds + juce::jmax (0.01, note.getLength());

                if (endSeconds <= startSeconds)
                    continue;

                if (hasDrumMap)
                {
                    const int muteGroup = drumMap.getMuteGroupForNote (note.getPitch());

                    if (muteGroup > 0)
                    {
                        chokeNotes.push_back ({ muteGroup, startSeconds, endSeconds });
                        chokeTargets.push_back (pendingNotes.size());
                    }
                }

                pendingNotes.push_back ({ note.getPitch(), note.getVelocity(), startSeconds, endSeconds });
            }

            // 仕様書5.3.3：CCイベントも書き出す（Phase 23）。
            // メッセージの作り方は再生側（MidiPlayerProcessor）と同じヘッダを使う。
            // どちらかだけ変えると「鳴っている内容」と「書き出した内容」が食い違う。
            for (int e = 0; e < track.getNumCCEvents(); ++e)
            {
                auto event = track.getCCEvent (e);

                // 8.138：**拍はイベントが持っています**（Phase 176／8.105の宿題3）。
                // Phase 175までは`秒 → 拍`と直していました（8.134の宿題1）。
                // いまは拍のほうが保存されているので、秒へ直して戻す必要はありません
                const double ticks = beatsToTicks (event.getTimeBeats());


                sequence.addEvent (MidiCCMessage::create (1, event.getControllerNumber(),
                                                           event.getValue()), ticks);
            }
        }

        // 仕様書5.3.2：チョークグループを効かせてから、ノートを積む（Phase 25）。
        // 再生側（MidiPlayerProcessor）と同じ`applyChokeGroups()`を通しているので、
        // 切れ方も必ず一致する。
        if (! chokeNotes.empty())
        {
            applyChokeGroups (chokeNotes);

            for (size_t i = 0; i < chokeTargets.size(); ++i)
                pendingNotes[chokeTargets[i]].endSeconds = chokeNotes[i].endTime;
        }

        for (const auto& pending : pendingNotes)
        {
            if (pending.endSeconds <= pending.startSeconds)
                continue; // チョークで潰れたノートは書き出さない

            // 8.138：**ノートは秒を通ります**（Phase 176）。チョークグループが
            // 「後の音が前の音を止める」を**秒で**計算するため（再生側と同じ`applyChokeGroups()`
            // を使う決まり。1.14）。**往復しても値は変わりません**——
            // `getTimeForBeat()`と`getBeatAtTime()`は同じ表の逆関数どうしです（8.101）
            const double startTicks = beatsToTicks (project.getBeatPositionAt (pending.startSeconds));
            const double endTicks = beatsToTicks (project.getBeatPositionAt (pending.endSeconds));

            sequence.addEvent (juce::MidiMessage::noteOn (1, pending.pitch,
                                                           (juce::uint8) pending.velocity), startTicks);
            sequence.addEvent (juce::MidiMessage::noteOff (1, pending.pitch), endTicks);
        }

        sequence.updateMatchedPairs();
        midiFile.addTrack (sequence);
        ++numExportedTracks;
    }

    if (numExportedTracks == 0)
    {
        if (numSilencedTracks > 0)
            return utf8 ("書き出せるMIDIトラックがありません"
                          "（ミュート、またはソロで外れているトラックは書き出しません）。");

        return utf8 ("書き出せるMIDIノートがありません。");
    }

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
        return utf8 ("ファイルを作成できませんでした: ") + file.getFullPathName();

    if (! midiFile.writeTo (*stream))
    {
        stream.reset();
        file.deleteFile();
        return utf8 ("MIDIファイルの書き込みに失敗しました。");
    }

    return {};
}
