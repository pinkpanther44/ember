#include "PluginSandboxHost.h"

#include "Utf8.h"

//==============================================================================

PluginSandboxHost::PluginSandboxHost()
{
    parameterQueue.resize ((size_t) parameterFifo.getTotalSize());
    incomingQueue.resize ((size_t) incomingFifo.getTotalSize());
    scratchMidi.ensureSize (SandboxIPC::midiRingBytes);
}

PluginSandboxHost::~PluginSandboxHost()
{
    shutdownWorker();

    sharedMemory = nullptr;

    if (sharedMemoryFile.existsAsFile())
        sharedMemoryFile.deleteFile();
}

//==============================================================================

bool PluginSandboxHost::createSharedMemory()
{
    // 共有メモリはメモリマップトファイルで作ります
    // （JUCEの`MemoryMappedFile`はWindowsでもLinuxでも動き、依存が増えません）
    sharedMemory = nullptr;

    sharedMemoryFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("personaldaw_sandbox_" + juce::Uuid().toString() + ".shm");

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

    // **placement newでヘッダを作ること**（`std::atomic`はゼロ埋めでは構築されません）
    auto* headerData = new (sharedMemory->getData()) SandboxIPC::SharedAudioHeader();
    headerData->magic.store (SandboxIPC::sharedMemoryMagic);

    requestCounter = 0;
    lastSeenResponse = 0;
    hasPendingBlock = false;

    return true;
}

bool PluginSandboxHost::sendTree (const juce::ValueTree& tree)
{
    juce::MemoryOutputStream stream;
    tree.writeToStream (stream);

    return sendMessageToWorker (stream.getMemoryBlock());
}

//==============================================================================

bool PluginSandboxHost::loadPlugin (const juce::PluginDescription& description,
                                     double sampleRate, int blockSize,
                                     LoadedPluginInfo& infoOut, juce::String& errorMessage)
{
    if (! createSharedMemory())
    {
        errorMessage = utf8 ("共有メモリを用意できませんでした");
        return false;
    }

    if (! workerRunning.load())
    {
        // **自分自身のexeを子として起動します**（設計書3.4）。
        // 子は`Main.cpp`でコマンドラインを見て、ワーカーとして動きます
        const auto thisExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);

        if (! launchWorkerProcess (thisExe, SandboxIPC::commandLineUID,
                                    SandboxIPC::connectionTimeoutMs))
        {
            errorMessage = utf8 ("サンドボックスプロセスを起動できませんでした");
            return false;
        }

        workerRunning.store (true);
    }

    juce::ValueTree request (SandboxIPC::MSG_LOAD_PLUGIN);

    if (auto xml = description.createXml())
        request.setProperty (SandboxIPC::propPluginDescription, xml->toString(), nullptr);

    request.setProperty (SandboxIPC::propSampleRate, sampleRate, nullptr);
    request.setProperty (SandboxIPC::propBlockSize, blockSize, nullptr);
    request.setProperty (SandboxIPC::propSharedMemoryName, sharedMemoryFile.getFullPathName(), nullptr);

    loadResultArrived.reset();

    if (! sendTree (request))
    {
        errorMessage = utf8 ("サンドボックスプロセスへ指示を送れませんでした");
        return false;
    }

    // **返事を待ちます。** メッセージスレッドを止めますが、
    // IPCのコールバックは別スレッドで届くので詰まりません（`SandboxIPC.h`）
    if (! loadResultArrived.wait (SandboxIPC::loadTimeoutMs))
    {
        errorMessage = utf8 ("サンドボックスプロセスが応答しません（タイムアウト）");
        shutdownWorker();
        return false;
    }

    juce::ValueTree result;

    {
        const juce::ScopedLock lock (replyLock);
        result = lastLoadResult;
    }

    if (! result.isValid() || ! (bool) result.getProperty (SandboxIPC::propSuccess, false))
    {
        errorMessage = result.getProperty (SandboxIPC::propErrorMessage).toString();

        if (errorMessage.isEmpty())
            errorMessage = utf8 ("サンドボックス内でのロードに失敗しました");

        return false;
    }

    infoOut = {};
    infoOut.name = result.getProperty (SandboxIPC::propPluginName).toString();
    infoOut.latencySamples = result.getProperty (SandboxIPC::propLatencySamples, 0);
    infoOut.acceptsMidi = result.getProperty (SandboxIPC::propAcceptsMidi, false);
    infoOut.producesMidi = result.getProperty (SandboxIPC::propProducesMidi, false);
    infoOut.hasEditor = result.getProperty (SandboxIPC::propHasEditor, false);
    infoOut.tailSeconds = result.getProperty (SandboxIPC::propTailSeconds, 0.0);

    if (auto parameters = result.getChildWithName (SandboxIPC::treeParameters); parameters.isValid())
    {
        for (const auto& parameter : parameters)
        {
            LoadedPluginInfo::Parameter entry;

            entry.id = parameter.getProperty (SandboxIPC::propParamId).toString();
            entry.name = parameter.getProperty (SandboxIPC::propParamName).toString();
            entry.label = parameter.getProperty (SandboxIPC::propParamLabel).toString();
            entry.defaultValue = (float) (double) parameter.getProperty (SandboxIPC::propParamDefault, 0.0);
            entry.currentValue = (float) (double) parameter.getProperty (SandboxIPC::propParamValue, 0.0);
            entry.numSteps = parameter.getProperty (SandboxIPC::propParamSteps, 0);
            entry.isDiscrete = parameter.getProperty (SandboxIPC::propParamDiscrete, false);

            infoOut.parameters.push_back (entry);
        }
    }

    pluginLatency.store (infoOut.latencySamples);
    pluginLoaded.store (true);

    // 1ブロック遅らせるための置き場（**音のスレッドで確保しないため、ここで**）
    pendingOutput.setSize (SandboxIPC::maxChannels, SandboxIPC::maxBlockSize, false, true, true);
    hasPendingBlock = false;

    // **落ちたことに早く気づくため**の見張り（`timerCallback()`の説明）
    startTimer (500);

    return true;
}

