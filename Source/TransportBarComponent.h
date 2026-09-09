#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ValueEntrySlider.h"   // 8.118：マスター音量もConsoleと同じつまみへ
#include "TransportIcons.h"    // 8.132：再生・停止・録音を記号で（Phase 168）
#include "IconAssets.h"        // 8.133：本人が用意した絵（Phase 169）
#include "SegmentDisplay.h"    // 8.177：7セグメントの数字表示（Phase 218）

//==============================================================================
/**
    画面**下端**のトランスポートバー（Phase 30）。

    Phase 29までは、再生・録音・テンポ・拍子・パネル開閉のすべてが
    トップバーに同居していた（`TopBarComponent`。Phase 66で廃止）。機能が増えるにつれて
    「ファイルやページの話」と「演奏の話」が1本の帯に混ざり、押し間違えやすくなっていた。

    Phase 30で役割を上下に分けた：

    - **上**（トップバー）… どの画面を見るか・どのプロジェクトを開いているか
      → **Phase 66で廃止。** ページの概念が無くなり、曲名はメニュー行へ移した（8.27）
    - **下**（このクラス）… 演奏（時間・録音・再生）と、その基準となるテンポ／拍子、
      それに**どのパネルを出すか**の切り替え

    パネル開閉のボタンを下の中央に置いているのは、
    左（インスペクタ）・下（エディタ）・右（ブラウザ）のどれもが
    **中央の作業領域を囲むもの**で、ページの切り替えとは性質が違うため。

    このクラスは状態を持たない。再生中か・録音中かの真実は`AudioEngine`側にあり、
    ここは`setXxxState()`で見た目を合わせるだけ（設計書1.2）。
*/
class TransportBarComponent : public juce::Component
{
public:
    TransportBarComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** バーの高さ。MainComponentのレイアウト計算に使う。

        **Phase 66で2段になった**（8.27）。上段＝パネルの開閉、下段＝演奏と曲の前提。
        1段に詰めていたころは、パネルの開閉ボタンが中央に、演奏ボタンがその左に並び、
        **録音時間の有無で中央の位置がずれる**という無理があった。 */
    static constexpr int barHeight = 84;

    /** 上段（パネルの開閉ボタン）の高さ。 */
    static constexpr int toggleRowHeight = 26;

    //==========================================================================
    // 仕様書5.7：マスター音量（Phase 37）。
    //
    // Consoleのマスターストリップにもある同じ値。**どちらを動かしても同じ**で、
    // 片方を動かせばもう片方も追う（値の実体はProjectModelにあるため）。
    // ここに置いたのは、ミックスの最終段だけは画面を切り替えずに触れてほしいから。
    //
    // このクラスはモデルを知らないので、値の出し入れはコールバックと`setMasterVolumeDb()`で行う。

    std::function<void (float)> onMasterVolumeChanged;

    /** つまみを掴んだ／離したときに呼ばれる（Undoの区切りと、
        仕様書5.6のTouch/Latchの記録開始判定に使う）。 */
    std::function<void()> onMasterVolumeDragStart;
    std::function<void()> onMasterVolumeDragEnd;

    /** モデルの値を表示へ反映する。**他の画面で動かされたときにも呼ぶこと。** */
    void setMasterVolumeDb (float volumeDb);

    //==========================================================================
    // 演奏

    /** Play/Stopボタンが押されたときに呼ばれる。実際の再生/停止判断は呼び出し元で行う。 */
    std::function<void()> onPlayButtonClicked;

    /** 仕様書5.4：Recordボタンが押されたときに呼ばれる。開始/停止の判断は呼び出し元で行う。 */
    std::function<void()> onRecordButtonClicked;

    /** 仕様書5.9：先頭（0秒）へ戻すボタンが押されたときに呼ばれる（Phase 30で追加）。
        Phase 29まではルーラーの左端をクリックするしか手が無かった。 */
    std::function<void()> onGoToStartClicked;

    /** Play/Stopボタンの見た目だけを更新する（実際の再生状態はAudioEngine側が真実）。 */
    void setPlayingState (bool isPlaying);

    /** 録音中の表示を更新する。録音時間（秒）はボタン脇のラベルに出す。 */
    void setRecordingState (bool isRecording, double recordedSeconds = 0.0);

