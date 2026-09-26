#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "SandboxWorker.h"
#include "SandboxIPC.h"
#include "AppColours.h"
#include "MusicalTimeBench.h"   // 8.137：動作の重さの計測（Phase 175／8.1のE1）
#include "SandboxSelfTest.h"    // 8.260：サンドボックスの往復を測る（Phase 268）
#include "FooterValuesSelfTest.h" // 8.268：フッターの値と書き換え先（Phase 270）
#include "SnapGridSelfTest.h"   // 8.271：刻み（3連符）の寄せ先（Phase 272）
#include "DrumMapPresetSelfTest.h" // 8.284：ドラムマップのプリセット（Phase 277）
#include "StorageSelfTest.h"   // 8.286：プロジェクトの置き場所（Phase 279）
#include "TrackHeaderLayoutSelfTest.h" // 8.295：ヘッダーの並び（Phase 288）
#include "TimeRangeSelfTest.h"     // 8.325：MIDIの時間範囲（Phase 314）
#include "AudioDeviceSelfTest.h" // 8.316：デバイスの付け替え（Phase 309）
#include "SplashWindow.h"        // 8.151：起動画面（Phase 189／改善案⑰）
#include "ProjectChooser.h"      // 8.151：プロジェクト選択画面（Phase 189／改善案⑰）
#include "Utf8.h"
#include "Branding.h"   // 8.175：表に出る名前（Phase 216）
#include "AppIcon.h"    // 8.229：窓のアイコン（Phase 249）
#include "CrashLog.h"   // 8.316：落ちたときに番地を残す（Phase 309）
#include "AppMessageBox.h"   // 8.322：メッセージボックスの入口（Phase 312）


//==============================================================================
class PersonalDAWApplication : public juce::JUCEApplication
{
public:
    PersonalDAWApplication() = default;