void PluginSandboxHost::timerCallback()
{
    probeWorker();
}

juce::uint32 PluginSandboxHost::getMidiDropCount() const
{
    if (sharedMemory == nullptr || sharedMemory->getData() == nullptr)
        return 0;

    auto* head = SandboxIPC::header (sharedMemory->getData());

    return head->midiToWorker.dropped.load() + head->midiFromWorker.dropped.load();
}

bool PluginSandboxHost::probeWorker()
{
    if (! workerRunning.load())
        return false;

    // 空のメッセージでも、**送れたかどうか**は分かります
    if (sendTree (juce::ValueTree (SandboxIPC::MSG_PING)))
        return true;

    handleConnectionLost();

    return false;
}

void PluginSandboxHost::prepare (double sampleRate, int blockSize)
{
    if (! isAlive())
        return;

    juce::ValueTree request (SandboxIPC::MSG_PREPARE);
    request.setProperty (SandboxIPC::propSampleRate, sampleRate, nullptr);
    request.setProperty (SandboxIPC::propBlockSize, blockSize, nullptr);

    prepareDoneArrived.reset();

    if (! sendTree (request))
        return;

    // **用意ができるまで待つこと。** 待たずに音を渡すと、子は古いレートのまま処理します
    if (prepareDoneArrived.wait (SandboxIPC::prepareTimeoutMs))
        hasPendingBlock = false;
}

void PluginSandboxHost::release()
{
    if (! workerRunning.load())
        return;

    sendTree (juce::ValueTree (SandboxIPC::MSG_RELEASE));
    hasPendingBlock = false;
}

void PluginSandboxHost::shutdownWorker()
{
    pluginLoaded.store (false);

    if (! workerRunning.load())
        return;

    sendTree (juce::ValueTree (SandboxIPC::MSG_SHUTDOWN));

    killWorkerProcess();
    workerRunning.store (false);
}

//==============================================================================

void PluginSandboxHost::queueParameterChange (int index, float value)
{
    const auto scope = parameterFifo.write (1);

    if (scope.blockSize1 > 0)
        parameterQueue[(size_t) scope.startIndex1] = { index, value };
    else if (scope.blockSize2 > 0)
        parameterQueue[(size_t) scope.startIndex2] = { index, value };
}

int PluginSandboxHost::readParameterChanges (SandboxIPC::ParamChange* destination, int capacity)
{
    const int available = juce::jmin (capacity, incomingFifo.getNumReady());

    if (available <= 0)
        return 0;

    const auto scope = incomingFifo.read (available);

    int written = 0;

    for (int i = 0; i < scope.blockSize1; ++i)
        destination[written++] = incomingQueue[(size_t) (scope.startIndex1 + i)];

    for (int i = 0; i < scope.blockSize2; ++i)
        destination[written++] = incomingQueue[(size_t) (scope.startIndex2 + i)];

    return written;
}

