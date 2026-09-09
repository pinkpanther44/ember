#include "PianoRollHeaderComponent.h"
#include "AppColours.h"
#include "Utf8.h"
#include <cmath>

PianoRollHeaderComponent::PianoRollHeaderComponent (ProjectModel& projectToUse,
                                                     PianoRollComponent& pianoRollToUse)
    : project (projectToUse), pianoRoll (pianoRollToUse)
{
    zoomOutButton.onClick = [this] { pianoRoll.zoomOut(); };
    zoomInButton.onClick  = [this] { pianoRoll.zoomIn(); };
    zoomFitButton.onClick = [this] { pianoRoll.zoomToFit(); };

    zoomOutButton.setTooltip (utf8 ("縮小（W／Ctrl+ホイール）"));
    zoomInButton.setTooltip (utf8 ("拡大（E／Ctrl+ホイール）"));
    zoomFitButton.setTooltip (utf8 ("クリップ全体が入る倍率にする（Alt+Z）"));

    addAndMakeVisible (zoomOutButton);
    addAndMakeVisible (zoomInButton);
    addAndMakeVisible (zoomFitButton);
}

//==============================================================================
// 座標。**変換は本体（PianoRollComponent）を通す**（8.28）
//==============================================================================

int PianoRollHeaderComponent::timeToX (double timelineSeconds) const
{
    return pianoRoll.timelineTimeToX (timelineSeconds);
}

double PianoRollHeaderComponent::xToTime (int x) const
{
    // Phase 126で本体のX座標がタイムライン基準になったので、そのまま返せる
    return pianoRoll.xToTimelineTime (x);
}

void PianoRollHeaderComponent::mouseWheelMove (const juce::MouseEvent& e,
                                                const juce::MouseWheelDetails& wheel)
{
    // 8.123：**ルーラーの上ではホイールだけで拡大縮小**（Phase 158／改善案20）。
    // アレンジ画面と同じ扱い（`TimelineComponent::mouseWheelMove`にも同じ話がある）。
    //
    // **Shift＋ホイールは横スクロールのまま**にすること。
    // 画面によって修飾キーの意味が変わるのがいちばん覚えにくい。
    //
    // ここは**コード帯も含めた帯ぜんぶ**が対象です。見た目にひと続きの帯なので、
    // 「ルーラーでは効くのに、そのすぐ下では何も起きない」を作らないため。
    //
    // X座標は本体と同じ原点（どちらも左端に鍵盤のぶんを空けている）なので、
    // そのまま拡大の中心として渡せる
    if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        const float delta = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
        pianoRoll.setScrollStartSeconds (pianoRoll.getScrollStartSeconds()
                                          - delta * pianoRoll.getVisibleSeconds() * 0.25);
        return;
    }

    // **刻みは本体が持っている**（ボタンと同じ量になる）。ここで数を書かないこと
    if (wheel.deltaY > 0.0f)
        pianoRoll.zoomIn (e.x);
    else
        pianoRoll.zoomOut (e.x);
}

juce::Rectangle<int> PianoRollHeaderComponent::getTimeArea() const

{
    // **左端は必ず鍵盤と同じ幅だけ空ける。** ここがずれると、目盛りが
    // グリッドより左（または右）へ寄る
    return getLocalBounds().withTrimmedLeft (PianoRollComponent::keyboardWidth);
}

juce::Rectangle<int> PianoRollHeaderComponent::getLoopStripArea() const
{
    return getTimeArea().withHeight (loopStripHeight);
}

juce::Rectangle<int> PianoRollHeaderComponent::getMarkerStripArea() const
{
    return getTimeArea().withTop (loopStripHeight).withHeight (markerStripHeight);
}

juce::Rectangle<int> PianoRollHeaderComponent::getChordStripArea() const
{
    return getTimeArea().withTop (rulerHeight).withHeight (chordStripHeight);
}

void PianoRollHeaderComponent::resized()
{
    auto corner = getLocalBounds().removeFromLeft (PianoRollComponent::keyboardWidth);

    auto zoomRow = corner.removeFromTop (rulerHeight);
    zoomOutButton.setBounds (zoomRow.removeFromLeft (zoomRow.getWidth() / 2).reduced (1));
    zoomInButton.setBounds (zoomRow.reduced (1));

    zoomFitButton.setBounds (corner.reduced (1));
}

