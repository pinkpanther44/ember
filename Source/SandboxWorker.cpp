#include "SandboxWorker.h"

#include "Plugins/MantaPluginFormat.h"

//==============================================================================
/**
    共有メモリに置かれた再生位置を、プラグインへ渡すための入れ物。

    8.206：**渡さないと、テンポ同期のプラグインがサンドボックスの中でだけ同期しません。**
*/
class SandboxWorker::SharedPlayHead final : public juce::AudioPlayHead
{
public:
    void setSharedMemory (void* base) { sharedBase = base; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (sharedBase == nullptr)
            return juce::nullopt;

        auto* head = SandboxIPC::header (sharedBase);

        if (! head->playHeadValid.load())
            return juce::nullopt;

        PositionInfo info;

        info.setIsPlaying (head->isPlaying.load());
        info.setIsRecording (false);
        info.setIsLooping (false);
        info.setBpm (head->bpm.load());
        info.setPpqPosition (head->ppqPosition.load());
        info.setTimeInSeconds (head->timeSeconds.load());
        info.setTimeInSamples (head->timeInSamples.load());
        info.setTimeSignature (TimeSignature { head->timeSigNumerator.load(),
                                                head->timeSigDenominator.load() });

        return info;
    }

private:
    void* sharedBase = nullptr;
};

//==============================================================================
// GUI（Phase 269／8.262）

/**
    プラグイン本来のエディタを入れる枠。

    **はめ込むときは、これ自身が窓になります**（`addToDesktop (0)`＝縁も題字も無い窓）。
    親はこの窓のハンドルを受け取って、自分の窓の子にします。

    別窓のときは、`SandboxEditorWindow`の中身になります。
*/
class SandboxWorker::EditorHolder final : public juce::Component
{
public:
    EditorHolder (juce::AudioProcessorEditor& editorToUse, std::function<void (int, int)> onResizeIn)
        : editor (editorToUse), onResize (std::move (onResizeIn))
    {
        setOpaque (true);
        addAndMakeVisible (editor);
        setSize (editor.getWidth(), editor.getHeight());
    }

    ~EditorHolder() override
    {
        removeChildComponent (&editor);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

    void resized() override
    {
        // **大きさはエディタが決めます**（本体の`PluginEditorContent`と同じ約束）。
        // ここで`setBounds`で潰すと、大きさを固定したエディタが歪みます
        editor.setTopLeftPosition (0, 0);
    }

    void childBoundsChanged (juce::Component* child) override
    {
        if (child != &editor)
            return;

        if (getWidth() == editor.getWidth() && getHeight() == editor.getHeight())
            return;   // **同じなら何もしない**（呼び戻しが止まらなくなる）

        setSize (editor.getWidth(), editor.getHeight());

        if (onResize != nullptr)
            onResize (editor.getWidth(), editor.getHeight());
    }

    juce::AudioProcessorEditor& editor;

private:
    std::function<void (int, int)> onResize;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorHolder)
};

//==============================================================================

SandboxWorker::SandboxWorker()
    : juce::Thread ("SandboxAudio")
{
    juce::addDefaultFormatsToManager (formatManager);

    // 8.260：**内蔵プラグインもここへ載せます**（Phase 268）。
    //
    // 内蔵のものをサンドボックスへ回す理由は普段ありませんが、
    // **確かめるときに要ります**——`--sandbox-selftest`が、
    // 手元に必ずある内蔵プラグインで往復を測れるようになります
    // （外のVST3に頼ると、その環境にそれが無いと試せません）
    formatManager.addFormat (new MantaPluginFormat());

    playHead = std::make_unique<SharedPlayHead>();
}

