#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>

//==============================================================================
/**
    設計書3.4「サンドボックスIPCプロトコル」。

    8.260：**Phase 268で、音だけの受け渡しから「挿して使える」ところまで広げました**。

    ```
    ┌── 親（Manta Studio） ──────────┐        ┌── 子（同じexe。SandboxWorker） ──┐
    │ SandboxedPluginProcessor        │        │  VST3 / LV2 の実体              │
    │   ├ 制御チャンネル（ValueTree） │◀──────▶│   ロード・状態・パラメータ表     │
    │   └ 共有メモリ（1ブロック）     │◀──────▶│   音・MIDI・パラメータ変化       │
    └─────────────────────────────────┘        └──────────────────────────────────┘
    ```

    **制御チャンネルは`juce::ChildProcessCoordinator`**（同じexeを子として起動できるので、
    別のexeをビルドしなくて済みます）。**音は共有メモリ**——制御チャンネルは
    メッセージごとに確保とコピーが入るので、ブロックごとに通せません。

    ### 親のオーディオスレッドは、子を待ちません（Phase 268で変更）

    Phase 5b-2では「書いて、子が終わるまで待って、読む」形にしていました。
    **待ち時間の上限が5ms**で、512サンプル・48kHzの予算（10.7ms）の半分です。

    いまは**1ブロック遅らせて**います：

    ```
    ブロックN：   前回（N-1）の結果を出力 → 今回の入力を書いて「処理して」と頼む
    ブロックN+1： N の結果を出力 → …
    ```

    子には**まるまる1ブロックぶんの時間**があるので、普通は待ち時間がゼロです
    （念のため2msだけ待つ保険は残してあります）。そのぶん**1ブロックの遅れ**が
    増えるので、`setLatencySamples()`で申告します（PDCが揃えます。設計書5.7.1）。

    ### 制御チャンネルのコールバックは、メッセージスレッドではありません

    JUCEの`ChildProcessCoordinator::Connection`は`InterprocessConnection (false, …)`で
    作られています——**専用のスレッドで届きます**。

    そのおかげで、**メッセージスレッドから返事を待って構いません**
    （プラグインを読む・状態を取り出す、は待たないと書けません）。
    **逆に、届いた側でUIを触るときは`callAsync()`でメッセージスレッドへ移すこと。**
*/
namespace SandboxIPC
{
    /** 親子プロセスを紐づける識別子。コマンドラインで渡されます。 */
    inline const juce::String commandLineUID { "personaldaw_plugin_sandbox" };

    /** 接続確立までの上限（ミリ秒）。 */
    constexpr int connectionTimeoutMs = 10000;

    /** プラグインを読む・状態をやり取りするときの上限（ミリ秒）。

        **プラグインのロードは遅いものがあります**（Kontaktのように数秒かかるもの）。
        短くすると「重いプラグインだけサンドボックスで開けない」になります。 */
    constexpr int loadTimeoutMs = 20000;
    constexpr int stateTimeoutMs = 3000;
    constexpr int prepareTimeoutMs = 5000;

    //==========================================================================
    // 制御チャンネルのメッセージ

    inline const juce::Identifier MSG_LOAD_PLUGIN   { "LOADPLUGIN" };    ///< 親→子：読み込め
    inline const juce::Identifier MSG_LOAD_RESULT   { "LOADRESULT" };    ///< 子→親：結果＋パラメータ表
    inline const juce::Identifier MSG_PREPARE       { "PREPARE" };       ///< 親→子：レートとブロック長が変わった
    inline const juce::Identifier MSG_PREPARE_DONE  { "PREPAREDONE" };   ///< 子→親：用意できた
    inline const juce::Identifier MSG_RELEASE       { "RELEASE" };       ///< 親→子：`releaseResources()`
    inline const juce::Identifier MSG_SET_STATE     { "SETSTATE" };      ///< 親→子：状態を入れろ
    inline const juce::Identifier MSG_GET_STATE     { "GETSTATE" };      ///< 親→子：状態をくれ
    inline const juce::Identifier MSG_STATE         { "STATE" };         ///< 子→親：状態
    inline const juce::Identifier MSG_SHUTDOWN      { "SHUTDOWN" };      ///< 親→子：終われ