    /** 仕様書5.9：現在の再生位置を時間表示へ反映する（Phase 30）。
        再生中はタイマーから、停止中はシークのたびに呼ばれる。 */
    void setPlayheadSeconds (double seconds);

    //==========================================================================
    // 設計書2.2：テンポ・拍子（Phase 26でトップバーへ、Phase 30でここへ移した）
    //
    // どちらも「再生の基準」なので、トランスポートと同じ帯に置くほうが読み取りやすい。

    /** テンポ（BPM）が編集されたときに呼ばれる。 */
    std::function<void (double)> onTempoChanged;

    /** 拍子が編集されたときに呼ばれる（"4/4"のような文字列）。 */
    std::function<void (juce::String)> onTimeSignatureChanged;

    /** 表示を現在のプロジェクトの値へ合わせる（読み込み後や外部からの変更時に呼ぶ）。 */
    void setTempoAndTimeSignature (double tempo, const juce::String& timeSignature);

    //==========================================================================
    // 仕様書5.11.1：プロジェクトのキー（Phase 63／8.1のC9）
    //
    // **コードパッドの上段と同じ値**です。値の実体はコードトラックが持っているので、
    // どちらで変えても揃います（`ProjectModel::setProjectKey()`）。
    // ここに置いたのは、キーが**テンポ・拍子と同じ「曲全体の前提」**だから。

    /** キーが選び直されたときに呼ばれる（root＝0〜11、minor＝マイナーかどうか）。 */
    std::function<void (int root, bool minor)> onProjectKeyChanged;

    /** 表示をモデルの値へ合わせる。**コードパッドで変えられたときにも呼ぶこと。**

        `hasChordTrack`がfalseなら押せなくする（キーの置き場所が無いため）。 */
    void setProjectKey (int root, bool minor, bool hasChordTrack);

    // 仕様書5.5・5.9の編集の刻み（スナップ）は、**この帯には無い**。
    // Phase 54でここへ置いたが、Phase 55で編集画面側（`SnapGridSelector`）へ移した。
    // 理由は8.15：刻みを変えるのは編集している最中で、ツールの隣にあるほうが手が届く。
    // エディタをポップアウトすると、この帯は別ウィンドウの外に出てしまうのもある。

    //==========================================================================
    // メトロノーム（Phase 38）。テンポの隣に置く（拍を刻むもの同士のまとまり）

    std::function<void (bool)> onMetronomeToggled;

    void setMetronomeEnabled (bool isEnabled);

    /** 「拍」ボタンの右クリックで出す設定メニュー（Phase 39）。
        クリック音量とカウントインの小節数を選ぶ。中身は呼び出し側が作る
        （設定の保存先を知っているのはMainComponent側のため）。

        `screenBounds`はボタンの画面座標（メニューをその隣に出すため）。 */
    std::function<void (juce::Rectangle<int> screenBounds)> onMetronomeSettingsRequested;

    /** カウントイン中の表示（Phase 39）。残り小節ではなく「数えている最中」だけを示す。 */
    void setCountingIn (bool isCountingIn);

    /** 仕様書5.9：ループの入切ボタンが押されたとき（Phase 48）。 */
    std::function<void()> onLoopToggled;

    /** ボタンの見た目をモデルに合わせる（ショートカットから変えたときにも呼ぶ）。 */
    void setLoopEnabled (bool isEnabled);

    //==========================================================================
    // 設計書2.2：パネルの開閉（Phase 17／Phase 30でここへ移した／Phase 66で上段へ）
    //
    // **Consoleもここに並びます**（Phase 66／8.27）。それまではトップバーの
    // ページタブでしたが、下部パネルの中身の1つになったので、
    // Editor・Inspector・Browserと同じ「パネルの開閉」に揃えました。

    std::function<void()> onInspectorToggled;
    std::function<void()> onEditorToggled;
    std::function<void()> onConsoleToggled;

    /** 8.74：コードパッドを下部パネルへ出したい（Phase 114/改善案⑭）。 */
    std::function<void()> onChordPadToggled;

    std::function<void()> onBrowserToggled;

    void setInspectorPanelOpen (bool isOpen);
    void setEditorPanelOpen (bool isOpen);
    void setConsolePanelOpen (bool isOpen);