SandboxWorker::~SandboxWorker()
{
    signalThreadShouldExit();
    notify();               // `wait()`中なら起こして、速やかに終わらせる
    stopThread (2000);
    cancelPendingUpdate();

    // **ウィンドウを先に閉じてから**プラグイン本体を壊すこと（1.5）
    closeEditorWindow();

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

//==============================================================================

void SandboxWorker::sendTree (const juce::ValueTree& tree)
{
    juce::MemoryOutputStream stream;
    tree.writeToStream (stream);
    sendMessageToCoordinator (stream.getMemoryBlock());
}

void SandboxWorker::handleMessageFromCoordinator (const juce::MemoryBlock& message)
{
    auto tree = juce::ValueTree::readFromData (message.getData(), message.getSize());

    if (! tree.isValid())
        return;

    if (tree.hasType (SandboxIPC::MSG_SHUTDOWN))   { juce::JUCEApplication::quit(); return; }
    if (tree.hasType (SandboxIPC::MSG_LOAD_PLUGIN)) { loadPlugin (tree); return; }
    if (tree.hasType (SandboxIPC::MSG_PREPARE))     { prepare (tree); return; }
    if (tree.hasType (SandboxIPC::MSG_RELEASE))     { releasePlugin(); return; }
    if (tree.hasType (SandboxIPC::MSG_GET_STATE))   { sendState(); return; }
    if (tree.hasType (SandboxIPC::MSG_SET_STATE))   { applyState (tree); return; }
    // 8.262：**GUIの3つだけは、メッセージスレッドへ渡し直します**（`SandboxIPC.h`）。
    //
    // ここはIPCの専用スレッドです。**そこで窓を作ると、JUCEのGUIを
    // 別スレッドから触る**ことになります——Phase 5b-3で3度失敗した
    // 「ヒープ不整合」（7.5・4.3）の、いちばん確からしい原因がこれでした
    if (tree.hasType (SandboxIPC::MSG_OPEN_EDITOR))
    {
        const bool wantEmbedding = tree.getProperty (SandboxIPC::propWantEmbedding, false);

        juce::MessageManager::callAsync ([safe = juce::WeakReference<SandboxWorker> (this), wantEmbedding]
        {
            if (safe != nullptr)
                safe->openEditorWindow (wantEmbedding);
        });
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_CLOSE_EDITOR))
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<SandboxWorker> (this)]
        {
            if (safe != nullptr)
                safe->closeEditorWindow();
        });
        return;
    }

    if (tree.hasType (SandboxIPC::MSG_SET_EDITOR_SIZE))
    {
        const int width = tree.getProperty (SandboxIPC::propEditorWidth, 0);
        const int height = tree.getProperty (SandboxIPC::propEditorHeight, 0);

        juce::MessageManager::callAsync ([safe = juce::WeakReference<SandboxWorker> (this), width, height]
        {
            if (safe != nullptr)
                safe->resizeEditor (width, height);
        });
        return;
    }
}

//==============================================================================

void SandboxWorker::loadPlugin (const juce::ValueTree& request)
{
    const auto xmlString = request.getProperty (SandboxIPC::propPluginDescription).toString();
    currentSampleRate = request.getProperty (SandboxIPC::propSampleRate, 44100.0);
    currentBlockSize = request.getProperty (SandboxIPC::propBlockSize, 512);

    const auto sharedMemoryPath = request.getProperty (SandboxIPC::propSharedMemoryName).toString();

    juce::PluginDescription description;

    if (auto xml = juce::XmlDocument::parse (xmlString))
        description.loadFromXml (*xml);
    else
    {
        sendResult (false, "PluginDescription could not be parsed");
        return;
    }

    juce::String errorMessage;

    // **ここが本番**。読んでいる途中で落ちても、落ちるのはこの子プロセスだけ
    auto newInstance = formatManager.createPluginInstance (description, currentSampleRate,
                                                            currentBlockSize, errorMessage);

    if (newInstance == nullptr)
    {
        sendResult (false, errorMessage.isNotEmpty() ? errorMessage : "unknown error");
        return;
    }

    newInstance->setPlayHead (playHead.get());
    newInstance->prepareToPlay (currentSampleRate, currentBlockSize);

    {
        const juce::ScopedLock lock (pluginLock);
        pluginInstance = std::move (newInstance);
    }

    // 共有メモリを開いて、処理ループを始める
    if (sharedMemoryPath.isNotEmpty())
    {
        sharedMemoryFile = juce::File (sharedMemoryPath);
        sharedMemory = std::make_unique<juce::MemoryMappedFile> (
            sharedMemoryFile, juce::MemoryMappedFile::readWrite);

        if (sharedMemory->getData() != nullptr)
        {
            auto* head = SandboxIPC::header (sharedMemory->getData());

            if (head->magic.load() != SandboxIPC::sharedMemoryMagic)
            {
                sendResult (false, "shared memory header mismatch");
                return;
            }

            playHead->setSharedMemory (sharedMemory->getData());

            lastHandledRequest = head->requestCounter.load();
            head->latencySamples.store (pluginInstance->getLatencySamples());
            head->workerReady.store (true);

            // **`high`まで**（`highest`はメッセージスレッドを圧迫し、
            // pingに応えられなくなります。クラスの説明）
            startThread (juce::Thread::Priority::high);
        }
    }

    sendResult (true, {});
}

