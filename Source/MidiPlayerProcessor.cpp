#include "MidiPlayerProcessor.h"
#include <cmath>   // std::ceil／std::round（CCの補間。Phase 76）
#include "MidiCCMessage.h" // 仕様書5.3.3：CC番号→MIDIメッセージの対応（書き出し側と共有）

#include <algorithm>
#include <map>

MidiPlayerProcessor::MidiPlayerProcessor (ProjectModel& projectToUse, Transport& transportToUse,
                                            juce::String trackIdToPlay)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      project (projectToUse),
      transport (transportToUse),
      trackId (std::move (trackIdToPlay))
{
}

void MidiPlayerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    // 再生の開始・終了の判定を持ち越さない。書き出し（オフライン処理）の前後などで
    // prepareToPlayは何度も呼ばれるため、前回の状態が残っていると
    // 開始直後に不要なノートオフを送ってしまう。
    wasPlaying = false;
    allNotesOffPending.store (false);
}

void MidiPlayerProcessor::releaseResources()
{
    const juce::SpinLock::ScopedLockType lock (notesLock);
    scheduledNotes.clear();
}

void MidiPlayerProcessor::rebuildNoteList()
{
    // 新しいリストはロックの外で組み立て、完成したら一瞬だけロックして入れ替える
    std::vector<ScheduledNote> newNotes;
    std::vector<ScheduledCC> newCCs;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // 担当トラック以外は無視する（Phase 14。並び順ではなくIDで対応させることで、
        // ノードの作り直しと行き違っても別トラックのノートを鳴らしてしまわない）
        if (track.getType() != TrackType::Midi || track.getId() != trackId)
            continue;

        // 仕様書5.3.2：ドラムマップがあれば、行ミュートとチョークグループを効かせる（Phase 25）。
        // 未割り当てのトラックでは無効なDrumMapが返り、従来どおりの挙動になる。
        auto drumMap = project.getDrumMapForTrack (track);
        const bool hasDrumMap = drumMap.state.isValid();

        // チョークの適用には全ノートが揃っている必要があるので、
        // ここでは対応表だけ作っておき、下でまとめて処理する
        std::vector<ChokeNote> chokeNotes;
        std::vector<size_t> chokeTargets; // newNotes内の位置

        // 8.91：**ノートはトラックが直接持つ**（Phase 131）。クリップをまたぐ走査も、
        // 窓の判定も、時刻の換算も要らなくなった——**時刻が1つしか無いため**
        {
            for (int n = 0; n < track.getNumNotes(); ++n)
            {
                auto note = track.getNote (n);

                // 仕様書5.3.2：ミュートされている行の音は鳴らさない（Phase 25）
                if (hasDrumMap && drumMap.isNoteMuted (note.getPitch()))
                    continue;

                // 8.91：**ノートの時刻はそのまま曲の時刻**（Phase 131）。
                // 窓の判定も換算も無くなった——「クリップの外は鳴らさない」という
                // 決まりごと自体が消えたため（そもそも外が無い）
                const double absoluteStart = note.getStartTime();
                const double absoluteEnd = absoluteStart + note.getLength();

                if (absoluteEnd <= absoluteStart)
                    continue;

                ScheduledNote scheduled;
                scheduled.pitch = juce::jlimit (0, 127, note.getPitch());
                scheduled.velocity = juce::jlimit (1, 127, note.getVelocity());
                scheduled.startSample = (juce::int64) (absoluteStart * currentSampleRate);
                scheduled.endSample = (juce::int64) (absoluteEnd * currentSampleRate);
                scheduled.isSounding = false;

                // 仕様書5.3.2：チョークグループに属する音は、後でまとめて長さを詰める
                if (hasDrumMap)
                {
                    const int muteGroup = drumMap.getMuteGroupForNote (scheduled.pitch);

                    if (muteGroup > 0)
                    {
                        chokeNotes.push_back ({ muteGroup, absoluteStart, absoluteEnd });
                        chokeTargets.push_back (newNotes.size());
                    }
                }

                newNotes.push_back (scheduled);
            }

            // 仕様書5.3.3：CCイベント（Phase 23。Phase 131でトラックの持ち物になった）。
            //
            // **Phase 76：点と点のあいだを繋ぎ方に従って埋めます**（8.36）。
            // それまでは点の数だけメッセージを出す階段状で固定でした。
            // ピアノロールが線を斜めに描くようになったので、**鳴り方と見た目を
            // 揃えるために**ここも補間します（1.9と同じ「表示と実際を食い違わせない」話）。
            for (int e = 0; e < track.getNumCCEvents(); ++e)
            {
                auto event = track.getCCEvent (e);
                const double absoluteTime = event.getTime();

                ScheduledCC scheduled;
                scheduled.controllerNumber = event.getControllerNumber();
                scheduled.value = event.getValue();
                scheduled.sample = (juce::int64) (absoluteTime * currentSampleRate);

                newCCs.push_back (scheduled);

                // 同じコントローラーの「次の点」を探して、あいだを埋める。
                // **ステップのときは何も足しません**（次の点まで前の値を保つ）
                const auto curve = event.getCurve();

                if (curve == AutomationCurve::Step)
                    continue;

                auto next = track.findNextCCEvent (event);

                if (! next.state.isValid())
                    continue;

                const double span = next.getTime() - absoluteTime;
                const int fromValue = event.getValue();
                const int toValue = next.getValue();

                if (span <= 0.0 || fromValue == toValue)
                    continue;

                // **刻みは固定（20ms）**。細かくするほど滑らかになるが、
                // MIDIのメッセージ数は増える。値が変わらない刻みは足さない
                const int steps = juce::jlimit (1, 512, (int) std::ceil (span / ccInterpolationStep));
                int previousValue = fromValue;

                for (int s = 1; s < steps; ++s)
                {
                    // 8.37：**曲がり具合も渡す**（Phase 77）。渡し忘れると、
                    // 画面では曲がっているのに直線で鳴る
                    const float t = applyAutomationCurve (curve, (float) s / (float) steps,
                                                           event.getCurveAmount());
                    const int value = fromValue + (int) std::round (t * (float) (toValue - fromValue));

                    if (value == previousValue)
                        continue;

                    ScheduledCC between;
                    between.controllerNumber = scheduled.controllerNumber;
                    between.value = value;
                    between.sample = (juce::int64) ((absoluteTime + span * s / steps) * currentSampleRate);

                    newCCs.push_back (between);
                    previousValue = value;
                }
            }
        }

        // 仕様書5.3.2：クリップをまたいで並んだ音も含めてチョークを効かせる（Phase 25）。
        // クリップ単位でやると、隣のクリップのオープンハイハットを切れない。
        if (! chokeNotes.empty())
        {
            applyChokeGroups (chokeNotes);

            for (size_t i = 0; i < chokeTargets.size(); ++i)
            {
                auto& scheduled = newNotes[chokeTargets[i]];
                scheduled.endSample = (juce::int64) (chokeNotes[i].endTime * currentSampleRate);
            }
        }
    }

    // 追いかけ（sendCCChase）は「位置以前の最後の値」を探すので、時刻順に並んでいる必要がある。
    // クリップが複数ある場合、上のループの出来上がり順は時刻順とは限らない。
    std::sort (newCCs.begin(), newCCs.end(),
                [] (const ScheduledCC& a, const ScheduledCC& b) { return a.sample < b.sample; });

    // 8.116：**鳴っている最中のノートを引き継ぐ**（Phase 151）。
    //
    // 入れ替えると新しいノートは`isSounding = false`から始まります。
    // **再生中に組み直すと、いま鳴っているノートのノートオフが誰にも送られず、
    // 鳴りっぱなしになります**（Play時にしか組み直していなかったので、
    // これまでは起こりませんでした）。
    //
    //   - 位置をまたいでいる同じ音は`isSounding`を立て直す（**押したままにする**）
    //   - 行き先が無くなった音は**ノートオフの予約**を積む（次のブロックの頭で送る）
    const juce::int64 position = transport.getPositionSamples();
    const bool playing = transport.isPlaying();

    const juce::SpinLock::ScopedLockType lock (notesLock);

    std::vector<int> stillSounding;

    if (playing)
        for (const auto& note : scheduledNotes)
            if (note.isSounding)
                stillSounding.push_back (note.pitch);

    scheduledNotes = std::move (newNotes);
    scheduledCCs = std::move (newCCs);

    // 8.116：**CCも追いかけ直す**（Phase 151）。再生中にCCを編集したとき、
    // いまの位置で効いている値を送り直さないと、次の点まで古い値のままになります
    if (playing)
        ccChasePending.store (true);

    if (! playing || stillSounding.empty())
        return;

    for (auto& note : scheduledNotes)
    {
        if (note.startSample > position || note.endSample <= position)
            continue;   // いまの位置をまたいでいない

        auto it = std::find (stillSounding.begin(), stillSounding.end(), note.pitch);

        if (it == stillSounding.end())
            continue;


        stillSounding.erase (it);
    }

    // 残ったのは「行き先が無くなった音」。**消さないと鳴りっぱなし**になります
    for (int pitch : stillSounding)
        pendingNoteOffPitches.push_back (pitch);
}

