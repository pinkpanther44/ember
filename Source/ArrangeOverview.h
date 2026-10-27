#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ProjectModel.h"
#include "TimelineComponent.h"

/**
    8.350：**オーバービュー**——曲全体の縮図（Phase 339／本人の要望）。

    > 「トラック全体をざっと確認できるオーバービューを設置することできる？」

    8.359：**作りを改めた**（Phase 348／本人の要望）。

    > 「オーバービューに横スクロールバーと縦スクロールバーを配置する」
    > 「ズーム機能は廃止。常にArrangeの全体縮図を固定表示（FITを押した時の状態で固定）。枠も要らなくなると思う」
    > 「オーバービュー上をクリックでカーソルを移動できる」
    > 「オーバービューエリア左側が空白の状態であるが、この空白エリアにもオーバービューを広げよう」

    | | |
    |---|---|
    | 置き場所 | アレンジ画面の上端（ルーラーの上）。ツールバーのボタンで出し入れ、状態は覚える |
    | 描くもの | **曲全体（Fit と同じ長さ）を、左端（ヘッダーの上）から右のスクロールバーの手前まで**。トラックごとの行にクリップとノートの塊をトラックの色で。マーカー（旗と線）・ループ範囲・再生位置も。**枠は描かない** |
    | 横・縦のスクロールバー | **アレンジ画面を動かす**（アレンジ画面のスクロールバーと同じ範囲・同じ位置。どちらを動かしても両方が揃う） |
    | クリック・ドラッグ | **再生位置を動かす**（ルーラーと同じくスナップに従う） |

    ### 作り

    - 行の並びと高さは**タイムラインから引く**（`TimelineComponent::getOverviewRows()`）。自分で数え直さない
    - 中身の絵（行・クリップ・ノート・ループ）は**曲が変わったときだけ描き直して取っておく**。
      マーカーと再生位置は毎回その上に重ねる。アレンジ画面のスクロールやズームでは描き直さない（8.352）
    - 曲の変化はプロジェクトの木を購読して知る。**読み込みで木が差し替わる**ので、毎回見比べて付け替える（1.15）
*/
class ArrangeOverview : public juce::Component,
                        private juce::Timer,
                        private juce::ValueTree::Listener,
                        private juce::ScrollBar::Listener
{
public:
    ArrangeOverview (ProjectModel& projectToUse, TimelineComponent& timelineToUse);
    ~ArrangeOverview() override;

    /** 縮図 48px ＋ 下の横スクロールバー。 */
    static constexpr int scrollBarThickness = 10;
    static constexpr int preferredHeight = 48 + scrollBarThickness;

    /** 8.357：マーカーの旗の高さと、名前の文字の大きさ（アレンジ画面のルーラーの旗と同じ 11px）。 */
    static constexpr float markerFlagHeight = 11.0f;
    static constexpr float markerFontHeight = 9.5f;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /** 8.359：押した所・引いた所へ再生位置を動かす。 */
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;

    /** 中身の絵を描き直す（テーマが変わったときなど）。 */
    void invalidateContent();

    /** 縮図を描く場所。**左端から**、右の縦スクロールバーと下の横スクロールバーの手前まで（8.359）。 */
    juce::Rectangle<int> getMapArea() const
    {
        return { 0, 0, juce::jmax (0, getWidth() - scrollBarThickness), juce::jmax (0, getHeight() - scrollBarThickness) };
    }

    /** 試験用：いまの換算。 */
    ArrangeOverviewMap getMapForTesting() const { return currentMap(); }

    juce::ScrollBar& getHorizontalScrollBarForTesting() { return horizontalScrollBar; }
    juce::ScrollBar& getVerticalScrollBarForTesting()   { return verticalScrollBar; }

    /** 試験用：見張りを1回ぶん回す（スクロールバーをアレンジ画面に合わせる）。 */
    void tickForTesting() { timerCallback(); }

    /** 試験用：中身の絵を描き直した回数（スクロールで増えないこと。8.352）。 */
    int getRenderCountForTesting() const noexcept { return renderCount; }
    int getSongEndCountForTesting() const noexcept { return songEndCount; }

private:
    void timerCallback() override;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markSongChanged(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override               { markSongChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override        { markSongChanged(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override                { markSongChanged(); }

    ArrangeOverviewMap currentMap() const
    {
        const auto area = getMapArea();
        return timeline.getOverviewMap (area.getWidth(), area.getHeight(), getSongEnd());   // 8.352：覚えておいた端
    }

    void updateSubscription();

    // 8.359：スクロールバーはアレンジ画面を動かす
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void syncScrollBars();

    /** 押した x の時刻へ再生位置を動かす。 */
    void seekToX (float x);

    void renderContent (const ArrangeOverviewMap& map);

    /** 8.357：マーカーを旗（上端・名前）と縦線で描く。`local`は縮図の座標。 */
    void drawMarkers (juce::Graphics& g, const ArrangeOverviewMap& map, juce::Rectangle<float> local) const;

    /** 中身の絵の描き直しが要るかを決める値（大きさ・全体の長さ・行の並び）。 */
    juce::String contentSignature (const ArrangeOverviewMap& map) const;

    ProjectModel& project;
    TimelineComponent& timeline;

    juce::ValueTree subscribedState;
    bool contentDirty = true;

    juce::Image contentImage;
    juce::String lastContentSignature;
    double lastPlayheadSeconds = -1.0;

    juce::ScrollBar horizontalScrollBar { false };
    juce::ScrollBar verticalScrollBar { true };

    // 8.352：**曲の中身の端は、曲が変わったときだけ数え直す**（全ノートを見るので重い。Phase 341）
    mutable double cachedSongEnd = -1.0;
    mutable bool songEndDirty = true;
    double getSongEnd() const
    {
        if (songEndDirty || cachedSongEnd < 0.0)
        {
            cachedSongEnd = timeline.getSongEndSecondsForOverview();
            songEndDirty = false;
            ++songEndCount;
        }

        return cachedSongEnd;
    }

    void markSongChanged() { contentDirty = true; songEndDirty = true; }

    int renderCount = 0;
    mutable int songEndCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeOverview)
};