    const juce::String getApplicationName() override    { return Branding::productName; }
    // 8.230：**版を直に書かないこと**（Phase 249）。
    //
    // ここは`"0.1.0"`のままで、0.2.0を出したあとも**古い版を答えていました。**
    // `CMakeLists.txt`の`project(... VERSION x.y.z)`が唯一の出どころで、
    // `JUCE_APPLICATION_VERSION_STRING`がそれを運んできます
    // （バージョン情報ダイアログは前からこちらを見ています）。
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override           { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // 8.316：**いちばん先に入れること**（Phase 309）。
        // ここから下で落ちたぶんは全部残ります——**子プロセスも同じ**です
        // （落ちるのは子のほうが多い。サンドボックスはそのために在ります）。
        CrashLog::install();

        // 8.316：`--crash-selftest`なら、**わざと落ちて**仕掛けを確かめる（Phase 309）
        if (CrashLog::runSelfTestIfRequested (commandLine))
            return;

        // 設計書3.4：同じexeを子プロセスとしても使う。
        // コマンドラインに識別子が含まれていれば、UIを出さずワーカーとして動作する。
        sandboxWorker = std::make_unique<SandboxWorker>();

        if (sandboxWorker->initialiseFromCommandLine (commandLine, SandboxIPC::commandLineUID))
        {
            // 8.320：**子のログは名前を分ける**（Phase 312）。子が落ちても本体は落ちていないので、
            // 次の起動で「前回落ちました」と出すのは間違いです（`CrashLog.h`）
            CrashLog::markAsSandboxWorker();

           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            return; // ワーカーとして起動したので、メインウィンドウは作らない
        }

        sandboxWorker = nullptr; // 親プロセスなのでワーカーは不要

        // 8.322：`--msgbox-exit-test`（Phase 312）。**メッセージボックスを開いたまま終わっても
        // 落ちないこと**を見ます。1.0.1はこれで落ちていました（`AppMessageBox.h`）。
        //
        // **窓を出します**（箱を1つ、1秒ほど）。箱が本当に開いていないと、
        // 踏みたい瞬間（終了の片付けで箱が閉じる）が来ないためです。
        //
        // `--unmanaged`を付けると**直す前の出し方**（`NativeMessageBox::showAsync`を直に）になり、
        // **落ちるのが正解**です。直したことの証拠は、この2つの差でしか出ません。
        //
        // 合格の印は**終了コード0で終わること**。落ちれば0以外になり、
        // `selftest-crash-…`のログが残ります（起動時の案内には出ません）
        // 8.322：`--msgbox-answer-test`（Phase 312）。**押したボタンの番号が、今までどおり
        // 押した順で届くか**を見ます。`showScopedAsync`は数え方が違うので、`AppMessageBox`が
        // 押した順へ戻しています——**戻し方を間違えると、31箇所の選択が黙って入れ替わります**
        // （「復元する」を押したのに破棄される）。
        //
        // ボタン3つの箱を出し、**外からボタンを押して**（`TDM_CLICK_BUTTON`）、
        // 届いた番号を`pressed N`と出して終わります
        if (commandLine.contains ("--msgbox-answer-test"))
        {
            CrashLog::markAsSelfTest();

            juce::MessageManager::callAsync ([this]
            {
                AppMessageBox::showAsync (juce::MessageBoxOptions()
                                              .withTitle ("msgbox answer test")
                                              .withMessage ("Press any button.")
                                              .withButton ("Zero")
                                              .withButton ("One")
                                              .withButton ("Two"),
                                          [this] (int pressed)
                                          {
                                              std::cout << "pressed " << pressed << std::endl;
                                              quit();
                                          });
            });

            return;
        }

        if (commandLine.contains ("--msgbox-exit-test"))
        {
            CrashLog::markAsSelfTest();

            const bool unmanaged = commandLine.contains ("--unmanaged");

            juce::MessageManager::callAsync ([unmanaged]
            {
                const auto options = juce::MessageBoxOptions()
                                         .withIconType (juce::MessageBoxIconType::InfoIcon)
                                         .withTitle ("msgbox exit test")
                                         .withMessage ("This box closes by itself - the app quits while it is open.")
                                         .withButton ("OK")
                                         .withButton ("Cancel");

                if (unmanaged)
                    juce::NativeMessageBox::showAsync (options, nullptr);   // 直す前の出し方（試験のためだけ）
                else
                    AppMessageBox::showAsync (options, nullptr);
            });

            // **箱の窓ができるまで待ってから**終わる。早すぎると、閉じる相手の窓がまだありません
            juce::Timer::callAfterDelay (1200, [this, unmanaged]
            {
                std::cout << (unmanaged ? "unmanaged box (the old way)" : "managed box")
                          << ", open boxes: " << AppMessageBox::getNumOpen()
                          << " - quitting with it open" << std::endl;
                quit();
            });

            return;
        }

        // 8.137：`--measure-musical-time`なら、測ってすぐ終わる（Phase 175／8.1のE1）。
        // **ワーカーと同じで、メインウィンドウは作りません**（`MusicalTimeBench.h`）
        if (MusicalTimeBench::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.260：`--sandbox-selftest`なら、サンドボックスの往復を測ってすぐ終わる
        // （Phase 268／`SandboxSelfTest.h`）。**これもメインウィンドウを作りません**
        if (SandboxSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.268：`--footer-selftest`も窓を出しません（Phase 270）
        if (FooterValuesSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.271：`--snap-selftest`も窓を出しません（Phase 272）
        if (SnapGridSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.284：`--drummap-selftest`も窓を出しません（Phase 277）
        if (DrumMapPresetSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.286：`--storage-selftest`も窓を出しません（Phase 279）
        if (StorageSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.295：`--header-selftest`も窓を出しません（Phase 288）。
        // **`TimelineComponent`は作りますが、画面には出しません**
        // ——位置を聞くだけなので、ピア（OSの窓）は要りません
        if (TrackHeaderLayoutSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.325：`--timerange-selftest`も窓を出しません（Phase 314）。
        // 押す操作は**マウスの出来事を作って**渡します（`TimeRangeSelfTest.h`）
        if (TimeRangeSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.316：`--audio-selftest`も窓を出しません（Phase 309）。
        // **本物のデバイスは開きます**——踏みたいのがオーディオスレッドの
        // 振る舞いなので、開かずには確かめられません（`AudioDeviceSelfTest.h`）
        if (AudioDeviceSelfTest::runIfRequested (commandLine))
        {
            quit();
            return;
        }

        // 8.262：`--sandbox-editor-test`は**窓を出します**（GUIの話なので、
        // 出さずには確かめられません。Phase 269）。配色と言語だけ先に読みます
        if (commandLine.contains ("--sandbox-editor-test"))
        {
            Localise::loadFromSettings();
            AppColours::loadThemeFromSettings();
            lookAndFeel = AppColours::createLookAndFeel();
            juce::Desktop::getInstance().setDefaultLookAndFeel (lookAndFeel.get());

            SandboxSelfTest::runEditorTestIfRequested (commandLine);
            quit();
            return;
        }

        // 8.163：**表示の言語**（Phase 201／本人の要望。仕様書6章）。
        //
        // **いちばん先に読むこと。** これより後は`utf8()`——画面に出る日本語が
        // 通る唯一の道——が言語を見て訳します。読む前に通ったぶんは
        // **日本語のまま焼き付いてしまう**（配色を先に読むのとまったく同じ理由）。
        // すぐ下の起動画面の文字が、その最初の1つです
        Localise::loadFromSettings();

        // 仕様書6.2：配色（Phase 35）。**ウィンドウを作る前に読み込むこと。**
        // 各コンポーネントはコンストラクタで色を覚え込むものがあるため、
        // 後から読むとテーマが半分しか反映されない（AppColours.hの説明参照）。
        AppColours::loadThemeFromSettings();

        // Phase 53：JUCEの既定LookAndFeelはダーク配色（文字が白）。
        // 色を指定していない部品は、ライトテーマの明るい地に白文字を描いてしまうため、
        // パレットから作った配色を既定に据える（8.1のA1・HANDOVER 1.34）。
        // **ウィンドウを作る前に据えること**：DocumentWindowの地の色も、
        // 既定のLookAndFeelから引いている（下のMainWindowのコンストラクタ）。
        lookAndFeel = AppColours::createLookAndFeel();
        juce::Desktop::getInstance().setDefaultLookAndFeel (lookAndFeel.get());

        // 8.151：起動画面（Phase 189／改善案⑰）。**ここから先は非同期です。**
        //
        // `initialise()`は**メッセージループが回り始める前**に呼ばれます。
        // この中で重い初期化まで済ませてしまうと、起動画面は「出したのに
        // 一度も描かれないまま消える」ことになります。
        //
        // 出すだけ出して`initialise()`を抜け、続きは`callAsync()`で
        // ループが回り始めてから行います
        splash = std::make_unique<SplashWindow>();
        splash->setStatus (utf8 ("起動しています..."));

        commandLineProjectFile = findProjectFileInCommandLine();

        juce::MessageManager::callAsync ([this] { continueStartup(); });
    }

    //==========================================================================
    /** 8.151：起動画面が出てから走る、重いほうの初期化（Phase 189／⑰）。

        ```
        起動画面 ──▶ プロジェクト選択画面 ──▶ プロジェクト
        ```

        **選択画面を飛ばす道が2つあります**（`ProjectChooser.h`）。 */
    void continueStartup()
    {
        if (splash != nullptr)
            splash->setStatus (utf8 ("画面とオーディオを準備しています..."));

        // ここが重い（`AudioEngine::initialise()`、プラグイン一覧、ビューの組み立て）。
        // **まだ見えません**——`revealMainWindow()`で初めて出ます
        mainWindow.reset (new MainWindow (getApplicationName()));

        auto* mainComponent = getMainComponent();

        if (mainComponent == nullptr)
        {
            quit();
            return;
        }

        // 仕様書5.1：`.ms1`をダブルクリックして起動された場合。**開くものが決まっている**ので、
        // 「どれで始めるか」を訊く意味がない
        if (commandLineProjectFile != juce::File())
        {
            mainComponent->loadProjectFile (commandLineProjectFile);
            revealMainWindow();
            return;
        }

        // 仕様書5.1：前回異常終了している場合。**先に選択画面を出すと、
        // その答えを「復元しますか」で捨てることになります**（`ProjectChooser.h`）
        if (mainComponent->hasRecoverableAutoSave())
        {
            revealMainWindow();
            return;
        }

        if (splash != nullptr)
        {
            splash->closeAndThen ([this]
            {
                splash = nullptr;
                showProjectChooser();
            });

            return;
        }

        showProjectChooser();
    }

    /** 起動画面を畳んでから、メインウィンドウを出す。 */
    void revealMainWindow()
    {
        auto reveal = [this]
        {
            splash = nullptr;

            if (mainWindow != nullptr)
                mainWindow->revealAfterStartup();

            // 8.151：**見えてから訊く**（Phase 189）。前回の異常終了と
            // オートセーブの復元は、Phase 188までMainComponentのコンストラクタで
            // 出していました（起動画面より前に出てしまう）
            if (auto* mainComponent = getMainComponent())
                mainComponent->beginStartupChecks();
        };

        if (splash != nullptr)
        {
            splash->closeAndThen (std::move (reveal));
            return;
        }

        reveal();
    }

    void showProjectChooser()
    {
        chooserWindow = std::make_unique<ProjectChooserWindow>();

        chooserWindow->onChosen = [this] (const ProjectChooser::Result& result)
        {
            // 1.5：**自分のコールバックの中で自分を破棄しない。**
            // ここで`chooserWindow`を消すと、呼び出し元（ボタンや×）の
            // スタックが解放済みメモリを触る
            juce::MessageManager::callAsync ([this, result]
            {
                if (result.type == ProjectChooser::Result::Type::quit)
                {
                    chooserWindow = nullptr;
                    applyProjectChoice (result);
                    return;
                }

                // 8.254：**選択画面は閉じません**（Phase 262／本人の要望）。
                //
                // Phase 261まではここで窓を消していたので、**本体が出るまでの
                // 数秒〜十数秒、画面に何も無い**状態でした
                // （本人の報告：「落ちてしまったのかと思うこともある」）。
                //
                // 中身だけ「読み込み中」へ差し替えて、**窓は残します。**
                chooserWindow->showLoading (utf8 ("プロジェクトを読み込んでいます..."),
                                             describeChoice (result));

                // **一度メッセージループへ返してから始めること。**
                // ここで続けて読み込みに入ると、差し替えた中身が**一度も描かれないまま**
                // 数秒固まります（8.151で起動画面に踏んだのとまったく同じ形）。
                //
                // `callAsync`ではなく**少しだけ待つ**のは、
                // 描き直しの順番が保証されないためです——60msなら目には見えません
                juce::Timer::callAfterDelay (60, [this, result]
                {
                    applyProjectChoice (result);
                });
            });
        };
    }

    /** 8.254：読み込み中に出す「何を開いているか」（Phase 262）。 */
    static juce::String describeChoice (const ProjectChooser::Result& result)
    {
        switch (result.type)
        {
            case ProjectChooser::Result::Type::fromTemplate:
                return result.entry.name;

            case ProjectChooser::Result::Type::openFile:
                return result.file.getFileNameWithoutExtension();

            case ProjectChooser::Result::Type::quit:
            default:
                return {};
        }
    }

    void applyProjectChoice (const ProjectChooser::Result& result)
    {
        auto* mainComponent = getMainComponent();

        if (mainComponent == nullptr)
        {
            quit();
            return;
        }

        switch (result.type)
        {
            case ProjectChooser::Result::Type::fromTemplate:
                mainComponent->applyTemplate (result.entry);
                break;

            case ProjectChooser::Result::Type::openFile:
                mainComponent->loadProjectFile (result.file);
                break;

            case ProjectChooser::Result::Type::quit:
                // 選ばずに閉じた。**空のプロジェクトで開き直さない**——
                // それでは「閉じる」が「選ぶ」と同じ結果になります（`ProjectChooser.h`）
                quit();
                return;
        }

        // 8.254：**ここまでが「重いほう」**（Phase 262）。
        //
        // 上の1行（`applyTemplate()`／`loadProjectFile()`）の中で、プロジェクトに
        // 入っている市販プラグインを1つずつ作り直します。13個のプロジェクトで
        // 10秒近くかかることがあり、**そのあいだメッセージスレッドは塞がったまま**です。
        //
        // 読み込みが終わったので、**本体を出す前に文字だけ差し替えます。**
        // 画面を組み立てるのにもまだ少しかかるので、**そこも無言にしない**
        if (chooserWindow != nullptr)
            chooserWindow->setLoadingMessage (utf8 ("画面を開いています..."));

        juce::Timer::callAfterDelay (60, [this]
        {
            revealMainWindow();

            // **本体を出してから消すこと。** 先に消すと、そこで一瞬また
            // 画面から何も無くなります——直したかったのはまさにそれです
            chooserWindow = nullptr;
        });
    }

    //==========================================================================
    MainComponent* getMainComponent() const
    {
        return mainWindow != nullptr
                   ? dynamic_cast<MainComponent*> (mainWindow->getContentComponent())
                   : nullptr;
    }

    /** コマンドライン引数からプロジェクトファイルを探す。無ければ空のFile。 */
    juce::File findProjectFileInCommandLine() const
    {
        for (const auto& parameter : getCommandLineParameterArray())
        {
            // 相対パスをjuce::Fileへ直接渡すとアサーションで止まるため、
            // 絶対パスでない場合はカレントディレクトリを基準に解決する。
            const auto path = parameter.unquoted();

            if (path.isEmpty())
                continue;

            const auto file = juce::File::isAbsolutePath (path)
                                  ? juce::File (path)
                                  : juce::File::getCurrentWorkingDirectory().getChildFile (path);

            // 新旧どちらの拡張子でも開く（判定はモデル側の1箇所にある）
            if (file.existsAsFile() && ProjectModel::isProjectFile (file))
                return file; // 複数渡された場合は最初の1つだけ（1ウィンドウ＝1プロジェクト）
        }

        return {};
    }

    void shutdown() override
    {
        // 8.322：**いちばん先に、開いているメッセージボックスを全部閉じること**（Phase 312）。
        // この関数を抜けたあとで`MessageManager`が消えます。箱が開いたままだと、
        // 片付けで閉じた箱が別スレッドから`MessageManager`を作り直して落ちます
        // （1.0.1で実際に落ちていました。`AppMessageBox.h`）
        AppMessageBox::closeAll();

        chooserWindow = nullptr;
        splash = nullptr;
        mainWindow = nullptr;
        sandboxWorker = nullptr;

        // **ウィンドウを壊してから外すこと。** LookAndFeelを使っている
        // コンポーネントが残ったまま解放すると、JUCEのアサートで止まる。
        juce::Desktop::getInstance().setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override
    {
        // 仕様書5.1：未保存の変更がある状態で閉じられた場合は、確認してから終了する。
        // 確認ダイアログは非同期なので、いったんこの関数を抜けて、
        // ユーザーが選択した時点でquit()を呼ぶ形になる。
        //
        // 8.151：**まだ選択画面が出ている間は訊きません**（Phase 189）。
        // その時点のプロジェクトは、選ばれる前の空のもので、捨てて困りません
        if (chooserWindow == nullptr && mainWindow != nullptr)
        {
            if (auto* mainComponent = getMainComponent())
            {
                mainComponent->confirmDiscardChanges ([this] (bool shouldQuit)
                {
                    if (shouldQuit)
                        quit();
                });

                return;
            }
        }

        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour (juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            // 8.229：**窓のアイコンは自分で渡すこと**（Phase 249／本人の報告）。
            //
            // `juce_add_gui_app`の`ICON_BIG`は**WindowsのexeとmacOSのバンドル**に効きますが、
            // **Linuxには効きません**——juceaideが作るのは`.ico`と`.icns`だけで、
            // X11の窓に付けるアイコンはアプリが渡す決まりです。
            //
            // 渡さないと、デスクトップ環境が「実行ファイル」の既定の絵を出します
            // （**歯車**が出るのはこれ）。AppImageに`.desktop`とアイコンを入れてあっても、
            // **走り出したあとの窓とタスクバーはそれとは別**です。
            //
            // 8.234：**渡すのは`revealAfterStartup()`**（Phase 251）。
            // ここで`DocumentWindow::setIcon()`を呼んでいましたが、
            // **あれはJUCEが自分で描くタイトルバーの絵**で、X11へは届きません
            // （`AppIcon::applyToWindow()`の説明）。ピアは窓が出てから出来ます

            setContentOwned (new MainComponent(), true);

            setResizable (true, true);

            // 設計書2.2の5領域（トップバー／中央エリア／下部エディタパネル）が
            // 成り立たなくなる大きさまでは縮めさせない。Phase 16でエディタパネルが
            // 画面下部を占めるようになったため、際限なく縮むと中央エリアが潰れる。
            setResizeLimits (800, 560, 10000, 10000);

            centreWithSize (getWidth(), getHeight());

            // 8.151：**ここでは出しません**（Phase 189／改善案⑰）。
            //
            // Phase 188までは、この位置で`setVisible(true)`→`setFullScreen(true)`と
            // していました。**組み立ての途中が見えます**——中央寄せの小さい大きさで
            // 一度描かれ、その絵が大きな窓の左上に貼り付いたまま、
            // 最大化が終わるのを待つ形になっていました。
            //
            // 出すのは`revealAfterStartup()`で、**中身が全部そろってから**です
        }

        /** 8.151：組み立てが終わったので画面へ出す（Phase 189）。 */
        void revealAfterStartup()
        {
            if (isVisible())
                return;

            setVisible (true);

            // 8.234：**ここで窓へアイコンを渡します**（Phase 251／本人の報告）。
            // `setVisible(true)`で初めてピアが出来るので、**これより前では届きません**
            AppIcon::applyToWindow (*this);

            // 設計書2.2：起動時は画面いっぱいに開く（Phase 63／8.1のC8）。
            //
            // **`centreWithSize()`の後に呼ぶこと。** 「元のサイズに戻す」を押したときに
            // 戻る先が、その中央寄せの大きさになる。
            //
            // ネイティブのタイトルバーを使っているので、これは**最大化**で、
            // タイトルバーの消えるフルスクリーンではない
            // （メニューとウィンドウ操作が出ていないと、閉じることもできなくなる）。
            setFullScreen (true);

            // 8.235：**最大化のあともう一度渡します**（Phase 251）。
            // ピアが作り直されたときに消えないための保険で、128pxなら安いものです
            AppIcon::applyToWindow (*this);

            toFront (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    // **mainWindowより先に宣言すること。** メンバは宣言と逆の順で壊れるので、
    // ここに置いておくと「ウィンドウ→LookAndFeel」の順で解放される
    // （逆だと、LookAndFeelを使っている部品が残ったまま解放されてアサートで止まる）。
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel;

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<SandboxWorker> sandboxWorker;

    // 8.151：起動の道具（Phase 189／改善案⑰）。**どちらも一時的**で、
    // メインウィンドウが出た時点で消えます
    std::unique_ptr<SplashWindow> splash;
    std::unique_ptr<ProjectChooserWindow> chooserWindow;

    juce::File commandLineProjectFile;
};

//==============================================================================
START_JUCE_APPLICATION (PersonalDAWApplication)
