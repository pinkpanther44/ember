#include "MainComponent.h"
#include "AppColours.h"
#include "AppSettings.h"
#include "StorageLocations.h"   // 設計書2.3.8：保存先の設定（Phase 57）
#include "NameEntry.h"          // 8.33：名前をひとつ入れてもらうダイアログ（Phase 72）
#include "ProjectChooser.h"     // 8.151：プロジェクト選択画面（Phase 189／⑰）
#include "RecentProjects.h"     // 8.151：最近開いたプロジェクト（Phase 189／⑰）
#include "Utf8.h"

namespace
{
    /** 仕様書4.2：エディタをポップアウトするためのウィンドウ（Phase 16）。

        中身（ピアノロール）は`setContentNonOwned`で借りるだけで、所有しない。
        メインウィンドウへ戻すときに、そのまま親を付け替えられるようにするため。 */
    class EditorWindow : public juce::DocumentWindow
    {
    public:
        EditorWindow (const juce::String& name, juce::Colour backgroundColour)
            : DocumentWindow (name, backgroundColour, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
        }

        std::function<void()> onCloseRequested;

        void closeButtonPressed() override
        {
            // HANDOVER 1.5：**ウィンドウが自分のコールバックの中で自分を破棄してはいけない。**
            // 呼び出し元のスタックが解放済みメモリを触ることになる。
            // ここでは依頼を投げるだけにして、実際の破棄は呼ばれた側が
            // このスタックを抜けてから行う（MainComponent::dockEditor）。
            if (onCloseRequested != nullptr)
                onCloseRequested();
        }
    };

    // 設計書2.5：アプリ全体の設定として保存するキー
    const juce::String editorWindowBoundsKey  { "editorWindowBounds" };
    const juce::String editorPanelHeightKey   { "editorPanelHeight" };
    const juce::String browserPanelWidthKey   { "browserPanelWidth" };
    const juce::String inspectorPanelWidthKey { "inspectorPanelWidth" };
    const juce::String browserPanelOpenKey    { "browserPanelOpen" };
    const juce::String inspectorPanelOpenKey  { "inspectorPanelOpen" };
    const juce::String metronomeEnabledKey    { "metronomeEnabled" };
    const juce::String metronomeGainKey       { "metronomeGainPercent" }; // 0〜100（intで持つため）
    const juce::String countInBarsKey         { "countInBars" };
}

MainComponent::MainComponent()
{
    // 仕様書7章：Undo/Redoをはじめとするコマンドを登録し、ショートカットを効かせる。
    // addKeyListener()を入れないと、メニューには出てもキー入力では反応しない。
    commandManager.registerAllCommandsForTarget (this);

    // 実行先を明示的に固定する。指定しない場合、JUCEは「今フォーカスがある
    // コンポーネントの親をたどってApplicationCommandTargetを探す」という解決をするため、
    // プラグインのエディタウィンドウなど別ウィンドウにフォーカスがあると見つからなくなる。
    commandManager.setFirstCommandTarget (this);

    // 仕様書6.2：前回変更したショートカットの割り当てを復元する（Phase 47）。
    // **既定を作った後（registerAllCommandsForTarget）でなければ効かない。**
    // 復元は既定の上から差し替える形なので、順番が逆だと既定に上書きされる。
    PreferencesDialog::loadKeyMappings (commandManager);

    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (arrangeView);

    // Phase 66：曲名はメニュー行の右へ（トップバー廃止。8.27）
    projectNameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    projectNameLabel.setJustificationType (juce::Justification::centredRight);
    projectNameLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    addAndMakeVisible (projectNameLabel);

    // pianoRollView・chordPadPanel・consoleViewはここでは親に付けない。
    // **下部パネルの中身として置く**ので、`editorPanel.setContent()`が親になる
    // （Phase 16／Phase 66でConsoleも同じ扱いにした）。

    // 設計書4.2：アレンジ画面でMIDIのノートをダブルクリックしたら、ピアノロールで開く（Phase 15）。
    // 対象を先に伝えてからページを切り替える。切り替えで走る
    // PianoRollViewのvisibilityChanged()は選択を引き継ぐ作りなので、この順なら
    // 「開いた瞬間に別のトラックへ戻る」ことがない。
    //
    // 8.91：**渡すのはトラックと「見たい時刻」**（Phase 131）。
    // クリップという入れ物が無くなったので、番号ではなく時刻で指す
    arrangeView.onMidiClipDoubleClicked = [this] (int trackIndex, double timelineSeconds)
    {
        // コードパッドを出している最中でも、MIDIを開いたらピアノロールへ戻す
        setEditorContent (EditorContent::pianoRoll);
        pianoRollView.showTrack (trackIndex, timelineSeconds);
        setEditorPanelOpen (true);
    };


    // 8.49：オーディオクリップの右クリックから、オーディオエディタで開く（Phase 88）。
    // **MIDIクリップのダブルクリックと同じ形**：対象を先に選んでから中身を切り替える
    arrangeView.onAudioClipEditorRequested = [this] (int trackIndex, int clipIndex)
    {
        if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
            return;

        // **選択を先に動かすこと。** オーディオエディタは「いま選んでいるクリップ」を
        // 出す作りなので（8.39）、ここで選び直せば中身は勝手に決まる
        selection.selectClip (project.getTrack (trackIndex).getId(), clipIndex, false);

        setEditorContent (EditorContent::audioEditor);
        setEditorPanelOpen (true);
    };
    // 設計書2.3.5：コード区間をダブルクリックしたらコードパッドを開く（Phase 43）。
    // MIDIクリップ→ピアノロールと同じ操作感にしてある。
    arrangeView.onChordRegionDoubleClicked = [this] (double startTimeSeconds)
    {
        audioEngine.setPlayheadSeconds (startTimeSeconds);
        setPlayheadDisplay (startTimeSeconds);
        transportBar.setPlayheadSeconds (startTimeSeconds);

        setEditorContent (EditorContent::chordPad);
        chordPadPanel.setInsertPosition (startTimeSeconds);
        setEditorPanelOpen (true);
    };


    //==========================================================================
    // 設計書2.3.4：オーディオエディタ（Phase 79／8.39）

    // ルーラーのクリックでのシーク。**アレンジ画面・ピアノロールと同じ入口を通す**（8.33）
    audioEditorView.onSeekRequested = [this] (double seconds) { seekTo (seconds); };

    // 「まだ何も無い」表示からの取り込み。**選んでいるトラックの、再生位置へ置く**
    audioEditorView.onImportRequested = [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            utf8 ("取り込むオーディオファイルを選んでください"),
            juce::File(), "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");

        const auto flags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync (flags, [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult() == juce::File())
                return;

            arrangeView.importAudioFile (fc.getResult(), selection.getTrackId(),
                                          audioEngine.getPlayheadSeconds());

            // 置いたクリップをそのまま出す（取り込んだのに空のままだと、失敗に見える）
            refreshEditorContent();
        });
    };

    // 「まだ何も無い」表示からの録音。**そのトラックを録音待機にしてから**転がす
    audioEditorView.onRecordRequested = [this]
    {
        auto track = project.findTrackById (selection.getTrackId());

        if (track.state.getParent().isValid() && ! track.isArmed())
        {
            project.beginAction (utf8 ("録音待機の切り替え"));
            track.setArmed (true, &project.getUndoManager());   // 画面はモデルを見て追う（1.15）
        }

        recordButtonClicked();
    };

    // 設計書2.3.5：コードパッド（Phase 43）。
    // 挿入位置は再生位置で、パッドを押すと1小節ぶん進む（仕様書5.11.2）。
    chordPadPanel.onInsertPositionChanged = [this] (double newPositionSeconds)
    {
        audioEngine.setPlayheadSeconds (newPositionSeconds);

        // 停止中はタイマーが回っていないので、表示は自分で合わせる（HANDOVER 1.28）
        setPlayheadDisplay (newPositionSeconds);
        transportBar.setPlayheadSeconds (newPositionSeconds);
    };

    // 設計書2.3.5：コードトラックを選んだらコードパッドに切り替える（Phase 43）
    selection.addChangeListener (this);

    // 仕様書4.4：ブラウザからタイムラインへプラグインをドロップしたとき（Phase 21）。
    // 落とした先のトラックが挿し先になる（選択状態は関係しない）。
    arrangeView.onPluginDropped = [this] (const juce::String& trackId,
                                           const juce::PluginDescription& description)
    {
        insertPluginIntoTrack (trackId, description);
    };

    // 設計書2.2：エディタパネル（Phase 16）。ピアノロールをその中身として置く。
    // **先にパネルを非表示で親へ付けてから**中身を渡すこと。順番を逆にすると、
    // 中身が「表示中」の状態で始まってしまい、あとでパネルを開いても
    // 中身側のvisibilityChanged()が飛ばない（値が変わらないため）。
    addChildComponent (editorPanel); // 既定は閉じた状態
    editorPanel.setContent (&pianoRollView);
    editorPanel.onCloseClicked = [this] { setEditorPanelOpen (false); };
    editorPanel.onPopOutClicked = [this] { popOutEditor(); };
    editorPanel.onHeightChangeRequested = [this] (int newHeight) { setEditorPanelHeight (newHeight); };

    // 設計書2.5：前回のパネル高さを復元する
    editorPanelHeight = juce::jmax (EditorPanel::minimumHeight,
                                     AppSettings::getInt (editorPanelHeightKey, editorPanelHeight));

    //==========================================================================
    // 設計書2.2：左右のサイドパネル（Phase 17）
    browserPanelWidth = juce::jmax (BrowserPanel::minimumWidth,
                                     AppSettings::getInt (browserPanelWidthKey, browserPanelWidth));
    inspectorPanelWidth = juce::jmax (InspectorPanel::minimumWidth,
                                       AppSettings::getInt (inspectorPanelWidthKey, inspectorPanelWidth));

    browserPanel.onPluginChosen = [this] (const juce::PluginDescription& description)
    {
        insertPluginFromBrowser (description);
    };

    browserPanel.onAudioFileChosen = [this] (const juce::File& file)
    {
        // Phase 66：ページの概念が無くなったので、アレンジ画面へ切り替える必要は無い
        arrangeView.importAudioFile (file);
    };

    browserResizer.getCurrentSize = [this] { return browserPanelWidth; };
    browserResizer.onSizeDragged = [this] (int newWidth) { setBrowserPanelWidth (newWidth); };

    inspectorResizer.getCurrentSize = [this] { return inspectorPanelWidth; };
    inspectorResizer.onSizeDragged = [this] (int newWidth) { setInspectorPanelWidth (newWidth); };

    addChildComponent (browserPanel);
    addChildComponent (browserResizer);
    addChildComponent (inspectorPanel);
    addChildComponent (inspectorResizer);

    transportBar.onBrowserToggled = [this] { setBrowserPanelOpen (! browserPanel.isVisible()); };
    transportBar.onInspectorToggled = [this] { setInspectorPanelOpen (! inspectorPanel.isVisible()); };


    // 設計書2.2の標準レイアウトは左右パネルを開いた状態。前回の開閉状態は覚えておく
    setBrowserPanelOpen (AppSettings::getInt (browserPanelOpenKey, 1) != 0);
    setInspectorPanelOpen (AppSettings::getInt (inspectorPanelOpenKey, 1) != 0);

    // 設計書2.2：下部パネルの中身を選ぶ（Phase 66／8.27）。
    // **EditorとConsoleは同じ枠を取り合う。** どちらのボタンも「その中身にする／
    // 既にそれなら閉じる」という同じ形にしてあるので、処理は1箇所（`toggleEditorContent`）
    transportBar.onEditorToggled  = [this] { toggleEditorContent (lastEditorContent); };
    transportBar.onConsoleToggled = [this] { toggleEditorContent (EditorContent::console); };

    // 8.71：コードパッドは**別の窓**（Phase 111／改善案⑭）。
    // EditorとConsoleが取り合っている枠には入れない——ピアノロールと**同時に**見たいため
    transportBar.onChordPadToggled = [this] { toggleEditorContent (EditorContent::chordPad); };

    transportBar.onPlayButtonClicked = [this] { playButtonClicked(); };
    transportBar.onRecordButtonClicked = [this] { recordButtonClicked(); };

    // 仕様書5.9：ループ再生（Phase 48）。ボタンもショートカットも同じ経路を通す
    transportBar.onLoopToggled = [this]
    {
        project.beginAction (utf8 ("ループの入切"));
        project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());
        applyLoopSettings();
    };

    // ルーラーの帯をドラッグして範囲を変えたとき
    arrangeView.onLoopChanged = [this] { applyLoopSettings(); };

    // 仕様書6.2：複数選択が変わったら、コマンドの有効/無効を作り直す（Phase 51）
    arrangeView.onClipSelectionChanged = [this] { commandManager.commandStatusChanged(); };

    // 8.60：トラックヘッダーの「i」でインスペクタを開く／閉じる（Phase 97／改善案①）。
    //
    // **同じトラックをもう一度押したときだけ閉じます。**
    // どのトラックでも閉じる形にすると、隣のトラックを見ようとして押したときに
    // パネルごと消えてしまい、もう一度押し直すことになります
    arrangeView.onInspectorRequested = [this] (bool allowClose)
    {
        setInspectorPanelOpen (allowClose ? ! inspectorPanel.isVisible() : true);
    };

    // 8.60：Consoleの空きを右クリックしてトラックを追加（Phase 97／改善案㉚）。
    // **メニューを組むのはArrangeView**（入口は増やしても中身は1つ。8.27）
    consoleView.onAddTrackRequested = [this] (juce::Rectangle<int> anchorScreenBounds)
    {
        arrangeView.showAddTrackMenuAt (anchorScreenBounds);
    };

    // 仕様書6.2：ツールバーのボタンから選ばれたツールも、ここで両方の画面へ配る（Phase 52）。
    // **ショートカット（1〜4）と同じ経路を通す**ので、どちらで選んでも同じ結果になる
    arrangeView.onEditToolSelected = [this] (EditTool tool)
    {
        setEditTool (tool);
        commandManager.commandStatusChanged();   // メニューのチェックも合わせる
    };

    // 仕様書6.2：ピアノロールの中で選ばれたツール（空きグリッドの右クリック）も、
    // ツールバーのボタンと同じ経路を通す（Phase 69／8.29）
    pianoRollView.onEditToolSelected = [this] (EditTool tool)
    {
        setEditTool (tool);
        commandManager.commandStatusChanged();
    };

    // 仕様書6.2：クリップのメニューからの「複製」（Phase 71）。
    // ショートカット（D）と同じ実装を通す
    arrangeView.onDuplicateClipRequested = [this] { duplicateSelectedClip(); };

    // 8.76：クリップのメニューからの分割・結合（Phase 116/改善案④㉟）。
    // **ショートカットとまったく同じ関数を通す**（8.32）
    arrangeView.onSplitClipRequested = [this] { splitSelectedClip(); };
    arrangeView.onMergeClipRequested = [this] { mergeSelectedClip(); };

    // 8.77：インスペクタのトランスポーズ（Phase 117/改善案㉝）。
    // **効かせる先を決めているのはアレンジ画面**なので、そこへ渡す（メニューと同じ経路）
    inspectorPanel.onTransposeRequested = [this] (int semitones)
    {
        arrangeView.transposeSelection (semitones);
    };

    // 8.147：トランスポーズ済みのファイルが出来たとき（Phase 185／改善案㉞）。
    //
    // **読み直させるのを忘れないこと。** 出来ていても、
    // `prepareClipsForPlayback()`が呼び直されるまでは素のファイルが鳴り続けます
    clipAudioRenderer.onFinished = [this] (const juce::String& message)
    {
        audioEngine.refreshPlaybackSources();
        arrangeView.showStatusMessage (message);
    };

    // 仕様書5.9：ルーラーの右クリックメニューからのマーカー挿入（Phase 50）。
    // ショートカットからの挿入と同じ関数を通す
    arrangeView.onInsertMarkerRequested = [this] (double timeSeconds, bool askForName)
    {
        insertMarkerAt (timeSeconds, askForName);
    };

    // 仕様書5.9：ピアノロールのルーラーからの要求（Phase 72／8.33）。
    // **アレンジ画面と同じ入口を通す**ので、どちらの画面から触っても結果が揃う
    pianoRollView.onSeekRequested = [this] (double timelineSeconds) { seekTo (timelineSeconds); };
    pianoRollView.onLoopChanged = [this] { applyLoopSettings(); };

    pianoRollView.onInsertMarkerRequested = [this] (double timeSeconds, bool askForName)
    {
        insertMarkerAt (timeSeconds, askForName);
    };

    pianoRollView.onChordRegionDoubleClicked = [this] (double startTimeSeconds)
    {
        seekTo (startTimeSeconds);
        setEditorContent (EditorContent::chordPad);
        chordPadPanel.setInsertPosition (startTimeSeconds);
        setEditorPanelOpen (true);
    };

    // 仕様書5.9：先頭へ戻す（Phase 30）。停止中でも時間表示を合わせる必要があるため、
    // シークしたら必ずここを通してトランスポートバーへ伝える。
    transportBar.onGoToStartClicked = [this]
    {
        audioEngine.setPlayheadSeconds (0.0);
        setPlayheadDisplay (0.0);
        transportBar.setPlayheadSeconds (0.0);
    };

    // 仕様書5.9：ルーラーのクリックで動いた再生位置も、時間表示へ反映する（Phase 30）。
    // **タイマーは再生中しか回っていない**ので、これが無いと停止中のシークで表示が固まる。
    // 8.90：**シークの入口は`seekTo()`ひとつ**（Phase 130）。
    //
    // Phase 30まで、ここはトランスポートバーとコードパッドにしか配っていませんでした
    // （その頃はそれで足りていた）。**ピアノロールに再生カーソルが載った時点で
    // 足りなくなっていた**のに、ピアノロール側は`seekTo()`を通す作りにしたので、
    // **アレンジ画面から動かしたときだけ追従しない**、という食い違いが残っていました。
    //
    // 「入口が2つあるものは、片方だけ直して食い違う」（8.12）の実例です。
    arrangeView.onPlayheadMoved = [this] (double seconds) { seekTo (seconds); };

    // 設計書2.2：テンポ・拍子の編集（Phase 26。Phase 30でトランスポートバーへ移動）。
    //
    // ルーラーの小節線はテンポと拍子から計算しているが、**画面を描き直す指示は要らない**：
    // TimelineComponentはプロジェクトのルートを購読しているので（Phase 22a・1.15）、
    // プロパティが変わればそのまま追従する。
    transportBar.onTempoChanged = [this] (double newTempo)
    {
        project.beginAction (utf8 ("テンポの変更"));
        project.setTempo (newTempo, &project.getUndoManager());
        // Phase 141：メトロノームへの伝達はValueTreeで拾う（8.103）。ここで呼ばないこと
    };

    transportBar.onTimeSignatureChanged = [this] (juce::String newTimeSignature)
    {
        project.beginAction (utf8 ("拍子の変更"));

        // 受け付けられなかった場合は、表示を現在の値へ戻す
        // （打ち間違いがそのまま残ると、直したつもりで直っていない状態になる）
        if (! project.setTimeSignature (newTimeSignature, &project.getUndoManager()))
            transportBar.setTempoAndTimeSignature (project.getTempo(), project.getTimeSignature());

        // Phase 141：メトロノームへの伝達はValueTreeで拾う（8.103）。ここで呼ばないこと
    };

    // 仕様書5.11.1：プロジェクトのキー（Phase 63／8.1のC9）。
    // **入口はコードパッドの上段とここの2つ。** 値の実体はコードトラックにあるので、
    // どちらで変えてもモデルは1つ（`ProjectModel::setProjectKey()`）。
    // 表示だけ両方へ配り直す（1.27）。
    transportBar.onProjectKeyChanged = [this] (int root, bool minor)
    {
        Scale key;
        key.root = root;
        key.minor = minor;

        project.beginAction (utf8 ("キーの変更"));

        if (project.setProjectKey (key, &project.getUndoManager()))
            applyProjectKeyToViews();
    };

    // 仕様書5.5・5.9：編集の刻み（Phase 54／Phase 55で入口を2つに増やした）。
    // **Undoには積まない**（`setSnapGrid()`の説明を参照）ので、beginAction()も呼ばない。
    //
    // **入口はアレンジ画面とピアノロールの2つ**あるので、ツールと同じく
    // 「決めるのはここ1箇所、両方へ配る」形にしてある（1.27・8.15）。
    // 片方だけを更新すると、もう片方のコンボボックスに古い値が残る。
    arrangeView.onSnapGridSelected  = [this] (SnapGrid newGrid) { applySnapGrid (newGrid); };
    pianoRollView.onSnapGridSelected = [this] (SnapGrid newGrid) { applySnapGrid (newGrid); };

    // メトロノーム（Phase 38）。プロジェクトではなくアプリ全体の設定にしてある
    // （「今このセッションで拍が欲しいか」であって、曲の内容ではないため）。
    transportBar.onMetronomeToggled = [this] (bool shouldBeEnabled)
    {
        audioEngine.setMetronomeEnabled (shouldBeEnabled);
        AppSettings::setInt (metronomeEnabledKey, shouldBeEnabled ? 1 : 0);
    };

    // 仕様書5.4：クリック音量とカウントイン（Phase 39）。
    // どちらも「今このセッションの好み」なので、プロジェクトではなくアプリ設定に置く。
    transportBar.onMetronomeSettingsRequested = [this] (juce::Rectangle<int> screenBounds)
    {
        showMetronomeSettingsMenu (screenBounds);
    };

    transportBar.setTempoAndTimeSignature (project.getTempo(), project.getTimeSignature());
    applyLoopSettings();   // 仕様書5.9：ループボタンの初期表示（Phase 48）
    applyProjectKeyToViews();   // 仕様書5.11.1：キーの初期表示（Phase 63／8.1のC9）

    // 仕様書5.7：マスター音量（Phase 37）。Consoleのマスターフェーダーと同じ値を触る。
    // 区切りとTouch/Latchの扱いも、MasterStripComponentと同じにしてある。
    transportBar.onMasterVolumeDragStart = [this]
    {
        project.beginAction (utf8 ("マスター音量の変更"));
        audioEngine.beginAutomationTouch ({}, AutomationTargets::volume); // マスターはtrackId空文字
    };
    transportBar.onMasterVolumeDragEnd = [this]
    {
        audioEngine.endAutomationTouch ({}, AutomationTargets::volume);
    };
    transportBar.onMasterVolumeChanged = [this] (float newVolumeDb)
    {
        project.setMasterVolumeDb (newVolumeDb, &project.getUndoManager());
        audioEngine.updateMixerSettings();
    };

    transportBar.setMasterVolumeDb (project.getMasterVolumeDb());
    updateProjectSubscription();

    addAndMakeVisible (transportBar);

    // メトロノーム（Phase 38）。前回の状態を復元する。
    // **audioEngine.initialise()より前に呼んでも意味が無い**（ノードがまだ無い）ので、
    // 実際にエンジンへ伝えるのは初期化のすぐ後（下）で行う。
    transportBar.setMetronomeEnabled (AppSettings::getInt (metronomeEnabledKey, 0) != 0);

    // 設計書1.5：AudioEngineを初期化する。失敗した場合は画面上にエラーを表示する
    // （オーディオデバイスが無い/使用中等、環境依存の問題は普通に起こり得るため）。
    auto audioError = audioEngine.initialise();
    if (audioError.isNotEmpty())
    {
        audioErrorLabel.setText (utf8 ("オーディオデバイスの初期化に失敗しました: ") + audioError,
                                  juce::dontSendNotification);
        audioErrorLabel.setColour (juce::Label::textColourId, juce::Colours::red);
        addAndMakeVisible (audioErrorLabel);
    }

    // 仕様書5.4：入力デバイスが開けたかどうかが確定するのはinitialise()の後なので、
    // ここで改めてArrangeViewの入力表示（メーター・モニタリングボタン）を更新する。
    arrangeView.refreshInputState();

    // メトロノーム（Phase 38／39）。ノードが出来るのもinitialise()の後なので、ここで伝える。
    audioEngine.setMetronomeEnabled (AppSettings::getInt (metronomeEnabledKey, 0) != 0);
    audioEngine.setMetronomeGain (juce::jlimit (0, 100, AppSettings::getInt (metronomeGainKey, 50)) / 100.0f);
    updateMetronomeTiming();

    updateEditorToggleButtons();   // Phase 66：フッターのEditor／Consoleの初期表示

    // 8.151：**前回の異常終了とオートセーブの復元は、ここでは訊きません**（Phase 189／⑰）。
    // Phase 188まではこの位置で出していましたが、**プロジェクト選択画面より先に
    // 出てしまいます**（コンストラクタは、画面が出るより前に走るため）。
    // メインウィンドウが見えてから`beginStartupChecks()`が呼ばれます

    // 仕様書5.1：未保存の変更があるかどうかをタイトルバーへ反映する
    project.onSavedStateChanged = [this] { updateWindowTitle(); };
    updateWindowTitle();

    // 仕様書5.1：オートセーブ。プラグインの内部状態（設計書3.8）も一緒に保存されるよう、
    // 書き出し直前にエンジンから取り込む。
    autoSave.onBeforeAutoSave = [this] { audioEngine.capturePluginStatesIntoProject(); };
    autoSave.onAutoSaved = [this] (const juce::File&)
    {
        // 裏で保存されたことが分かるよう、時刻付きでステータス欄に出す
        arrangeView.showStatusMessage (utf8 ("自動保存しました (")
                                           + juce::Time::getCurrentTime().formatted ("%H:%M:%S") + ")");
    };

    autoSave.start();

    setSize (760, 480);
}

