#include "ArrangeOverview.h"

#include "AppColours.h"

#include <algorithm>

ArrangeOverview::ArrangeOverview (ProjectModel& projectToUse, TimelineComponent& timelineToUse)
    : project (projectToUse), timeline (timelineToUse)
{
    setOpaque (true);
    updateSubscription();

    // 8.359：横・縦のスクロールバー（Phase 348）。**アレンジ画面を動かす**
    for (auto* bar : { &horizontalScrollBar, &verticalScrollBar })
    {
        bar->setAutoHide (false);
        bar->addListener (this);
        addAndMakeVisible (*bar);
    }

    setMouseCursor (juce::MouseCursor::PointingHandCursor);   // 押すと再生位置が動く
}

ArrangeOverview::~ArrangeOverview()
{
    if (subscribedState.isValid())
        subscribedState.removeListener (this);
}

void ArrangeOverview::updateSubscription()
{
    // **読み込みでルートの木が差し替わる**ので、毎回見比べて付け替える（1.15）
    auto state = project.getState();

    if (state == subscribedState)
        return;

    if (subscribedState.isValid())
        subscribedState.removeListener (this);

    subscribedState = state;
    subscribedState.addListener (this);
    markSongChanged();
}

void ArrangeOverview::invalidateContent()
{
    contentDirty = true;
    repaint();
}

void ArrangeOverview::resized()
{
    contentDirty = true;

    const auto area = getMapArea();
    horizontalScrollBar.setBounds (area.getX(), area.getBottom(), area.getWidth(), getHeight() - area.getBottom());
    verticalScrollBar.setBounds (area.getRight(), 0, getWidth() - area.getRight(), area.getBottom());
    syncScrollBars();
}

void ArrangeOverview::visibilityChanged()
{
    // 出ていないあいだは見張らない（再生中ずっと回り続けないように）
    if (isVisible())
    {
        contentDirty = true;
        startTimerHz (20);
    }
    else
    {
        stopTimer();
    }
}

//==============================================================================
// 8.359：スクロールバーは**アレンジ画面のスクロールバーと同じ範囲・同じ位置**を持つ

void ArrangeOverview::syncScrollBars()
{
    const auto s = timeline.getScrollStateForOverview();

    horizontalScrollBar.setRangeLimits ({ 0.0, juce::jmax (s.totalSeconds, s.visibleSeconds) }, juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange ({ s.startSeconds, s.startSeconds + s.visibleSeconds }, juce::dontSendNotification);

    verticalScrollBar.setRangeLimits ({ 0.0, (double) juce::jmax (s.contentHeight, s.visibleHeight) }, juce::dontSendNotification);
    verticalScrollBar.setCurrentRange ({ (double) s.scrollY, (double) (s.scrollY + s.visibleHeight) }, juce::dontSendNotification);
}

void ArrangeOverview::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    const auto s = timeline.getScrollStateForOverview();

    if (bar == &horizontalScrollBar)
        timeline.setViewFromOverview (newRangeStart, (double) s.scrollY);
    else
        timeline.setViewFromOverview (s.startSeconds, newRangeStart);

    syncScrollBars();   // はみ出しはタイムラインが丸めるので、丸めたあとの位置へ戻す
}

//==============================================================================

juce::String ArrangeOverview::contentSignature (const ArrangeOverviewMap& map) const
{
    // 大きさ・全体の長さと高さ・行の並び・テーマ。**どれかが変われば中身の絵を描き直す**
    juce::String s;
    s << map.width << "x" << map.height << "|" << map.totalSeconds << "|" << map.contentHeight
      << "|" << AppColours::canvas.toString();

    for (const auto& row : timeline.getOverviewRows())
        s << "|" << row.trackIndex << ":" << row.top << ":" << row.height;

    return s;
}