    /** 親→子：**生きているか**（子は何もしません）。

        返事は要りません——**送れたかどうか**だけで分かります
        （`PluginSandboxHost::timerCallback()`）。 */
    inline const juce::Identifier MSG_PING          { "PING" };

    //==========================================================================
    // GUI（Phase 269）
    //
    // 8.262：**窓は子プロセスが作ります。親はそれを自分の窓へはめ込むだけ**です。
    //
    // ```
    //  親： SandboxedPluginEditor                子： EditorHolder（枠なしの窓）
    //        └ HWNDComponent ◀── ハンドル ──────── └ プラグイン本来のエディタ
    // ```
    //
    // **`MSG_OPEN_EDITOR`だけは、子のメッセージスレッドへ渡し直すこと。**
    // 制御チャンネルのコールバックは専用スレッドで届くので（上の注意書き）、
    // そこで窓を作ると**JUCEのGUIを別スレッドから触る**ことになります。

    inline const juce::Identifier MSG_OPEN_EDITOR     { "OPENEDITOR" };    ///< 親→子：GUIを開け
    inline const juce::Identifier MSG_CLOSE_EDITOR    { "CLOSEEDITOR" };   ///< 親→子：GUIを閉じよ
    inline const juce::Identifier MSG_SET_EDITOR_SIZE { "EDITORSIZE" };    ///< 親→子：この大きさにしろ
    inline const juce::Identifier MSG_EDITOR_OPENED   { "EDITOROPENED" };  ///< 子→親：開いた（＋ハンドルと大きさ）
    inline const juce::Identifier MSG_EDITOR_RESIZED  { "EDITORRESIZED" };///< 子→親：プラグインが自分で大きさを変えた
    inline const juce::Identifier MSG_EDITOR_CLOSED   { "EDITORCLOSED" };  ///< 子→親：別窓の「×」が押された

    // プロパティ
    inline const juce::Identifier propPluginDescription { "pluginDescription" };
    inline const juce::Identifier propSuccess           { "success" };
    inline const juce::Identifier propErrorMessage      { "errorMessage" };
    inline const juce::Identifier propPluginName        { "pluginName" };
    inline const juce::Identifier propLatencySamples    { "latencySamples" };
    inline const juce::Identifier propSampleRate        { "sampleRate" };
    inline const juce::Identifier propBlockSize         { "blockSize" };
    inline const juce::Identifier propSharedMemoryName  { "sharedMemoryName" };
    inline const juce::Identifier propAcceptsMidi       { "acceptsMidi" };
    inline const juce::Identifier propProducesMidi      { "producesMidi" };
    inline const juce::Identifier propTailSeconds       { "tailSeconds" };
    inline const juce::Identifier propHasEditor         { "hasEditor" };
    inline const juce::Identifier propStateData         { "stateData" };

    inline const juce::Identifier propWantEmbedding     { "wantEmbedding" };
    inline const juce::Identifier propEditorHandle      { "editorHandle" };   ///< HWND／X11のID（0なら別窓）
    inline const juce::Identifier propEditorWidth       { "editorWidth" };
    inline const juce::Identifier propEditorHeight      { "editorHeight" };
    inline const juce::Identifier propEditorResizable   { "editorResizable" };

    /** `MSG_LOAD_RESULT`にぶら下がるパラメータ1つぶん。

        **親はこれで同じ数・同じ名前のパラメータを立てます**——
        そうしないと、オートメーション（`insert:スロット:番号`。仕様書5.6）が
        番号で引けません。 */
    inline const juce::Identifier treeParameters  { "PARAMETERS" };
    inline const juce::Identifier treeParameter   { "PARAM" };
    inline const juce::Identifier propParamId      { "id" };
    inline const juce::Identifier propParamName    { "name" };
    inline const juce::Identifier propParamLabel   { "label" };
    inline const juce::Identifier propParamDefault { "default" };
    inline const juce::Identifier propParamValue   { "value" };
    inline const juce::Identifier propParamSteps   { "steps" };
    inline const juce::Identifier propParamDiscrete { "discrete" };

