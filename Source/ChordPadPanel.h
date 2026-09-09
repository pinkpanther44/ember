#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "ChordEngine.h"
#include "SelectionState.h"
#include "AudioEngine.h"

//==============================================================================
/**
    設計書2.3.5「コードパッドパネル」／仕様書5.11.2（Phase 43）。

    エディタパネルの中身として、ピアノロールと排他で出る。

    **計算はここに書かない。** 候補の並びは`buildChordGrid()`、色の元になる
    「繋がりやすさ」は`connectionScore()`が返すものをそのまま使う（`ChordEngine.h`）。
    ChordCanvasでは両方がパッドの描画関数に同居していて、値だけを確かめる術が
    無かった。分けてあるので、色や並びがおかしいと思ったら
    `Tools\ChordEngineTests`を先に走らせれば、UIとロジックのどちらの問題か切り分けられる。

    **パッドは子コンポーネントではない。** 12×12＝最大144個になるうえ、
    キーやページを変えるたびに作り直しになるため、矩形を自前で持って
    `paint()`で描き、`mouseDown()`で当たり判定をしている（ChordCanvasと同じ方針）。
*/
class ChordPadPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::ValueTree::Listener
{
public:
    ChordPadPanel (ProjectModel& projectToUse, SelectionState& selectionToUse,
                   AudioEngine& audioEngineToUse);
    ~ChordPadPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    /** モデルが差し替わった／外で変わったときに呼ぶ（プロジェクト読み込み後など）。 */
    void refreshFromModel();

    /** コードを挿す位置（秒）。MainComponentが再生位置を渡す。 */
    void setInsertPosition (double seconds);

    /** コードを挿したあと、次の位置へ進めてほしいときに呼ばれる（仕様書5.11.2の
        「カーソルが自動前進する」）。再生位置を実際に動かすのはMainComponentの仕事。

        **「モデルが変わった」ことを知らせるコールバックは用意していない。**
        アレンジ画面はValueTreeを直接購読していて（`TimelineComponent`）、
        コード区間の増減もプロパティの変化もそこで拾われる。ここから改めて
        伝えると、同じ更新の経路が2本になる（HANDOVER 1.15と同じ話）。 */
    std::function<void (double newPositionSeconds)> onInsertPositionChanged;

private:
    /** 右クリックを基底へ渡さないボタン（1.39）。

        このパネルは**右クリック＝試聴**に割り当てているので、ボタンの上で
        右クリックしたときに押したことになってしまうと、聞くつもりが
        コードを入れてしまいます。基底の`mouseDown`を呼ばなければ、
        `mouseUp`でのクリック通知も起きません。 */
    class LeftClickOnlyButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;

