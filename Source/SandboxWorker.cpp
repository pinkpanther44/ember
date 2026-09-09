#include "SandboxWorker.h"
#include "SandboxIPC.h"

SandboxWorker::SandboxWorker()
    : juce::Thread ("SandboxAudio")
{
    juce::addDefaultFormatsToManager (formatManager);
}

SandboxWorker::~SandboxWorker()
{
    signalThreadShouldExit();
    notify(); // wait()中なら起こして、速やかに終了させる
    stopThread (2000);
    cancelPendingUpdate();

    // ウィンドウを先に閉じてから、プラグイン本体を破棄する
    editorWindow = nullptr;

    // プラグインの後片付けを明示的に行う。releaseResources()を呼ばずに破棄すると、
    // プラグイン内部が確保したリソースが正しく解放されないことがある。
    {
        const juce::ScopedLock lock (pluginLock);

        if (pluginInstance != nullptr)
        {
            pluginInstance->releaseResources();
            pluginInstance = nullptr;
        }
    }

    sharedMemory = nullptr;
}

void SandboxWorker::handleMessageFromCoordinator (const juce::MemoryBlock& message)
{
    auto tree = juce::ValueTree::readFromData (message.getData(), message.getSize());

    if (! tree.isValid())
        return;

    if (tree.hasType (SandboxIPC::MSG_PING))
    {
        // 設計書3.4のハートビート：生きていることを親へ返す
        juce::ValueTree pong (SandboxIPC::MSG_PONG);
        juce::MemoryOutputStream stream;
        pong.writeToStream (stream);
        sendMessageToCoordinator (stream.getMemoryBlock());
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_SHUTDOWN))
    {
        juce::JUCEApplication::quit();
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_LOAD_PLUGIN))
    {
        loadPlugin (tree);
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_OPEN_EDITOR))
    {
        openEditorWindow();
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_CLOSE_EDITOR))
    {
        closeEditorWindow();
        return;
    }
}

void SandboxWorker::loadPlugin (const juce::ValueTree& request)
{
    const auto xmlString = request.getProperty (SandboxIPC::propPluginDescription).toString();
    const double sampleRate = request.getProperty (SandboxIPC::propSampleRate, 44100.0);
    const int blockSize = request.getProperty (SandboxIPC::propBlockSize, 512);
    const auto sharedMemoryPath = request.getProperty (SandboxIPC::propSharedMemoryName).toString();

    juce::PluginDescription description;

    if (auto xml = juce::XmlDocument::parse (xmlString))
        description.loadFromXml (*xml);
    else
    {
        sendResult (false, "PluginDescriptionの解析に失敗しました", {}, 0);
        return;
    }

    juce::String errorMessage;

    // ここが本番。もしこのプラグインが不正なバイナリだったり、
    // ロード中にクラッシュしたりしても、落ちるのはこの子プロセスだけ。
    auto newInstance = formatManager.createPluginInstance (description, sampleRate, blockSize, errorMessage);

    if (newInstance == nullptr)
    {
        sendResult (false, errorMessage.isNotEmpty() ? errorMessage : "不明なエラー", {}, 0);
        return;
    }

    newInstance->prepareToPlay (sampleRate, blockSize);

    {
        // オーディオ処理ループが古いインスタンスを触っている最中に
        // 差し替えてしまわないよう保護する
        const juce::ScopedLock lock (pluginLock);
        pluginInstance = std::move (newInstance);
    }

    // 共有メモリを開いて、オーディオ処理ループを開始する（設計書3.4）
    if (sharedMemoryPath.isNotEmpty())
    {
        sharedMemoryFile = juce::File (sharedMemoryPath);
        sharedMemory = std::make_unique<juce::MemoryMappedFile> (
            sharedMemoryFile, juce::MemoryMappedFile::readWrite);

        if (sharedMemory->getData() != nullptr)
        {
            auto* header = static_cast<SandboxIPC::SharedAudioHeader*> (sharedMemory->getData());
            lastHandledRequest = header->requestCounter.load();
            header->workerReady.store (true);

            // 優先度はhighに留める。highestにするとメッセージスレッドを圧迫し、
            // JUCE内部のping応答が滞って接続断と誤判定される（上記run()のコメント参照）。
            startThread (juce::Thread::Priority::high);
        }
    }

    sendResult (true, {}, pluginInstance->getName(), pluginInstance->getLatencySamples());
}