    //==========================================================================
    // 共有メモリ

    constexpr juce::uint32 sharedMemoryMagic = 0x504d4442;   // 'PMDB'

    constexpr int maxChannels = 2;
    constexpr int maxBlockSize = 8192;

    /** 1ブロックで運べるパラメータ変化の数（方向ごと）。 */
    constexpr int maxParamChanges = 512;

    struct ParamChange
    {
        int index;
        float value;
    };

    //==========================================================================
    // MIDIは**置き場ではなく、行列（リングバッファ）で運びます**（8.263／Phase 269）
    //
    // 音は置き場と同じ寿命で構いません——**間に合わなかったブロックの音は捨てて
    // 素通しにするのが正解**です。MIDIは違います。
    //
    // > **音は流れ、MIDIは状態です。** 音を1ブロック落とすと「一瞬エフェクトが
    // > 掛からない」で済みますが、**ノートオフを1つ落とすと、その音は鳴りっぱなし**に
    // > なります。あとから取り返せません。
    //
    // 置き場に載せていた頃は、**子が置き場の数（2つ）より遅れると消えていました**。
    // 実測：子のオーディオスレッドが立ち上がる前の1〜2ブロックが飛んで、
    // **10回に1回ほど、最初のノートが鳴りませんでした**（`--sandbox-selftest`）。
    //
    // 行列なら**置き場と切り離される**ので、子が何ブロック遅れても消えません
    // （遅れて届くだけです。位置はそのブロックの中へ丸めます）。

    /** 行列の大きさ（方向ごと）。**1イベント8バイト＋中身**なので、数千件入ります。 */
    constexpr int midiRingBytes = 65536;

    /**
        一方通行の行列。**書くのは1つのスレッド、読むのも1つのスレッド**です
        （親の音のスレッド ⇄ 子の音のスレッド）。だから鍵が要りません。

        `writePos`と`readPos`は**増え続ける通し番号**で、実際の位置は
        `% midiRingBytes`です（引き算が符号なしで正しく回ります）。
    */
    struct MidiRing
    {
        std::atomic<juce::uint32> writePos { 0 };
        std::atomic<juce::uint32> readPos { 0 };

        /** 入り切らずに捨てた数。**0でないなら、行列が小さすぎます**。 */
        std::atomic<juce::uint32> dropped { 0 };
    };

    inline void ringWrite (juce::uint8* buffer, juce::uint32 position,
                            const void* source, int numBytes)
    {
        const auto start = position % (juce::uint32) midiRingBytes;
        const auto firstPart = juce::jmin ((int) ((juce::uint32) midiRingBytes - start), numBytes);

        std::memcpy (buffer + start, source, (size_t) firstPart);

        if (firstPart < numBytes)
            std::memcpy (buffer, static_cast<const juce::uint8*> (source) + firstPart,
                          (size_t) (numBytes - firstPart));
    }

    inline void ringRead (const juce::uint8* buffer, juce::uint32 position,
                           void* destination, int numBytes)
    {
        const auto start = position % (juce::uint32) midiRingBytes;
        const auto firstPart = juce::jmin ((int) ((juce::uint32) midiRingBytes - start), numBytes);

        std::memcpy (destination, buffer + start, (size_t) firstPart);

        if (firstPart < numBytes)
            std::memcpy (static_cast<juce::uint8*> (destination) + firstPart, buffer,
                          (size_t) (numBytes - firstPart));
    }

    /** 1件ぶんの見出し（位置と長さ）。中身はこのすぐ後ろに続きます。 */
    constexpr int midiRecordHeaderBytes = (int) sizeof (juce::int32) * 2;

