#include "SandboxSelfTest.h"

#include "Plugins/MantaPluginFormat.h"
#include "SandboxedPluginProcessor.h"

#include <iostream>

#if JUCE_WINDOWS
 // **自分のプロセスIDを知るためだけ**に読みます（自分の子だけを止めるため）。
 // `NOMINMAX`と`WIN32_LEAN_AND_MEAN`を先に置くこと——無いと`min`/`max`のマクロが
 // 後ろのコードを壊します
 #ifndef NOMINMAX
  #define NOMINMAX 1
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN 1
 #endif
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace SandboxSelfTest
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

        //======================================================================
        /** 試す相手を決める。引数があればそれ、無ければ内蔵のEQ。 */
        bool findDescription (const juce::String& path, juce::PluginDescription& result,
                               juce::AudioPluginFormatManager& formats)
        {
            if (path.isEmpty())
                return MantaPlugins::findDescription ("manta:eq", result);

            juce::OwnedArray<juce::PluginDescription> found;

            for (auto* format : formats.getFormats())
                format->findAllTypesForFile (found, path);

            if (found.isEmpty())
                return false;

            result = *found.getFirst();
            return true;
        }

        /** サイン波を作る（両方の経路へ同じものを入れる）。 */
        void fillWithSine (juce::AudioBuffer<float>& buffer, double& phase, double sampleRate)
        {
            const double increment = juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = (float) (0.25 * std::sin (phase));
                phase += increment;

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.setSample (channel, i, value);
            }
        }
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        juce::ArgumentList args ("selftest", commandLine);

        if (! args.containsOption ("--sandbox-selftest"))
            return false;

        problems = 0;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        constexpr int numBlocks = 40;

        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);
        formats.addFormat (new MantaPluginFormat());

        juce::String path;

        if (args.size() > 1 && ! args[args.size() - 1].isLongOption())
            path = args[args.size() - 1].text.unquoted();

        juce::PluginDescription description;

        if (! findDescription (path, description, formats))
        {
            say ("no plugin to test (" + (path.isEmpty() ? juce::String ("manta:eq") : path) + ")");
            return true;
        }

        say ("sandbox self-test: " + description.name
               + " (" + description.pluginFormatName + ")");

        //----------------------------------------------------------------------
        // ① ネイティブで読んで、比べるための音を作る
        juce::String nativeError;
        auto native = formats.createPluginInstance (description, sampleRate, blockSize, nativeError);

        if (native == nullptr)
        {
            say ("  native load failed: " + nativeError);
            ++problems;
            return true;
        }

        native->prepareToPlay (sampleRate, blockSize);

        //----------------------------------------------------------------------
        // ①-b **そのプラグインは、そもそも同じ入力で同じ音を出すか。**
        //
        // ディザやランダムな揺れを持つものは、**同じ設定でも毎回違う音**になります。
        // それを知らずに「サンドボックスの音が違う」と言うと、
        // **道具の欠陥を本物の不具合に見せる**ことになります（8.257で1度やりました）
        bool deterministic = true;

        {
            // **`native`はここで使いません。** 8ブロック鳴らしてから
            // `releaseResources()`で戻したつもりでも、**戻り切らないプラグインがあります**
            // （実測：それで比べたら0.25ずれ、サンドボックスの不具合に見えました）。
            // 比べる相手は**一度も鳴らしていないもの**であること
            juce::String twinError;
            auto twinA = formats.createPluginInstance (description, sampleRate, blockSize, twinError);
            auto twinB = formats.createPluginInstance (description, sampleRate, blockSize, twinError);

            if (twinA != nullptr && twinB != nullptr)
            {
                twinA->prepareToPlay (sampleRate, blockSize);
                twinB->prepareToPlay (sampleRate, blockSize);

                juce::AudioBuffer<float> a (2, blockSize), b (2, blockSize);
                juce::MidiBuffer emptyMidi;
                double phaseA = 0.0, phaseB = 0.0;

                for (int i = 0; i < 8 && deterministic; ++i)
                {
                    fillWithSine (a, phaseA, sampleRate);
                    fillWithSine (b, phaseB, sampleRate);

                    emptyMidi.clear();
                    twinA->processBlock (a, emptyMidi);
                    emptyMidi.clear();
                    twinB->processBlock (b, emptyMidi);

                    for (int s = 0; s < blockSize; ++s)
                        if (std::abs (a.getSample (0, s) - b.getSample (0, s)) > 1.0e-5f)
                            deterministic = false;
                }

                twinA->releaseResources();
                twinB->releaseResources();
            }

            if (! deterministic)
                say ("  (this plugin does not repeat itself - the audio comparison is skipped)");
        }

        //----------------------------------------------------------------------
        // ② サンドボックスで読む
        juce::String sandboxError;
        auto sandboxed = SandboxedPluginProcessor::create (description, sampleRate, blockSize,
                                                            sandboxError);

        check (sandboxed != nullptr, "loaded in a child process"
                                       + (sandboxed == nullptr ? " (" + sandboxError + ")"
                                                                : juce::String()));

        if (sandboxed == nullptr)
            return true;

        sandboxed->prepareToPlay (sampleRate, blockSize);

        check (sandboxed->getName() == native->getName(), "name matches: " + sandboxed->getName());
        check (sandboxed->getParameters().size() == native->getParameters().size(),
                "parameter count matches ("
                  + juce::String (sandboxed->getParameters().size()) + ")");
        check (sandboxed->getLatencySamples() == native->getLatencySamples() + blockSize,
                "latency = plugin + one block ("
                  + juce::String (sandboxed->getLatencySamples()) + ")");

        //----------------------------------------------------------------------
        // ③ 同じ音を両方へ通して比べる。
        //    **サンドボックスは1ブロック遅れて出ます**ので、そのぶんずらして比べます
        juce::AudioBuffer<float> nativeOutput (2, blockSize * numBlocks);
        juce::AudioBuffer<float> sandboxOutput (2, blockSize * numBlocks);
        nativeOutput.clear();
        sandboxOutput.clear();

        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;

        double nativePhase = 0.0, sandboxPhase = 0.0;

        for (int i = 0; i < numBlocks; ++i)
        {
            fillWithSine (block, nativePhase, sampleRate);
            midi.clear();
            native->processBlock (block, midi);

            for (int channel = 0; channel < 2; ++channel)
                nativeOutput.copyFrom (channel, i * blockSize, block, channel, 0, blockSize);
        }

        // **間に合わなかったブロックは比べません**（そこは素通しになっているので、
        // 中身が違って当たり前です）。何回あったかは下で報告します
        std::vector<bool> blockWasLate ((size_t) numBlocks, false);
        int dropoutsBefore = sandboxed->getDropoutCount();

        for (int i = 0; i < numBlocks; ++i)
        {
            fillWithSine (block, sandboxPhase, sampleRate);
            midi.clear();
            sandboxed->processBlock (block, midi);

            const int dropoutsNow = sandboxed->getDropoutCount();
            blockWasLate[(size_t) i] = (dropoutsNow != dropoutsBefore);
            dropoutsBefore = dropoutsNow;

            for (int channel = 0; channel < 2; ++channel)
                sandboxOutput.copyFrom (channel, i * blockSize, block, channel, 0, blockSize);
        }

        // 最初の1ブロックはサンドボックス側が素通し（＝1ブロック遅れ）。そこを飛ばして比べる。
        //
        // **間に合わなかったブロックが出たら、そこで比べるのをやめます。**
        // 捨てられた入力はプラグインへ届いていないので、**その先は内部状態が
        // 食い違って当たり前**です（`SandboxWorker::run()`の説明）。
        // 続けて比べると、**道具の側の都合を本物の不具合に見せる**ことになります
        //
        // **1ブロックでも間に合わなかったら、比べられません。**
        //
        // 間に合わなかったぶんの入力は**子のプラグインへ届いていません**
        // （`SandboxWorker::run()`の説明）。ネイティブ側は切れ目なく受け取っているので、
        // **そこから先は内部状態が違って当たり前**です——どこで落ちたかに関わらず。
        //
        // なので「落ちたら以降を無視する」ではなく、**落ちたら比較そのものをやめます**。
        // 道具の都合を本物の不具合に見せないため（8.257の教訓）
        const int lateBlocks = sandboxed->getDropoutCount();

        float worstDifference = 0.0f;
        double nativeEnergy = 0.0;
        int comparedBlocks = 0;

        for (int b = 1; b < numBlocks && lateBlocks == 0; ++b)
        {
            juce::ignoreUnused (blockWasLate);
            ++comparedBlocks;

            for (int i = 0; i < blockSize; ++i)
            {
                const float a = nativeOutput.getSample (0, (b - 1) * blockSize + i);
                const float c = sandboxOutput.getSample (0, b * blockSize + i);

                worstDifference = juce::jmax (worstDifference, std::abs (a - c));
                nativeEnergy += (double) a * a;
            }
        }

        // **遅れは「たまに」なら許します**（挿した直後の1〜2ブロックがほとんど）。
        // 続けて出るなら、そのプラグインはこの機械ではサンドボックスに載りません
        check (lateBlocks <= numBlocks / 8,
                "late blocks are rare (" + juce::String (lateBlocks)
                  + " of " + juce::String (numBlocks) + ", they pass the dry signal through)");

        if (lateBlocks > 0)
            say ("  skip  audio comparison (a late block never reached the plugin,"
                  " so its state diverges from there on)");
        else if (! deterministic)
            say ("  skip  audio comparison (this plugin does not repeat itself)");
        else
        {
            check (nativeEnergy > 1.0e-6, "the plugin actually produced sound");
            check (comparedBlocks == numBlocks - 1,
                    "every block compared (" + juce::String (comparedBlocks) + ")");
            check (worstDifference < 1.0e-5f,
                    "sandboxed output matches native (worst difference "
                      + juce::String (worstDifference, 8) + ")");
        }

        //----------------------------------------------------------------------
        // ④ 状態の往復
        {
            juce::MemoryBlock nativeState, sandboxState;
            native->getStateInformation (nativeState);
            sandboxed->getStateInformation (sandboxState);

            check (sandboxState.getSize() > 0, "state came back from the child ("
                                                  + juce::String ((int) sandboxState.getSize()) + " bytes)");
            check (sandboxState == nativeState, "state matches the native one");

            sandboxed->setStateInformation (sandboxState.getData(), (int) sandboxState.getSize());
        }

        //----------------------------------------------------------------------
        // ⑤ パラメータを動かして、子へ届いているか
        if (! sandboxed->getParameters().isEmpty())
        {
            auto* parameter = sandboxed->getParameters()[0];
            const float target = parameter->getValue() > 0.5f ? 0.1f : 0.9f;

            parameter->setValueNotifyingHost (target);

            // **1ブロック渡して、少し待つこと。**
            // 溜まった変化が子へ届くのは次のブロックで、当てるのは子の音のスレッド、
            // 木へ反映するのは子のメッセージスレッドです（`ParameterNotifier`）
            for (int i = 0; i < 4; ++i)
            {
                fillWithSine (block, sandboxPhase, sampleRate);
                midi.clear();
                sandboxed->processBlock (block, midi);
                juce::Thread::sleep (20);
            }

            juce::MemoryBlock changedState;
            sandboxed->getStateInformation (changedState);

            juce::MemoryBlock nativeState;
            native->getStateInformation (nativeState);

            check (changedState != nativeState,
                    "a parameter change reached the plugin in the child process");
        }

        //----------------------------------------------------------------------
        // ⑤-b **MIDIが通るか**（音源をサンドボックスへ入れて、鍵盤を叩く）。
        //
        // ここだけは**必ず内蔵のシンセ**で試します——手元に音源のVST3が無くても
        // 確かめられるように（`SandboxWorker`が`MantaPluginFormat`を載せている理由）
        {
            juce::PluginDescription synthDescription;

            if (MantaPlugins::findDescription ("manta:synth", synthDescription))
            {
                juce::String synthError;
                auto synth = SandboxedPluginProcessor::create (synthDescription, sampleRate,
                                                                blockSize, synthError);

                if (synth == nullptr)
                {
                    check (false, "loaded the built-in synth in a child process (" + synthError + ")");
                }
                else
                {
                    synth->prepareToPlay (sampleRate, blockSize);

                    check (synth->acceptsMidi(), "the sandboxed synth says it accepts MIDI");

                    double energy = 0.0;

                    for (int i = 0; i < 20; ++i)
                    {
                        juce::MidiBuffer notes;

                        if (i == 0)
                            notes.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

                        block.clear();
                        synth->processBlock (block, notes);

                        for (int s = 0; s < blockSize; ++s)
                            energy += (double) block.getSample (0, s) * block.getSample (0, s);
                    }

                    check (energy > 1.0e-6,
                            "a note played through the sandbox made sound (energy "
                              + juce::String (energy, 6) + ")");

                    // 8.263：**ノートオフも届くこと**（Phase 269）。
                    //
                    // **ここが緩いと、いちばん困る壊れ方を見落とします。** 間に合わなかった
                    // ブロックのMIDIが捨てられていた頃、たまに**鳴りっぱなし**になりました。
                    // 音は「一瞬エフェクトが掛からない」で済んでも、MIDIは済みません。
                    {
                        juce::MidiBuffer off;
                        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);

                        block.clear();
                        synth->processBlock (block, off);

                        // 減衰しきるまで流す（**リリースぶんは鳴ります**）
                        for (int i = 0; i < 120; ++i)
                        {
                            juce::MidiBuffer empty;
                            block.clear();
                            synth->processBlock (block, empty);
                        }

                        double tail = 0.0;

                        for (int i = 0; i < 20; ++i)
                        {
                            juce::MidiBuffer empty;
                            block.clear();
                            synth->processBlock (block, empty);

                            for (int s = 0; s < blockSize; ++s)
                                tail += (double) block.getSample (0, s) * block.getSample (0, s);
                        }

                        check (tail < energy * 1.0e-3,
                                "the note stopped when the note-off was sent (tail "
                                  + juce::String (tail, 8) + ")");

                        say ("  (dropped blocks so far: "
                               + juce::String (synth->getDropoutCount()) + ")");

                        // **MIDIのほうは0であるべき数**です（8.263）。
                        // 音の落ちは許せますが、MIDIの落ちは許せません
                        check (synth->getMidiDropCount() == 0,
                                "no MIDI was dropped on the way (the queue was big enough)");
                    }

                    synth->releaseResources();
                }
            }
        }

        //----------------------------------------------------------------------
        // ⑥ **子を殺しても、親は生きているか**（サンドボックスの目的そのもの）。
        //
        // プラグインが落ちるのを待つ代わりに、**こちらから止めます**——
        // 外から見れば同じこと（接続が切れる）です
        {
            check (sandboxed->isSandboxAlive(), "the child is running before we kill it");

            // **自分の子だけ**を止めます（ほかのManta Studioのサンドボックスを巻き込まない）
            // **引数は`StringArray`で渡すこと。** 1本の文字列で渡すと、
            // 中の引用符がJUCEの分解で壊れて、**何も起きないまま成功したように見えます**
           #if JUCE_WINDOWS
            const juce::StringArray command
            {
                "powershell", "-NoProfile", "-Command",
                // **自分自身を外すこと。** このPowerShellのコマンドラインにも
                // 目印の文字列が入っているので、除かないと**自分を止めて終わります**
                // （「found 2」と出て、肝心の子は生き残っていました）
                juce::String ("$target = Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq ")
                  + juce::String ((int) GetCurrentProcessId())
                  + " -and $_.ProcessId -ne $PID"
                  + " -and $_.CommandLine -like '*" + SandboxIPC::commandLineUID + "*' };"
                  + "Write-Output ('found ' + @($target).Count);"
                  + "$target | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
            };
           #else
            const juce::StringArray command
            {
                "pkill", "-P", juce::String ((int) getpid()), "-f", SandboxIPC::commandLineUID
            };
           #endif

            juce::ChildProcess killer;
            killer.start (command);

            const auto killerOutput = killer.readAllProcessOutput().trim();
            killer.waitForProcessToFinish (10000);

            if (killerOutput.isNotEmpty())
                say ("  (kill: " + killerOutput + ")");

            // 接続が切れたことは**IPCのスレッド**が知らせます（メッセージループは要りません）
            //
            // **ここではタイマーが回りません**（窓を出していないので、
            // メッセージループが動いていません）。本体では0.5秒ごとに
            // `PluginSandboxHost::timerCallback()`が同じことをします
            // **長めに待つこと。** ここで待っているのは「気づくまでの速さ」ではなく、
            // **外の`powershell`が本当に止め終わるまで**です——機械が混んでいると
            // 数秒かかります。3秒で切っていた頃、12回に1回ここだけ落ちました。
            // 速さは`(noticed after ...)`で見えるようにしてあります
            bool noticed = false;
            int waitedMs = 0;

            for (int i = 0; i < 300 && ! noticed; ++i)
            {
                juce::Thread::sleep (50);
                waitedMs += 50;
                sandboxed->probeSandbox();
                noticed = ! sandboxed->isSandboxAlive();
            }

            check (noticed, "the host noticed that the child died (after "
                             + juce::String (waitedMs) + " ms)");

            // **殺した後も、DAW側は動き続けること。** 音は素通しになります
            fillWithSine (block, sandboxPhase, sampleRate);

            juce::AudioBuffer<float> inputCopy (2, blockSize);

            for (int channel = 0; channel < 2; ++channel)
                inputCopy.copyFrom (channel, 0, block, channel, 0, blockSize);

            midi.clear();
            sandboxed->processBlock (block, midi);

            float worstBypassDifference = 0.0f;

            for (int i = 0; i < blockSize; ++i)
                worstBypassDifference = juce::jmax (worstBypassDifference,
                                                     std::abs (block.getSample (0, i)
                                                                - inputCopy.getSample (0, i)));

            check (worstBypassDifference == 0.0f,
                    "audio passes through untouched once the child is gone");

            say ("  (the host is still running - that is the whole point)");
        }

        native->releaseResources();
        sandboxed->releaseResources();

        say ("--- " + juce::String (problems) + " problem(s) ---");

        juce::JUCEApplication::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }

    //==========================================================================
    // 8.262：**本来のGUIを、はめ込んだ状態で確かめる**（Phase 269）
    //
    // こちらは窓を出します——**GUIの話なので、出さずには確かめられません**。
    // そのぶん、見るものは「絵が合っているか」ではなく**繋がりの事実**です：
    //
    // | 見るもの | どうやって |
    // |---|---|
    // | 本当に別プロセスの窓がはまったか | 親の窓の子を数え、**所有プロセスが違うこと**を見ます |
    // | 大きさが合っているか | `GetWindowRect`と`getBounds()`を突き合わせます |
    // | 引き伸ばしが伝わるか | エディタを広げて、子の窓が追うかを見ます |
    // | 閉じて開き直せるか | 2度目が同じようにはまること |
    // | **落ちても親が生きているか** | 子を外から止めて、画面が「落ちました」に変わること |
    //
    // 絵そのものは**PNGに落とします**（目で見るため。合否には数えません）。

   #if JUCE_WINDOWS
    namespace
    {
        /** 窓の見た目をそのまま画像にする（**子プロセスの中身ごと**）。 */
        juce::Image captureWindow (HWND hwnd)
        {
            RECT rect {};

            if (! GetWindowRect (hwnd, &rect))
                return {};

            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;

            if (width <= 0 || height <= 0)
                return {};

            auto screenDC = GetDC (nullptr);
            auto memoryDC = CreateCompatibleDC (screenDC);
            auto bitmap = CreateCompatibleBitmap (screenDC, width, height);
            auto previous = SelectObject (memoryDC, bitmap);

            // `PW_RENDERFULLCONTENT`（2）。**これが無いと、子プロセスの窓は真っ黒**に出ます
            if (! PrintWindow (hwnd, memoryDC, 2))
                BitBlt (memoryDC, 0, 0, width, height, screenDC, rect.left, rect.top, SRCCOPY);

            BITMAPINFO info {};
            info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width;
            info.bmiHeader.biHeight = -height;     // 上から下へ
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            juce::HeapBlock<juce::uint8> pixels ((size_t) width * (size_t) height * 4);
            GetDIBits (memoryDC, bitmap, 0, (UINT) height, pixels.getData(), &info, DIB_RGB_COLORS);

            juce::Image image (juce::Image::RGB, width, height, false);

            {
                juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

                for (int y = 0; y < height; ++y)
                {
                    auto* source = pixels.getData() + (size_t) y * (size_t) width * 4;

                    for (int x = 0; x < width; ++x)
                        data.setPixelColour (x, y, juce::Colour (source[x * 4 + 2],
                                                                  source[x * 4 + 1],
                                                                  source[x * 4]));
                }
            }

            SelectObject (memoryDC, previous);
            DeleteObject (bitmap);
            DeleteDC (memoryDC);
            ReleaseDC (nullptr, screenDC);

            return image;
        }

        /** 親の窓にぶら下がっている、**別プロセスの**窓を探す。 */
        HWND findForeignChildWindow (HWND parent)
        {
            for (auto child = GetWindow (parent, GW_CHILD); child != nullptr;
                 child = GetWindow (child, GW_HWNDNEXT))
            {
                DWORD owner = 0;
                GetWindowThreadProcessId (child, &owner);

                if (owner != GetCurrentProcessId())
                    return child;
            }

            return nullptr;
        }
    }
   #endif

    namespace
    {
        /**
            メッセージループを少しだけ回す（**窓が描かれるのを待つ**）。

            `MessageManager::runDispatchLoopUntil()`は使えません——
            この企ては`JUCE_MODAL_LOOPS_PERMITTED`を切ってあるので、
            **その関数がそもそもコンパイルされません**（切ってあるのは正しい。
            画面を止める入れ子のループは、DAWでは事故の元です）。

            ここは試験なので、**Win32のループを直に回します**。JUCEの窓も
            タイマーも`callAsync`も、Windowsでは全部この上に載っています。
        */
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

        /** 自分の子のサンドボックスプロセスを、外から止める（⑥と同じやり方）。 */
        void killOwnSandboxChildren()
        {
           #if JUCE_WINDOWS
            const juce::StringArray command
            {
                "powershell", "-NoProfile", "-Command",
                juce::String ("$target = Get-CimInstance Win32_Process | Where-Object { $_.ParentProcessId -eq ")
                  + juce::String ((int) GetCurrentProcessId())
                  + " -and $_.ProcessId -ne $PID"
                  + " -and $_.CommandLine -like " + juce::String::charToString ((juce::juce_wchar) 39)
                  + "*" + SandboxIPC::commandLineUID + "*"
                  + juce::String::charToString ((juce::juce_wchar) 39) + " };"
                  + "$target | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
            };
           #else
            const juce::StringArray command
            {
                "pkill", "-P", juce::String ((int) getpid()), "-f", SandboxIPC::commandLineUID
            };
           #endif

            juce::ChildProcess killer;
            killer.start (command);
            killer.readAllProcessOutput();
            killer.waitForProcessToFinish (10000);
        }
    }

    bool runEditorTestIfRequested (const juce::String& commandLine)
    {
        juce::ArgumentList args ("editortest", commandLine);

        if (! args.containsOption ("--sandbox-editor-test"))
            return false;

        problems = 0;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;

        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);
        formats.addFormat (new MantaPluginFormat());

        juce::String path;

        if (args.size() > 1 && ! args[args.size() - 1].isLongOption())
            path = args[args.size() - 1].text.unquoted();

        juce::PluginDescription description;

        if (! findDescription (path, description, formats))
        {
            say ("no plugin to test (" + (path.isEmpty() ? juce::String ("manta:eq") : path) + ")");
            return true;
        }

        say ("sandbox editor test: " + description.name
               + " (" + description.pluginFormatName + ")");

        // **窓を出せるか、先に確かめること**（8.264）。
        //
        // Linuxで`DISPLAY`は立っていても**X側に断られる**ことがあります
        // （SSH越しに他人のセッションを覗こうとしたとき）。そのとき画面は0枚になり、
        // `centreWithSize()`が**primaryDisplayのnullを踏んで落ちます**
        // ——JUCEの中で落ちるので、こちらの不具合に見えます。
        //
        // **「確かめられなかった」は問題として数えます。** 黙って0を返すと、
        // **試験を通していないのに通ったことになります**（7.3）。
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            check (false, "there is a screen to put a window on"
                            " - run this from a desktop session (or under xvfb-run)");

            say ("--- " + juce::String (problems) + " problem(s) ---");
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (problems);

            return true;
        }

        juce::String error;
        auto sandboxed = SandboxedPluginProcessor::create (description, sampleRate, blockSize, error);

        if (sandboxed == nullptr)
        {
            check (false, "loaded in the sandbox (" + error + ")");
            say ("--- " + juce::String (problems) + " problem(s) ---");
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
            return true;
        }

        sandboxed->setPlayConfigDetails (2, 2, sampleRate, blockSize);
        sandboxed->prepareToPlay (sampleRate, blockSize);

        check (sandboxed->hasEditor(), "the proxy says the plugin has an editor of its own");

        //----------------------------------------------------------------------
        // ① 開く

        auto window = std::make_unique<juce::DocumentWindow> (
            "sandbox editor test", juce::Colours::black, juce::DocumentWindow::closeButton);

        auto* editor = sandboxed->createEditorAndMakeActive();

        check (editor != nullptr, "the proxy made an editor");

        if (editor == nullptr)
        {
            say ("--- " + juce::String (problems) + " problem(s) ---");
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
            return true;
        }

        window->setUsingNativeTitleBar (true);
        window->setContentNonOwned (editor, true);
        window->centreWithSize (window->getWidth(), window->getHeight());
        window->setVisible (true);
        window->toFront (true);

        pump (1500);

       #if JUCE_WINDOWS
        check (sandboxed->isEditorEmbedded(), "the editor was embedded rather than left as a panel");
       #else
        // **はめ込めるのはWindowsだけ**（`SandboxedPluginEditor.h`）。
        // ほかの環境では、子が自分の窓として出し、こちらはパラメータ一覧になります
        check (! sandboxed->isEditorEmbedded(),
                "the editor falls back to a panel where embedding is not available");
       #endif

       #if JUCE_WINDOWS
        auto parentHandle = (HWND) window->getWindowHandle();
        auto foreign = parentHandle != nullptr ? findForeignChildWindow (parentHandle) : nullptr;

        check (foreign != nullptr, "a window from ANOTHER process is sitting inside ours");

        if (foreign != nullptr)
        {
            check (IsWindowVisible (foreign) != 0, "that window is visible");

            DWORD ownerProcess = 0;
            GetWindowThreadProcessId (foreign, &ownerProcess);
            say ("  (our pid " + juce::String ((int) GetCurrentProcessId())
                   + ", the window's pid " + juce::String ((int) ownerProcess) + ")");

            RECT rect {};
            GetWindowRect (foreign, &rect);

            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;

            check (std::abs (width - editor->getWidth()) <= 2
                    && std::abs (height - editor->getHeight()) <= 2,
                    "it is the size we asked for (" + juce::String (width) + "x"
                      + juce::String (height) + ")");

            auto image = captureWindow (parentHandle);

            if (image.isValid())
            {
                auto file = juce::File::getCurrentWorkingDirectory()
                              .getChildFile ("sandbox-editor.png");

                file.deleteFile();

                if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                {
                    juce::PNGImageFormat png;
                    png.writeImageToStream (image, *stream);
                    say ("  (a picture of it: " + file.getFullPathName() + ")");
                }
            }
        }
       #endif

        //----------------------------------------------------------------------
        // ② **触れるか**（Manta EQのときだけ）
        //
        // 見た目がはまっているだけでは足りません。**押した先がプラグインに届き、
        // 値が親まで返ってくる**ところまで見ます。
        //
        // Manta EQは**カーブの上をダブルクリックするとバンドができます**——
        // 広い面のどこでも効くので、座標に神経質にならずに済みます。
        // ほかのプラグインでは、そういう当てになる操作が無いので飛ばします。

       #if JUCE_WINDOWS
        if (description.fileOrIdentifier.contains ("manta:eq") && foreign != nullptr)
        {
            std::vector<float> before;

            for (auto* parameter : sandboxed->getParameters())
                before.push_back (parameter->getValue());

            RECT rect {};
            GetWindowRect (foreign, &rect);

            const int width = rect.right - rect.left;
            const int height = rect.bottom - rect.top;

            // カーブの面のまんなかあたり（上から4割）。**窓のどこでもよい訳ではない**ので、
            // 当てにできる広い面を選びます
            const int x = width / 2;
            const int y = height * 2 / 5;

            // **画面の上に出ている窓を確かめます。** ここが子プロセスのものであること自体が、
            // 「押した先が子へ行く」ことの証しです（当たり判定はWindowsがやります）
            POINT screenPoint { rect.left + x, rect.top + y };
            auto under = WindowFromPoint (screenPoint);

            DWORD ownerOfPoint = 0;

            if (under != nullptr)
                GetWindowThreadProcessId (under, &ownerOfPoint);

            check (under != nullptr && ownerOfPoint != GetCurrentProcessId(),
                    "the window under the pointer belongs to the child process");

            const auto position = (LPARAM) ((y << 16) | (x & 0xffff));

            // マウスを持っていかずに済むよう、**窓へ直に送ります**
            for (int i = 0; i < 2; ++i)
            {
                PostMessage (foreign, WM_MOUSEMOVE, 0, position);
                PostMessage (foreign, WM_LBUTTONDOWN, MK_LBUTTON, position);
                PostMessage (foreign, WM_LBUTTONUP, 0, position);
                pump (60);
            }

            pump (600);

            // **音を1ブロック流すこと。** 子で動いた値は`processBlock()`で返ってきます
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;

            for (int i = 0; i < 8; ++i)
            {
                block.clear();
                midi.clear();
                sandboxed->processBlock (block, midi);
                pump (40);
            }

            int moved = 0;

            for (int i = 0; i < (int) before.size(); ++i)
                if (std::abs (sandboxed->getParameters()[i]->getValue() - before[(size_t) i]) > 1.0e-6f)
                    ++moved;

            check (moved > 0, "clicking inside the embedded window moved "
                               + juce::String (moved) + " parameter(s) of the plugin");
        }
        else
        {
            say ("  (no dependable control to click on this plugin - skipped)");
        }
       #endif

        //----------------------------------------------------------------------
        // ③ 引き伸ばしが伝わるか（伸ばせるエディタのときだけ）

        if (editor->isResizable())
        {
            const int originalWidth = editor->getWidth();
            const int originalHeight = editor->getHeight();
            const int wantedWidth = originalWidth + 80;
            const int wantedHeight = originalHeight + 60;

            editor->setSize (wantedWidth, wantedHeight);
            pump (800);

           #if JUCE_WINDOWS
            if (auto child = parentHandle != nullptr ? findForeignChildWindow (parentHandle) : nullptr)
            {
                RECT rect {};
                GetWindowRect (child, &rect);

                check (std::abs ((rect.right - rect.left) - wantedWidth) <= 4,
                        "the plugin window followed when we stretched ours");
            }
           #endif

            // **元へ戻しておくこと。** プラグインによっては大きさを自分で覚えるので、
            // 試すたびに窓が育ちます（実測：980→1060→1220と伸びました）
            editor->setSize (originalWidth, originalHeight);
            pump (400);
        }
        else
        {
            say ("  (this editor cannot be resized - nothing to stretch)");
        }

        //----------------------------------------------------------------------
        // ④ 閉じて、開き直す

        window->clearContentComponent();
        sandboxed->editorBeingDeleted (editor);
        delete editor;
        editor = nullptr;
        pump (600);

        check (sandboxed->isSandboxAlive(), "closing the window did not take the child with it");

        const auto beforeSecondOpen = juce::Time::getMillisecondCounter();
        editor = sandboxed->createEditorAndMakeActive();
        check (editor != nullptr, "the proxy made a second editor");

        if (editor != nullptr)
        {
            window->setContentNonOwned (editor, true);
            pump (2000);

            say ("  (the second open took "
                   + juce::String ((int) (juce::Time::getMillisecondCounter() - beforeSecondOpen))
                   + " ms)");

           #if JUCE_WINDOWS
            check (sandboxed->isEditorEmbedded(), "it opens a second time");
            check (parentHandle != nullptr && findForeignChildWindow (parentHandle) != nullptr,
                    "the plugin window is inside ours again");
           #endif
        }

        //----------------------------------------------------------------------
        // ⑤ **落としても、親は生きているか**（GUIを開いたまま）

        killOwnSandboxChildren();

        bool noticed = false;
        int waitedMs = 0;

        for (int i = 0; i < 300 && ! noticed; ++i)
        {
            pump (50);
            waitedMs += 50;
            sandboxed->probeSandbox();
            noticed = ! sandboxed->isSandboxAlive();
        }

        check (noticed, "the host noticed that the child died while its window was open (after "
                          + juce::String (waitedMs) + " ms)");

        pump (500);

        check (! sandboxed->isEditorEmbedded(),
                "the editor let go of the window that died with it");

        // 描き直させてみる（**ここで落ちないこと**が確かめたいこと）
        window->repaint();
        pump (500);

        say ("  (the window is still being drawn - the DAW is alive)");

        //----------------------------------------------------------------------

        if (editor != nullptr)
        {
            window->clearContentComponent();
            sandboxed->editorBeingDeleted (editor);
            delete editor;
        }

        window = nullptr;
        sandboxed->releaseResources();
        sandboxed = nullptr;

        say ("--- " + juce::String (problems) + " problem(s) ---");

        juce::JUCEApplication::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