void MidiPlayerProcessor::sendCCChase (juce::MidiBuffer& midiMessages, juce::int64 positionSamples)
{
    // 再生位置以前にある「各コントローラーの最後の値」を集めて、まとめて送る。
    // scheduledCCsは時刻順なので、前から順に上書きしていけば最後の値が残る。
    std::map<int, int> latestValues;

    for (const auto& cc : scheduledCCs)
    {
        if (cc.sample > positionSamples)
            break;

        latestValues[cc.controllerNumber] = cc.value;
    }

    for (const auto& [controllerNumber, value] : latestValues)
        midiMessages.addEvent (MidiCCMessage::create (1, controllerNumber, value), 0);
}

void MidiPlayerProcessor::prepareNotesForPlayback()
{
    rebuildNoteList();
}

juce::int64 MidiPlayerProcessor::getEndPositionSamples() const
{
    const juce::SpinLock::ScopedLockType lock (notesLock);

    juce::int64 endPosition = 0;

    for (const auto& note : scheduledNotes)
        endPosition = juce::jmax (endPosition, note.endSample);

    return endPosition;
}

void MidiPlayerProcessor::allNotesOff()
{
    // 依頼を立てるだけ。実際の送出は次のprocessBlock()（オーディオスレッド）が行う
    allNotesOffPending.store (true);
}

