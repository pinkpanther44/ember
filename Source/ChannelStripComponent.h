#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "LevelMeterComponent.h"
#include "ValueEntrySlider.h"   // Phase 61：ダブルクリックでの数値入力（8.1のC2）
#include "TrackRackComponent.h"
#include "ConsoleLayout.h"   // 8.283：ラックの高さは全ストリップ共通（Phase 276）
#include "TrackTypeIcons.h"   // 8.295：種類の絵（アレンジのヘッダーと共用。Phase 288）

class AudioEngine;

//==============================================================================
/**
    仕様書5.7・設計書2.3.2「コンソール（ミキサー）ビュー」のチャンネルストリップ1本ぶん。

    表示・操作するのはトラックの音量／パン／ミュート／ソロで、値の実体は
    ProjectModel（ValueTree）側にある。このコンポーネント自身は状態を持たない
    （設計書1.2の「UI側に状態を持たせない」原則）。

    Phase 12bでレベルメーター、Phase 12c-2でインサートスロット、
    Phase 13でセンド（仕様書5.2.2）、Phase 14で音源スロット（仕様書5.3）を追加した。

    **Phase 29で、音源・インサート・センド・VCA・書き込みモードを
    `TrackRackComponent`へ切り出した。** インスペクタパネルからも同じ操作を
    できるようにするためで、このストリップは「フェーダーまわり＋ラックを置く場所」になった。
    ラックの中身を触りたいときは`TrackRackComponent`を見ること。
*/
class ChannelStripComponent : public juce::Component,
                               public juce::DragAndDropTarget,
                               private juce::ValueTree::Listener
{
public:
    /** 8.296：**置かれる場所**（Phase 289／本人の指定）。

        | | Console | インスペクタ |
        |---|---|---|
        | 形 | **縦一列**（幅104px） | **2列**（幅220px前後） |
        | ラック | フェーダーの**上**、枠の中でスクロール | **属性は全幅・インサートとセンドは右列** |
        | 境目のドラッグ | あり（全ストリップ共通の高さ） | **なし**（インスペクタ自身がスクロールする） |
        | 地と枠 | 板を敷く | **敷かない**（インスペクタの地に直に置く） |
        | 並べ替え | 地を掴んでトラックを並べ替え | **なし**（並びを持っているのはConsole） |

        **中身と操作はまったく同じ**です——つまみも、数値の打ち込みも、
        Touch/Latchの記録も、種類の絵も（8.61と同じ考え方）。
        違うのは**置き方だけ**なので、片方だけ古くなることがありません。 */
    enum class Layout
    {
        Console,
        Inspector
    };

    ChannelStripComponent (const Track& trackToControl, ProjectModel& projectToUse,
                            AudioEngine& audioEngineToUse, Layout layoutToUse = Layout::Console);
    ~ChannelStripComponent() override;

    /** 8.296：インスペクタに置くときの、必要な高さ（Phase 289）。

        **Consoleでは使いません**（あちらは与えられた高さに合わせて中を配る）。
        インスペクタは縦にスクロールするので、**こちらが高さを申告します**。 */
    int getPreferredHeight (int width);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 音量・パン・ミュート／ソロが操作されたときに呼ばれる（エンジンへの反映用）。 */
    std::function<void()> onMixerValueChanged;

    /** 8.301：**このストリップが押された**（Phase 294／本人の要望）。

        受けるのは`ConsoleView`——**選択を持っているのはあちら**（`SelectionState`）です。
        ここで直に書くと、Consoleだけが知っている選択ができてしまいます（8.12）。

        **押されるのは「地」と「トラック名」だけ**です。フェーダー・つまみ・ボタン・
        ラックはそれぞれ自分でマウスを受けるので、ここへは落ちてきません
        ——**触ろうとしただけで選択が飛ぶ**ことはありません。 */
    std::function<void()> onSelected;

    /** 8.301：**選ばれているか**（Phase 294）。見た目だけを変えます。

        **自分では決めません。** 誰が選ばれているかを知っているのは`SelectionState`で、
        配るのは`ConsoleView`です——ここで覚えると、
        **アレンジ画面で選び直したときに、両方光ったまま**になります。 */
    void setSelected (bool shouldBeSelected);

    /** 8.283：境目をドラッグしたときに呼ばれる（Phase 276／本人の要望）。

        **受けるのは`ConsoleView`**です——覚えるのと、**全ストリップへ配る**のは
        あちらの仕事。ここで自分の高さだけ変えると、**掴んだ1本だけが変わります**。 */
    std::function<void (int)> onFaderAreaHeightDragged;

    /** 8.296：**必要な高さが変わった**（Phase 289／インスペクタのみ）。

        インサートやセンドが増えると、この部品の高さが変わります。
        置いた側（`InspectorPanel`）は並べ直すこと——
        Consoleは枠の中でスクロールするので、こちらは呼ばれません。 */
    std::function<void()> onPreferredHeightChanged;

    /** 仕様書5.7：メーターに表示するレベルを外から与える（ConsoleViewがタイマーで更新する）。 */
    void setLevels (float leftLevel, float rightLevel) { meter.setLevels (leftLevel, rightLevel); }

    /** このストリップが担当しているトラックのID（メーターの値をエンジンから引くのに使う）。 */
    juce::String getTrackId() const { return track.getId(); }

    /** 8.54：**再生位置を受け取って、フェーダーをそこの値に合わせる**（Phase 93）。

        オートメーションはモデルの値を書き換えないので、**画面が自分で引きに行く**
        必要がある（`Track::getEffectiveVolumeDbAt()`）。配るのは
        `MainComponent::setPlayheadDisplay()`1箇所（8.33と同じ形）。 */
    void setPlayheadSeconds (double seconds);

    /** ストリップ1本の標準幅。ConsoleView側のレイアウト計算に使う。 */
    static constexpr int stripWidth = 104;

    //==========================================================================
    // 8.298：Consoleでの行の高さ（Phase 291）
    //
    // 8.299：**`ConsoleLayout`へ移しました**（Phase 292）——
    // マスターのストリップも同じ数字で割り付けるためです。
    // ここに残っているのは**そちらへの別名**だけです（書き写しではありません）。

    static constexpr int stripMargin = ConsoleLayout::stripMargin;
    static constexpr int nameRowHeight = ConsoleLayout::nameRowHeight;
    static constexpr int panKnobHeight = ConsoleLayout::panKnobHeight;
    static constexpr int panReadoutHeight = ConsoleLayout::panReadoutHeight;
    static constexpr int buttonRowHeight = ConsoleLayout::buttonRowHeight;
    static constexpr int rowGap = ConsoleLayout::rowGap;

    /** 8.298：**Consoleでこの1本が要る、いちばん低い高さ**（Phase 291／本人の指定）。

        中身は「フェーダーとメーターの最低（`ConsoleLayout::minimumFaderAreaHeight`）」に、
        **高さの決まっている行を全部足したもの**です（`ConsoleLayout::stripFixedHeight`）。
        **ラックは0**で数えています——本人の指定で、
        ここまで縮めたときに消えてよいのはラックだけだからです。

        `ConsoleView`がこれを使って**パネルの下限**を申告し、
        `MainComponent`がそこまでしか縮められないようにします。 */
    static constexpr int minimumConsoleHeight = ConsoleLayout::stripFixedHeight
                                                  + ConsoleLayout::minimumFaderAreaHeight;

    /** 8.298：フェーダーとメーターに実際に割り当てた高さ（Phase 291）。

        **`--header-selftest`が見ます。** 「下限を割らない」は足し算で決めていますが、
        **足し算が実際の割り付けと合っているか**は、置いてみないと分かりません。 */
    int getFaderAreaHeight() const { return volumeSlider.getHeight(); }

    /** 仕様書5.7：インサートスロットの表示を作り直す（サイドチェインのUndo/Redo追従用）。 */
    void refreshInsertSlots() { rack.refreshInsertSlots(); }

    /** 仕様書5.2.4：VCAの割り当て表示を更新する（Phase 12d-2）。

        リンク情報はVCAトラック側のValueTreeに入っているため、
        **リンクされる側のストリップは自分の購読では変化に気づけない。**
        割り当てが変わったら、ConsoleViewが全ストリップに対してこれを呼ぶ。 */
    void refreshVcaAssignment() { rack.refreshVcaAssignment(); }

    /** 8.314：**表示をモデルへ合わせ直す**（Phase 307／本人の要望）。

        ストリップが購読しているのは**自分のトラックだけ**なので、
        **フォルダのM・Sが変わっても届きません**——
        「借りている点灯」はそこを見ているので、外から声を掛けてもらいます
        （`ConsoleView`が root の通知を受けて配ります）。 */
    void refreshFromModel() { updateControlsFromModel(); }

    /** 仕様書5.7.1：このトラックのプラグインが持つレイテンシの表示を更新する（Phase 12e）。

        **どのトラックが全体の補正量を押し上げているか**を見るための表示なので、
        0のトラックには何も出さない（並んだときに情報量が増えすぎるため）。
        ConsoleViewがメーターと同じタイマーで呼ぶ。 */
    void refreshLatencyDisplay();

    /** 8.295：MIDIの絵の色を、音源GUIが出ているかどうかに合わせる（Phase 288／改善案1）。

        **`ConsoleView`のタイマーから呼ばれます**（メーターと同じ経路）。
        窓はGUIの「×」でも閉じられるので、**こちらから見に行かないと**
        オレンジが消えません。値が変わらなければ何もしません。 */
    void refreshInstrumentEditorState();

    //==========================================================================
    // 仕様書4.4・6章：ブラウザからのドラッグ&ドロップの受け口（Phase 21）。
    //
    // **落とし先はストリップ全体**にしてある（ラックの範囲だけに狭めると、
    // フェーダーの上へ落としたときに何も起きず、理由も分からないため）。
    // 判定と実行そのものはTrackRackComponentの静的関数に集約している。

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    //==========================================================================
    /** 8.67：**空いているところを掴んでトラックを並べ替える**（Phase 106／改善案㉛）。

        Consoleでミックスしている最中に並びを変えたくなったとき、
        それまではアレンジ画面まで戻る必要がありました（8.60の「+ Track」と同じ話）。

        **掴めるのはストリップの地とトラック名**です。フェーダー・つまみ・ボタン・
        ラックはそれぞれ自分でマウスを受けるので、ここへは落ちてきません
        （＝**操作の邪魔をしない**）。トラック名は`setInterceptsMouseClicks(false)`に
        してあり、名前を掴む＝そのトラックを掴む、になります。

        受け口は`ConsoleView`（並びを知っているのはあちら）。 */
    void mouseDrag (const juce::MouseEvent& e) override;

    /** 8.162：**トラック名をダブルクリックで書き換える**（Phase 200／本人の要望）。

        名前のラベルはマウスを受けない（並べ替えのために透明にしてある）ので、
        **ダブルクリックはストリップの地へ落ちてきます。**
        押した場所が名前の上かどうかは、ここで見ます。 */
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    /** 8.295：種類の絵を押した（Phase 288／改善案1）。

        **押せるのはMIDIだけ**です（音源のGUIを出す／しまう）。Consoleには
        フォルダを畳む場所がないので、フォルダの絵は**見せるだけ**にしてあります
        ——押せない絵には枠を付けません（8.161）。

        絵はマウスを受けない（自前で描いているだけ）ので、
        **クリックはストリップの地へ落ちてきます**（名前と同じ形）。 */
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void updateControlsFromModel();

    /** 8.162：名前の板をトラックの色で塗り直す（Phase 200）。

        **色は`updateControlsFromModel()`から呼んで合わせること**——
        インスペクタで色を変えたときも、ここへ届くのはモデルの変更通知だけです（1.15）。 */
    void updateNameColours();

    /** 8.162：入力欄で確定した名前をモデルへ書く（Phase 200）。 */
    void commitNameEdit();

    /** 8.295：種類の絵を描く（Phase 288）。**Consoleでもインスペクタでも同じ**なので、
        `paint()`の分かれ道の両方から呼びます。 */
    void drawTypeIcon (juce::Graphics& g);

    /** 8.115：そのパラメータをオートメーションのレーンが握っているか（Phase 150）。

        **点が1つ以上あって、バイパスされていない**ときだけ真です。
        レーンを作っただけ（点が0本）では握っていません。 */
    bool isParameterAutomated (const juce::String& targetId) const;
    void notifyChanged();

    // 他の場所（ソロによる表示変化など）でモデルが変わったときも表示を合わせる
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    Track track;
    ProjectModel& project;
    AudioEngine& audioEngine;

    /** 8.54：いまの再生位置（Phase 93）。フェーダーに出す値を決めるのに使う。
        **停止中も意味を持つ**（止めた場所のオートメーション値が出る）。 */
    double playheadSeconds = 0.0;

    juce::Label nameLabel;
    // Phase 61：ダブルクリックで数値入力（8.1のC2）。Alt＋クリックで初期値に戻る
    ValueEntrySlider panSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    /** 8.63：パンの数値（Phase 101／改善案⑦）。

        **dB表示と同じ扱い**：クリックで打ち込み、右クリックでつまみと同じメニュー。
        ノブは小さく、つまみの上では打ち込めないため（8.22と同じ理由）。 */
    ValueReadoutLabel panReadout;
    ValueEntrySlider volumeSlider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    /** Phase 63：右クリックで数値入力、ダブルクリックで初期値（8.24）。 */
    ValueReadoutLabel volumeValueLabel;
    juce::TextButton muteButton { "M" };
    juce::TextButton soloButton { "S" };
    LevelMeterComponent meter;

    // 仕様書5.3/5.7/5.2.2/5.2.4/5.6：音源・インサート・センド・VCA・書き込みモード（Phase 29）
    TrackRackComponent rack;

    /** ラックを入れる枠（Phase 66／8.1のC14）。

        **インサートやセンドが増えると、ラックが縦を食い尽くしていました。**
        フェーダーとメーターは残りをもらう作りなので、スロットを4つも挿すと
        メーターが数ピクセルまで縮み、何も読めなくなります。

        ラックへ渡す高さに上限を設け、**入り切らないぶんはスクロールで届く**
        ようにしてあります。Consoleを下部パネルへ移した（Phase 66）ことで
        縦がさらに限られるため、ここを直さないと使えません。 */
    juce::Viewport rackViewport;

    /** 8.283：ラックとフェーダーの境目（Phase 276／本人の要望）。
        **掴んで動かすと、全ストリップのラックの高さが同時に変わります。** */
    ConsoleLayout::FaderResizer faderResizer;

    /** フェーダーとメーターに必ず残す高さは`ConsoleLayout::minimumFaderAreaHeight`。
        **ここに書き写さないこと**——マスターと食い違うと、行が揃いません（8.283）。 */

    //==========================================================================
    // 8.296：インスペクタに置くとき（Phase 289／本人の指定）

    const Layout layout;

    /** インスペクタでの2列の割り付け。**同じ関数から測って置く**ので、
        高さと実際の位置が食い違いません（`TrackRackComponent`と同じ作り）。 */
    int layOutForInspector (juce::Rectangle<int> area, bool apply);

    /** インスペクタでのフェーダー＋メーターの高さ。

        **Consoleの`minimumFaderAreaHeight`（130px）とは別の数字**です。
        あちらは「下部パネルが狭いときに割ってはいけない線」ですが、
        こちらはパネルの下端に固定されているので、
        「これだけあれば読める」を素直に書けます。

        8.297：**170から255へ**（Phase 290／本人の指定で1.5倍）。
        「ボリュームフェーダーとメーターが窮屈にも感じる」——
        170pxではつまみ（20px）を引いた**動かせる幅が150px**しかなく、
        66dBぶんをそこに詰めていました（1pxで0.44dB）。255pxなら0.28dBです。 */
    static constexpr int inspectorFaderHeight = 255;

    /** 画面が低くて`inspectorFaderHeight`が入らないときに、**ここまでは縮める**高さ。

        **縮むのはフェーダーだけ**です（他は数字が決まっている）。
        ここを下回らせないので、**メーターが読めない太さになることはありません**。 */
    static constexpr int inspectorMinimumFaderHeight = 110;

    /** 2列のあいだの間隔。 */
    static constexpr int inspectorColumnGap = 8;

    //==========================================================================
    /** VCAトラックのストリップか。設計書2.3.2にならい、フェーダー・ミュート・ソロ以外
        （パン・メーター）を持たない簡易表示にする。 */
    bool isVca = false;

    // 仕様書5.7.1：このトラックのプラグインが持つレイテンシ（Phase 12e）。
    // 0のときは非表示にして場所も取らない。
    juce::Label latencyLabel;
    int lastShownLatencySamples = -1; // -1＝未表示。無駄な描き直しを避けるために持つ

    bool isUpdatingFromModel = false; // モデル→UI反映中に、UI→モデルの書き戻しを防ぐ

    // 仕様書4.4：ドラッグ中のプラグインがこのストリップの上にあるか（Phase 21）
    bool isDragOver = false;

    /** 8.301：いま選ばれているトラックか（Phase 294）。`ConsoleView`が入れます。 */
    bool isSelected = false;

    //==========================================================================
    // 8.295：トラックの種類を示す絵（Phase 288／改善案1。本人の指定）
    //
    // **置き場所はdB表示の右**（メーターの下）——本人の指定です。
    // 絵と色の決め方は`TrackTypeIcons`が持っていて、
    // **アレンジのトラックヘッダーと同じもの**を使います（対応表は1つ。1.27）。

    TrackTypeIcons::Cached typeIcon;

    /** 絵の位置。**`resized()`が決めて、`paint()`と`mouseDown()`が読む**
        ——描いてある場所と押せる場所を1つの値から出すため（8.125と同じ話）。 */
    juce::Rectangle<int> typeIconBounds;

    /** 音源GUIが出ているか（`refreshInstrumentEditorState()`が更新する）。 */
    bool instrumentEditorOpen = false;

    /** 絵の大きさ（正方形）。

        8.295：**アレンジのヘッダーと同じ15px**（Phase 288／本人の指定で
        「元々の●やINボタンと同じサイズ」）。**同じ絵を2つの画面に出すので、
        大きさも揃えること**——片方だけ大きいと、同じものに見えません。

        行のほう（`ConsoleLayout::volumeReadoutRowHeight`）は18pxです。
        押せる絵の枠は1px外へ出るので（`TrackTypeIcons`）、17pxぶん要ります。 */
    static constexpr int typeIconSize = 15;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripComponent)
};
