#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "SandboxIPC.h"

#include <atomic>
#include <functional>

//==============================================================================
/**
    設計書3.4のサンドボックス親プロセス側。

    8.260：**Phase 268で「1インスタンスに1つ」へ変えました**。

    Phase 5b-2まで、これは`AudioEngine`の**メンバ1つ**でした——
    つまり**サンドボックスで動かせるプラグインは、同時に1つだけ**。
    いまは`SandboxedPluginProcessor`が1つずつ持ちます（＝プラグイン1つに子プロセス1つ）。

    > **プロセスは安くありません**（数十MB＋起動に数百ms）。
    > だからこそ**落ちた履歴のあるものだけ**サンドボックスへ回します
    > （`AudioEngine::createPluginNode()`。設計書5.8.1）。

    ### 何をする係か

    | | |
    |---|---|
    | 子プロセスを起こす | 自分自身のexeを`launchWorkerProcess()`で |
    | 読み込ませる | **返事を待ちます**（待たないと、成功したかを呼び出し側へ返せない） |
    | 1ブロック渡す | 共有メモリ。**待ちません**（`SandboxIPC.h`の「1ブロック遅らせて」） |
    | 状態をやり取りする | 保存のときに取り出し、読み込みのときに入れる |
    | 落ちたのを見張る | `handleConnectionLost()` → 以後はバイパス |

    **音のスレッドから呼んでよいのは`processBlock()`だけ**です。
    ほかは全部メッセージスレッドから呼びます。
*/
class PluginSandboxHost : private juce::ChildProcessCoordinator,
                           private juce::Timer
{
public:
    PluginSandboxHost();
    ~PluginSandboxHost() override;

    //==========================================================================
    /** 読み込んだプラグインの素性（`MSG_LOAD_RESULT`で返ってきたもの）。 */
    struct LoadedPluginInfo
    {
        juce::String name;
        int latencySamples = 0;
        bool acceptsMidi = false;
        bool producesMidi = false;
        bool hasEditor = false;
        double tailSeconds = 0.0;

        /** パラメータの表（番号順）。 */
        struct Parameter
        {
            /** プラグイン側のID（**番号ではなくこちらで覚えます**。版が上がると番号は動く）。 */
            juce::String id;

            juce::String name, label;
            float defaultValue = 0.0f;
            float currentValue = 0.0f;
            int numSteps = 0;
            bool isDiscrete = false;
        };

        std::vector<Parameter> parameters;
    };

    /** 子を起こしてプラグインを読ませ、**結果が返るまで待ちます**。

        メッセージスレッドから呼ぶこと（`createPluginInstance()`の途中で呼ばれます）。
        失敗したら`errorMessage`に理由が入ります。 */
    bool loadPlugin (const juce::PluginDescription& description,
                      double sampleRate, int blockSize,
                      LoadedPluginInfo& infoOut, juce::String& errorMessage);

    /** レートやブロック長が変わったことを伝える（**書き出しのときに来ます**。8.153）。 */
    void prepare (double sampleRate, int blockSize);

    void release();

    void shutdownWorker();

    bool isAlive() const { return workerRunning.load() && pluginLoaded.load(); }

    //==========================================================================
    // ここだけが音のスレッドから呼ばれます

    /** 1ブロック渡して、**前のブロックの結果**を受け取る。

        戻り値は「子が生きていて、音が返ってきたか」。falseのときは、
        呼び出し側が入力をそのまま通します（バイパス）。 */
    /** `replaceMidi`は「子が出したMIDIで置き換えるか」。

        **MIDIを出さないプラグインでは`false`にすること。** 置き換えてしまうと、
        そのプラグインを通るMIDIの経路が**そこで途切れます**。 */
    bool processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                        const juce::AudioPlayHead::PositionInfo* position, bool replaceMidi);

    /** オートメーションや画面から動かしたぶんを、次のブロックで子へ渡す。 */
    void queueParameterChange (int index, float value);

    /** 子のGUIで動いたぶんを受け取る（`processBlock()`のあとに呼ぶ）。 */
    int readParameterChanges (SandboxIPC::ParamChange* destination, int capacity);

    //==========================================================================
    // 状態（保存・復元）

    /** 子から状態を取り出す。**返事を待ちます**（保存のときだけ呼ばれます）。

        返事が来なければ、**最後に受け取ったもの**を返します（空よりはまし）。 */
    juce::MemoryBlock getPluginState();

    void setPluginState (const juce::MemoryBlock& state);

    //==========================================================================
    // GUI（Phase 269／8.262）

    /** 子が開いたGUIの素性。 */
    struct EditorInfo
    {
        /** 子プロセス側の窓のハンドル（Windowsは`HWND`）。

            **`nullptr`なら「はめ込めない」**——子は自分の窓として出しています。 */
        void* nativeHandle = nullptr;

        int width = 0, height = 0;
        bool resizable = false;
    };

    /**
        子にGUIを開けと**頼むだけ**。結果は`onEditorOpened`で返ってきます。

        `wantEmbedding`が真なら、親の窓へはめ込めるように**枠なしの窓**を作らせます。
        偽なら子が自分の窓として出します。

        ### **ここで返事を待ってはいけません**（8.262で踏みました）

        待つ形にしたところ、**2度目に開くときだけ5秒固まって失敗**しました。
        理由はWindowsの`SetParent`です——プロセスをまたいで窓を子にすると、
        **両方のスレッドの入力キューが繋がります**（`AttachThreadInput`と同じ状態）。
        繋がった相手が待ちに入ると、こちらの`CreateWindowEx`が**進みません**。
        実測では、親が諦める5秒ちょうどまで`addToDesktop()`が返りませんでした。

        待たない形なら、**親のメッセージスレッドは常に回っています**。
        ついでに、重いプラグインのGUIで**DAWが固まることも無くなります**。

        戻り値は「頼めたか」だけ。メッセージスレッドから呼ぶこと。
    */
    bool requestEditor (bool wantEmbedding);

    /** GUIが開いた（または開けなかった）。**メッセージスレッドで呼ばれます**。 */
    std::function<void (bool success, const EditorInfo&)> onEditorOpened;

    void closeEditor();

    /** はめ込んだ枠の大きさが変わった（＝利用者が窓を引き伸ばした）。 */
    void setEditorSize (int width, int height);

    /** プラグインが**自分で**大きさを変えた。**メッセージスレッドで呼ばれます**。 */
    std::function<void (int, int)> onEditorResized;

    /** 子の別窓の「×」が押された。**メッセージスレッドで呼ばれます**。 */
    std::function<void()> onEditorClosed;

    /** 子が落ちた（＝プラグインが落ちた）ときに呼ばれます。**IPCのスレッドから**。 */
    std::function<void()> onConnectionLost;

    /** 子のプラグイン自身が申告しているレイテンシ（ブロックぶんは含みません）。 */
    int getPluginLatencySamples() const { return pluginLatency.load(); }

    /** 行列に入り切らずに捨てたMIDIの数（8.263）。**0でなければ行列が小さすぎます**。

        音の`dropouts`と違い、こちらは**0であるべき数**です——
        MIDIを落とすと、そのあとずっと食い違ったままになります。 */
    juce::uint32 getMidiDropCount() const;

    /** **間に合わなかったブロックの数**（子が1ブロック以上遅れた回数）。

        そのブロックは**素通し**になります（無音より、原音が残るほうが害が小さい）。 */
    int getDropoutCount() const { return dropouts; }

    /** 子がまだ居るかを、その場で確かめる（`timerCallback()`と同じ中身）。

        **窓もメッセージループも無いところ**——`--sandbox-selftest`——から
        確かめるために公開しています。 */
    bool probeWorker();