void SandboxWorker::prepare (const juce::ValueTree& request)
{
    currentSampleRate = request.getProperty (SandboxIPC::propSampleRate, currentSampleRate);
    currentBlockSize = request.getProperty (SandboxIPC::propBlockSize, currentBlockSize);

    {
        const juce::ScopedLock lock (pluginLock);

        if (pluginInstance != nullptr)
        {
            pluginInstance->releaseResources();
            pluginInstance->prepareToPlay (currentSampleRate, currentBlockSize);

            if (sharedMemory != nullptr && sharedMemory->getData() != nullptr)
                SandboxIPC::header (sharedMemory->getData())
                    ->latencySamples.store (pluginInstance->getLatencySamples());
        }
    }

    sendTree (juce::ValueTree (SandboxIPC::MSG_PREPARE_DONE));
}

void SandboxWorker::releasePlugin()
{
    const juce::ScopedLock lock (pluginLock);

    if (pluginInstance != nullptr)
        pluginInstance->releaseResources();
}

//==============================================================================

void SandboxWorker::sendState()
{
    juce::MemoryBlock state;

    {
        const juce::ScopedLock lock (pluginLock);

        if (pluginInstance != nullptr)
            pluginInstance->getStateInformation (state);
    }

    juce::ValueTree message (SandboxIPC::MSG_STATE);
    message.setProperty (SandboxIPC::propStateData, juce::var (state), nullptr);

    sendTree (message);
}

void SandboxWorker::applyState (const juce::ValueTree& request)
{
    auto* data = request.getProperty (SandboxIPC::propStateData).getBinaryData();

    if (data == nullptr || data->getSize() == 0)
        return;

    const juce::ScopedLock lock (pluginLock);

    if (pluginInstance != nullptr)
        pluginInstance->setStateInformation (data->getData(), (int) data->getSize());
}

//==============================================================================

void SandboxWorker::sendResult (bool success, const juce::String& errorMessage)
{
    juce::ValueTree result (SandboxIPC::MSG_LOAD_RESULT);

    result.setProperty (SandboxIPC::propSuccess, success, nullptr);
    result.setProperty (SandboxIPC::propErrorMessage, errorMessage, nullptr);

    const juce::ScopedLock lock (pluginLock);

    if (success && pluginInstance != nullptr)
    {
        result.setProperty (SandboxIPC::propPluginName, pluginInstance->getName(), nullptr);
        result.setProperty (SandboxIPC::propLatencySamples, pluginInstance->getLatencySamples(), nullptr);
        result.setProperty (SandboxIPC::propAcceptsMidi, pluginInstance->acceptsMidi(), nullptr);
        result.setProperty (SandboxIPC::propProducesMidi, pluginInstance->producesMidi(), nullptr);
        result.setProperty (SandboxIPC::propTailSeconds, pluginInstance->getTailLengthSeconds(), nullptr);
        result.setProperty (SandboxIPC::propHasEditor, pluginInstance->hasEditor(), nullptr);

        // **パラメータの表**（親が同じ数だけ立てます。`SandboxedPluginProcessor.h`）
        juce::ValueTree parameters (SandboxIPC::treeParameters);

        const auto& hosted = pluginInstance->getParameters();
        lastParameterValues.assign ((size_t) hosted.size(), 0.0f);

        for (int i = 0; i < hosted.size(); ++i)
        {
            auto* parameter = hosted[i];

            juce::ValueTree entry (SandboxIPC::treeParameter);

            // **IDがあるならIDで覚えます**（番号はプラグインの版で動きます）
            const auto parameterId =
                dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (parameter) != nullptr
                    ? dynamic_cast<juce::AudioPluginInstance::HostedParameter*> (parameter)->getParameterID()
                    : juce::String (i);

            entry.setProperty (SandboxIPC::propParamId, parameterId, nullptr);
            entry.setProperty (SandboxIPC::propParamName, parameter->getName (128), nullptr);
            entry.setProperty (SandboxIPC::propParamLabel, parameter->getLabel(), nullptr);
            entry.setProperty (SandboxIPC::propParamDefault, (double) parameter->getDefaultValue(), nullptr);
            entry.setProperty (SandboxIPC::propParamValue, (double) parameter->getValue(), nullptr);
            entry.setProperty (SandboxIPC::propParamSteps, parameter->getNumSteps(), nullptr);
            entry.setProperty (SandboxIPC::propParamDiscrete, parameter->isDiscrete(), nullptr);

            parameters.appendChild (entry, nullptr);
            lastParameterValues[(size_t) i] = parameter->getValue();
        }

        result.appendChild (parameters, nullptr);
    }

    sendTree (result);
}