bool MidiPlayerProcessor::sendAllNotesOff (juce::MidiBuffer& midiMessages)
{
    // ノート一覧を触るのでロックが要る。オーディオスレッドを待たせないためtryLockにし、
    // 取れなければfalseを返して次のブロックへ持ち越す（依頼は下ろさない）。
    const juce::SpinLock::ScopedTryLockType tryLock (notesLock);

    if (! tryLock.isLocked())
        return false;

    for (auto& note : scheduledNotes)
    {
        if (! note.isSounding)
            continue;

        midiMessages.addEvent (juce::MidiMessage::noteOff (1, note.pitch), 0);
        note.isSounding = false;
    }

    // 加えてCC123（All Notes Off）も送る。個別のノートオフだけでは、
    // ピアノロールの試聴で鳴らした音（scheduledNotesに含まれない）が残るため。
    // 逆にCC123を無視するプラグインもあるので、両方送るのが確実。
    midiMessages.addEvent (juce::MidiMessage::allNotesOff (1), 0);

    return true;
}

void MidiPlayerProcessor::addLiveMidiMessage (const juce::MidiMessage& message)
{
    // 8.83：試聴と同じ場所・同じロックで流し込む（Phase 123/改善案⑬）。
    // **MIDIスレッドから呼ばれる**ので、必ずロックを取ること
    //
    // 8.146：**入れ物だけ分けました**（Phase 184/改善案⑬a）。録るのはこちらだけで、
    // ピアノロールの試聴は録りません。流し込む場所は`processBlock()`の同じ1箇所です
    const juce::SpinLock::ScopedLockType lock (previewLock);
    liveInputBuffer.addEvent (message, 0);
}

//==============================================================================
// 8.146：弾いたものを録る（Phase 184／改善案⑬a。仕様書5.4）
//==============================================================================