void PianoRollHeaderComponent::setPlayheadSeconds (double timelineSeconds)
{
    if (playheadSeconds == timelineSeconds)
        return;

    // **X座標が変わらないなら描き直さない**（再生中は毎フレーム呼ばれる。8.33）
    const int oldX = timeToX (playheadSeconds);
    playheadSeconds = timelineSeconds;

    if (timeToX (playheadSeconds) != oldX)
        repaint();
}

//==============================================================================
// 描画
//==============================================================================

void PianoRollHeaderComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::panel);

    drawRuler (g);
    drawLoopRange (g);
    drawMarkers (g);
    drawChordStrip (g);

    // 仕様書5.9：再生カーソル（Phase 72）。**ルーラーの上にも引く**
    // （本体側の線はこの帯まで届かない。8.20の固定行と同じ話）
    {
        const int playheadX = timeToX (playheadSeconds);

        if (playheadX >= PianoRollComponent::keyboardWidth && playheadX < getWidth())
        {
            g.setColour (AppColours::orange);
            g.fillRect (playheadX - 1, rulerContentTop, 3, getHeight() - rulerContentTop);
        }
    }

    // 下端の線。ここから下がノートグリッドであることの区切り
    g.setColour (AppColours::border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void PianoRollHeaderComponent::drawRuler (juce::Graphics& g)
{
    auto area = getTimeArea().withTop (rulerContentTop).withBottom (rulerHeight);

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);

    // 仕様書5.9：小節/拍表示。**長さはProjectModelに訊く**（8.98／Phase 138）。
    // **ピアノロールは小節/拍だけ**にしてある（アレンジ画面のようなタイムコード表示は
    // 出さない）。打ち込みの最中に秒で位置を数えることはまず無い。
    //
    // 画面の左端に来ているタイムライン上の時刻から数え始める（グリッド線と同じ考え方）
    const double leftTimeline = pianoRoll.getScrollStartSeconds();
    const double secondsPerBeat = project.getBeatSecondsAt (leftTimeline);
    const double secondsPerBar = project.getBarSecondsAt (leftTimeline);
    const double pixelsPerSecond = pianoRoll.getPixelsPerSecond();
    const double pixelsPerBar = secondsPerBar * pixelsPerSecond;

    if (pixelsPerBar <= 0.0)
        return;

    // 縮小して小節が詰まってきたら、目盛りを間引く（数字が重ならないように）。
    // **間引きの基準はアレンジ画面と同じ44px**（同じ倍率なら同じ見え方になる）
    int barStep = 1;

    while (pixelsPerBar * barStep < 44.0)
        barStep *= 2;

    const bool showBeats = (secondsPerBeat * pixelsPerSecond) >= 10.0;

    const int firstBar = project.getBarIndexAt (leftTimeline);

    g.setFont (juce::FontOptions (10.0f));

    for (int bar = firstBar - (firstBar % barStep); ; bar += barStep)
    {
        // **`小節番号 × 小節の長さ` と書かないこと**（8.98／Phase 138）
        const double barStart = project.getBarStartTime (bar);
        const int x = timeToX (barStart);

        if (x > area.getRight())
            break;

        g.setColour (AppColours::textSecondary);
        g.drawLine ((float) x, (float) area.getY(), (float) x, (float) area.getBottom(), 1.0f);

        // **番号は曲の小節番号**（クリップの中身の何秒目かではない）。
        // アレンジ画面のルーラーと同じ数字が出ないと、行き来したときに迷う
        g.drawText (juce::String (bar + 1), x + 3, area.getY(), 46, area.getHeight(),
                     juce::Justification::centredLeft, false);

        // 拍の目盛りは、間引きしていないときだけ（間引き中は意味を持たないため）
        if (showBeats && barStep == 1)
        {
            for (int beat = 1; beat < project.getBeatsPerBarAt (barStart); ++beat)
            {
                const int beatX = timeToX (project.getBeatStartTime (bar, beat));

                g.setColour (AppColours::border);
                g.drawLine ((float) beatX, (float) area.getBottom() - 6.0f,
                             (float) beatX, (float) area.getBottom(), 1.0f);
            }
        }
    }
}