void SandboxWorker::run()
{
    // 共有メモリを監視し、親から処理要求が来たらプラグインで処理して返す。
    // スピン待ちに近い形になるが、オーディオのレイテンシを抑えるためにはこの方が確実。
    // （sleepを挟むと、ブロックあたりの待ち時間が跳ね上がって音が途切れやすくなる）
    auto* header = static_cast<SandboxIPC::SharedAudioHeader*> (sharedMemory->getData());
    auto* audioData = reinterpret_cast<float*> (
        static_cast<char*> (sharedMemory->getData()) + sizeof (SandboxIPC::SharedAudioHeader));

    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midiBuffer;

    // 待機方法の注意：以前はThread::yield()による純粋なスピン待ちを最高優先度で
    // 行っていたが、これはメッセージスレッドを枯渇させる。その結果、
    // JUCEのChildProcessWorkerが内部で行うping（生存確認）に応答できなくなり、
    // 「接続断」と判定されて子プロセスが自ら終了していた
    // （プラグインGUIを開くと描画負荷が加わり、必ず再現した）。
    //
    // そこで、短時間だけスピンして低レイテンシを稼ぎ、それでも要求が来なければ
    // wait()で確実にCPUを手放す方式にする。
    constexpr int spinCountBeforeWaiting = 200;
    int spinCount = 0;

    while (! threadShouldExit())
    {
        const int currentRequest = header->requestCounter.load();

        if (currentRequest == lastHandledRequest)
        {
            if (++spinCount < spinCountBeforeWaiting)
                juce::Thread::yield();
            else
                wait (1); // 1ミリ秒待つ。メッセージスレッドに実行機会を渡す

            continue;
        }

        spinCount = 0;
        const int numChannels = juce::jlimit (1, SandboxIPC::maxChannels, header->numChannels.load());
        const int numSamples = juce::jlimit (1, SandboxIPC::maxBlockSize, header->numSamples.load());

        // 共有メモリ上のデータを、プラグインに渡すバッファへコピーする
        buffer.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, audioData + ch * SandboxIPC::maxBlockSize, numSamples);

        // pluginInstanceの差し替え・破棄と競合しないよう保護する
        const juce::ScopedLock instanceLock (pluginLock);

        if (pluginInstance != nullptr)
        {
            midiBuffer.clear();

            // 重要：プラグインのコールバックロックを取得してからprocessBlockを呼ぶ。
            // このロックが無いと、GUI（メッセージスレッド）からのパラメータ変更と
            // オーディオ処理（このスレッド）が同時に走り、多くのVST3プラグインが
            // それを想定していないためクラッシュする（実際に発生・修正済み）。
            const juce::ScopedLock callbackLock (pluginInstance->getCallbackLock());
            pluginInstance->processBlock (buffer, midiBuffer);
        }

        // 処理結果を共有メモリへ書き戻す
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy (audioData + ch * SandboxIPC::maxBlockSize,
                                                buffer.getReadPointer (ch), numSamples);

        lastHandledRequest = currentRequest;
        header->responseCounter.store (currentRequest); // 完了を親へ通知
    }
}

namespace
{
    /** 子プロセス側でプラグインのGUIを表示するウィンドウ。 */
    class SandboxEditorWindow : public juce::DocumentWindow
    {
    public:
        SandboxEditorWindow (const juce::String& name, std::function<void()> onCloseIn)
            // 8.154：親プロセス側（`AudioEngine`のPluginEditorWindow）と揃える（Phase 192）
            : DocumentWindow (name, juce::Colours::darkgrey,
                               DocumentWindow::minimiseButton | DocumentWindow::closeButton),
              onClose (std::move (onCloseIn))
        {
        }

        void closeButtonPressed() override
        {
            if (onClose != nullptr)
                onClose();
        }

    private:
        std::function<void()> onClose;
    };
}

void SandboxWorker::openEditorWindow()
{
    if (pluginInstance == nullptr || ! pluginInstance->hasEditor())
        return;

    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true);
        return;
    }

    auto* editor = pluginInstance->createEditorIfNeeded();
    if (editor == nullptr)
        return;

    // 設計書3.7ではウィンドウハンドルを親プロセスへ埋め込む方式（SetParent等）を
    // 挙げているが、プロセスをまたぐウィンドウ親子関係はフォーカスやDPIの扱いが
    // 不安定になりやすい。ここでは子プロセス側で独立したウィンドウとして表示する
    // 方式を採る（見た目上の体験は同じで、実装リスクがはるかに低い）。
    auto window = std::make_unique<SandboxEditorWindow> (
        pluginInstance->getName() + " (sandboxed)",
        [this] { closeEditorWindow(); });

    window->setUsingNativeTitleBar (true);
    window->setContentOwned (editor, true);
    window->setResizable (true, false);
    window->centreWithSize (window->getWidth(), window->getHeight());
    window->setVisible (true);
    window->toFront (true);

    editorWindow = std::move (window);
}

void SandboxWorker::closeEditorWindow()
{
    editorWindow = nullptr;
}

void SandboxWorker::sendResult (bool success, const juce::String& errorMessage,
                                 const juce::String& pluginName, int latencySamples)
{
    juce::ValueTree result (SandboxIPC::MSG_LOAD_RESULT);
    result.setProperty (SandboxIPC::propSuccess, success, nullptr);
    result.setProperty (SandboxIPC::propErrorMessage, errorMessage, nullptr);
    result.setProperty (SandboxIPC::propPluginName, pluginName, nullptr);
    result.setProperty (SandboxIPC::propLatencySamples, latencySamples, nullptr);

    juce::MemoryOutputStream stream;
    result.writeToStream (stream);
    sendMessageToCoordinator (stream.getMemoryBlock());
}

void SandboxWorker::handleConnectionLost()
{
    // 親プロセスが終了した／接続が切れた場合は、この子プロセスも終了する
    // （孤児プロセスがバックグラウンドに残り続けるのを防ぐ）
    triggerAsyncUpdate();
}

void SandboxWorker::handleAsyncUpdate()
{
    juce::JUCEApplication::quit();
}