    /** 書く側（親も子も使います）。**入り切らないぶんは捨てて数えます**。 */
    inline void pushMidi (MidiRing& ring, juce::uint8* buffer, const juce::MidiBuffer& source)
    {
        if (source.isEmpty())
            return;

        auto write = ring.writePos.load (std::memory_order_relaxed);
        const auto read = ring.readPos.load (std::memory_order_acquire);

        for (const auto metadata : source)
        {
            const int needed = midiRecordHeaderBytes + metadata.numBytes;
            const auto used = (juce::uint32) (write - read);

            if ((juce::uint32) needed > (juce::uint32) midiRingBytes - used)
            {
                ring.dropped.fetch_add (1);
                break;
            }

            const juce::int32 position = metadata.samplePosition;
            const juce::int32 length = metadata.numBytes;

            ringWrite (buffer, write, &position, (int) sizeof (position));
            write += (juce::uint32) sizeof (position);
            ringWrite (buffer, write, &length, (int) sizeof (length));
            write += (juce::uint32) sizeof (length);
            ringWrite (buffer, write, metadata.data, length);
            write += (juce::uint32) length;
        }

        // **最後に位置を進めること。** 先に進めると、まだ書いていない中身を読まれます
        ring.writePos.store (write, std::memory_order_release);
    }

    /** 読む側。**溜まっているぶんを全部**取り出します（`numSamples`の中へ丸めます）。 */
    inline void popMidi (MidiRing& ring, const juce::uint8* buffer,
                          juce::MidiBuffer& destination, int numSamples)
    {
        auto read = ring.readPos.load (std::memory_order_relaxed);
        const auto write = ring.writePos.load (std::memory_order_acquire);

        // **SysExが入る大きさにすること**（256では普通のSysExが飛ばされます）
        juce::uint8 bytes[4096];

        while ((juce::uint32) (write - read) >= (juce::uint32) midiRecordHeaderBytes)
        {
            juce::int32 position = 0, length = 0;

            ringRead (buffer, read, &position, (int) sizeof (position));
            ringRead (buffer, read + (juce::uint32) sizeof (position), &length, (int) sizeof (length));

            if (length <= 0)
                break;   // 壊れている：これ以上読まない

            if ((juce::uint32) (write - read) < (juce::uint32) (midiRecordHeaderBytes + length))
                break;   // まだ書き終わっていない

            // **長すぎるものは飛ばすこと**（止めてはいけません）。
            // ここで`break`すると`readPos`が進まず、**以後のMIDIが永久に詰まります**
            // ——長いSysEx1本で、鍵盤が効かなくなるということです
            if (length > (int) sizeof (bytes))
            {
                read += (juce::uint32) (midiRecordHeaderBytes + length);
                ring.dropped.fetch_add (1);
                continue;
            }

            ringRead (buffer, read + (juce::uint32) midiRecordHeaderBytes, bytes, length);

            // **遅れて届いたぶんは、このブロックの頭へ寄せます**
            destination.addEvent (bytes, length, juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                                                 (int) position));

            read += (juce::uint32) (midiRecordHeaderBytes + length);
        }