//==============================================================================

void SandboxWorker::applyParameterChanges (int count, int slot)
{
    if (pluginInstance == nullptr || count <= 0)
        return;

    auto* changes = SandboxIPC::paramIn (sharedMemory->getData(), slot);
    const auto& hosted = pluginInstance->getParameters();

    for (int i = 0; i < count; ++i)
    {
        const auto& change = changes[i];

        if (! juce::isPositiveAndBelow (change.index, hosted.size()))
            continue;

        // 音のスレッドでは**`setValue()`まで**（`setValueNotifyingHost()`は呼ばない）
        hosted[change.index]->setValue (change.value);

        if (juce::isPositiveAndBelow (change.index, (int) lastParameterValues.size()))
            lastParameterValues[(size_t) change.index] = change.value;
    }

    // **「変わったよ」はメッセージスレッドから**（`ParameterNotifier`の説明）。
    // これを飛ばすと、JUCE製プラグインで**音は変わるのに保存すると戻る**が起きます
    parameterNotifier.notifySoon();
}

void SandboxWorker::ParameterNotifier::handleAsyncUpdate()
{
    const juce::ScopedLock lock (owner.pluginLock);

    if (owner.pluginInstance == nullptr)
        return;

    const auto& hosted = owner.pluginInstance->getParameters();

    owner.notifiedValues.resize ((size_t) hosted.size(), -1.0f);

    for (int i = 0; i < hosted.size(); ++i)
    {
        const float current = hosted[i]->getValue();

        if (std::abs (current - owner.notifiedValues[(size_t) i]) < 1.0e-6f)
            continue;

        owner.notifiedValues[(size_t) i] = current;
        hosted[i]->sendValueChangedMessageToListeners (current);
    }
}

void SandboxWorker::collectParameterChanges (int slot)
{
    if (pluginInstance == nullptr || sharedMemory == nullptr)
        return;

    auto* head = SandboxIPC::header (sharedMemory->getData());
    auto* outgoing = SandboxIPC::paramOut (sharedMemory->getData(), slot);

    const auto& hosted = pluginInstance->getParameters();
    int count = 0;

    for (int i = 0; i < hosted.size() && count < SandboxIPC::maxParamChanges; ++i)
    {
        if (! juce::isPositiveAndBelow (i, (int) lastParameterValues.size()))
            break;

        const float current = hosted[i]->getValue();

        // **変わったぶんだけ**（毎ブロック全部返すと、512個の枠がすぐ埋まります）
        if (std::abs (current - lastParameterValues[(size_t) i]) < 1.0e-6f)
            continue;

        lastParameterValues[(size_t) i] = current;
        outgoing[count++] = { i, current };
    }

    head->paramOutCount[slot].store (count);
}

//==============================================================================

