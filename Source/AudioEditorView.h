#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "ProjectModel.h"
#include "WaveformCache.h"

//==============================================================================
/**
    設計書2.3.4のオーディオエディタ（Phase 79／8.39）。

    **エディタパネルの中身の1つ**です（ピアノロール・コードパッド・Consoleと同じ枠を
    取り合う。8.27・8.11）。オーディオトラックを選ぶとここへ切り替わります。

    ### 表示するのは「ソースファイルの時間」

    ピアノロールは**クリップの中身の時刻**、アレンジ画面は**曲の時刻**で描いています（8.28）。
    ここは**3つめの基準＝ソースファイルの先頭からの秒数**です。
    ヒットポイントがこの基準で保存されており（`AudioClip::getHitPoints()`）、
    ワープマーカー（仕様書5.5.1）も同じ基準になるためです。

    **ファイル全体を出し、クリップが使っている範囲の外は伏せます。**
    トリムで隠れているところを見せないと、「どこを切り出したのか」が分かりません
    （アレンジ画面・ピアノロールの伏せ表示と同じ考え方）。

    3つの基準の行き来はこのクラスの中だけで完結させ、外へは
    **曲の時刻**（再生位置・シーク要求）で渡します。
*/
class AudioEditorView : public juce::Component,
                         private juce::ChangeListener,
                         private juce::ScrollBar::Listener
{
public:
    AudioEditorView (ProjectModel& projectToUse, WaveformCache& waveformCacheToUse);
    ~AudioEditorView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    /** 編集対象のクリップを渡す。無効なものを渡すと「まだ何も無い」表示になる。

        **ValueTreeで持ちます**（番号ではなく。1.32）。トラック内のクリップが
        増減しても、別のクリップへすり替わりません。 */
    void setClip (AudioClip clipToEdit);

    /** いま出しているクリップ（無効なこともある）。 */
    AudioClip getClip() const { return clip; }

    /** モデルを読み直して表示を合わせる（パネルを開くとき・プロジェクトが変わったとき）。 */
    void refreshFromModel();

    /** 仕様書5.1：プロジェクトを読み込み／新規作成した後に作り直す。

        **`keepSelection`はUndo/Redoのとき**（8.41/1.33）。同じプロジェクトの中の話なので、
        掴んでいるクリップを捨てない——捨てると「まだ何も無い」表示に戻ってしまう。 */
    void refreshAfterProjectChanged (bool keepSelection = false);

    /** 再生位置（**曲の時刻**）を渡す。クリップの範囲内にあるときだけ線を描く。 */
    void setPlayheadSeconds (double timelineSeconds);

    //==========================================================================
    // 仕様書5.5：波形の拡大縮小

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    //==========================================================================
    // 外への要求。**どれもMainComponentの入口へ返す**（アレンジ画面と同じものを通す）

    /** ルーラーをクリックしたとき（**曲の時刻**で返す）。 */
    std::function<void (double timelineSeconds)> onSeekRequested;

    /** 「まだ何も無い」表示のボタンから。取り込み先・録音先は呼ばれた側が決める。 */
    std::function<void()> onImportRequested;
    std::function<void()> onRecordRequested;

    void visibilityChanged() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart) override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    //==========================================================================
    // 領域

    juce::Rectangle<int> getToolbarBounds() const;
    juce::Rectangle<int> getRulerBounds() const;
    juce::Rectangle<int> getWaveformBounds() const;

    //==========================================================================
    // 座標の変換。**ソースファイルの先頭からの秒数**が基準（クラスの説明を参照）

    int sourceTimeToX (double sourceSeconds) const;
    double xToSourceTime (int x) const;

    /** 8.47：**逆再生のときだけ、クリップが使っている範囲を左右反転して見せる**（Phase 87）。

        画面の左から右が「鳴る順」になるので、**聞こえるとおりの形**が見えます。
        窓の外（トリムで隠れている範囲）はそのまま——鳴らない場所なので、
        ひっくり返す意味がありません。

        フェード・ゲイン・再生位置は**クリップの先頭からの位置**で描いているので、
        この反転をそのまま重ねて正しくなります（直す必要があるのはヒットポイントだけ）。 */
    int sourceTimeToDisplayX (double sourceSeconds) const;

    /** その逆。**波形の上をクリックした場所**を、ソースの時刻へ直す。 */
    double displayXToSourceTime (int x) const;

    /** いま反転して見せているか（逆再生で、窓の中を描くとき）。 */
    bool isShowingReversed() const;

    /** ソースの時刻 ⇔ 曲の時刻。クリップの位置とトリム量で決まる。 */
    double sourceTimeToTimeline (double sourceSeconds) const;
    double timelineTimeToSource (double timelineSeconds) const;

    /** 出しているファイルの長さ（秒）。読めないときはクリップの長さで代用する。 */
    double getSourceLengthSeconds() const;

    /** 見えている秒数と、スクロールの上限。 */
    double getVisibleSeconds() const;
    void updateScrollBar();

    /** 拡大率を変える。`anchorX`の位置にある時刻が動かないように寄せ直す。 */
    void setZoom (double newPixelsPerSecond, int anchorX);

    //==========================================================================
    // 描画

    void drawRuler (juce::Graphics& g, juce::Rectangle<int> area);
    void drawWaveform (juce::Graphics& g, juce::Rectangle<int> area);
    void drawEmptyState (juce::Graphics& g, juce::Rectangle<int> area);

    /** ルーラーの目盛りの間隔（秒）。**拡大率から決める**ので、
        拡大しても目盛りが詰まりすぎない。 */
    double getRulerStepSeconds() const;

    //==========================================================================
    void updateButtonVisibility();

    //==========================================================================
    // 仕様書5.5：フェードとクリップゲインの編集（Phase 80／8.40）

    /** 掴んでいるもの。**アレンジ画面のフェードハンドルと同じ操作**にしてある（8.29）。 */
    enum class DragMode { None, FadeIn, FadeOut, Gain, HitPoint };   // HitPointはPhase 85

    /** フェードハンドルの位置（波形の上端）。`fadeIn`でどちらかを選ぶ。 */
    juce::Point<int> getFadeHandlePosition (bool fadeIn) const;

    /** クリップゲインの線のY座標と、その逆算。
        **-24〜+24dBを波形の高さへ写す**（範囲は`minClipGainDb`／`maxClipGainDb`）。 */
    int gainDbToY (float gainDb) const;
    float yToGainDb (int y) const;

    /** 波形の右クリックメニュー（数値入力・0dBへ戻す・フェードを消す）。 */
    void showClipMenu (juce::Point<int> screenPosition);

    /** ゲインを数値で入れる欄を出す。 */
    void showGainEntry();

    void drawFades (juce::Graphics& g, juce::Rectangle<int> area);
    void drawGainLine (juce::Graphics& g, juce::Rectangle<int> area);

    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    DragMode dragMode = DragMode::None;

    /** 掴んだ時点の値。**ドラッグ中は「元の値＋動かした量」で書く**
        （いまの値に足すと、1回のドラッグで二重に動く。8.38と同じ話）。 */
    double dragOriginalFadeSeconds = 0.0;
    float dragOriginalGainDb = 0.0f;
    juce::Point<int> dragStartPosition;

    static constexpr float fadeHandleRadius = 5.0f;      // アレンジ画面と同じ大きさ（8.41）
    static constexpr float fadeHandleHitRadius = 9.0f;
    static constexpr float gainLineHitRadius = 6.0f;

    ProjectModel& project;
    WaveformCache& waveformCache;

    AudioClip clip { juce::ValueTree() };
    bool clipIsSet = false;

    /** 波形を描くために開いているファイル。**変わったときだけリスナーを付け直す**
        （毎回付けると、同じサムネイルに何重にも登録される）。 */
    juce::String thumbnailFilePath;

    /** 波形がまだ届いていない/幅が決まっていないうちは、全体表示にできない。
        **できるようになった時点で1回だけやる**ための覚え（Phase 79）。 */
    bool zoomToFitPending = false;

    double pixelsPerSecond = 120.0;
    double scrollStartSeconds = 0.0;
    double playheadSeconds = 0.0;

    static constexpr double minPixelsPerSecond = 4.0;
    static constexpr double maxPixelsPerSecond = 20000.0;
    static constexpr double zoomStepFactor = 1.25;

    static constexpr int toolbarHeight = 30;
    static constexpr int rulerHeight = 18;
    static constexpr int scrollBarHeight = 12;

    juce::Label clipNameLabel;
    juce::TextButton zoomInButton, zoomOutButton, zoomFitButton;

    /** 「まだ何も無い」ときだけ出すボタン（D1のメモにある2つ）。 */
    juce::TextButton importButton, recordButton;


    //==========================================================================
    // 仕様書5.5.1：ヒットポイント（Phase 85／8.45）

    /** 自動検出を走らせる（**別スレッド**。終わったらモデルへ書き戻す）。

        `HitPointDetector`はファイルを読み直すので、**画面のスレッドで回さないこと**
        （長いファイルではその間ずっと固まる）。 */
    void detectHitPoints();

    /** 座標に近いヒットポイントの番号（無ければ-1）。並びは時刻順。 */
    int findHitPointAt (juce::Point<int> position) const;

    void drawHitPoints (juce::Graphics& g, juce::Rectangle<int> area);

    /** 検出中かどうか（ボタンの表示に使う）。 */
    bool isDetectingHitPoints = false;

    /** 掴んでいるヒットポイントの番号と、掴んだ時点の時刻。 */
    int draggedHitPoint = -1;
    double dragOriginalHitTime = 0.0;

    static constexpr float hitPointHitRadius = 5.0f;

    /** 検出を回すスレッド（Phase 85）。

        **`juce::Thread::launch`ではなくここに置く理由**：あちらは投げっぱなしなので、
        検出中にアプリを閉じると、**壊れたあとのオブジェクトを触りに来ます**。
        `ThreadPool`はデストラクタで仕事の終わりを待ってくれます。 */
    juce::ThreadPool detectionPool { 1 };

    juce::TextButton detectButton;
    juce::ScrollBar horizontalScrollBar { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEditorView)
};
