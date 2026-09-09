#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ProjectModel.h"
#include "Transport.h"
#include "MidiRecording.h"

//==============================================================================
/**
    仕様書5.3・設計書1.5に対応する、MIDIノートの再生用プロセッサ。

    **Phase 14で「1トラックにつき1つ」になった。** それ以前は1つのプロセッサが
    全MIDIトラックのノートをまとめて出力しており、音源もプロジェクト全体で1台しか
    持てなかった（トラックごとに別の音源を鳴らせなかった）。現在は
    ClipPlayerProcessor（Phase 12c）と同じく、trackIdで担当トラックを限定する。

    trackIdで指定したトラックのMIDIクリップのノートを、共有の再生位置
    （プレイヘッド）に応じたMIDIイベントとして生成する。
    生成したMIDIは、後段に接続されたインストゥルメントプラグインへ渡される。

    このプロセッサ自体は音を出さない（MIDIを生成するだけ）。
    実際の発音は、後段のインストゥルメントプラグインが行う。

    現時点での既知の簡略化：
    - Play開始時にノート一覧を読み込み直す方式のため、再生中にノートを
      追加・削除しても次にPlayを押すまでは反映されない
    - 出力するMIDIチャンネルは常に1ch（トラックごとに音源が分かれるため、
      1台の音源をチャンネルで鳴らし分けるマルチティンバー運用は対象外）
*/
class MidiPlayerProcessor : public juce::AudioProcessor
{
public:
    /** trackIdで指定したトラックのノートだけを再生する。 */
    MidiPlayerProcessor (ProjectModel& projectToUse, Transport& transportToUse, juce::String trackIdToPlay);

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    /** 現在のノート一覧を読み込み直す（再生開始時に呼ぶ）。
        再生位置は持たず、共有のTransportから読む（Phase 12c）。 */
    void prepareNotesForPlayback();

    /** 停止時に、鳴りっぱなしのノートを止める。

        **実際のノートオフは次のprocessBlock()で送る。** ここで直接MIDIを送れないのは、
        この関数がメッセージスレッドから呼ばれるのに対し、MIDIの出力先である
        `midiMessages`はオーディオスレッドしか触れないため。
        なお停止操作を経ずに終端で自動停止した場合も、processBlock()側が
        再生の終わりを検知して同じ処理を行う（Phase 14a-fix）。 */
    void allNotesOff();

    /** ノートが終わる位置（サンプル）。自動停止位置の計算に使う。 */
    juce::int64 getEndPositionSamples() const;


    /** ピアノロールでの試聴用に、1つのノートを即座に鳴らす／止める。 */
    void triggerPreviewNoteOn (int pitch, int velocity);
    void triggerPreviewNoteOff (int pitch);

    /** 8.83：**外から届いたMIDIをそのまま流す**（Phase 123/改善案⑬。仕様書5.4）。

        鍵盤やパッドからのメッセージを、このトラックの音源へ渡します。
        ノートだけでなく**CC・ピッチベンドもそのまま**通すので、
        モジュレーションホイールやサステインペダルも効きます。

        **試聴（`triggerPreviewNoteOn`）と同じ入れ物へ入れます。**
        別に持つと、オーディオスレッドが2つのキューを見ることになり、
        **同じ音の重なり方が入口ごとに変わります**（8.2）。

        **MIDIスレッドから呼ばれます**（メッセージスレッドでもオーディオスレッドでもない）。
        入れ物はスピンロックで守ってあるので、そのまま呼んで構いません。 */
    void addLiveMidiMessage (const juce::MidiMessage& message);

    //==========================================================================
    // 8.146：**弾いたものを録る**（Phase 184／改善案⑬a。仕様書5.4）