void ArrangeOverview::timerCallback()
{
    updateSubscription();
    syncScrollBars();

    const auto map = currentMap();

    if (contentSignature (map) != lastContentSignature)
        contentDirty = true;

    const double playhead = timeline.getPlayheadSecondsForOverview();

    if (contentDirty || playhead != lastPlayheadSeconds)
        repaint();
}

void ArrangeOverview::renderContent (const ArrangeOverviewMap& map)
{
    ++renderCount;   // 8.352：自己検査用（スクロールで増えないこと）

    const int w = juce::jmax (1, map.width);
    const int h = juce::jmax (1, map.height);

    contentImage = juce::Image (juce::Image::RGB, w, h, true);
    juce::Graphics g (contentImage);

    g.fillAll (AppColours::canvas);

    // ループ範囲（アレンジ画面と同じオレンジを薄く）
    if (project.isLoopEnabled() && project.getLoopEndTime() > project.getLoopStartTime())
    {
        const float x1 = map.timeToX (project.getLoopStartTime());
        const float x2 = map.timeToX (project.getLoopEndTime());
        g.setColour (AppColours::orange.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, 0.0f, x2, (float) h));
    }

    // 8.360：パンチの範囲（入っているときだけ。赤を薄く、両端に線。Phase 349）
    if (project.isPunchEnabled() && project.hasPunchRange())
    {
        const float x1 = map.timeToX (project.getPunchStartTime());
        const float x2 = map.timeToX (project.getPunchEndTime());
        g.setColour (AppColours::punch().withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, 0.0f, x2, (float) h));
        g.setColour (AppColours::punch().withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (std::floor (x1), 0.0f, 1.0f, (float) h));
        g.fillRect (juce::Rectangle<float> (std::floor (x2), 0.0f, 1.0f, (float) h));
    }

    // トラックごとの行。**行の位置と高さはタイムラインのまま、全部を高さいっぱいに縮める**（8.359）
    const double gapSeconds = project.getNoteBlockGapSeconds();

    for (const auto& row : timeline.getOverviewRows())
    {
        auto track = project.getTrack (row.trackIndex);

        const float top = map.contentYToY (row.top);
        const float areaBottom = map.contentYToY (row.top + row.trackAreaHeight);

        // 行が細すぎても1px は残す（多いトラックでも「何かある」は見える）。上下に少し隙間
        const float rowHeight = juce::jmax (1.0f, areaBottom - top);
        const float inset = rowHeight >= 4.0f ? 1.0f : 0.0f;
        const float y = top + inset;
        const float barHeight = juce::jmax (1.0f, rowHeight - 2.0f * inset);

        const auto colour = juce::Colour::fromString (track.getColourString());
        g.setColour (colour.withAlpha (0.85f));

        auto fillSpan = [&] (double start, double end)
        {
            const float x1 = map.timeToX (start);
            const float x2 = juce::jmax (x1 + 1.0f, map.timeToX (end));   // 短くても1px
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x1, y, x2, y + barHeight));
        };

        switch (track.getType())
        {
            case TrackType::Midi:
                for (const auto& block : track.getNoteBlocks (gapSeconds))
                    fillSpan (block.startTime, block.endTime);
                break;

            case TrackType::Audio:
                for (int c = 0; c < track.getNumClips(); ++c)
                {
                    auto clip = track.getClip (c);
                    fillSpan (clip.getStartTime(), clip.getStartTime() + clip.getLength());
                }
                break;

            default:
                // フォルダ・センド・VCA などは中身を持たない。行の場所だけ薄く
                g.setColour (colour.withAlpha (0.15f));
                g.fillRect (juce::Rectangle<float> (0.0f, y, (float) w, barHeight));
                break;
        }
    }

    // マーカーは`paint()`で上に重ねる（8.357）

    contentDirty = false;
    lastContentSignature = contentSignature (map);
}