void MidiPlayerProcessor::startMidiRecording (juce::int64 latencyCompensationSamples)
{
    // **`midiRecordingActive`を立てる前に入れること。** 立ててから入れると、
    // その隙に来たイベントが補正なしで刻まれます
    recordLatencySamples = juce::jmax ((juce::int64) 0, latencyCompensationSamples);

    {
        const juce::SpinLock::ScopedLockType lock (recordLock);

        recordedEvents.clear();

        // **ここで場所を取っておくこと。** オーディオスレッドでの`push_back`が
        // 確保を起こすと音が途切れます（1.15）。`clear()`は容量を返さないので、
        // 2回目以降はこの`reserve()`は何もしません
        recordedEvents.reserve ((size_t) maxRecordedMidiEvents);
    }

    recordOverflowed.store (false);
    midiRecordingActive.store (true);
}

void MidiPlayerProcessor::captureLiveInput (juce::int64 blockStartSamples)
{
    // **待たないこと。** ここはオーディオスレッドで、`previewLock`も持ったままです。
    // 相手（メッセージスレッド）が持っているのは録音の開始と終了の一瞬だけなので、
    // 取れなかったブロックを捨てても実害はありません
    const juce::SpinLock::ScopedTryLockType lock (recordLock);

    if (! lock.isLocked())
        return;

    for (const auto metadata : liveInputBuffer)
    {
        // 3バイトを超えるもの（SysEx等）はノートにもCCにもならないので捨てる
        if (metadata.numBytes < 2 || metadata.numBytes > 3)
            continue;

        // **確保はしない。** 満杯になったら、そこから先は捨てて印だけ立てます
        if (recordedEvents.size() >= recordedEvents.capacity())
        {
            recordOverflowed.store (true);
            return;
        }

        RecordedMidiEvent event;
        event.status = metadata.data[0];
        event.data1 = metadata.data[1];
        event.data2 = (metadata.numBytes >= 3) ? metadata.data[2] : (juce::uint8) 0;
        // **遅れを引いてから刻む**（`startMidiRecording()`の説明）。
        // 曲の頭より前へは戻さない
        event.samplePosition = juce::jmax ((juce::int64) 0,
                                            blockStartSamples + metadata.samplePosition
                                             - recordLatencySamples);

        recordedEvents.push_back (event);
    }
}

std::vector<RecordedMidiEvent> MidiPlayerProcessor::stopMidiRecording (bool& overflowedOut)
{
    // **先に下ろすこと。** 下ろす前に取り出すと、その隙に積まれたぶんが消えます
    midiRecordingActive.store (false);

    overflowedOut = recordOverflowed.load();

    const juce::SpinLock::ScopedLockType lock (recordLock);
    return std::move (recordedEvents);
}

void MidiPlayerProcessor::triggerPreviewNoteOn (int pitch, int velocity)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewBuffer.addEvent (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) velocity), 0);
}

void MidiPlayerProcessor::triggerPreviewNoteOff (int pitch)
{
    const juce::SpinLock::ScopedLockType lock (previewLock);
    previewBuffer.addEvent (juce::MidiMessage::noteOff (1, pitch), 0);
}

void MidiPlayerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();
    midiMessages.clear();

    // ピアノロールでの試聴用イベントと、外から届いたMIDIを先に流し込む。
    // **どちらも同じ場所・同じロック**なので、音の重なり方は入口によらず同じです（8.83）
    {
        const juce::SpinLock::ScopedTryLockType tryLock (previewLock);

        if (tryLock.isLocked())
        {
            if (! previewBuffer.isEmpty())
            {
                midiMessages.addEvents (previewBuffer, 0, -1, 0);
                previewBuffer.clear();
            }

            if (! liveInputBuffer.isEmpty())
            {
                midiMessages.addEvents (liveInputBuffer, 0, -1, 0);

                // 8.146：**録るのは外から届いたぶんだけ**（Phase 184/改善案⑬a）。
                // トランスポートが動いている間だけなので、カウントイン中は入りません
                if (midiRecordingActive.load() && transport.isPlaying())
                    captureLiveInput (transport.getPositionSamples());

                liveInputBuffer.clear();
            }
        }
    }

    const bool playing = transport.isPlaying();

    // 「再生中だったのに止まった」瞬間を、ここで捉える。
    //
    // 終端での自動停止はTransportがオーディオスレッド上で行うため（Transport::advance）、
    // AudioEngine::stop()は呼ばれず、誰もノートオフを送らない。しかも最後のノートは
    // ちょうど終端位置でノートオフになるので、下のノートオフ判定
    // （endSample < blockEnd）から漏れやすい。この2つが重なると、
    // **最後のノートが鳴りっぱなしになる**（実際に発生した不具合）。
    if (wasPlaying && ! playing)
        allNotesOffPending.store (true);

    // 仕様書5.3.3：再生を始めた瞬間を捉えて、CCの追いかけを予約する（Phase 23）。
    // シークしてからPlayを押した場合も、ここを通る。
    if (! wasPlaying && playing)
        ccChasePending.store (true);

    wasPlaying = playing;

    // 仕様書5.9：**再生位置が飛んだ瞬間**を捉える（Phase 48）。
    // ループの折り返しと、再生中のシークの両方がここに当たる。
    //
    // 捉えないと、鳴っている最中のノートの`isSounding`がtrueのまま取り残される。
    // そのノートは**二度とノートオフされず、次の周でも鳴り直さない**
    // （＝鳴りっぱなしになる）。飛んだら全部切って作り直すのが確実。
    //
    // **ロックを取る前に判定すること。** 下のsendAllNotesOff()が同じ
    // notesLockを取るので、ロックの中で呼ぶと自分で自分を待つことになる
    // （juce::SpinLockは再入可能ではない）。
    if (playing)
    {
        const juce::int64 blockStart = transport.getPositionSamples();

        if (lastBlockEndSamples >= 0 && blockStart != lastBlockEndSamples)
        {
            allNotesOffPending.store (true);
            ccChasePending.store (true);   // 飛んだ先のCCの値に追いつく必要がある（5.3.3）

            // 8.114：**ループの折り返しで飛び越した区間を覚えておく**（Phase 150）。
            //
            // `Transport::advance()`は行き過ぎたぶんを足した位置へ戻します
            // （`loopStart + 行き過ぎ`）。つまり**区間`[loopStart, blockStart)`は
            // 誰も再生しません**——**ループ先頭ちょうどのノート（＝1発目）が
            // 毎周落ちて**いました（本人の報告）。1周目だけ鳴るのは、
            // `Transport::start()`が位置をloopStartちょうどに置くからです。
            //
            // 折り返しの見分け方：**前のブロックがループ終端を越えていて、
            // 新しい位置がループ先頭の直後**。ただのシークと区別できます
            if (transport.isLoopEnabled())
            {
                const auto loopStart = transport.getLoopStartSamples();

                if (blockStart >= loopStart && blockStart < loopStart + buffer.getNumSamples()
                     && lastBlockEndSamples >= transport.getLoopEndSamples())
                    loopCatchUpFromSample = loopStart;
            }
        }

        lastBlockEndSamples = blockStart + buffer.getNumSamples();
    }
    else
    {
        lastBlockEndSamples = -1;   // 止まっている間は連続性を見ない
        loopCatchUpFromSample = -1;  // 8.114：止まったら遡りの予約も捨てる
    }

    // 送り終えたときだけ依頼を下ろす。ロックが取れなかった場合は次のブロックで再試行する
    // （sendAllNotesOff()が、鳴っていたノートのisSoundingも下ろしてくれる）
    if (allNotesOffPending.load() && sendAllNotesOff (midiMessages))
        allNotesOffPending.store (false);

    // 8.116：**行き先が無くなった音を消す**（Phase 151）。
    // 再生中に一覧を組み直したとき、消された・動かされたノートのぶんがここへ来ます。
    // **`allNotesOffPending`とは別物**——あちらは全部止める、こちらは無くなったぶんだけ
    {
        const juce::SpinLock::ScopedTryLockType offLock (notesLock);

        if (offLock.isLocked() && ! pendingNoteOffPitches.empty())
        {
            for (int pitch : pendingNoteOffPitches)
                midiMessages.addEvent (juce::MidiMessage::noteOff (1, pitch), 0);

            pendingNoteOffPitches.clear();
        }
    }

    if (! playing)
        return;

    const juce::SpinLock::ScopedTryLockType tryLock (notesLock);
    if (! tryLock.isLocked())
        return; // ノート一覧の更新中はこのブロックを飛ばす（オーディオスレッドを待たせない）

    // 再生位置は共有のTransportから読む（自分では進めない。Transport.h参照）
    const juce::int64 blockStart = transport.getPositionSamples();
    const int numSamples = buffer.getNumSamples();
    const juce::int64 blockEnd = blockStart + numSamples;

    // 仕様書5.3.3：CCの追いかけは、通常のイベント送出より先に行う（Phase 23）。
    // 順序が逆だと、このブロック内にあるイベントを追いかけの値が上書きしてしまう。
    if (ccChasePending.load())
    {
        sendCCChase (midiMessages, blockStart);
        ccChasePending.store (false);
    }

    // 仕様書5.3.3：このブロック内に来るCCイベントを送る。
    // ノートと違い「送信済みフラグ」を持たないのは、区間で判定すれば
    // 二重送出も送り漏れも起きないため（シークで戻った場合は追いかけが面倒を見る）。
    for (const auto& cc : scheduledCCs)
    {
        if (cc.sample < blockStart)
            continue;

        if (cc.sample >= blockEnd)
            break; // 時刻順に並んでいるので、以降は全部このブロックより後

        midiMessages.addEvent (MidiCCMessage::create (1, cc.controllerNumber, cc.value),
                                (int) (cc.sample - blockStart));
    }

    // 8.114：**折り返しで飛び越した区間まで遡って探す**（Phase 150）。
    // 普段は`blockStart`と同じなので、ループしていないときの動きは変わりません
    const juce::int64 noteSearchStart = (loopCatchUpFromSample >= 0) ? loopCatchUpFromSample
                                                                    : blockStart;

    for (auto& note : scheduledNotes)
    {
        // このブロック内でノートオンのタイミングが来るか。
        // **飛び越した区間のぶんは、このブロックの頭（offset 0）で鳴らす**（8.114）。
        // `endSample > blockStart`を見るのは、**飛び越した区間の中で鳴り終わっている
        // 短いノート**を鳴らしっぱなしにしないため（ブロックより短いノート）
        if (! note.isSounding && note.startSample >= noteSearchStart && note.startSample < blockEnd
             && note.endSample > blockStart)
        {
            const int offset = juce::jmax (0, (int) (note.startSample - blockStart));
            midiMessages.addEvent (juce::MidiMessage::noteOn (1, note.pitch, (juce::uint8) note.velocity), offset);
            note.isSounding = true;
        }

        // このブロック内でノートオフのタイミングが来るか
        if (note.isSounding && note.endSample >= blockStart && note.endSample < blockEnd)
        {
            const int offset = (int) (note.endSample - blockStart);
            midiMessages.addEvent (juce::MidiMessage::noteOff (1, note.pitch), offset);
            note.isSounding = false;
        }
    }

    // 8.114：**1ブロックぶんだけの効き目**（Phase 150）。使ったらすぐ下ろすこと——
    // 下ろし忘れると、以後ずっとループ先頭まで遡って探し続けます
    loopCatchUpFromSample = -1;

    // 位置を進めるのも自動停止の判断も、共有のTransportが行う（設計書1.5、Transport.h）
}

const juce::String MidiPlayerProcessor::getName() const     { return "MidiPlayer"; }
double MidiPlayerProcessor::getTailLengthSeconds() const     { return 0.0; }
bool MidiPlayerProcessor::acceptsMidi() const                { return false; }
bool MidiPlayerProcessor::producesMidi() const                { return true; }

juce::AudioProcessorEditor* MidiPlayerProcessor::createEditor() { return nullptr; }
bool MidiPlayerProcessor::hasEditor() const                      { return false; }

int MidiPlayerProcessor::getNumPrograms()                          { return 1; }
int MidiPlayerProcessor::getCurrentProgram()                       { return 0; }
void MidiPlayerProcessor::setCurrentProgram (int)                  {}
const juce::String MidiPlayerProcessor::getProgramName (int)       { return {}; }
void MidiPlayerProcessor::changeProgramName (int, const juce::String&) {}

void MidiPlayerProcessor::getStateInformation (juce::MemoryBlock&) {}
void MidiPlayerProcessor::setStateInformation (const void*, int)   {}