    /** 外から届いたMIDIを溜め始める。**メッセージスレッドから**呼ぶこと。

        溜めるのは`addLiveMidiMessage()`で来たものだけです——
        **ピアノロールの試聴は録りません**（音を確かめるために押した鍵が
        ノートになって残るのは、まず望まれない）。

        `Transport`が動いている間だけ溜めます。カウントイン中はまだ動いていないので、
        **数えている間に弾いた音は入りません**（8.85のカウントインと同じ扱い）。

        ### `latencyCompensationSamples`（遅れの引き算）

        **弾いた音は、そのままだと必ず後ろへずれます。**

        オーディオスレッドが位置`P`のブロックを作っているとき、
        **耳に届いているのは`P - 出力レイテンシ`のあたり**です（まだ鳴っていない音を
        作っている最中なので）。弾く人はその聞こえている音に合わせて叩くので、
        届いたメッセージを`P`へ置くと、**出力レイテンシぶん遅れて刻まれます**。
        バッファ256サンプルのWindows Audioで20ms前後——**十分に聞いて分かる量**です。

        だから**引いた位置へ置きます**。渡す値は`AudioEngine`が決めます（1.27）。 */
    void startMidiRecording (juce::int64 latencyCompensationSamples);

    /** 溜めたものを取り出して、録るのをやめる。**メッセージスレッドから**呼ぶこと。

        `overflowedOut`には、入れ物が満杯になって捨てたぶんがあるかを入れます
        （`maxRecordedMidiEvents`）。 */
    std::vector<RecordedMidiEvent> stopMidiRecording (bool& overflowedOut);

    bool isMidiRecording() const { return midiRecordingActive.load(); }

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
    struct ScheduledNote
    {
        int pitch = 60;
        int velocity = 100;
        juce::int64 startSample = 0;
        juce::int64 endSample = 0;
        bool isSounding = false; // ノートオンを送信済みか
    };

    /** 仕様書5.3.3：CCの点と点のあいだを埋める刻み（秒。Phase 76／8.36）。

        **細かくするほど滑らかになりますが、MIDIのメッセージ数が増えます。**
        20msは「フェードやスイープが階段に聞こえない」程度の目安で、
        値が変わらない刻みは足さないので、緩やかな変化では実際の数はもっと少なくなります。 */
    static constexpr double ccInterpolationStep = 0.02;

    /** 仕様書5.3.3：CCイベント1つぶん（Phase 23）。
        ノートと違い「始まりと終わり」が無く、その時刻に1回送るだけ。 */
    struct ScheduledCC
    {
        int controllerNumber = 1;
        int value = 0;
        juce::int64 sample = 0;
    };

    void rebuildNoteList();

    /** 8.146：`liveInputBuffer`の中身を`recordedEvents`へ写す（Phase 184）。

        **オーディオスレッドから、`previewLock`を持ったまま呼ばれます。**
        中で確保も待ちもしないこと（`recordLock`は`try`で取り、
        取れなければそのブロックは捨てます——ここで待つと音が途切れます）。 */
    void captureLiveInput (juce::int64 blockStartSamples);

    /** 仕様書5.3.3：再生開始位置での各コントローラーの値を送る（Phase 23）。

        **途中から再生したときに、そこまでのCCが反映されていないと困る**ため。
        例えば曲の後半でエクスプレッションを絞っていても、そこから再生を始めると
        「まだ何も送られていない＝初期値」の音で鳴ってしまう。
        多くのDAWが行っている「イベントの追いかけ（chase）」に相当する。 */
    void sendCCChase (juce::MidiBuffer& midiMessages, juce::int64 positionSamples);

    /** 鳴っているノートのノートオフを`midiMessages`へ積む。
        ノート一覧のロックを取れなかった場合はfalseを返す（呼び出し側が次のブロックで再試行する）。 */
    bool sendAllNotesOff (juce::MidiBuffer& midiMessages);

    ProjectModel& project;
    Transport& transport;
    const juce::String trackId;

    std::vector<ScheduledNote> scheduledNotes;

    // 仕様書5.3.3：CCイベント（Phase 23）。時刻（サンプル）の昇順で並んでいる。
    // ノート一覧と同じロックで守る（同じタイミングで作り直すため）。
    std::vector<ScheduledCC> scheduledCCs;

    juce::SpinLock notesLock; // rebuildNoteList（メッセージスレッド）とprocessBlock（オーディオスレッド）の保護

