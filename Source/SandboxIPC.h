#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

//==============================================================================
/**
    設計書3.4「サンドボックスIPCプロトコル」の共通定義。

    Phase 5b-1では制御チャンネルのみを実装する（ValueTreeをシリアライズして送受信）。
    オーディオ／MIDIバッファの共有メモリ経由での受け渡しはPhase 5b-2で対応する。

    JUCEのChildProcessCoordinator/ChildProcessWorkerは、同じ実行ファイルを
    子プロセスとして起動できるため、別exeをビルドする必要がない。
    起動時にコマンドラインを見て、親か子かを判定する（Main.cpp参照）。
*/
namespace SandboxIPC
{
    /** 親子プロセスを紐づけるための識別子。コマンドラインで受け渡される。 */
    inline const juce::String commandLineUID { "personaldaw_plugin_sandbox" };

    /** 接続確立までのタイムアウト（ミリ秒）。 */
    constexpr int connectionTimeoutMs = 10000;

    // メッセージのValueTree種別
    inline const juce::Identifier MSG_LOAD_PLUGIN   { "LOADPLUGIN" };   // 親→子：プラグインをロードせよ
    inline const juce::Identifier MSG_LOAD_RESULT   { "LOADRESULT" };   // 子→親：ロード結果
    inline const juce::Identifier MSG_PING          { "PING" };          // 親→子：生存確認（設計書3.4のハートビート）
    inline const juce::Identifier MSG_PONG          { "PONG" };          // 子→親：生存応答
    inline const juce::Identifier MSG_SHUTDOWN      { "SHUTDOWN" };      // 親→子：終了せよ
    inline const juce::Identifier MSG_OPEN_EDITOR   { "OPENEDITOR" };    // 親→子：プラグインGUIを開け（設計書3.7）
    inline const juce::Identifier MSG_CLOSE_EDITOR  { "CLOSEEDITOR" };   // 親→子：プラグインGUIを閉じよ

    // メッセージのプロパティ
    inline const juce::Identifier propPluginDescription { "pluginDescription" }; // PluginDescriptionのXML文字列
    inline const juce::Identifier propSuccess           { "success" };
    inline const juce::Identifier propErrorMessage      { "errorMessage" };
    inline const juce::Identifier propPluginName        { "pluginName" };
    inline const juce::Identifier propLatencySamples    { "latencySamples" };
    inline const juce::Identifier propSampleRate        { "sampleRate" };
    inline const juce::Identifier propBlockSize         { "blockSize" };
    inline const juce::Identifier propSharedMemoryName  { "sharedMemoryName" };

    //==============================================================================
    /**
        共有メモリの先頭に置くヘッダー。設計書3.4の「オーディオ／MIDIチャンネル」に対応。

        Phase 5b-2では、親がバッファを書き込んで要求フラグを立て、子が処理して
        完了フラグを返す、というシンプルなハンドシェイク方式を採る。
        設計書が挙げるリングバッファ方式より単純だが、1ブロックずつ確実に
        同期して処理でき、実装とデバッグがはるかに容易。

        重要な制約：この方式では、親のオーディオコールバックが子の処理完了を
        待つことになる（同期処理）。子プロセスが応答しない場合に備え、
        待ち時間には必ず上限を設ける（無音を返してでもコールバックは返す）。
    */
    struct SharedAudioHeader
    {
        std::atomic<int> requestCounter { 0 };  // 親がインクリメント：処理してほしい
        std::atomic<int> responseCounter { 0 }; // 子がインクリメント：処理し終えた
        std::atomic<int> numChannels { 0 };
        std::atomic<int> numSamples { 0 };
        std::atomic<bool> workerReady { false };
    };

    constexpr int maxChannels = 2;
    constexpr int maxBlockSize = 8192;

    /** 共有メモリ全体のサイズ（ヘッダー＋オーディオデータ） */
    constexpr size_t sharedMemorySize = sizeof (SharedAudioHeader)
                                          + sizeof (float) * maxChannels * maxBlockSize;

    /** 親がブロック処理の完了を待つ上限時間（マイクロ秒）。
        これを超えたら諦めて無音を返す（オーディオスレッドを止め続けないため）。 */
    constexpr int processTimeoutMicroseconds = 5000;
}
