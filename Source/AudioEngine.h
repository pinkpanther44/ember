#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <map> // 仕様書5.7.1：ノードごとのレイテンシを覚えておくのに使う

#include "PluginManager.h"
#include "ClipPlayerProcessor.h"
#include "MidiPlayerProcessor.h"
#include "RecorderProcessor.h"
#include "MasterChannelProcessor.h"
#include "ExportOptions.h"        // 8.80：書き出しの設定（Phase 120／D9・D10・D11）
#include "MetronomeProcessor.h"
#include "TrackChannelProcessor.h"
#include "SendGainProcessor.h"
#include "Transport.h"
#include "ProjectModel.h"
#include "PluginCrashTracker.h"
#include "PluginSandboxHost.h"
#include "SandboxedPluginProcessor.h" // 仕様書5.8.1：サンドボックス本格運用（Phase 26時点では未接続）

//==============================================================================
/**
    設計書1.5「オーディオエンジンアーキテクチャ」の土台。

    Phase 2でテストトーン→出力の最小構成を作り、Phase 3でプラグイン挿入に対応した。

    **Phase 12cで、設計書1.5の「各Trackをノードとしてラップする」構成へ移行した。**
    それ以前は1つのClipPlayerProcessorが全トラックを内部でミックスしていたため、
    トラックごとにインサートプラグインを挟むことができなかった。

    **Phase 14で、MIDIトラックも同じ構成に乗せた。** それ以前は音源がプロジェクト全体で
    1台だけで、全MIDIトラックのノートがそこへまとめて送られており、MIDIトラックは
    ミキサーに出せなかった（フェーダーもインサートもセンドも使えなかった）。
    現在の構成：

        [Audioトラック] ClipPlayerProcessor ──────┐
        [Midiトラック] MidiPlayer →(MIDI)→ 音源 ──┼→ …インサート… → TrackChannel ─┐
        [Sendトラック] （他トラックからの送り）─────┘                                │
        （Phase 26でテストトーン用の試聴チェーンは廃止）
        入力 → RecorderProcessor ───────────────────────────────────────────────────┘

    再生位置は各プロセッサが持たず、共有のTransportから読む（Transport.h参照）。

    サンドボックス（設計書3.4・3.5）はまだ実装しておらず、常に同一プロセス内で
    ロードする（設計書3.3のうち「ネイティブロード」側のみ）。
*/
class AudioEngine : private juce::ChangeListener,
                     private juce::ValueTree::Listener,
                     private juce::AsyncUpdater,
                     private juce::MidiInputCallback,   // 8.83：MIDIキーボード入力（Phase 123/⑬）
                     private juce::Timer
{
public:
    explicit AudioEngine (ProjectModel& projectToUse);
    ~AudioEngine();

    /** オーディオデバイスを初期化し、グラフを組み立てて再生を開始する。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String initialise();

    PluginManager& getPluginManager() { return pluginManager; }
    PluginCrashTracker& getCrashTracker() { return crashTracker; }
    PluginSandboxHost& getSandboxHost() { return sandboxHost; }

    /** 仕様書5.7：ミキサーの設定（音量・パン・ミュート／ソロ）をエンジンへ反映する。
        再生中に呼んでも即座に効く。 */
    void updateMixerSettings();

    //==========================================================================
    // メトロノーム（Phase 38）

    /** クリックを鳴らすかを切り替える。再生中に切り替えても即座に効く。 */
    void setMetronomeEnabled (bool shouldBeEnabled);
    bool isMetronomeEnabled() const;

    /** Phase 141：クリックの位置を決める表を渡す（`TempoMap.h`）。
        **曲の途中でテンポや拍子が変わる**ので、値2つでは足りない（8.103）。 */
    void updateMetronomeTiming (const TempoMap& tempoMap);

    /** クリックの音量（0.0〜1.0）。 */
    void setMetronomeGain (float gain);
    float getMetronomeGain() const;

    //==========================================================================
    // 仕様書5.4：録音のカウントイン（Phase 39）

    /** カウントインを鳴らしてから録音を始める。

        `countInBars`が0なら、これまでどおり即座に始まる。
        1以上なら、その小節ぶんクリックを鳴らし、**鳴らし終えた時点で**
        再生と録音が始まる（開始の判定はオーディオスレッドで行う。MetronomeProcessor参照）。

        録音そのものは`RecorderProcessor`が**トランスポートが動いている間だけ**書くので、
        カウントインの時間はファイルに入らない。 */
    juce::String startRecordingWithCountIn (int countInBars);

    /** カウントイン中か（UIで「まだ録れていない」ことを示すのに使う）。 */
    bool isCountingIn() const;

    /** 8.147：**再生に使っているファイルを読み直す**（Phase 185／改善案㉞）。

        トランスポーズ済みのファイルが**裏で出来上がったとき**に呼びます
        （`ClipAudioRenderer`）。**止まっているときは何もしません**——
        次のPlayが読み直すので、そこで効きます。 */
    void refreshPlaybackSources() { triggerPlaybackRefresh(); }

    /** 仕様書5.7：トラックのレベル（0.0〜1.0）。UIのメーター表示用。
        並び順ではなくtrackIdで引く（UIとエンジンでトラックの絞り込み条件が
        食い違ったときに、別トラックのメーターを表示してしまわないため）。 */
    float getTrackLevel (const juce::String& trackId, int channel) const;

    /** マスター出力のレベル（0.0〜1.0）。 */
    float getMasterLevel (int channel) const;

    /** 設計書1.5・仕様書5.5：プロジェクトのクリップ再生（Phase 4d新設） */
    void play();
    void stop();
    bool isPlaying() const;
    double getPlayheadSeconds() const;

    /** 仕様書5.9：再生位置を動かす（シーク）。停止中でも再生中でも呼べる。
        Playを押すと、ここで決めた位置から再生が始まる。 */
    void setPlayheadSeconds (double seconds);

    /** 仕様書5.9：ループ範囲を設定する（Phase 48）。

        **モデル（ProjectModel）が正で、ここはその写し**です。
        変わったら呼び直してください（`MainComponent::applyLoopSettings()`が面倒を見ます）。
        再生中に呼んでも構いません（次のブロックから効きます）。 */
    void setLoop (bool shouldLoop, double startSeconds, double endSeconds);

    //==========================================================================
    // 仕様書5.6：オートメーションの書き込みとプラグインパラメータ（Phase 20）

    /** 指定トラックのプラグインが公開しているパラメータの一覧（名前）。
        インスペクタ等で「どのパラメータをオートメーションするか」を選ぶのに使う。
        insertIndexが負なら音源スロット。 */
    juce::StringArray getPluginParameterNames (const juce::String& trackId, int insertIndex) const;

    /** そのトラックが持っているプラグインスロットの説明（"Instrument: xxx" / "Insert 1: yyy"）。
        insertIndexは-1が音源、0以上がインサート。 */
    juce::Array<int> getAutomatablePluginSlots (const juce::String& trackId) const;
    juce::String getPluginSlotName (const juce::String& trackId, int insertIndex) const;

    /** 仕様書5.6：つまみに触れた／離したことを知らせる（Touch/Latchの判定に使う）。
        trackIdが空文字ならマスターチャンネルを指す。 */
    void beginAutomationTouch (const juce::String& trackId, const juce::String& targetId);
    void endAutomationTouch (const juce::String& trackId, const juce::String& targetId);

    /** 書き込み中に記録された内容が確定したときに呼ばれる（UIの再描画用）。 */
    std::function<void()> onAutomationRecorded;

    //==========================================================================
    // 仕様書5.3：トラックごとの音源割り当て（Phase 14）

    /** 指定したMIDIトラックへインストゥルメント（音源）プラグインを割り当てる。
        既に音源があれば差し替える。失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String loadInstrumentForTrack (const juce::String& trackId, const juce::PluginDescription& description);

    /** 指定したMIDIトラックから音源を外す（トラックは無音になる）。 */
    void removeInstrumentFromTrack (const juce::String& trackId);

    bool trackHasInstrument (const juce::String& trackId) const;
    juce::String getTrackInstrumentName (const juce::String& trackId) const;

    /** 指定したMIDIトラックの音源のネイティブエディタウィンドウを開く（設計書3.7）。 */
    void openTrackInstrumentEditor (const juce::String& trackId);

    /** ピアノロールでの試聴用に、指定トラックの音源で1音を鳴らす／止める。 */
    void previewNoteOn (const juce::String& trackId, int pitch, int velocity);
    void previewNoteOff (const juce::String& trackId, int pitch);

    //=========================================================================
    // 8.83：**MIDIキーボード等の入力**（Phase 123/改善案⑬。仕様書5.4）
    //
    // 有効にするデバイスは**環境設定のAudioカテゴリ**で選びます
    // （`AudioDeviceSelectorComponent`のMIDI入力欄。有効/無効は`AudioDeviceManager`が覚える）。
    //
    // 届いたメッセージは**音を出す先のトラックの`MidiPlayerProcessor`**へそのまま渡します。
    // ノートだけでなくCC・ピッチベンドも通るので、
    // モジュレーションホイールやサステインペダルも効きます。

    /** 鍵盤で鳴らす先のトラック（選択中のMIDIトラック）。

        **録音待機（アーム）中のMIDIトラックがあれば、そちらが優先されます。**
        録りたいトラックをアームしてから別のトラックを選んでも、
        **鳴るのはアームしたほう**——そうしないと「録る先と鳴る先が違う」ことになります。 */
    void setMidiInputTargetTrackId (const juce::String& trackId);

    /** 仕様書5.4：オーディオ入力が使える状態か（入力デバイスを開けたか）。 */
    bool isAudioInputAvailable() const { return inputAvailable; }

    /** 入力デバイスを開けなかった場合の理由。開けている場合は空文字。 */
    juce::String getAudioInputError() const { return inputError; }

    /** 仕様書5.4：入力モニタリング（入力音をそのまま出力へ返す）のON/OFF。 */
    void setInputMonitoringEnabled (bool shouldMonitor);
    bool isInputMonitoringEnabled() const;

    /** 入力レベル（0.0〜1.0）。UIのレベルメーター表示用。 */
    float getInputLevel (int channel) const;

    /** 現在のオーディオデバイスの説明（ドライバ種別／デバイス名／入力ch数）。 */
    juce::String getInputDeviceDescription() const;

    /** 仕様書5.4：録音を開始する。録音ファイルは「録音用フォルダ」へ自動命名で作られる。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String startRecording();

    /** 録音を停止する。**オーディオもMIDIも止めます。**

        8.146：MIDIで録れたものは`takeRecordedMidi()`で取り出してください
        （ここで捨てずに持っておくのは、止めるのと片付けるのを分けるためです）。 */
    void stopRecording();

    /** **何かを録音中か**（オーディオ・MIDIのどちらでも）。

        8.146：MIDIだけを録っているときもtrueです（Phase 184）。
        Rec ボタンの入切はこれで判断するので、**片方だけを見ないこと**——
        MIDIだけ録っている最中にfalseを返すと、Recを押しても止まりません。 */
    bool isRecording() const;

    /** オーディオ（WAV）を録音中か。**録音の後始末で使います**——
        録っていないのにクリップを置くと、前回のファイルが二重に置かれます。 */
    bool isAudioRecording() const;

    /** MIDIを録音中か。 */
    bool isMidiRecording() const;

    /** 録音の長さ（秒）。UIの録音時間表示用。 */
    double getRecordedSeconds() const;

    /** いま動いているサンプルレート。

        8.146：録ったMIDIのサンプル位置を秒へ直すのに要ります（Phase 184）。
        **デバイスを開くまでは既定値**なので、録音していない時点の値は当てにしないこと。 */
    double getSampleRate() const;

    //==========================================================================
    // 8.146：**MIDI録音**（Phase 184／改善案⑬a。仕様書5.4）

    /** 録音待機（アーム）中のMIDIトラックがあるか。 */
    bool hasArmedMidiTracks() const;

    /** 録音待機（アーム）中のオーディオトラックがあるか。 */
    bool hasArmedAudioTracks() const;

    /** `stopRecording()`が集めたものを取り出す（**取り出すと空になります**）。

        **止めたあとに呼ぶこと。** 録音中に呼んでも空が返ります。 */
    std::vector<RecordedMidiTake> takeRecordedMidi();

    /** 録ったMIDIから引く遅れ（サンプル）。**判断はここ1箇所**（1.27）。
        中身の理由は`MidiPlayerProcessor::startMidiRecording()`を参照。 */
    juce::int64 getMidiRecordLatencySamples() const;

    /** 直近に録音したファイル（まだ録音していなければ存在しないFile）。 */
    juce::File getLastRecordingFile() const { return lastRecordingFile; }

    /** 録音を開始した時点のタイムライン位置（秒）。録音クリップの配置位置に使う。 */
    double getRecordStartSeconds() const { return recordStartSeconds; }

    /** 録音ファイルの保存先フォルダ（仕様書9章「外部パス参照のみ」の方針に沿う）。 */
    static juce::File getRecordingsDirectory();

    //==========================================================================
    // 設計書3.8：プラグイン状態の保存・復元

    /** 現在ロード中のプラグインの内部状態をProjectModelへ書き出す。
        プロジェクトを保存する直前に呼ぶこと（つまみを動かした結果を反映するため）。 */
    void capturePluginStatesIntoProject();

    /** プロジェクト読み込み後に、トラックに属さないプラグインを復元する。
        復元できなかったものがあれば、その説明を返す（全て成功すれば空）。

        **Phase 26時点では復元するものが無い**（テストトーン用の試聴チェーンを
        廃止したため）。マスターへのインサート（設計書1.4の`MASTERBUS/INSERTS`）を
        実装したときの受け口として残してある。 */
    juce::StringArray restorePluginsFromProject();

    /** 仕様書6.2：オーディオデバイス選択のUIを作って返す（環境設定へ埋め込む用）。
        USBマイク等、OSの既定以外の入力を使いたい場合はここから選ぶ。 */
    std::unique_ptr<juce::Component> createAudioSettingsComponent();

    /** オーディオデバイスの構成が変わったときに呼ばれる（UIの入力表示更新用）。
        メッセージスレッドから呼ばれる。 */
    std::function<void()> onAudioDeviceChanged;

    /** 仕様書5.1：プロジェクトが差し替わった後に、トラックのノード構成を作り直す。 */
    void rebuildTrackNodes();

    /** 8.111：1トラックぶんのノードを作ってグラフへ足す（Phase 147）。

        **音を通さない種別（Chord/VCA）なら何もしません。**
        出口・センド・サイドチェインはここでは張りません——**行き先のノードが
        まだ無いことがある**ので、全部揃ってからまとめて張ります（8.51）。 */
    void createTrackNodes (const Track& track);

    /** トラックの増減による再構築を、待たずに今すぐ済ませる。

        トラックを足した直後の再構築は`AsyncUpdater`で先送りされる（変更通知が連続して
        飛んでくるのを1回にまとめるため）。そのため「トラックを追加した直後に
        そのトラックのノードを触る」処理は、何もしないと空振りする。
        UI側がエンジンの状態を読む前や、追加直後のトラックへ音源を割り当てる前に呼ぶこと。 */
    void flushPendingTrackRebuild();

    //==========================================================================
    // 仕様書5.7・設計書1.4：インサートスロット

    /** 指定トラックの末尾にインサートを追加する。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。

        8.69：`trackId`が空文字なら**マスター**（Phase 108／D6）。 */
    juce::String addInsertToTrack (const juce::String& trackId, const juce::PluginDescription& description);

    /** 指定トラックのインサートを取り除く。 */
    void removeInsertFromTrack (const juce::String& trackId, int insertIndex);

    /** 8.66：**インサートの並び順を変える**（Phase 104／改善案㉘㉙）。

        **モデルとグラフとエディタウィンドウの3つを、同じ順番で動かします。**
        `Track::moveInsert()`だけを呼ぶと、モデルの並びとグラフの並びが食い違い、
        **画面に出ている名前と実際に掛かるプラグインがずれます**（番号で対応させているため）。

        `toIndex`は移動後に置きたい位置（0が先頭）。 */
    void moveInsertInTrack (const juce::String& trackId, int fromIndex, int toIndex);

    /** 8.66：**インサートを別のトラックへコピーする**（Phase 104／改善案㉙＋D7）。

        **設定ごと写します**（`getStateInformation()`の中身とバイパス状態）。
        「同じEQをもう1本のトラックにも」がドラッグだけで済むようにするのが目的なので、
        **空のプラグインを挿すだけでは意味がありません**。

        サイドチェインの設定は**写しません**：送り元のトラックはそのトラック用に
        選んだものなので、写すと「意図しないトラックの音で潰れる」が黙って起きます。

        `destIndex`は挿し込む位置（0が先頭。範囲外なら末尾）。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String copyInsertToTrack (const juce::String& sourceTrackId, int sourceIndex,
                                     const juce::String& destTrackId, int destIndex);

    /** 8.63：**バイパスを切り替えたあとに配線を張り直す**（Phase 101／改善案㉘）。

        バイパスはモデル（`PluginInstance::isBypassed()`）にあり、
        **音の側は「そのノードを繋がない」ことで実現している**ので、
        モデルを変えただけでは何も起きません。ここを呼んで張り直します。

        **どのトラックのどのスロットかは渡しません**：出口・送り・サイドチェインまで
        まとめて張り直す必要があり（8.52）、絞り込んでも安くならないためです。 */
    void rebuildGraphForBypassChange();

    /** 指定トラックのインサートのエディタウィンドウを開く。 */
    void openInsertEditor (const juce::String& trackId, int insertIndex);

    //==========================================================================
    // 仕様書5.7.2・設計書3.9：サイドチェイン

    /** そのインサートがサイドチェイン入力（第2入力バス）を持っているか。

        持っていないプラグインではUIに設定項目自体を出さない（設計書3.9）。
        判定にはロード済みのプラグイン本体が要るので、エンジン側でしか答えられない。 */
    bool insertSupportsSidechain (const juce::String& trackId, int insertIndex) const;

    /** 仕様書5.7.2：インサートのサイドチェイン入力に使うトラックを設定する。
        sourceTrackIdが空文字なら解除。設定後、グラフの配線を張り直す。

        **配線できたかどうかを確かめて返す。** `addConnection()`は失敗しても何も言わずに
        falseを返すだけなので（HANDOVER 2章）、「設定したのに効かない」が黙って起きる。
        失敗した場合はその理由を返す（成功時は空文字）。 */


    /** 8.112：前に落ちたプラグインか（Phase 148）。**読み込む前に必ず訊くこと。** */
    bool isPluginBlockedByCrashes (const juce::PluginDescription& description) const;

    /** 8.112：読み込まなかった理由の文（再挑戦の手順つき）。 */
    juce::String getPluginBlockedMessage (const juce::PluginDescription& description) const;

    /** 8.111：サイドチェイン入力バスを（対応していれば）有効にする（Phase 147）。

        **呼ぶのはソースを実際に割り当てるときだけ**にすること。
        バス構成を変える操作はVST3では`setBusArrangements()`になり、
        **プラグインによっては落ちます**——作った直後に一律で叩いていたのを
        やめたのがPhase 147です（8.111）。

        有効にできたか（既に有効だった場合も含む）を返します。 */
    bool ensureSidechainBusEnabled (juce::AudioProcessorGraph::Node::Ptr node);
    juce::String setInsertSidechainSource (const juce::String& trackId, int insertIndex,
                                            const juce::String& sourceTrackId);

    /** 全トラックの配線（経路・センド・サイドチェイン）をモデルの内容に合わせて張り直す。

        **プラグインの読み込み直しは行わない**ので、`rebuildTrackNodes()`と違って
        音作りもGUIウィンドウも失われない（HANDOVER 1.8）。
        Undo/Redoのようにモデルだけが外から変わった場合に、グラフを追従させるのに使う。 */
    void rewireAllTrackConnections();

    //==========================================================================
    // 仕様書5.7.1・設計書3.9：PDC（プラグイン遅延補正）
    //
    // **トラック間の時間軸を揃える処理そのものは、`juce::AudioProcessorGraph`が
    // 内部で行っている。** 各ノードの入力側の最大レイテンシを求め、遅れの少ない経路へ
    // 自動的に遅延を挿入する仕組みが最初から入っている（JUCEの
    // `juce_AudioProcessorGraph.cpp`：`getInputLatencyForNode()` / `addDelayChannelOp()`）。
    //
    // そのため設計書1.5の「レイテンシが小さい経路に自動でDelayLineノードを挿入する」を
    // **自前で実装してはいけない**（二重に遅れる）。ここでやるのは残りの2つだけ：
    //   1. プラグインが後からレイテンシを変えたときにグラフへ再計算させる
    //   2. 現在の補正量を画面に出す（仕様書5.7.1）

    /** グラフ全体の補正量（サンプル）。仕様書5.7.1の「現在の補正量」。 */
    int getTotalLatencySamples() const;

    /** そのトラックの経路上のプラグイン（音源＋インサート）が報告している
        レイテンシの合計（サンプル）。0なら、そのトラックに遅延を持つプラグインは無い。

        **「補正量」ではなく「原因の量」**である点に注意。どのトラックが
        全体の補正量を押し上げているかを見るための値。 */
    int getTrackLatencySamples (const juce::String& trackId) const;

    /** 現在のオーディオデバイスのサンプルレート（ミリ秒換算に使う）。
        デバイスが開けていない場合は0を返す。 */
    double getCurrentSampleRate() const;

    /** 8.85：**いまのレイテンシ**を人が読む形で返す（Phase 125）。

        バッファ・出力・入力・PDC・「鍵盤を弾いてから鳴るまで」の目安。
        環境設定のAudioカテゴリに出しています——
        **バッファサイズを変えた結果が数字で見えない**と、
        どこまで下げればよいか判断できないためです。 */
    juce::String getLatencyDescription() const;

    /** 8.85：いまのデバイス設定をアプリの設定へ書く（Phase 125）。

        **デバイスを触るたびに書きます。** 終了時にまとめて書くと、
        落ちたときに設定が消えます。触る頻度は高くないので、そのたびで構いません。 */
    void saveAudioDeviceState();

    /** サイドチェイン入力の配線を、モデルの内容に合わせて張り直す。

        `disconnectTrackChain()`はノードの接続を**入ってくるぶんも含めて**全部外すため、
        インサートや音源を抜き差ししたトラックでは、他トラックから入っていた
        サイドチェインも一緒に切れてしまう。センドと同じく、経路を触ったら必ず呼ぶこと。 */
    void rebuildSidechainConnections();

    //==========================================================================
    // 仕様書5.2.2：センド

    /** 送り元トラックから、センドトラックへの送りを1本追加する。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String addSendToTrack (const juce::String& sourceTrackId, const juce::String& targetTrackId);

    void removeSendFromTrack (const juce::String& sourceTrackId, int sendIndex);

    /** 8.66：**センドの並び順を変える**（Phase 105／改善案㉘㉙）。

        インサートと違い、**グラフ側で持ち替えるものがありません**：
        送りの配線は`rebuildSendConnections()`がモデルから作り直すので、
        モデルを動かして張り直すだけで済みます。 */
    void moveSendInTrack (const juce::String& trackId, int fromIndex, int toIndex);

    /** 8.66：**センドを別のトラックへコピーする**（Phase 105／改善案㉙＋D7）。

        送り先と送り量を写します。**輪になる組み合わせは断ります**
        （センドトラック自身へ、そのセンドトラック行きの送りを付けると信号が回る）。

        `destIndex`は挿し込む位置（0が先頭。範囲外なら末尾）。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    juce::String copySendToTrack (const juce::String& sourceTrackId, int sourceIndex,
                                   const juce::String& destTrackId, int destIndex);

    /** 送りの配線を、モデルの内容に合わせて張り直す。
        送り量だけの変更ならupdateMixerSettings()で足りる（配線は変わらないため）。 */
    void rebuildSendConnections();

    //==========================================================================
    // 8.143：**マルチアウト音源のパラアウト**（Phase 181／改善案⑮）

    /** 音源の出力バスを、受け皿トラック（`TrackType::DrumOut`）の入口へ張る。

        **全部のノードが揃ってから呼ぶこと**（センドの配線と同じ理由。8.51）。 */
    void rebuildDrumOutConnections();

    /** その音源が持っている出力バスの数（メイン出力の0番を含む）。
        音源が無ければ0。**バスを数えるのはエンジンの仕事**——
        プラグインの実体を持っているのがここだけだからです。 */
    int getInstrumentOutputBusCount (const juce::String& trackId) const;

    /** 出力バスの名前（"Kick"・"Out 3/4" など、プラグインが返すもの）。 */
    juce::String getInstrumentOutputBusName (const juce::String& trackId, int busIndex) const;

    /** そのトラックの音源に、パラアウトの受け皿が付いているか（8.144／Phase 182）。

        **付いているときだけ、音源を作るときに出力バスを有効にします**——
        バス構成を触るのは繊細なので、要らないトラックでは一度も触りません（8.111）。 */
    bool trackNeedsDrumOutBuses (const juce::String& trackId) const;

    /** 8.51：各トラックの出口を張る（Phase 90／D2）。
        **フォルダに入っていればそのフォルダへ、入っていなければマスターへ。**
        **全ノードが揃ってから呼ぶこと**（行き先がまだ無いことがある）。 */
    void rebuildTrackOutputConnections();

    //==========================================================================
    // 仕様書5.10：書き出し

    /** マスター出力をWAVファイルへ書き出す（ミックスダウン）。

        実時間で再生しながら録るのではなく、**オフライン（実時間より速く）**処理する。
        処理中はオーディオデバイスのコールバックを外すため、スピーカーからは何も鳴らない。

        progressCallbackは進捗（0.0〜1.0）を受け取り、falseを返すと中断する。
        失敗・中断した場合はエラーメッセージを返す（成功時は空文字）。

        **メッセージスレッド以外（バックグラウンドスレッド）から呼ぶこと。**
        処理中はUIからグラフを触らせないよう、呼び出し側でモーダル表示にする必要がある。 */
    juce::String renderMixdownToFile (const juce::File& file, const ExportOptions& options,
                                       std::function<bool (double)> progressCallback);

    /** 仕様書5.10：トラックごとに1ファイルずつ書き出す（ステム書き出し）。

        対象トラック以外を黙らせた状態で、ミックスダウンと同じ経路を通して書き出す。
        センドトラックは鳴らしたままにするため、そのトラックが送っているリバーブ等の
        残響もステムに含まれる（全ステムを足すとミックス全体とほぼ一致する）。

        ミックスダウンと同じく、**バックグラウンドスレッドから呼ぶこと。** */
    juce::String renderStemsToFolder (const juce::File& folder, const ExportOptions& options,
                                       std::function<bool (double)> progressCallback);

    /** 書き出し時、最後に付け足す余韻の長さ（秒）。
        リバーブやディレイの残響が切れないようにするため。 */
    static constexpr double exportTailSeconds = 2.0;

private:
    /** 現在のデバイスの入力チャンネル数に合わせて、入力→Recorderの配線を張り直す。 */
    void updateInputConnections();

    //==========================================================================
    // 仕様書5.6：オートメーション（Phase 20）
    //
    // **プラグインパラメータの適用と、書き込みモードの記録は、
    // オーディオスレッドではなくメッセージスレッドのタイマーで行う。**
    //
    // 音量・パンのようにプロセッサへ写し取っておける値と違い、
    // プラグインパラメータは`AudioProcessorParameter`へ直接設定する必要があり、
    // 記録の側もValueTree（＝メッセージスレッド専用）を書き換える。
    // どちらもタイマー（automationUpdateHz）の粒度で十分実用になる。

    static constexpr int automationUpdateHz = 60;

    /** 書き込み中の1レーンぶんの状態。 */
    struct AutomationWriteState
    {
        juce::String trackId; // 空文字ならマスター
        juce::String targetId;
        bool touched = false;       // つまみに触れているか（Touch/Latch用）
        bool recording = false;     // 実際に記録中か
        double lastWriteTime = 0.0; // 直前に点を置いた位置（この間の既存点を消す）
        float lastWrittenValue = -1.0f;
    };

    void timerCallback() override;

    /** タイマー1回ぶんの処理：プラグインパラメータの適用と、書き込みモードの記録。 */
    void applyPluginParameterAutomation (double positionSeconds);
    void recordAutomation (double positionSeconds);

    /** 対象のいまの値（正規化済み）を読む。記録する値を決めるのに使う。 */
    float readCurrentNormalisedValue (const juce::String& trackId, const juce::String& targetId) const;

    /** trackIdとinsertIndexから、対象のプラグインノードを引く（無ければnullptr）。 */
    juce::AudioProcessorGraph::Node::Ptr findPluginNode (const juce::String& trackId, int insertIndex) const;

    AutomationWriteState* findWriteState (const juce::String& trackId, const juce::String& targetId);

    /** そのトラック（空文字ならマスター）の書き込みモード。 */
    AutomationMode getAutomationModeFor (const juce::String& trackId) const;

    std::vector<AutomationWriteState> automationWriteStates;

    //==========================================================================
    // 仕様書5.7.1：レイテンシの見張り（Phase 12e）
    //
    // **プラグインは、読み込んだ後でもレイテンシを変えてくる**（オーバーサンプリング倍率の
    // 変更、リニアフェイズ／最小位相の切り替えなど）。グラフが補正を計算し直すのは
    // トポロジが変わったときか`rebuild()`を呼んだときだけで、**ポーリングはしていない**。
    // そのため、こちらから定期的に見張って、変わっていたら`rebuild()`を促す必要がある。
    //
    // 設計書3.9は`AudioProcessorListener::audioProcessorChanged`を受ける方式を挙げているが、
    // ここでは**ポーリング**にしている。`rebuildTrackNodes()`が毎回すべてのノードを
    // 作り直すため（1.8）、リスナーの付け外しを漏れなく行うほうがかえって危ういため
    // （外し忘れると破棄済みノードを掴む）。読むのはintのゲッターだけなので負荷は無い。

    static constexpr int latencyPollIntervalMs = 250;

    /** ノードID → そのプラグインが報告しているレイテンシ。前回の値と見比べるために持つ。 */
    std::map<juce::uint32, int> lastKnownPluginLatency;

    /** 現在のレイテンシ構成を集める（見張りと表示の両方が使う）。 */
    std::map<juce::uint32, int> collectPluginLatencies() const;

    void checkForLatencyChanges();

    // AudioEngineはjuce::Timerを1つしか継承できない（そちらはオートメーション用で、
    // 再生中しか回らない）。レイテンシの見張りは停止中も必要なので別立てにする。
    juce::TimedCallback latencyPollTimer { [this] { checkForLatencyChanges(); } };

    /** 書き込みが1回でも起きたか。UIへ知らせるかどうかの判断に使う。 */
    bool automationWasRecorded = false;

    // トラックの増減を検知してノード構成を作り直す（設計書1.5）。
    // ValueTreeの変更通知は連続して飛んでくるため、AsyncUpdaterで1回にまとめる。
    /** 8.41：ミキサーに関わる値の変更をエンジンへ反映する（Phase 81）。

        **入口ごとに`updateMixerSettings()`を呼ぶのをやめ、モデルを見て追う**（1.15）。
        アレンジ画面のヘッダーのソロ／ミュートも、Undo/Redoも、これで効くようになる。 */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int oldIndex, int newIndex) override;
    void handleAsyncUpdate() override;

    /** 8.110：**次の非同期更新で「作り直し」まで行うか**（Phase 146）。

        `false`なら**配線の張り直しだけ**で済ませます。トラックの並び替えと
        フォルダの出し入れは**配線しか変わらない**（行き先はtrackIdで引いている）ので、
        プラグインを読み込み直す理由がありません。

        **9秒かかっていたのはここです**（HANDOVER 1.8・8.110）。
        両方が溜まったら作り直しが勝ちます（そちらが上位互換なので）。 */
    bool pendingFullRebuild = false;

    /** 8.110：**配線だけ張り直す**予約（並び替え・フォルダの出し入れ）。 */
    bool pendingRewire = false;

    /** 8.116：**再生データを写し直す**予約（Phase 151）。

        ノート・CC・オートメーション・クリップは、オーディオスレッドが読めるよう
        **再生の前に写し取った表**で鳴っています（1.12）。編集したら写し直さないと、
        **止めて鳴らし直すまで反映されません**でした。 */
    bool pendingPlaybackRefresh = false;

    /** ノードを作り直す（プラグインの読み込み直しを伴う。**重い**）。 */
    void triggerFullRebuild();

    /** 配線だけを張り直す（**プラグインには触らない**）。 */
    void triggerRewireOnly();

    /** 8.116：再生中の編集を反映するため、再生データを写し直す（Phase 151）。
        **止まっているときは何もしません**（次のPlayが写し取るので）。 */
    void triggerPlaybackRefresh();

    //==========================================================================
    // 8.162：**再生中のテンポ変更を、止めずに反映する**（Phase 200／本人の要望）。

    /** テンポ・拍子が変わったとき。**拍の位置を保ったまま**写し直しを予約する。 */
    void handleTempoMapChanged();

    /** 直前のテンポの表。**「いま何拍目にいるか」を測るのに要ります。**

        テンポが変わると、同じ「秒」が別の「拍」を指すようになります。
        変わった**後**の表で測ると、変わる前にいた場所が分からないので、
        変える直前の表を控えておきます（8.101の「換算は必ず対で」の応用）。

        **再生を始めるときにも取り直します**（プロジェクトを開き直したときなど、
        通知を伴わずに表が入れ替わる道があるため）。 */
    TempoMap lastKnownTempoMap;

    /** 仕様書5.2.2：送り1本ぶんのノード（送り量を適用するゲイン）。 */
    struct SendNodes
    {
        juce::String targetTrackId;
        juce::AudioProcessorGraph::Node::Ptr gainNode;
        SendGainProcessor* gain = nullptr; // 所有権はgraph側
    };

    /** 1トラックぶんのノード列（音の出所 → …インサート… → フェーダー）。 */
    struct TrackNodes
    {
        juce::String trackId;

        // センドトラックは自分のクリップを持たず、他トラックからの送りを受けるだけ
        bool isSendTrack = false;

        // MIDIトラックはクリップ再生の代わりに「MIDIプレイヤー → 音源」が入口になる
        bool isMidiTrack = false;

        std::vector<SendNodes> sendNodes;

        juce::AudioProcessorGraph::Node::Ptr clipPlayerNode;
        juce::AudioProcessorGraph::Node::Ptr channelNode;
        ClipPlayerProcessor* clipPlayer = nullptr;      // 所有権はgraph側
        TrackChannelProcessor* channel = nullptr;       // 所有権はgraph側

        // 仕様書5.3：MIDIトラックの音源（Phase 14）。音源は未設定のこともあるので、
        // instrumentNodeはnullptrになり得る（その場合そのトラックは無音）。
        juce::AudioProcessorGraph::Node::Ptr midiPlayerNode;
        MidiPlayerProcessor* midiPlayer = nullptr;      // 所有権はgraph側
        juce::AudioProcessorGraph::Node::Ptr instrumentNode;
        std::unique_ptr<juce::DocumentWindow> instrumentEditorWindow;

        // 仕様書5.7・設計書1.4：インサートプラグイン（モデルと同じ並び順）。
        // エディタウィンドウをここで保持しているのは、トラックが消えたときに
        // プラグイン本体と一緒に確実に片付けるため。
        std::vector<juce::AudioProcessorGraph::Node::Ptr> insertNodes;
        juce::OwnedArray<juce::DocumentWindow> insertEditorWindows;
    };

    std::vector<TrackNodes> trackNodes;

    // 購読中のルートValueTree。差し替え時に「古い方の購読を外す」ために保持する。
    juce::ValueTree subscribedState;

    /** 1トラックぶんのノード（クリップ再生／MIDIプレイヤー／音源／インサート／フェーダー）を
        グラフから外す。エディタウィンドウはプラグイン本体より先に閉じる（HANDOVER 1.5）。 */
    void removeTrackNodesFromGraph (TrackNodes& nodes);

    /** プラグインを1つロードしてグラフへ追加する。失敗したらnullptrを返す。 */
    /** プラグインを読み込んでグラフのノードにする。

        8.144：`enableExtraOutputBuses`は**パラアウトを使う音源にだけ**true（Phase 182）。
        **グラフへ入れる前に**バスを有効にするための引数で、
        入れた後に有効にしても効きません（実装のコメント）。 */
    juce::AudioProcessorGraph::Node::Ptr createPluginNode (const juce::PluginDescription& description,
                                                            bool enableExtraOutputBuses = false);

    /** クリップ再生 → …インサート… → フェーダー、と数珠つなぎに接続する。 */
    void connectTrackChain (TrackNodes& nodes);

    /** そのトラックの経路上の接続を全て外す（繋ぎ直す前に呼ぶ）。 */
    void disconnectTrackChain (TrackNodes& nodes);

    /** 8.144：**出力バスを有効にした状態で音源を読み込み直す**（Phase 182）。

        バスを有効にできるのは**グラフへ入れる前だけ**なので（`createPluginNode()`）、
        既に載っている音源のバスを増やすには作り直すしかありません。
        **つまみの状態は持ち越します**——パラアウトを作ったら音色が変わった、
        というのが最悪の驚き方なので（1.8）。 */
    void reloadInstrumentWithOutputBuses (TrackNodes& nodes);

    /** 各トラックのプレイヤー（クリップ再生・MIDI）に、現在のモデルの内容を読み込ませる。
        いちばん遅い終端をendPositionSamplesへ反映する（自動停止・書き出し長の計算用）。
        再生開始時と、オフライン書き出しの直前に呼ぶ。 */
    void prepareTrackPlayersForPlayback (juce::int64& endPositionSamples);

    TrackNodes* findTrackNodes (const juce::String& trackId);
    const TrackNodes* findTrackNodes (const juce::String& trackId) const;

    /** オフライン処理の本体（グラフを1ブロックずつ回してファイルへ書く）。
        グラフが既にオフラインモードで準備済みであることが前提。

        8.153：**形式（WAV／MP3）はライターを差し替えるだけ**（Phase 191／D9b）。
        回すループはどちらでも同じなので、`options`から作った
        `juce::AudioFormatWriter`を1つ受け取る形にしてあります。 */
    juce::String renderOfflineToFile (const juce::File& file, double sampleRate, int blockSize,
                                       juce::int64 startPositionSamples,
                                       juce::int64 endPositionSamples,
                                       const ExportOptions& options, bool writeMono,
                                       std::function<bool (double)> progressCallback);

    /** 8.153：書き出すレートを決める（Phase 191／8.1のD9a）。

        **判断はここ1箇所**（8.2）。`options.sampleRate`が0なら
        いまのオーディオデバイスのレートを使います。 */
    double getExportSampleRate (const ExportOptions& options) const;

    /** 8.80：書き出す範囲を秒からサンプルへ直す（Phase 120/D11）。

        **判断はここ1箇所。** ミックスダウンとステムで別々に書くと、
        片方だけループ範囲が効かない、という食い違いになります（8.2）。
        ループが引かれていなければ、曲全体（0〜`endPositionSamples`）を返します。 */
    /** 8.81：そのステムを書き出すとき、**このトラックの音を通しておくか**（Phase 121）。

        ### なぜ要るか

        フォルダは**バス**です（8.51）。中のトラックはフォルダへ流れ、
        フォルダがマスターへ流します。
        「対象以外を全部黙らせる」だけだと、

          - フォルダの中のトラックを書き出す → **通り道のフォルダが黙っている**ので無音
          - フォルダ自身を書き出す → **中身が黙っている**ので無音

        となります（Phase 120まで、実際にそうなっていました）。

        ### 通すもの

          - 対象そのもの
          - 対象の**先祖のフォルダ**（通り道）
          - 対象がフォルダなら、その**子孫**（中身）

        センドトラックは呼び出し側が別に通します（送り先の残響を含めるため）。 */
    bool shouldTrackSoundForStem (const juce::String& trackId, const juce::String& targetId) const;

    //=========================================================================
    // 8.83：MIDI入力（Phase 123/改善案⑬）

    /** JUCEから届く入口。**MIDIスレッドから呼ばれます**
        （メッセージスレッドでもオーディオスレッドでもない）。 */
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;

    /** 鳴らす先を洗い直す。**メッセージスレッドから**呼ぶこと。

        トラックの作り直し・アームの切り替え・選択の移動で変わるので、
        そのどれからも呼びます（**判断はここ1箇所**。1.26）。 */
    void refreshMidiInputTargets();

    /** 8.84：鳴らす先1つぶん（Phase 124/改善案⑯）。

        **絞り込みの条件も一緒に持ちます。** MIDIスレッドからモデルを読みに行くと、
        メッセージスレッドの書き換えとぶつかります（1.15）。
        読む値は`refreshMidiInputTargets()`が写し取っておきます。 */
    struct MidiInputTarget
    {
        MidiPlayerProcessor* player = nullptr;
        juce::String deviceName;   // 空＝すべてのデバイスから
        int inputChannel = 0;      // 0＝すべてのチャンネル
        int outputChannel = 0;     // 0＝そのまま
    };

    /** 鳴らす先。**`midiInputTargetLock`で守ること**——
        MIDIスレッドが読み、メッセージスレッドが書き換えます。 */
    std::vector<MidiInputTarget> midiInputTargets;
    juce::CriticalSection midiInputTargetLock;

    /** 選択中のMIDIトラック（アームされたトラックが無いときの行き先）。 */
    juce::String midiInputTargetTrackId;

    void getExportRange (const ExportOptions& options, double sampleRate,
                          juce::int64 songEndSamples,
                          juce::int64& startOut, juce::int64& endOut) const;

    /** trackIdからモデル側のトラックを引く（見つからなければ無効なTrackを返す）。 */
    Track findTrackById (const juce::String& trackId) const;

    /** センドを受け取る入口のノード（インサート列の先頭、無ければフェーダー）。 */
    juce::AudioProcessorGraph::Node::Ptr getChainInputNode (TrackNodes& nodes) const;

    /** 8.144：1番以降の出力バスをまとめて有効にする（Phase 182）。有効な本数を返す。

        **`graph.addNode()`へ渡す前のインスタンスにだけ呼ぶこと。**
        入れた後だとグラフが古いチャンネル数のまま用意され、
        **増えたチャンネルへの接続が黙って断られます**（`createPluginNode()`の説明）。 */
    static int enableExtraOutputBusesOn (juce::AudioProcessor& instance);

    /** 仕様書5.7.2：そのノードのサイドチェイン入力バス（第2入力バス）の位置を調べる。

        `firstChannelOut`はグラフの接続で使うチャンネル番号（＝processBlockのバッファ内の位置）。
        サイドチェインを持たない・無効になっているプラグインではfalseを返す。 */
    static bool getSidechainInputChannels (juce::AudioProcessorGraph::Node::Ptr node,
                                            int& firstChannelOut, int& numChannelsOut);

    /** フェーダー直前のノード（プリフェーダー送り用）。 */
    juce::AudioProcessorGraph::Node::Ptr getPreFaderOutputNode (TrackNodes& nodes) const;

    /** ノードのプラグインから内部状態を取り出してモデルへ書き込む／書き戻す（設計書3.8）。 */
    void captureStateFromNode (PluginInstance instance, juce::AudioProcessorGraph::Node::Ptr node);
    static void applyPluginStateToNode (juce::AudioProcessorGraph::Node::Ptr node, const juce::MemoryBlock& data);

    /** プラグインのエディタを独立ウィンドウで開く（設計書3.7）。
        エディタを持たないプラグインでは、JUCE標準のパラメータ一覧で代用する。

        8.130：`isOn` / `setOn`を渡すと、**GUIの上にオン／オフの帯**が付きます
        （Phase 166／改善案9）。**切り替えられないものには渡さないこと**——
        押せないボタンが1つ増えるだけになります。 */
    void openEditorWindowFor (juce::AudioProcessorGraph::Node::Ptr node,
                               std::unique_ptr<juce::DocumentWindow>& windowMember,
                               std::function<bool()> isOn = nullptr,
                               std::function<void (bool)> setOn = nullptr);

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    ProjectModel& project;

    Transport transport;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph graph;

    juce::AudioProcessorGraph::Node::Ptr outputNode;

    // 仕様書5.7：マスターチャンネル（すべての音が最後に通る）
    juce::AudioProcessorGraph::Node::Ptr masterNode;
    MasterChannelProcessor* masterProcessor = nullptr; // 所有権はgraph側

    //==========================================================================
    // 8.69：マスターへのインサート（Phase 108／設計書1.4の`MASTERBUS/INSERTS`。8.1のD6）
    //
    // ### 並びはトラックと同じ「フェーダーの手前」
    //
    //   （全トラック・録音・メトロノーム）→ insert1 → … → insertN → マスター → 出力
    //
    // **トラックの`connectTrackChain()`と同じ向き**にしてあります。
    // マスターの後ろに置くと、**フェーダーを動かすたびにリミッターへ入る音が変わり**、
    // 「音量を下げたら潰れ方まで変わった」という分かりにくい振る舞いになります。
    //
    // ### 入り口は必ず`getMasterInputNode()`から取ること
    //
    // マスターへ音を流し込んでいる場所は何箇所もあります（トラックの出口・録音・
    // メトロノーム）。**`masterNode`へ直に繋ぐと、そこだけインサートを素通りします。**
    // Phase 108で、直に繋いでいた箇所は全部この関数経由へ寄せました。

    std::vector<juce::AudioProcessorGraph::Node::Ptr> masterInsertNodes;

    /** 開いているマスターインサートのエディタウィンドウ（本数はスロットに合わせて伸ばす）。 */
    juce::OwnedArray<juce::DocumentWindow> masterInsertEditorWindows;

    /** 実際に配線できたインサート列の先頭（`connectMasterChain()`が決める）。
        **「モデルの1本目」ではありません**：通さない指定のものや、
        ステレオでなくて繋がらなかったものは列に入らないため。 */
    juce::AudioProcessorGraph::Node::Ptr masterChainInputNode;

    /** マスターへ音を流し込む先。**インサート列の先頭**（無ければマスター本体）。 */
    juce::AudioProcessorGraph::Node::Ptr getMasterInputNode() const;

    /** 8.69：インサートの持ち主を引く。**trackIdが空文字ならマスター**（Phase 108）。

        エンジンはもともと「空文字＝マスター」という決まりで動いているので
        （`beginAutomationTouch()`）、インサートもその決まりに合わせている。 */
    Track findInsertHost (const juce::String& trackId);

    /** マスターのインサート列を配線する（先頭←→末尾→マスター）。 */
    void connectMasterChain();

    /** マスターのインサート列の配線を外す（**マスター→出力は残す**）。 */
    void disconnectMasterChain();

    /** モデルの`<MASTERBUS><INSERTS>`からプラグインを読み込み直す（プロジェクト読み込み時）。 */
    void rebuildMasterInsertNodes();

    // メトロノーム（Phase 38）。マスターの手前に繋ぐので、
    // **書き出しの前に必ず止めること**（クリックが混ざる）。
    juce::AudioProcessorGraph::Node::Ptr metronomeNode;
    MetronomeProcessor* metronomeProcessor = nullptr; // 所有権はgraph側

    // 仕様書5.4：入力（マイク／オーディオIF）→ RecorderProcessor → 出力 の経路。
    // 入力デバイスを開けなかった環境ではノードを作らないため、nullptrになり得る。
    bool inputAvailable = false;
    juce::String inputError;
    juce::AudioProcessorGraph::Node::Ptr inputNode;
    juce::AudioProcessorGraph::Node::Ptr recorderNode;
    RecorderProcessor* recorderProcessor = nullptr; // 所有権はgraph側
    juce::File lastRecordingFile;
    double recordStartSeconds = 0.0;

    /** 8.146：`stopRecording()`が集めたMIDI（Phase 184／改善案⑬a）。
        `takeRecordedMidi()`で取り出すまで持っています。 */
    std::vector<RecordedMidiTake> recordedMidiTakes;

    /** 8.146：MIDIを録音中か。**アームの切り替えで揺れないように、自分で持ちます**——
        録音中にアームを外されても、そのトラックの録り分は最後まで集めます。 */
    bool midiRecordingActive = false;

    PluginManager pluginManager;
    PluginCrashTracker crashTracker;
    PluginSandboxHost sandboxHost;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