MainComponent::~MainComponent()
{
    stopTimer();

    // 破棄後に通知が飛んでこないよう、購読は先に外す（Phase 37）
    if (subscribedProjectState.isValid())
        subscribedProjectState.removeListener (this);

    selection.removeChangeListener (this); // Phase 43

    // 仕様書4.2：ポップアウトしたまま終了した場合。次回のために位置を控えてから、
    // **中身を外してウィンドウを破棄する**。ピアノロールはこのクラスのメンバーで、
    // ウィンドウは借りているだけなので、先に返しておかないと親が宙に浮く。
    if (editorWindow != nullptr)
    {
        saveEditorWindowBounds();
        editorWindow->clearContentComponent();
        editorWindow.reset();
    }

    // 破棄されるコンポーネントをコマンドの実行先に残さない
    commandManager.setFirstCommandTarget (nullptr);

    // 仕様書5.1：正常に終了できたので、復旧用のオートセーブは破棄する。
    // 逆に言うと、クラッシュや強制終了ではここが動かず、ファイルが残る。
    // それが次回起動時の「前回は正常終了しなかった」という判断材料になる（設計書3.5と同じ考え方）。
    autoSave.stop();
    autoSave.clearAutoSave();

    // ProjectModelはこのクラスのメンバーだが、コールバックにthisを握らせたままにしないよう
    // 破棄の入口で明示的に外しておく（他のビューでの扱いと揃える）。
    project.onSavedStateChanged = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    // 設計書2.2：メニュー行はフッターと同じ地にする（Phase 73）。
    // **メニューバーは必要な幅しか無い**ので、ここで行全体を塗っておかないと、
    // 左上だけ色の違う四角が出ているように見える（曲名の側が地のままになる）
    auto menuRow = getLocalBounds().removeFromTop (menuRowHeight);

    g.setColour (AppColours::panel);
    g.fillRect (menuRow);

    g.setColour (AppColours::border);
    g.drawLine (0.0f, (float) menuRow.getBottom() - 0.5f,
                 (float) getWidth(), (float) menuRow.getBottom() - 0.5f);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // 設計書2.2：メニューと曲名を同じ行に並べる（Phase 66／8.27）。
    // トップバー1段ぶん（40px）が丸ごと空いたので、フッターが2段になったぶんを吸収できる。
    // **メニューには必要なだけ渡し、残りを曲名へ**：メニュー名が増えても押せなくならない
    {
        auto menuRow = area.removeFromTop (menuRowHeight);

        menuBar.setBounds (menuRow.removeFromLeft (juce::jmin (240, menuRow.getWidth())));
        projectNameLabel.setBounds (menuRow.withTrimmedRight (10));
    }

    // 設計書2.2：トランスポートは画面**下端**（Phase 30）。
    // **エディタパネルより先に取ること。** 順番を逆にすると、エディタを開いたときに
    // トランスポートがその下へ押し出されて画面外へ消える。
    transportBar.setBounds (area.removeFromBottom (TransportBarComponent::barHeight));

    if (audioErrorLabel.isVisible())
        audioErrorLabel.setBounds (area.removeFromTop (24));

    // 設計書2.2：左のインスペクタ、右のブラウザ（Phase 17／**Phase 28で左右を入れ替えた**）。
    // インスペクタは選択中のトラック——つまり左に並ぶトラックヘッダー——の設定を出すので、
    // 見ている対象のすぐ隣に置いたほうが視線の移動が少ない。
    //
    // 8.71：**エディタパネルより先に取ります**（Phase 110／改善案㉜）。
    // Phase 109まで逆で、**下部エディタとConsoleだけが左右いっぱいに広がっていました**。
    // インスペクタを開くとアレンジ画面だけが狭まり、その下のConsoleは幅そのまま——
    // **同じトラックの縦の並びが、上下でずれます**。
    // 先に取れば、サイドパネルは画面の高さいっぱいになり、
    // エディタとConsoleはアレンジ画面と同じ幅に収まります。
    if (inspectorPanel.isVisible())
    {
        auto inspectorArea = area.removeFromLeft (juce::jmin (inspectorPanelWidth, area.getWidth()));
        inspectorPanel.setBounds (inspectorArea);

        // 帯はパネルの右端に重ねて置く。パネル側はresized()でこのぶんの幅を空けている
        inspectorResizer.setBounds (inspectorArea.removeFromRight (PanelResizerBar::thickness));
    }

    if (browserPanel.isVisible())
    {
        auto browserArea = area.removeFromRight (juce::jmin (browserPanelWidth, area.getWidth()));
        browserPanel.setBounds (browserArea);
        browserResizer.setBounds (browserArea.removeFromLeft (PanelResizerBar::thickness));
    }

    // 設計書2.2：エディタパネルはトランスポートのすぐ上に置き、残りが中央エリアになる（Phase 16）。
    // ページ（Arrange/Console/Plugins）より先に場所を取ることで、どのページを
    // 開いていても同じ高さでパネルが出る。
    if (editorPanel.isVisible())
    {
        const int panelHeight = juce::jlimit (EditorPanel::minimumHeight,
                                               juce::jmax (EditorPanel::minimumHeight, area.getHeight()),
                                               editorPanelHeight);
        editorPanel.setBounds (area.removeFromBottom (panelHeight));
    }

    // Phase 66：中央エリアはアレンジ画面だけになった。
    // Consoleは下部パネルの中身なので、ここでは場所を取らない（8.27）
    arrangeView.setBounds (area);
}

//==============================================================================
// 設計書2.2・仕様書4.2：エディタパネル（Phase 16）
//==============================================================================

void MainComponent::setEditorPanelOpen (bool shouldBeOpen)
{
    // 既に目的の状態なら何もしない。ここで中身を更新し直すと、
    // 直前にshowClip()で選んだクリップを選び直してしまう（ダブルクリック経路）。
    if (editorPanel.isVisible() == shouldBeOpen)
        return;

    editorPanel.setVisible (shouldBeOpen);
    updateEditorToggleButtons();   // Phase 66：Editor／Consoleのどちらが灯るかは中身で決まる

    // 開くときは明示的に読み直す。visibilityChanged()頼みにすると、
    // 表示状態が既に一致している場合に通知が飛ばず、古い内容が残る
    if (shouldBeOpen)
        refreshEditorContent();

    resized(); // パネルのぶんだけ、中央エリアの高さが変わる
}

//==============================================================================
// 設計書2.3.5：エディタパネルの中身の切り替え（Phase 43）

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source != &selection)
        return;

    // 8.83：**鍵盤で鳴らす先を、選んでいるトラックに合わせる**（Phase 123/改善案⑬）。
    // アームしたトラックがあればそちらが優先されます（判断はエンジン側に1つだけ）
    audioEngine.setMidiInputTargetTrackId (selection.getTrackId());

    // 8.39：**選んだトラックの種別に合わせて中身を切り替える**（Phase 79／D1）。
    // Phase 78までは「コードトラックならコードパッドへ」だけでした
    updateEditorContentForSelectedTrack();
}

AudioClip MainComponent::getSelectedAudioClip() const
{
    auto track = project.findTrackById (selection.getTrackId());

    if (! track.state.getParent().isValid() || track.getType() != TrackType::Audio)
        return AudioClip (juce::ValueTree());

    // クリップを選んでいればそれ。**番号はここで引くだけ**にして、
    // 覚えるのはValueTreeのほう（AudioEditorViewが持つ。1.32）
    if (selection.getType() == SelectionState::Type::AudioClip
         && juce::isPositiveAndBelow (selection.getClipIndex(), track.getNumClips()))
        return track.getClip (selection.getClipIndex());

    // トラックだけ選んでいるなら、最初のクリップを出す
    if (track.getNumClips() > 0)
        return track.getClip (0);

    return AudioClip (juce::ValueTree());
}

