#include "AudioEngine.h"
#include "Utf8.h"
#include "Mp3Writer.h"   // 8.153：MP3の書き出し（Phase 191／D9b）
#include "AppSettings.h"        // 8.85：オーディオ・MIDIデバイスの設定を覚える（Phase 125）
#include "AppColours.h"          // 8.85：レイテンシ表示の色（Phase 125）
#include "StorageLocations.h"   // 設計書2.3.8：録音ファイルの保存先（Phase 57）
#include "Plugins/MantaPluginFormat.h"   // 9.5：内蔵プラグイン（Phase 204）
#include "IconAssets.h"          // 8.171：帯のボタンの絵（Phase 210）

namespace
{
    /** 8.85：オーディオ／MIDIデバイスの設定の保存先（Phase 125/設計書2.5）。

        `juce::AudioDeviceManager`が**XMLで丸ごと**出し入れできるので、そのまま持ちます。
        中身はドライバ種別・入出力デバイス名・サンプルレート・**バッファサイズ**、
        そして**有効にしたMIDI入力**です。 */
    const juce::String audioDeviceStateKey { "audioDeviceState" };

    /** 8.168：プラグインの窓を前面に留めるか（Phase 206／本人の質問）。
        **次に開く窓の既定**です（既に開いている窓は変わりません）。 */
    const juce::String alwaysOnTopKey { "pluginWindowsAlwaysOnTop" };
}

AudioEngine::AudioEngine (ProjectModel& projectToUse)
    : project (projectToUse)
{
}

AudioEngine::~AudioEngine()
{
    // 明示的にプラグインを先に取り除く。VST3のホスティング基盤（pluginManager配下）が
    // まだ生きているうちにプラグインインスタンスを破棄しないと、内部のCOM参照が
    // 正しく解放されず、シャットダウン時にJUCEのLeakedObjectDetectorに
    // 引っかかることがある（メンバーの自動破棄順序だけに頼らない）。
    //
    // トラック側のプラグイン（音源・インサート）は、プラグイン本体（graphが所有）より
    // 先にエディタを閉じ、ノードを外しておく。trackNodesはgraphより後に破棄されるメンバーなので、
    // ここで片付けないとエディタが死んだプラグインを掴んだままになる。
    // 8.83：**MIDIの受け取りを先に外す**（Phase 123）。
    // 残したままトラックを片付けると、MIDIスレッドが消えたノードを掴みます（1.15）
    deviceManager.removeMidiInputDeviceCallback ({}, this);

    {
        const juce::ScopedLock lock (midiInputTargetLock);
        midiInputTargets.clear();
    }

    for (auto& nodes : trackNodes)
        removeTrackNodesFromGraph (nodes);

    trackNodes.clear();

    // 録音中に終了された場合でも、ここでファイルを正しく閉じる
    stopRecording();

    transport.stop();

    // 破棄後に通知が届かないよう、購読を先に外す
    stopTimer();
    latencyPollTimer.stopTimer();
    cancelPendingUpdate();
    subscribedState.removeListener (this);
    project.onStateReplaced = nullptr;

    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
}

juce::String AudioEngine::initialise()
{
    // 重要：グラフ自身のバスを有効化してから配線しないと、
    // audioOutputNodeのチャンネル数が実質0のままになり、
    // 後述のaddConnection()がエラーも出さずに静かに失敗する
    // （JUCE公式チュートリアル「Cascading plug-in effects」の標準手順）。
    graph.enableAllBuses();

    // 仕様書5.4：録音のため、入力2ch・出力2chで開く。
    // ただし入力デバイスが無い／他アプリに専有されている環境は普通にあり得るので、
    // 失敗しても諦めずに「出力のみ」でもう一度開き直す。ここで打ち切ってしまうと、
    // 入力が無いだけで再生機能まで一切使えなくなってしまうため。
    // 8.85：**前回の設定に戻す**（Phase 125/設計書2.5）。
    //
    // Phase 124まで`initialiseWithDefaultDevices()`だったので、
    // **起動するたびに既定のデバイスへ戻って**いました。そのため
    //   - 環境設定で有効にしたMIDI入力が、次に開くと外れている
    //   - **バッファサイズを小さくしても、次の起動で元へ戻る**（レイテンシが戻る）
    // という状態でした。
    //
    // `AudioDeviceManager`はこの一式をXMLで丸ごと出し入れできるので、そのまま覚えます。
    // **第4引数をtrue**にしておくと、覚えていたデバイスが見つからないとき
    // （挿していない等）に既定のデバイスへ落ちてくれます
    auto savedState = juce::parseXML (AppSettings::getString (audioDeviceStateKey));

    auto error = deviceManager.initialise (2, 2, savedState.get(), true);

    if (error.isEmpty())
    {
        inputAvailable = true;
    }
    else
    {
        inputError = error;

        auto outputOnlyError = deviceManager.initialise (0, 2, savedState.get(), true);
        if (outputOnlyError.isNotEmpty())
            return outputOnlyError; // 出力すら開けない場合は本当にエラー
    }

    // 先にコールバックを登録する（この時点ではplayerにまだprocessorが無いので無音のまま）。
    deviceManager.addAudioCallback (&player);

    using AudioGraphIOProcessor = juce::AudioProcessorGraph::AudioGraphIOProcessor;

    outputNode = graph.addNode (std::make_unique<AudioGraphIOProcessor> (AudioGraphIOProcessor::audioOutputNode));

    // 仕様書5.7：マスターチャンネル。すべての音はここを通ってから出力へ向かう。
    // 出力ノードの直前に1枚挟むことで、マスター音量と最終出力レベルを一箇所で扱える。
    auto master = std::make_unique<MasterChannelProcessor> (transport);
    masterProcessor = master.get();
    masterNode = graph.addNode (std::move (master));

    // MIDI再生用ノード（仕様書5.3）はトラックごとに作る（Phase 14）。
    // rebuildTrackNodes()の中でMidiPlayerProcessorと音源を組み立てるため、ここでは作らない。

    // 仕様書5.4：入力 → RecorderProcessor → 出力 の経路を作る。
    // 入力ノードは「起動時に入力が使えたかどうか」に関わらず必ず作る。
    // 起動後にオーディオ設定でUSBマイク等を選び直せるようにするためで、
    // 実際の配線（と入力の有無の判定）はupdateInputConnections()が担当する。
    inputNode = graph.addNode (std::make_unique<AudioGraphIOProcessor> (AudioGraphIOProcessor::audioInputNode));

    auto recorder = std::make_unique<RecorderProcessor> (transport);
    recorderProcessor = recorder.get();
    recorderNode = graph.addNode (std::move (recorder));

    // メトロノーム（Phase 38）。
    // マスターの手前に繋ぐので、マスター音量の影響を受ける（クリックだけ爆音、を防ぐ）。
    // **書き出しのときは止めること**（renderMixdownToFile参照）。
    auto metronome = std::make_unique<MetronomeProcessor> (transport);
    metronomeProcessor = metronome.get();
    metronomeNode = graph.addNode (std::move (metronome));

    // 8.69：**録音モニター・メトロノーム・マスター→出力の配線は`connectMasterChain()`が張る**
    // （Phase 108／D6）。ここで直に張っていたぶんを寄せたもので、
    // **マスターにインサートを挿したときも同じ経路をたどる**ようにするため。
    //
    // Recorder自身がモニタリングOFF時にバッファをクリアするので、
    // 常時繋いだままでもハウリングの心配はない（RecorderProcessor.cpp参照）
    connectMasterChain();

    // 入力 → Recorder の配線と、入力が実際に使えるかの判定はここで行う。
    updateInputConnections();

    // 保存されているマスター音量を反映する
    updateMixerSettings();

    // オーディオ設定でデバイスを変更したときに、入力の配線を張り直せるよう購読する
    // （デバイスが変わると入力チャンネル数も変わるため）。
    deviceManager.addChangeListener (this);

    // 8.85：**開けた時点の設定を1回書いておく**（Phase 125）。
    // 購読を始めるのがここなので、起動時に開いたぶんは通知として来ません。
    // 書いておかないと、**何も触らずに終了した回は設定ファイルが空のまま**になります
    saveAudioDeviceState();

    // 8.83：**有効になっている全MIDI入力から受け取る**（Phase 123/改善案⑬）。
    // 識別子を空にすると「今後有効になるものも含めて全部」の意味になります
    // （デバイスを挿し直すたびに登録し直さずに済む）
    deviceManager.addMidiInputDeviceCallback ({}, this);

    // 設計書1.5：トラックごとのノード列を組み立てる。
    // 以降、トラックが増減したらValueTreeの通知を受けて自動で作り直す。
    subscribedState = project.getState();
    subscribedState.addListener (this);
    rebuildTrackNodes();

    // プロジェクトを読み込むとルートのValueTree自体が差し替わるため、購読し直す。
    // これを忘れると、以降トラックを追加してもノードが作られない。
    project.onStateReplaced = [this]
    {
        subscribedState.removeListener (this); // 古いツリーの購読を外してから
        subscribedState = project.getState();
        subscribedState.addListener (this);

        triggerFullRebuild();   // プロジェクトそのものが差し替わった＝全部作り直す
    };

    // 最後にグラフをplayerへセットする。ここでprepareToPlayが走り、実際に再生対象になる。
    player.setProcessor (&graph);

    // 仕様書5.7.1：プラグインのレイテンシ変化を見張る（Phase 12e）。
    // 再生中かどうかに関わらず必要なので、オートメーション用のタイマーとは別に回す
    // （プラグインのGUIでオーバーサンプリング倍率を変えるのは、たいてい停止中のため）。
    lastKnownPluginLatency = collectPluginLatencies();
    latencyPollTimer.startTimer (latencyPollIntervalMs);

    return {};
}

//==============================================================================
// 設計書1.5：トラックごとのノード構成
//==============================================================================
/** 8.116：そのノードが「再生中の音そのもの」を決めるか（Phase 151）。

    ノート・CC・オートメーションの点・オーディオクリップ。
    **どれもオーディオスレッドが読む表の材料**なので、変わったら写し直しが要ります。

    **ミキサーの値（音量・パン・ミュート）は含めません**——あちらは
    `updateMixerSettings()`が別に配っていて、表を作り直す必要がありません。 */
static bool affectsPlaybackData (const juce::ValueTree& tree)
{
    return tree.hasType (IDs::NOTE)  || tree.hasType (IDs::CC)
        || tree.hasType (IDs::POINT) || tree.hasType (IDs::AUTOMATION)
        || tree.hasType (IDs::AUDIOCLIP) || tree.hasType (IDs::MIDICLIP)
        || tree.hasType (IDs::NOTES) || tree.hasType (IDs::CCEVENTS)
        || tree.hasType (IDs::CLIPS)
        // 8.150：ワープマーカー（Phase 188／8.48）
        || tree.hasType (IDs::WARP) || tree.hasType (IDs::WARPMARKER);
}



void AudioEngine::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // 8.41：**ミキサーに関わる値が変わったら、エンジンへ反映する**（Phase 81）。
    // 8.116：**再生中の編集を、止めずに反映する**（Phase 151／本人の要望）。
    // ノート・CC・オートメーション点・クリップの中身が変わったら、写し直しを予約する。
    // **止まっているときは何もしません**（`triggerPlaybackRefresh()`が判断）
    if (affectsPlaybackData (tree))
        triggerPlaybackRefresh();

    // 8.162：**テンポ・拍子も再生データに効く**（Phase 200／本人の要望）。
    // 判定は`ProjectIds.h`の1本（メトロノーム側と同じものを使う。8.2）
    if (::isTempoMapChange (tree, property))
        handleTempoMapChanged();

    //
    // Phase 80まではConsoleとインスペクタだけが`updateMixerSettings()`を呼んでいたので、
    // **アレンジ画面のヘッダーでソロ／ミュートを押しても音が変わりません**でした。
    // **Undo／Redoでも同じ**（誰も呼ばないため）。
    //
    // **入口ごとに呼ぶのをやめて、モデルを見て追う形にしてあります**（1.15）。
    // こうしておけば、後から別の入口が増えても勝手に効きます。
    // 8.51：**入っているフォルダが変わったら、配線をやり直す**（Phase 90／D2）。
    // 出口（マスターか、フォルダか）が変わるので、ミキサー設定の更新だけでは足りません。
    // **並べ替えを伴わないこともある**（「フォルダから出す」はその場に残す）ので、
    // 行の入れ替えの通知（`valueTreeChildOrderChanged`）頼みにはできません
    if (property == IDs::trackParentFolder)
    {
        // 8.110：**配線だけで足ります**（Phase 146）。出口はtrackIdで引いているので、
        // フォルダを出入りしてもノードの中身は変わりません
        triggerRewireOnly();
        return;
    }

    if (! tree.hasType (IDs::TRACK) && ! tree.hasType (IDs::MASTERBUS) && ! tree.hasType (IDs::SEND))
        return;

    if (property == IDs::mute || property == IDs::solo
         || property == IDs::volume || property == IDs::pan
         || property == IDs::sendLevel || property == IDs::vcaLinkedTrackIds)
    {
        updateMixerSettings();
        return;
    }

    // 8.83：**録音待機が変わったら、鍵盤の行き先も洗い直す**（Phase 123/改善案⑬）。
    // アームしたトラックが鳴る先になるので、ここを拾わないと
    // **アームしたのに前のトラックが鳴り続けます**
    // 8.84：入力デバイス・チャンネルの設定も、変わったら写し直す（Phase 124/改善案⑯）
    if (property == IDs::armed
         || property == IDs::midiInputDevice
         || property == IDs::midiInputChannel
         || property == IDs::midiOutputChannel)
        refreshMidiInputTargets();
}

void AudioEngine::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (IDs::TRACK))
        triggerFullRebuild();   // トラックが増えた＝ノードを作る

    // 8.116：**ノートを1つ足した／消したときも写し直す**（Phase 151）
    if (affectsPlaybackData (child))
        triggerPlaybackRefresh();

    // 8.162：**変化点の出し入れも同じ**（Phase 200）。テンポの札は「足す・消す」が
    // 主な操作なので、値の変更（`valueTreePropertyChanged`）だけでは取りこぼす
    if (::isSignatureLaneNode (child))
        handleTempoMapChanged();
}

void AudioEngine::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (IDs::TRACK))
        triggerFullRebuild();   // トラックが減った＝ノードを片付ける

    if (affectsPlaybackData (child))
        triggerPlaybackRefresh();

    // 8.162：変化点を消したときも（Phase 200）
    if (::isSignatureLaneNode (child))
        handleTempoMapChanged();
}

void AudioEngine::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // 8.110：**並び替えは配線だけ**（Phase 146）。ノードの中身は1つも変わりません——
    // 出口もセンドもサイドチェインも**trackIdで引いている**ので、行の順番に依りません。
    //
    // **ここが「並び替えの反映に9秒」の正体でした。** 1本動かすたびに
    // 全トラックのプラグインを破棄して読み込み直していました（1.8）
    if (parent.hasType (IDs::TRACKS))
        triggerRewireOnly();
}

void AudioEngine::triggerFullRebuild()
{
    // **ノードを作り直す＝プラグインの読み込み直し。** 重いので、
    // 本当に構成が変わったときだけ（トラックの増減・プロジェクトの差し替え）
    pendingFullRebuild = true;
    triggerAsyncUpdate();
}

void AudioEngine::triggerRewireOnly()
{
    // **配線だけ。** プラグインには触らないので、音作りもGUIウィンドウも残ります
    pendingRewire = true;
    triggerAsyncUpdate();
}

void AudioEngine::triggerPlaybackRefresh()
{
    // 8.116：**再生中の編集を、止めずに反映する**（Phase 151／本人の要望）。
    //
    // ノート・CC・オートメーション・クリップは、オーディオスレッドが読めるよう
    // **再生の前に写し取った表**で鳴っています（1.12）。写し取りは
    // `prepareTrackPlayersForPlayback()`＝**Playを押した瞬間にしか走っていません**
    // でした。だから**編集しても、いったん止めて鳴らし直すまで反映されません**でした。
    //
    // **止まっているときは何もしません**（次のPlayが写し取るので無駄）
    if (! transport.isPlaying())
        return;

    pendingPlaybackRefresh = true;
    triggerAsyncUpdate();
}

void AudioEngine::handleTempoMapChanged()
{
    // 8.162：**再生中にテンポを変えたら、その場で効く**（Phase 200／本人の要望）。
    //
    // 本人の言葉は「再生中にBPMを変えても、反映されるのは次の再生時なので、
    // できるのならリアルタイムでも変更が効くようにできるかな？」。
    //
    // ノートもCCもオートメーションも、**曲の秒**に置き換えてから写し取っています
    // （1.12）。テンポが変わると、同じ拍が別の秒になるので、
    // **写し取った表を作り直さないかぎり、前のテンポのまま鳴り続けます。**
    // 作り直す道は前からあります（`triggerPlaybackRefresh()`／Phase 151）——
    // **テンポの変更がその道に繋がっていなかっただけ**でした。
    const auto newMap = project.getTempoMap();
    const double sampleRate = transport.getSampleRate();

    if (transport.isPlaying() && sampleRate > 0.0)
    {
        // **拍の位置を保つこと。** 秒のまま置いていくと、テンポを上げた瞬間に
        // 曲の別の場所へ飛びます（2小節目にいたはずが4小節目、という形で出る）。
        // 変える**前**の表で「いま何拍目か」を測り、新しい表で秒へ戻します
        const double currentSeconds = (double) transport.getPositionSamples() / sampleRate;
        const double beat = lastKnownTempoMap.getBeatAtTime (currentSeconds);

        setPlayheadSeconds (newMap.getTimeForBeat (beat));
    }

    lastKnownTempoMap = newMap;
    triggerPlaybackRefresh();
}

void AudioEngine::handleAsyncUpdate()
{
    // 連続して飛んでくる変更通知を1回にまとめるためのクッション。
    // 例えばプロジェクト読み込みでは、トラックの数だけ通知が来る。
    // **ノートを1つ動かすだけでも通知は何度も飛びます**ので、まとめる意味は大きい。
    //
    // 8.110：**重い順に3つ**（Phase 146・151）。上のものは下のものを含みます：
    //   作り直し（プラグイン読み込み直し）> 配線の張り直し > 再生データの写し直し
    if (pendingFullRebuild)
    {
        pendingFullRebuild = false;
        pendingRewire = false;
        pendingPlaybackRefresh = false;   // rebuildTrackNodes()が再生中なら写し直す

        rebuildTrackNodes();
        return;
    }

    if (pendingRewire)
    {
        pendingRewire = false;
        rewireAllTrackConnections();
    }

    if (pendingPlaybackRefresh)
    {
        pendingPlaybackRefresh = false;

        // **止まっていたら要りません**（次のPlayが写し取ります）。
        // 終端はここでは触らない——再生中に自動停止位置を動かすと、
        // 編集しただけで曲が止まる／伸びることになります
        if (transport.isPlaying())
        {
            juce::int64 endPositionSamples = 0;
            prepareTrackPlayersForPlayback (endPositionSamples);
        }
    }
}

void AudioEngine::flushPendingTrackRebuild()
{
    // 先送りされている再構築があれば、ここで済ませてしまう。
    // 何も溜まっていなければ何も起きない（余分な作り直しは走らない）。
    handleUpdateNowIfNeeded();
}