//==============================================================================

bool PluginSandboxHost::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                                       const juce::AudioPlayHead::PositionInfo* position,
                                       bool replaceMidi)
{
    if (! isAlive() || sharedMemory == nullptr || sharedMemory->getData() == nullptr)
        return false;

    auto* base = sharedMemory->getData();
    auto* head = SandboxIPC::header (base);

    if (! head->workerReady.load())
        return false;

    const int numChannels = juce::jlimit (1, SandboxIPC::maxChannels, buffer.getNumChannels());
    const int numSamples = juce::jlimit (1, SandboxIPC::maxBlockSize, buffer.getNumSamples());

    //--------------------------------------------------------------------------
    // ① **前のブロックの結果**を取り出す（`SandboxIPC.h`の「1ブロック遅らせて」）。
    //
    // 子にはまるまる1ブロックぶんの時間があるので、普通はもう終わっています。
    // **それでも保険の待ちは要ります**——重いプラグインは1ブロックを超えることがあり、
    // そこで待たずに諦めると、**遅れた回数だけ音が欠けます**
    bool haveOutput = false;

    if (hasPendingBlock)
    {
        const int slot = SandboxIPC::slotFor (requestCounter);

        const auto startTicks = juce::Time::getHighResolutionTicks();
        const auto timeoutTicks = (juce::int64) (juce::Time::getHighResolutionTicksPerSecond() * 0.002);

        while (head->responseCounter.load() != requestCounter)
        {
            if (juce::Time::getHighResolutionTicks() - startTicks > timeoutTicks)
                break;

            juce::Thread::yield();
        }

        if (head->responseCounter.load() == requestCounter)
        {
            auto* audio = SandboxIPC::audioData (base, slot);

            const int pendingSamples = juce::jmin (head->numSamples[slot].load(),
                                                    pendingOutput.getNumSamples());
            const int pendingChannels = juce::jmin (head->numChannels[slot].load(),
                                                     pendingOutput.getNumChannels());

            for (int channel = 0; channel < pendingChannels; ++channel)
                pendingOutput.copyFrom (channel, 0, audio + channel * SandboxIPC::maxBlockSize,
                                         pendingSamples);


            // 子のGUIで動いたパラメータ
            const int changes = juce::jlimit (0, SandboxIPC::maxParamChanges,
                                               head->paramOutCount[slot].load());

            if (changes > 0)
            {
                auto* incoming = SandboxIPC::paramOut (base, slot);
                const auto scope = incomingFifo.write (changes);

                for (int i = 0; i < scope.blockSize1; ++i)
                    incomingQueue[(size_t) (scope.startIndex1 + i)] = incoming[i];

                for (int i = 0; i < scope.blockSize2; ++i)
                    incomingQueue[(size_t) (scope.startIndex2 + i)] = incoming[scope.blockSize1 + i];
            }

            pluginLatency.store (head->latencySamples.load());
            haveOutput = (pendingSamples == numSamples && pendingChannels >= numChannels);
        }
        else
        {
            // **間に合わなかったぶん**。数えておいて、上へ出せるようにします
            ++dropouts;
        }
    }

    //--------------------------------------------------------------------------
    // ② 今回の入力を渡して、処理を頼む。**次の置き場へ書きます**（交互。`SandboxIPC.h`）
    const int nextCounter = requestCounter + 1;
    const int nextSlot = SandboxIPC::slotFor (nextCounter);

    {
        auto* audio = SandboxIPC::audioData (base, nextSlot);

        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::copy (audio + channel * SandboxIPC::maxBlockSize,
                                                buffer.getReadPointer (channel), numSamples);
    }

    // 8.263：**MIDIは行列へ**（置き場には載せません）。
    // 子が何ブロック遅れても、ここに入れたぶんは消えません
    SandboxIPC::pushMidi (head->midiToWorker, SandboxIPC::midiToWorkerBuffer (base), midi);

    {
        // 溜まっているパラメータ変化を詰める
        const int ready = juce::jmin (SandboxIPC::maxParamChanges, parameterFifo.getNumReady());
        auto* outgoing = SandboxIPC::paramIn (base, nextSlot);

        if (ready > 0)
        {
            const auto scope = parameterFifo.read (ready);
            int written = 0;

            for (int i = 0; i < scope.blockSize1; ++i)
                outgoing[written++] = parameterQueue[(size_t) (scope.startIndex1 + i)];

            for (int i = 0; i < scope.blockSize2; ++i)
                outgoing[written++] = parameterQueue[(size_t) (scope.startIndex2 + i)];
        }

        head->paramInCount[nextSlot].store (ready);
    }

    if (position != nullptr)
    {
        // 8.206：**テンポと再生位置**（渡さないと、サンドボックスに入れた途端に同期が外れます）
        head->playHeadValid.store (true);
        head->isPlaying.store (position->getIsPlaying());
        head->bpm.store (position->getBpm().orFallback (120.0));
        head->ppqPosition.store (position->getPpqPosition().orFallback (0.0));
        head->timeSeconds.store (position->getTimeInSeconds().orFallback (0.0));
        head->timeInSamples.store (position->getTimeInSamples().orFallback (0));

        const auto signature = position->getTimeSignature();
        head->timeSigNumerator.store (signature.hasValue() ? signature->numerator : 4);
        head->timeSigDenominator.store (signature.hasValue() ? signature->denominator : 4);
    }
    else
    {
        head->playHeadValid.store (false);
    }

    head->numChannels[nextSlot].store (numChannels);
    head->numSamples[nextSlot].store (numSamples);

    requestCounter = nextCounter;
    head->requestCounter.store (requestCounter);
    hasPendingBlock = true;

    //--------------------------------------------------------------------------
    // 8.263：子が出したMIDI（アルペジエーターなど）を行列から引き取ります。
    //
    // **間に合わなかったブロックでも引き取ること。** 引き取らないと行列に溜まり、
    // こちらでもノートオフを落とすことになります
    scratchMidi.clear();
    SandboxIPC::popMidi (head->midiFromWorker, SandboxIPC::midiFromWorkerBuffer (base),
                          scratchMidi, numSamples);

    //--------------------------------------------------------------------------
    // ③ 出口。**最初の1ブロックは無音**です（それが「1ブロック遅れ」の中身）
    if (haveOutput)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom (channel, 0,
                              pendingOutput.getReadPointer (juce::jmin (channel, numChannels - 1)),
                              numSamples);

    }

    // **置き換えは、間に合わなかったブロックでも行うこと。**
    // `haveOutput`の中に入れていると、そのブロックのぶんだけMIDIが消えます
    if (replaceMidi)
    {
        midi.swapWith (scratchMidi);
        scratchMidi.clear();
    }

    // **間に合わなかったときは、入ってきた音をそのまま残します**（素通し）。
    //
    // 無音にすると、遅れるたびに**プツッと切れます**。素通しなら
    // 「一瞬だけエフェクトが掛からない」で済みます——音楽的にはそちらが軽い。
    // いちばん最初の1ブロックも、ここを通ります（＝1ブロック遅れの中身）

    return true;
}