void PianoRollHeaderComponent::drawLoopRange (juce::Graphics& g)
{
    auto area = getLoopStripArea();

    g.setColour (AppColours::background);
    g.fillRect (area);

    const bool dragging = (dragTarget == DragTarget::loopCreate || dragTarget == DragTarget::loopStart
                            || dragTarget == DragTarget::loopEnd);

    const double startTime = dragging ? loopPreviewStart : project.getLoopStartTime();
    const double endTime   = dragging ? loopPreviewEnd   : project.getLoopEndTime();

    if (endTime <= startTime)
        return;

    const int startX = juce::jmax (area.getX(), timeToX (startTime));
    const int endX = juce::jmin (area.getRight(), timeToX (endTime));

    if (endX <= startX)
        return;

    // **有効・無効を濃さで表す**（アレンジ画面と同じ。範囲は残しつつ切れる）
    g.setColour (AppColours::orange.withAlpha (project.isLoopEnabled() ? 0.8f : 0.3f));
    g.fillRect (startX, area.getY(), endX - startX, area.getHeight());
}

void PianoRollHeaderComponent::drawMarkers (juce::Graphics& g)
{
    auto area = getMarkerStripArea();

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);

    g.setFont (juce::FontOptions (9.0f));

    for (int i = 0; i < project.getNumMarkers(); ++i)
    {
        auto bounds = getMarkerFlagBounds (i);

        if (bounds.getRight() <= area.getX() || bounds.getX() >= area.getRight())
            continue;

        const bool isDragged = (markerDragIndex == i);

        g.setColour (AppColours::purple.withAlpha (isDragged ? 0.9f : 0.7f));
        g.fillRect (bounds);

        g.setColour (AppColours::textPrimary);
        g.drawText (project.getMarker (i).getName(), bounds.reduced (3, 0),
                     juce::Justification::centredLeft, false);
    }
}