void AudioEngine::rebuildTrackNodes()
{
    if (masterNode == nullptr)
        return; // まだinitialise()が済んでいない

    // 8.111：**差分更新**（Phase 147）。
    //
    // Phase 146までは、トラックを1本足すだけで**全トラックのノードを破棄して
    // 作り直して**いました。プラグインの読み込み直しが伴うので、
    // **追加も削除も9秒近くかかっていました**（1.8に既知として記載）。
    //
    // ここが呼ばれるのは**トラックの増減とプロジェクトの差し替えだけ**です
    // （インサートや音源の差し替えは専用の関数が直に触る。並び替えは配線だけ。8.110）。
    // つまり**残るトラックのプラグイン構成は変わりません**——触らなければよい。

    // いま必要なトラックの一覧。**順番は関係ありません**（配線はtrackIdで引く。8.110）
    juce::StringArray wantedIds;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);
        const auto type = track.getType();

        // 8.144：**パラアウトの受け皿も忘れずに**（Phase 182／本人の報告）。
        //
        // Phase 181ではここに足していませんでした。**ノードが1つも作られない**ので、
        // 受け皿のフェーダーは何にも繋がっていませんでした——
        // **本人の報告「フェーダーが反応していない」の正体**です。
        //
        // `createTrackNodes()`の絞り込みだけ直して、こちらを見落としていました。
        // **同じ「どの種別が音を通すか」を2箇所が持っている**という1.27の形です
        if (type == TrackType::Audio || type == TrackType::Send
             || type == TrackType::Midi || type == TrackType::Folder
             || type == TrackType::DrumOut)
            wantedIds.add (track.getId());
    }

    // 1) 消えたトラックのノードを外す
    bool removedAny = false;

    for (int i = (int) trackNodes.size(); --i >= 0;)
    {
        if (wantedIds.contains (trackNodes[(size_t) i].trackId))
            continue;

        removeTrackNodesFromGraph (trackNodes[(size_t) i]);
        trackNodes.erase (trackNodes.begin() + i);
        removedAny = true;
    }

    // **消すときだけ、つまみの状態をモデルへ退避する**（設計書3.8）。
    //
    // 残るトラックのノードは壊さないので、退避は要りません。
    // Phase 146までは毎回全プラグインに`getStateInformation()`を掛けていました
    // ——トラックを足すだけのときは、まるごと無駄でした
    if (removedAny)
        capturePluginStatesIntoProject();

    // 2) 増えたトラックのノードを作る。**既にあるものには触らない**——
    //    ここが「9秒」を消している本体です
    for (const auto& id : wantedIds)
    {
        if (findTrackNodes (id) != nullptr)
            continue;

        auto track = project.findTrackById (id);

        if (track.state.getParent().isValid())
            createTrackNodes (track);
    }

    // 2b) 8.144：**パラアウトが増えた音源は、読み込み直す**（Phase 182／本人の報告）。
    //
    // 出力バスを有効にできるのは**グラフへ入れる前だけ**です（`createPluginNode()`）。
    // ところがここは**差分更新**なので、既にある音源のノードには触りません
    // ——「受け皿を作った」だけでは、音源は2ch出力のままでした。
    //
    // **要るときだけ読み込み直します。** 受け皿が付いていて、まだ1番のバスが
    // 有効になっていない音源だけが対象なので、**普段は一度も走りません。**
    for (auto& nodes : trackNodes)
    {
        if (! nodes.isMidiTrack || nodes.instrumentNode == nullptr)
            continue;

        if (! trackNeedsDrumOutBuses (nodes.trackId))
            continue;

        auto* instance = nodes.instrumentNode->getProcessor();

        if (instance == nullptr || instance->getBusCount (false) <= 1)
            continue;

        auto* bus = instance->getBus (false, 1);

        if (bus == nullptr || bus->isEnabled())
            continue;   // もう有効。触る必要はありません

        reloadInstrumentWithOutputBuses (nodes);
    }

    // 3) 配線を張り直す。**残ったトラックも含めて全部**——
    //    センドとサイドチェインはトラックをまたぐので、増減があれば張り直しが要ります（8.69）
    rewireAllTrackConnections();

    // 新しく作ったノードへ、現在のミキサー設定を反映する
    updateMixerSettings();

    // 8.83：**鍵盤の行き先も洗い直す**（Phase 123/改善案⑬）。
    // ノードが入れ替わったので、前に覚えていたポインタはもう無効です（1.15）
    refreshMidiInputTargets();

    // 8.110：**再生中に作り直したら、中身も詰め直す**（Phase 146）。
    //
    // 作ったばかりの`MidiPlayerProcessor`はノート一覧が空、`ClipPlayerProcessor`は
    // 音源が空、`TrackChannelProcessor`はオートメーションが空です。詰めるのは
    // `prepareTrackPlayersForPlayback()`だけで、これは**Playを押した瞬間にしか
    // 走っていませんでした**——そのため、**再生中にトラックを1本足すと、
    // 既存トラックまで含めて全部が無音**になっていました（本人の報告）。
    //
    // **`transport.start()`は呼ばないこと**（再生位置が0へ飛びます）。
    // 終端はループ中なら見ない（`play()`と同じ判断）
    if (transport.isPlaying())
    {
        juce::int64 endPositionSamples = 0;
        prepareTrackPlayersForPlayback (endPositionSamples);
    }
}

/** 1トラックぶんのノードを作ってグラフへ足す（Phase 147で切り出し）。

    **音を通さない種別（Chord/VCA）なら何もしません。**
    出口・センド・サイドチェインは張りません——**行き先のノードが
    まだ無いことがある**ので、全部揃ってからまとめて張ります（8.51）。 */
void AudioEngine::createTrackNodes (const Track& track)
{
    const bool isSendTrack = (track.getType() == TrackType::Send);
    const bool isMidiTrack = (track.getType() == TrackType::Midi);

    // 8.51：**フォルダもオーディオを通す**（Phase 90／D2）。
    // 中身の音をここへ集めてからマスターへ送るので、
    // フェーダー・インサート・センド・オートメーションがそのまま使えます。
    // **自分の音源は持ちません**（センドトラックと同じ。入口は他から入ってくる音だけ）
    const bool isFolderTrack = (track.getType() == TrackType::Folder);

    // 8.143：**パラアウトの受け皿も、器はセンドトラックと同じ**（Phase 181／改善案⑮）。
    // 自分の音源もクリップも持たず、**音は外から入ってくる**——違うのは
    // 入口が「他トラックからの送り」ではなく「音源の出力バス」という点だけです
    const bool isDrumOutTrack = (track.getType() == TrackType::DrumOut);

    // 音声そのものを持たない種別（Chord/VCA）はここでは扱わない。
    if (track.getType() != TrackType::Audio && ! isSendTrack && ! isMidiTrack
         && ! isFolderTrack && ! isDrumOutTrack)
        return;

    TrackNodes nodes;
    nodes.trackId = track.getId();
    nodes.isSendTrack = isSendTrack;
    nodes.isMidiTrack = isMidiTrack;

    if (isMidiTrack)
    {
        // 仕様書5.3：MIDIトラックの入口は「MIDIプレイヤー → 音源」（Phase 14）
        auto midiPlayer = std::make_unique<MidiPlayerProcessor> (project, transport, nodes.trackId);
        nodes.midiPlayer = midiPlayer.get();
        nodes.midiPlayerNode = graph.addNode (std::move (midiPlayer));

        // 保存されている音源を読み込む（設計書3.8。インサートと同じ扱い）
        auto instrument = track.getInstrument();

        if (instrument.state.isValid())
        {
            juce::PluginDescription description;

            if (instrument.getDescription (description))
            {
                // 読み込めなかった音源は飛ばす（そのトラックだけ無音になり、
                // 他のトラックは今までどおり鳴る）
                // 8.144：**パラアウトの受け皿が付いていれば、出力バスも有効にして作る**
                // （Phase 182／本人の報告）。**入れた後では効きません**
                if (auto instrumentNode = createPluginNode (description,
                                                             trackNeedsDrumOutBuses (nodes.trackId)))
                {
                    applyPluginStateToNode (instrumentNode, instrument.getPluginState());
                    nodes.instrumentNode = instrumentNode;
                }
            }
        }
    }
    else if (! isSendTrack && ! isFolderTrack && ! isDrumOutTrack)
    {
        // センドトラックは自分でクリップを持たず、他トラックからの送りを受けるだけ。
        // そのためクリップ再生ノードは作らない（仕様書5.2.2）。
        // 8.143：パラアウトの受け皿も同じ（Phase 181）
        auto clipPlayer = std::make_unique<ClipPlayerProcessor> (project, transport, nodes.trackId);
        nodes.clipPlayer = clipPlayer.get();
        nodes.clipPlayerNode = graph.addNode (std::move (clipPlayer));
    }

    auto channel = std::make_unique<TrackChannelProcessor> (transport);
    nodes.channel = channel.get();
    nodes.channelNode = graph.addNode (std::move (channel));

    // 仕様書5.7：保存されているインサートを読み込む（設計書1.4の<INSERTS>）
    //
    // **読み込めなかったインサートの場所にはnullptrを積む。** 飛ばして詰めてしまうと
    // モデル側のスロット番号とノードの番号がずれ、「3番目のインサートを削除」
    // 「3番目のパラメータをオートメーション」といった番号での指定が
    // 別のプラグインに当たる。番号のずれはビルドでも見た目でも分からないので、
    // ここで必ず本数を揃えておくこと（サイドチェインの配線も番号で引く）。
    for (int i = 0; i < track.getNumInserts(); ++i)
    {
        auto insert = track.getInsert (i);
        juce::PluginDescription description;

        juce::AudioProcessorGraph::Node::Ptr insertNode;

        if (insert.getDescription (description))
            insertNode = createPluginNode (description);

        if (insertNode != nullptr)
            applyPluginStateToNode (insertNode, insert.getPluginState());

        nodes.insertNodes.push_back (insertNode);
    }

    // クリップ再生 → …インサート… → フェーダー（設計書1.5）。
    // **出口（マスターか、入っているフォルダか）は後でまとめて張る**（8.51）：
    // 行き先のフォルダのノードは、まだ作られていないことがある
    connectTrackChain (nodes);

    trackNodes.push_back (std::move (nodes));
}

//==============================================================================
// 仕様書5.2.2：センド
//==============================================================================

juce::AudioProcessorGraph::Node::Ptr AudioEngine::getChainInputNode (TrackNodes& nodes) const
{
    // センドを受け取る入口は「インサート列の先頭」。インサートが無ければフェーダー。
    // センドトラックではここが唯一の入力になる。
    //
    // 読み込みに失敗したインサートはnullptrとして並んでいる（本数をモデルと揃えるため）。
    // 先頭がnullptrでも、その先に生きているインサートがあればそこが入口になる。
    for (const auto& insertNode : nodes.insertNodes)
        if (insertNode != nullptr)
            return insertNode;

    return nodes.channelNode;
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::getPreFaderOutputNode (TrackNodes& nodes) const
{
    // フェーダー直前のノード＝インサート列の最後。無ければ音の出所
    // （オーディオトラックはクリップ再生、MIDIトラックは音源）。
    for (auto it = nodes.insertNodes.rbegin(); it != nodes.insertNodes.rend(); ++it)
        if (*it != nullptr)
            return *it;

    return nodes.instrumentNode != nullptr ? nodes.instrumentNode : nodes.clipPlayerNode;
}

//==============================================================================
// 8.143：**マルチアウト音源のパラアウト**（Phase 181／改善案⑮）
//==============================================================================

int AudioEngine::getInstrumentOutputBusCount (const juce::String& trackId) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || nodes->instrumentNode == nullptr)
        return 0;

    auto* instance = nodes->instrumentNode->getProcessor();

    return instance != nullptr ? instance->getBusCount (false) : 0;
}

juce::String AudioEngine::getInstrumentOutputBusName (const juce::String& trackId, int busIndex) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || nodes->instrumentNode == nullptr)
        return {};

    auto* instance = nodes->instrumentNode->getProcessor();

    if (instance == nullptr)
        return {};

    if (auto* bus = instance->getBus (false, busIndex))
        return bus->getName();

    return {};
}

int AudioEngine::enableExtraOutputBusesOn (juce::AudioProcessor& instance)
{
    // 8.61と同じ作法です（サイドチェインの入力バスを有効にするところ）。
    // **バスの構成を触るのは、プラグインによっては落ちるほど繊細**なので：
    //
    //   - **対応しているかを先に確かめる**（`isLayoutSupported`）
    //   - **戻り値を見る**（黙って失敗させない）
    //   - 既に有効なら触らない
    //
    // 8.144：**呼ぶのはグラフへ入れる前だけ**（Phase 182）。
    // 入れた後だと、グラフが古いチャンネル数のまま用意されます（`createPluginNode()`）
    const int numOutputBuses = instance.getBusCount (false);

    if (numOutputBuses <= 1)
        return 0;

    // 8.144：**構成は一度で決めること**（Phase 182／本人の報告）。
    //
    // Phase 181の最初の版は`Bus::enable()`を**1本ずつ**呼んでいました。
    // これは1回ごとに`setBusesLayout()`が走るということで、**途中の
    // 「半分だけ有効」な形**をプラグインへ見せることになります。
    // **Addictive Drums 2はそれで音が出なくなりました**（本人の報告のとおり）。
    //
    // 欲しい形を全部組んでから、**受け付けるかを確かめて、1回だけ**渡します。
    auto layout = instance.getBusesLayout();
    bool wantsChange = false;

    // **0番は触りません。** あれはメイン出力で、音源トラック自身が受けています
    for (int busIndex = 1; busIndex < numOutputBuses; ++busIndex)
    {
        if (busIndex >= layout.outputBuses.size())
            break;

        if (! layout.outputBuses.getReference (busIndex).isDisabled())
            continue;   // もう有効

        auto* bus = instance.getBus (false, busIndex);

        if (bus == nullptr)
            continue;

        layout.outputBuses.getReference (busIndex) = bus->getDefaultLayout();
        wantsChange = true;
    }

    // **断られたら、何もしないこと。** 半端に触ると音が出なくなります
    if (wantsChange
         && (! instance.checkBusesLayoutSupported (layout) || ! instance.setBusesLayout (layout)))
        return 0;

    int enabled = 0;

    for (int busIndex = 1; busIndex < numOutputBuses; ++busIndex)
        if (auto* bus = instance.getBus (false, busIndex))
            if (bus->isEnabled())
                ++enabled;

    return enabled;
}

void AudioEngine::reloadInstrumentWithOutputBuses (TrackNodes& nodes)
{
    // 8.144：**出力バスを有効にした状態で、音源を読み込み直す**（Phase 182／本人の報告）。
    //
    // バスを有効にできるのは**グラフへ入れる前だけ**なので、既に載っている音源の
    // バスを増やすには、作り直すしかありません（`createPluginNode()`の説明）。
    //
    // **つまみの状態は持ち越します。** 読み込み直しで音作りが消えたら、
    // 「パラアウトを作ったら音色が変わった」という最悪の驚き方になります（1.8）。
    auto track = findTrackById (nodes.trackId);

    if (! track.state.getParent().isValid())
        return;

    auto instrument = track.getInstrument();
    juce::PluginDescription description;

    if (! instrument.state.isValid() || ! instrument.getDescription (description))
        return;

    // いまの状態をモデルへ退避してから作り直す（設計書3.8の道具をそのまま使う）
    captureStateFromNode (instrument, nodes.instrumentNode);

    auto newNode = createPluginNode (description, true);

    if (newNode == nullptr)
        return;   // 読み込めなかった。**今の音源はそのまま残す**（無音にしない）

    disconnectTrackChain (nodes);

    // エディタウィンドウはプラグイン本体より先に閉じる（HANDOVER 1.5）
    nodes.instrumentEditorWindow = nullptr;

    if (nodes.instrumentNode != nullptr)
        graph.removeNode (nodes.instrumentNode->nodeID);

    nodes.instrumentNode = newNode;
    applyPluginStateToNode (nodes.instrumentNode, instrument.getPluginState());
}

bool AudioEngine::trackNeedsDrumOutBuses (const juce::String& trackId) const
{
    // 8.144：**受け皿があるトラックだけ、出力バスを有効にして作ります**（Phase 182）。
    //
    // 一律で有効にしないのは8.111と同じ理由です——バス構成を触るのは
    // プラグインによっては落ちるほど繊細なので、**要るときだけ**。
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto candidate = project.getTrack (t);

        if (candidate.getType() == TrackType::DrumOut
             && candidate.getDrumOutSourceTrackId() == trackId)
            return true;
    }

    return false;
}

void AudioEngine::rebuildDrumOutConnections()
{
    // 8.143：**音源の出力バス → 受け皿トラックの入口**（Phase 181／改善案⑮）。
    //
    // センドの配線（`rebuildSendConnections()`）とまったく同じ形です。違うのは
    // 音の出所が「送り元のフェーダー」ではなく「音源の何番目の出力バス」という点だけ。
    //
    // **全部のノードが揃ってから張ること**（8.51）。受け皿が音源より上の行にいると、
    // トラックを作るループの中ではまだノードがありません
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::DrumOut)
            continue;

        auto* targetNodes = findTrackNodes (track.getId());
        auto* sourceNodes = findTrackNodes (track.getDrumOutSourceTrackId());

        if (targetNodes == nullptr || sourceNodes == nullptr || sourceNodes->instrumentNode == nullptr)
            continue;   // 音源が外れている／読み込めなかった。その受け皿は無音のまま

        auto* instance = sourceNodes->instrumentNode->getProcessor();
        const int busIndex = track.getDrumOutSourceBus();

        if (instance == nullptr || busIndex <= 0 || busIndex >= instance->getBusCount (false))
            continue;

        auto* bus = instance->getBus (false, busIndex);

        // 8.144：**ここではバスを触りません**（Phase 182／本人の報告）。
        //
        // Phase 181は張る直前に有効にしていました。**グラフへ入れた後**なので、
        // グラフは古いチャンネル数のままで、この下の`addConnection()`が黙って
        // 断られていました（＝フェーダーが動かない）。有効にするのは
        // `createPluginNode()`が**グラフへ入れる前**にやります。
        //
        // 有効になっていないバスは、ここでは黙って飛ばします——
        // 受け皿を作った直後に作り直しが走り、そこで有効になった状態で読み直されます
        if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() <= 0)
            continue;

        auto targetInput = getChainInputNode (*targetNodes);

        if (targetInput == nullptr)
            continue;

        // グラフの接続で使うのは「processBlockのバッファ内での位置」。
        // メイン出力がステレオなら、2番目のバスは2番から始まります（8.61と同じ）
        const int firstChannel = bus->getChannelIndexInProcessBlockBuffer (0);
        const int numChannels = bus->getNumberOfChannels();

        // **モノのバスは両チャンネルへ配ります**（片側だけから鳴るのを避ける）
        for (int ch = 0; ch < 2; ++ch)
        {
            const int sourceChannel = firstChannel + juce::jmin (ch, numChannels - 1);

            graph.addConnection ({ { sourceNodes->instrumentNode->nodeID, sourceChannel },
                                    { targetInput->nodeID, ch } });
        }
    }
}

void AudioEngine::rebuildTrackOutputConnections()
{
    // 8.51：**各トラックの出口を張る**（Phase 90／D2。仕様書5.2）。
    //
    // 行き先は2つ：**フォルダに入っていればそのフォルダ、入っていなければマスター**。
    //
    // **全部のノードが揃ってから張ること。** 行き先のフォルダが自分より下の行にいると、
    // トラックを作るループの中では、まだノードが存在しません
    // （センドの配線を後回しにしているのと同じ理由）。
    if (masterNode == nullptr)
        return;

    for (auto& nodes : trackNodes)
    {
        if (nodes.channelNode == nullptr)
            continue;

        auto track = findTrackById (nodes.trackId);

        // 8.69：**マスターの入口はインサート列の先頭**（Phase 108/D6）。
        // `masterNode`へ直に繋ぐと、そのトラックだけマスターのインサートを素通りする
        auto destination = getMasterInputNode();

        if (track.state.getParent().isValid())
        {
            const auto parentId = track.getParentFolderId();

            if (parentId.isNotEmpty())
            {
                for (auto& candidate : trackNodes)
                {
                    if (candidate.trackId != parentId || candidate.channelNode == nullptr)
                        continue;

                    // **フォルダの入口はインサート列の先頭**（無ければフェーダー）。
                    // センドの受け口と同じ場所です（`getChainInputNode()`）
                    destination = getChainInputNode (candidate);
                    break;
                }
            }
        }

        if (destination == nullptr)
            destination = getMasterInputNode();

        graph.addConnection ({ { nodes.channelNode->nodeID, 0 }, { destination->nodeID, 0 } });
        graph.addConnection ({ { nodes.channelNode->nodeID, 1 }, { destination->nodeID, 1 } });
    }
}
void AudioEngine::rebuildSendConnections()
{
    // 既存の送り用ゲインノードを一度すべて片付ける。
    // 送り先や本数が変わっている可能性があるため、作り直すほうが確実。
    for (auto& nodes : trackNodes)
    {
        for (auto& send : nodes.sendNodes)
            if (send.gainNode != nullptr)
                graph.removeNode (send.gainNode->nodeID);

        nodes.sendNodes.clear();
    }

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);
        auto* sourceNodes = findTrackNodes (track.getId());

        if (sourceNodes == nullptr)
            continue;

        for (int s = 0; s < track.getNumSends(); ++s)
        {
            auto send = track.getSend (s);
            auto* targetNodes = findTrackNodes (send.getTargetTrackId());

            if (targetNodes == nullptr || ! targetNodes->isSendTrack)
                continue; // 送り先が消えている場合は、その送りだけ無視する

            auto targetTrack = findTrackById (send.getTargetTrackId());

            auto gain = std::make_unique<SendGainProcessor>();
            gain->setLevelDb (send.getLevelDb());

            SendNodes sendNodes;
            sendNodes.targetTrackId = send.getTargetTrackId();
            sendNodes.gain = gain.get();
            sendNodes.gainNode = graph.addNode (std::move (gain));

            // プリ／ポストフェーダーの分岐（仕様書5.2.2）。
            // ポスト＝フェーダー通過後の音を送る（フェーダーを下げると送りも減る）。
            // プリ＝フェーダーの手前から送る（フェーダーと独立した送り量になる）。
            const bool preFader = targetTrack.state.isValid() && targetTrack.isPreFader();
            auto sourceNode = preFader ? getPreFaderOutputNode (*sourceNodes) : sourceNodes->channelNode;

            if (sourceNode != nullptr)
            {
                graph.addConnection ({ { sourceNode->nodeID, 0 }, { sendNodes.gainNode->nodeID, 0 } });
                graph.addConnection ({ { sourceNode->nodeID, 1 }, { sendNodes.gainNode->nodeID, 1 } });

                auto targetInput = getChainInputNode (*targetNodes);

                if (targetInput != nullptr)
                {
                    graph.addConnection ({ { sendNodes.gainNode->nodeID, 0 }, { targetInput->nodeID, 0 } });
                    graph.addConnection ({ { sendNodes.gainNode->nodeID, 1 }, { targetInput->nodeID, 1 } });
                }
            }

            sourceNodes->sendNodes.push_back (sendNodes);
        }
    }
}