private:
    void handleMessageFromWorker (const juce::MemoryBlock& message) override;
    void handleConnectionLost() override;

    /**
        **子がまだ居るかを、こちらから確かめます**（0.5秒ごと）。

        JUCEの`ChildProcessCoordinator`にも生存確認（ping）はありますが、
        **落ちたと判断するまでに`connectionTimeoutMs`（10秒）かかります**。
        その10秒のあいだ、プラグインは黙って素通しになり、
        **理由が画面に出ません**（実測：子を殺しても3秒では気づけませんでした）。

        > **pingの時間を短くするのは駄目**です。プラグインを読み込んでいる最中は、
        > 子のメッセージスレッドがそれに掛かりきりでpingに答えられません
        > ——重い音源（Kontakt等）を**読んでいる途中で殺す**ことになります。

        こちらは**送れたかどうかだけ**を見ます。相手が居ない名前付きパイプへの
        書き込みはその場で失敗するので、返事を待つ必要がありません。 */
    void timerCallback() override;

    bool createSharedMemory();
    bool sendTree (const juce::ValueTree& tree);

    //==========================================================================
    std::atomic<bool> workerRunning { false };
    std::atomic<bool> pluginLoaded { false };

    juce::File sharedMemoryFile;
    std::unique_ptr<juce::MemoryMappedFile> sharedMemory;

    int requestCounter = 0;
    int lastSeenResponse = 0;

    /** **前のブロックの結果**（1ブロック遅らせるため。`SandboxIPC.h`）。 */
    juce::AudioBuffer<float> pendingOutput;
    bool hasPendingBlock = false;

    std::atomic<int> pluginLatency { 0 };

    /** 間に合わなかった回数（音のスレッドだけが増やします）。 */
    std::atomic<int> dropouts { 0 };

    //==========================================================================
    // 返事待ち（IPCのスレッドから起こされます）

    juce::WaitableEvent loadResultArrived { true };
    juce::WaitableEvent prepareDoneArrived { true };
    juce::WaitableEvent stateArrived { true };

    juce::CriticalSection replyLock;
    juce::ValueTree lastLoadResult;
    juce::MemoryBlock lastState;

    //==========================================================================
    /** 親→子のパラメータ変化。

        **書くのはメッセージスレッドだけ**（オートメーションは`AudioEngine::timerCallback()`、
        画面もメッセージスレッド）。**読むのは音のスレッド**。
        1対1なので`AbstractFifo`で足ります——**音のスレッドで鍵を取りません**。 */
    juce::AbstractFifo parameterFifo { SandboxIPC::maxParamChanges * 4 };
    std::vector<SandboxIPC::ParamChange> parameterQueue;

    /** 子が返してきた「GUIで動いたぶん」。`processBlock()`が詰め、画面側が読み取ります。 */
    juce::AbstractFifo incomingFifo { SandboxIPC::maxParamChanges * 4 };
    std::vector<SandboxIPC::ParamChange> incomingQueue;

    juce::MidiBuffer scratchMidi;


    /** 接続が切れたことはIPCのスレッドで分かりますが、**タイマーはメッセージスレッドのもの**です。
        向こうへ回すあいだに自分が消えている場合に備えます。 */
    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginSandboxHost)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSandboxHost)
};