void PianoRollHeaderComponent::drawChordStrip (juce::Graphics& g)
{
    auto area = getChordStripArea();

    // 地を1段暗くして、ルーラーとは別の行であることを示す（8.26の「歯抜けのグリッドには
    // 地が要る」と同じ理由。コードが入っていない場所が続くと、行の存在が読めなくなる）
    g.setColour (AppColours::canvasAlt);
    g.fillRect (area);

    g.setColour (AppColours::border);
    g.drawHorizontalLine (rulerHeight, 0.0f, (float) getWidth());

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);

    auto chordTrack = project.findChordTrack();

    if (! chordTrack.state.getParent().isValid() || chordTrack.getNumChordRegions() == 0)
    {
        // **空の帯を黙って出さない。** 何のための行か分からないまま20px取られる
        g.setColour (AppColours::textSecondary.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (utf8 ("Chord（ダブルクリックでコード区間を追加）"),
                     area.reduced (6, 0), juce::Justification::centredLeft, false);
        return;
    }

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
    {
        auto bounds = getChordRegionBounds (r);

        // 画面の外にある区間は飛ばす（進行が長いとここが効いてくる）
        if (bounds.getRight() <= area.getX() || bounds.getX() >= area.getRight())
            continue;

        // 8.129：**アレンジ画面と同じく旗で表す**（Phase 165／本人の要望）。
        //
        // Phase 164でアレンジ画面のコード区間を旗にしたので、ここも揃えました。
        // **同じものが画面によって違う形で出る**のがいちばん分かりにくいためです（1.27）。
        //
        // 区間ぜんぶを薄く塗り、始まりに縦線、その上に名前の札を置く——
        // アレンジ画面の`drawChordRegion()`と同じ組み立てです
        g.setColour (AppColours::purple.withAlpha (0.12f));
        g.fillRect (bounds);

        g.setColour (AppColours::purple);
        g.fillRect (bounds.getX(), bounds.getY(), 2, bounds.getHeight());

        const auto name = chordTrack.getChordRegion (r).getChord().getName();

        const juce::Font font (juce::FontOptions (11.0f, juce::Font::bold));
        const int labelWidth = juce::GlyphArrangement::getStringWidthInt (font, name) + 10;

        // **区間からはみ出す札は描かない。** はみ出すと隣の区間の上に乗り、
        // どちらのコードなのか読めなくなります（アレンジ画面と同じ判断）
        if (labelWidth > bounds.getWidth())
            continue;

        const auto label = juce::Rectangle<int> (bounds.getX(), bounds.getY(),
                                                  labelWidth, bounds.getHeight());

        g.setColour (AppColours::purple);
        g.fillRect (label);

        // 札の地はパープルで塗り切ってあるので、文字は白で固定。
        // **`textPrimary`にしないこと**——ライトテーマで紫の上に沈みます（1.43）
        g.setColour (juce::Colours::white);
        g.setFont (font);
        g.drawText (name, label.reduced (5, 0), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
// 当たり判定
//==============================================================================

juce::Rectangle<int> PianoRollHeaderComponent::getMarkerFlagBounds (int markerIndex) const
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return {};

    const double time = (markerDragIndex == markerIndex) ? markerDragPreviewTime
                                                          : project.getMarker (markerIndex).getTime();
    const int x = timeToX (time);

    // **旗は右へ伸びる**（時刻はいちばん左）。幅は名前が読める程度で固定
    return { x, loopStripHeight, 62, markerStripHeight };
}

int PianoRollHeaderComponent::findMarkerAt (juce::Point<int> position) const
{
    // **後ろから見る**（重なっているときは、後から置いた旗のほうが上に描かれている）
    for (int i = project.getNumMarkers(); --i >= 0;)
        if (getMarkerFlagBounds (i).contains (position))
            return i;

    return -1;
}

juce::Rectangle<int> PianoRollHeaderComponent::getChordRegionBounds (int regionIndex) const
{
    auto chordTrack = project.findChordTrack();

    if (! chordTrack.state.getParent().isValid()
         || ! juce::isPositiveAndBelow (regionIndex, chordTrack.getNumChordRegions()))
        return {};

    auto region = chordTrack.getChordRegion (regionIndex);

    const bool isDragged = (draggedChordRegion.isValid() && region.state == draggedChordRegion);
    const double startTime = isDragged ? chordPreviewStart : region.getStartTime();
    const double length = isDragged ? chordPreviewLength : region.getLength();

    const int startX = timeToX (startTime);
    const int endX = timeToX (startTime + length);
    auto area = getChordStripArea();

    return { startX, area.getY() + 1, juce::jmax (4, endX - startX), area.getHeight() - 2 };
}

int PianoRollHeaderComponent::findChordRegionAt (juce::Point<int> position) const
{
    auto chordTrack = project.findChordTrack();

    if (! chordTrack.state.getParent().isValid())
        return -1;

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
        if (getChordRegionBounds (r).contains (position))
            return r;

    return -1;
}

//==============================================================================
// 操作（8.29の表。アレンジ画面と同じ割り当て）
//==============================================================================

void PianoRollHeaderComponent::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < PianoRollComponent::keyboardWidth)
        return;   // 角（ズームのボタン）は子コンポーネントが受ける

    // **判定の順番はアレンジ画面と同じ**（8.29）。
    // ループの帯とマーカーの旗は、シークより先に見ること
    if (e.mods.isPopupMenu() && e.y < rulerHeight && findMarkerAt (e.getPosition()) < 0)
    {
        showRulerMenu (e);
        return;
    }

    // 仕様書5.9：ループ範囲の帯
    if (getLoopStripArea().contains (e.getPosition()))
    {
        const double startTime = project.getLoopStartTime();
        const double endTime = project.getLoopEndTime();
        const bool hasRange = (endTime > startTime);

        loopPreviewStart = startTime;
        loopPreviewEnd = endTime;

        if (hasRange && std::abs (e.x - timeToX (startTime)) <= edgeGrabMargin)
        {
            dragTarget = DragTarget::loopStart;
        }
        else if (hasRange && std::abs (e.x - timeToX (endTime)) <= edgeGrabMargin)
        {
            dragTarget = DragTarget::loopEnd;
        }
        else
        {
            dragTarget = DragTarget::loopCreate;

            // **掴んだ側の端も寄せること**（8.14）
            loopDragAnchorTime = project.snapTime (xToTime (e.x));
            loopPreviewStart = loopDragAnchorTime;
            loopPreviewEnd = loopDragAnchorTime;
        }

        repaint();
        return;
    }

    // 仕様書5.9：マーカーの旗
    {
        const int markerIndex = findMarkerAt (e.getPosition());

        if (markerIndex >= 0)
        {
            if (e.mods.isPopupMenu())
            {
                showMarkerMenu (markerIndex, e.getScreenPosition());
                return;
            }

            dragTarget = DragTarget::marker;
            markerDragIndex = markerIndex;
            markerDragPreviewTime = project.getMarker (markerIndex).getTime();

            // **マーカーの時刻そのものへ動かす**（画面の座標を経由すると、
            // スナップが効いて旗と再生位置がずれる。8.14）
            if (onSeek != nullptr)
                onSeek (markerDragPreviewTime);

            repaint();
            return;
        }
    }

    // 8.29の表：コード帯（Phase 72）
    if (getChordStripArea().contains (e.getPosition()))
    {
        const int regionIndex = findChordRegionAt (e.getPosition());

        if (regionIndex < 0)
            return;   // 空いている場所はダブルクリックで追加（`mouseDoubleClick`）

        auto chordTrack = project.findChordTrack();
        auto region = chordTrack.getChordRegion (regionIndex);

        if (e.mods.isPopupMenu())
        {
            showChordRegionMenu (regionIndex, e.getScreenPosition());
            return;
        }

        auto bounds = getChordRegionBounds (regionIndex);

        if (e.x <= bounds.getX() + edgeGrabMargin)
            dragTarget = DragTarget::chordTrimLeft;
        else if (e.x >= bounds.getRight() - edgeGrabMargin)
            dragTarget = DragTarget::chordTrimRight;
        else
            dragTarget = DragTarget::chordMove;

        draggedChordRegion = region.state;
        chordDragOriginalStart = region.getStartTime();
        chordDragOriginalLength = region.getLength();
        chordPreviewStart = chordDragOriginalStart;
        chordPreviewLength = chordDragOriginalLength;
        dragStartPosition = e.getPosition();

        repaint();
        return;
    }

    // 仕様書5.9：ルーラーのクリックで再生位置を決める
    if (e.y < rulerHeight && ! e.mods.isPopupMenu())
    {
        dragTarget = DragTarget::seek;

        if (onSeek != nullptr)
            onSeek (project.snapTime (xToTime (e.x)));
    }
}