//==============================================================================
// 仕様書5.7.1・設計書3.9：PDC（プラグイン遅延補正）
//==============================================================================

double AudioEngine::getCurrentSampleRate() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    return device != nullptr ? device->getCurrentSampleRate() : 0.0;
}

int AudioEngine::getTotalLatencySamples() const
{
    // グラフが自分で計算した補正量。各ノードの入力側レイテンシを辿って求めた
    // 全体の最大値で、`setLatencySamples()`として報告されている（AudioEngine.hの説明を参照）。
    return graph.getLatencySamples();
}

int AudioEngine::getTrackLatencySamples (const juce::String& trackId) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr)
        return 0;

    int total = 0;

    auto addLatencyOf = [&total] (const juce::AudioProcessorGraph::Node::Ptr& node)
    {
        if (node != nullptr)
            if (auto* processor = node->getProcessor())
                total += processor->getLatencySamples();
    };

    addLatencyOf (nodes->instrumentNode);

    for (const auto& insertNode : nodes->insertNodes)
        addLatencyOf (insertNode);

    return total;
}

std::map<juce::uint32, int> AudioEngine::collectPluginLatencies() const
{
    std::map<juce::uint32, int> result;

    auto record = [&result] (const juce::AudioProcessorGraph::Node::Ptr& node)
    {
        if (node != nullptr)
            if (auto* processor = node->getProcessor())
                result[node->nodeID.uid] = processor->getLatencySamples();
    };

    for (const auto& nodes : trackNodes)
    {
        record (nodes.instrumentNode);

        for (const auto& insertNode : nodes.insertNodes)
            record (insertNode);
    }

    return result;
}

void AudioEngine::checkForLatencyChanges()
{
    auto current = collectPluginLatencies();

    if (current == lastKnownPluginLatency)
        return;

    lastKnownPluginLatency = std::move (current);

    // ここへ来るのは「プラグインがレイテンシを変えた」ときだけではなく、
    // 「プラグインが増減した」ときも含む。後者はグラフが既に計算し直しているが、
    // 表示は更新したいので、どちらも同じ扱いにしている。

    // グラフに補正を計算し直させる。
    // **中身が変わっていなければJUCE側が何もしない**ので、呼びすぎても実害は無い
    // （`handleAsyncUpdate()`が構成の署名を見比べ、同じならレンダリング列を作り直さない）。
    //
    // 表示側（ConsoleView）へは通知しない。ミキサーは自前のタイマーで
    // メーターと一緒にレイテンシも読み直しており、そのほうがコールバックの
    // 寿命管理（破棄済みのビューを掴む事故）を持ち込まずに済むため。
    graph.rebuild();
}

//==============================================================================
// 仕様書5.7.2・設計書3.9：サイドチェイン
//==============================================================================

bool AudioEngine::getSidechainInputChannels (juce::AudioProcessorGraph::Node::Ptr node,
                                              int& firstChannelOut, int& numChannelsOut)
{
    if (node == nullptr)
        return false;

    auto* processor = node->getProcessor();

    if (processor == nullptr || processor->getBusCount (true) < 2)
        return false;

    auto* bus = processor->getBus (true, 1);

    if (bus == nullptr || ! bus->isEnabled() || bus->getNumberOfChannels() <= 0)
        return false;

    // グラフの接続で使うのは「processBlockのバッファ内での位置」。
    // メイン入力がステレオなら、サイドチェインは2番から始まる。
    firstChannelOut = bus->getChannelIndexInProcessBlockBuffer (0);
    numChannelsOut = bus->getNumberOfChannels();

    return true;
}

bool AudioEngine::insertSupportsSidechain (const juce::String& trackId, int insertIndex) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || ! juce::isPositiveAndBelow (insertIndex, (int) nodes->insertNodes.size()))
        return false;

    int firstChannel = 0;
    int numChannels = 0;

    return getSidechainInputChannels (nodes->insertNodes[(size_t) insertIndex], firstChannel, numChannels);
}

void AudioEngine::rewireAllTrackConnections()
{
    if (masterNode == nullptr)
        return; // まだinitialise()が済んでいない

    // 経路上の接続をいったん全部外してから繋ぎ直す。
    // **1トラックだけ繋ぎ直すのでは足りない**：サイドチェインは他トラックの
    // フェーダー出力から入ってくるため、外す／張るのどちらもトラックをまたぐ。
    for (auto& nodes : trackNodes)
    {
        disconnectTrackChain (nodes);
        connectTrackChain (nodes);
    }

    // 8.69：**出口は`rebuildTrackOutputConnections()`に任せる**（Phase 108／D6）。
    //
    // ここではマスターへ直に繋いでいました。2つ間違っていました：
    //   - **フォルダに入っているトラックまでマスターへ行っていた**（8.51の行き先を無視）
    //   - マスターにインサートを挿すと、そこだけ素通りする（8.69）
    //
    // 「トラックの出口はどこか」を決める場所は1つにしてあります（8.2）
    rebuildTrackOutputConnections();

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();
}

void AudioEngine::rebuildSidechainConnections()
{
    for (auto& nodes : trackNodes)
    {
        auto track = findTrackById (nodes.trackId);

        if (! track.state.getParent().isValid())
            continue;

        // インサートの本数はモデルとノードで揃えてある（rebuildTrackNodes参照）が、
        // 念のため短いほうに合わせる
        const int numInserts = juce::jmin (track.getNumInserts(), (int) nodes.insertNodes.size());

        for (int i = 0; i < numInserts; ++i)
        {
            auto insertNode = nodes.insertNodes[(size_t) i];

            if (insertNode == nullptr)
                continue;

            const auto sourceTrackId = track.getInsert (i).getSidechainSourceTrackId();

            // 自分自身をソースにすると信号が輪になるので受け付けない
            // （UI側でも自分を候補から外しているが、保存されたファイル由来の値もあり得る）
            if (sourceTrackId.isEmpty() || sourceTrackId == nodes.trackId)
                continue;

            auto* sourceNodes = findTrackNodes (sourceTrackId);

            if (sourceNodes == nullptr || sourceNodes->channelNode == nullptr)
                continue; // ソースのトラックが消えている場合は、その1本だけ無視する


            // 8.111：**ここで初めてサイドチェインバスを有効にする**（Phase 147）。
            // 作った直後に一律で有効化するのをやめました——バス構成を変える操作は
            // プラグインによっては落ちるほど繊細なので、**本当に要るときだけ**触ります
            ensureSidechainBusEnabled (insertNode);
            int firstChannel = 0;
            int numChannels = 0;

            if (! getSidechainInputChannels (insertNode, firstChannel, numChannels))
                continue; // サイドチェイン非対応のプラグインに設定が残っている場合

            // ソースは**フェーダー通過後**（TrackChannelProcessorの出力）から取る。
            // 画面のフェーダーで送る量を調整できるほうが直感的なため
            // （センドのポストフェーダーと同じ考え方。仕様書5.2.2）。
            for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
                graph.addConnection ({ { sourceNodes->channelNode->nodeID, ch },
                                        { insertNode->nodeID, firstChannel + ch } });
        }
    }
}

juce::String AudioEngine::setInsertSidechainSource (const juce::String& trackId, int insertIndex,
                                                     const juce::String& sourceTrackId)
{
    auto track = findTrackById (trackId);

    if (! track.state.getParent().isValid() || ! juce::isPositiveAndBelow (insertIndex, track.getNumInserts()))
        return utf8 ("インサートが見つかりません。");

    project.beginAction (utf8 ("サイドチェインの設定"));
    track.getInsert (insertIndex).setSidechainSourceTrackId (sourceTrackId, &project.getUndoManager());

    // 解除したときに古い接続が残らないよう、経路そのものを繋ぎ直す。
    // `rebuildSidechainConnections()`は接続を**足すだけ**なので、これだけでは
    // 「サイドチェインを外したのに繋がったまま」になる。
    rewireAllTrackConnections();

    auto* nodes = findTrackNodes (trackId);

    if (sourceTrackId.isEmpty())
        return {}; // 解除なので、繋がっていないのが正しい

    // 実際に繋がったかを確かめる。ここを省くと「設定したのに効かない」が黙って起きる
    auto* sourceNodes = findTrackNodes (sourceTrackId);

    if (nodes == nullptr || sourceNodes == nullptr || sourceNodes->channelNode == nullptr
         || ! juce::isPositiveAndBelow (insertIndex, (int) nodes->insertNodes.size()))
        return utf8 ("サイドチェインのソースになるトラックが見つかりません。");

    auto insertNode = nodes->insertNodes[(size_t) insertIndex];
    int firstChannel = 0;
    int numChannels = 0;

    if (insertNode == nullptr || ! getSidechainInputChannels (insertNode, firstChannel, numChannels))
        return utf8 ("このプラグインはサイドチェイン入力を持っていません。");

    if (! graph.isConnected ({ { sourceNodes->channelNode->nodeID, 0 },
                                { insertNode->nodeID, firstChannel } }))
        return utf8 ("サイドチェインの配線に失敗しました。");

    return {};
}

Track AudioEngine::findTrackById (const juce::String& trackId) const
{
    // 引き当ての実装はProjectModel側に1つだけ置いてある（見つからない場合は空のTrack）
    return project.findTrackById (trackId);
}

juce::String AudioEngine::addSendToTrack (const juce::String& sourceTrackId, const juce::String& targetTrackId)
{
    auto sourceTrack = findTrackById (sourceTrackId);

    if (! sourceTrack.state.getParent().isValid())
        return utf8 ("送り元のトラックが見つかりません。");

    // 送りの追加はUndo対象にする（モデルだけの変更で、配線はここで作り直すため
    // 食い違いが起きない。プラグインの抜き差しとは事情が違う）。
    project.beginAction (utf8 ("センドの追加"));
    sourceTrack.addSend (targetTrackId, &project.getUndoManager());

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    updateMixerSettings();

    return {};
}

void AudioEngine::removeSendFromTrack (const juce::String& sourceTrackId, int sendIndex)
{
    auto sourceTrack = findTrackById (sourceTrackId);

    if (! sourceTrack.state.getParent().isValid())
        return;

    project.beginAction (utf8 ("センドの削除"));
    sourceTrack.removeSend (sendIndex, &project.getUndoManager());

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    updateMixerSettings();
}

void AudioEngine::moveSendInTrack (const juce::String& trackId, int fromIndex, int toIndex)
{
    auto track = findTrackById (trackId);

    if (! track.state.getParent().isValid())
        return;

    if (! juce::isPositiveAndBelow (fromIndex, track.getNumSends()))
        return;

    // 8.66：Undoに積む（Phase 105）。配線はモデルから作り直すので食い違わない
    project.beginAction (utf8 ("センドの並べ替え"));
    track.moveSend (fromIndex, toIndex, &project.getUndoManager());

    // **番号でモデルと対応させている**ので（`updateMixerSettings()`）、張り直しは必須
    rebuildSendConnections();
    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    updateMixerSettings();
}

juce::String AudioEngine::copySendToTrack (const juce::String& sourceTrackId, int sourceIndex,
                                            const juce::String& destTrackId, int destIndex)
{
    auto sourceTrack = findTrackById (sourceTrackId);
    auto destTrack   = findTrackById (destTrackId);

    if (! sourceTrack.state.getParent().isValid() || ! destTrack.state.getParent().isValid())
        return utf8 ("トラックが見つかりません。");

    if (! juce::isPositiveAndBelow (sourceIndex, sourceTrack.getNumSends()))
        return utf8 ("コピー元のセンドが見つかりません。");

    auto sourceSend = sourceTrack.getSend (sourceIndex);
    const auto targetTrackId = sourceSend.getTargetTrackId();

    // 仕様書5.2.2：送りを付けられるのは自分で音を出すトラックだけ
    // （`TrackRackComponent`の「+ Send」の出し分けと条件を合わせること）
    const bool canHaveSends = (destTrack.getType() == TrackType::Audio
                                || destTrack.getType() == TrackType::Midi
                                || destTrack.getType() == TrackType::Folder);

    if (! canHaveSends)
        return utf8 ("このトラックにはセンドを付けられません。");

    // **輪になる組み合わせは断る。** 送り先のセンドトラック自身へ同じ送りを付けると、
    // 出た音がそのまま自分へ戻ってくる
    if (destTrackId == targetTrackId)
        return utf8 ("送り先のセンドトラック自身へは、同じ送りを付けられません。");

    project.beginAction (utf8 ("センドのコピー"));

    // `addSend()`は同じ送り先が既にあればそれを返す（重複して作らない）。
    // その場合は**送り量だけを写す**ことになり、「同じ量にそろえる」操作として自然
    auto copied = destTrack.addSend (targetTrackId, &project.getUndoManager());
    copied.setLevelDb (sourceSend.getLevelDb(), &project.getUndoManager());

    // 挿し込む位置が指定されていれば、そこへ動かす。
    //
    // **末尾に足った前提で書かないこと。** 同じ送り先が既にあった場合、`addSend()`は
    // 何も足さずに既存のものを返すので、末尾は別の送りになっている
    int addedIndex = -1;

    for (int i = 0; i < destTrack.getNumSends(); ++i)
    {
        if (destTrack.getSend (i).state == copied.state)
        {
            addedIndex = i;
            break;
        }
    }

    if (addedIndex >= 0)
    {
        // 落とし先は「ここの**前**へ入る」を指しているので、
        // 自分より下から動かすときは1つ詰まる（`TrackRackComponent`の並べ替えと同じ）
        const int target = (destIndex > addedIndex) ? destIndex - 1 : destIndex;

        if (target >= 0 && target != addedIndex)
            destTrack.moveSend (addedIndex, target, &project.getUndoManager());
    }

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    updateMixerSettings();

    return {};
}

bool AudioEngine::isPluginBlockedByCrashes (const juce::PluginDescription& description) const
{
    // 8.112：**前に落ちたプラグインは読み込まない**（Phase 148）。
    //
    // 設計書3.5のマーカー方式は、**回数を数えるところまで**しか使われていませんでした
    // （画面では色を変えるだけ）。**判定を持っているのに使っていない**ので、
    // 落ちるプラグインを挿すたびにアプリごと落ち続けます。
    //
    // **サンドボックス（4.3）が繋がるまでの、いちばん確実な守り**です。
    // 再挑戦の入口はあります——環境設定のPluginsで「クラッシュ履歴をリセット」。
    //
    // 9.5：**内蔵プラグインは対象外**（Phase 204）。これは**外のプラグイン**のための
    // 仕掛けです。自前のものが落ちたらそれは直すべきバグで、
    // **飛ばして隠すと「なぜか挿せない」だけが残ります**（しかも隠したことに気づけません）。
    if (MantaPlugins::isMantaPlugin (description))
        return false;

    return crashTracker.shouldSandbox (description.createIdentifierString());
}

