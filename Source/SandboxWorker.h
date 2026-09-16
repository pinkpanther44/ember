#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "SandboxIPC.h"

#include <vector>

//==============================================================================
/**
    設計書3.4のサンドボックス子プロセス側。

    親（`PluginSandboxHost`）の指示でプラグインを読み、**この隔離されたプロセスの中で**
    鳴らします。ここでプラグインが落ちても、落ちるのはこのプロセスだけで、
    **親（DAW本体）は動き続けます**——それがサンドボックスの本質です（仕様書5.8.1）。

    8.260：**Phase 268で、音だけでなく MIDI・パラメータ・状態も通すようにしました。**

    ### 待ち方に気をつけること（4.2の再掲）

    **最高優先度でスピン待ちをしてはいけません。** メッセージスレッドが枯渇し、
    JUCEの`ChildProcessWorker`が内部で行うping（生存確認）に応えられなくなって、
    **「接続断」と誤判定され、この子プロセスが自分から終了します。**

    紛らわしいのは**クラッシュに見える**ことです。`コード 0 (0x0) で終了しました`と
    出ていたら、それは落ちたのではなく**正常終了**で、pingのタイムアウトを疑うこと。

    いまは「短くスピン → `wait(1)`」で、優先度は`high`までにしてあります。
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
    void run() override;   ///< オーディオ処理ループ（共有メモリを見張る）

    void loadPlugin (const juce::ValueTree& request);
    void prepare (const juce::ValueTree& request);
    void releasePlugin();
    void sendState();
    void applyState (const juce::ValueTree& request);

    void openEditorWindow (bool wantEmbedding);
    void resizeEditor (int width, int height);
    void closeEditorWindow();
    void closeEditorWindowAndTell();

    void sendEditorOpened (bool success, void* handle, int width, int height, bool resizable);
    void sendEditorResized (int width, int height);

    void sendResult (bool success, const juce::String& errorMessage);
    void sendTree (const juce::ValueTree& tree);

    /** 親が置いていったパラメータ変化を当てる（**音のスレッドから**）。 */
    void applyParameterChanges (int count, int slot);

    /** プラグイン側で動いた値を親へ返す（**音のスレッドから**）。 */
    void collectParameterChanges (int slot);

    //==========================================================================
    juce::AudioPluginFormatManager formatManager;
    std::unique_ptr<juce::AudioPluginInstance> pluginInstance;

    std::unique_ptr<juce::MemoryMappedFile> sharedMemory;
    juce::File sharedMemoryFile;
    int lastHandledRequest = 0;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    /** 親へ返す前の「前回の値」。**変わったぶんだけ返します**。 */
    std::vector<float> lastParameterValues;

    /**
        8.261：**当てた値を、メッセージスレッドから「変わったよ」と知らせる係**（Phase 268）。

        音のスレッドでは`setValue()`しか呼びません（そこで`setValueNotifyingHost()`を
        呼ぶのは筋が悪い）。ところが**JUCEで書かれたプラグインは、`APVTS`が
        「知らせ」を聞いて内部の木を更新します**——知らせないと、

        - 音は変わる（`getRawParameterValue()`は生の値を見るので）
        - **`getStateInformation()`は古いまま**（木が更新されていない）

        という食い違いが出ます。**保存したら元に戻る**、という形で出ます。

        `parameterValueChanged`は本来メッセージスレッドの話なので、
        **音のスレッドは旗を立てるだけ**にして、ここが後から知らせます。
    */
    class ParameterNotifier final : private juce::AsyncUpdater
    {
    public:
        explicit ParameterNotifier (SandboxWorker& ownerToUse) : owner (ownerToUse) {}
        ~ParameterNotifier() override { cancelPendingUpdate(); }

        void notifySoon() { triggerAsyncUpdate(); }

    private:
        void handleAsyncUpdate() override;

        SandboxWorker& owner;
    };

    ParameterNotifier parameterNotifier { *this };

    /** 知らせ済みの値。**`lastParameterValues`とは別**（あちらは親へ返すため）。 */
    std::vector<float> notifiedValues;

    /** `pluginInstance`の差し替え・破棄と、処理ループの参照がぶつからないように。 */
    juce::CriticalSection pluginLock;

    /** 共有メモリから読んだ再生位置を、プラグインへ渡すための入れ物。 */
    class SharedPlayHead;
    std::unique_ptr<SharedPlayHead> playHead;

    //==========================================================================
    // 8.262：**プラグイン本来のGUI**（Phase 269）。
    //
    // **宣言の順が3つとも大事**です（逆順に壊れます。1.5）：
    //
    // 1. `pluginInstance` ——いちばん後に消える
    // 2. `editorHolder`   ——エディタの入れ物
    // 3. `editorWindow`   ——別窓のときだけ。**いちばん先に消える**
    //
    // `editorWindow`は`setContentNonOwned`なので、窓が消えても入れ物は残ります。

    class EditorHolder;
    std::unique_ptr<EditorHolder> editorHolder;
    std::unique_ptr<juce::DocumentWindow> editorWindow;

    /** はめ込んだときの窓のハンドル（Windowsは`HWND`）。別窓なら`nullptr`。 */
    void* embeddedHandle = nullptr;

    /** 送り直した`callAsync`が、この子が消えたあとに走らないように。 */
    JUCE_DECLARE_WEAK_REFERENCEABLE (SandboxWorker)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SandboxWorker)
};