    double currentSampleRate = 44100.0;

    // 「鳴っているノートを止めてほしい」という依頼。停止操作（メッセージスレッド）と
    // 終端での自動停止の検知（オーディオスレッド）の両方から立つため、atomicで持つ。
    // 実際の送出はprocessBlock()が行い、送り終えたら下ろす。
    std::atomic<bool> allNotesOffPending { false };

    // 直前のブロックで再生中だったか（オーディオスレッドからのみ触る）。
    // 「再生中 → 停止」へ変わった瞬間を捉えるために持つ。終端での自動停止は
    // Transportがオーディオスレッド上で行うため、これが無いと誰も気づけない。
    bool wasPlaying = false;

    /** 仕様書5.9：前のブロックが終わった位置（Phase 48）。

        次のブロックの先頭がここと違えば、**再生位置が飛んだ**ということ
        （ループの折り返し、または再生中のシーク）。-1は「止まっている」。 */
    juce::int64 lastBlockEndSamples = -1;

    /** 8.114：**ループの折り返しで飛び越した区間の先頭**（Phase 150）。無ければ-1。

        `Transport::advance()`は`loopStart + 行き過ぎたぶん`へ戻すので、
        **区間`[loopStart, blockStart)`は誰も再生しません**。
        **ループ先頭ちょうどのノート（＝1発目）が毎周落ちて**いました。

        ここを覚えておいて、そのブロックだけ探す範囲を広げます。
        **1ブロックぶんだけ有効**で、使ったらすぐ-1へ戻します。 */
    juce::int64 loopCatchUpFromSample = -1;

    /** 8.116：**行き先が無くなった音のノートオフ待ち**（Phase 151）。

        再生中にノート一覧を組み直すと、鳴っていた音の一部は新しい一覧に
        居場所がありません（消された・動かされた）。そのままだと**鳴りっぱなし**なので、
        ここへ積んで、次のブロックの頭で送ります。

        **`notesLock`で守ること**——積むのはメッセージスレッド、
        送るのはオーディオスレッドです。 */
    std::vector<int> pendingNoteOffPitches;

    // 仕様書5.3.3：再生を始めた最初のブロックでCCの追いかけを行う（sendCCChase参照）。
    // 「停止→再生」への変化はwasPlayingで捉える。
    //
    // 8.116：**atomicにしました**（Phase 151）。再生中にCCを編集したときも
    // 追いかけ直したいので、**メッセージスレッドからも立てます**
    std::atomic<bool> ccChasePending { false };

    // ピアノロールでの試聴用。オーディオスレッドへ安全に渡すためのキュー
    juce::MidiBuffer previewBuffer;
    juce::SpinLock previewLock;

    /** 8.146：**外から届いたぶんだけ**の入れ物（Phase 184／改善案⑬a）。

        試聴（`previewBuffer`）と分けてあるのは、**録るのはこちらだけ**だからです。
        1つにまとめると、ピアノロールで音を確かめるために押した鍵まで
        ノートになって残ります。

        **流し込むのは`previewBuffer`と同じ場所・同じロック**なので、
        8.83の「オーディオスレッドが2つのキューを見る」ことにはなりません
        （音の重なり方は入口によらず同じ）。`previewLock`で守ります。 */
    juce::MidiBuffer liveInputBuffer;

    /** 溜めたイベント。**`recordLock`で守ること**——
        積むのはオーディオスレッド、取り出すのはメッセージスレッドです。

        **`startMidiRecording()`で先に場所を確保します。**
        オーディオスレッドで`push_back`が確保を起こすと音が途切れます（1.15）。 */
    std::vector<RecordedMidiEvent> recordedEvents;
    juce::SpinLock recordLock;

    std::atomic<bool> midiRecordingActive { false };
    std::atomic<bool> recordOverflowed { false };

    /** 録るときに引く遅れ（サンプル）。`startMidiRecording()`の説明を参照。
        **録音中は変わらない**ので、atomicにはしていません
        （立てるのは`midiRecordingActive`を立てる前）。 */
    juce::int64 recordLatencySamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPlayerProcessor)
};