juce::String AudioEngine::getPluginBlockedMessage (const juce::PluginDescription& description) const
{
    const int count = crashTracker.getCrashCount (description.createIdentifierString());

    return utf8 ("「") + description.name + utf8 ("」は読み込み中に ") + juce::String (count)
             + utf8 (" 回アプリを終了させています。安全のため読み込みませんでした。\n")
             + utf8 ("もう一度試すには、環境設定の Plugins で「クラッシュ履歴をリセット」してください。");
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::createPluginNode (const juce::PluginDescription& description,
                                                                     bool enableExtraOutputBuses)
{
    // 8.112：**落ちると分かっているものは読み込まない**（Phase 148）。
    // プロジェクトの読み込みでもここを通るので、**開いた瞬間に落ちる**のも防げます
    if (isPluginBlockedByCrashes (description))
    {
        DBG ("Plugin blocked by crash history: " << description.name);
        return nullptr;
    }

    auto* device = deviceManager.getCurrentAudioDevice();

    // 8.112：**0を渡さない**（Phase 148）。デバイスが開いていない・準備中のときに
    // `getCurrentSampleRate()`が0を返すことがあり、**0でプラグインを作ると
    // 落ちるものがあります**。nullptrだけを見ていて、値そのものを見ていませんでした
    const double deviceRate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const int deviceBlock   = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;

    const double sampleRate = deviceRate > 0.0 ? deviceRate : 44100.0;
    const int blockSize     = deviceBlock > 0  ? deviceBlock : 512;

    juce::String errorMessage;

    // 設計書3.5：ロード直前にクラッシュマーカーを書き、正常に戻ったら消す
    crashTracker.beginPluginOperation (description.createIdentifierString());
    auto instance = pluginManager.getFormatManager().createPluginInstance (description, sampleRate, blockSize, errorMessage);
    crashTracker.endPluginOperation();

    if (instance == nullptr)
        return nullptr;
    // 8.111：**ここではサイドチェインバスを触りません**（Phase 147）。
    //
    // Phase 146まで、作った直後のプラグインに対して**問答無用で第2入力バスを
    // `enable(true)`** していました。戻り値も見ていません。
    // `Bus::enable()`はVST3では`setBusArrangements()`の呼び出しになり、
    // **ホスティングで最も相性問題が出やすい場所**です——想定外のアレンジメントを
    // 渡されて落ちるプラグインが実在します。
    //
    // **本人の報告「あるプラグイン（NI Raum）を挿すとアプリが落ちる。開発初期には
    // 挿せた」に、時系列も合います**（この4行はサイドチェイン対応＝Phase 12d-3で
    // 後から入ったもの）。
    //
    // **要るときに要るぶんだけ有効にします**（`ensureSidechainBusEnabled()`）。
    // サイドチェインのソースを実際に割り当てたときだけなので、
    // **大半のプラグインは一度も触られません。**

    // 8.144：**出力バスは、グラフへ入れる前に有効にすること**（Phase 182／本人の報告）。
    //
    // Phase 181では**入れた後**で有効にしていました。`graph.addNode()`は
    // その場でノードを`prepareToPlay()`するので、**グラフは古いチャンネル数のまま**に
    // なります。結果：
    //
    //   - 3番以降のチャンネルへの`addConnection()`が**黙って断られる**
    //     （＝パラアウトのフェーダーが動かない）
    //   - プラグインは増えたバスぶんを出そうとするのに、渡されるバッファは
    //     2chのまま → **元の音まで出なくなる**
    //
    // **本人の報告「パラアウトすると音は消えるけど、フェーダーが反応していない」**が、
    // この2つとそのまま一致します。
    //
    // 入れる前なら`prepareToPlay()`はまだなので、**増えたチャンネル数でグラフが用意**されます
    if (enableExtraOutputBuses)
        enableExtraOutputBusesOn (*instance);

    return graph.addNode (std::move (instance));
}

bool AudioEngine::ensureSidechainBusEnabled (juce::AudioProcessorGraph::Node::Ptr node)
{
    // 8.111：**サイドチェインバスを有効にするのはここだけ**（Phase 147）。
    //
    // VST3ではサイドチェインバスが既定で無効なことが多く、そのままでは
    // グラフから繋ぐチャンネルが存在しません。ただし**バス構成を変える操作は
    // プラグインによっては落ちるほど繊細**なので：
    //
    //   - **本当にソースを割り当てたときだけ**呼ぶこと（作った直後に一律ではなく）
    //   - **対応しているかを先に確かめる**（`isLayoutSupported`）
    //   - **戻り値を見る**（黙って失敗させない）
    if (node == nullptr)
        return false;

    auto* instance = node->getProcessor();

    if (instance == nullptr || instance->getBusCount (true) <= 1)
        return false;

    auto* sidechainBus = instance->getBus (true, 1);

    if (sidechainBus == nullptr)
        return false;

    if (sidechainBus->isEnabled())
        return true;

    // **対応していない構成を渡さない。** ここを飛ばすと、
    // `setBusArrangements()`に想定外の値が渡ってプラグインが落ちることがあります
    const auto wanted = sidechainBus->getDefaultLayout();

    if (! sidechainBus->isLayoutSupported (wanted))
        return false;

    return sidechainBus->enable (true);
}

void AudioEngine::removeTrackNodesFromGraph (TrackNodes& nodes)
{
    // プラグイン本体より先にエディタウィンドウを閉じる（開いたまま破棄すると落ちる。
    // HANDOVER 1.5「プラグインGUIのウィンドウは閉じる＝隠す」参照）
    nodes.insertEditorWindows.clear();
    nodes.instrumentEditorWindow = nullptr;

    for (auto& insertNode : nodes.insertNodes)
        if (insertNode != nullptr)
            graph.removeNode (insertNode->nodeID);

    nodes.insertNodes.clear();

    for (auto& send : nodes.sendNodes)
        if (send.gainNode != nullptr)
            graph.removeNode (send.gainNode->nodeID);

    nodes.sendNodes.clear();

    if (nodes.instrumentNode != nullptr)
        graph.removeNode (nodes.instrumentNode->nodeID);

    if (nodes.midiPlayerNode != nullptr)
        graph.removeNode (nodes.midiPlayerNode->nodeID);

    if (nodes.clipPlayerNode != nullptr)
        graph.removeNode (nodes.clipPlayerNode->nodeID);

    if (nodes.channelNode != nullptr)
        graph.removeNode (nodes.channelNode->nodeID);

    nodes.instrumentNode = nullptr;
    nodes.midiPlayerNode = nullptr;
    nodes.clipPlayerNode = nullptr;
    nodes.channelNode = nullptr;
    nodes.midiPlayer = nullptr;
    nodes.clipPlayer = nullptr;
    nodes.channel = nullptr;
}

void AudioEngine::connectTrackChain (TrackNodes& nodes)
{
    if (nodes.channelNode == nullptr)
        return;

    // 仕様書5.3：MIDIトラックは、まずMIDIプレイヤーから音源へMIDIを送る。
    // 音源が未設定のトラックはここが繋がらず、音の出所が無い＝無音になる。
    if (nodes.midiPlayerNode != nullptr && nodes.instrumentNode != nullptr)
        graph.addConnection ({ { nodes.midiPlayerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex },
                                { nodes.instrumentNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex } });

    // 音の出所 → インサート1 → インサート2 → … → フェーダー、と数珠つなぎにする。
    // 直前のノードを覚えながら順に繋いでいくので、インサートが0個でも成り立つ。
    //
    // 音の出所は、オーディオトラックならクリップ再生ノード、MIDIトラックなら音源。
    // センドトラック（と音源未設定のMIDIトラック）には出所が無いため、
    // previousNodeがnullptrから始まる。センドトラックではインサート列の先頭が
    // 他トラックからの送りを受ける入口になる。
    auto previousNode = nodes.instrumentNode != nullptr ? nodes.instrumentNode
                                                        : nodes.clipPlayerNode;

    // 8.63：**バイパスしたインサートは配線から外す**（Phase 101／改善案㉘）。
    //
    // プラグイン側のバイパス機能には頼りません：**持っていないプラグインもあり**、
    // 持っていても「通さない」の意味が実装ごとに違います（内部の残響を流し続けるもの、
    // レイテンシだけ残すもの……）。**繋がないのがいちばん確か**です。
    //
    // 番号は`nodes.insertNodes`とモデルで揃えてあるので（上のrebuildTrackNodes参照）、
    // ここで引き当てられます
    auto bypassTrack = findTrackById (nodes.trackId);
    int insertIndex = -1;

    for (auto& insertNode : nodes.insertNodes)
    {
        ++insertIndex;

        if (insertNode == nullptr)
            continue;

        if (bypassTrack.state.getParent().isValid()
             && juce::isPositiveAndBelow (insertIndex, bypassTrack.getNumInserts())
             && bypassTrack.getInsert (insertIndex).isBypassed())
            continue;   // 通さない＝直前のノードをそのまま次へ渡す

        if (previousNode == nullptr)
        {
            previousNode = insertNode; // センドトラックの入口
            continue;
        }

        const bool ok = graph.addConnection ({ { previousNode->nodeID, 0 }, { insertNode->nodeID, 0 } })
                         && graph.addConnection ({ { previousNode->nodeID, 1 }, { insertNode->nodeID, 1 } });

        // ステレオ2ch入出力でないプラグインは繋がらない。その場合は経路から外し、
        // 音が途切れないように直前のノードをそのまま次へ渡す（バイパス扱い）。
        if (ok)
            previousNode = insertNode;
    }

    if (previousNode == nullptr)
        return; // インサートもクリップ再生も無いセンドトラック（送りが直接フェーダーへ入る）

    graph.addConnection ({ { previousNode->nodeID, 0 }, { nodes.channelNode->nodeID, 0 } });
    graph.addConnection ({ { previousNode->nodeID, 1 }, { nodes.channelNode->nodeID, 1 } });
}

void AudioEngine::disconnectTrackChain (TrackNodes& nodes)
{
    // 経路上のノードから出ている接続を全て外す。インサートの追加・削除では
    // 並びが変わるため、いったん全部外してから繋ぎ直すほうが確実。
    if (nodes.clipPlayerNode != nullptr)
        graph.disconnectNode (nodes.clipPlayerNode->nodeID);

    if (nodes.midiPlayerNode != nullptr)
        graph.disconnectNode (nodes.midiPlayerNode->nodeID);

    if (nodes.instrumentNode != nullptr)
        graph.disconnectNode (nodes.instrumentNode->nodeID);

    for (auto& insertNode : nodes.insertNodes)
        if (insertNode != nullptr)
            graph.disconnectNode (insertNode->nodeID);

    // フェーダー→マスターの接続まで消えるので、呼び出し側で張り直すこと
    if (nodes.channelNode != nullptr)
        graph.disconnectNode (nodes.channelNode->nodeID);
}

AudioEngine::TrackNodes* AudioEngine::findTrackNodes (const juce::String& trackId)
{
    for (auto& nodes : trackNodes)
        if (nodes.trackId == trackId)
            return &nodes;

    return nullptr;
}

const AudioEngine::TrackNodes* AudioEngine::findTrackNodes (const juce::String& trackId) const
{
    for (const auto& nodes : trackNodes)
        if (nodes.trackId == trackId)
            return &nodes;

    return nullptr;
}

//==============================================================================
// 8.69：マスターへのインサート（Phase 108／設計書1.4の`MASTERBUS/INSERTS`。8.1のD6）
//==============================================================================

Track AudioEngine::findInsertHost (const juce::String& trackId)
{
    return trackId.isEmpty() ? project.getMasterBusInsertHost()
                              : findTrackById (trackId);
}

juce::AudioProcessorGraph::Node::Ptr AudioEngine::getMasterInputNode() const
{
    return masterChainInputNode != nullptr ? masterChainInputNode : masterNode;
}

void AudioEngine::disconnectMasterChain()
{
    for (auto& node : masterInsertNodes)
        if (node != nullptr)
            graph.disconnectNode (node->nodeID);

    // **マスター本体も一度切る。** JUCEには「入ってくるぶんだけ外す」が無いので、
    // 出ていくぶん（マスター→出力）も一緒に消える。下で張り直すこと
    if (masterNode != nullptr)
        graph.disconnectNode (masterNode->nodeID);

    masterChainInputNode = nullptr;
}

void AudioEngine::connectMasterChain()
{
    if (masterNode == nullptr)
        return;

    disconnectMasterChain();

    auto host = project.getMasterBusInsertHost();

    juce::AudioProcessorGraph::Node::Ptr previousNode;

    // 数珠つなぎの作りは`connectTrackChain()`と同じ。
    // 8.63：**通さない指定のインサートは配線から外す**（繋がないのがいちばん確か）
    for (size_t i = 0; i < masterInsertNodes.size(); ++i)
    {
        auto insertNode = masterInsertNodes[i];

        if (insertNode == nullptr)
            continue;

        if ((int) i < host.getNumInserts() && host.getInsert ((int) i).isBypassed())
            continue;

        if (previousNode == nullptr)
        {
            // ここが「マスターへ音を流し込む先」になる
            previousNode = insertNode;
            masterChainInputNode = insertNode;
            continue;
        }

        const bool ok = graph.addConnection ({ { previousNode->nodeID, 0 }, { insertNode->nodeID, 0 } })
                         && graph.addConnection ({ { previousNode->nodeID, 1 }, { insertNode->nodeID, 1 } });

        // ステレオ2ch入出力でないプラグインは繋がらない。経路から外して音を途切れさせない
        if (ok)
            previousNode = insertNode;
    }

    if (previousNode != nullptr)
    {
        graph.addConnection ({ { previousNode->nodeID, 0 }, { masterNode->nodeID, 0 } });
        graph.addConnection ({ { previousNode->nodeID, 1 }, { masterNode->nodeID, 1 } });
    }

    // マスター → 出力。ここだけが実際にスピーカーへ繋がる経路
    if (outputNode != nullptr)
    {
        graph.addConnection ({ { masterNode->nodeID, 0 }, { outputNode->nodeID, 0 } });
        graph.addConnection ({ { masterNode->nodeID, 1 }, { outputNode->nodeID, 1 } });
    }

    auto input = getMasterInputNode();

    // 録音モニターとメトロノームも**インサートを通す**。
    // ここを`masterNode`のままにすると、**マスターに挿したリミッターを
    // メトロノームだけがすり抜ける**（クリックだけ潰れずに飛び出す）
    if (recorderNode != nullptr)
    {
        graph.addConnection ({ { recorderNode->nodeID, 0 }, { input->nodeID, 0 } });
        graph.addConnection ({ { recorderNode->nodeID, 1 }, { input->nodeID, 1 } });
    }

    if (metronomeNode != nullptr)
    {
        graph.addConnection ({ { metronomeNode->nodeID, 0 }, { input->nodeID, 0 } });
        graph.addConnection ({ { metronomeNode->nodeID, 1 }, { input->nodeID, 1 } });
    }

    // **マスターを切ると、トラックの出口も一緒に消える**ので張り直す
    rebuildTrackOutputConnections();
}

void AudioEngine::rebuildMasterInsertNodes()
{
    if (masterNode == nullptr)
        return;

    // プラグイン本体より先にエディタウィンドウを閉じる（HANDOVER 1.5）
    masterInsertEditorWindows.clear();

    for (auto& node : masterInsertNodes)
        if (node != nullptr)
            graph.removeNode (node->nodeID);

    masterInsertNodes.clear();

    auto host = project.getMasterBusInsertHost();

    for (int i = 0; i < host.getNumInserts(); ++i)
    {
        auto insert = host.getInsert (i);

        juce::PluginDescription description;
        juce::AudioProcessorGraph::Node::Ptr node;

        if (insert.getDescription (description))
        {
            node = createPluginNode (description);

            if (node != nullptr)
                applyPluginStateToNode (node, insert.getPluginState());   // 設計書3.8
        }

        // **読み込めなくてもnullptrを並べる。** 本数をモデルと揃えておかないと、
        // 2本目以降の番号がずれる（トラック側と同じ決まり）
        masterInsertNodes.push_back (node);
    }

    connectMasterChain();
}

//==============================================================================

juce::String AudioEngine::addInsertToTrack (const juce::String& trackId, const juce::PluginDescription& description)
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）
    if (trackId.isEmpty())
    {
        if (masterNode == nullptr)
            return utf8 ("オーディオデバイスが開けていません。");

        auto insertNode = createPluginNode (description);

        if (insertNode == nullptr)
            return isPluginBlockedByCrashes (description) ? getPluginBlockedMessage (description)
                                                          : utf8 ("プラグインを読み込めませんでした: ") + description.name;

        // トラックと同じくUndo対象にしない（モデルだけ巻き戻してもノードは外れない）
        project.getMasterBusInsertHost().addInsert (description, nullptr);

        masterInsertNodes.push_back (insertNode);
        connectMasterChain();

        return {};
    }

    // 8.111：**プラグインを先に作ってから、ノードを引く**（Phase 147）。
    //
    // 逆順だと、`findTrackNodes()`が返す**vectorの中を指す生ポインタ**を持ったまま
    // プラグインのDLL初期化を挟むことになります。**プラグインが初期化中に
    // メッセージをポンプする**と、溜まっていた`AsyncUpdater`（＝ノードの作り直し）が
    // 走ってポインタが宙に浮きます。NI系はライセンス確認などでこれをやり得ます。
    //
    // **入れ替えるだけで、コストはゼロです。**
    if (findTrackNodes (trackId) == nullptr)
        return utf8 ("トラックが見つかりません。");

    auto insertNode = createPluginNode (description);

    if (insertNode == nullptr)
        return isPluginBlockedByCrashes (description) ? getPluginBlockedMessage (description)
                                                      : utf8 ("プラグインを読み込めませんでした: ") + description.name;

    auto* nodes = findTrackNodes (trackId);   // **作った後に引き直す**

    if (nodes == nullptr)
        return utf8 ("トラックが見つかりません。");

    // モデルへ記録する。プラグインの抜き差しはUndo対象にしない
    // （モデルだけ巻き戻してもグラフ上のノードは外れず、状態が食い違うため）。
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getId() == trackId)
        {
            track.addInsert (description, nullptr);
            break;
        }
    }

    disconnectTrackChain (*nodes);
    nodes->insertNodes.push_back (insertNode);
    connectTrackChain (*nodes);

    // 8.52：**出口はまとめて張り直す**（Phase 91）。フォルダにインサートを挿すと
    // 中身の入口が変わるので、切ったまま張り直さないと行き先が消えます
    rebuildTrackOutputConnections();

    // 経路をいったん切ったので、送り（5.2.2）とサイドチェイン（5.7.2）も張り直す
    rebuildSendConnections();
    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();

    return {};
}

void AudioEngine::removeInsertFromTrack (const juce::String& trackId, int insertIndex)
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）
    if (trackId.isEmpty())
    {
        if (! juce::isPositiveAndBelow (insertIndex, (int) masterInsertNodes.size()))
            return;

        project.getMasterBusInsertHost().removeInsert (insertIndex, nullptr);

        // プラグイン本体より先にエディタウィンドウを閉じる（HANDOVER 1.5）
        if (insertIndex < masterInsertEditorWindows.size())
            masterInsertEditorWindows.remove (insertIndex);

        auto nodeToRemove = masterInsertNodes[(size_t) insertIndex];
        masterInsertNodes.erase (masterInsertNodes.begin() + insertIndex);

        if (nodeToRemove != nullptr)
            graph.removeNode (nodeToRemove->nodeID);

        connectMasterChain();
        return;
    }

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || ! juce::isPositiveAndBelow (insertIndex, (int) nodes->insertNodes.size()))
        return;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getId() == trackId)
        {
            track.removeInsert (insertIndex, nullptr);
            break;
        }
    }

    disconnectTrackChain (*nodes);

    // プラグイン本体より先にエディタウィンドウを閉じる
    if (insertIndex < nodes->insertEditorWindows.size())
        nodes->insertEditorWindows.remove (insertIndex);

    auto nodeToRemove = nodes->insertNodes[(size_t) insertIndex];
    nodes->insertNodes.erase (nodes->insertNodes.begin() + insertIndex);

    if (nodeToRemove != nullptr)
        graph.removeNode (nodeToRemove->nodeID);

    connectTrackChain (*nodes);

    // 8.52：出口はまとめて張り直す（Phase 91。追加時と同じ理由）
    rebuildTrackOutputConnections();

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections(); // 同上（仕様書5.7.2）
}

void AudioEngine::moveInsertInTrack (const juce::String& trackId, int fromIndex, int toIndex)
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）
    if (trackId.isEmpty())
    {
        const int numInserts = (int) masterInsertNodes.size();

        if (! juce::isPositiveAndBelow (fromIndex, numInserts))
            return;

        toIndex = juce::jlimit (0, numInserts - 1, toIndex);

        if (fromIndex == toIndex)
            return;

        project.getMasterBusInsertHost().moveInsert (fromIndex, toIndex, nullptr);

        auto movingNode = masterInsertNodes[(size_t) fromIndex];
        masterInsertNodes.erase (masterInsertNodes.begin() + fromIndex);
        masterInsertNodes.insert (masterInsertNodes.begin() + toIndex, movingNode);

        while (masterInsertEditorWindows.size() < numInserts)
            masterInsertEditorWindows.add (nullptr);

        masterInsertEditorWindows.move (fromIndex, toIndex);

        connectMasterChain();
        return;
    }

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr)
        return;

    const int numInserts = (int) nodes->insertNodes.size();

    if (! juce::isPositiveAndBelow (fromIndex, numInserts))
        return;

    toIndex = juce::jlimit (0, numInserts - 1, toIndex);

    if (fromIndex == toIndex)
        return;

    // 8.66：**モデルの並びも一緒に動かす**（Phase 104）。
    // インサートは「番号」でモデルとグラフを対応させているので（8.63）、
    // 片方だけ動かすと**画面の名前と実際に掛かるプラグインがずれる**。
    //
    // 抜き差しと同じくUndoには積まない：モデルだけ巻き戻してもグラフの並びは戻らず、
    // 状態が食い違う（`addInsertToTrack()`と同じ理由）
    auto track = project.findTrackById (trackId);

    if (track.state.isValid())
        track.moveInsert (fromIndex, toIndex, nullptr);

    disconnectTrackChain (*nodes);

    auto movingNode = nodes->insertNodes[(size_t) fromIndex];
    nodes->insertNodes.erase (nodes->insertNodes.begin() + fromIndex);
    nodes->insertNodes.insert (nodes->insertNodes.begin() + toIndex, movingNode);

    // 開いているエディタウィンドウも同じ順に持っていく。
    // **先に本数を揃えておくこと**：この配列は必要になったぶんだけ後から伸ばす作りなので
    // （`openInsertEditor()`）、短いまま`move()`すると番号がずれる
    while (nodes->insertEditorWindows.size() < numInserts)
        nodes->insertEditorWindows.add (nullptr);

    nodes->insertEditorWindows.move (fromIndex, toIndex);

    connectTrackChain (*nodes);

    // 8.52：出口・送り・サイドチェインはまとめて張り直す（Phase 91。抜き差しと同じ理由）
    rebuildTrackOutputConnections();
    rebuildSendConnections();
    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();
}

juce::String AudioEngine::copyInsertToTrack (const juce::String& sourceTrackId, int sourceIndex,
                                              const juce::String& destTrackId, int destIndex)
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）。
    // マスターとトラックの行き来も、これで両方向とも通る
    auto sourceTrack = findInsertHost (sourceTrackId);
    auto destTrack   = findInsertHost (destTrackId);

    if (! sourceTrack.state.isValid() || ! destTrack.state.isValid())
        return utf8 ("トラックが見つかりません。");

    if (! juce::isPositiveAndBelow (sourceIndex, sourceTrack.getNumInserts()))
        return utf8 ("コピー元のインサートが見つかりません。");

    // 仕様書5.2.4：VCAは音声を通さないので、プラグインは挿せない
    if (destTrackId.isNotEmpty() && destTrack.getType() == TrackType::VCA)
        return utf8 ("VCAトラックにはインサートを挿せません。");

    auto sourceInsert = sourceTrack.getInsert (sourceIndex);

    juce::PluginDescription description;

    if (! sourceInsert.getDescription (description))
        return utf8 ("コピー元のプラグイン情報を読み取れませんでした。");

    // 8.66：**いま鳴っている設定を写す**（Phase 104）。
    // モデルに入っている`pluginState`は「最後に保存したときの中身」なので、
    // **今つまみを動かしたぶんは入っていない**。本体から取り直してから写す
    // 8.69：マスターも同じ関数で引ける（Phase 108。空文字＝マスター）
    captureStateFromNode (sourceInsert, findPluginNode (sourceTrackId, sourceIndex));

    const auto pluginState = sourceInsert.getPluginState();
    const bool bypassed    = sourceInsert.isBypassed();

    // **末尾へ挿してから動かす。** 挿す位置を指定できる経路をもう1本作ると、
    // グラフとエディタウィンドウの面倒を2箇所で見ることになる（8.2の「同じ判定を
    // 複数箇所に書くと、必ずずれる」）
    const auto error = addInsertToTrack (destTrackId, description);

    if (error.isNotEmpty())
        return error;

    const int addedIndex = destTrack.getNumInserts() - 1;

    if (addedIndex < 0)
        return utf8 ("インサートを追加できませんでした。");

    auto addedInsert = destTrack.getInsert (addedIndex);
    addedInsert.setPluginState (pluginState, nullptr);
    addedInsert.setBypassed (bypassed, nullptr);

    applyPluginStateToNode (findPluginNode (destTrackId, addedIndex), pluginState);

    if (juce::isPositiveAndBelow (destIndex, addedIndex))
        moveInsertInTrack (destTrackId, addedIndex, destIndex);
    else if (bypassed)
        rebuildGraphForBypassChange();   // 通さない指定を写したので、配線から外す

    return {};
}

