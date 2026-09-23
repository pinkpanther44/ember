#include "AudioDeviceSelfTest.h"

#include "ProjectModel.h"
#include "AudioEngine.h"

#include <juce_events/juce_events.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include <iostream>

namespace AudioDeviceSelfTest
{
    namespace
    {
        int problems = 0;

        void say (const juce::String& line)
        {
            std::cout << line << std::endl;
        }

        void check (bool condition, const juce::String& what)
        {
            if (condition)
            {
                say ("  ok    " + what);
            }
            else
            {
                ++problems;
                say ("  FAIL  " + what);
            }
        }

        /** メッセージを回しつつ、実際に音が何ブロックか流れるだけ待つ。

            **待たないと意味がありません。** 踏みたいのは
            「**止めたあとに、オーディオスレッドが1ブロック通す**」瞬間なので、
            **そのスレッドが回る時間**を与える必要があります。

            `MessageManager::runDispatchLoopUntil()`は**使えません**
            ——この企ては`JUCE_MODAL_LOOPS_PERMITTED`を切ってあります（8.260と同じ）。
            Win32のループを直に回します。 */
        void pump (int milliseconds)
        {
           #if JUCE_WINDOWS
            const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;

            while (juce::Time::getMillisecondCounter() < until)
            {
                MSG message;

                while (PeekMessage (&message, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage (&message);
                    DispatchMessage (&message);
                }

                juce::Thread::sleep (5);
            }
           #else
            juce::Thread::sleep (milliseconds);
           #endif
        }
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--audio-selftest"))
            return false;

        const bool withoutGuard = commandLine.contains ("--without-guard");

        say ("--- switching the audio input while the device is running");

        if (withoutGuard)
            say ("  (the guard is OFF - this is the shape of Phase 308, and it is meant to crash)");

        ProjectModel project;
        AudioEngine engine (project);

        const auto error = engine.initialise();

        if (error.isNotEmpty())
        {
            say ("  skipped: the audio device did not open (" + error + ")");
            say ("--- 0 problem(s) ---");
            juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (0);
            return true;
        }

        auto& deviceManager = engine.getDeviceManagerForTesting();

        if (withoutGuard)
            engine.getAudioCallbackGuardForTesting().setGuardEnabledForTesting (false);

        auto* device = deviceManager.getCurrentAudioDevice();

        if (device == nullptr)
        {
            say ("  skipped: no audio device is open on this machine");
            say ("--- 0 problem(s) ---");
            juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (0);
            return true;
        }

        say ("  device : " + device->getTypeName() + " / " + device->getName());

        auto setup = deviceManager.getAudioDeviceSetup();
        const auto originalInput = setup.inputDeviceName;

        say ("  input  : " + (originalInput.isEmpty() ? juce::String ("(none)") : originalInput));

        // **入力だけ外せるとは限りません**（Phase 309でLinuxに教わりました）。
        //
        // ALSA（PipeWire）では入出力が**1つのデバイス**で、JUCEの
        // `AudioDeviceSelectorComponent`も入力欄を出しません。
        // `inputDeviceName`を空にしても`updateConfig()`と同じように
        // **出力の名前で上書きされる**ので、入力は残ったままです。
        //
        // Windowsの形のまま「入力が無くなること」を数えると、
        // **Linuxで必ず落ちます**——落ちるべきでないところで落ちる試験は、
        // そのうち誰も読まなくなります。
        auto* deviceType = deviceManager.getCurrentDeviceTypeObject();
        const bool canDropInputAlone = deviceType != nullptr
                                        && deviceType->hasSeparateInputsAndOutputs();

        if (! canDropInputAlone)
            say ("  (this device type shares one device for input and output,"
                 " so the input cannot be taken away on its own)");

        // 環境設定の画面が開いているときと同じ状態にする。**入力レベルの測定**は
        // JUCEの選択UI（`SimpleDeviceManagerInputLevelMeter`）が点けるもので、
        // `audioDeviceIOCallbackInt`の中身が1つ増えます
        auto levelGetter = deviceManager.getInputLevelGetter();

        // **音を流しながら切り替えること。** 止まっているあいだに付け替えても、
        // 踏みたい瞬間（オーディオスレッドが回っている最中の`stop()`）が来ません
        pump (300);

        //======================================================================
        // ②〜⑤ **何度も往復させます。** 1回では踏めませんでした（実測）——
        // 狙っているのはスレッドの隙間なので、**回数で当てにいきます**
        constexpr int numSwitches = 8;

        bool switchFailed = false;
        bool restoreFailed = false;
        bool inputStayed = false;

        for (int i = 0; i < numSwitches; ++i)
        {
            // 入力を外す。**環境設定でnoneを選んだのと同じことです**
            setup.inputDeviceName = {};
            setup.useDefaultInputChannels = true;

            if (deviceManager.setAudioDeviceSetup (setup, true).isNotEmpty())
                switchFailed = true;

            pump (250);

            if (canDropInputAlone && engine.isAudioInputAvailable())
                inputStayed = true;

            // 戻す。**戻せないと、試した人の設定を壊したまま終わります**
            setup.inputDeviceName = originalInput;
            setup.useDefaultInputChannels = true;

            if (deviceManager.setAudioDeviceSetup (setup, true).isNotEmpty())
                restoreFailed = true;

            pump (250);
        }

        //======================================================================
        // ここへ辿り着けたこと自体が答え（落ちていれば、この行は出ません）
        check (true, "the application survived " + juce::String (numSwitches)
                        + " rounds of taking the input away and putting it back");

        check (! switchFailed,  "...taking it away never reported an error");
        check (! restoreFailed, "...putting it back never reported an error");
        if (canDropInputAlone)
            check (! inputStayed, "...and the engine said there was no input each time");
        else
            say ("  n/a   (there is no input to take away on its own here)");

        check (deviceManager.getCurrentAudioDevice() != nullptr,
                "the output device is still open at the end");

        say ("--- " + juce::String (problems) + " problem(s) ---");

        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