    /** 8.74：コードパッドが下部パネルに出ているか（ボタンの見た目に使う。Phase 114）。 */
    void setChordPadPanelOpen (bool isOpen);

    void setBrowserPanelOpen (bool isOpen);

private:
    // 仕様書5.7：マスター音量（Phase 37）。参考画像にならって左端へ置く。
    //
    // 8.118：**`ValueEntrySlider`に差し替えた**（Phase 153／改善案24）。
    // ここだけ`juce::Slider`のままで、**初期の見た目（丸いつまみ）が残っていた**。
    // 部品を替えるだけで、見た目もダブルクリック／右クリックの割り当ても
    // Console・インスペクタと揃う（1.27：入口ごとに設定すると必ずどれかを忘れる）。
    juce::Label masterVolumeCaption;
    ValueEntrySlider masterVolumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    // 仕様書5.9：再生位置。大きめの等幅寄りフォントで、離れていても読めるようにする
    /** 8.177：**7セグメント表示**（Phase 218）。Emberでは数字を棒で描き、
        Manta Studioでは今までどおり文字で描きます（`SegmentDisplay.h`） */
    SegmentDisplay timeLabel;

    /** 8.132：**記号で出すボタン**（Phase 168／実験枠の44）。

        文字（"Rec" / "|<" / "Play"）は**設定したまま残してあります**。
        `AppColours::useTransportIcons`を`false`にすれば、そのまま文字へ戻ります。 */
    TransportIcons::IconButton recordButton { "Rec" };
    juce::Label recordTimeLabel;
    TransportIcons::IconButton goToStartButton { "|<" };
    TransportIcons::IconButton playButton { "Play" };

    // 8.118：**上段は「左端＝Inspector／右端＝その他」に分けた**（Phase 153／改善案31）。
    //
    // Phase 66からPhase 152までは5つまとめて中央に置いていたが、
    // **Inspectorだけは開く場所が画面の左**で、他の4つは下と右。
    // ボタンの位置と、開いたパネルが出てくる位置を合わせてある。
    // 右端の並び（Editor→Chord Pad→Console→Browser）は改善案31の指定どおり。
    juce::TextButton inspectorButton { "Inspector" };
    juce::TextButton editorButton { "Editor" };
    juce::TextButton consoleButton { "Console" };

    /** 8.74：コードパッドも**下部パネルの中身**（Phase 114/改善案⑭）。
        Editor・Consoleと同じ枠を取り合う。**別ウィンドウにしたいときは
        パネルの「Pop Out」**（そちらの仕組みに乗るので、ここに窓の話は無い）。 */
    juce::TextButton chordPadButton { "Chord Pad" };

    juce::TextButton browserButton { "Browser" };

    juce::Label tempoCaption;

    /** 8.177：**数字は棒で、編集はそのまま**（Phase 219／`SegmentDisplay.h`） */
    SegmentLabel tempoLabel;
    SegmentLabel timeSignatureLabel;

    /** 仕様書5.11.1：プロジェクトのキー（Phase 63）。BPMの左に置く。
        項目のIDは**ルート音＋1**（ComboBoxはID=0を「選択なし」に使うため）。 */
    juce::Label keyCaption;
    juce::ComboBox keyRootBox;
    juce::ComboBox keyModeBox;

    /** 右クリックで設定メニューを出せるボタン（Phase 39）。

        **`juce::TextButton`は右クリックでもクリック扱いになる。** そのまま使うと、
        メニューを出すのと同時にメトロノームのON/OFFまで切り替わってしまう。
        `mouseDown()`を基底へ渡さなければ、基底は「押されていない」と見なすので
        `mouseUp()`でのクリック通知も起きない。 */
    struct RightClickableButton : public IconAssets::SvgButton
    {
        std::function<void()> onRightClick;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick != nullptr)
                    onRightClick();

                return;
            }

            IconAssets::SvgButton::mouseDown (e);
        }
    };

    // メトロノーム（Phase 38）。押している間だけパープルになる
    RightClickableButton metronomeButton;

    /** 仕様書5.9：ループ再生の入切（Phase 48）。 */
    IconAssets::SvgButton loopButton;   // 8.133（Phase 169）

    // 表示済みの時間（1/10秒単位）。同じ値での描き直しを避けるために持つ
    int lastShownTenths = -1;

    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBarComponent)
};