void MainComponent::updateEditorContentForSelectedTrack()
{
    auto track = project.findTrackById (selection.getTrackId());

    if (! track.state.getParent().isValid())
        return;

    // **種別 → 中身**の対応はここ1箇所（1.26と同じ話）。
    //
    // **センド・フォルダ・VCAには出すものがない。** 何も持たないトラックを選んだときは
    // **いま出しているものをそのままにする**（勝手に入れ替わるほうが困る）
    EditorContent wanted = editorContent;

    switch (track.getType())
    {
        case TrackType::Chord: wanted = EditorContent::chordPad;    break;
        case TrackType::Audio: wanted = EditorContent::audioEditor; break;
        case TrackType::Midi:  wanted = EditorContent::pianoRoll;   break;

        case TrackType::Send:
        case TrackType::DrumOut:   // 8.143：エディタで開くものが無い（Phase 181）
        case TrackType::Folder:
        case TrackType::VCA:
        default:               return;
    }

    // **Consoleを出しているときは取り上げない**（8.27。同じ枠を取り合うため）。
    // Editorボタンで戻る先だけ覚えておけば、次に押したときに正しい中身が出る
    if (editorContent == EditorContent::console)
    {
        rememberEditorContent (wanted);
        return;
    }

    // 8.39：**MIDIクリップを選んだら、ピアノロールもそのクリップへ寄せる**（Phase 79）。
    //
    // オーディオエディタは選んだクリップを出すので、揃えておかないと
    // 「オーディオは付いてくるのにMIDIは付いてこない」ことになる。
    //
    // **ここ（選択が変わったとき）だけでやること。** `refreshEditorContent()`へ入れると、
    // ピアノロール自身のトラック一覧（G4）で選び直したものが、
    // パネルを開き直すたびにアレンジ画面の選択へ引き戻される。
    //
    // **中身を切り替える前に呼ぶこと**：`setEditorContent()`は最後に
    // `refreshEditorContent()`を通すので、先に対象を決めておけば1回で済む
    // 8.71：**トラックヘッダーを選んだときも寄せます**（Phase 110／改善案②）。
    //
    // Phase 109まではクリップを選んだときだけでした。ピアノロールを開いたまま
    // アレンジ画面で別のMIDIトラックのヘッダーを押すと、
    // **インスペクタとConsoleはそのトラックに変わるのに、ピアノロールだけ前のまま**——
    // 「いまどれを打ち込んでいるのか」が画面ごとに食い違っていました。
    //
    // オーディオエディタは前から**トラック選択でも**付いてくるので（`getSelectedAudioClip()`が
    // 「クリップを選んでいなければ先頭のクリップ」を返す）、ここを揃えると
    // 「オーディオは付いてくるのにMIDIは付いてこない」も無くなります。
    if (wanted == EditorContent::pianoRoll
         && (selection.getType() == SelectionState::Type::MidiClip
              || selection.getType() == SelectionState::Type::Track))
    {
        // **ノートが1つも無くても呼ぶこと**：そのトラックへ切り替わったうえで
        // 空のピアノロールが出るのが正しく、前のトラックが残るほうが紛らわしい
        for (int i = 0; i < project.getNumTracks(); ++i)
        {
            if (project.getTrack (i).getId() == selection.getTrackId())
            {
                pianoRollView.showTrack (i);
                break;
            }
        }
    }

    // 既にその中身なら、対象が変わっただけなので読み直すだけ。
    // **`setEditorContent()`は同じ値だと何もしない**ので、ここで呼んでおく
    if (editorContent == wanted)
    {
        refreshEditorContent();
        return;
    }

    setEditorContent (wanted);
}

juce::Component* MainComponent::getEditorContentComponent()
{
    switch (editorContent)
    {
        case EditorContent::chordPad:    return &chordPadPanel;
        case EditorContent::console:     return &consoleView;
        case EditorContent::audioEditor: return &audioEditorView;   // Phase 79
        case EditorContent::pianoRoll:
        default:                      return &pianoRollView;
    }
}

juce::String MainComponent::getEditorContentTitle() const
{
    switch (editorContent)
    {
        case EditorContent::chordPad:    return "Chord Pad";
        case EditorContent::console:     return "Console";
        case EditorContent::audioEditor: return "Audio Editor";   // Phase 79
        case EditorContent::pianoRoll:
        default:                      return "Piano Roll";
    }
}

void MainComponent::refreshEditorContent()
{
    if (editorContent == EditorContent::chordPad)
    {
        // コードを挿す位置は再生位置に合わせる（仕様書5.11.2の「カーソル位置」）
        chordPadPanel.setInsertPosition (audioEngine.getPlayheadSeconds());
        chordPadPanel.refreshFromModel();
        return;
    }

    // 8.39：オーディオエディタは**選択に合わせて対象クリップを選び直す**（Phase 79）。
    // クリップを選んでいればそれを、選んでいなければそのトラックの最初のクリップを出す
    if (editorContent == EditorContent::audioEditor)
    {
        audioEditorView.setClip (getSelectedAudioClip());
        audioEditorView.refreshFromModel();
        return;
    }

    // Consoleは自分の`visibilityChanged()`でストリップを組み直し、
    // メーターのタイマーも回し始める。ここから触ると更新の経路が2本になる（1.15）
    if (editorContent == EditorContent::console)
        return;

    pianoRollView.refreshFromModel();
}

bool MainComponent::isPianoRollFocused() const
{
    // ピアノロールが出ていない（コードパッド／Consoleを出している、または
    // エディタパネルを閉じている）なら、いつもどおりアレンジ画面へ渡す
    if (editorContent != EditorContent::pianoRoll || ! pianoRollView.isVisible())
        return false;

    // 子（ノートグリッド・コンボボックス等）が持っている場合も「ピアノロールを
    // 触っている」とみなす。8.11のCtrl+A／Ctrl+Dと同じ考え方
    return pianoRollView.hasKeyboardFocus (true);
}

void MainComponent::toggleEditorContent (EditorContent content)
{
    // ポップアウト中はドック側のパネルが空。**閉じずに中身だけ入れ替える**
    // （別ウィンドウを勝手に閉じると、置いた場所へ戻す手間が増える）
    if (editorWindow != nullptr)
    {
        setEditorContent (content);
        editorWindow->toFront (true);
        return;
    }

    // 同じ中身が既に出ていれば閉じる。**これが無いと閉じる手が無くなる**
    // （EditorとConsoleが同じ枠を取り合うため、押しても入れ替わるだけになる）
    if (editorPanel.isVisible() && editorContent == content)
    {
        setEditorPanelOpen (false);
        return;
    }

    setEditorContent (content);
    setEditorPanelOpen (true);
}

void MainComponent::rememberEditorContent (EditorContent content)
{
    // 8.75：**「Editorボタンで戻る先」に覚えてよいのは、Editorボタンのものだけ**
    // （Phase 115）。
    //
    // Phase 114でChord Padがフッターのボタンになったので、
    // Console と同じく**自分のボタンを持つ中身**になりました。
    // ここで覚えてしまうと、**一度Chord Padを開いた後はEditorを押してもChord Padが出ます**
    // （実際にそうなっていた）。
    //
    // 判断はこの関数1箇所。呼ぶ側で書くと、片方だけ直し忘れます（1.26）——
    // 現に、`setEditorContent()`と`updateEditorContentForSelectedTrack()`の
    // 2箇所に同じ判断が書いてありました。
    if (content == EditorContent::console || content == EditorContent::chordPad)
        return;

    lastEditorContent = content;
}

void MainComponent::setEditorContent (EditorContent newContent)
{
    if (editorContent == newContent)
        return;

    editorContent = newContent;

    rememberEditorContent (newContent);

    // ポップアウト中は別ウィンドウが中身を握っているので、そちらを付け替える。
    // 両方へ付けると、同じコンポーネントを2つの親が握ることになる。
    if (editorWindow != nullptr)
    {
        editorWindow->clearContentComponent();
        editorWindow->setName (getEditorContentTitle());
        editorWindow->setContentNonOwned (getEditorContentComponent(), false);
    }
    else
    {
        editorPanel.setContent (getEditorContentComponent());
    }

    editorPanel.setTitle (getEditorContentTitle());

    // フッターのボタンの見た目も、どちらが出ているかに合わせる（Phase 66）
    updateEditorToggleButtons();

    refreshEditorContent();
}

void MainComponent::updateEditorToggleButtons()
{
    // ポップアウト中も「出ている」扱いにする。別ウィンドウで見えているのに
    // ボタンが消灯していると、閉じたように見える
    const bool isShowing = editorPanel.isVisible() || editorWindow != nullptr;

    // 8.74：**Chord Padも下部パネルの中身**（Phase 114/改善案⑭の作り直し）。
    // Editorボタンは「ピアノロールかオーディオエディタ」を指すようになった
    const bool isChordPad = (editorContent == EditorContent::chordPad);
    const bool isConsole  = (editorContent == EditorContent::console);

    transportBar.setEditorPanelOpen (isShowing && ! isConsole && ! isChordPad);
    transportBar.setConsolePanelOpen (isShowing && isConsole);
    transportBar.setChordPadPanelOpen (isShowing && isChordPad);
}

void MainComponent::popOutEditor()
{
    if (editorWindow != nullptr)
    {
        editorWindow->toFront (true); // 既に出ているなら前面に出すだけ
        return;
    }

    // ドック側から中身を外してから移す。外し忘れると、同じコンポーネントを
    // 2箇所の親が握ることになる。
    setEditorPanelOpen (false);
    editorPanel.setContent (nullptr);

    auto window = std::make_unique<EditorWindow> (getEditorContentTitle(), AppColours::background);
    window->setContentNonOwned (getEditorContentComponent(), false);

    // 設計書2.5：前回の位置・サイズを復元する（毎回置き直さずに済むように）。
    // モニター構成が変わって画面外になっている場合に備え、必ず画面内へ寄せる。
    const auto savedBounds = juce::Rectangle<int>::fromString (AppSettings::getString (editorWindowBoundsKey));

    if (! savedBounds.isEmpty())
        window->setBounds (savedBounds);
    else
        window->centreWithSize (900, 520);

    window->setBoundsConstrained (window->getBounds());

    // 閉じたら自動的にドッキングし直す（設計書2.5「状態を失わない」）。
    // SafePointerで包むのは、非同期で戻ってくる前にMainComponentが
    // 消えている可能性（アプリ終了）を考慮するため。
    juce::Component::SafePointer<MainComponent> safeThis (this);

    window->onCloseRequested = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->dockEditor();
        });
    };

    // 別ウィンドウにフォーカスがあってもUndo等のショートカットが効くようにする。
    // HANDOVER 2章と同じ話で、キー入力は「フォーカスのあるウィンドウ」に届くため、
    // メインウィンドウ側のキーリスナーだけでは拾えない。
    window->addKeyListener (commandManager.getKeyMappings());

    window->setVisible (true);
    editorWindow = std::move (window);

    transportBar.setEditorPanelOpen (true); // ポップアウト中も「エディタは開いている」
    refreshEditorContent();
}


void MainComponent::dockEditor()
{
    if (editorWindow == nullptr)
        return;

    saveEditorWindowBounds();

    // ウィンドウを壊す前に中身を外す。付けたまま破棄すると、
    // 借りているだけのピアノロールの親が宙に浮く。
    editorWindow->clearContentComponent();
    editorWindow.reset();

    editorPanel.setContent (getEditorContentComponent());
    setEditorPanelOpen (true);
}

void MainComponent::saveEditorWindowBounds() const
{
    if (editorWindow != nullptr)
        AppSettings::setString (editorWindowBoundsKey, editorWindow->getBounds().toString());
}

//==============================================================================
// 設計書2.2：左右のサイドパネル（Phase 17）
//==============================================================================

void MainComponent::setBrowserPanelOpen (bool shouldBeOpen)
{
    if (browserPanel.isVisible() == shouldBeOpen)
        return;

    browserPanel.setVisible (shouldBeOpen);
    browserResizer.setVisible (shouldBeOpen);
    transportBar.setBrowserPanelOpen (shouldBeOpen);
    AppSettings::setInt (browserPanelOpenKey, shouldBeOpen ? 1 : 0);

    if (shouldBeOpen)
        browserPanel.refreshPluginList();

    resized();
}

void MainComponent::setInspectorPanelOpen (bool shouldBeOpen)
{
    if (inspectorPanel.isVisible() == shouldBeOpen)
        return;

    inspectorPanel.setVisible (shouldBeOpen);
    inspectorResizer.setVisible (shouldBeOpen);
    transportBar.setInspectorPanelOpen (shouldBeOpen);
    AppSettings::setInt (inspectorPanelOpenKey, shouldBeOpen ? 1 : 0);

    resized();
}

void MainComponent::setBrowserPanelWidth (int newWidth)
{
    // 中央エリアが潰れないよう、左右のパネルを合わせても余白が残る範囲に収める
    const int available = juce::jmax (BrowserPanel::minimumWidth,
                                       getWidth() - inspectorPanelWidth - 320);
    const int limited = juce::jlimit (BrowserPanel::minimumWidth, available, newWidth);

    if (limited == browserPanelWidth)
        return;

    browserPanelWidth = limited;
    AppSettings::setInt (browserPanelWidthKey, browserPanelWidth);
    resized();
}

void MainComponent::setInspectorPanelWidth (int newWidth)
{
    const int available = juce::jmax (InspectorPanel::minimumWidth,
                                       getWidth() - browserPanelWidth - 320);
    const int limited = juce::jlimit (InspectorPanel::minimumWidth, available, newWidth);

    if (limited == inspectorPanelWidth)
        return;

    inspectorPanelWidth = limited;
    AppSettings::setInt (inspectorPanelWidthKey, inspectorPanelWidth);
    resized();
}

void MainComponent::insertPluginFromBrowser (const juce::PluginDescription& description)
{
    // 仕様書4.4：ダブルクリックでの挿入。挿し先は「いま選んでいるトラック」。
    // ドラッグ&ドロップ（Phase 21）は落とした場所で挿し先が決まるので、
    // そちらはinsertPluginIntoTrack()を直接呼ぶ。
    const auto trackId = selection.getTrackId();

    if (trackId.isEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (utf8 ("挿入先のトラックがありません"))
                .withMessage (utf8 ("先にArrangeタブでトラック（またはクリップ）を選んでください。"))
                .withButton ("OK"),
            nullptr);
        return;
    }

    insertPluginIntoTrack (trackId, description);
}

void MainComponent::insertPluginIntoTrack (const juce::String& trackId,
                                             const juce::PluginDescription& description)
{
    if (trackId.isEmpty())
        return;

    const auto error = description.isInstrument
                          ? audioEngine.loadInstrumentForTrack (trackId, description)
                          : audioEngine.addInsertToTrack (trackId, description);

    if (error.isNotEmpty())
    {
        showPluginInsertError (error);
        return;
    }

    // ミキサーのスロット表示を更新する（開いていれば作り直される）
    consoleView.refreshAfterProjectChanged();
    pianoRollView.refreshFromModel();

    // 設計書3.7：挿したらそのままGUIで音作りに入れるよう、エディタを開く。
    // 挿入した直後は「一番最後のインサート」が今追加したものになる。
    if (description.isInstrument)
    {
        audioEngine.openTrackInstrumentEditor (trackId);
        return;
    }

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getId() != trackId)
            continue;

        if (track.getNumInserts() > 0)
            audioEngine.openInsertEditor (trackId, track.getNumInserts() - 1);

        break;
    }
}

void MainComponent::showPluginInsertError (const juce::String& error)
{
    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle (utf8 ("プラグインを挿入できませんでした"))
            .withMessage (error)
            .withButton ("OK"),
        nullptr);
}

void MainComponent::setEditorPanelHeight (int newHeight)
{
    auto area = getLocalBounds();
    area.removeFromTop (24 + 48); // メニューバーとトップバー

    if (audioErrorLabel.isVisible())
        area.removeFromTop (24);

    // 中央エリアが潰れないよう、上側にも最低限の高さを残す
    const int maximumHeight = juce::jmax (EditorPanel::minimumHeight, area.getHeight() - 120);
    const int limited = juce::jlimit (EditorPanel::minimumHeight, maximumHeight, newHeight);

    if (limited == editorPanelHeight)
        return;

    editorPanelHeight = limited;
    AppSettings::setInt (editorPanelHeightKey, editorPanelHeight); // 設計書2.5
    resized();
}

void MainComponent::playButtonClicked()
{
    if (audioEngine.isPlaying())
    {
        // 録音中にStopが押された場合も、録音を正しく閉じてファイルを確定させる。
        // 8.146：**後始末は`finishRecording()`1箇所**（Phase 184／1.27）
        if (audioEngine.isRecording())
            finishRecording();

        audioEngine.stop();
        stopTimer();
        transportBar.setPlayingState (false);
        setPlayheadDisplay (audioEngine.getPlayheadSeconds());
    }
    else
    {
        // 仕様書5.9：**再生を始める直前に必ず反映する**（Phase 48）。
        // ループ範囲は秒で持っているが、Transportはサンプルで持っている。
        // オーディオデバイスのサンプルレートが確定するのは起動時の初期化後なので、
        // 起動直後に1度だけ反映しただけでは、換算が古いレートのままになりうる。
        applyLoopSettings();

        audioEngine.play();
        transportBar.setPlayingState (true);
        startTimerHz (30); // プレイヘッドをおよそ30fpsで更新する
    }
}