void AudioEngine::rebuildGraphForBypassChange()
{
    // 8.63：**全トラックを張り直す**（Phase 101／改善案㉘）。
    //
    // 絞り込んでも安くなりません：1本のインサートを通さないだけでも、
    // **出口・送り・サイドチェインまで**張り直す必要があります（8.52と同じ理由）。
    for (auto& nodes : trackNodes)
    {
        disconnectTrackChain (nodes);
        connectTrackChain (nodes);
    }

    // 8.69：**マスターのインサートも同じボタンで一括切り替えできる**（Phase 108／D6）
    connectMasterChain();

    rebuildTrackOutputConnections();
    rebuildSendConnections();
    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();
}

void AudioEngine::openInsertEditor (const juce::String& trackId, int insertIndex)
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）
    if (trackId.isEmpty())
    {
        if (! juce::isPositiveAndBelow (insertIndex, (int) masterInsertNodes.size()))
            return;

        while (masterInsertEditorWindows.size() < (int) masterInsertNodes.size())
            masterInsertEditorWindows.add (nullptr);

        std::unique_ptr<juce::DocumentWindow> window (masterInsertEditorWindows.removeAndReturn (insertIndex));

        // 8.130：GUIの上のオン／オフ（Phase 166／改善案9）。
        // **モデルを引き直すこと**——ウィンドウは開いたまま残るので、
        // 掴んだときのインサートを覚えていると、差し替えた後に別のものを切り替えます（1.32）
        openEditorWindowFor (masterInsertNodes[(size_t) insertIndex], window,
                              [this, insertIndex]
                              {
                                  auto host = project.getMasterBusInsertHost();
                                  return juce::isPositiveAndBelow (insertIndex, host.getNumInserts())
                                          && ! host.getInsert (insertIndex).isBypassed();
                              },
                              [this, insertIndex] (bool shouldBeOn)
                              {
                                  auto host = project.getMasterBusInsertHost();

                                  if (! juce::isPositiveAndBelow (insertIndex, host.getNumInserts()))
                                      return;

                                  project.beginAction (utf8 ("インサートのオン／オフ"));
                                  host.getInsert (insertIndex).setBypassed (! shouldBeOn,
                                                                            &project.getUndoManager());
                                  rebuildGraphForBypassChange();
                              });

        masterInsertEditorWindows.insert (insertIndex, window.release());
        return;
    }

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || ! juce::isPositiveAndBelow (insertIndex, (int) nodes->insertNodes.size()))
        return;

    // ウィンドウ保持用の枠を、インサートの数だけ用意しておく
    while (nodes->insertEditorWindows.size() < (int) nodes->insertNodes.size())
        nodes->insertEditorWindows.add (nullptr);

    std::unique_ptr<juce::DocumentWindow> window (nodes->insertEditorWindows.removeAndReturn (insertIndex));

    // 8.130：GUIの上のオン／オフ（Phase 166／改善案9）。
    // **トラックはIDで引き直すこと**（番号もポインタも、後から別のものを指し得る。1.32）
    openEditorWindowFor (nodes->insertNodes[(size_t) insertIndex], window,
                          [this, trackId, insertIndex]
                          {
                              auto track = project.findTrackById (trackId);
                              return track.state.getParent().isValid()
                                      && juce::isPositiveAndBelow (insertIndex, track.getNumInserts())
                                      && ! track.getInsert (insertIndex).isBypassed();
                          },
                          [this, trackId, insertIndex] (bool shouldBeOn)
                          {
                              auto track = project.findTrackById (trackId);

                              if (! track.state.getParent().isValid()
                                   || ! juce::isPositiveAndBelow (insertIndex, track.getNumInserts()))
                                  return;

                              project.beginAction (utf8 ("インサートのオン／オフ"));
                              track.getInsert (insertIndex).setBypassed (! shouldBeOn,
                                                                         &project.getUndoManager());
                              rebuildGraphForBypassChange();
                          });
    nodes->insertEditorWindows.insert (insertIndex, window.release());
}

//==============================================================================
// 仕様書5.10：書き出し（ミックスダウン）
//==============================================================================

juce::String AudioEngine::renderMixdownToFile (const juce::File& file, const ExportOptions& options,
                                                std::function<bool (double)> progressCallback)
{
    if (masterNode == nullptr)
        return utf8 ("オーディオエンジンが初期化されていません。");

    stopRecording();
    transport.stop();

    auto* device = deviceManager.getCurrentAudioDevice();
    const double deviceSampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const int blockSize           = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    // 8.153：**書き出しのレートはデバイスと別**（Phase 191／8.1のD9a）。
    //
    // グラフごとこのレートで回します。**後から掛け直すのではありません**——
    // プラグインもこのレートで動くので、リバーブの尾も正しい長さになります。
    // クリップの音程が変わらないのは、Phase 190（8.152）でクリップ再生が
    // ファイルのレートを見るようになったからです
    const double sampleRate = getExportSampleRate (options);

    // オーディオデバイスのコールバックを外す。これをしないと、デバイス側と
    // このループの両方から同じグラフのprocessBlock()が呼ばれ、競合する。
    //
    // 重要：removeAudioCallback()は内部でaudioDeviceStopped()を呼び、そこから
    // graph.releaseResources()が各ノードへ伝播する。ClipPlayerProcessorは
    // releaseResources()で読み込み済みクリップを捨てるため、
    // **クリップの読み込みは必ずこの後で行うこと**（先に読み込むと消える）。
    deviceManager.removeAudioCallback (&player);

    graph.setNonRealtime (true);
    graph.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);

    // メトロノーム（Phase 38）はマスターの手前に繋がっているので、
    // **止めておかないと書き出したファイルにクリックが混ざる。**
    const bool metronomeWasEnabled = isMetronomeEnabled();
    setMetronomeEnabled (false);

    // 後始末をまとめておく。ここから先はどの経路で抜けても必ず通す必要がある
    // （通し忘れるとデバイスが繋がらないまま＝アプリの音が出なくなる）。
    // 8.153：**戻すのはデバイスのレート**（Phase 191／D9a）。
    // 書き出しのレートで戻すと、44.1kで1本出したあと**再生が44.1kのつもりのまま**になり、
    // 48kのデバイスで鳴らして音程がずれます
    auto restoreRealtimePlayback = [this, deviceSampleRate, blockSize, metronomeWasEnabled]
    {
        transport.stop();
        graph.setNonRealtime (false);
        graph.setPlayConfigDetails (2, 2, deviceSampleRate, blockSize);
        graph.prepareToPlay (deviceSampleRate, blockSize);
        transport.prepare (deviceSampleRate);

        setMetronomeEnabled (metronomeWasEnabled);

        deviceManager.addAudioCallback (&player);
    };

    // 各プレイヤーに内容を読み込ませ、書き出す長さを決める（play()と同じ手順）
    juce::int64 endPositionSamples = 0;
    prepareTrackPlayersForPlayback (endPositionSamples);

    if (endPositionSamples <= 0)
    {
        restoreRealtimePlayback();
        return utf8 ("書き出す内容がありません（クリップやノートを配置してください）。");
    }

    // 8.80：書き出す範囲（Phase 120/D11）。判断は`getExportRange()`1箇所
    juce::int64 startSample = 0;
    juce::int64 endSample = endPositionSamples;
    getExportRange (options, sampleRate, endPositionSamples, startSample, endSample);

    auto error = renderOfflineToFile (file, sampleRate, blockSize, startSample, endSample,
                                       options, false, progressCallback);

    restoreRealtimePlayback();
    return error;
}

//==============================================================================
// 8.83：MIDIキーボード等の入力（Phase 123／改善案⑬。仕様書5.4）
//==============================================================================

void AudioEngine::setMidiInputTargetTrackId (const juce::String& trackId)
{
    if (midiInputTargetTrackId == trackId)
        return;

    midiInputTargetTrackId = trackId;
    refreshMidiInputTargets();
}

void AudioEngine::refreshMidiInputTargets()
{
    // **鳴らす先を決める規則はここ1箇所**（1.26）。
    //
    //   1. 録音待機（アーム）中のMIDIトラックがあれば、**そこ全部**
    //   2. 無ければ、選んでいるMIDIトラック1本
    //
    // 1が優先なのは、**録る先と鳴る先を食い違わせない**ためです。
    // アームしてから別のトラックを選んでも、鳴るのはアームしたほうになります。
    std::vector<MidiInputTarget> targets;

    // 8.84：**絞り込みの条件もここで写し取る**（Phase 124／改善案⑯）。
    // MIDIスレッドからモデルを読みに行くと、書き換えとぶつかります（1.15）
    auto addTarget = [this, &targets] (const Track& track)
    {
        auto* nodes = findTrackNodes (track.getId());

        if (nodes == nullptr || nodes->midiPlayer == nullptr)
            return;

        MidiInputTarget target;
        target.player = nodes->midiPlayer;
        target.deviceName = track.getMidiInputDeviceName();
        target.inputChannel = track.getMidiInputChannel();
        target.outputChannel = track.getMidiOutputChannel();

        targets.push_back (target);
    };

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi && track.isArmed())
            addTarget (track);
    }

    if (targets.empty() && midiInputTargetTrackId.isNotEmpty())
    {
        auto track = findTrackById (midiInputTargetTrackId);

        if (track.state.getParent().isValid() && track.getType() == TrackType::Midi)
            addTarget (track);
    }

    const juce::ScopedLock lock (midiInputTargetLock);
    midiInputTargets = std::move (targets);
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    // **MIDIスレッドから呼ばれます。**
    //
    // ここで`trackNodes`やモデルをなめてはいけません——メッセージスレッドが
    // トラックを作り直している最中だと、消えたノードを掴みます（1.15）。
    // **先に決めておいた行き先と条件だけ**を、ロックを取って読みます。
    //
    // ロックを取るのは`refreshMidiInputTargets()`の**書き換えのあいだだけ**なので、
    // ここで待たされるのは一瞬です（オーディオスレッドではないので、
    // 万一待たされても音は途切れません）。
    const auto sourceName = (source != nullptr) ? source->getName() : juce::String();

    // 8.84：チャンネルを持たないメッセージ（システム系）は素通しにする。
    // `getChannel()`は持たないメッセージで0を返す
    const int messageChannel = message.getChannel();

    const juce::ScopedLock lock (midiInputTargetLock);

    for (const auto& target : midiInputTargets)
    {
        if (target.player == nullptr)
            continue;

        // デバイスの絞り込み（空なら「すべて」）
        if (target.deviceName.isNotEmpty() && target.deviceName != sourceName)
            continue;

        // 入力チャンネルの絞り込み（0なら「すべて」）
        if (target.inputChannel != 0 && messageChannel != 0 && messageChannel != target.inputChannel)
            continue;

        // 音源へ送るチャンネルの差し替え（0なら「そのまま」）
        if (target.outputChannel != 0 && messageChannel != 0)
        {
            auto remapped = message;
            remapped.setChannel (target.outputChannel);
            target.player->addLiveMidiMessage (remapped);
        }
        else
        {
            target.player->addLiveMidiMessage (message);
        }
    }
}

bool AudioEngine::shouldTrackSoundForStem (const juce::String& trackId, const juce::String& targetId) const
{
    if (trackId == targetId)
        return true;

    // 8.81：**対象の先祖のフォルダは通す**（Phase 121）。フォルダはバスなので、
    // 黙らせると中身の音がマスターまで届きません（8.51）
    auto target = project.findTrackById (targetId);

    if (target.state.getParent().isValid())
    {
        auto parentId = target.getParentFolderId();

        for (int guard = 0; guard < ProjectModel::maxFolderDepth && parentId.isNotEmpty(); ++guard)
        {
            if (parentId == trackId)
                return true;

            auto parent = project.findTrackById (parentId);

            if (! parent.state.getParent().isValid())
                break;

            parentId = parent.getParentFolderId();
        }

        // **対象がフォルダなら、その中身も通す**（黙らせると通すものが無くなる）
        if (target.getType() == TrackType::Folder
             && project.getFolderDescendantIds (targetId).contains (trackId))
            return true;
    }

    return false;
}

void AudioEngine::getExportRange (const ExportOptions& options, double sampleRate,
                                   juce::int64 songEndSamples,
                                   juce::int64& startOut, juce::int64& endOut) const
{
    startOut = 0;
    endOut = songEndSamples;

    if (! options.loopRangeOnly)
        return;

    // 8.80：ループ範囲だけ（Phase 120／D11）。
    //
    // **ループのON/OFFは見ません。** 「再生時に繰り返すか」と「どこを書き出すか」は
    // 別の話で、**範囲さえ引いてあれば使える**ほうが素直です（8.7）。
    const double loopStart = project.getLoopStartTime();
    const double loopEnd   = project.getLoopEndTime();

    if (loopEnd <= loopStart)
        return;   // 引かれていない（曲全体のまま）

    startOut = (juce::int64) (loopStart * sampleRate);
    endOut   = (juce::int64) (loopEnd * sampleRate);

    // **曲の終わりを超えた指定は詰める。** 超えたぶんは無音なので、
    // 書き出しても待たされるだけです
    endOut = juce::jlimit (startOut + 1, juce::jmax (startOut + 1, songEndSamples), endOut);
}

double AudioEngine::getExportSampleRate (const ExportOptions& options) const
{
    if (options.sampleRate > 0.0)
        return options.sampleRate;

    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr ? device->getCurrentSampleRate() : 48000.0;
}

juce::String AudioEngine::renderOfflineToFile (const juce::File& file, double sampleRate, int blockSize,
                                                juce::int64 startPositionSamples,
                                                juce::int64 endPositionSamples,
                                                const ExportOptions& options, bool writeMono,
                                                std::function<bool (double)> progressCallback)
{
    // リバーブ等の残響が切れないよう、末尾に余韻を足す
    const juce::int64 totalSamples = (endPositionSamples - startPositionSamples)
                                      + (juce::int64) (exportTailSeconds * sampleRate);

    if (totalSamples <= 0)
        return utf8 ("書き出す範囲がありません。");

    // 8.80：ビット深度とチャンネル数を選べるようにした（Phase 120／D9・D10）
    const int numOutputChannels = writeMono ? 1 : 2;

    std::unique_ptr<juce::AudioFormatWriter> writer;

    // 8.153：**形式で違うのはここだけ**（Phase 191／D9b）。
    // この下のループは、WAVでもMP3でも同じものを回します
    if (options.format == ExportOptions::Format::mp3)
    {
        juce::String mp3Error;
        writer = Mp3Writer::createWriter (file, sampleRate, numOutputChannels,
                                           options.mp3BitrateKbps, mp3Error);

        if (writer == nullptr)
            return mp3Error.isNotEmpty() ? mp3Error
                                          : utf8 ("MP3の書き出しを用意できませんでした。");
    }
    else
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();

        if (stream == nullptr)
            return utf8 ("ファイルを作成できませんでした: ") + file.getFullPathName();

        // **32bitはJUCEが浮動小数点で書きます**（`WavAudioFormat`が`bits == 32`を
        // そう扱う）。ダイアログの表記も「32 bit float」にしてあります。
        juce::WavAudioFormat wavFormat;
        writer = wavFormat.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                        .withSampleRate (sampleRate)
                                                        .withNumChannels (numOutputChannels)
                                                        .withBitsPerSample (options.bitsPerSample));

        if (writer == nullptr)
            return utf8 ("WAVファイルの書き出し形式を用意できませんでした。");
    }

    // 8.155：**書き出しのあいだはループを切る**（Phase 193／本人の報告）。
    //
    // `Transport::advance()`は、ループが入っていると**終端での自動停止より
    // 折り返しを優先します**（仕様書5.9。`Transport.h`）。切らずに回すと
    // 自動停止に一度も届かず、**書き出したファイルの末尾に「2周目の頭」が入ります**
    // ——「ループ範囲だけ」で出したときに、実際にそうなっていました
    // （余韻のぶん＝`exportTailSeconds`が、余韻ではなく2周目になる）。
    //
    // **曲全体を出すときも同じです。** ループが入ったままだと、
    // 曲の終わりへ進めないまま、ループの中身が繰り返し書かれます。
    //
    // **範囲はもう決まっている**（`getExportRange()`）ので、折り返しは要りません。
    // メトロノームと同じく、**戻すのはこの関数の中**（呼ぶ側に片付けさせない）
    const bool loopWasEnabled = transport.isLoopEnabled();
    const auto loopStart = transport.getLoopStartSamples();
    const auto loopEnd = transport.getLoopEndSamples();

    if (loopWasEnabled)
        transport.setLoop (false, loopStart, loopEnd);

    transport.prepare (sampleRate);
    transport.start (endPositionSamples, startPositionSamples);

    // **グラフは常に2chで回します。** モノラルはその結果をまとめるだけで、
    // 再生経路そのものは変えません（変えるとパンの効き方まで変わる）
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::AudioBuffer<float> monoBuffer (1, blockSize);
    juce::MidiBuffer midiBuffer;

    juce::String error;
    juce::int64 samplesRendered = 0;

    while (samplesRendered < totalSamples)
    {
        const int numSamples = (int) juce::jmin ((juce::int64) blockSize, totalSamples - samplesRendered);

        buffer.clear();
        midiBuffer.clear();

        // グラフのバッファは入出力兼用。クリアしてから渡すことで、
        // 入力（マイク）は無音として扱われる。
        juce::AudioBuffer<float> blockView (buffer.getArrayOfWritePointers(), 2, numSamples);
        graph.processBlock (blockView, midiBuffer);

        bool written = false;

        if (writeMono)
        {
            // 8.80：左右を**足して2で割る**（Phase 120／D10）。
            // 足すだけだと、両chに同じ音があるとき6dB上がって割れます
            monoBuffer.clear();
            monoBuffer.copyFrom (0, 0, blockView, 0, 0, numSamples);
            monoBuffer.addFrom (0, 0, blockView, 1, 0, numSamples);
            monoBuffer.applyGain (0, 0, numSamples, 0.5f);

            juce::AudioBuffer<float> monoView (monoBuffer.getArrayOfWritePointers(), 1, numSamples);
            written = writer->writeFromAudioSampleBuffer (monoView, 0, numSamples);
        }
        else
        {
            written = writer->writeFromAudioSampleBuffer (blockView, 0, numSamples);
        }

        if (! written)
        {
            error = utf8 ("ファイルへの書き込みに失敗しました。");
            break;
        }

        samplesRendered += numSamples;

        if (progressCallback != nullptr && ! progressCallback ((double) samplesRendered / (double) totalSamples))
        {
            error = utf8 ("書き出しを中断しました。");
            break;
        }
    }

    writer.reset(); // ここでファイルが閉じられる
    transport.stop();

    // 8.155：**引いてあった範囲はそのまま戻す**（Phase 193）。
    // 書き出しのために切っただけなので、ユーザーのループ設定は変えない
    if (loopWasEnabled)
        transport.setLoop (true, loopStart, loopEnd);

    if (error.isNotEmpty())
    {
        file.deleteFile(); // 中断・失敗時に中途半端なファイルを残さない
        return error;
    }

    return {};
}

