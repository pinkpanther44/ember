#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_events/juce_events.h>

//==============================================================================
/**
    設計書3.4のサンドボックス子プロセス側。

    親プロセス（PluginSandboxHost）からの指示を受けて、この隔離されたプロセス内で
    プラグインをロードする。ここでプラグインがクラッシュしても、
    このプロセスだけが落ち、親プロセス（DAW本体）は無事に動き続ける
    ——これがサンドボックス化の本質的な価値（仕様書5.8.1）。

    Phase 5b-1では「ロードできたか／落ちたか」の判定までを担当する。
    オーディオ処理（共有メモリ経由でのバッファ受け渡し）はPhase 5b-2で追加する。
*/
class SandboxWorker : public juce::ChildProcessWorker,
                       private juce::AsyncUpdater,
                       private juce::Thread
{
public:
    SandboxWorker();
    ~SandboxWorker() override;

    void handleMessageFromCoordinator (const juce::MemoryBlock& message) override;
    void handleConnectionLost() override;

private:
    void handleAsyncUpdate() override;
    void run() override; // オーディオ処理ループ（共有メモリを監視）
    void loadPlugin (const juce::ValueTree& request);
    void openEditorWindow();
    void closeEditorWindow();
    void sendResult (bool success, const juce::String& errorMessage,
                     const juce::String& pluginName, int latencySamples);

    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;

    std::unique_ptr<juce::MemoryMappedFile> sharedMemory;
    juce::File sharedMemoryFile;
    int lastHandledRequest = 0;

    /** pluginInstanceの差し替え・破棄と、オーディオ処理ループからの参照が
        競合しないよう保護する（メッセージスレッドとオーディオスレッドの排他）。 */
    juce::CriticalSection pluginLock;

    // 宣言順が重要：エディタウィンドウはpluginInstanceより後に宣言することで、
    // 破棄時（宣言と逆順）にウィンドウが先に閉じられ、プラグイン本体が後に破棄される
    std::unique_ptr<juce::DocumentWindow> editorWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxWorker)
};