void PianoRollHeaderComponent::mouseDrag (const juce::MouseEvent& e)
{
    const double snapped = project.snapTime (xToTime (e.x));

    switch (dragTarget)
    {
        case DragTarget::seek:
            if (onSeek != nullptr)
                onSeek (snapped);
            break;

        case DragTarget::loopCreate:
            loopPreviewStart = juce::jmin (loopDragAnchorTime, snapped);
            loopPreviewEnd   = juce::jmax (loopDragAnchorTime, snapped);
            repaint();
            break;

        case DragTarget::loopStart:
        case DragTarget::loopEnd:
        {
            // **最短の長さは目盛り1つぶん**（0にすると範囲が消える。8.14）
            const double snapSeconds = project.getSnapSecondsAt (snapped);
            const double minimumLength = (snapSeconds > 0.0) ? snapSeconds
                                                             : project.getBeatSecondsAt (snapped);

            if (dragTarget == DragTarget::loopStart)
                loopPreviewStart = juce::jmin (snapped, loopPreviewEnd - minimumLength);
            else
                loopPreviewEnd = juce::jmax (snapped, loopPreviewStart + minimumLength);

            repaint();
            break;
        }

        case DragTarget::marker:
            markerDragPreviewTime = snapped;
            repaint();
            break;

        case DragTarget::chordMove:
        case DragTarget::chordTrimLeft:
        case DragTarget::chordTrimRight:
        {
            const double deltaSeconds = (double) (e.x - dragStartPosition.x)
                                          / juce::jmax (1.0, pianoRoll.getPixelsPerSecond());
            const double minimumLength = project.getBeatSecondsAt (chordDragOriginalStart);

            if (dragTarget == DragTarget::chordMove)
            {
                chordPreviewStart = juce::jmax (0.0, project.snapTime (chordDragOriginalStart + deltaSeconds));
            }
            else if (dragTarget == DragTarget::chordTrimLeft)
            {
                const double end = chordDragOriginalStart + chordDragOriginalLength;
                chordPreviewStart = juce::jlimit (0.0, end - minimumLength,
                                                   project.snapTime (chordDragOriginalStart + deltaSeconds));
                chordPreviewLength = end - chordPreviewStart;
            }
            else
            {
                const double end = project.snapTime (chordDragOriginalStart + chordDragOriginalLength + deltaSeconds);
                chordPreviewLength = juce::jmax (minimumLength, end - chordPreviewStart);
            }

            repaint();
            break;
        }

        default:
            break;
    }
}