//==============================================================================
juce::String AudioEngine::renderStemsToFolder (const juce::File& folder, const ExportOptions& options,
                                                std::function<bool (double)> progressCallback)
{
    if (masterNode == nullptr)
        return utf8 ("オーディオエンジンが初期化されていません。");

    stopRecording();
    transport.stop();

    auto* device = deviceManager.getCurrentAudioDevice();
    const double deviceSampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    const int blockSize           = device != nullptr ? device->getCurrentBufferSizeSamples() : 512;

    // 8.153：ミックスダウンと同じ（Phase 191／D9a。判断は`getExportSampleRate()`1箇所）
    const double sampleRate = getExportSampleRate (options);

    deviceManager.removeAudioCallback (&player); // ミックスダウンと同じ理由（この関数の上の説明を参照）

    graph.setNonRealtime (true);
    graph.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    graph.prepareToPlay (sampleRate, blockSize);

    // メトロノーム（Phase 38）はミックスダウンと同じ理由で止めておく
    const bool metronomeWasEnabled = isMetronomeEnabled();
    setMetronomeEnabled (false);

    // 8.153：**戻すのはデバイスのレート**（ミックスダウンと同じ。Phase 191）
    auto restoreRealtimePlayback = [this, deviceSampleRate, blockSize, metronomeWasEnabled]
    {
        transport.stop();
        graph.setNonRealtime (false);
        graph.setPlayConfigDetails (2, 2, deviceSampleRate, blockSize);
        graph.prepareToPlay (deviceSampleRate, blockSize);
        transport.prepare (deviceSampleRate);

        setMetronomeEnabled (metronomeWasEnabled);

        updateMixerSettings(); // 書き出し用に上書きしたミキサー設定を元へ戻す
        deviceManager.addAudioCallback (&player);
    };

    juce::int64 endPositionSamples = 0;
    prepareTrackPlayersForPlayback (endPositionSamples);

    if (endPositionSamples <= 0)
    {
        restoreRealtimePlayback();
        return utf8 ("書き出す内容がありません（クリップやノートを配置してください）。");
    }

    // 8.80：書き出す範囲（Phase 120／D11）。**ミックスダウンと同じ関数を通す**（8.2）
    juce::int64 startSample = 0;
    juce::int64 endSample = endPositionSamples;
    getExportRange (options, sampleRate, endPositionSamples, startSample, endSample);

    // 書き出す対象＝自分で音を出すトラック（オーディオ・MIDI）と、
    // 8.145：**パラアウトの受け皿**（Phase 183／本人の要望）。
    // センドトラックは単体では書き出さない。送り先の残響は、送り元のステムに含まれる形になる。
    //
    // 8.81：**外したトラックはここで落とす**（Phase 121/D10a）。
    // 「書き出してから捨てる」より、**そもそも回さない**ほうが速い
    std::vector<juce::String> stemTrackIds;

    for (auto& nodes : trackNodes)
        if (! nodes.isSendTrack && options.shouldExportStem (nodes.trackId))
            stemTrackIds.push_back (nodes.trackId);

    if (stemTrackIds.empty())
    {
        restoreRealtimePlayback();
        return utf8 ("書き出すトラックが選ばれていません。");
    }

    juce::String error;

    // 8.82：**この書き出しで使った名前**（Phase 122）。同じ名前のトラックがあっても
    // 上書きし合わないようにするため。**書き出しごとに空から始める**ので、
    // 同じ曲を出し直したときは前のファイルを素直に上書きします
    juce::StringArray usedStemNames;

    for (size_t i = 0; i < stemTrackIds.size(); ++i)
    {
        const auto& targetId = stemTrackIds[i];

        // 対象トラック以外を黙らせる。センドトラックは鳴らしたままにするので、
        // そのトラックが送っているリバーブ等の残響はステムに含まれる。
        // （結果として、全ステムを足すとミックス全体とほぼ一致する）
        for (auto& nodes : trackNodes)
        {
            if (nodes.channel == nullptr)
                continue;

            auto track = findTrackById (nodes.trackId);

            // 8.81：**通り道も通す**（Phase 121）。フォルダはバスなので、
            // 対象だけ残すと**フォルダの中身も、フォルダ自身も無音**になります
            const bool shouldSound = nodes.isSendTrack
                                      || shouldTrackSoundForStem (nodes.trackId, targetId);

            // 仕様書5.2.4：VCAのオフセットはステムにも効かせる
            // （ミックスで聞こえていた音量のまま書き出されるようにするため）
            const auto vca = project.getVcaInfluenceFor (nodes.trackId);

            nodes.channel->setMixSettings (track.state.isValid() ? track.getVolumeDb() : 0.0f,
                                            track.state.isValid() ? track.getPan() : 0.0f,
                                            shouldSound && ! vca.muted,
                                            vca.offsetDb);
        }

        auto track = findTrackById (targetId);
        // 8.82：ファイル名は **(プロジェクト名)_(トラック名)**（Phase 122）。
        //
        // Phase 121までは`01_トラック名.wav`でした。複数の曲のステムを同じフォルダへ
        // 集めると、**どの曲のものか分からなくなります**（`01_Drums.wav`が並ぶ）。
        //
        // **番号を外したので、名前がぶつかり得ます**（同じ名前のトラックは作れる）。
        // 後から書いたものが前のものを上書きしないよう、2本目からは`_2`を足します
        const auto safeProjectName = juce::File::createLegalFileName (project.getName());
        const auto safeTrackName = juce::File::createLegalFileName (track.getName());

        auto baseName = (safeProjectName.isNotEmpty() ? safeProjectName + "_" : juce::String())
                          + safeTrackName;

        if (baseName.isEmpty())
            baseName = utf8 ("トラック") + juce::String ((int) i + 1);

        auto uniqueName = baseName;

        for (int suffix = 2; usedStemNames.contains (uniqueName); ++suffix)
            uniqueName = baseName + "_" + juce::String (suffix);

        usedStemNames.add (uniqueName);

        // 8.153：**拡張子は`options`から引く**（Phase 191／D9b。判定を書き写さない）
        auto stemFile = folder.getChildFile (uniqueName + options.getFileExtension());

        // 進捗は「何本目か」で按分する
        const double stemStart = (double) i / (double) stemTrackIds.size();
        const double stemSpan  = 1.0 / (double) stemTrackIds.size();

        error = renderOfflineToFile (stemFile, sampleRate, blockSize, startSample, endSample,
                                      options, options.isStemMono (targetId),
            [progressCallback, stemStart, stemSpan] (double progressWithinStem)
            {
                if (progressCallback == nullptr)
                    return true;

                return progressCallback (stemStart + progressWithinStem * stemSpan);
            });

        if (error.isNotEmpty())
            break;
    }

    restoreRealtimePlayback();
    return error;
}

void AudioEngine::updateInputConnections()
{
    if (inputNode == nullptr || recorderNode == nullptr)
        return;

    // 既存の入力配線をいったん全て外す。デバイスが変わるとチャンネル数も変わるため、
    // 古い配線が残っていると、存在しないチャンネルを指したままになり得る。
    graph.removeConnection ({ { inputNode->nodeID, 0 }, { recorderNode->nodeID, 0 } });
    graph.removeConnection ({ { inputNode->nodeID, 0 }, { recorderNode->nodeID, 1 } });
    graph.removeConnection ({ { inputNode->nodeID, 1 }, { recorderNode->nodeID, 1 } });

    auto* device = deviceManager.getCurrentAudioDevice();
    const int numInputChannels = device != nullptr ? device->getActiveInputChannels().countNumberOfSetBits() : 0;

    inputAvailable = (numInputChannels > 0);

    if (! inputAvailable)
    {
        inputError = device == nullptr
                        ? utf8 ("オーディオデバイスが開かれていません。")
                        : utf8 ("選択中のデバイスに有効な入力チャンネルがありません。");
        return;
    }

    inputError = {};

    // ノートPC内蔵マイクやUSBマイクのように入力が1chしか無い場合、入力ノードの
    // チャンネル1は存在しない。その場合は同じ入力を左右両方へ配り、モノラル入力として扱う。
    const int rightSourceChannel = (numInputChannels >= 2 ? 1 : 0);

    graph.addConnection ({ { inputNode->nodeID, 0 }, { recorderNode->nodeID, 0 } });
    graph.addConnection ({ { inputNode->nodeID, rightSourceChannel }, { recorderNode->nodeID, 1 } });
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source != &deviceManager)
        return;

    // 8.85：**変わったらその場で覚える**（Phase 125/設計書2.5）。
    //
    // 終了時にまとめて書かないのは、**落ちたときに設定が消える**からです。
    // デバイスを触るのは頻繁ではないので、そのたびに書いて構いません。
    //
    // **MIDI入力の有効／無効もここに入っています**（`createStateXml()`が
    // `<MIDIINPUT>`として書き出す）
    saveAudioDeviceState();

    updateInputConnections();

    if (onAudioDeviceChanged != nullptr)
        onAudioDeviceChanged();
}

juce::String AudioEngine::getInputDeviceDescription() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return utf8 ("デバイス未接続");

    const int numInputChannels = device->getActiveInputChannels().countNumberOfSetBits();

    juce::String text;
    text << device->getTypeName() << " / " << device->getName() << " (" << numInputChannels << "ch in)";
    return text;
}

namespace
{
    /**
        8.85：オーディオ設定の画面（Phase 125）。

        JUCE標準のデバイス選択UIの下に、**いまのレイテンシ**を出します。

        ### なぜ数字を出すのか

        バッファサイズは前から選べましたが、**変えた結果が数字で見えません**でした。
        「小さくすれば軽くなる」ことは分かっても、
        **どれくらい軽くなったのか／もう十分なのか**が判断できません。

        ### PDCもここに出す

        **プラグインの遅延補正（PDC）は、鍵盤の遅れに直結します。**
        レイテンシの大きいプラグイン（リニアフェイズEQ、ルックアヘッド付きの
        コンプ等）を1つ挿しただけで、バッファをいくら小さくしても遅いままです。

        バッファと並べて出すことで、**どちらが効いているか**が分かります。
    */
    class AudioSettingsComponent : public juce::Component,
                                    private juce::Timer
    {
    public:
        AudioSettingsComponent (juce::AudioDeviceManager& deviceManager, AudioEngine& engineToUse)
            : engine (engineToUse)
        {
            selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
                           deviceManager,
                           0, 2,   // 入力チャンネル数の許容範囲（0〜2ch）
                           0, 2,   // 出力チャンネル数の許容範囲（0〜2ch）
                           true,   // 8.83：MIDI入力の選択欄（Phase 123／改善案⑬）
                           false,  // MIDI出力の選択欄
                           true,   // 入出力チャンネルをステレオペアとして表示する
                           false); // 詳細設定を初期状態で開かない

            addAndMakeVisible (*selector);

            latencyLabel.setFont (juce::FontOptions (12.0f));
            latencyLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
            latencyLabel.setJustificationType (juce::Justification::topLeft);
            addAndMakeVisible (latencyLabel);

            updateLatencyText();

            // **ゆっくりで十分。** デバイスを選び直したときとプラグインを挿したときに
            // 変わるだけなので、秒に2回も見れば足ります
            startTimerHz (2);
        }

        void resized() override
        {
            auto area = getLocalBounds();

            latencyLabel.setBounds (area.removeFromBottom (latencyRowHeight).reduced (4, 2));
            selector->setBounds (area);
        }

    private:
        void timerCallback() override { updateLatencyText(); }

        void updateLatencyText()
        {
            const auto text = engine.getLatencyDescription();

            if (text == lastText)
                return;   // 変わらないなら描き直さない

            lastText = text;
            latencyLabel.setText (text, juce::dontSendNotification);
        }

        static constexpr int latencyRowHeight = 72;

        AudioEngine& engine;
        std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
        juce::Label latencyLabel;
        juce::String lastText;
    };
}

void AudioEngine::saveAudioDeviceState()
{
    // 8.85：デバイスの設定を覚える（Phase 125/設計書2.5）。
    // **MIDI入力の有効/無効もここに入っています**（`createStateXml()`が`<MIDIINPUT>`として書く）
    if (auto state = deviceManager.createStateXml())
        AppSettings::setString (audioDeviceStateKey, state->toString());
}

juce::String AudioEngine::getLatencyDescription() const
{
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return utf8 ("オーディオデバイスが開かれていません。");

    const double sampleRate = device->getCurrentSampleRate();

    if (sampleRate <= 0.0)
        return utf8 ("オーディオデバイスが開かれていません。");

    auto toMs = [sampleRate] (int samples) { return samples * 1000.0 / sampleRate; };

    const int bufferSamples = device->getCurrentBufferSizeSamples();
    const double bufferMs = toMs (bufferSamples);
    const double outputMs = toMs (device->getOutputLatencyInSamples());
    const double inputMs  = toMs (device->getInputLatencyInSamples());
    const double pdcMs    = toMs (getTotalLatencySamples());

    juce::String text;
    text << utf8 ("バッファ ") << bufferSamples << utf8 (" サンプル（")
         << juce::String (bufferMs, 1) << utf8 (" ms）／出力 ")
         << juce::String (outputMs, 1) << utf8 (" ms ／ 入力 ")
         << juce::String (inputMs, 1) << utf8 (" ms")
         << juce::newLine
         << utf8 ("プラグイン遅延補正（PDC）: ") << juce::String (pdcMs, 1) << utf8 (" ms")
         << juce::newLine
         << utf8 ("鍵盤を弾いてから鳴るまで：おおよそ ")
         << juce::String (bufferMs + outputMs + pdcMs, 1) << utf8 (" ms")
         << utf8 ("（小さくしたいときはバッファサイズを下げてください。"
                   "下げすぎると音が途切れます）");

    // 8.85：**いちばん効くのはドライバ種別**（Phase 125）。
    // Windows AudioやDirectSoundは、バッファをいくら下げても数十msから下がりません
    if (device->getTypeName().containsIgnoreCase ("ASIO"))
        return text;

    text << juce::newLine
         << utf8 ("※ ドライバ種別にASIOを選べる環境では、そちらのほうがずっと小さくなります。");

    return text;
}

std::unique_ptr<juce::Component> AudioEngine::createAudioSettingsComponent()
{
    // JUCE標準のデバイス選択UI。ドライバ種別（Windows Audio / DirectSound等）、
    // 入出力デバイス、サンプルレート、バッファサイズをまとめて設定できる。
    //
    // **Phase 26で「独立ダイアログを開く」から「コンポーネントを返す」へ変えた。**
    // 環境設定（仕様書6.2・設計書2.3.8）のAudioカテゴリへ埋め込むため。
    //
    // 8.85：**下にレイテンシの数字を足した**（Phase 125）。
    // バッファサイズを変えた結果が見えないと、どこまで下げればよいか判断できません
    return std::make_unique<AudioSettingsComponent> (deviceManager, *this);
}

namespace
{
    /** プラグインのエディタを表示するだけの、ごく薄いウィンドウ。

        「×」で閉じたときは**自分を破棄せず、隠すだけ**にしている。
        以前はコールバックで保持側のunique_ptrをnullptrにしていたが、
        - ウィンドウ自身のcloseButtonPressed()の中で自分を破棄することになり、
          呼び出し元のスタックが解放済みメモリを触る
        - 保持先が可変長コンテナ（インサートの配列）だと、参照が無効になり得る
        という2点で危険だった（実際にインサートのGUIを閉じるとアプリごと落ちた）。

        実体の破棄は、プラグインを外すときとエンジン終了時に保持側が行う。 */
    class PluginEditorWindow : public juce::DocumentWindow
    {
    public:
        explicit PluginEditorWindow (const juce::String& name)
            // 8.154：**最小化ボタンを足しました**（Phase 192/本人の要望）。
            // プラグインのGUIは大きく、いくつも開くと下のタイムラインが見えません。
            // ×は「隠す」（この下）なので閉じても状態は残りますが、
            // **開き直す手間が要ります**——最小化ならタスクバーから戻せます
            : DocumentWindow (name, juce::Colours::darkgrey,
                               DocumentWindow::minimiseButton | DocumentWindow::closeButton)
        {
        }

        void closeButtonPressed() override
        {
            setVisible (false);
        }
    };

    //==========================================================================
    /** 8.130：**プラグインのGUIの上に、オン／オフの帯を1本足す**（Phase 166／改善案9）。

        「掛けた音と掛けない音を聴き比べる」のに、いちいちConsoleやインスペクタへ
        戻るのが手間だった、という要望です。**聴きながら押せる場所**へ置きます。

        ### 大きさの決まり方

        **プラグインのエディタは自分で大きさを変えます**（表示を切り替えると縮む、など）。
        なので**こちらがエディタを引き伸ばしてはいけません**——位置だけ決めて、
        大きさはエディタに任せ、**自分がそれに合わせて**大きくなります。

        `resized()`でエディタの大きさまで決めると、`childBoundsChanged()`が呼ばれて
        また`resized()`へ戻り、**行ったり来たりが止まらなくなります**。 */
    class PluginEditorContent : public juce::Component
    {
    public:
        PluginEditorContent (juce::AudioProcessorEditor* editorToOwn,
                              std::function<bool()> isOn,
                              std::function<void (bool)> setOn)
            : editor (editorToOwn), getOnState (std::move (isOn)), applyOnState (std::move (setOn))
        {
            addAndMakeVisible (*editor);

            // **切り替える手立てが無いところではボタンを出さない**
            // （押せないボタンが1つ増えるだけになる）
            if (getOnState != nullptr && applyOnState != nullptr)
            {
                powerButton.setClickingTogglesState (true);
                powerButton.setIconResource ("plugin_bypass_svg");
                powerButton.setIconInset (2.5f);
                powerButton.setTooltip (utf8 ("掛けた音と掛けない音を聴き比べる"));
                powerButton.setToggleState (getOnState(), juce::dontSendNotification);
                updatePowerButtonLook();

                powerButton.onClick = [this]
                {
                    if (applyOnState != nullptr)
                        applyOnState (powerButton.getToggleState());

                    updatePowerButtonLook();
                };

                addAndMakeVisible (powerButton);
            }

            // 8.168：**プラグインの窓を前面に留める**（Phase 206／本人の質問）。
            //
            // 「DAW画面を選択するとプラグイン画面が表示から外れる」——
            // 普通のウィンドウなので、後ろへ回るのが既定の動きです。
            //
            // **窓ごとに切り替えられます**（ここのボタン）。
            // 押した状態は`AppSettings`に残り、**次に開く窓の既定**になります
            // （既に開いている窓は変わりません——2つ並べて片方だけ留める、
            //   という使い方ができるように）。
            pinButton.setClickingTogglesState (true);
            pinButton.setIconResource ("plugin_pin_svg");
            pinButton.setIconInset (3.5f);
            pinButton.setToggleState (alwaysOnTopIsDefault(), juce::dontSendNotification);
            updatePinButtonLook();

            pinButton.onClick = [this]
            {
                const bool shouldPin = pinButton.getToggleState();

                if (auto* window = getTopLevelComponent())
                    window->setAlwaysOnTop (shouldPin);

                AppSettings::setInt (alwaysOnTopKey, shouldPin ? 1 : 0);
                updatePinButtonLook();
            };

            pinButton.setTooltip (utf8 ("この窓を常に前面に出したままにする"));
            addAndMakeVisible (pinButton);

            setSize (juce::jmax (minimumWidth, editor->getWidth()),
                      editor->getHeight() + headerHeight);
        }

        /** 次に開く窓を前面に留めるか（`AppSettings`。**既定は留める**）。 */
        static bool alwaysOnTopIsDefault() { return AppSettings::getInt (alwaysOnTopKey, 1) != 0; }

        void paint (juce::Graphics& g) override
        {
            g.setColour (AppColours::panel);
            g.fillRect (getLocalBounds().removeFromTop (headerHeight));

            g.setColour (AppColours::border);
            g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());
        }

        void resized() override
        {
            auto header = getLocalBounds().removeFromTop (headerHeight);

            if (powerButton.isVisible())
                powerButton.setBounds (header.removeFromLeft (headerButtonWidth).reduced (2, 2));

            pinButton.setBounds (header.removeFromRight (headerButtonWidth).reduced (2, 2));

            // **位置だけ決める。** 大きさはエディタが自分で決めます
            editor->setTopLeftPosition (0, headerHeight);
        }

        void childBoundsChanged (juce::Component* child) override
        {
            if (child != editor.get())
                return;

            const int wanted = editor->getHeight() + headerHeight;

            // **同じなら何もしない**（呼び戻しが止まらなくなる）
            if (getWidth() != editor->getWidth() || getHeight() != wanted)
                setSize (juce::jmax (minimumWidth, editor->getWidth()), wanted);
        }

    private:
        void updatePowerButtonLook()
        {
            const bool isOn = powerButton.getToggleState();

            // **オンが既定の状態**なので、目立たせるのは「切ってある」ほう。
            // 切ってあることに気づかずに「音が変わらない」と悩むのを防ぐ
            powerButton.setButtonText (isOn ? utf8 ("オン") : utf8 ("バイパス中"));
            powerButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
            powerButton.setColour (juce::TextButton::buttonOnColourId, AppColours::background);
            powerButton.setColour (juce::TextButton::textColourOffId, AppColours::orange);
            powerButton.setColour (juce::TextButton::textColourOnId, AppColours::textPrimary);
        }

        void updatePinButtonLook()
        {
            const bool pinned = pinButton.getToggleState();

            pinButton.setButtonText (pinned ? utf8 ("前面固定") : utf8 ("固定なし"));
            pinButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
            pinButton.setColour (juce::TextButton::buttonOnColourId, AppColours::background);
            pinButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
            pinButton.setColour (juce::TextButton::textColourOnId, AppColours::purple);
        }

