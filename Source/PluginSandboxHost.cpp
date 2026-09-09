#include "PluginSandboxHost.h"
#include "SandboxIPC.h"
#include "Utf8.h"

PluginSandboxHost::PluginSandboxHost() = default;

PluginSandboxHost::~PluginSandboxHost()
{
    stopTimer();
    shutdownWorker();

    sharedMemory = nullptr;
    if (sharedMemoryFile.existsAsFile())
        sharedMemoryFile.deleteFile();
}

bool PluginSandboxHost::createSharedMemory()
{
    // 共有メモリはメモリマップトファイルとして実装する
    // （JUCEのMemoryMappedFileはWindows/Linux両対応で、追加の依存が不要）。
    sharedMemory = nullptr;

    sharedMemoryFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("personaldaw_sandbox_" + juce::Uuid().toString() + ".shm");

    // 必要サイズ分のファイルをあらかじめ確保しておく
    {
        juce::FileOutputStream out (sharedMemoryFile);
        if (! out.openedOk())
            return false;

        juce::HeapBlock<char> zeros (SandboxIPC::sharedMemorySize, true);
        out.write (zeros.get(), SandboxIPC::sharedMemorySize);
        out.flush();
    }

    sharedMemory = std::make_unique<juce::MemoryMappedFile> (
        sharedMemoryFile, juce::MemoryMappedFile::readWrite);

    if (sharedMemory->getData() == nullptr)
    {
        sharedMemory = nullptr;
        return false;
    }

    // ヘッダーを初期化する（placement newでatomicを正しく構築する）
    new (sharedMemory->getData()) SandboxIPC::SharedAudioHeader();
    requestCounter = 0;

    return true;
}

bool PluginSandboxHost::testLoadPlugin (const juce::PluginDescription& description,
                                          double sampleRate, int blockSize)
{
    pluginLoaded.store (false);

    if (! createSharedMemory())
        return false;

    if (! workerRunning)
    {
        // 自分自身のexeを子プロセスとして起動する。
        // 子プロセス側はMain.cppでコマンドラインを見て、ワーカーとして動作する。
        const auto thisExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        if (! launchWorkerProcess (thisExe, SandboxIPC::commandLineUID, SandboxIPC::connectionTimeoutMs))
            return false;

        workerRunning = true;
    }

    // プラグイン情報と共有メモリの場所を子プロセスへ送る
    juce::ValueTree request (SandboxIPC::MSG_LOAD_PLUGIN);

    if (auto xml = description.createXml())
        request.setProperty (SandboxIPC::propPluginDescription, xml->toString(), nullptr);

    request.setProperty (SandboxIPC::propSampleRate, sampleRate, nullptr);
    request.setProperty (SandboxIPC::propBlockSize, blockSize, nullptr);
    request.setProperty (SandboxIPC::propSharedMemoryName, sharedMemoryFile.getFullPathName(), nullptr);

    juce::MemoryOutputStream stream;
    request.writeToStream (stream);

    awaitingLoadResult = true;
    startTimer (SandboxIPC::connectionTimeoutMs); // 応答が返らない場合の保険

    return sendMessageToWorker (stream.getMemoryBlock());
}

bool PluginSandboxHost::processBlockViaSandbox (juce::AudioBuffer<float>& buffer)
{
    if (! pluginLoaded.load() || sharedMemory == nullptr || sharedMemory->getData() == nullptr)
        return false;

    auto* header = static_cast<SandboxIPC::SharedAudioHeader*> (sharedMemory->getData());

    if (! header->workerReady.load())
        return false;

    const int numChannels = juce::jlimit (1, SandboxIPC::maxChannels, buffer.getNumChannels());
    const int numSamples = juce::jlimit (1, SandboxIPC::maxBlockSize, buffer.getNumSamples());

    auto* audioData = reinterpret_cast<float*> (
        static_cast<char*> (sharedMemory->getData()) + sizeof (SandboxIPC::SharedAudioHeader));

    // 入力を共有メモリへ書き込む
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::copy (audioData + ch * SandboxIPC::maxBlockSize,
                                            buffer.getReadPointer (ch), numSamples);

    header->numChannels.store (numChannels);
    header->numSamples.store (numSamples);

    // 要求カウンタを進めて、子プロセスに処理を依頼する
    ++requestCounter;
    header->requestCounter.store (requestCounter);

    // 子の処理完了を待つ。ただしオーディオスレッドを止め続けないよう、必ず上限を設ける。
    const auto startTime = juce::Time::getHighResolutionTicks();
    const auto timeoutTicks = (juce::int64) (juce::Time::getHighResolutionTicksPerSecond()
                                                * SandboxIPC::processTimeoutMicroseconds / 1000000.0);

    while (header->responseCounter.load() != requestCounter)
    {
        if (juce::Time::getHighResolutionTicks() - startTime > timeoutTicks)
            return false; // タイムアウト：呼び出し側が無音などで対処する

        juce::Thread::yield();
    }

    // 処理結果を読み戻す
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.copyFrom (ch, 0, audioData + ch * SandboxIPC::maxBlockSize, numSamples);

    return true;
}