void PianoRollHeaderComponent::mouseUp (const juce::MouseEvent&)
{
    // **離したときに1回だけモデルへ書く**（ドラッグ中に書くと履歴が積み上がる。3.1）
    switch (dragTarget)
    {
        case DragTarget::loopCreate:
        case DragTarget::loopStart:
        case DragTarget::loopEnd:
            if (loopPreviewEnd > loopPreviewStart)
            {
                project.beginAction (utf8 ("ループ範囲の変更"));
                project.setLoopRange (loopPreviewStart, loopPreviewEnd, &project.getUndoManager());

                // **範囲を引いたらループを有効にする**（引いたのに何も変わらないと、
                // 引けていないのかループが切れているのか分からない）
                if (! project.isLoopEnabled())
                    project.setLoopEnabled (true, &project.getUndoManager());

                if (onLoopChanged != nullptr)
                    onLoopChanged();
            }
            break;

        case DragTarget::marker:
            if (juce::isPositiveAndBelow (markerDragIndex, project.getNumMarkers()))
            {
                project.beginAction (utf8 ("マーカーの移動"));
                project.getMarker (markerDragIndex).setTime (markerDragPreviewTime,
                                                              &project.getUndoManager());
            }
            break;

        case DragTarget::chordMove:
        case DragTarget::chordTrimLeft:
        case DragTarget::chordTrimRight:
            if (draggedChordRegion.isValid() && draggedChordRegion.getParent().isValid())
            {
                auto chordTrack = project.findChordTrack();

                for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
                {
                    auto region = chordTrack.getChordRegion (r);

                    if (region.state != draggedChordRegion)
                        continue;

                    project.beginAction (utf8 ("コード区間の編集"));

                    // **`setChordRegionTime()`を通すこと。** 時刻だけ書き換えると
                    // 並びが崩れ、「直前のコード」が別のコードになる（8.5）
                    chordTrack.setChordRegionTime (region, chordPreviewStart, chordPreviewLength,
                                                    &project.getUndoManager());
                    break;
                }
            }
            break;

        default:
            break;
    }

    dragTarget = DragTarget::none;
    markerDragIndex = -1;
    draggedChordRegion = {};
    repaint();
}

void PianoRollHeaderComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.x < PianoRollComponent::keyboardWidth)
        return;

    // 8.29の表：ループの帯 → ループの有効/無効（アレンジ画面と同じ）
    if (getLoopStripArea().contains (e.getPosition()))
    {
        dragTarget = DragTarget::none;   // 1回目で始まった引き直しを畳む

        project.beginAction (utf8 ("ループの入切"));
        project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());

        if (onLoopChanged != nullptr)
            onLoopChanged();

        repaint();
        return;
    }

    // 8.29の表：コード帯（Phase 72）。区間の上ならコードパッド、空いていれば追加
    if (getChordStripArea().contains (e.getPosition()))
    {
        dragTarget = DragTarget::none;
        draggedChordRegion = {};

        auto chordTrack = project.findChordTrack();

        if (! chordTrack.state.getParent().isValid())
            return;   // コードトラックが無ければ置き場所が無い

        const int regionIndex = findChordRegionAt (e.getPosition());

        if (regionIndex >= 0)
        {
            if (onChordRegionDoubleClicked != nullptr)
                onChordRegionDoubleClicked (chordTrack.getChordRegion (regionIndex).getStartTime());

            return;
        }

        // 空いている場所には1小節ぶんのコード区間を足す（アレンジ画面と同じ。8.5）。
        // **既にある区間と重ねない**：判定はモデル側の1箇所（Track）にある
        const double startTime = project.snapTimeDown (xToTime (e.x));
        const double barSeconds = project.getBarSecondsAt (startTime);

        if (chordTrack.hasOverlappingChordRegion (startTime, startTime + barSeconds))
            return;

        // 既定はキーのトニック（`TimelineComponent::addChordRegionAt()`と同じ）
        const auto key = project.getProjectKeyAt (startTime);
        Chord chord;
        chord.root = key.degreeRoot (0);
        chord.type = key.diatonicSeventh (0);

        project.beginAction (utf8 ("コード区間の追加"));
        chordTrack.addChordRegion (chord, startTime, barSeconds, &project.getUndoManager());
        repaint();
    }
}