void MainComponent::finishRecording()
{
    // 8.146：**録音の後始末はここだけ**（Phase 184／1.27）。
    //
    // Phase 183まで、これがStopボタンとRecボタンの2箇所に書いてありました。
    // MIDIのぶんが増えるので、**片方に足し忘れる**ことが確実に起きます
    // （実際、カウントイン中の始末はRecボタン側にしかありませんでした）。

    // 仕様書5.4：カウントインの途中で止められた場合は、まだ何も録れていない（Phase 39）。
    // 空のファイルからクリップを作ろうとしないよう、先に判定しておく
    const bool wasCountingIn = audioEngine.isCountingIn();

    // **止める前に読むこと。** 止めたあとでは、どちらを録っていたか分かりません
    const bool hadAudio = audioEngine.isAudioRecording();
    const bool hadMidi = audioEngine.isMidiRecording();
    const double stopSeconds = audioEngine.getPlayheadSeconds();
    const double sampleRate = audioEngine.getSampleRate();

    audioEngine.stopRecording();

    transportBar.setRecordingState (false);
    transportBar.setCountingIn (false);

    auto midiTakes = audioEngine.takeRecordedMidi();

    if (wasCountingIn)
    {
        // 中身の無いWAV（ヘッダーだけ）が残るので消す。
        // 数え直すたびに空ファイルが溜まっていくのを避けるため。
        //
        // 8.146：**オーディオを録っていたときだけ消すこと**（Phase 184）。
        // MIDIだけの録音では`lastRecordingFile`は**前回のWAV**を指したままなので、
        // 無条件に消すと**前の録音が消えます**
        if (hadAudio)
            audioEngine.getLastRecordingFile().deleteFile();

        arrangeView.showStatusMessage (utf8 ("カウントイン中に停止しました（録音していません）。"));
        return;
    }

    if (hadAudio)
        arrangeView.showRecordingResult (audioEngine.getLastRecordingFile(),
                                          audioEngine.getRecordStartSeconds());

    // **1つも録れていなくても呼ぶこと。** ここを`! midiTakes.empty()`で
    // 囲っていたときは、**鍵盤のデバイスが有効になっていないと画面に何も出ません**でした
    // ——録音を回して止めたのに、成功とも失敗とも分からない状態です（実機で確認）
    if (hadMidi)
    {
        const auto message = arrangeView.applyMidiRecordingResult (midiTakes, sampleRate, stopSeconds);

        // 入れたノートは**再生と画面の両方**へ反映が要る（1.15）。
        // モデルを見ているのは画面だけなので、エンジンには別途知らせる
        if (! midiTakes.empty())
            refreshAllViews (true);

        // **知らせるのは作り直したあと。** 先に出すと`refreshAllViews()`が
        // ステータス欄を普通のトラック一覧へ戻してしまい、**何も出ません**でした
        // （実機で確認。`ArrangeView::applyMidiRecordingResult()`の説明）
        arrangeView.showStatusMessage (message);
    }
}

void MainComponent::recordButtonClicked()
{
    // 仕様書5.4：録音はトランスポートと連動させる。
    // 録音開始と同時に再生も始めることで、既存クリップを聴きながら重ね録りできる。
    if (audioEngine.isRecording())
    {
        // 8.146：**後始末は`finishRecording()`1箇所**（Phase 184／1.27）。
        // **トランスポートを止める前に**呼ぶこと——押しっぱなしの鍵を切る位置に
        // 録音の終わりの時刻が要ります
        finishRecording();

        audioEngine.stop();
        stopTimer();

        transportBar.setPlayingState (false);
        setPlayheadDisplay (audioEngine.getPlayheadSeconds());
        transportBar.setPlayheadSeconds (audioEngine.getPlayheadSeconds());

        return;
    }

    const int countInBars = getCountInBars();

    // 仕様書5.4：カウントインの拍とタイムラインの小節線を揃える（Phase 40）。
    // **録音を始める前に動かすこと**：開始位置は startRecording() の中で
    // そのときのプレイヘッドから決まるため、後から動かしても間に合わない。
    if (countInBars > 0)
        snapPlayheadToBarForCountIn();

    auto error = audioEngine.startRecordingWithCountIn (countInBars);

    if (error.isNotEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("録音を開始できませんでした"))
                .withMessage (error)
                .withButton ("OK"),
            nullptr);
        return;
    }

    // 再生の開始はstartRecordingWithCountIn()の中で行われる
    // （カウントイン有りの場合は、数え終わった時点でオーディオスレッドが始める）。
    transportBar.setRecordingState (true, 0.0);
    transportBar.setPlayingState (true);
    transportBar.setCountingIn (audioEngine.isCountingIn());
    startTimerHz (30);
}

//==============================================================================
// 仕様書5.1：プロジェクト管理（Fileメニュー）
//==============================================================================

namespace CommandIds
{
    enum
    {
        newProject = 0x2001,
        openProject,
        saveProject,
        saveProjectAs,
        autoSaveNow,
        exportMixdown,
        exportStems,
        exportMidi,
        showPreferences,
        undo,
        redo,

        //======================================================================
        // 仕様書6.2：キーボードショートカット（Phase 47）。
        //
        // **既定のキーはStudio Oneに合わせてある**（このDAWが手本にしているため。
        // 仕様書6章）。同じ操作が両方にあるものは同じキーにし、片方にしか無い操作は
        // 空けてある。割り当ては環境設定のShortcutsカテゴリで変えられる。
        //
        // **番号は末尾に足すこと。** 割り当ての保存はコマンドIDを鍵にしているので、
        // 途中に挿すと、保存済みの割り当てが別のコマンドに付く。

        playStop,
        stopTransport,
        returnToStart,
        recordToggle,
        toggleClick,
        toggleCountIn,

        toggleEditorPanel,
        toggleConsolePage,
        toggleInspectorPanel,
        toggleBrowserPanel,

        zoomIn,
        zoomOut,
        zoomToFit,

        addTrack,
        deleteTrack,
        toggleArm,
        toggleSolo,
        toggleMute,
        toggleInputMonitor,

        quitApp,

        // 仕様書5.9：ループ再生（Phase 48）
        toggleLoop,
        goToLoopStart,
        goToLoopEnd,
        setLoopStart,
        setLoopEnd,
        loopToSelection,

        // 仕様書5.9：マーカー（Phase 49）
        insertMarker,
        insertNamedMarker,
        goToPreviousMarker,
        goToNextMarker,

        // 仕様書5.5：クリップの分割・結合・複製（Phase 50）
        splitClip,
        mergeClip,

        // 8.78：クリップのグループ（Phase 118/改善案㊱）
        groupClips,
        ungroupClips,

        // 仕様書6.2：カット／コピー／貼り付け（Phase 71）
        cutSelection,
        copySelection,
        pasteSelection,
        duplicateClip,

        // 仕様書6.2：ツール切り替えと複数選択（Phase 51）
        selectArrowTool,
        selectPencilTool,
        selectCutTool,
        selectEraserTool,   // Phase 83（C11）
        selectAllClips,
        deselectAllClips,

        // 8.151：プロジェクトテンプレート（Phase 189／8.1のD8）
        newFromTemplate,
        saveAsTemplate
    };
}

//==============================================================================
namespace
{
    /**
        仕様書5.10：ミックスダウン書き出しの進捗ウィンドウ。

        書き出しはオフライン処理（実時間より速い）だが、長いプロジェクトでは
        数秒かかる。その間UIを固めないようバックグラウンドスレッドで実行し、
        同時にモーダル表示にすることで、書き出し中にトラックを触られて
        グラフが変化することを防いでいる。
    */
    class MixdownExportTask : public juce::ThreadWithProgressWindow
    {
    public:
        /** isStems=trueならトラックごと（ステム）、falseならミックスダウン1本を書き出す。
            fileOrFolderは、ステムのときは出力先フォルダ、ミックスダウンのときはファイル。 */
        MixdownExportTask (AudioEngine& engineToUse, juce::File fileOrFolder, bool isStems,
                            ExportOptions optionsToUse)
            : ThreadWithProgressWindow (isStems ? utf8 ("ステムを書き出しています...")
                                                 : utf8 ("ミックスダウンを書き出しています..."),
                                         true, true),
              engine (engineToUse), file (std::move (fileOrFolder)), stems (isStems),
              options (optionsToUse)   // 8.80：書き出しの設定（Phase 120）
        {
        }

        void run() override
        {
            auto onProgress = [this] (double progress)
            {
                setProgress (progress);
                return ! threadShouldExit(); // キャンセルボタンで中断できるようにする
            };

            error = stems ? engine.renderStemsToFolder (file, options, onProgress)
                           : engine.renderMixdownToFile (file, options, onProgress);
        }

        void threadComplete (bool) override
        {
            if (error.isNotEmpty())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("書き出しできませんでした"))
                        .withMessage (error)
                        .withButton ("OK"),
                    nullptr);
            }
            else
            {
                juce::String message;
                message << file.getFullPathName();

                if (! stems)
                    message << "\n(" << juce::File::descriptionOfSizeInBytes (file.getSize()) << ")";

                // 8.82：**書き出した場所をその場で開けるように**（Phase 122）。
                //
                // 書き出した直後にやりたいのは「聴く」か「渡す」で、どちらも
                // **エクスプローラで開くところから**始まります。パスを読んで
                // 自分で辿り直すのは、書き出しのたびに要る手間でした。
                //
                // **開く先はファイルそのもの**（`revealToUser()`は入っているフォルダを開き、
                // そのファイルを選んだ状態にする）。ステムはフォルダを渡すので、
                // そのフォルダが開きます
                const auto target = file;

                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::InfoIcon)
                        .withTitle (utf8 ("書き出しが完了しました"))
                        .withMessage (message)
                        .withButton (utf8 ("フォルダを開く"))
                        .withButton ("OK"),
                    [target] (int buttonIndex)
                    {
                        // **0番が最初に足したボタン**（「フォルダを開く」）
                        if (buttonIndex == 0)
                            target.revealToUser();
                    });
            }

            // launchThread()で起動した場合、後始末は自分で行うのがJUCEの作法
            delete this;
        }

    private:
        AudioEngine& engine;
        juce::File file;
        bool stems = false;
        ExportOptions options;   // 8.80：書き出しの設定（Phase 120/D9・D10・D11）
        juce::String error;
    };
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { utf8 ("ファイル"), utf8 ("編集") };
}

juce::PopupMenu MainComponent::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    // addCommandItem()を使うと、項目名の右にショートカットキーが自動表示され、
    // 有効/無効もgetCommandInfo()の設定に従う。
    if (topLevelMenuIndex == 0)
    {
        menu.addCommandItem (&commandManager, CommandIds::newProject);
        menu.addCommandItem (&commandManager, CommandIds::newFromTemplate);   // 8.151（Phase 189／D8）
        menu.addCommandItem (&commandManager, CommandIds::openProject);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIds::saveProject);
        menu.addCommandItem (&commandManager, CommandIds::saveProjectAs);
        menu.addCommandItem (&commandManager, CommandIds::saveAsTemplate);    // 8.151（Phase 189／D8）
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIds::autoSaveNow);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIds::exportMixdown);
        menu.addCommandItem (&commandManager, CommandIds::exportStems);
        menu.addCommandItem (&commandManager, CommandIds::exportMidi);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIds::showPreferences);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, CommandIds::quitApp);   // Phase 47
    }
    else if (topLevelMenuIndex == 1)
    {
        menu.addCommandItem (&commandManager, CommandIds::undo);
        menu.addCommandItem (&commandManager, CommandIds::redo);
        menu.addSeparator();

        // 仕様書6.2：カット／コピー／貼り付け（Phase 71）。
        // **メニューにも出しておくこと**：ショートカットしか無いと、
        // 使えるかどうか（貼り付けるものがあるか）が確かめられない
        menu.addCommandItem (&commandManager, CommandIds::cutSelection);
        menu.addCommandItem (&commandManager, CommandIds::copySelection);
        menu.addCommandItem (&commandManager, CommandIds::pasteSelection);
    }

    return menu;
}