void SandboxWorker::run()
{
    auto* base = sharedMemory->getData();
    auto* head = SandboxIPC::header (base);

    juce::AudioBuffer<float> buffer (SandboxIPC::maxChannels, SandboxIPC::maxBlockSize);
    juce::MidiBuffer midiBuffer;
    midiBuffer.ensureSize (SandboxIPC::midiRingBytes);

    // 待ち方（クラスの説明）：**短くスピンして低遅延を稼ぎ、
    // それでも来なければ`wait(1)`で確実にCPUを手放す**
    constexpr int spinCountBeforeWaiting = 200;
    int spinCount = 0;

    while (! threadShouldExit())
    {
        const int currentRequest = head->requestCounter.load();

        if (currentRequest == lastHandledRequest)
        {
            if (++spinCount < spinCountBeforeWaiting)
                juce::Thread::yield();
            else
                wait (1);

            continue;
        }

        spinCount = 0;

        // **遅れたぶんは追いかけません**（`currentRequest`は"いちばん新しい要求"）。
        //
        // 置き場は2つなので、親が2つ先へ進んだ時点で**古い入力は上書き済み**です。
        // そこを順番に処理しようとしても、もう中身がありません。
        //
        // つまり**間に合わなかったブロックの音は捨てられます**。
        // 親はそのブロックを素通しで出し（`PluginSandboxHost::processBlock()`）、
        // ここは最新から再開します。**プラグインの内部状態には、そのぶんの穴が空きます**
        // ——リバーブなら尾が一瞬途切れます。音が飛ぶより軽い、という選択です。
        const int slot = SandboxIPC::slotFor (currentRequest);
        auto* audio = SandboxIPC::audioData (base, slot);

        const int numChannels = juce::jlimit (1, SandboxIPC::maxChannels,
                                               head->numChannels[slot].load());
        const int numSamples = juce::jlimit (1, SandboxIPC::maxBlockSize,
                                              head->numSamples[slot].load());

        buffer.setSize (numChannels, numSamples, false, false, true);

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.copyFrom (channel, 0, audio + channel * SandboxIPC::maxBlockSize, numSamples);

        // 8.263：**MIDIは行列から**（置き場と違って、飛ばされた番号のぶんも残っています）
        midiBuffer.clear();
        SandboxIPC::popMidi (head->midiToWorker, SandboxIPC::midiToWorkerBuffer (base),
                              midiBuffer, numSamples);

        {
            const juce::ScopedLock instanceLock (pluginLock);

            if (pluginInstance != nullptr)
            {
                // **プラグインのコールバックロックを取ってから呼ぶこと。**
                // GUI（メッセージスレッド）からのパラメータ変更と処理が同時に走ると、
                // それを想定していないVST3が落ちます（実際に起きて、直したところ）
                const juce::ScopedLock callbackLock (pluginInstance->getCallbackLock());

                applyParameterChanges (juce::jlimit (0, SandboxIPC::maxParamChanges,
                                                      head->paramInCount[slot].load()), slot);

                pluginInstance->processBlock (buffer, midiBuffer);

                collectParameterChanges (slot);
                head->latencySamples.store (pluginInstance->getLatencySamples());
            }
        }

        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::copy (audio + channel * SandboxIPC::maxBlockSize,
                                                buffer.getReadPointer (channel), numSamples);

        // プラグインが出したMIDIも行列で返します（8.263）。
        // **`processBlock()`が中身を置き換えている**ので、そのまま送れます
        SandboxIPC::pushMidi (head->midiFromWorker, SandboxIPC::midiFromWorkerBuffer (base),
                               midiBuffer);

        lastHandledRequest = currentRequest;
        head->responseCounter.store (currentRequest);   // 終わったことを親へ
    }
}

//==============================================================================

