#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <atomic>

//==============================================================================
/**
    設計書3.4のサンドボックス親プロセス側。

    子プロセスを起動し、制御チャンネル経由でプラグインのロードを指示する。
    子プロセスがクラッシュしても、このクラスは接続断として検知するだけで、
    DAW本体は影響を受けない（仕様書5.8.1のサンドボックス化の目的）。

    Phase 5b-1では「安全に検証する」用途に限定している：
    - 危険なプラグインを別プロセスでロードして、成功／失敗を確認できる
    - 実際の音声処理は行わない（オーディオの共有メモリ受け渡しはPhase 5b-2）
*/
class PluginSandboxHost : private juce::ChildProcessCoordinator,
                           private juce::Timer
{
public:
    PluginSandboxHost();
    ~PluginSandboxHost() override;

    /** 子プロセスを起動して、指定プラグインのロードを試みる。
        結果は onLoadResult コールバックで非同期に通知される。
        戻り値は「子プロセスの起動自体に成功したか」（プラグインのロード可否ではない）。 */
    bool testLoadPlugin (const juce::PluginDescription& description, double sampleRate, int blockSize);

    /** 子プロセスを終了する。 */
    void shutdownWorker();

    bool isWorkerRunning() const { return workerRunning; }

    /** サンドボックス内のプラグインで1ブロック処理する（オーディオスレッドから呼ぶ）。
        子プロセスが応答しない場合はfalseを返す（呼び出し側は無音を出す等の判断をする）。 */
    bool processBlockViaSandbox (juce::AudioBuffer<float>& buffer);

    bool isPluginLoaded() const { return pluginLoaded; }

    /** サンドボックス内プラグインのGUIウィンドウを開く／閉じる（設計書3.7）。
        ウィンドウは子プロセス側で独立して表示される。 */
    void openSandboxedEditor();
    void closeSandboxedEditor();

    /** ロード結果の通知。success=falseの場合、messageに理由が入る。
        子プロセスがクラッシュした場合もsuccess=falseで、その旨がmessageに入る。 */
    std::function<void (bool success, const juce::String& message)> onLoadResult;

private:
    void handleMessageFromWorker (const juce::MemoryBlock& message) override;
    void handleConnectionLost() override;
    void timerCallback() override;

    void notifyResult (bool success, const juce::String& message);
    bool createSharedMemory();

    bool workerRunning = false;
    bool awaitingLoadResult = false;
    std::atomic<bool> pluginLoaded { false };

    juce::File sharedMemoryFile;
    std::unique_ptr<juce::MemoryMappedFile> sharedMemory;
    int requestCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginSandboxHost)
};
