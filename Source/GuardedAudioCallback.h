#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

//==============================================================================
/**
    8.316：**止まったあとに1ブロック届く**のを堰き止める（Phase 309／本人の報告）。

    > 「AudioのInputをnoneにするとアプリが落ちる」

    落ちていたのは**JUCEの中**でした。道筋はこうです。

    ```
    WASAPIAudioIODevice::run()                        ← オーディオスレッド
      → AudioDeviceManager::CallbackHandler
        → AudioProcessorPlayer::audioDeviceIOCallbackWithContext +0x3DD
             currentDevice->getWorkgroup()            ← currentDevice が null
    ```

    ### なぜ null なのか

    `AudioProcessorPlayer`で`currentDevice`に null を入れる場所は**1つだけ**です。

    ```cpp
    void AudioProcessorPlayer::audioDeviceStopped()  { ... currentDevice = nullptr; }
    ```

    つまり「**止まったと伝えたあとに、始まったと伝えないまま、音のブロックが届いた**」。
    JUCE自身、その手前に`jassert (currentDevice != nullptr)`を置いています
    ——**あちらも「起きてはいけない」と考えている**状態です（Releaseでは消えます）。

    ### 隙間はここにあります（`juce_WASAPI_windows.cpp`）

    ```cpp
    void run() override
    {
        while (! threadShouldExit())
        {
            const auto loadedFlags = flags.load (std::memory_order_acquire);   // ①ここで読む
            ...
            WaitForSingleObject (inputDevice->clientEvent, 1000);              // ②最大1秒待つ
            ...
            {
                const ScopedTryLock sl (startStopLock);
                if (sl.isLocked() && (loadedFlags & flagStarted) != 0)         // ③①の写しで判定
                    callback->audioDeviceIOCallbackWithContext (...);
    ```

    ```cpp
    void stop() override
    {
        auto* callbackLocal = ...{ const ScopedLock sl (startStopLock);
                                   flags.fetch_and (~flagStarted, ...); ... };
        if (callbackLocal != nullptr)
            callbackLocal->audioDeviceStopped();      // ← 鍵の外
    }
    ```

    **③が見ているのは①で取った写し**で、鍵の中で取り直していません。
    ②で待っているあいだに`stop()`が走ると、旗は下りているのに**写しは立ったまま**
    ——鍵が空いた瞬間に、**止めたはずのコールバックが1回通ります**。

    **②が「最大1秒」なので、隙間は広い**です。だから毎回落ちました
    （本人の手元で4回、全部同じ番地）。

    ### なぜ「入力をnoneにする」と踏むのか

    入力デバイスを変えると、JUCEは**デバイスを閉じて開き直します**。
    つまり`stop()`が走ります。**普通に使っているあいだは走らない**ので、
    ここまで誰も踏みませんでした。

    ### ここで直す理由

    **JUCEを書き換えていません。** あちらはCMakeが引いてくるので、
    書き換えると**引き直すたびに消え**、公開リポジトリから組んだ人には効きません。

    代わりに**間に1枚挟みます**。`AudioDeviceManager`へ渡すのはこちらで、
    **「始まった」と聞いてから「止まった」と聞くまでのあいだ**しか中へ通しません。
    デバイスの種類にも、JUCEの版にも依りません。

    ### 鍵は`ScopedTryLock`

    **オーディオスレッドを待たせないため**です。取れなかったときは
    「いま開始か停止の最中」なので、**無音を出して1ブロック捨てます**。
    待たせて直すと、**直した先で音が途切れます**。
*/
class GuardedAudioCallback final : public juce::AudioIODeviceCallback
{
public:
    explicit GuardedAudioCallback (juce::AudioIODeviceCallback& target) : inner (target) {}

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const juce::ScopedLock sl (lock);

        inner.audioDeviceAboutToStart (device);
        running = true;   // **中を先に起こしてから旗を立てること**
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl (lock);

        running = false;  // **旗を先に下ろしてから中へ伝えること**
        inner.audioDeviceStopped();
    }

    void audioDeviceError (const juce::String& errorMessage) override
    {
        inner.audioDeviceError (errorMessage);
    }

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override
    {
        if (! guarding)   // 直す前の姿（試験用。`setGuardEnabledForTesting`）
        {
            inner.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                                     outputChannelData, numOutputChannels,
                                                     numSamples, context);
            return;
        }

        const juce::ScopedTryLock sl (lock);

        if (sl.isLocked() && running)
        {
            inner.audioDeviceIOCallbackWithContext (inputChannelData, numInputChannels,
                                                     outputChannelData, numOutputChannels,
                                                     numSamples, context);
            return;
        }

        // 通さなかったぶんは**必ず無音で埋めること**。
        // 放っておくと、デバイスが渡してきたバッファの中身がそのまま出ます
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);
    }

    /** 8.316：**直す前の姿に戻す口**（試験用）。

        `false`にすると素通しになり、**Phase 308までと同じ**——
        止まったあとのブロックもそのまま中へ流します。
        `--audio-selftest`が「直す前なら落ちること」を見るために使います。
        **本番では触らないこと。** */
    void setGuardEnabledForTesting (bool shouldGuard) { guarding = shouldGuard; }

private:
    juce::AudioIODeviceCallback& inner;
    juce::CriticalSection lock;
    bool running = false;
    bool guarding = true;
};