namespace
{
    /** はめ込めないとき（Linux等）に、子プロセス側が自分で出す窓。 */
    class SandboxEditorWindow final : public juce::DocumentWindow
    {
    public:
        SandboxEditorWindow (const juce::String& name, std::function<void()> onCloseIn)
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

void SandboxWorker::openEditorWindow (bool wantEmbedding)
{
    // **ここはメッセージスレッドです**（`handleMessageFromCoordinator()`が回しました）
    JUCE_ASSERT_MESSAGE_THREAD

    if (pluginInstance == nullptr || ! pluginInstance->hasEditor())
    {
        sendEditorOpened (false, nullptr, 0, 0, false);
        return;
    }

    // 既に開いているなら、作り直さずそのまま返します
    if (editorHolder != nullptr)
    {
        if (editorWindow != nullptr)
            editorWindow->toFront (true);

        sendEditorOpened (true, embeddedHandle, editorHolder->getWidth(), editorHolder->getHeight(),
                           editorHolder->editor.isResizable());
        return;
    }

    auto* editor = pluginInstance->createEditorAndMakeActive();

    if (editor == nullptr)
    {
        sendEditorOpened (false, nullptr, 0, 0, false);
        return;
    }

    editorHolder = std::make_unique<EditorHolder> (*editor,
        [this] (int width, int height) { sendEditorResized (width, height); });

    if (wantEmbedding)
    {
        // 設計書3.7の「親の窓へはめ込む」方式。**窓を作るのは子**で、
        // 親は`HWNDComponent`でそれを引き取ります（`SandboxedPluginEditor`）。
        //
        // **画面の外で作ること。** `addToDesktop()`の瞬間は、まだ普通の
        // 最上位の窓なので、真ん中に作ると**一瞬ちらつきます**
        editorHolder->setTopLeftPosition (-20000, -20000);
        editorHolder->setVisible (true);
        editorHolder->addToDesktop (0);

        embeddedHandle = editorHolder->getWindowHandle();

        if (embeddedHandle == nullptr)
        {
            // 窓にできなかった：別窓へ落とします（下と同じ道）
            editorHolder->removeFromDesktop();
            wantEmbedding = false;
        }
    }

    if (! wantEmbedding)
    {
        auto window = std::make_unique<SandboxEditorWindow> (
            pluginInstance->getName(), [this] { closeEditorWindowAndTell(); });

        window->setUsingNativeTitleBar (true);
        window->setContentNonOwned (editorHolder.get(), true);
        window->setResizable (editorHolder->editor.isResizable(), false);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->setVisible (true);
        window->toFront (true);

        editorWindow = std::move (window);
    }

    sendEditorOpened (true, embeddedHandle, editorHolder->getWidth(), editorHolder->getHeight(),
                       editor->isResizable());
}

void SandboxWorker::resizeEditor (int width, int height)
{
    if (editorHolder == nullptr || width <= 0 || height <= 0)
        return;

    editorHolder->editor.setSize (width, height);
}

void SandboxWorker::closeEditorWindow()
{
    JUCE_ASSERT_MESSAGE_THREAD

    embeddedHandle = nullptr;

    // **窓が先、エディタが後**（1.5）。窓は`setContentNonOwned`なので、
    // ここで消えても中身（`editorHolder`）は生き残ります
    editorWindow = nullptr;

    if (editorHolder != nullptr)
    {
        // **VST3のエディタは自分で`editorBeingDeleted()`を呼びます**が、
        // 呼ばない実装に当たっても壊れないよう、こちらからも伝えておきます
        if (pluginInstance != nullptr)
            pluginInstance->editorBeingDeleted (&editorHolder->editor);

        auto* editor = &editorHolder->editor;
        editorHolder = nullptr;
        delete editor;
    }
}

void SandboxWorker::closeEditorWindowAndTell()
{
    closeEditorWindow();

    sendTree (juce::ValueTree (SandboxIPC::MSG_EDITOR_CLOSED));
}

void SandboxWorker::sendEditorOpened (bool success, void* handle, int width, int height, bool resizable)
{
    juce::ValueTree result (SandboxIPC::MSG_EDITOR_OPENED);

    result.setProperty (SandboxIPC::propSuccess, success, nullptr);
    result.setProperty (SandboxIPC::propEditorHandle,
                         (juce::int64) (juce::pointer_sized_int) handle, nullptr);
    result.setProperty (SandboxIPC::propEditorWidth, width, nullptr);
    result.setProperty (SandboxIPC::propEditorHeight, height, nullptr);
    result.setProperty (SandboxIPC::propEditorResizable, resizable, nullptr);

    sendTree (result);
}

void SandboxWorker::sendEditorResized (int width, int height)
{
    juce::ValueTree message (SandboxIPC::MSG_EDITOR_RESIZED);

    message.setProperty (SandboxIPC::propEditorWidth, width, nullptr);
    message.setProperty (SandboxIPC::propEditorHeight, height, nullptr);

    sendTree (message);
}

//==============================================================================

void SandboxWorker::handleConnectionLost()
{
    // 親が終わった／繋がりが切れた：**この子も終わります**
    // （置き去りのプロセスが residual に残らないように）
    triggerAsyncUpdate();
}

void SandboxWorker::handleAsyncUpdate()
{
    juce::JUCEApplication::quit();
}