        ring.readPos.store (read, std::memory_order_release);
    }


    /**
        置き場は**2つずつ**あり、ブロックごとに交互に使います（Phase 268で追加）。

        1ブロック遅らせる形にすると、**親が次の入力を書く時刻と、
        子が前の入力を読む時刻が重なります**。1つしか無いと、
        子が遅れた瞬間に**読んでいる最中のものを親が上書き**します
        （実測：測り比べが0.14ずれて出ました。音では「たまにプチッ」になります）。

        交互にすれば、**子が1ブロック遅れるまでは絶対に重なりません**。
        2ブロック以上遅れたぶんは捨てます（そこは`dropouts`で数えます）。

        **捨てるのは音だけです。** 8.263：MIDIは置き場に載せず、
        **独立した行列**で運びます（`MidiRing`）——ノートオフを1つ落とすと、
        その音は**鳴りっぱなし**になるからです。
    */
    constexpr int numSlots = 2;

    inline int slotFor (int counter) { return counter & 1; }

    /**
        共有メモリの先頭。**親と子で同じ並びであること**（同じexeなので、
        版が食い違うことはありません。`magic`は消し忘れた古いファイルよけです）。
    */
    struct SharedAudioHeader
    {
        std::atomic<juce::uint32> magic { 0 };

        std::atomic<int> requestCounter { 0 };   ///< 親が進める：このブロックを処理してほしい
        std::atomic<int> responseCounter { 0 };  ///< 子が進める：処理し終えた

        std::atomic<bool> workerReady { false };

        // **ブロックごとの値は、置き場と同じく2つずつ**（上の`numSlots`）
        std::atomic<int> numChannels[numSlots] { { 0 }, { 0 } };
        std::atomic<int> numSamples[numSlots] { { 0 }, { 0 } };

        std::atomic<int> paramInCount[numSlots] { { 0 }, { 0 } };    ///< 親→子
        std::atomic<int> paramOutCount[numSlots] { { 0 }, { 0 } };   ///< 子→親（GUIで動いたぶん）

        std::atomic<int> latencySamples { 0 };   ///< 子が知らせる、プラグイン自身の遅れ

        // 8.263：**MIDIは置き場ではなく行列で運びます**（下の`MidiRing`）
        MidiRing midiToWorker;     ///< 親→子（鍵盤・オートメーション外のCC）
        MidiRing midiFromWorker;   ///< 子→親（アルペジエーターなどが出すぶん）

        //----------------------------------------------------------------------
        // 8.206：**再生位置とテンポ**（Phase 238で本体に入れたもの）。
        //
        // **サンドボックス越しでも渡すこと。** 渡さないと、テンポ同期の
        // ディレイやアルペジエーターが、**サンドボックスに入れた瞬間だけ**同期しなくなります
        std::atomic<bool> playHeadValid { false };
        std::atomic<bool> isPlaying { false };
        std::atomic<double> bpm { 120.0 };
        std::atomic<double> ppqPosition { 0.0 };
        std::atomic<double> timeSeconds { 0.0 };
        std::atomic<juce::int64> timeInSamples { 0 };
        std::atomic<int> timeSigNumerator { 4 };
        std::atomic<int> timeSigDenominator { 4 };
    };

    //==========================================================================
    // 置き場所（**両側がこの関数を通すこと**。数えるところを2つ持たない。1.27）

    constexpr size_t audioSlotBytes = sizeof (float) * maxChannels * maxBlockSize;
    constexpr size_t paramSlotBytes = sizeof (ParamChange) * maxParamChanges;

    constexpr size_t audioOffset      = sizeof (SharedAudioHeader);
    constexpr size_t midiToWorkerOffset   = audioOffset + audioSlotBytes * numSlots;
    constexpr size_t midiFromWorkerOffset = midiToWorkerOffset + (size_t) midiRingBytes;
    constexpr size_t paramInOffset    = midiFromWorkerOffset + (size_t) midiRingBytes;
    constexpr size_t paramOutOffset   = paramInOffset + paramSlotBytes * numSlots;
    constexpr size_t sharedMemorySize = paramOutOffset + paramSlotBytes * numSlots;

    inline SharedAudioHeader* header (void* base)
    {
        return static_cast<SharedAudioHeader*> (base);
    }

    inline float* audioData (void* base, int slot)
    {
        return reinterpret_cast<float*> (static_cast<char*> (base) + audioOffset
                                           + audioSlotBytes * (size_t) slot);
    }

    /** 親→子のMIDIの行列（**置き場と違って、番号で割りません**。8.263）。 */
    inline juce::uint8* midiToWorkerBuffer (void* base)
    {
        return reinterpret_cast<juce::uint8*> (static_cast<char*> (base) + midiToWorkerOffset);
    }

    /** 子→親のMIDIの行列。 */
    inline juce::uint8* midiFromWorkerBuffer (void* base)
    {
        return reinterpret_cast<juce::uint8*> (static_cast<char*> (base) + midiFromWorkerOffset);
    }

    inline ParamChange* paramIn (void* base, int slot)
    {
        return reinterpret_cast<ParamChange*> (static_cast<char*> (base) + paramInOffset
                                                 + paramSlotBytes * (size_t) slot);
    }

    inline ParamChange* paramOut (void* base, int slot)
    {
        return reinterpret_cast<ParamChange*> (static_cast<char*> (base) + paramOutOffset
                                                 + paramSlotBytes * (size_t) slot);
    }

}
