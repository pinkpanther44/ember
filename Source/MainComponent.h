#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
// Phase 66：トップバー（`TopBarComponent`）は廃止した。ページタブが不要になり、
// 曲名はメニュー行の右へ移したため（8.27）
#include "TransportBarComponent.h"
#include "ArrangeView.h"
#include "ConsoleView.h"
#include "PreferencesDialog.h"
#include "PianoRollView.h"
#include "EditorPanel.h"
#include "ChordPadPanel.h"
#include "AudioEditorView.h"   // 設計書2.3.4：オーディオエディタ（Phase 79）
#include "BrowserPanel.h"
#include "InspectorPanel.h"
#include "ExportOptions.h"        // 8.80：書き出しの設定（Phase 120／D9・D10・D11）
#include "PanelResizerBar.h"
#include "SelectionState.h"
#include "AudioEngine.h"
#include "AutoSaveManager.h"
#include "ClipAudioRenderer.h"   // 8.147：トランスポーズを裏で作る（Phase 185）
#include "ProjectTemplates.h"     // 8.151：プロジェクトテンプレート（Phase 189／D8）
#include "MidiFileExporter.h"

//==============================================================================
/**
    メインウィンドウ（設計書2.2）の組み立て役。

    Phase 4dでは、Play/Stopボタンの配線と、再生中のプレイヘッド位置を
    タイムラインへ反映するためのタイマー処理を追加した。
*/
/*
    注意：ApplicationCommandTargetは必ず**public継承**にすること。
    JUCEは`dynamic_cast<ApplicationCommandTarget*>`でコマンドの実行先を探すが、
    private継承だとこのキャストが失敗し（アクセス不可の基底クラスへは変換できない）、
    メニュー項目もショートカットも「押せるのに何も起きない」状態になる。
    実際にこれでUndoが動かない不具合を出した。
*/
/*
    仕様書4.4・6章：ドラッグ&ドロップ（Phase 21）。
    `DragAndDropContainer`は、**ドラッグの送り手と受け手の共通の祖先**である必要がある
    （JUCEが`findParentDragContainerFor()`で親をたどって探すため）。
    ブラウザパネルもタイムラインもミキサーもここの子孫なので、この階層に置いている。
*/
class MainComponent : public juce::Component,
                       public juce::ApplicationCommandTarget,
                       public juce::DragAndDropContainer,
                       private juce::Timer,
                       private juce::MenuBarModel,
                       private juce::ValueTree::Listener,
                       private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 指定したプロジェクトファイルを開く（プラグインの復元・全ビューの更新まで行う）。
        起動時にコマンドライン引数で渡されたファイルを開くため、Main.cppからも呼ぶ。 */
    void loadProjectFile (const juce::File& file);

    /** 仕様書5.1：未保存の変更があれば確認ダイアログを出す。
        続行してよい場合に onResult(true)、キャンセルされた場合に onResult(false) を呼ぶ。
        アプリ終了時にもMain.cppから使うため、publicに置いている。 */
    void confirmDiscardChanges (std::function<void (bool)> onResult);

    /** 8.151：**テンプレートを適用して作り直す**（Phase 189／D8）。

        起動時（プロジェクト選択画面）から呼ぶので**確認は出しません**——
        まだ何も作っていないため。メニューから呼ぶときは
        `newProjectFromTemplate()`のほうを使うこと（確認が入ります）。 */
    void applyTemplate (const ProjectTemplates::Entry& entry);

    /** 8.151：**起動してから訊くこと**（Phase 189／改善案⑰）。

        前回の異常終了とオートセーブの復元は、Phase 188までコンストラクタで
        訊いていました。**プロジェクト選択画面より先に出てしまう**ので、
        メインウィンドウが見えてから呼ぶ形に移しています。 */
    void beginStartupChecks();

    /** 復元できるオートセーブが残っているか（`Main.cpp`が起動の道筋を決めるのに使う）。 */
    bool hasRecoverableAutoSave() const;