        static constexpr int headerHeight = 28;
        static constexpr int minimumWidth = 220;   // 帯のボタンが2つ入る最低限
        static constexpr int headerButtonWidth = 30;

        std::unique_ptr<juce::AudioProcessorEditor> editor;

        /** 8.171：**絵のボタン**（Phase 210／本人が用意）。

            `IconAssets::SvgButton`は**文字の色（`textColourOffId`／`textColourOnId`）で
            絵を塗ります**。色の決め方は文字だったときのままなので、
            押されているときの見え方も変わりません。

            **文字も設定したまま残してあります**——`AppColours::useTransportIcons`を
            `false`にすれば、そのまま文字のボタンへ戻ります（8.132と同じ形）。 */
        IconAssets::SvgButton powerButton, pinButton;

        std::function<bool()> getOnState;
        std::function<void (bool)> applyOnState;
    };
}

void AudioEngine::openEditorWindowFor (juce::AudioProcessorGraph::Node::Ptr node,
                                        std::unique_ptr<juce::DocumentWindow>& windowMember,
                                        std::function<bool()> isOn,
                                        std::function<void (bool)> setOn)
{
    if (node == nullptr)
        return;

    auto* processor = node->getProcessor();

    if (processor == nullptr)
        return;

    // 既に作ってあるなら、作り直さず表示して最前面へ出す。
    // 「×」で閉じたウィンドウは破棄されず隠れているだけなので、ここで復帰する。
    if (windowMember != nullptr)
    {
        windowMember->setVisible (true);
        windowMember->toFront (true);
        return;
    }

    auto* editor = processor->hasEditor() ? processor->createEditorAndMakeActive() : nullptr;

    // 独自GUIを持たないプラグイン（あるいはエディタ生成に失敗した場合）は、
    // JUCE標準のパラメータ一覧で代用する。「GUIが無いから何も確認できない」
    // という状態を作らないため（設計書3.6でパラメータを列挙する話とも繋がる）。
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (*processor);

    auto window = std::make_unique<PluginEditorWindow> (processor->getName());
    window->setUsingNativeTitleBar (true);

    // 8.130：**オン／オフの帯を挟む**（Phase 166／改善案9）。
    //
    // 8.168：**バイパスできないものにも挟みます**（Phase 206）。
    // 前面固定のボタンは**どのプラグインにも要る**ためです
    // （オン／オフのほうは、切り替える手立てが無ければ出ません）。
    // 8.172：**中身が伸びないなら、窓も伸ばさせない**（Phase 212）。
    //
    // 大きさを固定したエディタ（Manta EQ／Manta Comp）を伸縮できる窓に入れると、
    // 掴んで広げたぶんが**そのまま余白**になります——「動かせるのに何も起きない」
    // という、いちばん分かりにくい壊れ方です。
    // 伸びるかどうかは**エディタ自身が知っている**ので、それを聞いて決めます
    const bool editorCanResize = editor->isResizable();

    window->setContentOwned (new PluginEditorContent (editor, std::move (isOn), std::move (setOn)), true);

    window->setResizable (editorCanResize, false);
    window->centreWithSize (window->getWidth(), window->getHeight());

    // 8.168：**前面に留める**（Phase 206／本人の質問）。
    // 既定は留めます——DAWの画面を触るたびに隠れると、
    // 「触りながら聴く」ができません
    window->setAlwaysOnTop (PluginEditorContent::alwaysOnTopIsDefault());

    window->setVisible (true);

    windowMember = std::move (window);
}

//==============================================================================
// メトロノーム（Phase 38）
//==============================================================================

void AudioEngine::setMetronomeEnabled (bool shouldBeEnabled)
{
    if (metronomeProcessor != nullptr)
        metronomeProcessor->setEnabled (shouldBeEnabled);
}

bool AudioEngine::isMetronomeEnabled() const
{
    return metronomeProcessor != nullptr && metronomeProcessor->isEnabled();
}

void AudioEngine::updateMetronomeTiming (const TempoMap& tempoMap)
{
    if (metronomeProcessor == nullptr)
        return;

    // Phase 141：**テンポと拍子は表で渡す**（曲の途中で変わるため。8.103）。
    // 画面のルーラーと同じ表なので、線とクリックが食い違いようがない
    metronomeProcessor->setTempoMap (tempoMap);
}

void AudioEngine::setMetronomeGain (float gain)
{
    if (metronomeProcessor != nullptr)
        metronomeProcessor->setGain (gain);
}

float AudioEngine::getMetronomeGain() const
{
    return metronomeProcessor != nullptr ? metronomeProcessor->getGain() : 0.5f;
}

bool AudioEngine::isCountingIn() const
{
    return metronomeProcessor != nullptr && metronomeProcessor->isCountingIn();
}

juce::String AudioEngine::startRecordingWithCountIn (int countInBars)
{
    // 8.146：**録るものはアームで決まる**（Phase 184／改善案⑬a。仕様書5.4）。
    //
    // | アームしているもの | 録るもの |
    // |---|---|
    // | MIDIトラックだけ | **MIDIだけ**（WAVは作らない） |
    // | オーディオトラックがある | 今までどおりオーディオ＋（あれば）MIDI |
    // | 何もアームしていない | 今までどおりオーディオ |
    //
    // MIDIだけのときにWAVを作らないのは、**録るつもりの無い音のファイルが
    // 毎回1本できて、アレンジにクリップとして置かれる**からです
    const bool midiArmed = hasArmedMidiTracks();
    const bool wantAudio = (! midiArmed) || hasArmedAudioTracks();

    if (wantAudio)
    {
        auto error = startRecording();

        // **MIDIが録れるなら、入力が無くても止めない。** オーディオインターフェイスの
        // 無い機械でも鍵盤は録れます（そもそも入力を要らないのがMIDI録音です）
        if (error.isNotEmpty() && ! midiArmed)
            return error;
    }
    else
    {
        // クリップを置く位置＝録り始めの位置。オーディオのときは
        // `startRecording()`が入れるので、こちらは入れなかった場合だけ
        recordStartSeconds = transport.getPositionSeconds();
    }

    // 再生に必要な準備（クリップ・ノートの写し取り。HANDOVER 1.12）は
    // **カウントインの前に**済ませておく。オーディオスレッドが
    // トランスポートを開始した瞬間には、もう読み込みが終わっている必要がある。
    juce::int64 endPositionSamples = 0;
    prepareTrackPlayersForPlayback (endPositionSamples);

    // 8.146：アームしたMIDIトラックの再生器へ「溜め始めろ」と言う（Phase 184）。
    //
    // **`prepareTrackPlayersForPlayback()`の後**に置くこと——あちらが
    // ノートを詰め直すので、先に立てても意味が無いどころか、
    // 作り直しに巻き込まれると印だけ消えます
    recordedMidiTakes.clear();
    midiRecordingActive = false;

    if (midiArmed)
    {
        const auto latency = getMidiRecordLatencySamples();

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getType() != TrackType::Midi || ! track.isArmed())
                continue;

            if (auto* nodes = findTrackNodes (track.getId()))
                if (nodes->midiPlayer != nullptr)
                {
                    nodes->midiPlayer->startMidiRecording (latency);
                    midiRecordingActive = true;
                }
        }
    }

    // **どちらも始まらなかったなら、転がさずに知らせる。** ここを通さないと
    // 「Recが点いているのに何も録れていない」状態になります
    if (! isAudioRecording() && ! midiRecordingActive)
        return utf8 ("録音を始められませんでした。オーディオ入力を選ぶか、"
                      "MIDIトラックを録音待機（Rec）にしてください。");

    const juce::int64 startPosition = transport.getPositionSamples();

    // 仕様書5.4：**録音中は自動停止させない**（Phase 40）。
    // play()は既存クリップの終端で止まるようにトランスポートへ終端を渡すが、
    // 録音でそれをやると「既に置いてあるクリップの終わりで録音が切れる」ことになる。
    // 空のプロジェクトでは終端が0（＝自動停止しない）ため、長く気づかなかった。
    constexpr juce::int64 noAutoStop = 0;

    // 仕様書5.6：書き込みモードの状態を作り直す（play()と同じ理由。Phase 20）
    for (auto& writeState : automationWriteStates)
    {
        writeState.recording = false;
        writeState.lastWrittenValue = -1.0f;
    }

    if (countInBars <= 0 || metronomeProcessor == nullptr)
    {
        // カウントイン無し：その場で始める
        transport.start (noAutoStop, startPosition);
    }
    else
    {
        // ここではトランスポートを**開始しない**。数え終わった時点で
        // MetronomeProcessorがオーディオスレッドから開始する（HANDOVER 1.38）。
        metronomeProcessor->startCountIn (countInBars, startPosition, noAutoStop);
    }

    // プラグインパラメータの適用と記録のタイマー（play()と同じ）
    startTimerHz (automationUpdateHz);

    return {};
}

//==============================================================================
void AudioEngine::updateMixerSettings()
{
    // ソロが1つでも立っていれば、ソロでないトラックは鳴らさない。
    // **判定はProjectModel側に集約してある**（Phase 53）。MIDI書き出しも同じものを
    // 通しているので、「鳴っている内容」と「書き出した内容」がずれない（HANDOVER 8.2）。
    // ここで1回だけ数えて、各トラックの判定へ渡す。
    const bool anyTrackSoloed = project.isAnySoloActive();

    for (auto& nodes : trackNodes)
    {
        if (nodes.channel == nullptr)
            continue;

        // trackIdでモデル側のトラックを引き当てる（並び順ではなくIDで対応させることで、
        // ノードの作り直しと行き違っても別トラックの設定を適用してしまわない）
        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getId() != nodes.trackId)
                continue;

            // 仕様書5.2.4：リンク先VCAのゲインオフセットは、ここでしか使わない。
            // ミュート／ソロの解釈は`isTrackAudible()`が持っている（同じ判定を
            // 各所に書くとずれるため。HANDOVER 8.2）。
            const auto vca = project.getVcaInfluenceFor (nodes.trackId);
            const bool audible = project.isTrackAudible (track, anyTrackSoloed);

            nodes.channel->setMixSettings (track.getVolumeDb(), track.getPan(), audible, vca.offsetDb);

            // 仕様書5.2.2：送り量も反映する（配線は変えず、ゲインだけ更新する）
            const int numToUpdate = juce::jmin (track.getNumSends(), (int) nodes.sendNodes.size());

            for (int s = 0; s < numToUpdate; ++s)
                if (nodes.sendNodes[(size_t) s].gain != nullptr)
                    nodes.sendNodes[(size_t) s].gain->setLevelDb (track.getSend (s).getLevelDb());

            break;
        }
    }

    if (masterProcessor != nullptr)
        masterProcessor->setVolumeDb (project.getMasterVolumeDb());
}

float AudioEngine::getTrackLevel (const juce::String& trackId, int channel) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || nodes->channel == nullptr)
        return 0.0f;

    return nodes->channel->getLevel (channel);
}

float AudioEngine::getMasterLevel (int channel) const
{
    return masterProcessor != nullptr ? masterProcessor->getOutputLevel (channel) : 0.0f;
}

void AudioEngine::play()
{
    // 8.162：**始めるときにテンポの表を取り直す**（Phase 200）。
    // プロジェクトを開き直す道では、通知を伴わずに表が入れ替わります——
    // 古い表のまま「いま何拍目か」を測ると、最初のテンポ変更で位置が飛ぶ
    lastKnownTempoMap = project.getTempoMap();

    // 各プレイヤーに、現在のモデルの内容を読み込み直させる
    juce::int64 endPositionSamples = 0;

    prepareTrackPlayersForPlayback (endPositionSamples);

    // 仕様書5.9：現在のプレイヘッド位置から再生する（Phase 18）。
    // 終端より後ろにいる場合は、そこから始めても何も鳴らないので先頭へ戻す。
    juce::int64 startPosition = transport.getPositionSamples();

    if (endPositionSamples > 0 && startPosition >= endPositionSamples)
        startPosition = 0;

    // 仕様書5.6：書き込みモードの状態を作り直す（Phase 20）。
    // 前回の再生で「記録中」のまま残っていると、Playした瞬間から書き始めてしまう。
    for (auto& writeState : automationWriteStates)
    {
        writeState.recording = false;
        writeState.lastWrittenValue = -1.0f;
    }

    // 仕様書5.9：ループ中は自動停止しない（Phase 48）。
    // **ループの終わりがクリップの終端より後ろにあると、折り返す前に止まってしまう。**
    // 空の小節を含めてループしたいことは普通にあるので、ループ中は終端を見ない。
    if (transport.isLoopEnabled() && transport.getLoopEndSamples() > transport.getLoopStartSamples())
        endPositionSamples = 0;

    // 全トラックのうち最も遅い終端で自動停止する。
    // 中身が空（0）のときは自動停止しない＝停止操作があるまで走り続ける。
    transport.start (endPositionSamples, startPosition);

    // プラグインパラメータの適用と記録はタイマーで回す（AudioEngine.hの説明を参照）
    startTimerHz (automationUpdateHz);
}

//==============================================================================
// 仕様書5.6：プラグインパラメータのオートメーションと書き込みモード（Phase 20）
//==============================================================================

juce::AudioProcessorGraph::Node::Ptr AudioEngine::findPluginNode (const juce::String& trackId,
                                                                    int insertIndex) const
{
    // 8.69：**空文字＝マスター**（Phase 108／D6）。マスターは音源を持たないので、
    // インサートだけを引く
    if (trackId.isEmpty())
    {
        if (! juce::isPositiveAndBelow (insertIndex, (int) masterInsertNodes.size()))
            return nullptr;

        return masterInsertNodes[(size_t) insertIndex];
    }

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr)
        return nullptr;

    if (insertIndex < 0)
        return nodes->instrumentNode;

    if (! juce::isPositiveAndBelow (insertIndex, (int) nodes->insertNodes.size()))
        return nullptr;

    return nodes->insertNodes[(size_t) insertIndex];
}

juce::Array<int> AudioEngine::getAutomatablePluginSlots (const juce::String& trackId) const
{
    juce::Array<int> slots;
    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr)
        return slots;

    if (nodes->instrumentNode != nullptr)
        slots.add (-1); // 音源

    for (int i = 0; i < (int) nodes->insertNodes.size(); ++i)
        if (nodes->insertNodes[(size_t) i] != nullptr)
            slots.add (i);

    return slots;
}

juce::String AudioEngine::getPluginSlotName (const juce::String& trackId, int insertIndex) const
{
    auto node = findPluginNode (trackId, insertIndex);

    if (node == nullptr || node->getProcessor() == nullptr)
        return {};

    const auto name = node->getProcessor()->getName();

    return insertIndex < 0 ? utf8 ("音源: ") + name
                            : "Insert " + juce::String (insertIndex + 1) + ": " + name;
}

juce::StringArray AudioEngine::getPluginParameterNames (const juce::String& trackId, int insertIndex) const
{
    juce::StringArray names;
    auto node = findPluginNode (trackId, insertIndex);

    if (node == nullptr || node->getProcessor() == nullptr)
        return names;

    for (auto* parameter : node->getProcessor()->getParameters())
        names.add (parameter->getName (40));

    return names;
}

void AudioEngine::beginAutomationTouch (const juce::String& trackId, const juce::String& targetId)
{
    if (auto* existing = findWriteState (trackId, targetId))
    {
        existing->touched = true;
        return;
    }

    AutomationWriteState newState;
    newState.trackId = trackId;
    newState.targetId = targetId;
    newState.touched = true;

    automationWriteStates.push_back (newState);
}

void AudioEngine::endAutomationTouch (const juce::String& trackId, const juce::String& targetId)
{
    auto* writeState = findWriteState (trackId, targetId);

    if (writeState == nullptr)
        return;

    writeState->touched = false;

    // Touchは離した時点で記録終了。Latchは停止するまで記録し続けるので、
    // ここでは何もしない（仕様書5.6）。
    if (getAutomationModeFor (trackId) == AutomationMode::Touch)
        writeState->recording = false;
}

AudioEngine::AutomationWriteState* AudioEngine::findWriteState (const juce::String& trackId,
                                                                 const juce::String& targetId)
{
    for (auto& writeState : automationWriteStates)
        if (writeState.trackId == trackId && writeState.targetId == targetId)
            return &writeState;

    return nullptr;
}

AutomationMode AudioEngine::getAutomationModeFor (const juce::String& trackId) const
{
    if (trackId.isEmpty())
        return project.getMasterAutomationMode();

    auto track = findTrackById (trackId);

    return track.state.getParent().isValid() ? track.getAutomationMode() : AutomationMode::Read;
}

float AudioEngine::readCurrentNormalisedValue (const juce::String& trackId, const juce::String& targetId) const
{
    if (AutomationTargets::isPluginTarget (targetId))
    {
        int insertIndex = 0;
        int parameterIndex = 0;

        if (! AutomationTargets::parsePluginTarget (targetId, insertIndex, parameterIndex))
            return 0.0f;

        auto node = findPluginNode (trackId, insertIndex);

        if (node == nullptr || node->getProcessor() == nullptr)
            return 0.0f;

        auto& parameters = node->getProcessor()->getParameters();

        if (! juce::isPositiveAndBelow (parameterIndex, parameters.size()))
            return 0.0f;

        return parameters[parameterIndex]->getValue();
    }

    if (trackId.isEmpty())
        return AutomationTargets::fromParameterValue (AutomationTargets::volume, project.getMasterVolumeDb());

    auto track = findTrackById (trackId);

    if (! track.state.getParent().isValid())
        return 0.0f;

    return targetId == AutomationTargets::pan
              ? AutomationTargets::fromParameterValue (AutomationTargets::pan, track.getPan())
              : AutomationTargets::fromParameterValue (AutomationTargets::volume, track.getVolumeDb());
}

void AudioEngine::timerCallback()
{
    if (! transport.isPlaying())
        return;

    const double position = transport.getPositionSeconds();

    applyPluginParameterAutomation (position);
    recordAutomation (position);

    if (automationWasRecorded)
    {
        automationWasRecorded = false;

        if (onAutomationRecorded != nullptr)
            onAutomationRecorded();
    }
}

void AudioEngine::applyPluginParameterAutomation (double positionSeconds)
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        for (int l = 0; l < track.getNumAutomationLanes(); ++l)
        {
            auto lane = track.getAutomationLane (l);
            const auto targetId = lane.getTargetId();

            // 8.59：バイパス中のレーンは効かせない（Phase 96）
            if (! AutomationTargets::isPluginTarget (targetId) || lane.isEmpty() || lane.isBypassed())
                continue;

            // 書き込み中のパラメータは、書いている値で上書きしない（つまみが取られてしまう）
            if (auto* writeState = findWriteState (track.getId(), targetId))
                if (writeState->recording)
                    continue;

            int insertIndex = 0;
            int parameterIndex = 0;

            if (! AutomationTargets::parsePluginTarget (targetId, insertIndex, parameterIndex))
                continue;

            auto node = findPluginNode (track.getId(), insertIndex);

            if (node == nullptr || node->getProcessor() == nullptr)
                continue;

            auto& parameters = node->getProcessor()->getParameters();

            if (! juce::isPositiveAndBelow (parameterIndex, parameters.size()))
                continue;

            auto* parameter = parameters[parameterIndex];
            const float value = lane.getValueAt (positionSeconds, parameter->getValue());

            // 同じ値を送り続けるとプラグインによっては無駄な再計算が走るため、変化時のみ
            if (std::abs (parameter->getValue() - value) > 1.0e-4f)
                parameter->setValueNotifyingHost (value);
        }
    }
}