//==============================================================================
// メニュー
//==============================================================================

void PianoRollHeaderComponent::showRulerMenu (const juce::MouseEvent& e)
{
    const double snappedTime = project.snapTime (xToTime (e.x));

    // **アレンジ画面のルーラーと同じ中身**（8.29）。片方だけ項目が違うと、
    // どちらで何ができるかを覚え直すことになる
    juce::PopupMenu menu;
    menu.addSectionHeader (utf8 ("マーカー"));
    menu.addItem (1, utf8 ("ここにマーカーを挿入"));
    menu.addItem (2, utf8 ("名前を付けてマーカーを挿入..."));
    menu.addSeparator();
    menu.addSectionHeader (utf8 ("ループ"));
    menu.addItem (3, utf8 ("ここをループの先頭にする"));
    menu.addItem (4, utf8 ("ここをループの終わりにする"));
    menu.addItem (5, utf8 ("ループを有効にする"), true, project.isLoopEnabled());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
        [this, snappedTime] (int result)
        {
            if (result <= 0)
                return;

            if (result == 1 || result == 2)
            {
                // **挿入はMainComponentへ返す**（名前を聞くダイアログを持っているのはあちら）
                if (onInsertMarkerRequested != nullptr)
                    onInsertMarkerRequested (snappedTime, result == 2);

                return;
            }

            if (result == 3)
            {
                project.beginAction (utf8 ("ループの先頭を設定"));
                project.setLoopRange (snappedTime, project.getLoopEndTime(), &project.getUndoManager());
                project.setLoopEnabled (true, &project.getUndoManager());
            }
            else if (result == 4)
            {
                project.beginAction (utf8 ("ループの終わりを設定"));
                project.setLoopRange (project.getLoopStartTime(), snappedTime, &project.getUndoManager());
                project.setLoopEnabled (true, &project.getUndoManager());
            }
            else
            {
                project.beginAction (utf8 ("ループの入切"));
                project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());
            }

            if (onLoopChanged != nullptr)
                onLoopChanged();

            repaint();
        });
}

void PianoRollHeaderComponent::showMarkerMenu (int markerIndex, juce::Point<int> screenPosition)
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader (project.getMarker (markerIndex).getName());
    menu.addItem (1, utf8 ("名前を変更..."));
    menu.addSeparator();
    menu.addItem (2, utf8 ("このマーカーを削除"));

    // **ValueTreeで掴んでおく**（メニューが閉じるまでに番号がずれる。1.32）
    const auto markerState = project.getMarker (markerIndex).state;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, markerState] (int result)
        {
            if (result <= 0 || ! markerState.getParent().isValid())
                return;

            Marker marker { markerState };

            if (result == 1)
            {
                NameEntry::show (utf8 ("マーカーの名前"), utf8 ("新しい名前を入力してください。"),
                                  marker.getName(),
                                  [this, markerState] (const juce::String& newName)
                                  {
                                      if (! markerState.getParent().isValid())
                                          return;

                                      project.beginAction (utf8 ("マーカーの名前の変更"));
                                      Marker (markerState).setName (newName, &project.getUndoManager());
                                      repaint();
                                  });
                return;
            }

            project.beginAction (utf8 ("マーカーの削除"));
            project.removeMarker (marker, &project.getUndoManager());
            repaint();
        });
}

void PianoRollHeaderComponent::showChordRegionMenu (int regionIndex, juce::Point<int> screenPosition)
{
    auto chordTrack = project.findChordTrack();

    if (! chordTrack.state.getParent().isValid()
         || ! juce::isPositiveAndBelow (regionIndex, chordTrack.getNumChordRegions()))
        return;

    // **ValueTreeで掴んでおく**（メニューが閉じるまでに番号がずれる。1.32）
    const auto regionState = chordTrack.getChordRegion (regionIndex).state;

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("このコードを削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, regionState] (int result)
        {
            if (result != 1 || ! regionState.getParent().isValid())
                return;

            auto track = project.findChordTrack();

            project.beginAction (utf8 ("コード区間の削除"));
            track.removeChordRegion (ChordRegion (regionState), &project.getUndoManager());
            repaint();
        });
}