void ArrangeOverview::drawMarkers (juce::Graphics& g, const ArrangeOverviewMap& map, juce::Rectangle<float> local) const
{
    // 8.357：**マーカーを旗と線で**（Phase 346／本人の要望「オーバービューにマーカーを表示させることできる？」）。
    // アレンジ画面のルーラーの旗と同じ形：上端に紫の旗（名前は白）、そこから下へ縦線。
    // 旗の幅は名前の幅まで、ただし**次のマーカーの手前まで**（重ねると読めない）。狭ければ名前を省いて小さな旗だけ
    struct Item { float x; juce::String name; };
    std::vector<Item> items;

    for (int i = 0; i < project.getNumMarkers(); ++i)
    {
        const auto marker = project.getMarker (i);
        items.push_back ({ std::floor (map.timeToX (marker.getTime())), marker.getName() });   // 画素に揃える（線がにじまない）
    }

    std::sort (items.begin(), items.end(), [] (const Item& a, const Item& b) { return a.x < b.x; });

    const juce::Font font { juce::FontOptions { markerFontHeight } };

    for (size_t i = 0; i < items.size(); ++i)
    {
        const float x = items[i].x;

        if (x < local.getX() - 1.0f || x > local.getRight())
            continue;

        const float next = i + 1 < items.size() ? items[i + 1].x : local.getRight();
        const float room = juce::jmax (0.0f, next - x - 1.0f);

        g.setColour (AppColours::purple.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, 2.0f, local.getHeight()));   // 線

        const float textWidth = juce::GlyphArrangement::getStringWidth (font, items[i].name) + 8.0f;
        const float flagWidth = room >= 20.0f ? juce::jmin (textWidth, room) : juce::jmin (6.0f, juce::jmax (2.0f, room));
        const auto flag = juce::Rectangle<float> (x, 0.0f, flagWidth, markerFlagHeight);

        g.fillRect (flag);   // 旗

        if (room >= 20.0f && items[i].name.isNotEmpty())
        {
            g.setColour (juce::Colours::white);
            g.setFont (font);
            g.drawText (items[i].name, flag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
}

void ArrangeOverview::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);

    const auto area = getMapArea();

    if (area.isEmpty())
        return;

    const auto map = currentMap();

    if (contentDirty || ! contentImage.isValid() || contentSignature (map) != lastContentSignature)
        renderContent (map);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    g.setOrigin (area.getPosition());

    g.drawImageAt (contentImage, 0, 0);

    const auto local = juce::Rectangle<float> (0.0f, 0.0f, (float) area.getWidth(), (float) area.getHeight());

    drawMarkers (g, map, local);

    // 再生位置（アレンジ画面と同じオレンジ）。曲の端より先にいるときは右端に
    lastPlayheadSeconds = timeline.getPlayheadSecondsForOverview();
    const float playheadX = juce::jmin (local.getRight() - 1.0f, map.timeToX (lastPlayheadSeconds));
    g.setColour (AppColours::orange);
    g.fillRect (juce::Rectangle<float> (std::floor (playheadX), 0.0f, 2.0f, local.getHeight()));

    // 外枠（区画の境目。角は四角）
    g.setColour (AppColours::border);
    g.drawRect (local, 1.0f);
}

//==============================================================================
// 8.359：**クリック・ドラッグで再生位置を動かす**（Phase 348／本人の要望「オーバービュー上をクリックでカーソルを移動できる」）

void ArrangeOverview::seekToX (float x)
{
    const auto map = currentMap();

    if (map.width <= 0)
        return;

    const float clamped = juce::jlimit (0.0f, (float) map.width, x - (float) getMapArea().getX());
    timeline.seekFromOverview (map.xToTime (clamped));
    repaint();
}

void ArrangeOverview::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! getMapArea().contains (e.getPosition()))
        return;

    seekToX (e.position.x);
}

void ArrangeOverview::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! getMapArea().contains (e.getMouseDownPosition()))
        return;

    seekToX (e.position.x);
}