//==============================================================================

juce::MemoryBlock PluginSandboxHost::getPluginState()
{
    if (isAlive())
    {
        stateArrived.reset();

        if (sendTree (juce::ValueTree (SandboxIPC::MSG_GET_STATE)))
            stateArrived.wait (SandboxIPC::stateTimeoutMs);
    }

    // **返事が来なくても、最後に受け取ったものを返します**（空よりはまし）
    const juce::ScopedLock lock (replyLock);

    return lastState;
}

void PluginSandboxHost::setPluginState (const juce::MemoryBlock& state)
{
    {
        const juce::ScopedLock lock (replyLock);
        lastState = state;
    }

    if (! isAlive() || state.getSize() == 0)
        return;

    juce::ValueTree message (SandboxIPC::MSG_SET_STATE);
    message.setProperty (SandboxIPC::propStateData, juce::var (state), nullptr);

    sendTree (message);
}

//==============================================================================


bool PluginSandboxHost::requestEditor (bool wantEmbedding)
{
    if (! isAlive())
        return false;

    // **返事は待ちません**（クラスの説明の「ここで返事を待ってはいけません」）
    juce::ValueTree message (SandboxIPC::MSG_OPEN_EDITOR);
    message.setProperty (SandboxIPC::propWantEmbedding, wantEmbedding, nullptr);

    return sendTree (message);
}