void AudioEngine::recordAutomation (double positionSeconds)
{
    // 記録の粒度。細かすぎると点が増えすぎ、粗すぎると動きが階段状になる
    constexpr double minimumWriteInterval = 1.0 / 20.0;
    constexpr float minimumValueChange = 0.001f;

    for (auto& writeState : automationWriteStates)
    {
        const auto mode = getAutomationModeFor (writeState.trackId);

        // 記録を始める条件（仕様書5.6）：
        // Write＝再生中は常に、Touch/Latch＝つまみに触れたら
        const bool shouldStart = (mode == AutomationMode::Write)
                                  || ((mode == AutomationMode::Touch || mode == AutomationMode::Latch)
                                       && writeState.touched);

        if (shouldStart && ! writeState.recording)
        {
            writeState.recording = true;
            writeState.lastWriteTime = positionSeconds;
            writeState.lastWrittenValue = -1.0f;

            // 1回の書き込みパス全体をUndoの1ステップにまとめる（HANDOVER 3.1）
            project.beginAction (utf8 ("オートメーションの記録"));
        }

        if (mode == AutomationMode::Read)
            writeState.recording = false;

        // 記録中のトラックは、書いている値がそのまま聞こえるようにする（Phase 20）。
        // これが無いと、既存のオートメーションが優先されてフェーダーの音が出ない。
        // 音量・パンは同じチャンネルを共有しているので、どちらかが記録中なら迂回させる。
        if (! AutomationTargets::isPluginTarget (writeState.targetId))
        {
            if (writeState.trackId.isEmpty())
            {
                if (masterProcessor != nullptr)
                    masterProcessor->setAutomationBypassed (writeState.recording);
            }
            else if (auto* nodes = findTrackNodes (writeState.trackId))
            {
                if (nodes->channel != nullptr)
                    nodes->channel->setAutomationBypassed (writeState.recording);
            }
        }

        if (! writeState.recording)
            continue;

        const float value = readCurrentNormalisedValue (writeState.trackId, writeState.targetId);
        const bool valueChanged = std::abs (value - writeState.lastWrittenValue) > minimumValueChange;
        const bool intervalPassed = (positionSeconds - writeState.lastWriteTime) >= minimumWriteInterval;

        if (! intervalPassed && ! valueChanged)
            continue;

        auto lane = writeState.trackId.isEmpty()
                       ? project.getOrCreateMasterAutomationLane (writeState.targetId, &project.getUndoManager())
                       : findTrackById (writeState.trackId)
                            .getOrCreateAutomationLane (writeState.targetId, &project.getUndoManager());

        if (! lane.state.isValid())
            continue;

        lane.writeValue (writeState.lastWriteTime, positionSeconds, value, &project.getUndoManager());

        writeState.lastWriteTime = positionSeconds;
        writeState.lastWrittenValue = value;
        automationWasRecorded = true;
    }
}

void AudioEngine::setPlayheadSeconds (double seconds)
{
    const double sampleRate = transport.getSampleRate();

    if (sampleRate <= 0.0)
        return;

    transport.setPositionSamples ((juce::int64) (juce::jmax (0.0, seconds) * sampleRate));

    // 位置が飛ぶと、鳴っているノートのノートオフが来なくなる（そのノートの終端を
    // 飛び越してしまうため）。再生中のシークでは明示的に止める必要がある。
    //
    // **Phase 48で、MidiPlayerProcessor側も位置の飛びを自分で検知するようになった。**
    // こちらは残してある：あちらは最大1ブロック遅れるのに対し、ここは即座に依頼を立てられる。
    if (transport.isPlaying())
        for (auto& nodes : trackNodes)
            if (nodes.midiPlayer != nullptr)
                nodes.midiPlayer->allNotesOff();
}

void AudioEngine::setLoop (bool shouldLoop, double startSeconds, double endSeconds)
{
    const double sampleRate = transport.getSampleRate();

    if (sampleRate <= 0.0)
        return;

    transport.setLoop (shouldLoop,
                        (juce::int64) (juce::jmax (0.0, startSeconds) * sampleRate),
                        (juce::int64) (juce::jmax (0.0, endSeconds) * sampleRate));
}

void AudioEngine::prepareTrackPlayersForPlayback (juce::int64& endPositionSamples)
{
    // 仕様書5.6：オートメーションを平坦な配列として写し取る（Phase 19）。
    // オーディオスレッドはValueTreeを読めないため、再生を始める前にここで用意する。
    auto* device = deviceManager.getCurrentAudioDevice();
    const double sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;

    auto snapshotLane = [sampleRate] (const AutomationLane& lane)
    {
        std::vector<TrackChannelProcessor::AutomationSample> points;

        // 8.59：**バイパス中のレーンは写し取らない**（Phase 96）。
        // 空の配列を渡すと、そのパラメータはフェーダーの値のまま鳴る
        if (! lane.state.isValid() || lane.isBypassed())
            return points;

        points.reserve ((size_t) lane.getNumPoints());

        for (int i = 0; i < lane.getNumPoints(); ++i)
        {
            auto point = lane.getPoint (i);
            points.push_back ({ (juce::int64) (point.getTime() * sampleRate),
                                 point.getValue(),
                                 (int) point.getCurve(),
                                 point.getCurveAmount() });
        }

        return points;
    };

    for (auto& nodes : trackNodes)
    {
        if (nodes.channel != nullptr)
        {
            auto track = findTrackById (nodes.trackId);

            if (track.state.getParent().isValid())
                nodes.channel->setAutomation (snapshotLane (track.findAutomationLane (AutomationTargets::volume)),
                                               snapshotLane (track.findAutomationLane (AutomationTargets::pan)));
        }
    }

    // 仕様書5.6：マスターチャンネルのオートメーション（Phase 20）
    if (masterProcessor != nullptr)
        masterProcessor->setVolumeAutomation (
            snapshotLane (project.findMasterAutomationLane (AutomationTargets::volume)));

    for (auto& nodes : trackNodes)
    {
        if (nodes.clipPlayer != nullptr)
        {
            nodes.clipPlayer->prepareClipsForPlayback();
            endPositionSamples = juce::jmax (endPositionSamples, nodes.clipPlayer->getEndPositionSamples());
        }

        // 仕様書5.3：MIDIトラックはトラックごとにMIDIプレイヤーを持つ（Phase 14）
        if (nodes.midiPlayer != nullptr)
        {
            nodes.midiPlayer->prepareNotesForPlayback();
            endPositionSamples = juce::jmax (endPositionSamples, nodes.midiPlayer->getEndPositionSamples());
        }
    }
}

void AudioEngine::stop()
{
    transport.stop();
    stopTimer();

    // 仕様書5.6：Latchは「停止するまで記録し続ける」ので、ここで終わらせる（Phase 20）。
    // 迂回フラグも戻さないと、次の再生でオートメーションが効かなくなる。
    for (auto& writeState : automationWriteStates)
        writeState.recording = false;

    for (auto& nodes : trackNodes)
        if (nodes.channel != nullptr)
            nodes.channel->setAutomationBypassed (false);

    if (masterProcessor != nullptr)
        masterProcessor->setAutomationBypassed (false);

    if (onAutomationRecorded != nullptr)
        onAutomationRecorded();

    for (auto& nodes : trackNodes)
        if (nodes.midiPlayer != nullptr)
            nodes.midiPlayer->allNotesOff();
}

bool AudioEngine::isPlaying() const
{
    return transport.isPlaying();
}

double AudioEngine::getPlayheadSeconds() const
{
    return transport.getPositionSeconds();
}

//==============================================================================
// 仕様書5.3：トラックごとの音源割り当て（Phase 14）
//==============================================================================

juce::String AudioEngine::loadInstrumentForTrack (const juce::String& trackId,
                                                    const juce::PluginDescription& description)
{
    // 「トラックを追加した直後に音源を割り当てる」操作で空振りしないよう、
    // 先送りされている再構築を先に済ませる
    flushPendingTrackRebuild();

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr || ! nodes->isMidiTrack)
        return utf8 ("MIDIトラックが見つかりません。");

    // 先にロードを試す。ここで失敗しても、今鳴っている音源はそのまま残る
    // （差し替えに失敗しただけでトラックが無音になるのは不便なため）。
    // 8.144：受け皿が付いていれば出力バスも有効にして作る（Phase 182）
    auto newInstrumentNode = createPluginNode (description, trackNeedsDrumOutBuses (trackId));

    if (newInstrumentNode == nullptr)
        return isPluginBlockedByCrashes (description) ? getPluginBlockedMessage (description)
                                                      : utf8 ("プラグインを読み込めませんでした: ") + description.name;

    // モデルへ記録する。プラグインの抜き差しはUndo対象にしない
    // （モデルだけ巻き戻してもグラフ上のノードは外れず、状態が食い違うため。HANDOVER 3.1）。
    auto track = findTrackById (trackId);

    if (track.state.getParent().isValid())
        track.setInstrument (description, nullptr);

    disconnectTrackChain (*nodes);

    // 前の音源を片付ける。エディタウィンドウはプラグイン本体より先に閉じる（HANDOVER 1.5）
    nodes->instrumentEditorWindow = nullptr;

    if (nodes->instrumentNode != nullptr)
        graph.removeNode (nodes->instrumentNode->nodeID);

    nodes->instrumentNode = newInstrumentNode;

    connectTrackChain (*nodes);

    // 8.69：出口は`rebuildTrackOutputConnections()`に任せる（Phase 108/D6）。
    // ここで直にマスターへ繋いでいたため、**フォルダに入れたMIDIトラックへ音源を
    // 割り当てると、そのトラックだけフォルダを素通りしていました**（8.51）
    rebuildTrackOutputConnections();

    // 経路をいったん切ったので、このトラックに出入りしている送り（仕様書5.2.2）と
    // サイドチェイン（仕様書5.7.2）も張り直す
    rebuildSendConnections();
    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();

    return {};
}

void AudioEngine::removeInstrumentFromTrack (const juce::String& trackId)
{
    flushPendingTrackRebuild();

    auto* nodes = findTrackNodes (trackId);

    if (nodes == nullptr)
        return;

    auto track = findTrackById (trackId);

    if (track.state.getParent().isValid())
        track.removeInstrument (nullptr);

    disconnectTrackChain (*nodes);

    nodes->instrumentEditorWindow = nullptr;

    if (nodes->instrumentNode != nullptr)
    {
        graph.removeNode (nodes->instrumentNode->nodeID);
        nodes->instrumentNode = nullptr;
    }

    connectTrackChain (*nodes);

    // 8.69：出口は`rebuildTrackOutputConnections()`に任せる（Phase 108/D6。上と同じ理由）
    rebuildTrackOutputConnections();

    rebuildSendConnections();

    rebuildDrumOutConnections();   // 8.143（Phase 181／改善案⑮）
    rebuildSidechainConnections();
}

bool AudioEngine::trackHasInstrument (const juce::String& trackId) const
{
    auto* nodes = findTrackNodes (trackId);

    return nodes != nullptr && nodes->instrumentNode != nullptr;
}

juce::String AudioEngine::getTrackInstrumentName (const juce::String& trackId) const
{
    auto* nodes = findTrackNodes (trackId);

    if (nodes != nullptr && nodes->instrumentNode != nullptr && nodes->instrumentNode->getProcessor() != nullptr)
        return nodes->instrumentNode->getProcessor()->getName();

    return {};
}

void AudioEngine::openTrackInstrumentEditor (const juce::String& trackId)
{
    if (auto* nodes = findTrackNodes (trackId))
        openEditorWindowFor (nodes->instrumentNode, nodes->instrumentEditorWindow);
}

juce::File AudioEngine::getRecordingsDirectory()
{
    // 設計書2.3.8：Phase 57から**環境設定で置き場所を変えられる**（8.17）。
    // 既定は`ドキュメント\PersonalDAW Recordings`で、それまでと同じ。
    //
    // **録音済みのファイルは移動しない。** クリップは録った場所をパスで
    // 参照しているので（仕様書9章）、動かすと音が出なくなる。
    return StorageLocations::getFolder (StorageLocations::Kind::recordings);
}

juce::String AudioEngine::startRecording()
{
    if (recorderProcessor == nullptr)
        return utf8 ("オーディオエンジンが初期化されていません。");

    if (! inputAvailable)
        return utf8 ("録音できる入力がありません。Audio Settings...で入力デバイスを選んでください。");

    auto directory = getRecordingsDirectory();
    auto directoryResult = directory.createDirectory();

    if (directoryResult.failed())
        return utf8 ("録音用フォルダを作成できませんでした: ") + directoryResult.getErrorMessage();

    // ファイル名は日時ベース。同じ秒に2回始めても衝突しないよう、既存なら連番を足す。
    const auto timeStamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
    auto file = directory.getChildFile ("Recording_" + timeStamp + ".wav");

    if (file.existsAsFile())
        file = directory.getNonexistentChildFile ("Recording_" + timeStamp, ".wav");

    auto error = recorderProcessor->startRecording (file);

    if (error.isNotEmpty())
        return error;

    lastRecordingFile = file;

    // 録音クリップを置く位置＝再生を始める位置。Phase 18でシークに対応したので、
    // プレイヘッドを動かしてから録音すれば、その位置へクリップが置かれる（仕様書5.4）。
    recordStartSeconds = transport.getPositionSeconds();

    return {};
}

void AudioEngine::stopRecording()
{
    // カウントインの途中で止められた場合（Phase 39）。
    // 消しておかないと、録音を止めたのに数え終わった時点で再生が始まってしまう。
    if (metronomeProcessor != nullptr)
        metronomeProcessor->cancelCountIn();

    if (recorderProcessor != nullptr)
        recorderProcessor->stopRecording();

    // 8.146：MIDIの録り分を集める（Phase 184／改善案⑬a）。
    //
    // **アームの一覧ではなく、録っている再生器を数えます。** 録音中にアームを
    // 外されても最後まで集めたいのと、**アームしたトラックが録音中に消される**
    // ことがあるためです（1.32）。ノードのほうが「実際に録っていたもの」に近い
    recordedMidiTakes.clear();

    for (auto& nodes : trackNodes)
    {
        if (nodes.midiPlayer == nullptr || ! nodes.midiPlayer->isMidiRecording())
            continue;

        RecordedMidiTake take;
        take.trackId = nodes.trackId;
        take.events = nodes.midiPlayer->stopMidiRecording (take.overflowed);

        if (! take.events.empty())
            recordedMidiTakes.push_back (std::move (take));
    }

    midiRecordingActive = false;
}

bool AudioEngine::isRecording() const
{
    // 8.146：**MIDIだけを録っているときもtrue**（Phase 184）。
    // Recボタンの入切はこれで判断するので、片方だけを見ると止められなくなります
    return isAudioRecording() || isMidiRecording();
}

bool AudioEngine::isAudioRecording() const
{
    return recorderProcessor != nullptr && recorderProcessor->isRecording();
}

bool AudioEngine::isMidiRecording() const
{
    return midiRecordingActive;
}

double AudioEngine::getRecordedSeconds() const
{
    if (isAudioRecording())
        return recorderProcessor->getRecordedSeconds();

    // 8.146：MIDIだけのときは、WAVが無いので**トランスポートで測ります**（Phase 184）。
    // 録り始めた位置からの経過なので、途中でシークすれば当然そのぶん飛びます
    if (midiRecordingActive)
        return juce::jmax (0.0, transport.getPositionSeconds() - recordStartSeconds);

    return 0.0;
}

double AudioEngine::getSampleRate() const
{
    return transport.getSampleRate();
}

//==============================================================================
// 8.146：MIDI録音（Phase 184／改善案⑬a。仕様書5.4）
//==============================================================================

bool AudioEngine::hasArmedMidiTracks() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi && track.isArmed())
            return true;
    }

    return false;
}

bool AudioEngine::hasArmedAudioTracks() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Audio && track.isArmed())
            return true;
    }

    return false;
}

std::vector<RecordedMidiTake> AudioEngine::takeRecordedMidi()
{
    return std::move (recordedMidiTakes);
}

juce::int64 AudioEngine::getMidiRecordLatencySamples() const
{
    // 8.146：**弾いた音を、聞こえていた位置へ戻す**（Phase 184／改善案⑬a）。
    //
    // オーディオスレッドが位置`P`のブロックを作っているとき、
    // **耳に届いているのは`P - 出力レイテンシ`のあたり**です。
    // 弾く人はその聞こえている音に合わせるので、引かないと**必ず後ろへずれます**。
    //
    // **PDCは足しません。** あれは「音源の音が出るまでの遅れ」で、
    // 合わせる相手（クリックや既存のトラック）の聞こえ方はそれで動きません（8.85）。
    // MIDI機器そのものの遅れも足しません——**測れないものを当て推量で引くと、
    // 今度は前へずれます。**
    auto* device = deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
        return 0;

    return (juce::int64) juce::jmax (0, device->getOutputLatencyInSamples());
}

void AudioEngine::setInputMonitoringEnabled (bool shouldMonitor)
{
    if (recorderProcessor != nullptr)
        recorderProcessor->setInputMonitoringEnabled (shouldMonitor);
}

bool AudioEngine::isInputMonitoringEnabled() const
{
    return recorderProcessor != nullptr && recorderProcessor->isInputMonitoringEnabled();
}

float AudioEngine::getInputLevel (int channel) const
{
    return recorderProcessor != nullptr ? recorderProcessor->getInputLevel (channel) : 0.0f;
}

//==============================================================================
// 設計書3.8：プラグイン状態の保存・復元
//==============================================================================

void AudioEngine::captureStateFromNode (PluginInstance instance, juce::AudioProcessorGraph::Node::Ptr node)
{
    if (! instance.state.isValid() || node == nullptr)
        return;

    auto* processor = node->getProcessor();

    if (processor == nullptr)
        return;

    // プラグインの状態取得・設定は、オーディオスレッドのprocessBlock()と
    // 同時に走ると競合し得る。JUCEが用意しているコールバックロックで保護する
    // （HANDOVER 4.2と同じ考え方。待たされるのは最大1ブロックぶん）。
    juce::MemoryBlock data;
    {
        const juce::ScopedLock lock (processor->getCallbackLock());
        processor->getStateInformation (data);
    }

    // 状態の取得はUndoの対象にしない（保存操作の副作用でUndo履歴が伸びるのは不自然なため）
    instance.setPluginState (data, nullptr);
}

void AudioEngine::applyPluginStateToNode (juce::AudioProcessorGraph::Node::Ptr node, const juce::MemoryBlock& data)
{
    if (node == nullptr || data.getSize() == 0)
        return;

    if (auto* processor = node->getProcessor())
    {
        const juce::ScopedLock lock (processor->getCallbackLock());
        processor->setStateInformation (data.getData(), (int) data.getSize());
    }
}

void AudioEngine::capturePluginStatesIntoProject()
{
    // 仕様書5.3・5.7：トラックごとの音源とインサートを退避する
    for (auto& nodes : trackNodes)
    {
        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getId() != nodes.trackId)
                continue;

            if (nodes.instrumentNode != nullptr)
                captureStateFromNode (track.getInstrument(), nodes.instrumentNode);

            const int numToCapture = juce::jmin (track.getNumInserts(), (int) nodes.insertNodes.size());

            for (int i = 0; i < numToCapture; ++i)
                captureStateFromNode (track.getInsert (i), nodes.insertNodes[(size_t) i]);

            break;
        }
    }

    // 8.69：マスターのインサートも退避する（Phase 108／D6）。
    // **これを忘れると、保存して開き直したときにマスターだけ設定が初期値へ戻る**
    auto masterHost = project.getMasterBusInsertHost();
    const int numMasterInserts = juce::jmin (masterHost.getNumInserts(), (int) masterInsertNodes.size());

    for (int i = 0; i < numMasterInserts; ++i)
        captureStateFromNode (masterHost.getInsert (i), masterInsertNodes[(size_t) i]);
}

juce::StringArray AudioEngine::restorePluginsFromProject()
{
    // トラックに属する音源・インサートは、ProjectModelのルートが差し替わったことで
    // 走るrebuildTrackNodes()が読み込み直す（Phase 14）。
    //
    // 8.69：**マスターのインサートはここで読み込み直す**（Phase 108／D6）。
    // マスターはトラックではないので`rebuildTrackNodes()`の対象外で、
    // **プロジェクトを開いても何も起きないまま**になる。
    // Phase 26からこの関数を空のまま残してあったのは、この日のため
    juce::StringArray errors;

    rebuildMasterInsertNodes();

    auto masterHost = project.getMasterBusInsertHost();

    // 読み込めなかったものは、**モデルには残したまま**名前だけ知らせる
    // （消してしまうと、プラグインを入れ直しても戻らない）
    for (int i = 0; i < masterHost.getNumInserts(); ++i)
    {
        if (i < (int) masterInsertNodes.size() && masterInsertNodes[(size_t) i] != nullptr)
            continue;

        errors.add (utf8 ("マスターのインサートを読み込めませんでした: ")
                     + masterHost.getInsert (i).getDisplayName());
    }

    return errors;
}

void AudioEngine::previewNoteOn (const juce::String& trackId, int pitch, int velocity)
{
    if (auto* nodes = findTrackNodes (trackId))
        if (nodes->midiPlayer != nullptr)
            nodes->midiPlayer->triggerPreviewNoteOn (pitch, velocity);
}

void AudioEngine::previewNoteOff (const juce::String& trackId, int pitch)
{
    if (auto* nodes = findTrackNodes (trackId))
        if (nodes->midiPlayer != nullptr)
            nodes->midiPlayer->triggerPreviewNoteOff (pitch);
}