void MainComponent::menuItemSelected (int, int)
{
    // すべての項目をコマンド（addCommandItem）として登録しているため、
    // 実行はperform()側で行われ、ここへは来ない。
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ CommandIds::newProject, CommandIds::openProject,
                          CommandIds::saveProject, CommandIds::saveProjectAs,
                          CommandIds::autoSaveNow, CommandIds::exportMixdown,
                          CommandIds::exportStems, CommandIds::exportMidi,
                          CommandIds::showPreferences, CommandIds::quitApp,
                          CommandIds::undo, CommandIds::redo,

                          // 仕様書6.2：ショートカット（Phase 47）
                          CommandIds::playStop, CommandIds::stopTransport,
                          CommandIds::returnToStart, CommandIds::recordToggle,
                          CommandIds::toggleClick, CommandIds::toggleCountIn,

                          CommandIds::toggleEditorPanel, CommandIds::toggleConsolePage,
                          CommandIds::toggleInspectorPanel, CommandIds::toggleBrowserPanel,

                          CommandIds::zoomIn, CommandIds::zoomOut, CommandIds::zoomToFit,

                          CommandIds::addTrack, CommandIds::deleteTrack,
                          CommandIds::toggleArm, CommandIds::toggleSolo,
                          CommandIds::toggleMute, CommandIds::toggleInputMonitor,

                          // 仕様書5.9：ループ再生（Phase 48）
                          CommandIds::toggleLoop, CommandIds::goToLoopStart, CommandIds::goToLoopEnd,
                          CommandIds::setLoopStart, CommandIds::setLoopEnd,
                          CommandIds::loopToSelection,

                          // 仕様書5.9：マーカー（Phase 49）
                          CommandIds::insertMarker, CommandIds::insertNamedMarker,
                          CommandIds::goToPreviousMarker, CommandIds::goToNextMarker,

                          // 仕様書5.5：クリップの分割・結合・複製（Phase 50）
                          CommandIds::splitClip, CommandIds::mergeClip,
                          CommandIds::groupClips, CommandIds::ungroupClips,   // 8.78（Phase 118）
                          CommandIds::duplicateClip,

                          // 仕様書6.2：カット／コピー／貼り付け（Phase 71）
                          CommandIds::cutSelection, CommandIds::copySelection,
                          CommandIds::pasteSelection,

                          // 仕様書6.2：ツール切り替えと複数選択（Phase 51）
                          CommandIds::selectArrowTool, CommandIds::selectPencilTool,
                          CommandIds::selectCutTool, CommandIds::selectEraserTool,
                          CommandIds::selectAllClips, CommandIds::deselectAllClips,
                          CommandIds::newFromTemplate, CommandIds::saveAsTemplate });
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result)
{
    auto& undoManager = project.getUndoManager();

    switch (commandID)
    {
        case CommandIds::newProject:
            result.setInfo (utf8 ("新規プロジェクト"), utf8 ("新しいプロジェクトを作る"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('n', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::newFromTemplate:
            // 8.151：既定のキーは割り当てていません（Phase 189）。
            // **Ctrl+Nは「新規プロジェクト」のまま**——手本にしているDAWと同じ
            result.setInfo (utf8 ("テンプレートから新規..."),
                             utf8 ("雛形を選んで新しいプロジェクトを作る"), utf8 ("ファイル"), 0);
            break;

        case CommandIds::saveAsTemplate:
            result.setInfo (utf8 ("テンプレートとして保存..."),
                             utf8 ("いまのプロジェクトを、次に使う雛形として保存する"), utf8 ("ファイル"), 0);
            break;

        case CommandIds::openProject:
            result.setInfo (utf8 ("開く..."), utf8 ("プロジェクトファイルを開く"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::saveProject:
            result.setInfo (utf8 ("保存"), utf8 ("プロジェクトを保存する"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::saveProjectAs:
            result.setInfo (utf8 ("名前を付けて保存..."), utf8 ("別名で保存する"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            break;

        case CommandIds::autoSaveNow:
            result.setInfo (utf8 ("今すぐ自動保存"), utf8 ("復旧用の自動保存をすぐ実行する"), utf8 ("ファイル"), 0);
            result.setActive (project.hasUnsavedChanges());
            break;

        case CommandIds::exportMixdown:
            result.setInfo (utf8 ("ミックスダウンを書き出し..."),
                             utf8 ("マスター出力をWAVファイルへ書き出す"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::exportStems:
            result.setInfo (utf8 ("ステムを書き出し..."),
                             utf8 ("トラックごとに1ファイルずつ書き出す"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('e', juce::ModifierKeys::commandModifier
                                              | juce::ModifierKeys::shiftModifier);   // Phase 47
            break;

        case CommandIds::exportMidi:
            result.setInfo (utf8 ("MIDIファイルを書き出し..."),
                             utf8 ("打ち込んだノートを標準MIDIファイルへ書き出す"), utf8 ("ファイル"), 0);
            break;

        case CommandIds::showPreferences:
            result.setInfo (utf8 ("環境設定..."),
                             utf8 ("オーディオデバイスとプラグインの設定（仕様書6.2）"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::undo:
            // 「何を元に戻すのか」が分かるよう、直前の操作名を項目名に含める
            result.setInfo (undoManager.canUndo()
                                ? utf8 ("元に戻す: ") + undoManager.getUndoDescription()
                                : utf8 ("元に戻す"),
                             utf8 ("直前の操作を取り消す"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier);
            result.setActive (undoManager.canUndo());
            break;

        case CommandIds::redo:
            result.setInfo (undoManager.canRedo()
                                ? utf8 ("やり直す: ") + undoManager.getRedoDescription()
                                : utf8 ("やり直す"),
                             utf8 ("取り消した操作をやり直す"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
            result.addDefaultKeypress ('y', juce::ModifierKeys::commandModifier); // Windowsの慣習
            result.setActive (undoManager.canRedo());
            break;

        //======================================================================
        // 仕様書6.2：Studio One互換のショートカット（Phase 47）。
        // カテゴリ名はメニューには出ないが、環境設定のShortcuts一覧の見出しになる。

        case CommandIds::playStop:
            result.setInfo (utf8 ("再生／停止"), utf8 ("再生と停止を切り替える"), utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::stopTransport:
            result.setInfo (utf8 ("停止"), utf8 ("再生・録音を止める"), utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPad0, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::returnToStart:
            result.setInfo (utf8 ("先頭へ戻る"), utf8 ("再生位置を0へ戻す"), utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::homeKey, juce::ModifierKeys::noModifiers);
            result.addDefaultKeypress (juce::KeyPress::numberPadDecimalPoint, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::recordToggle:
            result.setInfo (utf8 ("録音"), utf8 ("録音を開始／停止する"), utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPadMultiply, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::toggleClick:
            result.setInfo (utf8 ("クリック"), utf8 ("メトロノームの入切"), utf8 ("トランスポート"), 0);
            result.addDefaultKeypress ('c', juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::toggleCountIn:
            result.setInfo (utf8 ("プリカウント"), utf8 ("録音前のカウントインの入切（1小節）"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress ('c', juce::ModifierKeys::shiftModifier);
            break;

        case CommandIds::toggleEditorPanel:
            result.setInfo (utf8 ("エディター"), utf8 ("エディタパネルの開閉"), utf8 ("表示"), 0);
            result.addDefaultKeypress (juce::KeyPress::F2Key, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::toggleConsolePage:
            result.setInfo (utf8 ("コンソール"), utf8 ("ConsoleページとArrangeページを切り替える"),
                             utf8 ("表示"), 0);
            result.addDefaultKeypress (juce::KeyPress::F3Key, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::toggleInspectorPanel:
            result.setInfo (utf8 ("インスペクター"), utf8 ("インスペクタパネルの開閉"), utf8 ("表示"), 0);
            result.addDefaultKeypress (juce::KeyPress::F4Key, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::toggleBrowserPanel:
            result.setInfo (utf8 ("ブラウザー"), utf8 ("ブラウザパネルの開閉"), utf8 ("表示"), 0);
            result.addDefaultKeypress (juce::KeyPress::F5Key, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::zoomIn:
            result.setInfo (utf8 ("ズームイン"), utf8 ("タイムライン（ピアノロールにフォーカスがあればそちら）を横に拡大する"), utf8 ("ズーム"), 0);
            result.addDefaultKeypress ('e', juce::ModifierKeys::noModifiers);
            result.addDefaultKeypress ('=', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::zoomOut:
            result.setInfo (utf8 ("ズームアウト"), utf8 ("タイムライン（ピアノロールにフォーカスがあればそちら）を横に縮小する"), utf8 ("ズーム"), 0);
            result.addDefaultKeypress ('w', juce::ModifierKeys::noModifiers);
            result.addDefaultKeypress ('-', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::zoomToFit:
            result.setInfo (utf8 ("フルズーム"), utf8 ("全体が入る倍率にする（ピアノロールではクリップ全体）"), utf8 ("ズーム"), 0);
            result.addDefaultKeypress ('z', juce::ModifierKeys::altModifier);
            break;

        case CommandIds::addTrack:
            result.setInfo (utf8 ("トラックを追加"), utf8 ("追加するトラックの種類を選ぶ"),
                             utf8 ("トラック"), 0);
            result.addDefaultKeypress ('t', juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::deleteTrack:
            result.setInfo (utf8 ("トラックを削除"), utf8 ("選択中のトラックを削除する"),
                             utf8 ("トラック"), 0);
            result.addDefaultKeypress ('t', juce::ModifierKeys::shiftModifier);
            result.setActive (selection.getTrackId().isNotEmpty());
            break;

        case CommandIds::toggleArm:
            result.setInfo (utf8 ("アーム"), utf8 ("選択中のトラックの録音待機を切り替える"),
                             utf8 ("トラック"), 0);
            result.addDefaultKeypress ('r', juce::ModifierKeys::noModifiers);
            result.setActive (selection.getTrackId().isNotEmpty());
            break;

        case CommandIds::toggleSolo:
            result.setInfo (utf8 ("ソロ"), utf8 ("選択中のトラックのソロを切り替える"),
                             utf8 ("トラック"), 0);
            result.addDefaultKeypress ('s', juce::ModifierKeys::noModifiers);
            result.setActive (selection.getTrackId().isNotEmpty());
            break;

        case CommandIds::toggleMute:
            result.setInfo (utf8 ("ミュート"), utf8 ("選択中のトラックのミュートを切り替える"),
                             utf8 ("トラック"), 0);
            result.addDefaultKeypress ('m', juce::ModifierKeys::noModifiers);
            result.setActive (selection.getTrackId().isNotEmpty());
            break;

        case CommandIds::toggleInputMonitor:
            result.setInfo (utf8 ("モニター"), utf8 ("入力モニタリングの入切"), utf8 ("トラック"), 0);
            result.addDefaultKeypress ('u', juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::quitApp:
            result.setInfo (utf8 ("終了"), utf8 ("アプリケーションを終了する"), utf8 ("ファイル"), 0);
            result.addDefaultKeypress ('q', juce::ModifierKeys::commandModifier);
            break;

        //======================================================================
        // 仕様書5.9：ループ再生（Phase 48）。既定のキーはStudio Oneに合わせてある

        case CommandIds::toggleLoop:
            result.setInfo (utf8 ("ループの入切"), utf8 ("ループ再生を切り替える"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress ('/', juce::ModifierKeys::noModifiers);
            result.addDefaultKeypress (juce::KeyPress::numberPadDivide, juce::ModifierKeys::noModifiers);
            result.setTicked (project.isLoopEnabled());
            break;

        case CommandIds::goToLoopStart:
            result.setInfo (utf8 ("ループの先頭へ"), utf8 ("再生位置をループの開始位置へ移す"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPad1, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::goToLoopEnd:
            result.setInfo (utf8 ("ループの終わりへ"), utf8 ("再生位置をループの終了位置へ移す"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPad2, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::setLoopStart:
            result.setInfo (utf8 ("ループの先頭を設定"), utf8 ("いまの再生位置をループの開始にする"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPad1, juce::ModifierKeys::altModifier);
            break;

        case CommandIds::setLoopEnd:
            result.setInfo (utf8 ("ループの終わりを設定"), utf8 ("いまの再生位置をループの終了にする"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress (juce::KeyPress::numberPad2, juce::ModifierKeys::altModifier);
            break;

        case CommandIds::loopToSelection:
            result.setInfo (utf8 ("選択をループ"), utf8 ("選択中のクリップの範囲をループにする"),
                             utf8 ("トランスポート"), 0);
            result.addDefaultKeypress ('p', juce::ModifierKeys::shiftModifier);
            result.setActive (selection.isClipSelected());
            break;

        //======================================================================
        // 仕様書5.9：マーカー（Phase 49）

        case CommandIds::insertMarker:
            result.setInfo (utf8 ("マーカーを挿入"), utf8 ("いまの再生位置にマーカーを置く"),
                             utf8 ("マーカー"), 0);
            result.addDefaultKeypress ('y', juce::ModifierKeys::noModifiers);
            result.addDefaultKeypress (juce::KeyPress::insertKey, juce::ModifierKeys::noModifiers);
            break;

        case CommandIds::insertNamedMarker:
            result.setInfo (utf8 ("名前を付けてマーカーを挿入"),
                             utf8 ("いまの再生位置に、名前を入力してマーカーを置く"),
                             utf8 ("マーカー"), 0);
            result.addDefaultKeypress ('y', juce::ModifierKeys::shiftModifier);
            result.addDefaultKeypress (juce::KeyPress::insertKey, juce::ModifierKeys::shiftModifier);
            break;

        case CommandIds::goToPreviousMarker:
            result.setInfo (utf8 ("前のマーカーへ"), utf8 ("再生位置を1つ前のマーカーへ移す"),
                             utf8 ("マーカー"), 0);
            result.addDefaultKeypress ('b', juce::ModifierKeys::shiftModifier);
            result.setActive (project.getNumMarkers() > 0);
            break;

        case CommandIds::goToNextMarker:
            result.setInfo (utf8 ("次のマーカーへ"), utf8 ("再生位置を1つ次のマーカーへ移す"),
                             utf8 ("マーカー"), 0);
            result.addDefaultKeypress ('n', juce::ModifierKeys::shiftModifier);
            result.setActive (project.getNumMarkers() > 0);
            break;

        //======================================================================
        // 仕様書5.5：クリップの分割・結合・複製（Phase 50）

        case CommandIds::splitClip:
            result.setInfo (utf8 ("分割"), utf8 ("選択中のクリップを、再生位置で2つに割る"),
                             utf8 ("クリップ"), 0);
            result.addDefaultKeypress ('x', juce::ModifierKeys::altModifier);
            result.setActive (selection.isClipSelected());
            break;

        case CommandIds::mergeClip:
            result.setInfo (utf8 ("結合"), utf8 ("選択中のクリップを、次のクリップと1つにする"),
                             utf8 ("クリップ"), 0);

            // 8.78：**GはPhase 118でグループ化へ渡しました**（改善案㊱）。
            // 一般的なDAWでGは「まとめる」で、結合（2つを1つのクリップにする）とは別の操作です。
            // 近いところに置きたいのでAlt+Gにしてあります（環境設定のShortcutsで変えられます）
            result.addDefaultKeypress ('g', juce::ModifierKeys::altModifier);
            result.setActive (selection.isClipSelected());
            break;

        // 8.78：クリップのグループ（Phase 118/改善案㊱）
        case CommandIds::groupClips:
            result.setInfo (utf8 ("グループ化"),
                             utf8 ("選んだクリップをまとめる（以後まとめて選ばれ、一緒に動く）"),
                             utf8 ("クリップ"), 0);
            result.addDefaultKeypress ('g', juce::ModifierKeys::noModifiers);
            result.setActive (arrangeView.canGroupSelectedClips());
            break;

        case CommandIds::ungroupClips:
            result.setInfo (utf8 ("グループ解除"), utf8 ("選んだクリップのグループを解く"),
                             utf8 ("クリップ"), 0);
            result.addDefaultKeypress ('g', juce::ModifierKeys::shiftModifier);
            result.setActive (arrangeView.hasGroupedClipInSelection());
            break;

        case CommandIds::duplicateClip:
            result.setInfo (utf8 ("複製"), utf8 ("選択中のクリップを、その直後へ複製する"),
                             utf8 ("クリップ"), 0);
            result.addDefaultKeypress ('d', juce::ModifierKeys::noModifiers);
            result.setActive (selection.isClipSelected());
            break;

        // 仕様書6.2：カット／コピー／貼り付け（Phase 71／8.29の表）。
        // **触っている画面が対象**（ピアノロールにフォーカスがあればノート、
        // それ以外はアレンジ画面のクリップ）。Ctrl+A／Ctrl+Dと同じ考え方（8.11）

        case CommandIds::cutSelection:
            result.setInfo (utf8 ("カット"), utf8 ("選択したものを切り取る"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('x', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::copySelection:
            result.setInfo (utf8 ("コピー"), utf8 ("選択したものをコピーする"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('c', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::pasteSelection:
            result.setInfo (utf8 ("貼り付け"), utf8 ("再生カーソルの位置へ貼り付ける"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('v', juce::ModifierKeys::commandModifier);
            break;

        //======================================================================
        // 仕様書6.2：ツール切り替えと複数選択（Phase 51）。
        // 番号のキーはStudio Oneの「ツール1〜」に合わせてある。
        //
        // **Phase 69で範囲選択ツール（3）を廃止しました**（矢印に統合。8.29）。
        // **カットの「4」は詰め直していません**：環境設定に保存済みの割り当てと
        // 食い違わせないためです（8.6）。3は空き番です。

        case CommandIds::selectArrowTool:
            result.setInfo (utf8 ("選択ツール"),
                             utf8 ("選択・移動・伸縮／空いている場所のドラッグで範囲選択"),
                             utf8 ("ツール"), 0);
            result.addDefaultKeypress ('1', juce::ModifierKeys::noModifiers);
            result.setTicked (arrangeView.getEditTool() == EditTool::arrow);
            break;

        case CommandIds::selectPencilTool:
            result.setInfo (utf8 ("ペンツール"), utf8 ("MIDIトラックの空き場所にクリップを作る"),
                             utf8 ("ツール"), 0);
            result.addDefaultKeypress ('2', juce::ModifierKeys::noModifiers);
            result.setTicked (arrangeView.getEditTool() == EditTool::pencil);
            break;

        case CommandIds::selectCutTool:
            result.setInfo (utf8 ("カットツール"), utf8 ("クリックした位置でクリップを割る"),
                             utf8 ("ツール"), 0);
            result.addDefaultKeypress ('4', juce::ModifierKeys::noModifiers);
            result.setTicked (arrangeView.getEditTool() == EditTool::cut);
            break;

        case CommandIds::selectEraserTool:
            result.setInfo (utf8 ("消しゴムツール"), utf8 ("触れたクリップ・ノートを消す（なぞると続けて消える）"),
                             utf8 ("ツール"), 0);
            // **3ではなく5**：3は畳んだ範囲ツールが使っていた番号で、
            // 環境設定に古い割り当てが残っていることがある（8.6）
            result.addDefaultKeypress ('5', juce::ModifierKeys::noModifiers);
            result.setTicked (arrangeView.getEditTool() == EditTool::eraser);
            break;

        case CommandIds::selectAllClips:
            result.setInfo (utf8 ("すべてを選択"), utf8 ("すべてのクリップを選ぶ"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('a', juce::ModifierKeys::commandModifier);
            break;

        case CommandIds::deselectAllClips:
            result.setInfo (utf8 ("すべての選択を解除"), utf8 ("クリップの選択を外す"), utf8 ("編集"), 0);
            result.addDefaultKeypress ('d', juce::ModifierKeys::commandModifier);
            result.setActive (arrangeView.getNumSelectedClips() > 0);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case CommandIds::newProject:    newProject();       return true;
        case CommandIds::newFromTemplate: newProjectFromTemplate(); return true;
        case CommandIds::saveAsTemplate:  saveCurrentAsTemplate();  return true;
        case CommandIds::openProject:   openProject();      return true;
        case CommandIds::saveProject:   saveProject();      return true;
        case CommandIds::saveProjectAs: saveProjectAs();    return true;
        case CommandIds::autoSaveNow:   autoSave.saveNow(); return true;
        case CommandIds::exportMixdown: exportMixdown();    return true;
        case CommandIds::exportStems:   exportStems();      return true;
        case CommandIds::exportMidi:    exportMidiFile();   return true;
        case CommandIds::showPreferences:
            PreferencesDialog::launch (audioEngine, &commandManager,
                                        [this] { browserPanel.refreshPluginList(); });
            return true;
        case CommandIds::undo:          undo();             return true;
        case CommandIds::redo:          redo();             return true;

        //======================================================================
        // 仕様書6.2：ショートカット（Phase 47）。
        // **どれも既存の処理を呼ぶだけ**にしてある（ボタンから押したときとの挙動差を作らない）。

        case CommandIds::playStop:      playButtonClicked();   return true;
        case CommandIds::stopTransport: stopTransport();       return true;
        case CommandIds::recordToggle:  recordButtonClicked(); return true;

        case CommandIds::returnToStart:
            audioEngine.setPlayheadSeconds (0.0);
            setPlayheadDisplay (0.0);
            transportBar.setPlayheadSeconds (0.0);
            return true;

        case CommandIds::toggleClick:       toggleMetronome();       return true;
        case CommandIds::toggleCountIn:     toggleCountIn();         return true;
        case CommandIds::toggleInputMonitor: toggleInputMonitoring(); return true;

        case CommandIds::toggleEditorPanel:
            // フッターのボタンと同じ経路を通す（Phase 66）
            toggleEditorContent (lastEditorContent);
            return true;

        // Phase 66：ページの切り替えから「Consoleパネルの開閉」に変わった（8.27）。
        // **コマンドのIDと既定のキー（F3）はそのまま。** 割り当てを変えると、
        // 環境設定で覚えている設定と食い違う（8.6の`createXml(true)`の話）
        case CommandIds::toggleConsolePage:
            toggleEditorContent (EditorContent::console);
            return true;

        case CommandIds::toggleInspectorPanel:
            setInspectorPanelOpen (! inspectorPanel.isVisible());
            return true;

        case CommandIds::toggleBrowserPanel:
            setBrowserPanelOpen (! browserPanel.isVisible());
            return true;

        // 仕様書5.9：ズーム（Phase 67）。**倍率は画面ごとに別の値**なので、
        // 配るのではなく振り分ける。ピアノロールを触っている最中は
        // そちらへ、それ以外はアレンジ画面へ（`isPianoRollFocused()`）
        case CommandIds::zoomIn:
            if (isPianoRollFocused()) pianoRollView.zoomIn(); else arrangeView.zoomIn();
            return true;

        case CommandIds::zoomOut:
            if (isPianoRollFocused()) pianoRollView.zoomOut(); else arrangeView.zoomOut();
            return true;

        case CommandIds::zoomToFit:
            if (isPianoRollFocused()) pianoRollView.zoomToFit(); else arrangeView.zoomToFit();
            return true;

        case CommandIds::addTrack:    arrangeView.showAddTrackMenuFromKeyboard(); return true;
        case CommandIds::deleteTrack: arrangeView.deleteSelectedTrack();          return true;

        case CommandIds::toggleArm:  toggleSelectedTrackFlag (TrackFlag::armed); return true;
        case CommandIds::toggleSolo: toggleSelectedTrackFlag (TrackFlag::solo);  return true;
        case CommandIds::toggleMute: toggleSelectedTrackFlag (TrackFlag::mute);  return true;

        case CommandIds::quitApp:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return true;

        //======================================================================
        // 仕様書5.9：ループ再生（Phase 48）

        case CommandIds::toggleLoop:
            project.beginAction (utf8 ("ループの入切"));
            project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());
            applyLoopSettings();
            return true;

        case CommandIds::goToLoopStart: seekTo (project.getLoopStartTime()); return true;
        case CommandIds::goToLoopEnd:   seekTo (project.getLoopEndTime());   return true;

        case CommandIds::setLoopStart:
            project.beginAction (utf8 ("ループの先頭を設定"));
            project.setLoopRange (audioEngine.getPlayheadSeconds(), project.getLoopEndTime(),
                                   &project.getUndoManager());
            applyLoopSettings();
            return true;

        case CommandIds::setLoopEnd:
            project.beginAction (utf8 ("ループの終わりを設定"));
            project.setLoopRange (project.getLoopStartTime(), audioEngine.getPlayheadSeconds(),
                                   &project.getUndoManager());
            applyLoopSettings();
            return true;

        case CommandIds::loopToSelection: loopToSelectedClip(); return true;

        //======================================================================
        // 仕様書5.9：マーカー（Phase 49）

        case CommandIds::insertMarker:
            insertMarkerAt (audioEngine.getPlayheadSeconds(), false);
            return true;

        case CommandIds::insertNamedMarker:
            insertMarkerAt (audioEngine.getPlayheadSeconds(), true);
            return true;

        case CommandIds::goToPreviousMarker:
        {
            auto marker = project.findMarkerBefore (audioEngine.getPlayheadSeconds());

            if (marker.state.isValid())
                seekTo (marker.getTime());

            return true;
        }

        case CommandIds::goToNextMarker:
        {
            auto marker = project.findMarkerAfter (audioEngine.getPlayheadSeconds());

            if (marker.state.isValid())
                seekTo (marker.getTime());

            return true;
        }

        //======================================================================
        // 仕様書5.5：クリップの分割・結合・複製（Phase 50）

        case CommandIds::splitClip:     splitSelectedClip();     return true;
        case CommandIds::mergeClip:     mergeSelectedClip();     return true;

        // 8.78：クリップのグループ（Phase 118/改善案㊱）。判断はアレンジ画面に1つだけ
        case CommandIds::groupClips:    arrangeView.groupSelectedClips();   return true;
        case CommandIds::ungroupClips:  arrangeView.ungroupSelectedClips(); return true;
        case CommandIds::duplicateClip: duplicateSelectedClip(); return true;

        // 仕様書6.2：カット／コピー／貼り付け（Phase 71）。
        // **ズームと同じ振り分け**（`isPianoRollFocused()`）。倍率と違って
        // 入れ物は共通だが、**どこから取ってどこへ入れるか**は画面ごとに違う
        case CommandIds::cutSelection:
            if (isPianoRollFocused()) pianoRollView.cutSelection(); else arrangeView.cutSelection();
            return true;

        case CommandIds::copySelection:
            if (isPianoRollFocused()) pianoRollView.copySelection(); else arrangeView.copySelection();
            return true;

        case CommandIds::pasteSelection:
            // **貼り付け先は再生カーソル。** メニューからの「ここに貼り付け」は
            // 押した位置に入る（そちらは見えている場所なので基準が違う。8.32）
            if (isPianoRollFocused())
                pianoRollView.pasteAtTimelineTime (audioEngine.getPlayheadSeconds());
            else
                arrangeView.pasteAt (audioEngine.getPlayheadSeconds());

            return true;

        //======================================================================
        // 仕様書6.2：ツール切り替えと複数選択（Phase 51）

        case CommandIds::selectArrowTool:
            setEditTool (EditTool::arrow);
            commandManager.commandStatusChanged();
            return true;

        case CommandIds::selectPencilTool:
            setEditTool (EditTool::pencil);
            commandManager.commandStatusChanged();
            return true;

        case CommandIds::selectCutTool:
            setEditTool (EditTool::cut);
            commandManager.commandStatusChanged();
            return true;

        case CommandIds::selectEraserTool:
            setEditTool (EditTool::eraser);
            commandManager.commandStatusChanged();
            return true;

        case CommandIds::selectAllClips:
            arrangeView.selectAllClips();
            commandManager.commandStatusChanged();
            return true;

        case CommandIds::deselectAllClips:
            arrangeView.clearClipSelection();
            commandManager.commandStatusChanged();
            return true;

        default: break;
    }

    return false;
}

//==============================================================================
// 仕様書6.2：ショートカットから呼ばれる操作（Phase 47）

void MainComponent::toggleSelectedTrackFlag (TrackFlag flag)
{
    auto track = project.findTrackById (selection.getTrackId());

    if (! track.state.getParent().isValid())
        return;

    // **区切りを操作ごとに入れる**（3.1）。連打されるボタンなので、
    // 入れないと一連の入切がまとめて1ステップのUndoになる。
    switch (flag)
    {
        case TrackFlag::armed:
            project.beginAction (utf8 ("録音待機の切り替え"));
            track.setArmed (! track.isArmed(), &project.getUndoManager());
            break;

        case TrackFlag::solo:
            project.beginAction (utf8 ("ソロの切り替え"));
            track.setSoloed (! track.isSoloed(), &project.getUndoManager());
            break;

        case TrackFlag::mute:
            project.beginAction (utf8 ("ミュートの切り替え"));
            track.setMuted (! track.isMuted(), &project.getUndoManager());
            break;
    }

    // 仕様書5.7：ミュート／ソロは音にも効かせる必要がある（モデルを見ているのは画面だけ）
    audioEngine.updateMixerSettings();

    refreshAllViews (true);
}

void MainComponent::toggleMetronome()
{
    const bool shouldBeEnabled = ! audioEngine.isMetronomeEnabled();

    audioEngine.setMetronomeEnabled (shouldBeEnabled);
    AppSettings::setInt (metronomeEnabledKey, shouldBeEnabled ? 1 : 0);

    // トランスポートバーのボタンの見た目も合わせる（押したのはキーボードなので、
    // ボタン側は自分では変わらない）
    transportBar.setMetronomeEnabled (shouldBeEnabled);
}

void MainComponent::toggleCountIn()
{
    // 0（無し）と1小節を行き来する。2小節は右クリックのメニューから選ぶ（Phase 39）
    const int newBars = (getCountInBars() > 0) ? 0 : 1;
    AppSettings::setInt (countInBarsKey, newBars);
}

void MainComponent::toggleInputMonitoring()
{
    audioEngine.setInputMonitoringEnabled (! audioEngine.isInputMonitoringEnabled());
    arrangeView.refreshInputState();
}

//==============================================================================
// 仕様書5.9：ループ再生（Phase 48）

void MainComponent::setPlayheadDisplay (double seconds)
{
    // 仕様書5.9：再生位置は**画面ごとに持っている**（Phase 72でピアノロールにも出した）。
    // **配るのはここ1箇所。** 呼び出し側に2行書かせると、必ずどこかで片方を足し忘れ、
    // 「アレンジでは動くのにピアノロールでは止まっている」状態になる（8.33）
    arrangeView.setPlayheadSeconds (seconds);
    pianoRollView.setPlayheadSeconds (seconds);
    audioEditorView.setPlayheadSeconds (seconds);   // Phase 79：オーディオエディタにも出す

    // 8.54：**フェーダーも再生位置を見て値を決める**（Phase 93）。
    // オートメーションはモデルの`volume`を書き換えない作りなので、
    // 「いま鳴っている値」を知るには画面が自分で引きに行くしかない。
    // **配る役はここが既に持っている**ので、判断ではなく1行だけ足している
    consoleView.setPlayheadSeconds (seconds);
    inspectorPanel.setPlayheadSeconds (seconds);
}

void MainComponent::seekTo (double seconds)
{
    // **停止中はタイマーが回っていない**ので、表示は自分で合わせる（1.28）
    audioEngine.setPlayheadSeconds (seconds);
    setPlayheadDisplay (seconds);
    transportBar.setPlayheadSeconds (seconds);
    chordPadPanel.setInsertPosition (seconds);
}

void MainComponent::applyProjectKeyToViews()
{
    const auto key = project.getProjectKey();

    // **コードトラックが無ければ押せなくする。** 選んでも何も起きないので
    // （`setProjectKey()`がfalseを返す）、押せるままだと壊れて見える
    transportBar.setProjectKey (key.root, key.minor,
                                 project.findChordTrack().state.getParent().isValid());

    // **コードパッドはここから触らない。** あちらはプロジェクトのValueTreeを
    // 自分で購読していて（`ChordPadPanel::affectsPads`）、キーの変化で
    // グリッドを組み直す。ここからも呼ぶと、同じ更新の経路が2本になる（1.15）
}

void MainComponent::applySnapGrid (SnapGrid newGrid)
{
    project.setSnapGrid (newGrid);

    // **両方の入口へ配る。** 選んだ側にも配り直しているのは、
    // 「決めるのは1箇所」を崩さないため（選んだ側だけ自分で更新する形にすると、
    // 次に入口が増えたときに必ず配り忘れる。1.27）
    arrangeView.setSnapGrid (newGrid);
    pianoRollView.setSnapGrid (newGrid);
}

void MainComponent::applyLoopSettings()
{
    // **モデルが正で、エンジンはその写し。** 値を変えた側がここを通すことで、
    // 「画面ではループしているのに音は繰り返さない」という食い違いを防ぐ。
    audioEngine.setLoop (project.isLoopEnabled(),
                          project.getLoopStartTime(), project.getLoopEndTime());

    transportBar.setLoopEnabled (project.isLoopEnabled());
    setPlayheadDisplay (audioEngine.getPlayheadSeconds()); // 帯の描き直しを促す

    // コマンドの表示（チェックの有無）も作り直す
    commandManager.commandStatusChanged();
}

//==============================================================================
// 仕様書5.5：クリップの分割・結合・複製（Phase 50）

void MainComponent::setEditTool (EditTool tool)
{
    // 仕様書6.2：**両方の画面へ同じ値を配る**（Phase 52）。
    // ピアノロールはエディタパネルの中にもポップアウト先にもあり得るが、
    // 実体は1つなので、ここで渡せば両方に効く
    arrangeView.setEditTool (tool);
    pianoRollView.setEditTool (tool);
}

juce::ValueTree MainComponent::getSelectedClipState (Track& trackOut) const
{
    if (! selection.isClipSelected())
        return {};

    trackOut = project.findTrackById (selection.getTrackId());

    if (! trackOut.state.getParent().isValid())
        return {};

    const int clipIndex = selection.getClipIndex();

    // 8.94：**クリップはオーディオだけ**（Phase 134）
    if (selection.getType() == SelectionState::Type::MidiClip)
        return {};

    if (! juce::isPositiveAndBelow (clipIndex, trackOut.getNumClips()))
        return {};

    return trackOut.getClip (clipIndex).state;
}

void MainComponent::splitSelectedClip()
{
    Track track { juce::ValueTree() };
    auto clipState = getSelectedClipState (track);

    if (! clipState.isValid())
        return;

    project.beginAction (utf8 ("クリップの分割"));

    if (! track.splitClipAt (clipState, audioEngine.getPlayheadSeconds(), &project.getUndoManager()))
    {
        arrangeView.showStatusMessage (utf8 ("再生位置がクリップの内側にありません（分割できませんでした）。"));
        return;
    }

    // 割ったことで、選択していたクリップの番号が別のものを指す可能性がある。
    // 中途半端な選択を残すより外すほうが安全（1.32）
    selection.selectNone();
    refreshAllViews (true);
}

void MainComponent::mergeSelectedClip()
{
    Track track { juce::ValueTree() };
    auto clipState = getSelectedClipState (track);

    if (! clipState.isValid())
        return;

    juce::String reason;

    project.beginAction (utf8 ("クリップの結合"));

    if (! track.mergeClipWithNext (clipState, &project.getUndoManager(), reason))
    {
        arrangeView.showStatusMessage (reason);
        return;
    }

    selection.selectNone();
    refreshAllViews (true);
}

void MainComponent::duplicateSelectedClip()
{
    Track track { juce::ValueTree() };
    auto clipState = getSelectedClipState (track);

    if (! clipState.isValid())
        return;

    project.beginAction (utf8 ("クリップの複製"));
    track.duplicateClip (clipState, &project.getUndoManager());

    refreshAllViews (true);
}

void MainComponent::insertMarkerAt (double timeSeconds, bool askForName)
{
    const double time = juce::jmax (0.0, timeSeconds);

    if (! askForName)
    {
        project.beginAction (utf8 ("マーカーの挿入"));
        project.addMarker (time, project.getNextMarkerName(), &project.getUndoManager());

        // マーカーはValueTreeの子として増えるので、タイムラインは購読で追従する。
        // コマンドの有効/無効（マーカーが0本かどうか）だけは自分で作り直す
        commandManager.commandStatusChanged();
        return;
    }

    // **モーダルループは回さない**（オーディオのタイマーごと止まる）。非同期で出す
    auto* window = new juce::AlertWindow (utf8 ("マーカーを挿入"),
                                           utf8 ("マーカーの名前を入力してください。"),
                                           juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("name", project.getNextMarkerName(), utf8 ("名前"));
    window->addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (utf8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, window, time] (int result)
        {
            if (result == 1)
            {
                const auto name = window->getTextEditorContents ("name").trim();

                project.beginAction (utf8 ("マーカーの挿入"));
                project.addMarker (time, name.isNotEmpty() ? name : project.getNextMarkerName(),
                                    &project.getUndoManager());

                commandManager.commandStatusChanged();
            }

            delete window;
        }),
        false);
}

void MainComponent::loopToSelectedClip()
{
    if (! selection.isClipSelected())
        return;

    auto track = project.findTrackById (selection.getTrackId());

    if (! track.state.getParent().isValid())
        return;

    const int clipIndex = selection.getClipIndex();
    double startTime = 0.0;
    double lengthSeconds = 0.0;

    // 8.94：**クリップはオーディオだけ**（Phase 134）
    if (selection.getType() == SelectionState::Type::MidiClip)
        return;

    {
        if (! juce::isPositiveAndBelow (clipIndex, track.getNumClips()))
            return;

        auto clip = track.getClip (clipIndex);
        startTime = clip.getStartTime();
        lengthSeconds = clip.getLength();
    }

    if (lengthSeconds <= 0.0)
        return;

    project.beginAction (utf8 ("選択をループ"));
    project.setLoopRange (startTime, startTime + lengthSeconds, &project.getUndoManager());
    project.setLoopEnabled (true, &project.getUndoManager());

    applyLoopSettings();
}

void MainComponent::undo()
{
    if (! project.getUndoManager().undo())
        return;

    // ValueTreeが巻き戻っても、それを購読していないビュー（タイムライン等）は
    // 自動では描き直されない。モデルが外から変わったときと同じ扱いで作り直す。
    // 選択は残す：プロジェクトは同じなので、覚えているtrackIdは有効なまま（Phase 34）。
    refreshAllViews (true);

    // 仕様書5.7：フェーダー操作が巻き戻った場合、音にも反映する必要がある
    audioEngine.updateMixerSettings();
}

void MainComponent::redo()
{
    if (! project.getUndoManager().redo())
        return;

    refreshAllViews (true); // undo()と同じ理由で選択は残す
    audioEngine.updateMixerSettings();
}

void MainComponent::offerAutoSaveRecovery()
{
    if (! autoSave.hasRecoverableAutoSave())
        return;

    auto autoSaveFile = AutoSaveManager::getAutoSaveFile();
    const auto savedTime = autoSaveFile.getLastModificationTime().formatted ("%Y-%m-%d %H:%M:%S");

    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle (utf8 ("前回の作業内容が残っています"))
            .withMessage (utf8 ("前回、アプリが正常に終了しませんでした。\n")
                              + utf8 ("自動保存された内容（") + savedTime + utf8 ("）を復元しますか？\n\n")
                              + utf8 ("復元しない場合、この自動保存は破棄されます。"))
            .withButton (utf8 ("復元する"))
            .withButton (utf8 ("破棄する")),
        [this] (int buttonIndex)
        {
            // ボタンは押された順のインデックスで返る（0=復元する / 1=破棄する）。
            // HANDOVER 2章の「NativeMessageBoxの戻り値はインデックス」参照。
            if (buttonIndex != 0)
            {
                autoSave.clearAutoSave();
                return;
            }

            stopTransport();

            if (! autoSave.restoreFromAutoSave())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("復元できませんでした"))
                        .withMessage (utf8 ("自動保存ファイルを読み込めませんでした。"))
                        .withButton ("OK"),
                    nullptr);
                return;
            }

            const auto pluginErrors = audioEngine.restorePluginsFromProject();
            refreshAllViews();

            arrangeView.showStatusMessage (utf8 ("自動保存から復元しました。内容を確認して保存してください。"));

            if (! pluginErrors.isEmpty())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("一部のプラグインを復元できませんでした"))
                        .withMessage (pluginErrors.joinIntoString ("\n"))
                        .withButton ("OK"),
                    nullptr);
            }
        });
}

void MainComponent::stopTransport()
{
    // 再生・録音中に中身を入れ替えると、ClipPlayerProcessorが既に消えたクリップを
    // 参照し続けることになるため、プロジェクトを差し替える前に必ず止める。
    audioEngine.stopRecording();
    audioEngine.stop();
    stopTimer();
    transportBar.setPlayingState (false);
    transportBar.setRecordingState (false);
}

void MainComponent::confirmDiscardChanges (std::function<void (bool)> onResult)
{
    if (! project.hasUnsavedChanges())
    {
        onResult (true);
        return;
    }

    // 戻り値の注意：NativeMessageBoxは「押されたボタンの配列インデックス」を返す。
    // つまり 0=保存する／1=保存しない／2=キャンセル。
    // juce::AlertWindowのドキュメントには3ボタン時「1番目=1、2番目=2、最後=0」とあるが、
    // それはAlertWindow側の話で、NativeMessageBoxには当てはまらない
    // （Windows実装 juce_NativeMessageBox_windows.cpp のTaskDialogで、
    //   ボタンIDに配列インデックスをそのまま割り当てている）。
    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle (utf8 ("変更が保存されていません"))
            .withMessage (utf8 ("プロジェクト「") + project.getName() + utf8 ("」への変更が保存されていません。\n")
                              + utf8 ("保存しますか？"))
            .withButton (utf8 ("保存する"))
            .withButton (utf8 ("保存しない"))
            .withButton (utf8 ("キャンセル")),
        [this, onResult] (int buttonIndex)
        {
            // 2 は「キャンセル」。下のとおり「保存しない」以外はすべて中止扱いになるため、
            // 定数としては使っていない。
            enum { saveButton = 0, discardButton = 1 };

            if (buttonIndex == saveButton)
            {
                // 保存できたときだけ続行する。保存ダイアログでキャンセルされた場合に
                // そのまま破棄してしまうと、ユーザーの意図と逆の結果になる。
                saveProject ([onResult] (bool saved) { onResult (saved); });
                return;
            }

            // 「保存しない」のときだけ続行する。ダイアログを×やESCで閉じた場合も
            // ここに来るが、その場合は「キャンセル」と同じ扱い（＝変更を捨てない）になる。
            onResult (buttonIndex == discardButton);
        });
}

void MainComponent::newProject()
{
    confirmDiscardChanges ([this] (bool shouldProceed)
    {
        if (! shouldProceed)
            return;

        stopTransport();

        // トラックの音源・インサートは、この後のcreateNewProject()でルートが
        // 差し替わることで走るrebuildTrackNodes()が片付ける（Phase 26。
        // それ以前はトラックに属さない試聴用プラグインをここで外していた）。

        // 前のプロジェクトのオートセーブを残すと、クラッシュ時に
        // 「今開いているものとは別のプロジェクト」の復元を提案してしまう
        autoSave.clearAutoSave();

        project.createNewProject();

        // 8.69：**マスターのインサートは自分で片付ける**（Phase 108／D6）。
        // マスターはトラックではないので`rebuildTrackNodes()`の対象外で、
        // これが無いと**新規プロジェクトに前のプロジェクトのインサートが残ります**
        audioEngine.restorePluginsFromProject();

        refreshAllViews();
    });
}

void MainComponent::applyTemplate (const ProjectTemplates::Entry& entry)
{
    // 8.151：**`newProject()`と同じ手順**（Phase 189／D8）。
    // 違うのは`createNewProject()`のところが`ProjectTemplates::apply()`になる点だけで、
    // 前後の片付け（止める・オートセーブを捨てる・マスターのインサートを直す）は同じです
    stopTransport();

    autoSave.clearAutoSave();

    const auto error = ProjectTemplates::apply (project, entry);

    if (error.isNotEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("テンプレートを使えませんでした"))
                .withMessage (error)
                .withButton ("OK"),
            nullptr);

        // **読めなかったときは空のプロジェクトへ落とす。**
        // 途中まで読めた状態で放り出すと、何が入っているか分かりません
        project.createNewProject();
    }

    // 8.69：マスターのインサートは`rebuildTrackNodes()`の対象外なので自分で片付ける
    audioEngine.restorePluginsFromProject();

    refreshAllViews();
}

void MainComponent::newProjectFromTemplate()
{
    confirmDiscardChanges ([this] (bool shouldProceed)
    {
        if (! shouldProceed)
            return;

        // **起動時と同じ画面を出します**（1.27：テンプレートの一覧は1箇所）。
        // 起動時と違い、選ばずに閉じても終了はしません——いま開いているものが
        // あるので、「やめる」は「そのまま続ける」という意味になります
        auto* window = new ProjectChooserWindow();

        window->onChosen = [this, window] (const ProjectChooser::Result& result)
        {
            // 1.5：**自分のコールバックの中で自分を破棄しない。** ここで消すと、
            // 呼び出し元（ウィンドウのボタン）のスタックが解放済みメモリを触る
            juce::MessageManager::callAsync ([window] { delete window; });

            switch (result.type)
            {
                case ProjectChooser::Result::Type::fromTemplate:
                    applyTemplate (result.entry);
                    break;

                case ProjectChooser::Result::Type::openFile:
                    loadProjectFile (result.file);
                    break;

                case ProjectChooser::Result::Type::quit:
                    break;   // 起動時と違い、ここでは何もしない
            }
        };
    });
}

void MainComponent::saveCurrentAsTemplate()
{
    // 設計書3.8：保存の直前に、ロード中プラグインの内部状態を取り込む
    // （手動保存と同じ。取らないと、つまみを動かした結果が入りません）
    audioEngine.capturePluginStatesIntoProject();

    const auto suggested = project.getName().isNotEmpty() ? project.getName()
                                                           : utf8 ("マイテンプレート");

    NameEntry::show (utf8 ("テンプレートとして保存"),
                      utf8 ("いまのプロジェクトを、新しいプロジェクトの雛形として保存します。\n")
                          + utf8 ("保存先: ") + ProjectTemplates::getFolder().getFullPathName(),
                      suggested,
                      [this] (const juce::String& name)
                      {
                          const auto error = ProjectTemplates::saveCurrentAsTemplate (project, name);

                          if (error.isNotEmpty())
                          {
                              juce::NativeMessageBox::showAsync (
                                  juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (utf8 ("テンプレートを保存できませんでした"))
                                      .withMessage (error)
                                      .withButton ("OK"),
                                  nullptr);
                              return;
                          }

                          arrangeView.showStatusMessage (utf8 ("テンプレート「") + name.trim()
                                                              + utf8 ("」を保存しました。"));
                      },
                      utf8 ("テンプレート名"));
}

void MainComponent::beginStartupChecks()
{
    // 設計書3.5：前回セッションでプラグイン処理中に異常終了していないかを確認する。
    // マーカーが残っていれば、そのプラグインのcrashCountが加算される。
    const auto crashedPlugin = audioEngine.getCrashTracker().checkForPreviousCrash();

    if (crashedPlugin.isNotEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("前回の異常終了を検知しました"))
                .withMessage (utf8 ("前回、以下のプラグインの処理中にアプリが正常終了しませんでした。\n\n")
                                  + crashedPlugin
                                  + utf8 ("\n\n環境設定のPluginsで、このプラグインのクラッシュ回数を確認できます。"))
                .withButton ("OK"),
            nullptr);
    }

    // 前回のオートセーブが残っている＝前回は正常終了していない、と判断して復元を提案する。
    // ダイアログは非同期なので、この時点ではまだ結果が出ていない。
    offerAutoSaveRecovery();

    // 8.157：**プラグインは裏で見に行く**（Phase 195／8.1のE2）
    startBackgroundPluginScan();
}

void MainComponent::startBackgroundPluginScan()
{
    auto& manager = audioEngine.getPluginManager();

    // **始める前に、前回落ちたものを弾く**（`PluginManager.h`）。
    // ここはメッセージスレッド
    const auto skipped = manager.blacklistWhatCrashedLastTime();

    if (! skipped.isEmpty())
        arrangeView.showStatusMessage (utf8 ("前回のスキャンで落ちた ")
                                           + juce::String (skipped.size())
                                           + utf8 (" 個は、これから飛ばします。"));

    const bool wasEmpty = manager.hasNoKnownPlugins();
    const int knownBefore = manager.getKnownPlugins().size();

    // **分かっているものを渡す**（別スレッドから`knownPlugins`を読まないため）
    const auto alreadyKnown = manager.getKnownPlugins();

    juce::Component::SafePointer<MainComponent> safeThis (this);
    auto* managerPtr = &manager;

    pluginScanPool.addJob ([safeThis, managerPtr, alreadyKnown, wasEmpty, knownBefore]
    {
        const auto found = managerPtr->scanFoldersWithoutApplying ({}, alreadyKnown);

        juce::MessageManager::callAsync ([safeThis, managerPtr, found, wasEmpty, knownBefore]
        {
            // **戻ってきたときに窓が生きているとは限らない**（終了された）
            auto* self = safeThis.getComponent();

            if (self == nullptr)
                return;

            managerPtr->mergeScannedPlugins (found);

            // ブラウザパネルも同じ一覧を見ているので知らせる（`PluginScanView`と同じ）
            self->browserPanel.refreshPluginList();

            const int added = managerPtr->getKnownPlugins().size() - knownBefore;

            // **何も増えていないときは黙っている。** 起動のたびに
            // 「0個見つかりました」と出ると、ただの雑音になります
            if (added > 0)
                self->arrangeView.showStatusMessage (
                    (wasEmpty ? utf8 ("プラグインを ") : utf8 ("新しいプラグインを "))
                        + juce::String (added) + utf8 (" 個みつけました。"));
        });
    });
}

bool MainComponent::hasRecoverableAutoSave() const
{
    return autoSave.hasRecoverableAutoSave();
}

void MainComponent::openProject()
{
    confirmDiscardChanges ([this] (bool shouldProceed)
    {
        if (shouldProceed)
            showOpenProjectChooser();
    });
}

void MainComponent::showOpenProjectChooser()
{
    // 設計書2.3.8：最初に開くフォルダは環境設定から引く（Phase 57／8.17）。
    // 以前は指定しておらず、OSが覚えている場所（前回どこかで使ったフォルダ）が出ていた
    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("プロジェクトを開く"),
                                                        StorageLocations::getFolder (StorageLocations::Kind::projects),
                                                        ProjectModel::getFileWildcard());

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file != juce::File())
                loadProjectFile (file);
        });
}

void MainComponent::loadProjectFile (const juce::File& file)
{
    stopTransport();

    if (! project.loadFromFile (file))
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("プロジェクトを開けませんでした"))
                .withMessage (utf8 ("ファイルを読み込めないか、プロジェクト形式ではありません:\n")
                                  + file.getFullPathName())
                .withButton ("OK"),
            nullptr);
        return;
    }

    autoSave.clearAutoSave(); // 別のプロジェクトに切り替わったので、前の控えは捨てる

    // 8.151：起動時の選択画面に出す履歴（Phase 189／⑰）。
    // **開いたときと、名前を付けて保存したときの両方で足すこと**（`RecentProjects.h`）
    RecentProjects::add (file);

    // 設計書3.8：保存されていたプラグインを読み込み直し、内部状態を復元する。
    // 失敗しても読み込み自体は続行する（プラグインが1つ見つからないだけで
    // プロジェクトが開けなくなるのは不便なため）。
    const auto pluginErrors = audioEngine.restorePluginsFromProject();

    refreshAllViews();

    if (! pluginErrors.isEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("一部のプラグインを復元できませんでした"))
                .withMessage (utf8 ("以下のプラグインは読み込めませんでした。\n")
                                  + utf8 ("プロジェクトの他の内容は読み込まれています。\n\n")
                                  + pluginErrors.joinIntoString ("\n"))
                .withButton ("OK"),
            nullptr);
    }
}

void MainComponent::saveProject (std::function<void (bool)> onComplete)
{
    // 保存先がまだ決まっていない新規プロジェクトなら、名前を付けて保存に回す
    if (project.getCurrentFile() == juce::File())
    {
        saveProjectAs (std::move (onComplete));
        return;
    }

    // 設計書3.8：保存の直前に、ロード中プラグインの内部状態を取り込む。
    // ここで取らないと、読み込んだ後につまみを動かした結果が保存されない。
    audioEngine.capturePluginStatesIntoProject();

    const auto file = project.getCurrentFile();
    const bool saved = project.saveToFile (file);

    if (! saved)
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("保存できませんでした"))
                .withMessage (utf8 ("書き込みに失敗しました:\n") + file.getFullPathName())
                .withButton ("OK"),
            nullptr);
    }

    // 手動保存できた時点で、オートセーブ（＝復旧用の控え）は役目を終える。
    // 残したままにすると、次回起動時に「前回異常終了しました」と誤って案内してしまう。
    if (saved)
        autoSave.clearAutoSave();

    updateWindowTitle();

    if (onComplete != nullptr)
        onComplete (saved);
}

void MainComponent::saveProjectAs (std::function<void (bool)> onComplete)
{
    // 設計書2.3.8：まだ保存していないプロジェクトは、環境設定の保存先から始める
    // （Phase 57／8.17）。**既に保存してあるものは、そのファイルの隣から**：
    // 設定を後から変えたときに、開いているプロジェクトが別の場所へ飛ばないため
    auto initialFile = project.getCurrentFile() != juce::File()
                            ? project.getCurrentFile()
                            : StorageLocations::getFolder (StorageLocations::Kind::projects)
                                  .getChildFile (project.getName() + ProjectModel::getFileExtension());

    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("名前を付けて保存"),
                                                        initialFile,
                                                        ProjectModel::getFileWildcard());

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, onComplete] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File())
            {
                if (onComplete != nullptr)
                    onComplete (false); // 保存ダイアログでキャンセルされた
                return;
            }

            // 拡張子を省略して入力された場合に備えて補う。
            // **古い拡張子を選び直しても新しいものへ寄せる**（Phase 56）。
            // 上書き保存（Ctrl+S）は今開いているファイルへそのまま書くので、
            // 古いファイルを開いて上書きしたときだけ`.pdawproj`のまま残る
            if (! file.hasFileExtension (ProjectModel::getFileExtension()))
                file = file.withFileExtension (ProjectModel::getFileExtension());

            // ファイル名をプロジェクト名として採用する（タイトル表示と一致させるため）。
            // 保存の前に設定しないと、この変更がファイルへ書き込まれない。
            // ここだけはUndoManagerを渡さない（保存操作をUndoで巻き戻せても意味が無く、
            // 直後のmarkAsSaved()と食い違う「未保存」状態を作ってしまうため）。
            project.setName (file.getFileNameWithoutExtension(), nullptr);

            // 設計書3.8：保存の直前にプラグインの内部状態を取り込む（saveProject()と同じ理由）
            audioEngine.capturePluginStatesIntoProject();

            const bool saved = project.saveToFile (file);

            if (! saved)
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("保存できませんでした"))
                        .withMessage (utf8 ("書き込みに失敗しました:\n") + file.getFullPathName())
                        .withButton ("OK"),
                    nullptr);
            }

            if (saved)
            {
                autoSave.clearAutoSave(); // saveProject()と同じ理由

                // 8.151：**保存でも履歴へ足す**（Phase 189／⑰）。
                // 開いたときだけだと、作ったばかりのプロジェクトが
                // 起動時の一覧に出ません（`RecentProjects.h`）
                RecentProjects::add (file);
            }

            updateWindowTitle();

            if (onComplete != nullptr)
                onComplete (saved);
        });
}

void MainComponent::exportMixdown()
{
    // 書き出し中は再生を止める（デバイスのコールバックを外すため、鳴らしたままにできない）
    stopTransport();

    // 8.80：**先に設定を聞く**（Phase 120／D9・D10・D11）。
    //
    // 保存先を選んだ後に設定を聞くと、「やっぱりやめる」ときに
    // **ファイル名だけ決まった状態**が残ります。決めることを先に済ませ、
    // 最後に「どこへ置くか」で終わるほうが素直です。
    ExportOptionsDialog::show (false, getLoopRangeSecondsForExport(), lastExportOptions,
        [this] (const ExportOptions& options)
        {
            // 8.153：**拡張子は`options`から引く**（Phase 191／D9b）。
            // ここで"wav"と書き写すと、MP3を選んでも`.wav`が出ます（8.2）
            const auto extension = options.getFileExtension();

            auto initialFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getChildFile (project.getName() + extension);

            fileChooser = std::make_unique<juce::FileChooser> (utf8 ("ミックスダウンの書き出し先"),
                                                                initialFile, "*" + extension);

            fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                        | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, options, extension] (const juce::FileChooser& fc)
                {
                    auto file = fc.getResult();

                    if (file == juce::File())
                        return;

                    if (! file.hasFileExtension (extension))
                        file = file.withFileExtension (extension);

                    // launchThread()はモーダル表示のまま処理を進め、
                    // 終わったらthreadComplete()が呼ばれる（そこで自分を破棄する）。
                    lastExportOptions = options;   // 8.81：次もこの設定から始める（Phase 121）
                    (new MixdownExportTask (audioEngine, file, false, options))->launchThread();
                });
        });
}

std::vector<ExportStemTrack> MainComponent::buildStemTrackList() const
{
    std::vector<ExportStemTrack> result;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);
        const auto type = track.getType();

        // 8.81：**ステムとして出るものだけ並べる**（Phase 121／D10a）。
        //
        // 条件は`AudioEngine::renderStemsToFolder()`と揃えること：
        // 自分で音を出すトラックとフォルダ（バス）。センドは単体では書き出さず
        // （送り元のステムに残響として入る）、コードとVCAは音を通しません
        // 8.145：**パラアウトの受け皿もステムに出す**（Phase 183／本人の要望）。
        //
        // キックだけ・スネアだけのステムが要る、というのがパラアウトの主な用途です。
        // **二重には入りません**：受け皿を書き出すときは音源トラックが黙り、
        // 音源トラックを書き出すときは受け皿が黙ります
        // （`AudioEngine::shouldTrackSoundForStem()`）
        if (type != TrackType::Audio && type != TrackType::Midi
             && type != TrackType::Folder && type != TrackType::DrumOut)
            continue;

        ExportStemTrack entry;
        entry.trackId = track.getId();
        entry.name = track.getName();
        entry.isFolder = (type == TrackType::Folder);
        entry.depth = project.getTrackFolderDepth (track);

        // **前回の選択はIDで引き継ぐ**（名前は変えられる。1.32）。
        // 一覧に無い＝新しく足したトラックは「書き出す・ステレオ」で始まる
        for (const auto& previous : lastExportOptions.stemTracks)
        {
            if (previous.trackId != entry.trackId)
                continue;

            entry.include = previous.include;
            entry.mono = previous.mono;
            break;
        }

        result.push_back (entry);
    }

    return result;
}

double MainComponent::getLoopRangeSecondsForExport() const
{
    // 8.80：ループが引かれている長さ（Phase 120／D11）。0以下なら引かれていない。
    //
    // **ループのON/OFFは見ません**（`AudioEngine::getExportRange()`と同じ決まり）。
    // 「再生時に繰り返すか」と「どこを書き出すか」は別の話です
    return juce::jmax (0.0, project.getLoopEndTime() - project.getLoopStartTime());
}

void MainComponent::exportStems()
{
    stopTransport();

    // 8.81：**その場のトラック一覧を渡す**（Phase 121/D10a）。
    // 前回の選択はIDで引き継ぐので、名前を変えても付いてきます
    lastExportOptions.stemTracks = buildStemTrackList();

    ExportOptionsDialog::show (true, getLoopRangeSecondsForExport(), lastExportOptions,
        [this] (const ExportOptions& options)
        {
            fileChooser = std::make_unique<juce::FileChooser> (
                              utf8 ("ステムの書き出し先フォルダ"),
                              juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));

            fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectDirectories,
                [this, options] (const juce::FileChooser& fc)
                {
                    auto folder = fc.getResult();

                    if (folder == juce::File() || ! folder.isDirectory())
                        return;

                    lastExportOptions = options;   // 8.81：次もこの設定から始める（Phase 121）
                    (new MixdownExportTask (audioEngine, folder, true, options))->launchThread();
                });
        });
}

void MainComponent::exportMidiFile()
{
    auto initialFile = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (project.getName() + ".mid");

    fileChooser = std::make_unique<juce::FileChooser> (utf8 ("MIDIファイルの書き出し先"),
                                                        initialFile, "*.mid");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File())
                return;

            if (! file.hasFileExtension ("mid"))
                file = file.withFileExtension ("mid");

            // MIDIの書き出しはノートを並べ替えて書くだけで、オーディオ処理を伴わない。
            // 一瞬で終わるため、進捗ウィンドウは出さずにその場で実行する。
            const auto error = MidiFileExporter::exportToFile (project, file);

            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (error.isNotEmpty() ? juce::MessageBoxIconType::WarningIcon
                                                       : juce::MessageBoxIconType::InfoIcon)
                    .withTitle (error.isNotEmpty() ? utf8 ("書き出しできませんでした")
                                                    : utf8 ("書き出しが完了しました"))
                    .withMessage (error.isNotEmpty() ? error : file.getFullPathName())
                    .withButton ("OK"),
                nullptr);
        });
}

void MainComponent::updateProjectSubscription()
{
    auto currentState = project.getState();

    if (currentState == subscribedProjectState)
        return;

    if (subscribedProjectState.isValid())
        subscribedProjectState.removeListener (this);

    subscribedProjectState = currentState;

    if (subscribedProjectState.isValid())
        subscribedProjectState.addListener (this);
}

void MainComponent::updateMetronomeTiming()
{
    // Phase 141：**画面と同じ表をそのまま渡す**（8.103）。
    // メトロノームはオーディオスレッドで拍を数えるので、ValueTreeではなく
    // 写し取った表を読みます（1.12）。**変化点を足したらここを呼ぶこと**
    audioEngine.updateMetronomeTiming (project.getTempoMap());
}

void MainComponent::snapPlayheadToBarForCountIn()
{
    const double current = audioEngine.getPlayheadSeconds();

    // **いちばん近い小節線へ。位置はProjectModelに訊く**（8.98／Phase 138）——
    // 「1小節の長さで割って丸める」と書くと、小節ごとに長さが違う形にしたときに崩れます
    const auto position = project.getBarBeatAt (current);
    const double thisBarStart = project.getBarStartTime (position.bar);
    const double nextBarStart = project.getBarStartTime (position.bar + 1);

    const double snapped = ((current - thisBarStart) <= (nextBarStart - current)) ? thisBarStart
                                                                                  : nextBarStart;

    if (juce::approximatelyEqual (snapped, current))
        return;

    audioEngine.setPlayheadSeconds (snapped);

    // 動かした位置を画面にも出す（停止中なのでタイマーは回っていない。HANDOVER 1.28）
    setPlayheadDisplay (snapped);
    transportBar.setPlayheadSeconds (snapped);
}

int MainComponent::getCountInBars() const
{
    // 0（無し）／1／2小節のみ。それ以上は待たされるだけで使い道が無い
    return juce::jlimit (0, 2, AppSettings::getInt (countInBarsKey, 0));
}

void MainComponent::showMetronomeSettingsMenu (juce::Rectangle<int> screenBounds)
{
    const int gainPercent = juce::jlimit (0, 100, AppSettings::getInt (metronomeGainKey, 50));
    const int countInBars = getCountInBars();

    juce::PopupMenu menu;

    menu.addSectionHeader (utf8 ("クリック音量"));
    menu.addItem (1, utf8 ("小"), true, gainPercent <= 30);
    menu.addItem (2, utf8 ("中"), true, gainPercent > 30 && gainPercent < 80);
    menu.addItem (3, utf8 ("大"), true, gainPercent >= 80);

    menu.addSectionHeader (utf8 ("録音のカウントイン"));
    menu.addItem (11, utf8 ("なし"), true, countInBars == 0);
    menu.addItem (12, utf8 ("1小節"), true, countInBars == 1);
    menu.addItem (13, utf8 ("2小節"), true, countInBars == 2);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenBounds),
        [this] (int result)
        {
            if (result >= 1 && result <= 3)
            {
                const int newPercent = (result == 1) ? 25 : (result == 2 ? 50 : 100);

                AppSettings::setInt (metronomeGainKey, newPercent);
                audioEngine.setMetronomeGain (newPercent / 100.0f);
            }
            else if (result >= 11 && result <= 13)
            {
                AppSettings::setInt (countInBarsKey, result - 11);
            }
        });
}

// Phase 141：テンポの表に効く変更かどうか（8.103）。
//
// **8.162（Phase 200）で`ProjectIds.h`へ移しました。** オーディオエンジンも
// 同じ判定を要るようになったためです（再生中のテンポ変更に音を追いつかせる）。
// **書き写さないこと**（8.2）——書き写すと、片方だけ直したときに
// 「画面は正しいのにクリックだけずれる」が戻ってきます。


void MainComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // ルートを購読しているのでノートの打ち込みまで通知が飛んでくる。
    // ここで種別を絞っているので、実質的な負荷は比較1回ぶんしかない（ConsoleViewと同じ作り）。
    if (property == IDs::volume && tree.hasType (IDs::MASTERBUS))
        transportBar.setMasterVolumeDb (project.getMasterVolumeDb());

    // 仕様書5.11.1：キーはコードパッドからも変えられる（Phase 63／8.1のC9）。
    // **ValueTreeで追うのが要**：コードパッドから通知してもらう形にすると、
    // Undo/Redoで戻したときに置いていかれる（1.19）。
    //
    // **ノード種別まで見ること**（1.42）。`chordKeyRoot`はTRACKのプロパティで、
    // 名前だけで判定すると他のノードの変化でも反応する。
    if (tree.hasType (IDs::TRACK)
         && (property == IDs::chordKeyRoot || property == IDs::chordKeyMinor))
        applyProjectKeyToViews();

    // Phase 141：テンポ・拍子が変わったらメトロノームの表を入れ替える（8.103）
    if (::isTempoMapChange (tree, property))
        updateMetronomeTiming();

    // 8.147：**半音の値が入ったら、裏で作らせる**（Phase 185／改善案㉞）。
    //
    // **モデルを見て追うこと**（1.15）。入る道はメニュー・インスペクタ・
    // Undo／Redo・ペースト・複製といくつもあり、入口ごとに「作れ」と言う形にすると
    // 必ずどれかを忘れます。**ノード種別まで見ます**（1.42）
    // 8.149：**伸縮も同じ道**（Phase 187／8.48）。どちらも`AudioTransform`が
    // キャッシュへ作るもので、値が入る道（メニュー・ドラッグ・Undo・読み込み）も同じです
    // 8.150：**ワープマーカーを動かしたときも同じ道**（Phase 188／8.48）。
    // マーカーは`<WARPMARKER>`の`clipTime`が変わる形で動きます
    if ((tree.hasType (IDs::AUDIOCLIP)
          && (property == IDs::clipTranspose || property == IDs::clipStretch))
         || tree.hasType (IDs::WARPMARKER))
    {
        clipAudioRenderer.requestScan();

        // 8.148：**待たされるなら、待たされると出すこと**（Phase 186／本人の報告）。
        //
        // 出来ているものへ戻したときは一瞬（キャッシュがある）、
        // 新しい値は音の長さに応じて時間がかかります。**何も出ないと、
        // 同じ操作が速かったり遅かったりするようにしか見えません。**
        //
        // 出来上がったときの文面は`onFinished`が上書きします。
        //
        // **出すのは、書き換えが終わってから。** ここは書き換えの通知の中で、
        // この後に画面の作り直し（`updateStatusLabel()`）が走ります——
        // その場で出すと**トラック一覧へ戻されて消えます**（8.146とまったく同じ話。実機で確認）
        if (clipAudioRenderer.hasPendingWork())
        {
            juce::Component::SafePointer<MainComponent> safeThis (this);

            juce::MessageManager::callAsync ([safeThis]
            {
                // **もう一度訊くこと。** 短いクリップは待つ間もなく出来上がるので、
                // 済んだ後に「作っています」と出すと嘘になります
                if (safeThis != nullptr && safeThis->clipAudioRenderer.hasPendingWork())
                    safeThis->arrangeView.showStatusMessage (
                        utf8 ("音を作り直しています…（出来るまでは元のまま鳴ります）"));
            });
        }
    }
}

void MainComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (IDs::TRACK))
        applyProjectKeyToViews();

    if (::isSignatureLaneNode (child))
        updateMetronomeTiming();

    // 8.150：マーカーを置いた／入れ物ごと戻した（Phase 188。Undoもここを通る）
    if (child.hasType (IDs::WARP) || child.hasType (IDs::WARPMARKER))
        clipAudioRenderer.requestScan();
}

void MainComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (IDs::TRACK))
        applyProjectKeyToViews();

    if (::isSignatureLaneNode (child))
        updateMetronomeTiming();

    // 8.150：マーカーを消した／全部消した（Phase 188）
    if (child.hasType (IDs::WARP) || child.hasType (IDs::WARPMARKER))
        clipAudioRenderer.requestScan();
}

void MainComponent::refreshAllViews (bool keepSelection)
{
    // 先にエンジン側のトラック再構築（音源・インサートの読み込み直し）を済ませておく。
    // これが未処理のままビューを更新すると、復元されたはずの音源がまだ載っていない
    // 状態を読んでしまい、「音源を読み込めませんでした」と誤って表示される。
    audioEngine.flushPendingTrackRebuild();

    // 8.147：**プロジェクトを開いたときも見て回る**（Phase 185／改善案㉞）。
    // 保存されているのは半音の数だけで、音そのものはキャッシュにしか無いので、
    // **キャッシュを消した／別のPCで開いた**ときはここで作り直します。
    // 半音の付いたクリップが1つも無ければ、ただの空回りです
    clipAudioRenderer.requestScan();

    // 覚えていたtrackIdは別プロジェクトのものになるため、選択も捨てる（Phase 17）。
    // インスペクタより先に消しておくと、無効なトラックを一瞬でも表示せずに済む。
    // **Undo/Redoは同じプロジェクトの中の話**なので、そちらでは捨てない（Phase 34）。
    if (! keepSelection)
        selection.selectNone();

    // ProjectModelの中身がまるごと入れ替わるため、各ビューが持っている
    // 「選択中のクリップ」等の参照は一度リセットする必要がある。
    arrangeView.refreshAfterProjectChanged (keepSelection);
    inspectorPanel.refreshAfterProjectChanged();
    browserPanel.refreshPluginList();
    pianoRollView.refreshAfterProjectChanged (keepSelection);
    // 8.41：**「差し替わった」と「巻き戻った」を分ける**（Phase 81。1.33）。
    // Undo/Redoは同じプロジェクトの中の話なので、掴んでいるクリップを捨てない。
    // 捨てると、Ctrl+Zのたびに「まだ何も無い」表示へ戻ってしまう
    audioEditorView.refreshAfterProjectChanged (keepSelection);
    chordPadPanel.refreshFromModel(); // Phase 43：キーも区間も別プロジェクトのものになる
    consoleView.refreshAfterProjectChanged();

    // プロジェクトを読み込むとルートのValueTreeが差し替わるので、購読も付け替える（Phase 37）
    updateProjectSubscription();

    // 設計書2.2：読み込んだプロジェクトのテンポ・拍子を表示へ反映する（Phase 26）
    transportBar.setTempoAndTimeSignature (project.getTempo(), project.getTimeSignature());
    transportBar.setMasterVolumeDb (project.getMasterVolumeDb()); // 仕様書5.7（Phase 37）

    // 仕様書5.5・5.9：読み込んだプロジェクトの刻みを、**両方の入口へ**配る（Phase 55）
    arrangeView.setSnapGrid (project.getSnapGrid());
    pianoRollView.setSnapGrid (project.getSnapGrid());

    // 仕様書5.11.1：キー（Phase 63）。**コードトラックの有無ごと変わる**ので、
    // プロジェクトが差し替わったら必ず引き直す
    applyProjectKeyToViews();

    // 8.41：**エディタパネルの中身も、いまの選択から引き直す**（Phase 81）。
    // Undoでクリップが戻ってきたときに、掴み直す先はここで決まる
    refreshEditorContent();

    applyLoopSettings();                                          // 仕様書5.9（Phase 48）
    updateMetronomeTiming(); // Phase 38：読み込んだプロジェクトのテンポ・拍子で刻む

    // ArrangeView側が再生位置を0へ戻したときだけ、時間表示も揃える（Phase 30／34）
    if (! keepSelection)
        transportBar.setPlayheadSeconds (0.0);

    updateWindowTitle();
}

void MainComponent::updateWindowTitle()
{
    juce::String title;
    title << (project.getCurrentFile() != juce::File()
                  ? project.getCurrentFile().getFileNameWithoutExtension()
                  : project.getName());

    if (project.hasUnsavedChanges())
        title << " *";

    title << " - " << Branding::productName;

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName (title);

    // Phase 30：全画面表示ではタイトルバーが見えないので、画面の中にも出す
    // （Phase 66でトップバーからメニュー行へ移した。8.27）
    const auto name = project.getCurrentFile() != juce::File()
                          ? project.getCurrentFile().getFileNameWithoutExtension()
                          : project.getName();

    projectNameLabel.setText (project.hasUnsavedChanges() ? name + " *" : name,
                               juce::dontSendNotification);

    // 未保存は設計書2.6にならってオレンジ。タイトルの「*」だけだと見落としやすい
    projectNameLabel.setColour (juce::Label::textColourId,
                                 project.hasUnsavedChanges() ? AppColours::orange
                                                              : AppColours::textPrimary);
}

void MainComponent::timerCallback()
{
    // 仕様書5.4：カウントイン中（Phase 39）。数え終わりはオーディオスレッドが決めるので、
    // ここでは「終わったかどうか」を見て表示を切り替えるだけ。
    if (countingInLastFrame != audioEngine.isCountingIn())
    {
        countingInLastFrame = ! countingInLastFrame;
        transportBar.setCountingIn (countingInLastFrame);
    }

    if (countingInLastFrame)
        return; // まだ数えている。再生位置も録音時間も動いていない

    const double playheadSeconds = audioEngine.getPlayheadSeconds();

    setPlayheadDisplay (playheadSeconds);
    transportBar.setPlayheadSeconds (playheadSeconds); // 仕様書5.9（Phase 30）

    if (audioEngine.isRecording())
    {
        transportBar.setRecordingState (true, audioEngine.getRecordedSeconds());
        return; // 録音中は、再生側が自動停止してもタイマーを止めない
    }

    if (! audioEngine.isPlaying())
    {
        // 全クリップの再生が終わり、自動停止した場合
        transportBar.setPlayingState (false);
        stopTimer();
    }
}