void PluginSandboxHost::openSandboxedEditor()
{
    if (! workerRunning || ! pluginLoaded.load())
        return;

    juce::ValueTree msg (SandboxIPC::MSG_OPEN_EDITOR);
    juce::MemoryOutputStream stream;
    msg.writeToStream (stream);
    sendMessageToWorker (stream.getMemoryBlock());
}

void PluginSandboxHost::closeSandboxedEditor()
{
    if (! workerRunning)
        return;

    juce::ValueTree msg (SandboxIPC::MSG_CLOSE_EDITOR);
    juce::MemoryOutputStream stream;
    msg.writeToStream (stream);
    sendMessageToWorker (stream.getMemoryBlock());
}

void PluginSandboxHost::shutdownWorker()
{
    pluginLoaded.store (false);

    if (! workerRunning)
        return;

    juce::ValueTree shutdown (SandboxIPC::MSG_SHUTDOWN);
    juce::MemoryOutputStream stream;
    shutdown.writeToStream (stream);
    sendMessageToWorker (stream.getMemoryBlock());

    killWorkerProcess();
    workerRunning = false;
}

void PluginSandboxHost::handleMessageFromWorker (const juce::MemoryBlock& message)
{
    auto tree = juce::ValueTree::readFromData (message.getData(), message.getSize());

    if (! tree.isValid() || ! tree.hasType (SandboxIPC::MSG_LOAD_RESULT))
        return;

    stopTimer();
    awaitingLoadResult = false;

    const bool success = tree.getProperty (SandboxIPC::propSuccess, false);
    const auto pluginName = tree.getProperty (SandboxIPC::propPluginName).toString();
    const auto errorMessage = tree.getProperty (SandboxIPC::propErrorMessage).toString();

    if (success)
    {
        pluginLoaded.store (true);
        const int latency = tree.getProperty (SandboxIPC::propLatencySamples, 0);
        notifyResult (true, utf8 ("サンドボックス内でのロードに成功しました: ") + pluginName
                                + utf8 ("（レイテンシ: ") + juce::String (latency) + utf8 (" サンプル）"));
    }
    else
    {
        notifyResult (false, utf8 ("サンドボックス内でのロードに失敗しました: ") + errorMessage);
    }
}

void PluginSandboxHost::handleConnectionLost()
{
    // 子プロセスが落ちた＝プラグインがクラッシュした可能性が高い。
    // ここが重要な点：この状況でもDAW本体（親プロセス）は生きている。
    workerRunning = false;
    pluginLoaded.store (false);
    stopTimer();

    if (awaitingLoadResult)
    {
        awaitingLoadResult = false;
        notifyResult (false, utf8 ("プラグインのロード中にサンドボックスプロセスがクラッシュしました。")
                                 + utf8 ("DAW本体は影響を受けていません。"));
    }
    else
    {
        notifyResult (false, utf8 ("サンドボックスプロセスが終了しました（プラグインがクラッシュした可能性があります）。")
                                 + utf8 ("DAW本体は影響を受けていません。"));
    }
}

void PluginSandboxHost::timerCallback()
{
    stopTimer();

    if (awaitingLoadResult)
    {
        awaitingLoadResult = false;
        notifyResult (false, utf8 ("サンドボックスプロセスが応答しません（タイムアウト）。"));
        shutdownWorker();
    }
}

void PluginSandboxHost::notifyResult (bool success, const juce::String& message)
{
    if (onLoadResult == nullptr)
        return;

    // 重要：このメソッドはIPC専用スレッドから呼ばれることがある。
    // コールバック先はUI（Label等）を更新するため、必ずメッセージスレッドへ移す
    // （JUCEではUI操作はメッセージスレッドから行う必要があり、
    //  そうしないとJUCE_ASSERT_MESSAGE_MANAGER_IS_LOCKEDで停止する）。
    auto callback = onLoadResult;
    juce::MessageManager::callAsync ([callback, success, message]
    {
        callback (success, message);
    });
}