void PluginSandboxHost::closeEditor()
{
    if (workerRunning.load())
        sendTree (juce::ValueTree (SandboxIPC::MSG_CLOSE_EDITOR));
}

void PluginSandboxHost::setEditorSize (int width, int height)
{
    if (! isAlive() || width <= 0 || height <= 0)
        return;

    juce::ValueTree message (SandboxIPC::MSG_SET_EDITOR_SIZE);
    message.setProperty (SandboxIPC::propEditorWidth, width, nullptr);
    message.setProperty (SandboxIPC::propEditorHeight, height, nullptr);

    sendTree (message);
}

//==============================================================================

void PluginSandboxHost::handleMessageFromWorker (const juce::MemoryBlock& message)
{
    auto tree = juce::ValueTree::readFromData (message.getData(), message.getSize());

    if (! tree.isValid())
        return;

    if (tree.hasType (SandboxIPC::MSG_LOAD_RESULT))
    {
        {
            const juce::ScopedLock lock (replyLock);
            lastLoadResult = tree;
        }

        loadResultArrived.signal();
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_PREPARE_DONE))
    {
        prepareDoneArrived.signal();
        return;
    }

    // **3つともIPCのスレッドで届きます。** 画面を触るので、向こうへ回します
    if (tree.hasType (SandboxIPC::MSG_EDITOR_OPENED))
    {
        const bool success = tree.getProperty (SandboxIPC::propSuccess, false);

        EditorInfo info;

        // **`void*`はそのまま送れません**（`var`は64bit整数まで）。整数で渡して戻します
        const auto handle = (juce::int64) tree.getProperty (SandboxIPC::propEditorHandle,
                                                              (juce::int64) 0);

        info.nativeHandle = reinterpret_cast<void*> ((juce::pointer_sized_int) handle);
        info.width = tree.getProperty (SandboxIPC::propEditorWidth, 0);
        info.height = tree.getProperty (SandboxIPC::propEditorHeight, 0);
        info.resizable = tree.getProperty (SandboxIPC::propEditorResizable, false);

        juce::MessageManager::callAsync ([safe = juce::WeakReference<PluginSandboxHost> (this),
                                           success, info]
        {
            if (safe != nullptr && safe->onEditorOpened != nullptr)
                safe->onEditorOpened (success, info);
        });

        return;
    }


    if (tree.hasType (SandboxIPC::MSG_EDITOR_RESIZED))
    {
        const int width = tree.getProperty (SandboxIPC::propEditorWidth, 0);
        const int height = tree.getProperty (SandboxIPC::propEditorHeight, 0);

        juce::MessageManager::callAsync ([safe = juce::WeakReference<PluginSandboxHost> (this),
                                           width, height]
        {
            if (safe != nullptr && safe->onEditorResized != nullptr)
                safe->onEditorResized (width, height);
        });

        return;
    }

    if (tree.hasType (SandboxIPC::MSG_EDITOR_CLOSED))
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<PluginSandboxHost> (this)]
        {
            if (safe != nullptr && safe->onEditorClosed != nullptr)
                safe->onEditorClosed();
        });

        return;
    }

    if (tree.hasType (SandboxIPC::MSG_STATE))
    {
        if (auto* data = tree.getProperty (SandboxIPC::propStateData).getBinaryData())
        {
            const juce::ScopedLock lock (replyLock);
            lastState = *data;
        }

        stateArrived.signal();
        return;
    }
}

void PluginSandboxHost::handleConnectionLost()
{
    // **ここが肝心なところ**：子が落ちても、親（DAW本体）は生きています（仕様書5.8.1）
    workerRunning.store (false);
    pluginLoaded.store (false);

    // **タイマーはメッセージスレッドからしか止められません。**
    // ここはIPCのスレッドのことがあるので、止めるのは向こうへ回します
    juce::MessageManager::callAsync ([safe = juce::WeakReference<PluginSandboxHost> (this)]
    {
        if (safe != nullptr)
            safe->stopTimer();
    });

    // 待っている人を起こす（そうしないとタイムアウトまで止まります）
    loadResultArrived.signal();
    prepareDoneArrived.signal();
    stateArrived.signal();

    if (onConnectionLost != nullptr)
        onConnectionLost();
}
