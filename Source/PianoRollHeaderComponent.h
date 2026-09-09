#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"
#include "PianoRollComponent.h"
#include "NameEntry.h"   // 名前を入れるダイアログ（Phase 72で1つにまとめた）

//==============================================================================
/**
    ピアノロールのルーラーとコード帯（設計書2.3.3／8.1のG3、Phase 67）。
    **Phase 72でループ帯・マーカー帯・シーク・再生カーソル・コード区間の操作を足しました**（8.33）。

    **ピアノロール本体（`PianoRollComponent`）の外側に置く。**
    本体はViewportの中で縦にスクロールするので、同じ部品の中に描くと
    上へスクロールしたときに目盛りごと流れて消える。アレンジ画面で
    ルーラーとコードトラックを固定行にしてあるのと同じ形（8.20）。

    **座標は自分で計算しない。** 時刻→X座標の変換は本体の
    `timelineTimeToX()`を呼ぶ。別々に計算すると、拡大縮小したときに
    目盛りとグリッドが1pxずれる（8.23のC7で踏んだのと同じ）。

    構成（上から）：
    - ループ帯：範囲を引く／端を伸縮／ダブルクリックで有効・無効
    - マーカー帯：旗をクリックでジャンプ、ドラッグで移動
    - ルーラー：小節線・拍線と小節番号。**曲の小節番号**（クリップの中身の何秒目かではない）。
      クリックで再生位置（ドラッグで追従）
    - コード帯：コードトラックのコード区間。掴んで移動／端で伸縮／ダブルクリックでコードパッド
    - 左上の角：ズームのボタン（−／＋／Fit）

    **アレンジ画面（`TimelineComponent`）と同じ操作にしてあります**（8.29の表）。
    モデルを触る部分は同じ`ProjectModel`の関数を通るので、
    「片方だけ違う動きをする」ことは起きません。
*/
class PianoRollHeaderComponent : public juce::Component
{
public:
    PianoRollHeaderComponent (ProjectModel& projectToUse, PianoRollComponent& pianoRollToUse);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    /** 8.123：**この帯の上ではホイールだけで拡大縮小**（Phase 158／改善案20）。
        アレンジ画面のルーラーと同じ扱いにしてある。 */
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;


    // アレンジ画面のルーラーと同じ比率にしてある（見比べたときに揃って見えるように）
    static constexpr int loopStripHeight = 7;
    static constexpr int markerStripHeight = 11;
    static constexpr int rulerContentTop = loopStripHeight + markerStripHeight;
    static constexpr int rulerHeight = rulerContentTop + 18;
    static constexpr int chordStripHeight = 20;
    static constexpr int totalHeight = rulerHeight + chordStripHeight;

    /** 再生位置を渡す（タイムライン上の時刻）。 */
    void setPlayheadSeconds (double timelineSeconds);

    //==========================================================================
    // ここから外へ返すもの。**モデルを触るのはこの中でも、
    // 「再生位置を動かす」「名前を付けてマーカーを挿す」はエンジンとダイアログの話**なので、
    // ビュー（`PianoRollView`）経由でMainComponentへ返す（8.11と同じ形）。

    std::function<void (double timelineSeconds)> onSeek;
    std::function<void (double timeSeconds, bool askForName)> onInsertMarkerRequested;
    std::function<void()> onLoopChanged;
    std::function<void (double startTimeSeconds)> onChordRegionDoubleClicked;

private:
    /** 目盛りを描く範囲（左端の鍵盤のぶんを除いた部分）。 */
    juce::Rectangle<int> getTimeArea() const;
    juce::Rectangle<int> getLoopStripArea() const;
    juce::Rectangle<int> getMarkerStripArea() const;
    juce::Rectangle<int> getChordStripArea() const;

    void drawRuler (juce::Graphics& g);
    void drawLoopRange (juce::Graphics& g);
    void drawMarkers (juce::Graphics& g);
    void drawChordStrip (juce::Graphics& g);

    /** 旗の当たり判定用の矩形（アレンジ画面と同じ形）。 */
    juce::Rectangle<int> getMarkerFlagBounds (int markerIndex) const;
    int findMarkerAt (juce::Point<int> position) const;

    /** コード区間の矩形。無効な番号なら空の矩形。 */
    juce::Rectangle<int> getChordRegionBounds (int regionIndex) const;
    int findChordRegionAt (juce::Point<int> position) const;

    void showRulerMenu (const juce::MouseEvent& e);
    void showMarkerMenu (int markerIndex, juce::Point<int> screenPosition);
    void showChordRegionMenu (int regionIndex, juce::Point<int> screenPosition);

    /** 時刻をX座標へ（本体の変換を通す）。 */
    int timeToX (double timelineSeconds) const;
    double xToTime (int x) const;

    ProjectModel& project;
    PianoRollComponent& pianoRoll;

    double playheadSeconds = 0.0;

    // 角に置くズームのボタン。**ツールバーの段は増やさない**（Phase 32で4段を2段に
    // 畳んだ経緯がある。狭いエディタパネルでは、段が増えるとノートグリッドが潰れる）。
    juce::TextButton zoomOutButton { "-" };
    juce::TextButton zoomInButton  { "+" };
    juce::TextButton zoomFitButton { "Fit" };

    //==========================================================================
    // 掴んでいる最中の状態（アレンジ画面と同じ持ち方）

    enum class DragTarget { none, seek, loopStart, loopEnd, loopCreate, marker,
                            chordMove, chordTrimLeft, chordTrimRight };

    DragTarget dragTarget = DragTarget::none;

    double loopDragAnchorTime = 0.0;
    double loopPreviewStart = 0.0;
    double loopPreviewEnd = 0.0;

    int markerDragIndex = -1;
    double markerDragPreviewTime = 0.0;

    /** 掴んでいるコード区間。**ValueTreeで覚える**（番号だと増減でずれる。1.32）。 */
    juce::ValueTree draggedChordRegion;
    double chordDragOriginalStart = 0.0;
    double chordDragOriginalLength = 0.0;
    double chordPreviewStart = 0.0;
    double chordPreviewLength = 0.0;
    juce::Point<int> dragStartPosition;

    static constexpr int edgeGrabMargin = 5;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollHeaderComponent)
};