        /** 8.72：右クリックにメニューを付けたいボタン用（Phase 112）。
            **空のままなら今までどおり、右クリックは何も起こしません。** */
        std::function<void()> onRightClick;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onRightClick != nullptr)
                    onRightClick();

                return;
            }

            juce::TextButton::mouseDown (e);
        }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    //==========================================================================
    // モデルの変化に追従する（Undo/Redo、アレンジ画面での追加・削除、テンポ変更）。
    //
    // **これが無いと、コードのUndoでパッドの色が古いままになる。** 色は
    // 「直前のコード」から計算しているので、区間が1つ消えただけで全部変わる。
    // アレンジ画面は購読しているので追従しますが、こちらは別の画面です（1.15）。

    void updateProjectSubscription();

    /** その変化が、パッドの表示に影響するか。

        全部の変化に反応すると、クリップをドラッグしている間じゅうグリッドを
        組み直すことになる。

        **ノードの種別まで見ること。** `ProjectIds.cpp`では、コード区間の
        `startTime`とクリップの`startTime`が**同じ文字列**で、`juce::Identifier`として
        も等しくなります（`trackId`と`clipId`がどちらも`"id"`なのと同じ）。
        プロパティ名だけで判定すると、オーディオクリップを動かしただけで
        ここが反応します。 */
    static bool affectsPads (const juce::ValueTree& tree, const juce::Identifier& property);


    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;

    /** キー・ページ・Triad設定から候補グリッドを作り直し、矩形を計算する。 */
    void rebuildGrid();

    /** 現在のサイズから、各パッドの矩形を計算する。 */
    void layoutPads();

    /** グリッドの地（1行おきの帯と、列の区切り線）を描く（Phase 65／8.26）。

        **パッドより先に呼ぶこと。** 歯抜けのグリッドではパッドの無い場所が広く空くので、
        地が無いと「どの度数の欄か」を目で追えない。 */
    void drawGridBackground (juce::Graphics& g);

    juce::Rectangle<int> getGridArea() const;

    /** 対象のコードトラック。無ければ`state.getParent().isValid()`がfalse。 */
    Track getChordTrack() const;

    /** 挿入位置の直前にあるコード（スコアの基準）。無ければnullptrを返す。 */
    const Chord* getPreviousChord() const;

    /** その時刻を含む1小節の長さ（秒）。**中身はProjectModelへ**（8.98／Phase 138）。 */
    double getBarSeconds (double atTime) const;

    /** 8.75：コードを入れる位置（Phase 115）。**カーソルの位置そのもの**で、
        小節線へは寄せません（寄せると、寄せた先が別の区間に入ることがある）。 */
    double getChordInsertPosition() const;

    void insertChord (const Chord& chord);

    void updateHeaderText();

    //==========================================================================
    // 仕様書5.11.3：コード進行をMIDIノートにする（Phase 44）

    /** 書き込み先の候補（MIDIトラック）を並べ直す。トラックが増減すると変わる。 */
    void refreshTargetTrackList();

    /** 下段のコントロールから発音パラメータを組み立てる。

        **時間の単位は拍**（`generateChordNotes()`がそう決めている）。
        秒へ直すのは書き込みのときだけ。 */
    ChordPerformance getPerformance() const;

    /** 発音パラメータを前回の値に戻し、以後の変更を保存するようにする（Phase 53／8.1のA3）。

        **コンストラクタの最後に1回だけ呼ぶこと。** 各コントロールの初期値を
        設定し終えた後でないと、既定値で上書きされる。

        保存先は`AppSettings`（設計書2.5）。プロジェクトファイルではないのは、
        **どのプロジェクトを開いても前回の設定のままにしたいから**で、
        ピアノロールの構成音カラーリング（Phase 46）と同じ考え方。 */
    void setUpPerformanceSettingsPersistence();

    //==========================================================================
    // 8.72：**Writeは「カーソル上の1コードだけ」**（Phase 112／8.1のD12。仕様書5.11.3）
    //
    // Phase 111まで、Writeは**進行まるごとを1つのクリップにする**作りでした（8.9）。
    // 組み終わってから一度に出す使い方には合いますが、
    // **1コード入れては聴く**という組み立て方には合いません
    // （押すたびに進行全体が作り直され、手で直したノートも消えます）。
    //
    // ### 決めた規則（Phase 112で決めたこと）
    //
    // | | |
    // |---|---|
    // | カーソル | **再生位置**（コードパッドの挿入位置と同じ基準） |
    // | 書き足し先 | **その区間を含んでいるクリップ**。無ければその区間ぶんの新しいクリップ |
    // | 2度押し | **その区間ぶんのノートだけ差し替える**（クリップは増えない） |
    //
    // **クリップは増えません。** 押すたびに1コードぶんが同じクリップへ積まれ、
    // 見た目は今までと同じ「進行まるごと1クリップ」に育ちます。
    //
    // **進行まるごと**は右クリックのメニューに残してあります（組み終わってから
    // 一度に出す使い方は、これはこれで要るため）。

    /** カーソル（再生位置）の上のコード区間。無ければ`state.isValid()`がfalse。

        **区間の中に無いときは、その手前の区間を返します。** パッドを押すと
        挿入位置は次の小節へ進むので（8.9）、そのまま素直に「含む区間」だけを見ると
        **いま入れたコードが対象にならない**。 */
    ChordRegion findChordRegionAtCursor() const;

    /** 区間1つぶんのノートを、書き足し先のクリップへ入れる。
        **Undoの区切りは呼び出し側で切ること**（まとめ書きは1回で戻したい）。 */
    void writeChordRegionToTrack (const ChordRegion& region, Track& targetTrack);

    /** 8.72：カーソル上の1コードだけ書き込む（Writeボタン。Phase 112）。 */
    void writeChordAtCursorToTrack();

    /** コードトラックの全区間を、選んだMIDIトラックのクリップへ書き込む
        （Writeボタンの右クリックメニュー）。 */
    void writeNotesToTrack();

    /** 書き込み先に選ばれているMIDIトラック。無ければ`state.getParent().isValid()`がfalse。 */
    Track getTargetTrack() const;

    //==========================================================================
    // 8.73：Phase 113で足したもの（仕様書5.11.3）

    /** ストロークを選ぶ（3つのボタンの見た目もここで揃える）。 */
    void setStroke (ChordStroke newStroke);

    /** 8.73：**「4分音符」を選んだら4分音符1個**（Phase 113）。

        Phase 112までは、音価を選ぶと**コード区間を敷き詰めて**いました
        （1小節ぶんのDm7が4分音符4個になる）。
        Writeが1コードずつになった（8.72）いま、それでは
        **1回押すたびに1小節ぶん進む**ことになり、刻みを手で組み立てられません。

        | 音価の選択 | 1回のWriteで出るもの | カーソルの進み |
        |---|---|---|
        | コード区間 | その区間まるごとのロングトーン | 区間の終わりへ |
        | 全音符〜32分 | **その音価1個** | **その音価ぶん** |

        まとめ書き（右クリック）は今までどおり**敷き詰め**ます
        （組み終わってから一度に出す操作なので、そちらは刻みたい）。

        書き出す長さ（秒）。0以下なら「コード区間ぶん」。**音価は`atTime`の拍で測ります**（8.98／Phase 138）。 */
    double getSingleHitLengthSeconds (double atTime) const;

    /** コード1つを、指定の位置・長さで書き込み先のクリップへ入れる。
        `tile`がtrueなら、その長さを音価で敷き詰める（まとめ書き用）。 */
    void writeChordToTrack (const Chord& chord, double startSeconds, double lengthSeconds,
                             Track& targetTrack, bool tile);

    /** 8.73：**次にWriteで出るコード**（Phase 113）。無ければ`state.isValid()`がfalse。 */
    ChordRegion getWriteTargetRegion() const;

    /** 8.73：`writeTargetLabel`の文字を、いまの状態に合わせる（Phase 113）。 */
    void updateWriteTargetText();

    /** 8.73：発音パラメータの段が要る高さ（1段ぶん／2段ぶん）。

        **`resized()`と`getGridArea()`の両方が読む。** 片方だけ直すと、
        パッドが段の下に潜ります（そこに前から注意書きがあります）。 */
    int getWriteAreaHeight() const;

    //==========================================================================
    // 仕様書5.11.2：カスタムコード入力と右クリック試聴（Phase 45）

    /** 下段の Root／Type／Bass ＋テンショントグルから、コードを組み立てる。 */
    Chord buildCustomChord() const;

    /** 右クリックしたコードを、いまの発音パラメータのボイシングで鳴らす。

        **鳴らすのは書き込み先のトラックの音源**（`AudioEngine::previewNoteOn`）。
        パッドで聞いた音と、Writeで入る音を一致させるため。 */
    void startAudition (const Chord& chord);

    /** 鳴らしている音を止める。**マウスを離すときだけでなく、パネルが隠れるときや
        壊れるときにも呼ぶこと。** 呼ばないと鳴りっぱなしになる（1.4と同じ話）。 */
    void stopAudition();

    /** スコア（0.0〜1.0）を4段階のパッド色にする（仕様書5.11.2）。

        **新しい色をパレットへ足さず、`AppColours::orange`の濃さで表している。**
        ライトとダークの両方を埋め忘れる事故（HANDOVER 1.34）を避けるためで、
        地の色の上に重ねるぶん、テーマが変わっても自動的に馴染む。 */
    static juce::Colour padColourForScore (float score);

    ProjectModel& project;
    SelectionState& selection;
    AudioEngine& audioEngine;

    /** いま購読しているルート。**プロジェクトを読み込むと差し替わる**ので、
        そのたびに付け替える必要がある（3.1）。 */
    juce::ValueTree subscribedProjectState;

    ChordGrid grid;

    struct Pad
    {
        Chord chord;
        juce::Rectangle<int> area;
    };

    std::vector<Pad> pads;

    /** 1行ぶんの領域（見出しの列も含む全幅）。**交互の地を塗るのにも使う**ので、
        見出しの矩形ではなく行そのものを覚えている（Phase 65／8.26）。 */
    std::vector<juce::Rectangle<int>> rowAreas;

    ChordGridPage page = ChordGridPage::main;
    bool triadMode = false;

    double insertPositionSeconds = 0.0;

    // getPreviousChord()がポインタを返す先。進行の先頭かどうかで加点が変わるので、
    // 「無い」ことをnullptrで表せるようにしてある（値だけでは区別できない）。
    mutable Chord previousChord;

    juce::Label keyCaption;
    juce::ComboBox keyRootBox;
    juce::ComboBox keyModeBox;

    /** 8.74：直前のコードの枠の見出し（Phase 114）。枠自体は矩形で持つ。 */
    juce::Label previousCaption;
    LeftClickOnlyButton pageButton { "Main" };
    LeftClickOnlyButton triadButton { "7th" };
    juce::Label positionLabel;

    // 仕様書5.11.3の発音パラメータ（Phase 44）。
    // **プロジェクトには保存していない。** 音そのものではなく「どう鳴らすか」の
    // 作業設定で、書き込んだ結果はノートとして残るため。
    // Phase 53で、代わりに`AppSettings`へ入れて次回起動時に戻すようにした
    // （`setUpPerformanceSettingsPersistence()`）。
    //
    // **書き込み先のトラック（targetTrackBox）だけは保存しない。**
    // トラックはプロジェクトごとに違うので、別のプロジェクトで復元しても
    // 意味のある選択にならない。
    juce::ComboBox targetTrackBox;
    juce::ComboBox voicingBox;
    juce::ComboBox octaveBox;
    juce::ComboBox lengthBox;
    juce::Slider gateSlider;
    juce::Slider velocitySlider;

    //==========================================================================
    // 8.73：ストロークは**トグル3つ**（Phase 113／仕様書5.11.3）
    //
    // Phase 112まではコンボボックス（None／Down／Up／Alt）でした。
    // **いちばんよく切り替えるもの**なのに、開いて選ぶ2手が要り、
    // しかも**いまどれなのかが幅次第で読めない**（狭いと文字が切れる）。
    //
    // **Altは外しました。** 「1回ごとに交互」は敷き詰めるときの動きで、
    // Phase 113で**Writeが1個だけ書く**ようになったため、
    // 押すたびに同じ向きになり、Downと区別が付かなくなります。

    LeftClickOnlyButton strokeNoneButton;
    LeftClickOnlyButton strokeDownButton { "Down" };
    LeftClickOnlyButton strokeUpButton   { "Up" };

    /** いま選ばれているストローク。**ボタンの`getToggleState()`を見に行かないこと**——
        3つのうちどれが正か、という判断が読む側ごとに散らばります（1.26）。 */
    ChordStroke stroke = ChordStroke::none;

    juce::Slider deviationSlider;

    /** 8.73：**次にWriteで出るコード**（Phase 113）。Writeボタンのすぐ左。

        押す前に何が出るか分かるようにするためのもの。上段の「直前：」は
        **パッドで入れるときの手前のコード**で、別のものです。 */
    juce::Label writeTargetLabel;

    LeftClickOnlyButton writeButton { "Write" };

    // 仕様書5.11.2：カスタムコード入力（Phase 45）
    juce::ComboBox customRootBox;
    juce::ComboBox customTypeBox;
    juce::ComboBox customBassBox;
    juce::OwnedArray<juce::TextButton> tensionButtons;
    LeftClickOnlyButton customAddButton { "Add" };

    /** 組み立て中のコードを出す枠。パッドと同じ扱い（左クリックで入力、右クリックで試聴）。 */
    juce::Rectangle<int> customPreviewArea;

    /** 8.74：**直前のコードの枠**（Phase 114）。上段、キーの右。

        文字だけだと**どんな響きだったか思い出せない**ので、枠にして
        **右クリックで鳴らせる**ようにしてある（パッドと同じ扱い）。

        **左クリックでは何も起きません。** ここは「もう置いてあるコード」なので、
        押して入力できると「同じコードがもう1つ入る」ことになります。 */
    juce::Rectangle<int> previousChordArea;

    /** 8.74：`previousChordArea`に出しているコード。空なら枠を出さない。 */
    mutable ChordRegion previousChordRegion { juce::ValueTree() };

    /** 鳴らしている音。止めるときに使うので、鳴らした先のトラックIDも一緒に覚える。 */
    std::vector<int> auditionPitches;
    juce::String auditionTrackId;

    static constexpr int controlRowHeight = 30;
    static constexpr int customRowHeight = 30;
    static constexpr int writeRowHeight = 32;

    /** 8.73：発音パラメータの段を**2段に折り返す**のに要る幅（Phase 113）。

        独立した窓（8.71）は狭くできるので、1段に詰めると
        **右のほうのつまみが幅0になって消えます**（`jmin`で削られるため）。
        消えたことに気づけないので、**入らないなら折り返す**ようにしてあります。 */
    /** 1段に収まる最小の幅。**これより狭ければ2段に折り返す**（8.73／Phase 113）。

        8.99：**1080では足りていませんでした**（Phase 137）。実際に要るのは
        左の3つ（130+66+56）＋間＋右の8つ＋右端のWriteとトラック名で約1163px。
        足りないと**右端のつまみが幅0になって消えます**（`place()`が`jmin`で削るため）。

        **中身を足したらここも数え直すこと。** 数えずに足すと、
        「広げたのに1つ消えている」という気づきにくい壊れ方をします。 */
    static constexpr int writeRowSingleLineWidth = 1170;
    static constexpr int columnHeaderHeight = 18;
    static constexpr int rowLabelWidth = 96;
    static constexpr int numColumns = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordPadPanel)
};