private:
    /** 8.157：**起動したら裏でプラグインを見に行く**（Phase 195／8.1のE2）。

        本人の要望は「毎回このために環境設定を開くのが面倒」。
        **`beginStartupChecks()`から呼びます**——メインウィンドウが出た後なので、
        待たされている感じになりません。

        **覚えてある一覧があって初めて現実的になります**（`PluginManager.h`）。
        Phase 194までは毎回124個を開き直していたので、起動のたびに1分でした。

        > **落ちるプラグインがあると、起動の途中で落ちます。**
        > ただし`deadMansPedal`が記録するので、**次の起動では飛ばします**（8.157）。 */
    void startBackgroundPluginScan();

    /** スキャン用のスレッド。**1本だけ**（同時に走らせても速くならず、
        落ちたときにどれが原因か分からなくなる）。 */
    juce::ThreadPool pluginScanPool { 1 };

public:

private:
    // Phase 66でページ（Arrange／Console）の概念は無くなった。
    // Consoleは下部パネルの中身の1つ（`EditorContent::console`）になっている（8.27）

    //==========================================================================
    // 設計書2.2・仕様書4.2：画面下部のエディタパネル（Phase 16）

    /** エディタパネルを開く／閉じる。開くとピアノロールが画面下部に現れる。 */
    void setEditorPanelOpen (bool shouldBeOpen);

    /** パネルの高さを変える（上端のドラッグから呼ばれる）。
        ウィンドウからはみ出さない範囲に収める。 */
    void setEditorPanelHeight (int newHeight);

    /** 仕様書4.2：エディタを別ウィンドウへ出す（マルチモニター対応）。 */
    void popOutEditor();

    /** 別ウィンドウのエディタを、メインウィンドウ下部へ戻す（設計書2.5）。 */
    void dockEditor();

    /** ポップアウトウィンドウの位置・サイズをアプリ設定へ控える（設計書2.5）。 */
    void saveEditorWindowBounds() const;

    /** 設計書2.3.5：下部パネルの中身（Phase 43／Phase 66でConsoleを足した。8.27）。

        **同時に出せるのは1つだけ。** Editorのポップアウトの仕組み（中身の所有権を
        移さず親だけ付け替える）をそのまま使うため、器は1つに保っている。 */
    enum class EditorContent { pianoRoll, audioEditor, chordPad, console };

    /** 設計書2.3.5：エディタパネルの中身を切り替える（ピアノロール⇔コードパッド）。

        **どちらを出しているかを1箇所で持つ。** パネル・ポップアウトウィンドウ・
        タイトル・「開いたときの読み直し」の4箇所が同じ答えを見る必要があり、
        `setContent()`を呼ぶ場所ごとに判断すると必ずずれる（HANDOVER 1.26と同じ話）。 */
    void setEditorContent (EditorContent newContent);

    /** フッターのボタンから、その中身へ切り替える／閉じる（Phase 66／8.27）。

        **同じ中身が出ているときに押したら閉じる。** Editor と Console は
        同じ枠を取り合うので、「押すたびに開く」だけだと閉じる手が無くなる。 */
    void toggleEditorContent (EditorContent content);

    /** フッターのEditor／Consoleボタンの見た目を、いまの中身に合わせる（Phase 66）。
        **開閉と中身の両方で変わる**ので、どちらを触ったところからも呼ぶこと。 */
    void updateEditorToggleButtons();

    /** Editorボタンで戻る先（Phase 66）。**ピアノロールかオーディオエディタのどちらか**で、
        Console と Chord Pad は入りません（自分のボタンを持っているため。8.75）。 */
    EditorContent lastEditorContent = EditorContent::pianoRoll;

    /** 8.75：`lastEditorContent`へ覚える（Phase 115）。**判断はここ1箇所**（1.26）。 */
    void rememberEditorContent (EditorContent content);

    //==========================================================================
    // 8.74：**コードパッドも下部パネルの中身**（Phase 114／改善案⑭の作り直し）。
    //
    // Phase 111では**独立した窓**にしていました（ピアノロールと同時に見たかったため）。
    // 使ってみて、**他のパネルと開け方が違う**のが分かりにくいという判断になり、
    // Editor・Consoleと同じ「フッターのボタンで下部パネルへ」に戻しています。
    //
    // **別ウィンドウにしたいときはパネルの「Pop Out」**——
    // Editorのポップアウトの仕組み（`popOutEditor()`）にそのまま乗るので、
    // コードパッド専用の窓は持ちません（同じ話を2箇所に書かない。8.2）。

    /** いま出している中身のコンポーネント。 */
    juce::Component* getEditorContentComponent();

    /** いま出している中身の名前（パネルのヘッダーとポップアウトウィンドウの題）。 */
    juce::String getEditorContentTitle() const;

    /** 中身を「今の状態」に合わせて読み直す（開くとき・プロジェクトが変わったとき）。 */
    void refreshEditorContent();

    /** 8.39：いま出すべきオーディオクリップ（Phase 79）。

        **クリップを選んでいればそれ**、トラックだけ選んでいればそのトラックの
        最初のクリップ。どちらでもなければ無効なものを返す（「まだ何も無い」表示）。 */
    AudioClip getSelectedAudioClip() const;

    /** 8.39：選んでいるトラックの種別に合った中身へ切り替える（Phase 79／D1）。

        **Consoleを出しているときは取り上げない。** 同じ枠を取り合うので（8.27）、
        トラックを選ぶたびにミキサーが消えると使い物にならない。
        次にEditorボタンを押したときに正しい中身が出るよう、行き先だけ覚えておく。 */
    void updateEditorContentForSelectedTrack();

    /** ズームのショートカットをピアノロールへ渡すか（Phase 67／8.1のG3）。

        **倍率はアレンジ画面とピアノロールで別の値**なので、ツールやスナップのように
        「配る」ことができない。ピアノロールが出ていて、そこにキーボードフォーカスが
        あるときだけそちらへ渡す（ポップアウトしていても同じ判定で通る）。 */
    bool isPianoRollFocused() const;

    //==========================================================================
    // 設計書2.2：左右のサイドパネル（Phase 17）

    void setBrowserPanelOpen (bool shouldBeOpen);
    void setInspectorPanelOpen (bool shouldBeOpen);
    void setBrowserPanelWidth (int newWidth);
    void setInspectorPanelWidth (int newWidth);

    /** ブラウザからプラグインが選ばれたときの挿入先を決める（仕様書4.4）。
        選択中のトラックへ挿す。 */
    void insertPluginFromBrowser (const juce::PluginDescription& description);

    /** 指定トラックへプラグインを挿す（仕様書4.4）。音源かエフェクトかで挿し先を振り分け、
        挿した後はそのままGUIを開く。ダブルクリックからもドラッグ&ドロップからも通る。 */
    void insertPluginIntoTrack (const juce::String& trackId, const juce::PluginDescription& description);

    /** プラグインを挿せなかったことを伝える（Phase 149で切り出し）。
        **同じ文面を4箇所に書いていたのを1つに**まとめたもの（8.2）。 */
    void showPluginInsertError (const juce::String& error);

    void playButtonClicked();
    void recordButtonClicked();

    /** 8.146：**録音の後始末**（Phase 184／改善案⑬a）。

        録音を閉じ、録れたもの（WAVのクリップ／MIDIのノート）をモデルへ入れる。
        **トランスポートを止める前に呼ぶこと**——押しっぱなしの鍵を切る位置に
        録音の終わりの時刻が要ります。 */
    void finishRecording();
    void timerCallback() override;

    //==========================================================================
    // 仕様書6.2：ショートカットから呼ばれる操作（Phase 47）。
    // **どれも既存の入口を呼ぶだけ**で、ここに独自の処理は書かないこと
    // （ボタンから実行したときと挙動がずれる）。

    /** 選択中のトラックの入切を切り替える対象。 */
    enum class TrackFlag { armed, solo, mute };

    void toggleSelectedTrackFlag (TrackFlag flag);
    void toggleMetronome();
    void toggleCountIn();
    void toggleInputMonitoring();

    //==========================================================================
    // 仕様書5.9：ループ再生（Phase 48）

    /** 再生位置を動かして、表示も揃える。**停止中はタイマーが回っていない**ので、
        シークするところは必ずここを通すこと（1.28）。 */
    void seekTo (double seconds);

    /** 仕様書5.9：再生位置を各画面へ配る（Phase 72）。

        **呼び出し側に2行書かせないこと。** アレンジ画面とピアノロールの両方が
        自分の再生カーソルを持っているので、足し忘れると片方だけ止まる（8.33）。 */
    void setPlayheadDisplay (double seconds);

    /** モデルのループ設定をオーディオエンジンとUIへ反映する。

        **モデルが正で、エンジンはその写し。** ループを変えたところは必ずここを通すこと。
        通さないと「画面ではループしているのに音は繰り返さない」状態になる。 */
    void applyLoopSettings();

    /** 仕様書5.5・5.9：編集の刻みを決めて、**両方の入口へ配る**（Phase 55）。

        アレンジ画面とピアノロールに同じコンボボックスが出ているため、
        片方で選んだらもう片方も追従させる必要がある（1.27）。
        ツールの配り方（`setEditTool`）と同じ形。 */
    void applySnapGrid (SnapGrid newGrid);

    /** 仕様書5.11.1：プロジェクトのキーを、**両方の入口の表示へ配る**（Phase 63／8.1のC9）。

        コードパッドの上段とトランスポートバーに同じ値が出ているため、
        片方で変えたらもう片方も追従させる必要がある（1.27）。
        コードトラックの有無でトランスポートバー側の押せる／押せないも決まる。 */
    void applyProjectKeyToViews();

    /** 選択中のクリップの範囲をループにする（仕様書5.9、Studio OneのShift+P相当）。 */
    void loopToSelectedClip();

    /** 仕様書5.9：指定した時刻にマーカーを置く（Phase 49）。

        `askForName`がtrueなら名前の入力欄を出す（Studio OneのShift+Y相当）。
        **入力欄は非同期で出すこと**：モーダルループを回すと、
        再生位置の更新タイマーごと止まる。

        **入口は2つある**（Phase 50）：ショートカット（再生位置へ置く）と、
        ルーラーの右クリックメニュー（クリックした位置へ置く）。
        どちらもここを通るので、名前の付け方や重なりの扱いが揃う。 */
    void insertMarkerAt (double timeSeconds, bool askForName);

    //==========================================================================
    // 仕様書5.5：クリップの分割・結合・複製（Phase 50）

    /** 選択中のクリップのValueTree。`trackOut`にはその持ち主が入る。

        **オーディオとMIDIで通し番号の数え方が違う**ので、引くところを1箇所にまとめてある。 */
    juce::ValueTree getSelectedClipState (Track& trackOut) const;

    void splitSelectedClip();
    void mergeSelectedClip();
    void duplicateSelectedClip();

    /** 仕様書6.2：ツールを両方の画面へ配る（Phase 52）。

        **ツールの「いまの値」を持っているのはここ。** アレンジ画面と
        ピアノロールがそれぞれ別に覚えると、切り替えたつもりで片方だけ変わる。 */
    void setEditTool (EditTool tool);

    /** 設計書2.3.5：選択がコードトラックへ移ったら、エディタパネルをコードパッドに
        切り替える（Phase 43）。選択を購読しているのはこのためだけ。 */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    //==========================================================================
    // 仕様書5.1：プロジェクト管理（メニューバーのFileメニュー）
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    //==========================================================================
    // 仕様書7章：Undo/Redo。キーボードショートカットを効かせるため、
    // ApplicationCommandManager経由でコマンドとして登録する
    // （仕様書6.2「ショートカットのカスタマイズ」も、この仕組みの上に載せられる）。
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform (const InvocationInfo& info) override;

    void undo();
    void redo();

    void newProject();
    void openProject();
    void showOpenProjectChooser();

    /** 8.151：ファイルメニューの「テンプレートから新規...」（Phase 189／D8）。
        未保存の確認を出してから、テンプレートの一覧を出す。 */
    void newProjectFromTemplate();

    /** 8.151：ファイルメニューの「テンプレートとして保存...」（Phase 189／D8）。 */
    void saveCurrentAsTemplate();

    /** 保存する。完了後（またはキャンセル時）に onComplete(保存できたか) を呼ぶ。 */
    void saveProject (std::function<void (bool)> onComplete = nullptr);
    void saveProjectAs (std::function<void (bool)> onComplete = nullptr);

    /** 仕様書5.10：マスター出力をWAVファイルへ書き出す。 */
    void exportMixdown();

    /** 仕様書5.10：トラックごとに1ファイルずつ書き出す（ステム）。 */
    void exportStems();

    /** 8.80：いま引かれているループの長さ（秒。Phase 120/D11）。0以下なら引かれていない。
        書き出しのダイアログに「ループ範囲だけ」を出すかの判断に使う。 */
    double getLoopRangeSecondsForExport() const;

    /** 8.81：ステム書き出しのトラック一覧を、いまのプロジェクトから組み立てる（Phase 121/D10a）。

        **前回の選択は名前ではなくIDで引き継ぎます**（名前は変えられるため。1.32）。
        一覧に無いトラック（新しく足したもの）は「書き出す・ステレオ」で始まります。 */
    std::vector<ExportStemTrack> buildStemTrackList() const;

    /** 8.81：書き出しの設定。**このセッションのあいだだけ**覚えます（Phase 121）。

        トラックごとの選択はトラックIDに紐づくので、アプリの設定（`AppSettings`）へ
        置いても別のプロジェクトでは意味を持ちません。かといって毎回まっさらだと、
        **同じステムを何度か書き出すときに毎回選び直す**ことになります。
        プロジェクトファイルへ入れると「書き出しただけで未保存になる」ので、
        **メモリに置く**という中を取っています。 */
    ExportOptions lastExportOptions;

    /** 仕様書5.10：MIDIノートを標準MIDIファイルへ書き出す。 */
    void exportMidiFile();

    /** 再生・録音を止めて、トランスポート表示を初期状態へ戻す。 */
    void stopTransport();

    /** 仕様書5.1：前回のオートセーブが残っていれば、復元するか確認する。 */
    void offerAutoSaveRecovery();

    /** 保存/読み込み後に、全ビューを現在のProjectModelの内容へ追従させる。 */
    /** 仕様書5.7：マスター音量が他の画面（Consoleのフェーダー、オートメーション、Undo）で
        変わったときに、下端のスライダーへ反映する（Phase 37）。

        **マスターボリュームは2箇所に出ている**ので、購読していないと片方が古いまま残る
        （HANDOVER 1.27と同じ形の話）。 */
    void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property) override;

    /** 仕様書5.11.1：トラックが増減すると「コードトラックがあるか」が変わる（Phase 63）。

        キーの選択欄を押せるかどうかがそれで決まるので、追加・削除の両方で見る。
        **`child.hasType()`で絞ること**（ルートを購読しているので、ノート1音の追加でも飛ぶ。1.42）。 */
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;

    /** 購読先を今のプロジェクトのルートへ合わせる（読み込みでルートが差し替わるため）。 */
    void updateProjectSubscription();

    /** メトロノームへ、今のプロジェクトのテンポと拍子を伝える（Phase 38）。
        テンポ・拍子が変わったときと、プロジェクトを読み込んだときに呼ぶ。 */
    void updateMetronomeTiming();

    /** 「拍」ボタンの右クリックメニュー（Phase 39）。
        クリック音量とカウントインの小節数を選ぶ。 */
    void showMetronomeSettingsMenu (juce::Rectangle<int> screenBounds);

    /** 仕様書5.4：カウントインの小節数（0＝無し）。アプリ設定から読む。 */
    int getCountInBars() const;

    /** 仕様書5.4：カウントインで録るときに、録音開始位置を小節の頭へ寄せる（Phase 40）。

        カウントインのクリックは**押した位置から**数え始めるので、そこが小節の頭で
        ないと、クリックの拍とタイムラインの小節線がずれたまま録ることになる。
        いちばん近い小節線へ動かしてから数え始めれば、両者が揃う。

        カウントインを使わないときは動かさない（位置を狙って押しているはずのため）。 */
    void snapPlayheadToBarForCountIn();

    /** 保存/読み込み後に、全ビューを現在のProjectModelの内容へ追従させる。

        `keepSelection`がfalse（既定）だと選択も捨てる。**プロジェクトが差し替わる場合**は
        覚えていたtrackIdが別プロジェクトのものになるため、必ず捨てる必要がある。

        Undo/Redoでは**プロジェクトは同じ**なので、trackIdは有効なまま。
        ここで捨てると、並べ替えをUndoするたびに選択が外れて選び直しになる（Phase 34）。
        消えたトラックを選んでいた場合は、TimelineComponentが購読側で選択を解除する（1.32）。 */
    void refreshAllViews (bool keepSelection = false);
    void updateWindowTitle();

    ProjectModel project;
    AudioEngine audioEngine { project };
    AutoSaveManager autoSave { project };

    /** 8.147：トランスポーズ済みのファイルを裏で作る係（Phase 185／改善案㉞）。

        **`audioEngine`より後に置くこと。** 出来上がった通知でエンジンへ
        読み直させるので、こちらが先に消えるようにします（逆だと、
        まだ生きているスレッドが消えたエンジンを触り得ます）。 */
    ClipAudioRenderer clipAudioRenderer { project };

    // 設計書2.3.7：インスペクタが「いま何を選んでいるか」を知るための共有状態（Phase 17）
    SelectionState selection;

    // 購読中のルート。差し替え時に古い方の購読を外すために持つ（HANDOVER 1.15）
    juce::ValueTree subscribedProjectState;

    // 仕様書5.4：カウントイン中かどうかの直前の値（Phase 39）。
    // 変化したときだけ表示を切り替えるために持つ（毎フレーム書き直さない）。
    bool countingInLastFrame = false;

    juce::ApplicationCommandManager commandManager;
    juce::MenuBarComponent menuBar { this };

    /** メニューと曲名の行の高さ（Phase 66／8.27）。

        **`paint()`と`resized()`の両方が使う**ので、片方に数値を書かないこと
        （ずれると、塗った帯とメニューの位置が食い違う）。 */
    static constexpr int menuRowHeight = 26;

    /** 開いているプロジェクト名（Phase 66でトップバーから移した。8.27）。

        **メニュー行の右側に並べている。** ページタブが無くなってトップバーの中身が
        曲名だけになり、40pxの帯を1段まるごと使うのが割に合わなくなったため。
        全画面ではタイトルバーが見えないので、画面の中にも出しておく必要がある。 */
    juce::Label projectNameLabel;

    TransportBarComponent transportBar;

    // 設計書2.2：左右のサイドパネルと、そのリサイズ用の帯（Phase 17）
    BrowserPanel browserPanel { audioEngine };
    InspectorPanel inspectorPanel { project, selection, audioEngine };
    // Phase 28で左右を入れ替えた。**帯の`Edge`はパネルがどちら側にあるかで決まる**ので、
    // 配置を入れ替えるときはここも必ず一緒に直すこと（直さないとドラッグの向きが逆になる）。
    PanelResizerBar inspectorResizer { PanelResizerBar::Edge::Right }; // 左パネルの右端
    PanelResizerBar browserResizer   { PanelResizerBar::Edge::Left };  // 右パネルの左端

    int browserPanelWidth = BrowserPanel::defaultWidth;
    int inspectorPanelWidth = InspectorPanel::defaultWidth;

    ArrangeView arrangeView { project, audioEngine, selection };
    ConsoleView consoleView { project, audioEngine };

    // 設計書2.2：ピアノロールはエディタパネルの「中身」として置く（Phase 16）。
    // 実体はここが持ち続け、パネル／ポップアウトウィンドウは親として並べるだけ。
    PianoRollView pianoRollView { project, audioEngine };

    // 設計書2.3.5：コードパッドはピアノロールと**排他**でエディタパネルに出る（Phase 43）。
    // 実体をここが持つのはピアノロールと同じ。切り替えはsetEditorContent()。
    ChordPadPanel chordPadPanel { project, selection, audioEngine };

    // 設計書2.3.4：オーディオエディタ（Phase 79/8.39）。これも同じ枠の中身の1つ。
    // **波形のキャッシュはアレンジ画面と共有する**ので、arrangeViewより後に宣言すること
    AudioEditorView audioEditorView { project, arrangeView.getWaveformCache() };

    EditorPanel editorPanel;

    EditorContent editorContent = EditorContent::pianoRoll;

    // パネルの高さ（ピクセル）。ドラッグで変えられ、アプリ設定へ保存される。
    // 既定値はピアノロールの操作行（トラック選択・クオンタイズ等で約140px）を引いても
    // ノートグリッドが十分見える高さにしてある。
    int editorPanelHeight = 380;

    // 仕様書4.2：ポップアウト中のみ生きるウィンドウ。ドッキング中はnullptr。
    // ピアノロール本体は所有せず（setContentNonOwned）、あくまで枠として使う。
    std::unique_ptr<juce::DocumentWindow> editorWindow;

    juce::Label audioErrorLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // アプリ全体のツールチップ表示（Phase 12e）。
    // **これを1つ置かないと、`setTooltip()`を書いても何も出ない**（JUCEは
    // 表示役のTooltipWindowを親から探すため）。狭い場所に補足を出したいときに使う。
    // 例：ミキサーのレイテンシ表示（仕様書5.7.1）で、サンプル数をここへ回している。
    juce::TooltipWindow tooltipWindow { this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
