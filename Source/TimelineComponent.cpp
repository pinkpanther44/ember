#include "TimelineComponent.h"
#include "AppColours.h"
#include "EditClipboard.h"   // 仕様書6.2：カット／コピー／貼り付け（Phase 71）
#include "NameEntry.h"       // 名前を入れるダイアログ（Phase 72で1つにまとめた）
#include "DragAndDropIds.h"
#include "Utf8.h"
#include "AudioTransform.h"   // 8.149：伸縮の上下限（Phase 187／8.48）
#include <algorithm>   // 8.126：std::find（複数選択。Phase 162）
#include <cmath>  // std::pow（ズーム倍率の計算）
#include <limits> // 隣の区間が無いときの「上限なし」（コード区間のドラッグ）

namespace
{
    /** 8.128：コード区間の**旗（コード名の札）**の矩形（Phase 164／改善案13）。

        **描くのと当たり判定で同じものを使うこと。** 別々に計算すると、
        見えている札と押せる場所がずれます（1.27）。

        区間からはみ出すなら**空を返します**——はみ出した札は隣の区間の上に乗り、
        どちらのコードなのか読めなくなります（半端に切れた名前を出さない方針。Phase 70）。 */
    juce::Rectangle<int> chordFlagRect (juce::Rectangle<int> regionBounds, const juce::String& chordName)
    {
        const juce::Font font (juce::FontOptions (13.0f, juce::Font::bold));
        const int labelWidth = juce::GlyphArrangement::getStringWidthInt (font, chordName) + 12;
        const int labelHeight = juce::jmin (18, regionBounds.getHeight());

        if (labelWidth > regionBounds.getWidth())
            return {};

        return { regionBounds.getX(), regionBounds.getY(), labelWidth, labelHeight };
    }

    /** 音量・パンを持つ（＝オーディオを通す）トラック種別か。


        オートメーションのレーンやドロップの受け入れ判定で何度も使うので、
        条件をここ1箇所にまとめてある。フォルダ・コード・VCAには音量の時間変化が無い。 */
    bool trackTypeHasAudioPath (TrackType type)
    {
        // 8.51：**フォルダも音を通す**（Phase 90／D2）。中身の音をまとめてから
        // マスターへ送るので、フェーダー・パン・インサート・センド・
        // オートメーションがそのまま使えます
        return type == TrackType::Audio || type == TrackType::Midi
                || type == TrackType::Send || type == TrackType::Folder
                || type == TrackType::DrumOut;   // 8.143（Phase 181／改善案⑮）
    }

    /** 8.146：**録音待機（Rec）を持てる**トラック種別か（Phase 184／改善案⑬a）。

        Phase 183まではオーディオトラックだけでした。MIDIトラックにも
        録音待機の値自体はありましたが（`refreshMidiInputTargets()`が
        「アーム中のMIDIトラックへ鳴らす」規則を持っていた。8.83）、
        **ヘッダーにボタンが出ていなかったので、押しようがありません**でした。
        ショートカット（`toggleSelectedTrackFlag`）からだけ立てられる、
        目に見えない状態になっていた——**8.12の「入口が1つしか無い前提」の逆**で、
        入口が足りていなかった例です。 */
    bool trackTypeCanArm (TrackType type)
    {
        return type == TrackType::Audio || type == TrackType::Midi;
    }
}

TimelineComponent::TimelineComponent (ProjectModel& projectToUse, WaveformCache& cacheToUse,
                                        SelectionState& selectionToUse)
    : project (projectToUse), waveformCache (cacheToUse), selection (selectionToUse)
{
    setWantsKeyboardFocus (true);

    // 仕様書5.9：小節/拍表示 ↔ タイムコード表示の切り替え（Phase 18）
    timeFormatButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    timeFormatButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
    timeFormatButton.onClick = [this]
    {
        showBarsAndBeats = ! showBarsAndBeats;
        timeFormatButton.setButtonText (showBarsAndBeats ? "Bars" : "Time");
        repaint();
    };
    addAndMakeVisible (timeFormatButton);

    horizontalScrollBar.addListener (this);
    verticalScrollBar.addListener (this);
    addAndMakeVisible (horizontalScrollBar);
    addAndMakeVisible (verticalScrollBar);

    // 8.162：旗の上での名前の打ち直し（Phase 200／本人の要望）。
    // **`addChildComponent`（見せない）で足すこと**——出しっぱなしにすると、
    // 何も選んでいないのに入力欄がルーラーに貼り付いたままになる
    markerNameEditor.setMultiLine (false);
    markerNameEditor.setReturnKeyStartsNewLine (false);
    markerNameEditor.setFont (juce::FontOptions ((float) markerStripHeight - 2.0f));
    markerNameEditor.setBorder (juce::BorderSize<int> (1));
    markerNameEditor.setIndents (2, 0);
    markerNameEditor.setColour (juce::TextEditor::backgroundColourId, AppColours::purple.darker (0.5f));
    markerNameEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
    markerNameEditor.setColour (juce::TextEditor::highlightColourId, AppColours::orange.withAlpha (0.4f));
    markerNameEditor.setColour (juce::TextEditor::outlineColourId, AppColours::orange);
    markerNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, AppColours::orange);
    markerNameEditor.onReturnKey = [this] { commitMarkerNameEdit(); };
    markerNameEditor.onEscapeKey = [this] { cancelMarkerNameEdit(); };
    markerNameEditor.onFocusLost = [this] { commitMarkerNameEdit(); };
    addChildComponent (markerNameEditor);

    updateProjectSubscription();
}

TimelineComponent::~TimelineComponent()
{
    // 破棄後に通知が届かないよう、購読を外してから壊れる
    subscribedProjectState.removeListener (this);
}

void TimelineComponent::refresh()
{
    // 8.126：**消えたトラックのIDを落とす**（Phase 162／改善案35）。
    // 残すと、削除したトラックへ音量を書きに行くことになります（1.32）
    pruneTrackSelection();

    // 8.158：**時間範囲のほうも同じ**（Phase 196）。トラックを消したりUndoしたりすると、
    // もう無いIDが残ります。**1本も残らなくなったら範囲ごと畳む**
    // ——どの行にも掛かっていない範囲は、選んでいないのと同じです
    if (! timeRangeTrackIds.empty())
    {
        timeRangeTrackIds.erase (std::remove_if (timeRangeTrackIds.begin(), timeRangeTrackIds.end(),
                                                  [this] (const juce::String& id)
                                                  {
                                                      return ! project.findTrackById (id).state.getParent().isValid();
                                                  }),
                                  timeRangeTrackIds.end());

        if (timeRangeTrackIds.empty())
            clearTimeRange();
    }

    // プロジェクトを読み込むとルートのValueTreeが差し替わる。
    // refresh()はその後（ArrangeView::refreshAfterProjectChanged）から必ず呼ばれるので、
    // ここで購読を付け替える。
    updateProjectSubscription();

    // 8.59：**消えた／隠したレーンを選んだままにしない**（Phase 96）。
    //
    // 閉じる「x」・右クリックの削除／隠す・Undo……と経路がいくつもあるので、
    // **選択が生きているかを1箇所で見張る**形にしてある（それぞれの経路で
    // 消しにいくと、必ずどれか1つを忘れる。1.27）
    if (selectedLaneTargetId.isNotEmpty())
    {
        bool stillVisible = false;

        for (int r = 0; r <= getMasterRowIndex() && ! stillVisible; ++r)
            for (int i = 0, n = getNumAutomationRows (r); i < n; ++i)
                if (isAutomationRowSelected ({ r, i }))
                {
                    stillVisible = true;
                    break;
                }

        if (! stillVisible)
        {
            selectedLaneTrackId.clear();
            selectedLaneTargetId.clear();
            selection.selectNone();
        }
    }

    updateScrollBars(); // クリップが増減すると、スクロールできる範囲も変わる
    layoutHeaderControls(); // 仕様書5.7：トラックが増減するとメーターの数も変わる（Phase 58）
    repaint();
}

//==============================================================================
// 他の画面での編集に追従する（Phase 22a）
//==============================================================================

void TimelineComponent::updateProjectSubscription()
{
    auto currentState = project.getState();

    if (subscribedProjectState == currentState)
        return; // 既に今のツリーを見ている

    subscribedProjectState.removeListener (this);
    subscribedProjectState = currentState;
    subscribedProjectState.addListener (this);
}

void TimelineComponent::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    // スクロール範囲に影響するのは、クリップとコード区間の位置と長さだけ。
    // オートメーションの点のように毎秒何度も変わる値まで含めて計算し直すのは無駄が大きい。
    //
    // 8.139：**ノードの種別まで見るようにしました**（Phase 177／1.42）。
    //
    // Phase 176まではプロパティ名だけを見ていて、コード区間は
    // **偶然拾えていただけ**でした——`chordRegionStartTime`と`clipStartTime`が
    // どちらも`"startTime"`で、`juce::Identifier`としても等しかったためです。
    // 拍で持つようになって名前が分かれたので、その偶然はもう効きません。
    const bool affectsScrollRange =
        (tree.hasType (IDs::AUDIOCLIP)
            && (property == IDs::clipStartTime || property == IDs::clipLength
                || property == IDs::clipOffset))
        || (tree.hasType (IDs::CHORDREGION)
            && (property == IDs::chordRegionStartBeats
                || property == IDs::chordRegionLengthBeats));

    if (affectsScrollRange)
        updateScrollBars();

    repaint();
}

void TimelineComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    updateScrollBars();
    repaint();
}

void TimelineComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    // Phase 33：トラックが消えた／並びが変わった可能性があるので、まず番号を引き直す。
    // クリップの判定はこの後で行う（トラック番号が正しくないと意味が無いため）。
    syncSelectedTrackIndexFromSelection();

    // 他の画面でクリップが消されている可能性がある。番号がずれたままにすると、
    // 「消したはずのクリップ」の位置に別のクリップが選択中として描かれてしまう。
    if (selectedTrackIndex >= 0 && selectedClipIndex >= 0)
    {
        bool stillValid = juce::isPositiveAndBelow (selectedTrackIndex, project.getNumTracks());

        if (stillValid)
        {
            auto track = project.getTrack (selectedTrackIndex);
            // 8.94：**クリップはオーディオだけ**（Phase 134）
            const int numClips = selectedIsMidi ? 0 : track.getNumClips();

            stillValid = juce::isPositiveAndBelow (selectedClipIndex, numClips);
        }

        if (! stillValid)
            clearSelection();
    }

    updateScrollBars();
    repaint();
}

void TimelineComponent::valueTreeChildOrderChanged (juce::ValueTree&, int, int)
{
    // Phase 33：並べ替えで番号がずれる。引き直さないと、選択の枠が
    // 「動かしたトラック」ではなく「その位置に来たトラック」に付いたままになる。
    syncSelectedTrackIndexFromSelection();

    updateScrollBars();
    repaint();
}

void TimelineComponent::syncSelectedTrackIndexFromSelection()
{
    const auto trackId = selection.getTrackId();

    if (trackId.isEmpty())
        return; // そもそも何も選んでいない

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        if (project.getTrack (t).getId() == trackId)
        {
            selectedTrackIndex = t;
            return;
        }
    }

    // 選んでいたトラックが消えた
    clearSelection();
}

//==============================================================================
// 座標変換とスクロール範囲
//==============================================================================

int TimelineComponent::timeToX (double timeSeconds) const
{
    return trackHeaderWidth + (int) ((timeSeconds - scrollStartSeconds) * pixelsPerSecond);
}

double TimelineComponent::xToTime (int x) const
{
    return scrollStartSeconds + (double) (x - trackHeaderWidth) / pixelsPerSecond;
}

int TimelineComponent::getRowHeight (int rowIndex) const
{
    const int trackArea = getTrackAreaHeight (rowIndex);

    // 8.56：**行の高さにレーンのぶんを含める**（Phase 94／D3）。
    //
    // ここ1箇所で足しておくと、行の位置（`getTrackRowY()`）も当たり判定も
    // スクロール範囲も、**今までの計算のまま**レーンのぶんだけ下がってくれる。
    // 8.18でそう作ってあったのがそのまま効いた形（8.50の「高さ0」と同じ要領）。
    //
    // **畳んだフォルダの中（高さ0）にはレーンも出さない。** 「居ないのと同じ」を崩さない
    if (trackArea <= 0)
        return 0;

    return trackArea + getAutomationRowsHeight (rowIndex);
}

int TimelineComponent::getTrackAreaHeight (int rowIndex) const
{
    // 仕様書5.2.3：コードトラックだけ半分（Phase 58／8.1のC15）。
    // マスター行と範囲外は標準の高さ（`getAddTrackRowBounds()`が
    // `getNumRows()`を渡してくるので、範囲外でも答えられる必要がある）
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return trackRowHeight;

    auto track = project.getTrack (rowIndex);

    // 8.50：**畳んだフォルダの中は高さ0**（Phase 89／D2）。
    //
    // **ここ1箇所で「居ないのと同じ」にできる**のが要点です。行の位置も当たり判定も
    // `getRowHeight()`を積んで求めているので（8.18のC15でそう作った）、
    // 0を返せば描画・クリック・並べ替えのすべてから自然に外れます。
    if (project.isTrackHiddenByCollapsedFolder (track))
        return 0;

    const auto type = track.getType();

    // 8.44：**コードトラックは、畳んでいるときは見出しだけの高さ**（Phase 84／C12）。
    // 0にはしません——**畳んだことと、行が消えたことは別**なので、
    // 開き直す取っ手（三角）が残る高さを確保します。
    // **手で決めた高さより優先**：畳んだのに高いままだと、畳んだ意味がありません。
    //
    // 8.65：**フォルダは畳んでも縮めません**（Phase 103）。
    //
    // フォルダの「畳む」は**中身を隠す**という意味で、
    // コードトラックの「自分の中身（コード区間）を隠す」とは別のものです。
    // 縮めるとフォルダ自身のソロ／ミュート・フェーダー・メーターまで消え、
    // **まとめて音を触るというフォルダの用途が果たせなくなります**（8.51）。
    // 変わるのは三角の向き（▼→▶）だけにしてあります
    if (type == TrackType::Chord && track.isCollapsed())
        return collapsedRowHeight;

    // 8.62：**手で決めた高さがあればそれ**（Phase 100）。
    // ヘッダーの下端をドラッグして決めたもの。**種別ごとの既定より優先**します
    if (const int custom = track.getCustomRowHeight(); custom > 0)
        return juce::jlimit (minimumTrackRowHeight, maximumTrackRowHeight, custom);

    // 8.51：**フォルダは通常の高さ**（Phase 90／D2）。
    // ソロ・ミュート・パン・メーターを持つようになったので、下段が要ります。
    // 仕様書5.2.3：コードトラックだけ半分（中身が「名前だけ」なので）
    return type == TrackType::Chord ? chordRowHeight : trackRowHeight;
}

int TimelineComponent::getPinnedRowIndex() const
{
    // 仕様書5.2.3：コードトラックは先頭に固定されている（ProjectModelが守っている。8.20）
    return project.hasPinnedChordTrack() ? 0 : -1;
}

int TimelineComponent::getPinnedRowsHeight() const
{
    const int pinned = getPinnedRowIndex();

    return pinned >= 0 ? getRowHeight (pinned) : 0;
}

int TimelineComponent::getScrollableTop() const
{
    return rulerHeight + getPinnedRowsHeight();
}

int TimelineComponent::getScrollableAreaHeight() const
{
    return juce::jmax (0, getTimelineArea().getHeight() - getPinnedRowsHeight());
}

int TimelineComponent::getTrackRowY (int rowIndex) const
{
    // Phase 18でルーラーが入ったぶん、トラック行はその下から始まる。
    //
    // **Phase 58から掛け算では出せない**（8.18）。行ごとに高さが違うので、
    // 上から順に足していくしかない。ここを「行番号 × trackRowHeight」に
    // 戻すと、コードトラックより下の行が全部ずれる。
    const int pinned = getPinnedRowIndex();

    // 仕様書5.2.3：固定行はルーラーの真下から動かない（Phase 60／8.20）
    if (rowIndex == pinned)
        return rulerHeight;

    int y = getScrollableTop() - verticalScrollPixels;

    for (int i = 0; i < rowIndex; ++i)
        if (i != pinned)
            y += getRowHeight (i);

    return y;
}

int TimelineComponent::getTrackIndexForY (int y) const
{
    if (y < rulerHeight)
        return -1; // ルーラーの上。トラックではない

    const int pinned = getPinnedRowIndex();

    // 固定行はスクロールしないので、判定も座標を積む前に済ませる
    if (pinned >= 0 && y < rulerHeight + getRowHeight (pinned))
        return pinned;

    int rowY = getScrollableTop() - verticalScrollPixels;

    if (y < rowY)
        return -1; // 上へスクロールし過ぎた位置。トラック無しとして扱う

    const int numRows = getNumRows();

    for (int i = 0; i < numRows; ++i)
    {
        if (i == pinned)
            continue;

        const int height = getRowHeight (i);

        if (y < rowY + height)
            return i;

        rowY += height;
    }

    // 行より下（「+ 新しいトラック」やその先）。**Phase 57までと同じ数え方**で
    // 行番号を伸ばしておく。呼び出し側は`isPositiveAndBelow()`で弾く前提だが、
    // マスター行の判定だけは`isShowingAutomation()`と併せて見ている（mouseDown参照）
    return numRows + (y - rowY) / trackRowHeight;
}

juce::Rectangle<int> TimelineComponent::getTimelineArea() const
{
    return { trackHeaderWidth, rulerHeight,
             juce::jmax (0, getWidth() - trackHeaderWidth - scrollBarThickness),
             juce::jmax (0, getHeight() - scrollBarThickness - rulerHeight) };
}

juce::Rectangle<int> TimelineComponent::getRulerArea() const
{
    return { trackHeaderWidth, 0,
             juce::jmax (0, getWidth() - trackHeaderWidth - scrollBarThickness),
             rulerHeight };
}

double TimelineComponent::getContentLengthSeconds() const
{
    double lastClipEnd = 0.0;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // MIDIトラックもスクロール範囲の計算に含める（Phase 15）。
        // 含めないと、MIDIだけが右へ伸びているときにそこまでスクロールできない。
        // 8.91：**端を決めるのはノート**（Phase 131でクリップの長さが無くなった）
        if (track.getType() == TrackType::Midi)
        {
            for (int n = 0; n < track.getNumNotes(); ++n)
            {
                auto note = track.getNote (n);
                lastClipEnd = juce::jmax (lastClipEnd, note.getStartTime() + note.getLength());
            }

            continue;
        }

        // 仕様書5.2.3：コード区間も範囲に含める（Phase 42）。
        // 含めないと、コード進行だけを先に置いたときに、その先までスクロールできない。
        if (track.getType() == TrackType::Chord)
        {
            for (int r = 0; r < track.getNumChordRegions(); ++r)
                lastClipEnd = juce::jmax (lastClipEnd, track.getChordRegion (r).getEndTime());

            continue;
        }

        if (track.getType() != TrackType::Audio)
            continue;

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);
            lastClipEnd = juce::jmax (lastClipEnd, clip.getStartTime() + clip.getLength());
        }
    }

    // クリップの右端ぴったりで止まると、続きへクリップを置けなくなるので余白を足す。
    // 何も無いプロジェクトでも最低30秒ぶんはスクロールできるようにしておく。
    return juce::jmax (30.0, lastClipEnd + 10.0);
}

double TimelineComponent::getVisibleSeconds() const
{
    const auto timelineArea = getTimelineArea();

    return pixelsPerSecond > 0.0 ? (double) timelineArea.getWidth() / pixelsPerSecond : 0.0;
}

double TimelineComponent::getScrollableLengthSeconds() const
{
    // 8.162：**右はどこまでも行ける**（Phase 200／本人の要望）。
    // 考え方も式もピアノロール側と同じ——**同じ話を2通りに書かない**（8.2）。
    // 上のクリップ余白（+10秒）は「中身の端」なので、こちらとは役割が違う
    return juce::jmax (getContentLengthSeconds(), scrollStartSeconds + getVisibleSeconds() * 2.0);
}

void TimelineComponent::resized()
{
    auto area = getLocalBounds();

    auto bottomRow = area.removeFromBottom (scrollBarThickness);

    // 縦スクロールバーは**スクロールする行の範囲だけ**に置く
    // （ルーラーと、固定表示のコードトラックのぶんは空ける。Phase 60）
    verticalScrollBar.setBounds (area.removeFromRight (scrollBarThickness).withTrimmedTop (getScrollableTop()));

    // 仕様書5.9：表示形式の切り替えは、ルーラー左端の角（トラックヘッダーの上）に置く。
    //
    // 8.199：**角の上半分だけを使います**（Phase 234／改善案5の3）。
    // 下半分は「+ Track」「+ Audio」の席で、置くのは`ArrangeView`の仕事です。
    // **幅もヘッダーいっぱいに広げました**——下に2つ並ぶので、
    // 54pxのままだと上だけ短くて、角が揃って見えません
    timeFormatButton.setBounds (getCornerArea().reduced (4, 0)
                                                .withTop (2)
                                                .withBottom (timeFormatBottom));

    // 横スクロールバーはタイムライン部分の幅だけに置く（ヘッダーの下には敷かない）
    bottomRow.removeFromLeft (trackHeaderWidth);
    bottomRow.removeFromRight (scrollBarThickness);
    horizontalScrollBar.setBounds (bottomRow);

    updateScrollBars();
    layoutHeaderControls();   // 仕様書5.7（Phase 58）
}

void TimelineComponent::layoutHeaderControls()
{
    // 8.61：ヘッダーの音量・パン・メーター（Phase 99／改善案⑨⑩）。
    //
    // **行と1対1で作る**（Phase 98までは「音を通すトラックだけ」でメーターを作っていて、
    // 数がトラック数と一致せず、行番号との対応を毎回組み直していた）。
    // 種別で中身を出し分けるのは`TrackHeaderControls`自身の仕事にしてある。
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        while (headerControls.size() <= t)
        {
            auto* controls = headerControls.add (new TrackHeaderControls (project));

            controls->onMixerValueChanged = [this]
            {
                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
            };

            // 8.126：まとめて選んでいるトラックも同じだけ動かす（Phase 162／改善案35）
            controls->onVolumeNudged = [this] (const juce::String& trackId, float deltaDb)
            {
                nudgeSelectedTrackVolumes (trackId, deltaDb);
            };

            // 仕様書5.6：Touch/Latchの記録開始・終了は`ArrangeView`経由でエンジンへ
            controls->onTouchStart = [this] (const juce::String& trackId, const juce::String& targetId)
            {
                if (onAutomationTouchStart != nullptr)
                    onAutomationTouchStart (trackId, targetId);
            };

            controls->onTouchEnd = [this] (const juce::String& trackId, const juce::String& targetId)
            {
                if (onAutomationTouchEnd != nullptr)
                    onAutomationTouchEnd (trackId, targetId);
            };

            addChildComponent (controls);
        }

        auto* controls = headerControls[t];

        // **担当トラックは付け替える**（作り直さない。ここはタイマーから毎回来る）
        controls->setTrack (project.getTrack (t));
        controls->setPlayheadSeconds (playheadSeconds);

        // 8.65：**名前の行も含めた領域を渡す**（Phase 103）。
        // メーターを行の高さいっぱいに伸ばすため
        auto bounds = getHeaderControlsBounds (t);

        // `setBounds()`は同じ値なら中で早期に戻るので、毎回呼んでも描き直しは起きない
        controls->setBounds (bounds);

        // **ルーラーへ食い込む位置になったら隠すこと。** 子コンポーネントは
        // 親の`paint()`より後に描かれるので、上へスクロールして行がルーラーの下へ
        // 潜っても、つまみだけが目盛りの上に残って見える。
        // 低い行（コードトラック・畳んだ行）では`bounds`が空になるので、そこでも隠れる
        controls->setVisible (! bounds.isEmpty()
                                 && bounds.getY() >= rulerHeight
                                 && bounds.getBottom() <= getHeight());
    }

    // 減ったぶんは捨てる（残しておくと、消したトラックのつまみが宙に浮く）
    while (headerControls.size() > project.getNumTracks())
        headerControls.removeLast();
}

void TimelineComponent::refreshHeaderMeters (const std::function<float (const juce::String&, int)>& getLevel)
{
    // **並べ直しを先にやる。** スクロールや行の増減を拾う経路をいくつも用意すると、
    // どれかを必ず忘れる。ここを毎回通しておけば、遅くとも次のタイマーで直る（8.18）
    layoutHeaderControls();

    if (getLevel == nullptr)
        return;

    for (int t = 0; t < project.getNumTracks() && t < headerControls.size(); ++t)
    {
        // 並び順ではなくtrackIdでエンジンへ問い合わせる（ConsoleViewと同じ理由）
        const auto trackId = project.getTrack (t).getId();

        headerControls[t]->setLevels (getLevel (trackId, 0), getLevel (trackId, 1));
    }
}

void TimelineComponent::updateScrollBars()
{
    auto timelineArea = getTimelineArea();

    const double visibleSeconds = pixelsPerSecond > 0.0 ? timelineArea.getWidth() / pixelsPerSecond : 0.0;
    const double contentSeconds = juce::jmax (getScrollableLengthSeconds(), visibleSeconds);   // 8.162（Phase 200）

    horizontalScrollBar.setRangeLimits ({ 0.0, contentSeconds }, juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange ({ scrollStartSeconds, scrollStartSeconds + visibleSeconds },
                                          juce::dontSendNotification);

    // Phase 60：固定行（コードトラック）はスクロールしないので、
    // 「見えている高さ」からも「中身の高さ」からも外して数える（8.20）
    const int visibleHeight = getScrollableAreaHeight();
    const double contentHeight = juce::jmax (getContentHeightPixels(), visibleHeight);

    // 8.162：旗が横へ動いたら入力欄も付いていく（Phase 200）。
    // **横に動く経路はここへ集まっている**ので、ここ1箇所で足りる
    updateMarkerNameEditorBounds();

    verticalScrollBar.setRangeLimits ({ 0.0, contentHeight }, juce::dontSendNotification);
    verticalScrollBar.setCurrentRange ({ (double) verticalScrollPixels,
                                          (double) verticalScrollPixels + visibleHeight },
                                        juce::dontSendNotification);
}

void TimelineComponent::setScrollStartSeconds (double newStartSeconds)
{
    auto timelineArea = getTimelineArea();
    const double visibleSeconds = pixelsPerSecond > 0.0 ? timelineArea.getWidth() / pixelsPerSecond : 0.0;
    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - visibleSeconds);   // 8.162（Phase 200）

    const double limited = juce::jlimit (0.0, maxStart, newStartSeconds);

    if (limited == scrollStartSeconds)
        return;

    scrollStartSeconds = limited;
    updateScrollBars();
    repaint();
}

void TimelineComponent::setVerticalScrollPixels (int newOffset)
{
    const int contentHeight = getContentHeightPixels();
    const int maxOffset = juce::jmax (0, contentHeight - getScrollableAreaHeight());

    const int limited = juce::jlimit (0, maxOffset, newOffset);

    if (limited == verticalScrollPixels)
        return;

    verticalScrollPixels = limited;
    updateScrollBars();
    layoutHeaderControls();   // 仕様書5.7：行が動いたらメーターも追わせる（Phase 58）
    repaint();
}

void TimelineComponent::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved == &horizontalScrollBar)
        setScrollStartSeconds (newRangeStart);
    else if (scrollBarThatHasMoved == &verticalScrollBar)
        setVerticalScrollPixels ((int) newRangeStart);
}

//==============================================================================
// ズーム
//==============================================================================

void TimelineComponent::setZoom (double newPixelsPerSecond, int anchorX)
{
    const double limited = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, newPixelsPerSecond);

    if (limited == pixelsPerSecond)
        return;

    // 拡大縮小の軸となる位置の時刻を先に覚えておき、倍率変更後も同じ位置に来るよう
    // スクロール量を調整する。これが無いと、拡大するたびに見ていた場所が画面外へ逃げる。
    const double anchorTime = xToTime (anchorX);
    const int anchorOffsetPixels = anchorX - trackHeaderWidth;

    pixelsPerSecond = limited;

    auto timelineArea = getTimelineArea();
    const double visibleSeconds = timelineArea.getWidth() / pixelsPerSecond;
    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - visibleSeconds);   // 8.162（Phase 200）

    scrollStartSeconds = juce::jlimit (0.0, maxStart, anchorTime - anchorOffsetPixels / pixelsPerSecond);

    updateScrollBars();
    repaint();
}

void TimelineComponent::zoomIn()
{
    setZoom (pixelsPerSecond * zoomStepFactor, getTimelineArea().getCentreX());
}

void TimelineComponent::zoomOut()
{
    setZoom (pixelsPerSecond / zoomStepFactor, getTimelineArea().getCentreX());
}

void TimelineComponent::zoomToFit()
{
    auto timelineArea = getTimelineArea();
    const double contentSeconds = getContentLengthSeconds();

    if (contentSeconds <= 0.0 || timelineArea.getWidth() <= 0)
        return;

    scrollStartSeconds = 0.0;
    pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond,
                                     timelineArea.getWidth() / contentSeconds);

    updateScrollBars();
    repaint();
}

void TimelineComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Ctrl（Macではcommand）+ホイールで拡大縮小。多くのDAW・エディタ共通の操作。
    if (e.mods.isCommandDown())
    {
        const double factor = std::pow (zoomStepFactor, wheel.deltaY > 0.0f ? 1.0 : -1.0);
        setZoom (pixelsPerSecond * factor, e.x);
        return;
    }

    // Shift+ホイール、またはホイールの横方向成分で横スクロール
    if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        const float delta = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
        setScrollStartSeconds (scrollStartSeconds - delta * (getTimelineArea().getWidth() * 0.25) / pixelsPerSecond);
        return;
    }

    // 8.123：**ルーラーの上ではホイールだけで拡大縮小**（Phase 158／改善案20）。
    //
    // Ctrl＋ホイールはどこでも効きますが、**押しながら回すのは片手では難しい**。
    // ルーラーは「時間の物差し」そのものなので、その上でだけ修飾キー無しにしています。
    //
    // **Ctrl・Shiftの分岐より後に置くこと。** 先に置くと、ルーラーの上で
    // Shift＋ホイールを回したときに横スクロールではなく拡大縮小になり、
    // 「画面のどこにいるか」で修飾キーの意味が変わってしまう
    if (getRulerArea().contains (e.getPosition()))
    {
        const double factor = std::pow (zoomStepFactor, wheel.deltaY > 0.0f ? 1.0 : -1.0);
        setZoom (pixelsPerSecond * factor, e.x);
        return;
    }

    setVerticalScrollPixels (verticalScrollPixels - (int) (wheel.deltaY * trackRowHeight * 2.0f));
}

void TimelineComponent::clearSelection()
{
    selectedTrackIds.clear();   // 8.126（Phase 162／改善案35）
    selectedTrackIndex = -1;
    selectedClipIndex = -1;
    selectedIsMidi = false;
    dragMode = DragMode::None;

    // 8.57：レーンの点も同じ扱い（Phase 95／D14）。プロジェクトが差し替わると
    // 覚えていたValueTreeは別プロジェクトのものになる
    selectedAutomationPoints.clear();
    automationDragOthers.clear();
    automationDragRow = {};
    automationPointIndex = -1;
    automationPendingAdd = false;
    automationRangeSelecting = false;
    lastEditedAutomationRow = {};

    publishSelection();
    repaint();
}

void TimelineComponent::publishSelection()
{
    // 8.59：**トラック／クリップを選んだらレーンの選択は解ける**（Phase 96）。
    //
    // **ここ1箇所で消すのが要点です。** 選択を変える経路はどれも最後にここを通るので、
    // 「解き忘れた経路」が出ません（レーン側は`selectAutomationRow()`が別に持つ）
    selectedLaneTrackId.clear();
    selectedLaneTargetId.clear();

    if (! juce::isPositiveAndBelow (selectedTrackIndex, project.getNumTracks()))
    {
        selection.selectNone();
        return;
    }

    const auto trackId = project.getTrack (selectedTrackIndex).getId();

    // 8.126：**クリップを選んだら、まとめ選択はそのトラック1本に畳む**
    // （Phase 162／改善案35）。
    //
    // クリップを押すのは「このトラックで作業する」という合図なので、
    // さっきCtrlで選んだ別のトラックまで音量が動くのは驚きです。
    // **ヘッダーのクリックはここへ来る前に自分で決めている**ので、
    // 畳むのはクリップのときだけで足ります
    if (selectedClipIndex >= 0)
    {
        setSingleTrackSelection (trackId);
        selection.selectClip (trackId, selectedClipIndex, selectedIsMidi);
    }
    else
    {
        selection.selectTrack (trackId);
    }
}

void TimelineComponent::setPlayheadSeconds (double seconds)
{
    // 値が変わらないときは描き直さない。Phase 30で下端のトランスポートにも
    // 再生位置を配るようになり、停止中にも呼ばれる経路が増えたため。
    if (seconds == playheadSeconds)
        return;

    playheadSeconds = seconds;
    repaint();
}

juce::Rectangle<int> TimelineComponent::getClipBounds (int trackIndex, int clipIndex) const
{
    auto track = project.getTrack (trackIndex);
    auto clip = track.getClip (clipIndex);

    const int rowY = getTrackRowY (trackIndex);
    const int clipX = timeToX (clip.getStartTime());
    const int clipWidth = juce::jmax (4, (int) (clip.getLength() * pixelsPerSecond));

    return { clipX, rowY + 4, clipWidth, getTrackAreaHeight (trackIndex) - 8 };
}

//==============================================================================
// 仕様書5.2.3：コードトラックのコード区間（Phase 42）

juce::Rectangle<int> TimelineComponent::getChordRegionBounds (int trackIndex, int regionIndex) const
{
    auto region = project.getTrack (trackIndex).getChordRegion (regionIndex);

    const int rowY = getTrackRowY (trackIndex);
    const int regionX = timeToX (region.getStartTime());
    const int regionWidth = juce::jmax (4, (int) (region.getLength() * pixelsPerSecond));

    return { regionX, rowY + 4, regionWidth, getTrackAreaHeight (trackIndex) - 8 };
}

juce::Rectangle<int> TimelineComponent::getChordFlagBounds (int trackIndex, int regionIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return {};

    auto track = project.getTrack (trackIndex);

    if (! juce::isPositiveAndBelow (regionIndex, track.getNumChordRegions()))
        return {};

    return chordFlagRect (getChordRegionBounds (trackIndex, regionIndex),
                           track.getChordRegion (regionIndex).getChord().getName());
}

bool TimelineComponent::hitTestChordFlag (juce::Point<int> position, int& trackIndexOut,
                                           int& regionIndexOut) const
{
    int trackIndex = -1;
    int regionIndex = -1;

    if (! hitTestChordRegion (position, trackIndex, regionIndex))
        return false;

    if (! getChordFlagBounds (trackIndex, regionIndex).contains (position))
        return false;

    trackIndexOut = trackIndex;
    regionIndexOut = regionIndex;
    return true;
}

bool TimelineComponent::hitTestChordRegion (juce::Point<int> position, int& trackIndexOut,

                                             int& regionIndexOut) const
{
    if (! getTimelineArea().contains (position))
        return false;

    const int trackIndex = getTrackIndexForY (position.y);

    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return false;

    auto track = project.getTrack (trackIndex);

    if (track.getType() != TrackType::Chord)
        return false;

    // 8.44：**畳んでいるあいだは掴めない**（Phase 84）。描いていないものは触れない
    if (track.isCollapsed())
        return false;

    for (int r = 0; r < track.getNumChordRegions(); ++r)
    {
        if (getChordRegionBounds (trackIndex, r).contains (position))
        {
            trackIndexOut = trackIndex;
            regionIndexOut = r;
            return true;
        }
    }

    return false;
}

void TimelineComponent::setSelectedChordRegion (const juce::ValueTree& regionState)
{
    if (selectedChordRegion == regionState)
        return;

    selectedChordRegion = regionState;
    repaint();
}

bool TimelineComponent::deleteSelectedChordRegion()
{
    // **消えた区間を掴んだままにしない**（親を失ったValueTreeは無効）
    if (! selectedChordRegion.isValid() || ! selectedChordRegion.getParent().isValid())
    {
        selectedChordRegion = {};
        return false;
    }

    auto parent = selectedChordRegion.getParent();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Chord)
            continue;

        for (int r = 0; r < track.getNumChordRegions(); ++r)
        {
            auto region = track.getChordRegion (r);

            if (region.state != selectedChordRegion)
                continue;

            project.beginAction (utf8 ("コード区間の削除"));
            track.removeChordRegion (region, &project.getUndoManager());
            selectedChordRegion = {};

            // 8.128：**消したら並べ直す**（Phase 164／改善案13）。
            // 手前の区間が、消えた旗のぶんまで伸びます——
            // 呼ばないと、そこだけ「コードの無い隙間」が残ります
            track.normaliseChordRegions (&project.getUndoManager());

            if (onModelChanged != nullptr)
                onModelChanged();

            repaint();
            return true;
        }
    }

    selectedChordRegion = {};
    return false;
}

void TimelineComponent::drawChordRegion (juce::Graphics& g, juce::Rectangle<int> bounds, ChordRegion region)
{
    // 8.128：**帯ではなく旗で表す**（Phase 164／改善案13）。
    //
    // 拍子・テンポ・キーのレーンと同じ「この旗から次の旗まで」という表し方に揃えました。
    // **長さを掴んで伸縮する操作は無くなりました**——次の旗までが自動的に長さです
    // （`Track::normaliseChordRegions()`）。
    //
    // **行の高さは変えていません**（本人の指定）。いまの大きさが見やすいとのことなので、
    // 旗を上端に置いて、下は区間の切れ目が読める薄い塗りにしてあります。
    const bool isSelected = (region.state.isValid() && region.state == selectedChordRegion);

    // 区間ぜんぶを薄く塗る。**帯の代わりではなく「どこからどこまでか」の下敷き**で、
    // 塗りだけでは読めないくらいに抑えてある（旗のほうを読ませたい）
    g.setColour (AppColours::purple.withAlpha (isSelected ? 0.22f : 0.10f));
    g.fillRect (bounds);

    // 始まりの縦線。**旗が指している位置**がこれ（マーカーの旗と同じ考え方）
    g.setColour (AppColours::purple);
    g.fillRect (bounds.getX(), bounds.getY(), isSelected ? 3 : 2, bounds.getHeight());

    const auto name = region.getChord().getName();
    const auto label = chordFlagRect (bounds, name);

    // 空＝区間からはみ出す。**札は描かない**（当たり判定も同じ判断で外れる）
    if (label.isEmpty())
        return;

    g.setColour (AppColours::purple);
    g.fillRect (label);

    // 8.29の表：選択中は**枠を足す**（これが Delete の対象、と読めるように）
    if (isSelected)
    {
        g.setColour (AppColours::textPrimary);
        g.drawRect (label, 1);
    }

    // 旗の地はパープルで塗り切ってあるので、文字は白で固定。
    // **`textPrimary`にしないこと**——ライトテーマでは濃い色になり、紫の上で沈みます（1.43）
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (name, label.reduced (6, 0), juce::Justification::centredLeft, false);
}

double TimelineComponent::getBeatSeconds (double atTime) const
{
    return project.getBeatSecondsAt (atTime);
}

//==============================================================================
// 仕様書6.2：ツール切り替えと複数選択（Phase 51）

void TimelineComponent::setEditTool (EditTool newTool)
{
    if (editTool == newTool)
        return;

    editTool = newTool;

    // 掴んでいる途中でツールが変わると、離したときに別の意味で処理されてしまう
    dragMode = DragMode::None;
    rangeSelecting = false;

    if (onEditToolChanged != nullptr)
        onEditToolChanged();

    repaint();
}

TimelineComponent::ClipRef TimelineComponent::makeClipRef (int trackIndex, int clipIndex, bool isMidi) const
{
    ClipRef ref;

    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return ref;

    auto track = project.getTrack (trackIndex);
    ref.trackId = track.getId();
    ref.isMidi = isMidi;

    // 8.94：**クリップはオーディオだけ**（Phase 134）。MIDIはIDを持たない塊なので、
    // ここへは来ません（`isMidi`はオーディオ側の取り違え防止に残してある）
    if (! isMidi && juce::isPositiveAndBelow (clipIndex, track.getNumClips()))
        ref.clipId = track.getClip (clipIndex).getId();

    return ref;
}

bool TimelineComponent::isClipSelected (const juce::String& trackId, const juce::String& clipId,
                                         bool isMidi) const
{
    for (const auto& ref : selectedClips)
        if (ref.trackId == trackId && ref.clipId == clipId && ref.isMidi == isMidi)
            return true;

    return false;
}

void TimelineComponent::toggleClipSelection (int trackIndex, int clipIndex, bool isMidi)
{
    const auto ref = makeClipRef (trackIndex, clipIndex, isMidi);

    if (ref.clipId.isEmpty())
        return;

    auto found = std::find (selectedClips.begin(), selectedClips.end(), ref);

    if (found != selectedClips.end())
    {
        selectedClips.erase (found);

        // 8.78：**グループは丸ごと外す**（Phase 118）。1つだけ外しても、
        // すぐ下の`expandSelectionToGroups()`が引き戻すので意味がありません
        if (const auto groupId = Track::getClipGroupId (findClipStateForRef (ref)); groupId.isNotEmpty())
            selectedClips.erase (std::remove_if (selectedClips.begin(), selectedClips.end(),
                                                  [this, groupId] (const ClipRef& other)
                                                  {
                                                      return Track::getClipGroupId (findClipStateForRef (other)) == groupId;
                                                  }),
                                  selectedClips.end());
    }
    else
    {
        selectedClips.push_back (ref);
        expandSelectionToGroups();   // 8.78：同じグループのものも一緒に選ぶ
    }

    if (onClipSelectionChanged != nullptr)
        onClipSelectionChanged();

    repaint();
}

void TimelineComponent::setSingleClipSelection (int trackIndex, int clipIndex, bool isMidi)
{
    const auto ref = makeClipRef (trackIndex, clipIndex, isMidi);

    selectedClips.clear();

    if (ref.clipId.isNotEmpty())
    {
        selectedClips.push_back (ref);
        expandSelectionToGroups();   // 8.78：同じグループのものも一緒に選ぶ（Phase 118）
    }

    if (onClipSelectionChanged != nullptr)
        onClipSelectionChanged();
}

void TimelineComponent::selectAllClips()
{
    selectedClips.clear();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        for (int c = 0; c < track.getNumClips(); ++c)
            selectedClips.push_back (makeClipRef (t, c, false));

    }

    if (onClipSelectionChanged != nullptr)
        onClipSelectionChanged();

    repaint();
}

void TimelineComponent::clearClipSelection()
{
    // 単数の選択（ドラッグの対象）も一緒に外す。**片方だけ残すと、
    // 見た目には何も選ばれていないのにキー操作が効いてしまう**
    selectedClips.clear();
    clearSelection();

    if (onClipSelectionChanged != nullptr)
        onClipSelectionChanged();

    repaint();
}

void TimelineComponent::applyRangeSelection()
{
    selectedClips.clear();

    int firstTrackIndex = -1;
    int firstClipIndex = -1;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        for (int c = 0; c < track.getNumClips(); ++c)
            if (getClipBounds (t, c).intersects (rangeSelectBounds))
            {
                selectedClips.push_back (makeClipRef (t, c, false));

                if (firstTrackIndex < 0)
                {
                    firstTrackIndex = t;
                    firstClipIndex = c;
                }
            }
    }

    // 8.156：**グループは丸ごと選ぶ**（Phase 194／本人の報告）。
    // 他の2つの選び方（`setSingleClipSelection()`・`toggleClipSelection()`）は
    // 前から呼んでいて、**範囲選択だけ抜けていました**——
    // 枠で囲うとグループの片割れだけが選ばれる、という食い違いになります（8.78）
    expandSelectionToGroups();

    // 8.159：**同じ枠で、MIDIの時間範囲も引く**（Phase 197／本人の要望）。
    //
    // 本人の言葉は「隣接する空いているエリアから、選択ツールで範囲を囲っても
    // 選択できるようにしてほしい」。Phase 196までは**囲い始めた場所で決まって**いました。
    //
    // | 押し始めた場所 | Phase 196まで |
    // |---|---|
    // | MIDIトラックの空き | 時間範囲（MIDIだけ。`handleTimeRangeMouseDown()`） |
    // | オーディオの空き・トラックの下 | クリップの枠選択（**MIDIには効かない**） |
    //
    // **どちらから始めても、囲ったものが選ばれる**ようにします。
    // クリップは上で拾ってあるので、ここではMIDIのぶんを足すだけです。
    //
    // **時刻は寄せます**（8.14）——本人の指定どおり拍（スナップ）単位
    {
        std::vector<juce::String> midiTracks;

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getType() != TrackType::Midi)
                continue;

            const juce::Rectangle<int> row (0, getTrackRowY (t), getWidth(), getTrackAreaHeight (t));

            if (row.intersects (rangeSelectBounds))
                midiTracks.push_back (track.getId());
        }

        if (! midiTracks.empty())
        {
            const double from = project.snapTime (juce::jmax (0.0, xToTime (rangeSelectBounds.getX())));
            const double to   = project.snapTime (juce::jmax (0.0, xToTime (rangeSelectBounds.getRight())));

            // **幅が0になったら引かない**（掴めず、選んだことも見えない）
            if (to > from + 1.0e-6)
            {
                hasTimeRange = true;
                timeRangeAllTracks = false;
                timeRangeTrackIds = std::move (midiTracks);
                timeRangeStart = from;
                timeRangeEnd = to;
            }
        }
        else
        {
            // **MIDIを1つも囲っていないなら、前の範囲は畳む**
            // ——囲い直したのに、さっきの範囲が残っているのは分かりにくい
            clearTimeRange();
        }
    }

    // 8.156：**選んだことを外へも伝える**（Phase 194／本人の報告）。
    //
    // ここが`selectedClips`を書き換えるだけだったので、**インスペクタも
    // `SelectionState`も、枠で囲う前のまま**でした。ハイライトは付くのに
    // 「選べていない」ように見えるのは、これが理由です。
    //
    // **代表は左上の1つ**（`selectedTrackIndex`／`selectedClipIndex`）。
    // インスペクタは1つしか出せないので、他の選び方と同じ決め方に揃えます
    selectedTrackIndex = firstTrackIndex;
    selectedClipIndex = firstClipIndex;
    selectedIsMidi = false;
    publishSelection();

    if (onClipSelectionChanged != nullptr)
        onClipSelectionChanged();

    repaint();
}

void TimelineComponent::cutClipAt (int trackIndex, int clipIndex, bool isMidi, int x)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (trackIndex);

    // 8.92：**MIDIには「分割」がありません**（Phase 132）。
    // 割る対象の入れ物が無く、ノートは既に1つずつ独立しています（8.91）
    if (isMidi)
        return;

    auto clipState = track.getClip (clipIndex).state;

    if (! clipState.isValid())
        return;

    project.beginAction (utf8 ("クリップの分割"));

    // Phase 54：割る位置もスナップに従う（8.14）。目分量で割ると、
    // 割った境目が拍から外れて、後で並べ直したときに合わなくなる
    if (track.splitClipAt (clipState, project.snapTime (xToTime (x)), &project.getUndoManager()))
    {
        // 割ったことで番号が変わる。中途半端な選択を残さない（1.32）
        clearClipSelection();

        if (onModelChanged != nullptr)
            onModelChanged();
    }

    repaint();
}

//==============================================================================
// 仕様書5.9：ループ範囲（Phase 48）

juce::Rectangle<int> TimelineComponent::getLoopStripArea() const
{
    // ルーラーの上端。目盛りの数字と重ならないよう、細い帯にしてある
    return { trackHeaderWidth, 0, juce::jmax (0, getWidth() - trackHeaderWidth), loopStripHeight };
}

void TimelineComponent::updateLoopDrag (const juce::MouseEvent& e)
{
    if (loopDragMode == LoopDragMode::none)
        return;

    // Phase 54：寄せ先は共通のスナップ設定に従う（8.14）。以前はここだけ小節固定だった。
    //
    // **最短の長さは目盛り1つぶん。** フリーのときだけ拍を下限にしている
    // （0にすると、掴んだ端をもう一方の端まで持っていったときに範囲が消える）。
    const double time = project.snapTime (xToTime (e.x));

    const double snapSeconds = project.getSnapSecondsAt (time);
    const double minimumLength = (snapSeconds > 0.0) ? snapSeconds : getBeatSeconds (time);

    switch (loopDragMode)
    {
        case LoopDragMode::create:
            loopDragPreviewStart = juce::jmin (loopDragAnchorTime, time);
            loopDragPreviewEnd   = juce::jmax (loopDragAnchorTime, time);
            break;

        case LoopDragMode::moveStart:
            loopDragPreviewStart = juce::jmin (time, loopDragPreviewEnd - minimumLength);
            break;

        case LoopDragMode::moveEnd:
            loopDragPreviewEnd = juce::jmax (time, loopDragPreviewStart + minimumLength);
            break;

        default:
            break;
    }

    repaint();
}

void TimelineComponent::commitLoopDrag()
{
    if (loopDragMode == LoopDragMode::none)
        return;

    if (loopDragPreviewEnd > loopDragPreviewStart)
    {
        project.beginAction (utf8 ("ループ範囲の変更"));
        project.setLoopRange (loopDragPreviewStart, loopDragPreviewEnd, &project.getUndoManager());

        // **範囲を引いたらループを有効にする。** 引いたのに何も変わらないと、
        // 「引けていないのか、ループが切れているのか」が分からない
        if (! project.isLoopEnabled())
            project.setLoopEnabled (true, &project.getUndoManager());
    }

    loopDragMode = LoopDragMode::none;

    if (onLoopChanged != nullptr)
        onLoopChanged();

    repaint();
}

//==============================================================================
// 仕様書5.9：マーカー（Phase 49）

juce::Rectangle<int> TimelineComponent::getMarkerStripArea() const
{
    return { trackHeaderWidth, loopStripHeight,
             juce::jmax (0, getWidth() - trackHeaderWidth), markerStripHeight };
}

juce::Rectangle<int> TimelineComponent::getMarkerFlagBounds (int markerIndex) const
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return {};

    auto marker = project.getMarker (markerIndex);
    const double time = (markerIndex == markerDragIndex) ? markerDragPreviewTime : marker.getTime();

    auto strip = getMarkerStripArea();

    // 名前が読める幅を取る。**名前が長くても青天井にはしない**：
    // 隣のマーカーを覆ってしまうと、掴めなくなる
    // **`Font::getStringWidthFloat()`はJUCE 9で無くなっている**（2章）。
    // 置き換えは`GlyphArrangement::getStringWidth()`。
    const juce::Font font (juce::FontOptions ((float) markerStripHeight - 2.0f));
    const int textWidth = juce::jlimit (26, 140,
                                         (int) juce::GlyphArrangement::getStringWidth (font, marker.getName()) + 10);

    return { timeToX (time), strip.getY(), textWidth, strip.getHeight() };
}

int TimelineComponent::findMarkerAt (juce::Point<int> position) const
{
    if (! getMarkerStripArea().contains (position))
        return -1;

    // **後ろから見る。** 重なっている場合は、後から置いた（右にある）ほうが上に描かれる
    for (int i = project.getNumMarkers(); --i >= 0;)
        if (getMarkerFlagBounds (i).contains (position))
            return i;

    return -1;
}

void TimelineComponent::drawMarkers (juce::Graphics& g)
{
    auto strip = getMarkerStripArea();

    if (strip.isEmpty())
        return;

    g.saveState();
    g.reduceClipRegion (strip.withRight (getWidth()));

    const int numMarkers = project.getNumMarkers();

    for (int i = 0; i < numMarkers; ++i)
    {
        auto bounds = getMarkerFlagBounds (i);

        if (bounds.getRight() <= strip.getX() || bounds.getX() >= strip.getRight())
            continue;

        const bool dragging = (i == markerDragIndex);

        g.setColour (dragging ? AppColours::purple : AppColours::purple.withAlpha (0.75f));
        g.fillRect (bounds);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions ((float) markerStripHeight - 2.0f));
        g.drawText (project.getMarker (i).getName(), bounds.reduced (4, 0),
                     juce::Justification::centredLeft, false);
    }

    g.restoreState();

    // 旗から下へ伸びる縦線。**タイムライン側にも出す**ことで、
    // マーカーがどのクリップの位置に当たるのかが分かる
    g.setColour (AppColours::purple.withAlpha (0.35f));

    for (int i = 0; i < numMarkers; ++i)
    {
        const double time = (i == markerDragIndex) ? markerDragPreviewTime : project.getMarker (i).getTime();
        const int x = timeToX (time);

        if (x < trackHeaderWidth || x > getWidth())
            continue;

        g.drawVerticalLine (x, (float) rulerHeight, (float) getHeight());
    }
}

void TimelineComponent::showMarkerMenu (int markerIndex, juce::Point<int> screenPosition)
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return;

    const juce::String markerId = project.getMarker (markerIndex).getId();

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("名前を変更..."));
    menu.addItem (2, utf8 ("このマーカーを削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, markerId] (int result)
        {
            if (result <= 0)
                return;

            // **番号ではなくIDで引き直す。** メニューは非同期に閉じるので、
            // その間にマーカーが増減していると番号は別のものを指す（1.32）
            int index = -1;

            for (int i = 0; i < project.getNumMarkers(); ++i)
                if (project.getMarker (i).getId() == markerId)
                {
                    index = i;
                    break;
                }

            if (index < 0)
                return;

            auto marker = project.getMarker (index);

            if (result == 2)
            {
                project.beginAction (utf8 ("マーカーの削除"));
                project.removeMarker (marker, &project.getUndoManager());
                repaint();
                return;
            }

            renameMarker (marker);
        });
}

void TimelineComponent::transposeSelectedMidiClips (int semitones)
{
    // 8.94：**上下させる対象は「選んでいる時間範囲」**（Phase 134）。
    //
    // Phase 133まではクリップの選択（`selectedClips`）を見ていましたが、
    // MIDIにクリップが無くなったので、選択に入ることがありません（8.92）。
    // **ショートカットとメニューの両方がここを通ります**（入口は1つ。8.12）。
    if (! hasTimeRange || semitones == 0)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("先に、動かしたい範囲を選んでください"
                                    "（MIDIトラックの上をドラッグ、または塊をクリック）。"));
        return;
    }

    // 8.158：選んでいるトラック全部へ（Phase 196）。**1つのUndoにまとめる**
    project.beginAction (utf8 ("範囲のトランスポーズ"));

    for (const int t : getTimeRangeTrackIndices())
        transposeNotesInRange (t, timeRangeStart, timeRangeEnd, semitones);
}

//==============================================================================
// 8.147：オーディオのトランスポーズ（Phase 185／改善案㉞。仕様書5.5）
//==============================================================================

//==============================================================================
// 8.150：ワープマーカー（Phase 188／8.48。仕様書5.5.1）
//==============================================================================

void TimelineComponent::drawWarpMarkers (juce::Graphics& g, juce::Rectangle<int> bounds,
                                          const AudioClip& clip) const
{
    const auto markers = clip.getWarpMarkers();

    if (markers.empty() || bounds.getWidth() < 4)
        return;

    const double length = clip.getLength();
    const double offset = clip.getOffset();
    const double sourceEnd = offset + clip.getSourceLength();

    for (const auto& marker : markers)
    {
        // **トリムで隠れている範囲のものは描かない。** 消しはしないので
        // （`getWarpMap()`と同じ判定）、トリムを戻せばまた出ます
        if (marker.sourceSeconds <= offset || marker.sourceSeconds >= sourceEnd
             || marker.clipSeconds <= 0.0 || marker.clipSeconds >= length)
            continue;

        const int x = bounds.getX() + (int) std::round (marker.clipSeconds * pixelsPerSecond);

        if (x < bounds.getX() || x > bounds.getRight())
            continue;

        // 設計書2.6：時間をいじる印はパープル（音程・伸縮の札と同じ仲間）
        g.setColour (AppColours::purple);
        g.drawLine ((float) x, (float) bounds.getY(), (float) x, (float) bounds.getBottom(), 1.0f);

        // 上端の取っ手（掴めることが見て分かるように、ここだけ塗る）
        g.fillRect (x - warpHandleGrabWidth / 2, bounds.getY(),
                     warpHandleGrabWidth, warpHandleHeight);
    }
}

std::optional<double> TimelineComponent::findWarpMarkerHandleAt (juce::Rectangle<int> bounds,
                                                                  const AudioClip& clip,
                                                                  juce::Point<int> position) const
{
    if (position.y < bounds.getY() || position.y > bounds.getY() + warpHandleHeight)
        return {};

    // **端の掴みしろは取り上げないこと。** トリムと伸縮のほうが使う回数が多いので、
    // ぶつかったらそちらを優先します
    if (position.x < bounds.getX() + edgeGrabMargin || position.x > bounds.getRight() - edgeGrabMargin)
        return {};

    const double length = clip.getLength();
    const double offset = clip.getOffset();
    const double sourceEnd = offset + clip.getSourceLength();

    for (const auto& marker : clip.getWarpMarkers())
    {
        if (marker.sourceSeconds <= offset || marker.sourceSeconds >= sourceEnd
             || marker.clipSeconds <= 0.0 || marker.clipSeconds >= length)
            continue;

        const int x = bounds.getX() + (int) std::round (marker.clipSeconds * pixelsPerSecond);

        if (std::abs (position.x - x) <= warpHandleGrabWidth)
            return marker.sourceSeconds;
    }

    return {};
}

bool TimelineComponent::addWarpMarkerAt (double timelineSeconds)
{
    return applyToSelectedAudioClips (utf8 ("ワープマーカーを置く"),
                                       [this, timelineSeconds] (AudioClip& clip)
                                       {
                                           // **置いた瞬間は何も動かないこと。**
                                           // 「そこにある音を、そこに留める」印なので、
                                           // ソース側の時刻は**いまの対応表から引きます**
                                           const double clipSeconds = timelineSeconds - clip.getStartTime();
                                           const double sourceSeconds = clip.timelineToSourceTime (timelineSeconds);

                                           clip.addWarpMarker (sourceSeconds, clipSeconds,
                                                                &project.getUndoManager());
                                       });
}

bool TimelineComponent::createWarpMarkersFromHitPoints()
{
    return applyToSelectedAudioClips (utf8 ("ヒットポイントからワープマーカー"),
                                       [this] (AudioClip& clip)
                                       {
                                           // **いまの対応表のとおりに置く**ので、
                                           // 作った瞬間は音がまったく変わりません。
                                           // ここからマーカーを動かして直していきます
                                           for (auto sourceSeconds : clip.getHitPoints())
                                           {
                                               const double timeline = clip.sourceTimeToTimeline (sourceSeconds);

                                               clip.addWarpMarker (sourceSeconds,
                                                                    timeline - clip.getStartTime(),
                                                                    &project.getUndoManager());
                                           }
                                       });
}

bool TimelineComponent::clearWarpMarkers()
{
    return applyToSelectedAudioClips (utf8 ("ワープマーカーを全部消す"),
                                       [this] (AudioClip& clip)
                                       {
                                           clip.clearWarpMarkers (&project.getUndoManager());
                                       });
}

WarpMap TimelineComponent::makeDragPreviewWarpMap (const AudioClip& clip) const
{
    // 8.150：ドラッグ中の絵に使う表（Phase 188／8.48）。
    //
    // **マーカーはプレビューの値では動きません**（動かせるのは1つだけで、
    // それは`drawClipDragPreview()`が線1本として描く）。ここが面倒を見るのは
    // **トリムと伸縮で変わる両端**だけです
    const double sourceLength = dragPreviewLength / juce::jmax (1.0e-9, dragPreviewStretch);

    WarpMap map;
    map.points.push_back ({ dragPreviewOffset, 0.0 });

    for (const auto& marker : clip.getWarpMarkers())
        if (marker.sourceSeconds > dragPreviewOffset
             && marker.sourceSeconds < dragPreviewOffset + sourceLength
             && marker.clipSeconds > 0.0 && marker.clipSeconds < dragPreviewLength)
            map.points.push_back ({ marker.sourceSeconds, marker.clipSeconds });

    map.points.push_back ({ dragPreviewOffset + sourceLength, dragPreviewLength });
    map.sanitise();

    return map;
}

bool TimelineComponent::changeSelectedClipTranspose (int semitones, bool resetToZero)
{
    if (! resetToZero && semitones == 0)
        return false;

    return applyToSelectedAudioClips (resetToZero ? utf8 ("トランスポーズを0へ")
                                                   : utf8 ("オーディオのトランスポーズ"),
                                       [this, semitones, resetToZero] (AudioClip& clip)
                                       {
                                           const int next = resetToZero ? 0
                                                                        : clip.getTranspose() + semitones;

                                           // **上限は`setTranspose()`が収めます**（1.27）。
                                           // ここで別に収めると、2箇所が別々の答えを持ちます
                                           clip.setTranspose (next, &project.getUndoManager());
                                       });
}

bool TimelineComponent::fitSelectedClipsToBars (int bars)
{
    // 8.149：**ループ素材を曲のテンポへ合わせる**（Phase 187/8.48）。
    //
    // **いちばん使う場面がこれ**です（4小節のループを、この曲の速さで4小節に）。
    // 端をドラッグして合わせることもできますが、**ぴったりには止まりません。**
    if (bars <= 0)
        return false;

    const auto& tempoMap = project.getTempoMap();

    return applyToSelectedAudioClips (utf8 ("テンポに合わせる"),
                                       [this, bars, &tempoMap] (AudioClip& clip)
                                       {
                                           // **クリップが置いてある場所の小節から数えること。**
                                           // 曲の途中でテンポや拍子が変わるので、
                                           // 「4小節ぶんの秒数」は置き場所で違います（8.102）
                                           const double startBeat = tempoMap.getBeatAtTime (clip.getStartTime());
                                           const int startBar = tempoMap.getBarPositionAtBeat (startBeat).bar;

                                           const double fromBeat = tempoMap.getBeatForBarStart (startBar);
                                           const double toBeat = tempoMap.getBeatForBarStart (startBar + bars);

                                           const double target = tempoMap.getTimeForBeat (toBeat)
                                                                   - tempoMap.getTimeForBeat (fromBeat);

                                           const double sourceLength = clip.getSourceLength();

                                           if (target <= 0.0 || sourceLength <= 0.0)
                                               return;

                                           // **倍率が先、長さが後**ではなく**対で書く**（どちらが先でも同じ）。
                                           // 上下限で丸められることがあるので、**丸めた後の倍率で長さを決める**
                                           const double ratio = juce::jlimit (AudioTransform::minStretch,
                                                                               AudioTransform::maxStretch,
                                                                               target / sourceLength);

                                           clip.setStretch (ratio, &project.getUndoManager());
                                           clip.setLength (sourceLength * ratio, &project.getUndoManager());
                                       });
}

bool TimelineComponent::resetSelectedClipStretch()
{
    return applyToSelectedAudioClips (utf8 ("伸縮を戻す"),
                                       [this] (AudioClip& clip)
                                       {
                                           // **長さも戻すこと。** 倍率だけ1.0にすると、
                                           // 使うソースの範囲が伸縮ぶんだけ変わります
                                           const double sourceLength = clip.getSourceLength();

                                           clip.setStretch (1.0, &project.getUndoManager());
                                           clip.setLength (sourceLength, &project.getUndoManager());
                                       });
}

void TimelineComponent::transposeSelection (int semitones)
{
    if (semitones == 0)
        return;

    // **オーディオとMIDIの両方に配る。** どちらを選んでいるかで自然に分かれるので、
    // 使う側は「トランスポーズ」を1つ覚えるだけで済みます
    const bool movedAudio = changeSelectedClipTranspose (semitones, false);

    // MIDIは**選んでいる時間範囲**が対象（8.94）。範囲が無いときに呼ぶと
    // 「範囲を選んでください」と出るので、**オーディオが動いたなら黙っておく**
    if (hasTimeRange)
    {
        transposeSelectedMidiClips (semitones);
        return;
    }

    if (! movedAudio && onStatusMessage != nullptr)
        onStatusMessage (utf8 ("先に、動かしたいものを選んでください"
                                "（オーディオクリップ、またはMIDIトラックの時間範囲）。"));
}


//==============================================================================
// 8.78：クリップのグループ（Phase 118／改善案㊱。仕様書5.5）
//==============================================================================

juce::ValueTree TimelineComponent::findClipStateForRef (const ClipRef& ref) const
{
    auto track = project.findTrackById (ref.trackId);

    if (! track.state.getParent().isValid())
        return {};

    // **IDで引き直す**（番号は増減でずれる。1.32）
    // 8.94：**クリップはオーディオだけ**（Phase 134）
    if (ref.isMidi)
        return {};

    for (int c = 0; c < track.getNumClips(); ++c)
        if (track.getClip (c).getId() == ref.clipId)
            return track.getClip (c).state;

    return {};
}

void TimelineComponent::expandSelectionToGroups()
{
    // 選ばれているクリップのグループIDを集める
    juce::StringArray groupIds;

    for (const auto& ref : selectedClips)
    {
        const auto groupId = Track::getClipGroupId (findClipStateForRef (ref));

        if (groupId.isNotEmpty())
            groupIds.addIfNotAlreadyThere (groupId);
    }

    if (groupIds.isEmpty())
        return;

    // 同じグループのものを足す。**種別をまたいで探すこと**——
    // オーディオとMIDIは同じ`<CLIPS>`の子なので、一組にできる（8.78）
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            if (! groupIds.contains (Track::getClipGroupId (track.getClip (c).state)))
                continue;

            const auto ref = makeClipRef (t, c, false);

            if (ref.clipId.isNotEmpty()
                 && std::find (selectedClips.begin(), selectedClips.end(), ref) == selectedClips.end())
                selectedClips.push_back (ref);
        }

    }
}

bool TimelineComponent::canGroupSelectedClips() const
{
    return selectedClips.size() >= 2;
}

bool TimelineComponent::hasGroupedClipInSelection() const
{
    for (const auto& ref : selectedClips)
        if (Track::getClipGroupId (findClipStateForRef (ref)).isNotEmpty())
            return true;

    return false;
}

void TimelineComponent::groupSelectedClips()
{
    if (! canGroupSelectedClips())
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("2つ以上のクリップを選んでからまとめてください。"));

        return;
    }

    // **毎回新しいIDを振る。** 選んだものの中に既にグループがあっても、
    // 「いま選んでいるものが1つのグループになる」ほうが読めます
    // （古いグループの残りは、そのIDを持ったまま別のグループとして残る）
    const auto groupId = juce::Uuid().toString();

    project.beginAction (utf8 ("クリップのグループ化"));

    for (const auto& ref : selectedClips)
        Track::setClipGroupId (findClipStateForRef (ref), groupId, &project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::ungroupSelectedClips()
{
    if (! hasGroupedClipInSelection())
        return;

    // **選択はグループごと広がっている**ので（`expandSelectionToGroups()`）、
    // 1つ選んだ状態で解けば、そのグループが丸ごと解けます
    project.beginAction (utf8 ("クリップのグループ解除"));

    for (const auto& ref : selectedClips)
        Track::setClipGroupId (findClipStateForRef (ref), {}, &project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}


bool TimelineComponent::canMergeSelectedClips() const
{
    // 8.79：2つ以上・同じトラック・同じ種別（Phase 119）。
    // **あいだに挟まっているかどうかは、ここでは見ません**——
    // メニューを開くたびにクリップを全部なめることになるので、
    // 押されたときに確かめて理由を出すほうにしてあります
    if (selectedClips.size() < 2)
        return false;

    const auto& first = selectedClips.front();

    for (const auto& ref : selectedClips)
        if (ref.trackId != first.trackId || ref.isMidi != first.isMidi)
            return false;

    return true;
}

void TimelineComponent::mergeSelectedClips()
{
    auto fail = [this] (const juce::String& reason)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (reason);
    };

    if (! canMergeSelectedClips())
        return fail (utf8 ("同じトラックのクリップを2つ以上選んでからまとめてください。"));

    const auto trackId = selectedClips.front().trackId;
    const bool isMidi = selectedClips.front().isMidi;

    auto track = project.findTrackById (trackId);

    if (! track.state.getParent().isValid())
        return;

    // **開始時刻の順に並べる。** 選んだ順ではなく並びの順に取り込まないと、
    // `mergeClipWithNext()`が「後ろに何も無い」と言って止まります
    struct Entry { juce::String clipId; double startTime = 0.0; };
    std::vector<Entry> entries;

    for (const auto& ref : selectedClips)
    {
        auto clipState = findClipStateForRef (ref);

        // 8.137：**アクセサを通すこと**（Phase 175／1.27）
        if (clipState.isValid())
            entries.push_back ({ ref.clipId, AudioClip (clipState).getStartTime() });
    }

    if (entries.size() < 2)
        return;

    std::sort (entries.begin(), entries.end(),
                [] (const Entry& a, const Entry& b) { return a.startTime < b.startTime; });

    // 8.79：**あいだに選んでいないクリップが挟まっていないか**（Phase 119）。
    //
    // `mergeClipWithNext()`は「トラック上の次のクリップ」を取り込むので、
    // 確かめずに繰り返すと**選んでいないものまで飲み込みます**
    // （しかもUndoするまで気づけません）。
    constexpr double tolerance = 0.001;
    const double firstStart = entries.front().startTime;
    const double lastStart  = entries.back().startTime;

    // 8.94：**クリップはオーディオだけ**（Phase 134）
    const int numClips = isMidi ? 0 : track.getNumClips();

    for (int c = 0; c < numClips; ++c)
    {
        const auto clipId = track.getClip (c).getId();
        const double start = isMidi ? 0.0
                                     : track.getClip (c).getStartTime();

        if (start <= firstStart + tolerance || start >= lastStart - tolerance)
            continue;   // 選んだ範囲の外

        const bool selected = std::any_of (entries.begin(), entries.end(),
                                            [&clipId] (const Entry& e) { return e.clipId == clipId; });

        if (! selected)
            return fail (utf8 ("あいだに選んでいないクリップがあります（続いているものだけまとめられます）。"));
    }

    project.beginAction (utf8 ("選択クリップの結合"));

    juce::String reason;
    int mergedCount = 0;

    // **いつも先頭へ取り込む。** 1回ごとに後ろのクリップが消えるので、
    // 番号ではなくIDで引き直すこと（1.32）
    for (size_t i = 1; i < entries.size(); ++i)
    {
        auto firstState = findClipStateForRef ({ trackId, entries.front().clipId, isMidi });

        if (! firstState.isValid())
            break;

        if (! track.mergeClipWithNext (firstState, &project.getUndoManager(), reason))
            break;

        ++mergedCount;
    }

    if (mergedCount == 0)
        return fail (reason.isNotEmpty() ? reason : utf8 ("クリップをまとめられませんでした。"));

    // 途中で止まった場合も、まとめられたぶんは残す（そのほうが手戻りが少ない）
    if (mergedCount < (int) entries.size() - 1 && reason.isNotEmpty())
        fail (reason);

    // **消えたクリップを指したままにしない**（1.32）。分割のときと同じ扱い
    clearClipSelection();

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::drawClipGroupMarker (juce::Graphics& g, juce::Rectangle<int> bounds,
                                              const juce::ValueTree& clipState) const
{
    // 8.78：**グループに入っていることが見て分かるように**（Phase 118）。
    //
    // 選択が広がるのは押してみて初めて分かるので、印が無いと
    // 「なぜ隣まで動いたのか」が読めません。
    // クリップの名前や波形を隠さないよう、**左上の小さな三角**にしてあります
    if (bounds.getWidth() < 8 || bounds.getHeight() < 8)
        return;   // 細いクリップでは、印のほうが大きくなって邪魔になる

    if (Track::getClipGroupId (clipState).isEmpty())
        return;

    juce::Path marker;
    marker.startNewSubPath ((float) bounds.getX(), (float) bounds.getY());
    marker.lineTo ((float) bounds.getX() + 8.0f, (float) bounds.getY());
    marker.lineTo ((float) bounds.getX(), (float) bounds.getY() + 8.0f);
    marker.closeSubPath();

    // 設計書2.6：まとまりの合図はオレンジ（選択のパープルとぶつからない）
    g.setColour (AppColours::orange);
    g.fillPath (marker);
}

void TimelineComponent::drawClipTransposeMarker (juce::Graphics& g, juce::Rectangle<int> bounds,
                                                  int semitones, double stretch) const
{
    // 8.148：**上下させたことが見て分かるように**（Phase 186／本人の報告）。
    //
    // Phase 185では、値が見えるのは**そのクリップを選んでインスペクタを見たとき**
    // だけでした。並んでいるクリップのどれを動かしたのかが分かりません。
    //
    // 波形を隠さないよう、**左上の小さな数字**にしてあります。
    //
    // **右上にはしないこと。** クリップの幅は曲の長さぶんあるので、
    // 30秒のクリップは3000px近くになり、**右端は画面の外**です
    // （実際に、印が出ていないように見えました）。
    // 左上ならグループの印（8.78）と同じで、クリップの頭が見えていれば必ず出ます
    // 8.149：**伸縮も同じ札に出す**（Phase 187/8.48）。
    // 別々に出すと、両方効いているクリップで札が2つ並んで波形を隠します
    juce::String text;

    if (semitones != 0)
        text << (semitones > 0 ? "+" : "") << semitones;

    if (std::abs (stretch - 1.0) > 1.0e-9)
        text << (text.isEmpty() ? "" : " ") << "x" << juce::String (stretch, 2);

    if (text.isEmpty() || bounds.getHeight() < 14)
        return;

    const int wanted = 10 + 6 * text.length();

    if (bounds.getWidth() < wanted + 10)
        return;   // 細いクリップでは、印のほうが大きくなって邪魔になる

    // グループの三角（左上の8px）とぶつからないよう、そのぶん右へ寄せる
    auto area = bounds.removeFromTop (13).withTrimmedLeft (9).removeFromLeft (wanted);

    g.setColour (AppColours::background.withAlpha (0.75f));
    g.fillRect (area);

    // 設計書2.6：音程はパープル（音量・録音のオレンジと区別する）
    g.setColour (AppColours::purple);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText (text, area, juce::Justification::centred, false);
}

bool TimelineComponent::applyToSelectedAudioClips (const juce::String& actionName,
                                                    std::function<void (AudioClip&)> action)
{
    // 8.46：選んでいる**オーディオクリップだけ**に同じことをする（Phase 86）。
    // 反転もオートフェードも「選んだぶん全部」に効かせたいので、道は1本にしてある
    auto refs = selectedClips;

    if (refs.empty() && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
        refs.push_back (makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi));

    if (refs.empty() || action == nullptr)
        return false;

    project.beginAction (actionName);

    bool changed = false;

    for (const auto& ref : refs)
    {
        if (ref.isMidi)
            continue;   // MIDIクリップには当てはまらない

        auto track = project.findTrackById (ref.trackId);

        if (! track.state.getParent().isValid())
            continue;

        // **IDで引き直す**（番号は増減でずれる。1.32）
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);

            if (clip.getId() != ref.clipId)
                continue;

            action (clip);
            changed = true;
            break;
        }
    }

    if (! changed)
        return false;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

void TimelineComponent::changeSelectedClipGain (float deltaDb, bool resetToZero)
{
    auto refs = selectedClips;

    if (refs.empty() && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
        refs.push_back (makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi));

    if (refs.empty())
        return;

    project.beginAction (resetToZero ? utf8 ("クリップゲインを0dBへ")
                                      : utf8 ("クリップゲインの変更"));

    bool changed = false;

    for (const auto& ref : refs)
    {
        // **MIDIクリップにゲインは無い**（音量はトラックとベロシティの仕事）
        if (ref.isMidi)
            continue;

        auto track = project.findTrackById (ref.trackId);

        if (! track.state.getParent().isValid())
            continue;

        // **IDで引き直す**（番号は増減でずれる。1.32）
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);

            if (clip.getId() != ref.clipId)
                continue;

            clip.setGainDb (resetToZero ? 0.0f : clip.getGainDb() + deltaDb,
                             &project.getUndoManager());
            changed = true;
            break;
        }
    }

    if (! changed)
        return;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}
void TimelineComponent::deleteSelectedClips()
{
    auto refs = selectedClips;

    if (refs.empty() && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
        refs.push_back (makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi));

    if (refs.empty())
        return;

    project.beginAction (utf8 ("クリップの削除"));

    for (const auto& ref : refs)
    {
        auto track = project.findTrackById (ref.trackId);

        if (! track.state.getParent().isValid())
            continue;

        // **IDで引き直してから消すこと**：1つ消すたびに他の番号がずれる（1.32）
        if (ref.isMidi)
            continue;   // 8.94：クリップはオーディオだけ（Phase 134）

        {
            for (int c = track.getNumClips(); --c >= 0;)
                if (track.getClip (c).getId() == ref.clipId)
                    track.removeClip (track.getClip (c), &project.getUndoManager());
        }
    }

    clearSelection();
    clearClipSelection();

    if (onModelChanged != nullptr)
        onModelChanged();

    refresh();
}

void TimelineComponent::showClipMenu (const juce::MouseEvent& e)
{
    const int numSelected = juce::jmax (1, (int) selectedClips.size());
    const auto countSuffix = numSelected > 1 ? utf8 ("（") + juce::String (numSelected) + utf8 ("個）")
                                              : juce::String();
    const bool canPaste = (EditClipboard::getKind() == EditClipboard::Kind::clips);
    const double clickTime = xToTime (e.x);

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("カット") + countSuffix);
    menu.addItem (2, utf8 ("コピー") + countSuffix);
    menu.addItem (3, utf8 ("ここに貼り付け"), canPaste);
    menu.addSeparator();
    menu.addItem (4, utf8 ("複製"), onDuplicateClipRequested != nullptr);
    menu.addItem (5, utf8 ("削除") + countSuffix);

    // 8.76：**分割と結合**（Phase 116／改善案④㉟）。
    //
    // どちらも前からモデルにあり（`splitClipAt()`／`mergeClipWithNext()`）、
    // **ショートカットからしか呼べませんでした**。押せる場所が無いと、
    // 使えることに気づけません。
    //
    // 分割は**再生位置**で行います（クリックした場所ではありません）。
    // ショートカットと同じ動きにするためで、狙った場所で割りたいときは
    // **カットツールか、オーディオのダブルクリック**が別にあります（8.29の表）。
    //
    // **1つ選んでいるときだけ。** どちらも「選んでいるクリップ」に効く作りなので
    // （`getSelectedClipState()`）、複数選んでいるときに出すと
    // 「1つにしか効かなかった」という結果になります
    const bool singleClip = (numSelected == 1);

    menu.addSeparator();
    menu.addItem (12, utf8 ("カーソル位置で分割"), singleClip && onSplitClipRequested != nullptr);
    menu.addItem (13, utf8 ("次のクリップと結合"), singleClip && onMergeClipRequested != nullptr);

    // 8.79：**選んだぶんをまとめて1つに**（Phase 119）。
    // 上の「次のクリップと結合」は1回に1つずつなので、
    // 割ったものを戻すのに何度も押すことになっていました
    menu.addItem (16, utf8 ("選択クリップを結合") + countSuffix, canMergeSelectedClips());

    // 8.77：**トランスポーズ**（Phase 117／改善案㉝）。
    //
    // 8.147：**オーディオにも効きます**（Phase 185／改善案㉞）。
    // 項目を2つに分けていないのは、使う側から見て**やりたいことが同じ**だからです
    // ——選んで「トランスポーズ」。効かせる先の振り分けは
    // `transposeSelection()`が持っています（1.27）。
    //
    // 番号は100番台。上の項目と混ざらないよう、種類ごとに帯を分けてある
    // （サイドチェインのメニューが101番から始まっているのと同じ決まり。8.63）
    // 8.79：**オーディオにしか効かないものは、MIDIだけのときは押せなくする**（Phase 119）。
    //
    // クリップゲイン・逆再生・オートフェードは、どれも
    // `applyToSelectedAudioClips()`／`changeSelectedClipGain()`が
    // **MIDIクリップを飛ばす**作りです。押せてしまうと、
    // **何も起きないのが「効かない」のか「壊れている」のか分かりません**。
    //
    // **この2つは対です。片方だけ足すと、混ぜて選んだときにずれます**
    const bool hasMidiSelection = selectedIsMidi
                                   || std::any_of (selectedClips.begin(), selectedClips.end(),
                                                    [] (const ClipRef& ref) { return ref.isMidi; });

    const bool hasAudioSelection = ! selectedIsMidi
                                    || std::any_of (selectedClips.begin(), selectedClips.end(),
                                                     [] (const ClipRef& ref) { return ! ref.isMidi; });

    juce::PopupMenu transposeMenu;
    transposeMenu.addItem (101, utf8 ("半音上げる（+1）"));
    transposeMenu.addItem (102, utf8 ("半音下げる（-1）"));
    transposeMenu.addSeparator();
    transposeMenu.addItem (103, utf8 ("1オクターブ上げる（+12）"));
    transposeMenu.addItem (104, utf8 ("1オクターブ下げる（-12）"));

    // 8.147：**0へ戻すのはオーディオだけ**（Phase 185／改善案㉞）。
    // MIDIのノートは音程そのものを書き換えるので、「戻す」はUndoの仕事です
    transposeMenu.addSeparator();
    transposeMenu.addItem (105, utf8 ("トランスポーズを0へ戻す（オーディオ）"), hasAudioSelection);

    menu.addSubMenu (utf8 ("トランスポーズ") + countSuffix, transposeMenu,
                      hasMidiSelection || hasAudioSelection);

    // 8.149：**クリップ全体の伸縮**（Phase 187/8.48。仕様書5.5.1）。
    //
    // **端をAltでドラッグしても伸縮できます**が、そちらは**ぴったりには止まりません**。
    // ループ素材を曲のテンポへ合わせるのがいちばん多い用なので、小節で指せるようにしてある
    juce::PopupMenu stretchMenu;
    stretchMenu.addItem (111, utf8 ("1小節に合わせる"));
    stretchMenu.addItem (112, utf8 ("2小節に合わせる"));
    stretchMenu.addItem (113, utf8 ("4小節に合わせる"));
    stretchMenu.addItem (114, utf8 ("8小節に合わせる"));
    stretchMenu.addSeparator();
    stretchMenu.addItem (115, utf8 ("伸縮を戻す"));

    menu.addSubMenu (utf8 ("テンポに合わせる（伸縮）") + countSuffix, stretchMenu, hasAudioSelection);

    // 8.150：**ワープマーカー**（Phase 188／8.48。仕様書5.5.1）。
    //
    // 上の「テンポに合わせる」は**全体を一定の倍率で**伸縮します。
    // こちらは**曲の途中でテンポが揺れている素材**を直すためのもので、
    // 打点ごとに「この音を、この拍へ」と留めていきます。
    juce::PopupMenu warpMenu;
    warpMenu.addItem (121, utf8 ("ここにマーカーを置く"));
    warpMenu.addItem (122, utf8 ("ヒットポイントからマーカーを作る"));
    warpMenu.addSeparator();
    warpMenu.addItem (123, utf8 ("マーカーを全部消す"));

    menu.addSubMenu (utf8 ("ワープ") + countSuffix, warpMenu, hasAudioSelection);

    // 8.78：**クリップのグループ**（Phase 118／改善案㊱）。
    //
    // **上の「次のクリップと結合」とは別もの**なので、名前を分けてあります：
    //   結合   … 2つが1つのクリップになる（元に戻すには割る）
    //   グループ … クリップはそのまま。まとめて選ばれるようになるだけ
    menu.addSeparator();
    menu.addItem (14, utf8 ("クリップをグループ化") + countSuffix + " (G)", canGroupSelectedClips());
    menu.addItem (15, utf8 ("グループを解除"), hasGroupedClipInSelection());

    // 8.40：**ボリューム上げ下げ**（Phase 80）。8.29の積み残しのうちの1つ。
    // 細かく決めたいときはオーディオエディタで線をドラッグする（そちらが本体）
    menu.addSeparator();
    menu.addItem (6, utf8 ("ボリュームを上げる（+6dB）"), hasAudioSelection);
    menu.addItem (7, utf8 ("ボリュームを下げる（-6dB）"), hasAudioSelection);
    menu.addItem (8, utf8 ("ボリュームを0dBへ戻す"), hasAudioSelection);
    menu.addSeparator();

    // 8.46：8.29の表の積み残し、最後の2つ（Phase 86）
    menu.addItem (9, utf8 ("逆再生にする／戻す"), hasAudioSelection);
    menu.addItem (10, utf8 ("オートフェードをかける"), hasAudioSelection);
    menu.addSeparator();

    // 8.49：**オーディオエディタで開く**（Phase 88）。
    // MIDIクリップはダブルクリックで開くが、オーディオのダブルクリックは
    // 「その位置で分割」に使っている（8.29の表）ので、入口はメニューに置く
    menu.addItem (11, utf8 ("オーディオエディタで開く"),
                   ! selectedIsMidi && onAudioClipEditorRequested != nullptr);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
        [this, clickTime] (int result)
        {
            if (result == 1)        cutSelection();
            else if (result == 2)   copySelection();
            else if (result == 3)   pasteAt (clickTime);
            else if (result == 5)   deleteSelectedClips();
            else if (result == 6)   changeSelectedClipGain (6.0f, false);
            else if (result == 7)   changeSelectedClipGain (-6.0f, false);
            else if (result == 8)   changeSelectedClipGain (0.0f, true);
            else if (result == 9)
                applyToSelectedAudioClips (utf8 ("逆再生の切り替え"),
                                            [this] (AudioClip& clip)
                                            {
                                                clip.setReversed (! clip.isReversed(),
                                                                   &project.getUndoManager());
                                            });
            else if (result == 10)
                applyToSelectedAudioClips (utf8 ("オートフェード"),
                                            [this] (AudioClip& clip)
                                            {
                                                clip.applyAutoFade (&project.getUndoManager());
                                            });
            else if (result == 11 && onAudioClipEditorRequested != nullptr)
                onAudioClipEditorRequested (selectedTrackIndex, selectedClipIndex);
            else if (result == 4 && onDuplicateClipRequested != nullptr)
                onDuplicateClipRequested();   // **複製の実装はMainComponentに1つだけ**（8.32）
            else if (result == 12 && onSplitClipRequested != nullptr)
                onSplitClipRequested();       // 8.76：分割もMainComponentに1つだけ
            else if (result == 13 && onMergeClipRequested != nullptr)
                onMergeClipRequested();       // 8.76：結合も同上
            else if (result == 16)   mergeSelectedClips();   // 8.79（Phase 119）
            // 8.78：クリップのグループ（Phase 118／改善案㊱）
            else if (result == 14)   groupSelectedClips();
            else if (result == 15)   ungroupSelectedClips();
            // 8.77：トランスポーズ（Phase 117／改善案㉝）。
            // 8.147：**振り分けは`transposeSelection()`1箇所**（Phase 185／改善案㉞）
            else if (result == 101)  transposeSelection (1);
            else if (result == 102)  transposeSelection (-1);
            else if (result == 103)  transposeSelection (12);
            else if (result == 104)  transposeSelection (-12);
            else if (result == 105)  changeSelectedClipTranspose (0, true);
            // 8.149：クリップ全体の伸縮（Phase 187/8.48）
            else if (result == 111)  fitSelectedClipsToBars (1);
            else if (result == 112)  fitSelectedClipsToBars (2);
            else if (result == 113)  fitSelectedClipsToBars (4);
            else if (result == 114)  fitSelectedClipsToBars (8);
            else if (result == 115)  resetSelectedClipStretch();
            // 8.150：ワープマーカー（Phase 188／8.48）
            else if (result == 121)  addWarpMarkerAt (clickTime);
            else if (result == 122)  createWarpMarkersFromHitPoints();
            else if (result == 123)  clearWarpMarkers();
        });
}

void TimelineComponent::showToolMenu (const juce::MouseEvent& e)
{
    const auto current = getEditTool();

    juce::PopupMenu menu;
    menu.addSectionHeader (utf8 ("ツール"));
    menu.addItem (1, utf8 ("矢印（選択・移動／ドラッグで範囲選択）"), true, current == EditTool::arrow);
    // 8.120：アレンジ画面のペンはオートメーション行専用（Phase 155／改善案29・37）
    menu.addItem (2, utf8 ("ペン（オートメーション行のなぞり書き）"), true, current == EditTool::pencil);
    menu.addItem (4, utf8 ("カット（割る）"), true, current == EditTool::cut);
    menu.addItem (5, utf8 ("消しゴム（触れたものを消す）"), true, current == EditTool::eraser);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
        [this] (int result)
        {
            if (result <= 0)
                return;

            const auto tool = result == 2 ? EditTool::pencil
                            : result == 4 ? EditTool::cut
                            : result == 5 ? EditTool::eraser
                                          : EditTool::arrow;

            // **自分で切り替えず、上へ返す**（Phase 52の教訓）。ツールはピアノロールにも
            // 効くので、ここで`setEditTool()`を直接呼ぶとアレンジ画面だけが変わる
            if (onEditToolSelected != nullptr)
                onEditToolSelected (tool);
            else
                setEditTool (tool);   // 繋がっていない場合の保険
        });
}

void TimelineComponent::showRulerMenu (const juce::MouseEvent& e)
{
    // Phase 54：マーカーもループも共通のスナップに寄せる（8.14）。
    // 以前はマーカーが拍、ループが小節と別々だった。**ドラッグしたときと同じ刻み**に
    // なっている、という関係だけは変わっていない（どちらも`snapTime()`を通るため）。
    const double snappedTime = project.snapTime (xToTime (e.x));

    juce::PopupMenu menu;
    menu.addSectionHeader (utf8 ("マーカー"));
    menu.addItem (1, utf8 ("ここにマーカーを挿入"));
    menu.addItem (2, utf8 ("名前を付けてマーカーを挿入..."));
    menu.addSeparator();
    menu.addSectionHeader (utf8 ("ループ"));
    menu.addItem (3, utf8 ("ここをループの先頭にする"));
    menu.addItem (4, utf8 ("ここをループの終わりにする"));

    // Phase 68（8.29の表）：**帯のダブルクリックと同じことをメニューからも。**
    // ループが切れているとき、帯は薄く出るだけなので「どこを押せば入るのか」が
    // 分からない。チェックの付いた項目にして、今の状態も読めるようにしてある
    menu.addItem (5, utf8 ("ループを有効にする"), true, project.isLoopEnabled());
    menu.addSeparator();
    menu.addSectionHeader (utf8 ("目盛り"));
    menu.addItem (6, utf8 ("小節／拍で表示"), true, showBarsAndBeats);
    menu.addItem (7, utf8 ("時間（分：秒）で表示"), true, ! showBarsAndBeats);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
        [this, snappedTime] (int result)
        {
            switch (result)
            {
                case 1:
                case 2:
                    if (onInsertMarkerRequested != nullptr)
                        onInsertMarkerRequested (snappedTime, result == 2);
                    break;

                case 3:
                    project.beginAction (utf8 ("ループの先頭を設定"));
                    project.setLoopRange (snappedTime, project.getLoopEndTime(), &project.getUndoManager());
                    project.setLoopEnabled (true, &project.getUndoManager());

                    if (onLoopChanged != nullptr)
                        onLoopChanged();

                    repaint();
                    break;

                case 4:
                    project.beginAction (utf8 ("ループの終わりを設定"));
                    project.setLoopRange (project.getLoopStartTime(), snappedTime, &project.getUndoManager());
                    project.setLoopEnabled (true, &project.getUndoManager());

                    if (onLoopChanged != nullptr)
                        onLoopChanged();

                    repaint();
                    break;

                case 5:
                    project.beginAction (utf8 ("ループの入切"));
                    project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());

                    if (onLoopChanged != nullptr)
                        onLoopChanged();

                    repaint();
                    break;

                case 6:
                case 7:
                    // **表示の切り替えはUndoに積まない**（プロジェクトの中身ではなく
                    // 画面の見え方なので、履歴に混ざると邪魔になる。8.14の刻みと同じ扱い）
                    showBarsAndBeats = (result == 6);
                    timeFormatButton.setButtonText (showBarsAndBeats ? "Bars" : "Time");
                    repaint();
                    break;

                default:
                    break;
            }
        });
}

void TimelineComponent::renameMarker (Marker marker)
{
    if (! marker.state.isValid())
        return;

    // **入力欄は1箇所にまとめてある**（`NameEntry`。Phase 72）。
    // マーカー名・トラック名・ドラムのパート名で同じものを3回書いていた
    NameEntry::show (utf8 ("マーカーの名前"), utf8 ("新しい名前を入力してください。"),
                      marker.getName(),
                      [this, marker] (const juce::String& newName) mutable
                      {
                          // 入力欄を開いているあいだに消えている可能性がある（1.32）
                          if (! marker.state.getParent().isValid())
                              return;

                          project.beginAction (utf8 ("マーカーの名前の変更"));
                          marker.setName (newName, &project.getUndoManager());
                          repaint();
                      });
}

//==============================================================================
// 8.162：旗の上での名前の打ち直し（Phase 200／本人の要望）
//==============================================================================

void TimelineComponent::showMarkerNameEditor (int markerIndex)
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return;

    auto marker = project.getMarker (markerIndex);

    markerNameEditorId = marker.getId();
    markerNameEditor.setText (marker.getName(), juce::dontSendNotification);
    markerNameEditor.setBounds (getMarkerFlagBounds (markerIndex));
    markerNameEditor.setVisible (true);
    markerNameEditor.grabKeyboardFocus();
    markerNameEditor.selectAll();
}

void TimelineComponent::commitMarkerNameEdit()
{
    // **畳んでから書く。** 書く前に畳まないと、モデルの変更→再描画→
    // フォーカスが移る、で`onFocusLost`からもう一度ここへ入ってきます（1.5と同じ用心）
    const auto markerId = markerNameEditorId;
    const auto newName = markerNameEditor.getText().trim();

    cancelMarkerNameEdit();

    if (markerId.isEmpty() || newName.isEmpty())
        return;

    // **番号ではなくIDで引き直す**（1.32）。打っているあいだに旗が増減している
    for (int i = 0; i < project.getNumMarkers(); ++i)
    {
        auto marker = project.getMarker (i);

        if (marker.getId() != markerId)
            continue;

        if (newName == marker.getName())
            return;

        project.beginAction (utf8 ("マーカーの名前の変更"));
        marker.setName (newName, &project.getUndoManager());
        repaint();
        return;
    }
}

void TimelineComponent::cancelMarkerNameEdit()
{
    if (markerNameEditorId.isEmpty() && ! markerNameEditor.isVisible())
        return;

    markerNameEditorId.clear();
    markerNameEditor.setVisible (false);

    // **キーボードを画面へ返す**（返さないと、閉じた後もShift＋矢印などが欄へ行く）
    grabKeyboardFocus();
}

void TimelineComponent::updateMarkerNameEditorBounds()
{
    if (markerNameEditorId.isEmpty())
        return;

    for (int i = 0; i < project.getNumMarkers(); ++i)
        if (project.getMarker (i).getId() == markerNameEditorId)
        {
            markerNameEditor.setBounds (getMarkerFlagBounds (i));
            return;
        }

    // 打っているあいだに消された旗——書く先が無いので畳む
    cancelMarkerNameEdit();
}

//==============================================================================
// 仕様書5.1・5.9・5.11.1：小節バーのレーン（Phase 142・144／改善案㉒㉓㉔㉕）
//
// **レーンは2本**です（Phase 144。改善案リスト3の11・12）。
// 1本にまとめていたら札同士が重なって読めず、3本に分けるとルーラーが厚すぎる——
// **拍子とキーはどちらも小節にしか置けない**ので、1つの札にまとめました。
//
// **札の中身はProjectModelの表**（getTempoMap()／getKeyMap()）です。
// ここでValueTreeを辿らないこと——画面とメトロノームが同じ表を読むから、
// 食い違いようがありません（8.103・8.106）。
//
// **札は「位置」で指します**（SignatureMarkerRef）。表は位置の昇順に並べ替えられるので、
// 番号で覚えると**もう1つ足した瞬間に別のものを指します**（1.32）。
//==============================================================================

juce::Rectangle<int> TimelineComponent::getTempoLaneArea() const
{
    return { trackHeaderWidth, rulerNumbersBottom,
             juce::jmax (0, getWidth() - trackHeaderWidth), signatureLaneHeight };
}

juce::Rectangle<int> TimelineComponent::getMeterKeyLaneArea() const
{
    return getTempoLaneArea().translated (0, signatureLaneHeight);
}

juce::Rectangle<int> TimelineComponent::getLaneAreaFor (SignatureMarkerRef::Kind kind) const
{
    return (kind == SignatureMarkerRef::Kind::tempo) ? getTempoLaneArea() : getMeterKeyLaneArea();
}

juce::Rectangle<int> TimelineComponent::getSignatureStripArea() const
{
    return getTempoLaneArea().getUnion (getMeterKeyLaneArea());
}

/** 拍子かキーの変化点がある小節の一覧（昇順・重複なし）。

    **描くときと当たりを見るときで、同じものを通すこと**（8.2）。
    別々に集めると、両方ある小節を2回描いて「掴んだのに片方だけ動く」が起きます。 */
static std::vector<int> collectMeterKeyBars (const ProjectModel& project)
{
    std::vector<int> bars;

    for (const auto& change : project.getTempoMap().meterChanges)
        bars.push_back (change.bar);

    for (const auto& change : project.getKeyMap().changes)
        bars.push_back (change.bar);

    std::sort (bars.begin(), bars.end());
    bars.erase (std::unique (bars.begin(), bars.end()), bars.end());

    return bars;
}

juce::String TimelineComponent::getSignatureMarkerText (const SignatureMarkerRef& ref) const
{
    if (ref.kind == SignatureMarkerRef::Kind::tempo)
    {
        // **小数第1位まで**（120.0は"120"に見せたいが、118.5を"118"と出すと、
        // 打ち込んだ値と違うものが出ることになる）
        const double bpm = project.getTempoMap().getTempoAtBeat (ref.beatPosition);

        return juce::String (bpm, bpm == std::floor (bpm) ? 0 : 1);
    }

    if (ref.kind == SignatureMarkerRef::Kind::meterKey)
    {
        // 改善案12のとおり「4/4 A Minor」の形。**片方だけ変えた札にも両方出します**——
        // その小節から効いている設定を読むのが目的で、「何を変えたか」ではないため
        const double barStart = project.getBarStartTime (ref.bar);
        const auto key = project.getProjectKeyAt (barStart);

        return project.getTimeSignatureAt (barStart) + " "
                 + pitchClassName (key.root) + (key.minor ? " Minor" : " Major");
    }

    return {};
}

double TimelineComponent::getSignatureMarkerTime (const SignatureMarkerRef& ref) const
{
    if (signatureDragRef.isValid() && signatureDragRef.sameAs (ref))
        return signatureDragPreviewTime;

    if (ref.kind == SignatureMarkerRef::Kind::meterKey)
        return project.getBarStartTime (ref.bar);

    if (ref.kind == SignatureMarkerRef::Kind::tempo)
        return project.getTimeForBeatPosition (ref.beatPosition);

    return 0.0;
}

juce::Rectangle<int> TimelineComponent::getSignatureMarkerBounds (const SignatureMarkerRef& ref) const
{
    if (! ref.isValid())
        return {};

    auto lane = getLaneAreaFor (ref.kind);

    // **Font::getStringWidthFloat()はJUCE 9で無くなっている**（2章）。
    // 置き換えはGlyphArrangement::getStringWidth()
    const juce::Font font (juce::FontOptions ((float) signatureLaneHeight - 3.0f));
    const int textWidth = juce::jlimit (24, 130,
                                         (int) juce::GlyphArrangement::getStringWidth (
                                             font, getSignatureMarkerText (ref)) + 10);

    return { timeToX (getSignatureMarkerTime (ref)), lane.getY(), textWidth, lane.getHeight() };
}

TimelineComponent::SignatureMarkerRef TimelineComponent::findSignatureMarkerAt (juce::Point<int> position) const
{
    SignatureMarkerRef found;

    if (getTempoLaneArea().contains (position))
    {
        // **後ろから見る**（重なっているときは、右にあるほうが上に描かれる）
        const auto& changes = project.getTempoMap().tempoChanges;

        for (int i = (int) changes.size(); --i >= 0;)
        {
            SignatureMarkerRef ref;
            ref.kind = SignatureMarkerRef::Kind::tempo;
            ref.beatPosition = changes[(size_t) i].beatPosition;

            if (getSignatureMarkerBounds (ref).contains (position))
                return ref;
        }

        return found;
    }

    if (getMeterKeyLaneArea().contains (position))
    {
        const auto bars = collectMeterKeyBars (project);

        for (int i = (int) bars.size(); --i >= 0;)
        {
            SignatureMarkerRef ref;
            ref.kind = SignatureMarkerRef::Kind::meterKey;
            ref.bar = bars[(size_t) i];

            if (getSignatureMarkerBounds (ref).contains (position))
                return ref;
        }
    }

    return found;
}

void TimelineComponent::drawOneSignatureLane (juce::Graphics& g, SignatureMarkerRef::Kind kind,
                                               const juce::String& legend, juce::Colour colour)
{
    auto lane = getLaneAreaFor (kind);

    if (lane.isEmpty())
        return;

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (lane.withRight (getWidth()));

    // 帯の地。**変化点が1つも無くても出す**——出しておかないと、
    // 「ここに置ける」ことが画面から分かりません（1.9）
    g.setColour (AppColours::canvasAlt);
    g.fillRect (lane);

    // 曲の頭の値を左端に薄く出す。「いま何拍子で何BPMか」が、
    // 変化点を置く前から読めるようにしておく
    g.setColour (AppColours::textSecondary);
    g.setFont (juce::FontOptions ((float) signatureLaneHeight - 3.0f));
    g.drawText (legend, lane.withWidth (110).reduced (4, 0), juce::Justification::centredLeft, false);

    auto drawMarker = [&] (const SignatureMarkerRef& ref)
    {
        auto bounds = getSignatureMarkerBounds (ref);

        if (bounds.getRight() <= lane.getX() || bounds.getX() >= lane.getRight())
            return;

        const bool dragging = signatureDragRef.isValid() && signatureDragRef.sameAs (ref);

        g.setColour (dragging ? colour : colour.withAlpha (0.8f));
        g.fillRect (bounds);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions ((float) signatureLaneHeight - 3.0f));
        g.drawText (getSignatureMarkerText (ref), bounds.reduced (4, 0),
                     juce::Justification::centredLeft, false);
    };

    if (kind == SignatureMarkerRef::Kind::tempo)
    {
        for (const auto& change : project.getTempoMap().tempoChanges)
        {
            SignatureMarkerRef ref;
            ref.kind = SignatureMarkerRef::Kind::tempo;
            ref.beatPosition = change.beatPosition;
            drawMarker (ref);
        }

        return;
    }

    for (int bar : collectMeterKeyBars (project))
    {
        SignatureMarkerRef ref;
        ref.kind = SignatureMarkerRef::Kind::meterKey;
        ref.bar = bar;
        drawMarker (ref);
    }
}

void TimelineComponent::drawSignatureLanes (juce::Graphics& g)
{
    const auto key = project.getProjectKey();

    drawOneSignatureLane (g, SignatureMarkerRef::Kind::tempo,
                           juce::String (project.getTempo(), 0),
                           AppColours::tempoMarker);

    drawOneSignatureLane (g, SignatureMarkerRef::Kind::meterKey,
                           project.getTimeSignature() + " "
                             + pitchClassName (key.root) + (key.minor ? " Minor" : " Major"),
                           AppColours::timeSignatureMarker);
}

void TimelineComponent::showSignatureMarkerMenu (const SignatureMarkerRef& ref,
                                                  juce::Point<int> screenPosition)
{
    if (! ref.isValid())
        return;

    juce::PopupMenu menu;

    if (ref.kind == SignatureMarkerRef::Kind::tempo)
    {
        menu.addItem (1, utf8 ("テンポを変える"));
        menu.addSeparator();
        menu.addItem (9, utf8 ("この変更を削除"));
    }
    else
    {
        // **1つの札に2つの値**が乗っているので、項目も2つに分けます（改善案12）。
        // 「値を変える」1つにまとめると、どちらを変えるのか押すまで分かりません
        menu.addSectionHeader (juce::String (ref.bar + 1) + utf8 ("小節目"));
        menu.addItem (2, utf8 ("拍子を変える"));
        menu.addItem (3, utf8 ("キーを変える"), project.findChordTrack().state.getParent().isValid());
        menu.addSeparator();
        menu.addItem (9, utf8 ("この小節の変更を削除"));
    }

    menu.showMenuAsync (juce::PopupMenu::Options()
                          .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, ref, screenPosition] (int result)
        {
            if (result == 1)
            {
                editTempoChange (ref.beatPosition);
            }
            else if (result == 2)
            {
                editTimeSignatureChange (ref.bar);
            }
            else if (result == 3)
            {
                showKeyChangeMenu (ref.bar, screenPosition);
            }
            else if (result == 9)
            {
                if (ref.kind == SignatureMarkerRef::Kind::tempo)
                {
                    project.beginAction (utf8 ("テンポの変更を削除"));
                    project.removeTempoChange (ref.beatPosition, &project.getUndoManager());
                }
                else
                {
                    // **札1つ＝その小節の変更ぶん全部**（拍子もキーも）。
                    // 片方だけ残すと、消したはずの札が別の値で出続けます
                    project.beginAction (utf8 ("小節の変更を削除"));
                    project.removeTimeSignatureChange (ref.bar, &project.getUndoManager());
                    project.removeKeyChange (ref.bar, &project.getUndoManager());
                }

                repaint();
            }
        });
}

void TimelineComponent::showSignatureLaneMenu (SignatureMarkerRef::Kind kind, double timeSeconds,
                                                juce::Point<int> screenPosition)
{
    const int bar = project.getBarIndexAt (timeSeconds);
    const double beat = std::floor (project.getBeatPositionAt (timeSeconds) + 0.5);

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (bar + 1) + utf8 ("小節目"));

    if (kind == SignatureMarkerRef::Kind::tempo)
    {
        // **0拍目には置けない**（そこは曲の既定値＝フッターの担当）。
        // 灰色にして出しておくのは、**「押しても何も起きない」を避ける**ため（1.9）
        menu.addItem (1, utf8 ("テンポの変更をここに追加"), beat > 0.0);
    }
    else
    {
        menu.addItem (2, utf8 ("拍子の変更をここに追加"), bar > 0);

        // **コードトラックが無ければキーは置けません**（キーの置き場所がそこだから。8.106）。
        // フッターのKey欄も同じ理由で押せなくしてあります——**片方だけ押せると、
        // 押しても何も起きないボタンになります**（1.9・8.12の「入口が2つ」）
        const bool hasChordTrack = project.findChordTrack().state.getParent().isValid();

        menu.addItem (3, utf8 ("キーの変更をここに追加"), bar > 0 && hasChordTrack);
    }

    menu.showMenuAsync (juce::PopupMenu::Options()
                          .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, bar, beat, screenPosition] (int result)
        {
            if (result == 1)
                editTempoChange (beat);
            else if (result == 2)
                editTimeSignatureChange (bar);
            else if (result == 3)
                showKeyChangeMenu (bar, screenPosition);
        });
}

void TimelineComponent::editTempoChange (double beatPosition)
{
    // **0拍目は曲の既定値（フッターのBPM）の担当**
    if (! (beatPosition > 0.0))
        return;

    NameEntry::show (utf8 ("テンポの変更"),
                      utf8 ("この位置からのテンポ（BPM）を入力してください。"),
                      juce::String (project.getTempoMap().getTempoAtBeat (beatPosition), 1),
                      [this, beatPosition] (const juce::String& text)
                      {
                          const double bpm = text.getDoubleValue();

                          // フッターのBPM欄と同じ範囲にすること（入口が2つある。8.12）
                          if (bpm < 20.0 || bpm > 300.0)
                              return;

                          project.beginAction (utf8 ("テンポの変更"));
                          project.setTempoChange (beatPosition, bpm, &project.getUndoManager());
                          repaint();
                      },
                      utf8 ("BPM"));
}

void TimelineComponent::editTimeSignatureChange (int bar)
{
    // **0小節目は曲の既定値（フッターの拍子）の担当**
    if (bar <= 0)
        return;

    NameEntry::show (utf8 ("拍子の変更"),
                      juce::String (bar + 1) + utf8 ("小節目からの拍子を入力してください（例：3/4）。"),
                      project.getTimeSignatureAt (project.getBarStartTime (bar)),
                      [this, bar] (const juce::String& text)
                      {
                          // **検証してから区切ること**（Phase 145）。先に`beginAction()`を
                          // 呼ぶと、受け付けられない値を入れたときに**空のUndo区切り**が残ります
                          if (! project.isValidTimeSignature (text))
                              return;   // 受け付けられない値は捨てる（フッターと同じ決まり）

                          project.beginAction (utf8 ("拍子の変更"));
                          project.setTimeSignatureChange (bar, text, &project.getUndoManager());
                          repaint();
                      },
                      utf8 ("拍子"));
}

void TimelineComponent::showKeyChangeMenu (int bar, juce::Point<int> screenPosition)
{
    if (bar <= 0)
        return;

    // **打ち込ませず、選ばせます。** "Am"／"A minor"／"a" のどれで書くかを
    // 決めさせる意味がないうえ、綴りを外したときに何も起きないのが分かりにくい（1.9）。
    // 選択肢にすれば、**受け付けられない値そのものが無くなります**
    const auto current = project.getProjectKeyAt (project.getBarStartTime (bar));

    juce::PopupMenu majorMenu, minorMenu;

    for (int root = 0; root < 12; ++root)
    {
        majorMenu.addItem (root + 1, pitchClassName (root) + " Major", true,
                            ! current.minor && current.root == root);
        minorMenu.addItem (root + 101, pitchClassName (root) + " Minor", true,
                            current.minor && current.root == root);
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (juce::String (bar + 1) + utf8 ("小節目からのキー"));
    menu.addSubMenu ("Major", majorMenu);
    menu.addSubMenu ("Minor", minorMenu);

    menu.showMenuAsync (juce::PopupMenu::Options()
                          .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, bar] (int result)
        {
            if (result <= 0)
                return;

            Scale key;
            key.minor = (result >= 101);
            key.root = key.minor ? (result - 101) : (result - 1);

            project.beginAction (utf8 ("キーの変更"));

            // **コードトラックが無ければ何も起きません**（`setKeyChange()`がfalseを返す）。
            // フッターのKey欄と同じ決まりで、片方だけ緩くしないこと（8.106）
            if (project.setKeyChange (bar, key, &project.getUndoManager()))
                repaint();
        });
}

void TimelineComponent::updateSignatureDrag (const juce::MouseEvent& e)
{
    if (! signatureDragRef.isValid())
        return;

    const double time = juce::jmax (0.0, xToTime (e.x));

    // **拍子とキーは小節へ、テンポは拍へ寄せる**（置ける場所がそれぞれ決まっている）。
    // ここでスナップ設定に従わせないのは、**1/16へ寄った拍子の変わり目**が
    // 表現できない位置だからです（`TempoMap.h`）
    if (signatureDragRef.kind == SignatureMarkerRef::Kind::meterKey)
    {
        const int bar = project.getBarIndexAt (time);
        const double thisBarStart = project.getBarStartTime (bar);
        const double nextBarStart = project.getBarStartTime (bar + 1);

        signatureDragPreviewTime = ((time - thisBarStart) <= (nextBarStart - time)) ? thisBarStart
                                                                                    : nextBarStart;
    }
    else
    {
        const double beat = std::floor (project.getBeatPositionAt (time) + 0.5);
        signatureDragPreviewTime = project.getTimeForBeatPosition (juce::jmax (0.0, beat));
    }

    repaint();
}

void TimelineComponent::commitSignatureDrag()
{
    if (! signatureDragRef.isValid())
        return;

    const auto ref = signatureDragRef;
    const double time = signatureDragPreviewTime;

    // **先に掴んでいる印を外す。** 外す前にモデルを変えると、
    // 描き直しがプレビュー位置を読んで、動かしたものが二重に見えます
    signatureDragRef = {};

    if (ref.kind == SignatureMarkerRef::Kind::meterKey)
    {
        const int newBar = project.getBarIndexAt (time);

        if (newBar == ref.bar || newBar <= 0)
        {
            repaint();
            return;
        }

        // **札1つぶん＝その小節にあるものを全部**まとめて動かす。
        // 片方だけ動かすと、1つに見えていた札が2つに割れます
        const auto& tempoMap = project.getTempoMap();
        const auto& keyMap = project.getKeyMap();

        juce::String movedSignature;
        bool hasSignature = false;

        for (const auto& change : tempoMap.meterChanges)
            if (change.bar == ref.bar)
            {
                movedSignature = project.getTimeSignatureAt (project.getBarStartTime (ref.bar));
                hasSignature = true;
            }

        Scale movedKey;
        bool hasKey = false;

        for (const auto& change : keyMap.changes)
            if (change.bar == ref.bar)
            {
                movedKey = change.key;
                hasKey = true;
            }

        project.beginAction (utf8 ("小節の変更を移動"));

        if (hasSignature)
        {
            project.removeTimeSignatureChange (ref.bar, &project.getUndoManager());
            project.setTimeSignatureChange (newBar, movedSignature, &project.getUndoManager());
        }

        if (hasKey)
        {
            project.removeKeyChange (ref.bar, &project.getUndoManager());
            project.setKeyChange (newBar, movedKey, &project.getUndoManager());
        }
    }
    else
    {
        const double newBeat = std::floor (project.getBeatPositionAt (time) + 0.5);

        if (juce::approximatelyEqual (newBeat, ref.beatPosition) || ! (newBeat > 0.0))
        {
            repaint();
            return;
        }

        const double bpm = project.getTempoMap().getTempoAtBeat (ref.beatPosition);

        project.beginAction (utf8 ("テンポの変更を移動"));
        project.removeTempoChange (ref.beatPosition, &project.getUndoManager());
        project.setTempoChange (newBeat, bpm, &project.getUndoManager());
    }

    repaint();
}

void TimelineComponent::drawLoopRange (juce::Graphics& g)
{
    const bool dragging = (loopDragMode != LoopDragMode::none);

    const double startTime = dragging ? loopDragPreviewStart : project.getLoopStartTime();
    const double endTime   = dragging ? loopDragPreviewEnd   : project.getLoopEndTime();

    auto strip = getLoopStripArea();

    // 帯の地。範囲があってもなくても出しておくことで、「ここを引けばループになる」
    // ことが分かるようにする
    g.setColour (AppColours::border.withAlpha (0.35f));
    g.fillRect (strip);

    if (endTime <= startTime)
        return;

    const int startX = timeToX (startTime);
    const int endX = timeToX (endTime);

    if (endX <= strip.getX() || startX >= strip.getRight())
        return;

    const int clippedStartX = juce::jmax (strip.getX(), startX);
    const int clippedEndX = juce::jmin (strip.getRight(), endX);

    // ループが切れているときは色を落とす。**範囲は残したまま切れる**ので、
    // 「範囲が消えた」と誤解しないようにする
    const bool enabled = project.isLoopEnabled() || dragging;
    const auto colour = enabled ? AppColours::orange : AppColours::textSecondary;

    g.setColour (colour);
    g.fillRect (clippedStartX, strip.getY(), clippedEndX - clippedStartX, strip.getHeight());

    // タイムライン側にも薄く敷いて、どこが繰り返されるのかを分かるようにする
    g.setColour (colour.withAlpha (enabled ? 0.10f : 0.05f));
    g.fillRect (clippedStartX, rulerHeight,
                 clippedEndX - clippedStartX, juce::jmax (0, getHeight() - rulerHeight));
}

void TimelineComponent::updateChordDrag (const juce::MouseEvent& e)
{
    if (chordDragMode == ChordDragMode::none)
        return;

    auto track = project.getTrack (chordDragTrackIndex);

    if (! juce::isPositiveAndBelow (chordDragRegionIndex, track.getNumChordRegions()))
        return;

    // Phase 54：寄せ先は共通のスナップ設定（8.14）。以前はここだけ拍で固定していた。
    //
    // **最短の長さは1拍のまま**にしてある。スナップを1/32にしたときに
    // 1/32のコード区間を作れても使いみちが無く、誤操作で潰れるほうが困るため
    // （「作るものの大きさ」と「置く位置の刻み」を分ける考え方。8.14）。
    const double minimumLength = getBeatSeconds (chordDragOriginalStart);

    auto snap = [this] (double seconds)
    {
        return project.snapTime (seconds);
    };

    // 8.128：**動かせるのは隣の旗と旗のあいだ**（Phase 164／改善案13）。
    //
    // Phase 163までは「前の区間の終わり」を下限にしていました。旗にしたことで
    // **区間は隙間なく並ぶ**ので、「前の区間の終わり」＝自分の始まりになり、
    // **左へ1pxも動かせなくなります**。見るのは隣の**旗の位置**です。
    //
    // **隣は時刻で探すこと。** 区間の並び順は時刻順とは限りません（足した順に入る）。
    // 番号の隣を見ると、飛び越したところで止まらなくなります
    double previousStart = 0.0;
    double nextStart = std::numeric_limits<double>::max();

    for (int r = 0; r < track.getNumChordRegions(); ++r)
    {
        if (r == chordDragRegionIndex)
            continue;

        const double other = track.getChordRegion (r).getStartTime();

        if (other < chordDragOriginalStart - 1.0e-6)
            previousStart = juce::jmax (previousStart, other);
        else if (other > chordDragOriginalStart + 1.0e-6)
            nextStart = juce::jmin (nextStart, other);
    }

    const double deltaSeconds = (double) (e.x - chordDragStartMousePosition.x) / pixelsPerSecond;

    const double lowest = (previousStart > 0.0) ? previousStart + minimumLength : 0.0;
    const double highest = (nextStart == std::numeric_limits<double>::max())
                              ? std::numeric_limits<double>::max()
                              : nextStart - minimumLength;

    const double wanted = snap (chordDragOriginalStart + deltaSeconds);

    chordDragPreviewStart = juce::jlimit (lowest, juce::jmax (lowest, highest), wanted);

    // 長さは掴んだときのまま持ち回す（描画の予告に使うだけ）。
    // **本当の長さは離した後に`normaliseChordRegions()`が決めます**
    chordDragPreviewLength = chordDragOriginalLength;

    repaint();
}

void TimelineComponent::commitChordDrag()
{
    if (chordDragMode == ChordDragMode::none)
        return;

    auto track = project.getTrack (chordDragTrackIndex);

    if (juce::isPositiveAndBelow (chordDragRegionIndex, track.getNumChordRegions()))
    {
        auto region = track.getChordRegion (chordDragRegionIndex);

        const bool moved = ! juce::approximatelyEqual (chordDragPreviewStart, chordDragOriginalStart)
                        || ! juce::approximatelyEqual (chordDragPreviewLength, chordDragOriginalLength);

        // 掴んだだけで動かしていないなら、Undoに空のステップを積まない
        if (moved)
        {
            project.beginAction (utf8 ("コード区間の移動"));
            track.setChordRegionTime (region, chordDragPreviewStart, chordDragPreviewLength,
                                       &project.getUndoManager());

            // 8.128：**動かしたら並べ直す**（Phase 164／改善案13）。
            // 動いた旗の前後が、そのぶん伸び縮みします
            track.normaliseChordRegions (&project.getUndoManager());
        }
    }

    chordDragMode = ChordDragMode::none;
    chordDragTrackIndex = -1;
    chordDragRegionIndex = -1;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::addChordRegionAt (int trackIndex, int x)
{
    auto track = project.getTrack (trackIndex);

    if (track.getType() != TrackType::Chord)
        return;

    // 8.44：**畳んでいるあいだは置かない**（Phase 84）。
    // 見えない場所にコードが増えると、鳴っている理由が分からなくなる
    if (track.isCollapsed())
        return;

    // Phase 54：置く位置は共通のスナップに従う（8.14）。以前は小節頭で固定していた。
    // **手前の目盛りへ**寄せるのは、クリックしたマスの頭から作りたいため
    // （丸めると、マスの後ろ半分をクリックしたときに次のマスへ入る）。
    const double startTime = project.snapTimeDown (xToTime (x));

    // 1小節ぶんの長さにする。コード進行は小節単位で置くことがほとんどなので、
    // 既定を秒ではなく小節にしておくと、テンポを変えても意図が保たれる。
    // **長さは「置く場所の小節」で測る**（8.98／Phase 138）——曲の頭の小節ではない
    const double secondsPerBar = project.getBarSecondsAt (startTime);

    // 8.128：**同じ位置に旗が既にあるときだけ断る**（Phase 164／改善案13）。
    //
    // Phase 163までは「1小節ぶんが既存の区間と重なるなら置かない」でした。
    // 旗にしたことで区間は隙間なく並ぶので、**その判定だとどこにも置けません**
    // （曲じゅうがどれかの区間の中）。旗は好きなところに立てられるべきで、
    // 長さは後から`normaliseChordRegions()`が決めます
    for (int r = 0; r < track.getNumChordRegions(); ++r)
        if (std::abs (track.getChordRegion (r).getStartTime() - startTime) < 1.0e-6)
            return;

    const auto key = project.getProjectKeyAt (startTime);
    Chord chord;
    chord.root = key.degreeRoot (0);
    chord.type = key.diatonicSeventh (0);

    project.beginAction (utf8 ("コード区間の追加"));
    track.addChordRegion (chord, startTime, secondsPerBar, &project.getUndoManager());

    // 8.128：**置いたら並べ直す**（Phase 164／改善案13）。
    // 手前の区間が、新しい旗の手前までに縮みます
    track.normaliseChordRegions (&project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

bool TimelineComponent::trackTypeHasSoloMute (TrackType type)
{
    // VCAはリンク先へまとめて効かせるためのソロ／ミュートを持つ（仕様書5.2.4）。
    // 8.51：**フォルダも持つ**（Phase 90）。中身をまとめて黙らせる／ソロにできる。
    // コードトラックだけが持たない（音を通さないため）
    return type != TrackType::Chord;
}

juce::Rectangle<int> TimelineComponent::getHeaderControlRow (int rowIndex) const
{
    // **低い行には下段が無い**（コードトラック）。空の矩形を返すので、
    // 下のボタンの`contains()`はどこでもfalseになり、押しようがなくなる
    const int trackArea = getTrackAreaHeight (rowIndex);

    if (trackArea < trackRowHeight)
        return {};

    const int rowY = getTrackRowY (rowIndex);

    // 8.50：フォルダの中のトラックは、下段も字下げに合わせる（Phase 89）。
    // **描画と当たり判定が同じ関数から出る**ので、片方だけずれることはない
    const int indent = getHeaderIndent (rowIndex);

    // 8.64：**高さは行に合わせる**（Phase 102）。
    //
    // `trackRowHeight`（固定値）で返していたので、**行を高くしても帯は伸びず**、
    // メーターの下端が行の下端から離れていました（8.62で行の高さを変えられるようにした）。
    // ここを行の高さから求めておけば、中身（`TrackHeaderControls`）は
    // **全高を取るだけ**で付いてきます
    return { headerColourBandWidth + headerControlLeftMargin + indent,
             rowY + headerNameRowHeight,
             trackHeaderWidth - headerColourBandWidth - headerControlLeftMargin - indent,
             trackArea - headerNameRowHeight };
}

juce::Colour TimelineComponent::getTrackColour (int rowIndex) const
{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return AppColours::purple;

    auto track = project.getTrack (rowIndex);

    // 設計書2.4／2.6：VCAだけは固定でオレンジ。**音声を持たない制御専用トラック**であることを、
    // 他のトラックと並んでいても取り違えないようにするため（色帯と同じ判断）
    if (track.getType() == TrackType::VCA)
        return AppColours::orange;

    return juce::Colour::fromString (track.getColourString());
}

//==============================================================================
// 8.126：トラックヘッダーの複数選択（Phase 162／改善案35）

bool TimelineComponent::isTrackInSelection (const juce::String& trackId) const
{
    return std::find (selectedTrackIds.begin(), selectedTrackIds.end(), trackId)
            != selectedTrackIds.end();
}

int TimelineComponent::getTrackIndexById (const juce::String& trackId) const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getId() == trackId)
            return t;

    return -1;
}

void TimelineComponent::setSingleTrackSelection (const juce::String& trackId)

{
    selectedTrackIds.clear();

    if (trackId.isNotEmpty())
        selectedTrackIds.push_back (trackId);
}

void TimelineComponent::selectTrackRangeTo (int trackIndex)
{
    // 8.154：起点からここまでを、まとめて選ぶ（Phase 192／本人の要望）
    const int anchorIndex = getTrackIndexById (trackSelectionAnchorId);

    if (anchorIndex < 0 || ! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    // **上へ向かって引いても同じ**（どちらが先でも、間を全部）
    const int from = juce::jmin (anchorIndex, trackIndex);
    const int to   = juce::jmax (anchorIndex, trackIndex);

    selectedTrackIds.clear();

    // **種類で選り分けていません。** 間にフォルダやセンドがあっても入ります——
    // 「1から3まで」と言われて2だけ飛ばされるほうが分かりにくい。
    // まとめて効く操作（音量・ソロ・ミュート）は、
    // それぞれが自分に関係ないトラックを弾いています（8.126）
    for (int i = from; i <= to; ++i)
        selectedTrackIds.push_back (project.getTrack (i).getId());
}

void TimelineComponent::toggleTrackInSelection (const juce::String& trackId)
{
    if (trackId.isEmpty())
        return;

    auto it = std::find (selectedTrackIds.begin(), selectedTrackIds.end(), trackId);

    if (it == selectedTrackIds.end())
    {
        selectedTrackIds.push_back (trackId);
        return;
    }

    // **最後の1本は外させない。** 何も選んでいない状態を
    // ヘッダーのクリックから作ると、インスペクタが空になる理由が分からない
    if (selectedTrackIds.size() > 1)
        selectedTrackIds.erase (it);
}

void TimelineComponent::pruneTrackSelection()
{
    for (auto it = selectedTrackIds.begin(); it != selectedTrackIds.end();)
    {
        if (project.findTrackById (*it).state.getParent().isValid())
            ++it;
        else
            it = selectedTrackIds.erase (it);
    }
}

void TimelineComponent::nudgeSelectedTrackVolumes (const juce::String& originTrackId, float deltaDb)
{
    // **動かした本人はもう書き換わっている**（`TrackHeaderControls`が先に書く）ので、
    // ここでは触らない。二重に足すと、掴んだ1本だけ倍動く
    if (selectedTrackIds.size() < 2 || ! isTrackInSelection (originTrackId)
         || juce::approximatelyEqual (deltaDb, 0.0f))
        return;

    pruneTrackSelection();

    // **区切りは作らない。** `TrackHeaderControls`が掴んだ時点で作っているので、
    // ここで作るとドラッグ1回がUndo何十回にもなります（3.1）
    for (const auto& id : selectedTrackIds)
    {
        if (id == originTrackId)
            continue;

        auto track = project.findTrackById (id);

        if (! track.state.getParent().isValid())
            continue;

        // Consoleのフェーダーと同じ範囲に収める（外れた値を書かない）
        track.setVolumeDb (juce::jlimit (-60.0f, 6.0f, track.getVolumeDb() + deltaDb),
                            &project.getUndoManager());
    }
}

void TimelineComponent::applyMuteToSelection (const juce::String& originTrackId, bool shouldBeMuted)
{
    if (selectedTrackIds.size() < 2 || ! isTrackInSelection (originTrackId))
        return;

    pruneTrackSelection();

    for (const auto& id : selectedTrackIds)
    {
        if (id == originTrackId)
            continue;

        auto track = project.findTrackById (id);

        // **押した1本と同じ状態に揃える**（それぞれ反転させない）。
        // 反転だと、ばらばらに鳴っているものがそのままばらばらに入れ替わって、
        // 「まとめて黙らせた」ことにならない
        if (track.state.getParent().isValid() && trackTypeHasSoloMute (track.getType()))
            track.setMuted (shouldBeMuted, &project.getUndoManager());
    }
}

void TimelineComponent::applySoloToSelection (const juce::String& originTrackId, bool shouldBeSoloed)
{
    if (selectedTrackIds.size() < 2 || ! isTrackInSelection (originTrackId))
        return;

    pruneTrackSelection();

    for (const auto& id : selectedTrackIds)
    {
        if (id == originTrackId)
            continue;

        auto track = project.findTrackById (id);

        if (track.state.getParent().isValid() && trackTypeHasSoloMute (track.getType()))
            track.setSoloed (shouldBeSoloed, &project.getUndoManager());
    }
}

juce::Rectangle<int> TimelineComponent::getHeaderColourBandBounds (int rowIndex) const

{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return {};

    // 描いているのと同じ形（`paint()`の`headerBounds.removeFromLeft()`）。
    // **ここで数を書き直さないこと**——描く場所と押せる場所がずれます
    return { getHeaderIndent (rowIndex), getTrackRowY (rowIndex),
             headerColourBandWidth, getTrackAreaHeight (rowIndex) };
}

void TimelineComponent::showTrackColourPalette (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (rowIndex);
    const auto trackId = track.getId();

    auto palette = std::make_unique<TrackColourPalette> (getTrackColour (rowIndex));

    // 8.160：**まとめて選んでいるなら、その全部に付ける**（Phase 198／本人の要望）。
    //
    // 相手の決め方はヘッダーの削除・フォルダ入れと同じです（8.159）——
    // **押したトラックがまとめ選択に入っていれば全部、入っていなければ押した1本だけ。**
    // 選んでいないトラックの色まで変わるのは驚きです。
    //
    // **IDで捕まえ直すこと。** パレットが開いているあいだに構成が変わると、
    // 番号は別のトラックを指し得ます（1.32）
    juce::StringArray targetIds;

    if (isTrackInSelection (trackId))
    {
        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            const auto id = project.getTrack (t).getId();

            if (isTrackInSelection (id))
                targetIds.add (id);
        }
    }
    else
    {
        targetIds.add (trackId);
    }

    palette->onColourChosen = [this, targetIds] (juce::Colour colour)
    {
        project.beginAction (utf8 ("トラックカラーの変更"));

        for (const auto& id : targetIds)
        {
            auto target = project.findTrackById (id);

            if (target.state.getParent().isValid())
                target.state.setProperty (IDs::trackColor, colour.toString(),
                                           &project.getUndoManager());
        }

        // **開いたままにしない。** 色を選んだら用は済んでいるので、
        // 押すたびに閉じるほうが手数が少ない
        if (auto* box = juce::Component::getCurrentlyModalComponent())
            box->exitModalState (0);

        repaint();
    };

    juce::CallOutBox::launchAsynchronously (std::move (palette),
                                             localAreaToGlobal (getHeaderColourBandBounds (rowIndex)),
                                             nullptr);
}

void TimelineComponent::setTrackHeaderWidth (int newWidth)

{
    const int clamped = juce::jlimit (minTrackHeaderWidth, maxTrackHeaderWidth, newWidth);

    if (trackHeaderWidth == clamped)
        return;

    trackHeaderWidth = clamped;

    // 8.125：**中身も置きなおすこと**（Phase 161／改善案38）。
    // ヘッダーのフェーダーとメーターは子コンポーネント（`TrackHeaderControls`）なので、
    // 描き直すだけでは元の幅のまま残ります
    layoutHeaderControls();
    updateScrollBars();
    repaint();
}

bool TimelineComponent::isOnHeaderResizeEdge (juce::Point<int> position) const
{
    return position.x >= trackHeaderWidth - headerResizeGrabMargin
            && position.x <= trackHeaderWidth + headerResizeGrabMargin;
}

juce::Rectangle<int> TimelineComponent::getTrackResizeGrabBounds (int rowIndex) const

{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return {};

    const int trackArea = getTrackAreaHeight (rowIndex);

    // 畳んだフォルダの中（高さ0）と、畳んだ行は伸縮させない
    // （畳んだものを引き伸ばせると、「畳む」の意味が読めなくなる）
    if (trackArea < collapsedRowHeight * 2)
        return {};

    const int bottom = getTrackRowY (rowIndex) + trackArea;

    // **トラックの領域の下端の内側**を掴ませる。外側（下）まで含めると、
    // レーンのヘッダー（8.56）やすぐ下の行と掴み合いになる
    return { 0, bottom - trackResizeGrabMargin, trackHeaderWidth, trackResizeGrabMargin };
}

juce::Rectangle<int> TimelineComponent::getTrackNameBounds (int rowIndex) const
{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return {};

    const int trackArea = getTrackAreaHeight (rowIndex);

    if (trackArea <= 0)
        return {};   // 畳んだフォルダの中（居ないのと同じ）

    // 8.51：**字下げはヘッダーの枠ごと縮めてある**（Phase 90）。
    // 色帯のぶんも外した残りが、名前の使える範囲
    const int left = getHeaderIndent (rowIndex) + headerColourBandWidth;

    // **下段が無い行（コードトラック・畳んだ行）では、名前を行の縦中央に置く。**
    // 上段の位置に固定すると、低い行では上に寄って見える
    const bool hasControlRow = ! getHeaderControlRow (rowIndex).isEmpty();
    const int height = hasControlRow ? headerNameRowHeight : trackArea;

    auto area = juce::Rectangle<int> (left, getTrackRowY (rowIndex), trackHeaderWidth - left, height);

    // 右端は「i」まで（録音待機中はさらにINのぶん手前まで）
    int rightLimit = area.getRight() - 6;

    if (auto inspectorBounds = getInspectorButtonBounds (rowIndex); ! inspectorBounds.isEmpty())
        rightLimit = inspectorBounds.getX() - 4;

    if (project.getTrack (rowIndex).getType() == TrackType::Audio
         && project.getTrack (rowIndex).isArmed())
        if (auto monitorBounds = getMonitorButtonBounds (rowIndex); ! monitorBounds.isEmpty())
            rightLimit = monitorBounds.getX() - 4;

    // 畳む三角のぶんだけ左を空ける（8.44）
    auto triangleBounds = getCollapseTriangleBounds (rowIndex);
    const int leftTrim = triangleBounds.isEmpty() ? 4
                                                   : triangleBounds.getRight() + 4 - area.getX();

    return area.withRight (juce::jmax (area.getX(), rightLimit)).withTrimmedLeft (leftTrim);
}

juce::Rectangle<int> TimelineComponent::getHeaderButtonRow (int rowIndex) const
{
    auto row = getHeaderControlRow (rowIndex);

    if (row.isEmpty())
        return {};

    // 8.61：**ボタンは2段目だけ**（Phase 99）。3段目は音量フェーダーが使う
    return row.withHeight (juce::jmin (row.getHeight(), TrackHeaderControls::buttonRowHeight));
}

juce::Rectangle<int> TimelineComponent::getHeaderControlsBounds (int rowIndex) const
{
    // 8.65：**名前の行も含めた、トラックの領域まるごと**（Phase 103）。
    //
    // `getHeaderControlRow()`（名前の行より下）に置いていたので、
    // **メーターの上端が名前のぶんだけ下がって**いました。行の高さいっぱいに
    // 伸ばすため、器のほうを上まで広げてあります。
    //
    // 名前・「i」・INは親が描きますが、部品側は`setInterceptsMouseClicks(false, true)`
    // なので透けて見え、クリックも通ります（8.61と同じ仕掛け）
    const int trackArea = getTrackAreaHeight (rowIndex);

    if (trackArea < trackRowHeight)
        return {};   // 低い行（コードトラック・畳んだ行）には入らない

    const int indent = getHeaderIndent (rowIndex);
    const int left = headerColourBandWidth + headerControlLeftMargin + indent;

    return { left, getTrackRowY (rowIndex), trackHeaderWidth - left - 3, trackArea };
}

/** 帯の中で、左から`offsetFromLeft`の位置に幅`width`・高さ`height`の部品を置く。
    高さは帯の縦中央に揃える（部品ごとに高さが違っても、目線が1本に揃う）。 */
static juce::Rectangle<int> placeInControlRow (juce::Rectangle<int> row, int offsetFromLeft,
                                                int width, int height)
{
    if (row.isEmpty())
        return {};

    return { row.getX() + offsetFromLeft,
             row.getY() + (row.getHeight() - height) / 2,
             width, height };
}

juce::Rectangle<int> TimelineComponent::getArmButtonBounds (int trackIndex) const
{
    // 設計書2.3.1：2段目の並びは左から Rec / A / S / M（Phase 58。8.61でパンが抜けた）。
    // **位置は固定**：出ない部品があっても隣が寄ってこないので、押し間違えない
    return placeInControlRow (getHeaderButtonRow (trackIndex), 0,
                               headerButtonSize, headerButtonSize);
}

juce::Rectangle<int> TimelineComponent::getAutomationButtonBounds (int trackIndex) const
{
    return placeInControlRow (getHeaderButtonRow (trackIndex), headerButtonSize + 4,
                               headerButtonSize, headerButtonSize);
}

juce::Rectangle<int> TimelineComponent::getSoloButtonBounds (int trackIndex) const
{
    return placeInControlRow (getHeaderButtonRow (trackIndex), (headerButtonSize + 4) * 2 + 4,
                               headerSoloMuteWidth, headerButtonSize);
}

juce::Rectangle<int> TimelineComponent::getMuteButtonBounds (int trackIndex) const
{
    return placeInControlRow (getHeaderButtonRow (trackIndex),
                               (headerButtonSize + 4) * 2 + 4 + headerSoloMuteWidth + 2,
                               headerSoloMuteWidth, headerButtonSize);
}

juce::Rectangle<int> TimelineComponent::getMonitorButtonBounds (int trackIndex) const
{
    // **Phase 58で上段（名前の行）の右端へ移した。** 下段に置くと、録音待機の
    // 入切のたびに部品が増減して、隣のボタンの位置が変わってしまう。
    //
    // 8.60：**「i」が右端に入ったので、その左隣へずらした**（Phase 97）。
    // 位置は「i」から求めるので、片方を動かしてももう片方が重ならない
    const int width = 22;
    const int height = 14;
    auto inspectorBounds = getInspectorButtonBounds (trackIndex);

    if (inspectorBounds.isEmpty())
        return {};

    return { inspectorBounds.getX() - width - 4,
             inspectorBounds.getCentreY() - height / 2,
             width, height };
}

juce::Rectangle<int> TimelineComponent::getInspectorButtonBounds (int rowIndex) const
{
    // 8.60：**トラックの行にだけ出す**（Phase 97／改善案①）。
    // マスター行はインスペクタに出るものが無い
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return {};

    const int trackArea = getTrackAreaHeight (rowIndex);

    // 畳んだフォルダの中（高さ0）と、押せる高さの無い行には出さない
    if (trackArea < inspectorButtonHeight + 2)
        return {};

    // **低い行（コードトラック・畳んだ行）では、行の縦中央**。
    // 上段の位置に固定すると、名前と同じように上へ寄って見える
    const int nameRowHeight = juce::jmin (headerNameRowHeight, trackArea);

    // 8.65：**メーターのぶんだけ左へ寄せる**（Phase 103）。
    // メーターが行の上端まで伸びたので、右端に置いたままだと重なり、
    // 「i」を押したつもりでピークがリセットされます。
    // **低い行にはメーターが出ない**（`getHeaderControlsBounds()`が空を返す）ので、
    // そのときは右端のまま
    const int meterColumn = getHeaderControlsBounds (rowIndex).isEmpty() ? 0
                                                                        : headerMeterColumnWidth;

    return { trackHeaderWidth - inspectorButtonWidth - 6 - meterColumn,
             getTrackRowY (rowIndex) + (nameRowHeight - inspectorButtonHeight) / 2,
             inspectorButtonWidth, inspectorButtonHeight };
}

void TimelineComponent::drawHeaderChip (juce::Graphics& g, juce::Rectangle<int> bounds,
                                         const juce::String& text, bool isOn, juce::Colour onColour)
{
    if (bounds.isEmpty())
        return;

    g.setColour (isOn ? onColour : AppColours::background);
    g.fillRoundedRectangle (bounds.toFloat(), AppColours::corner (3.0f));
    g.setColour (isOn ? onColour : AppColours::border);
    g.drawRoundedRectangle (bounds.toFloat(), AppColours::corner (3.0f), 1.0f);

    // 塗りつぶしたときだけ白文字。**地の色に合わせて自動では決めない**
    // （パープル／オレンジの上は、ライトでもダークでも白が読みやすい。1.34）
    g.setColour (isOn ? juce::Colours::white : AppColours::textSecondary);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (text, bounds, juce::Justification::centred);
}

void TimelineComponent::drawFolderSummaryBlock (juce::Graphics& g, int trackIndex)
{
    // 8.201：**フォルダの中身を1本の帯で示す**（Phase 235／改善案5の10）。
    //
    // **見た目だけです**（本人の指定：「機能面はいらない」）。
    // 掴めず、動かせず、選べません——**動かせるように見せない**のが肝で、
    // 押して初めて「これは飾りだった」と分かるのがいちばん困ります（8.161）。
    //
    // 描くのは「中身の音が鳴っている範囲」で、**畳んでいてもいなくても同じ**です。
    // 畳んだときにこそ要るものですが、開いているときに消すと
    // **畳んだ瞬間に無かったものが現れる**ことになります。

    auto folder = project.getTrack (trackIndex);

    const auto ids = project.getFolderDescendantIds (folder.getId());

    if (ids.isEmpty())
        return;   // 空のフォルダには何も描かない（**入っていないことが分かる**のが正しい）

    //--------------------------------------------------------------------------
    // 中身の範囲を数える。**塊ごとに描きます**——
    // いちばん端から端まで1本にすると、間が空いていても詰まって見えます

    struct Span { double start = 0.0; double end = 0.0; };

    std::vector<Span> spans;

    for (const auto& id : ids)
    {
        auto track = project.findTrackById (id);

        if (! track.state.getParent().isValid())
            continue;

        // オーディオはクリップ、MIDIはノートの塊（アレンジ画面が描いているものと同じ）
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip (c);
            spans.push_back ({ clip.getStartTime(), clip.getStartTime() + clip.getLength() });
        }

        // **`getNoteBlocks()`を使うこと**（1.27）。ここで自前に数え直すと、
        // トラック行の四角とフォルダの帯で切れ目が違う、という形で食い違います
        for (const auto& block : track.getNoteBlocks (project.getNoteBlockGapSeconds()))
            spans.push_back ({ block.startTime, block.endTime });
    }

    if (spans.empty())
        return;

    // 重なっているもの・くっついているものは1つにまとめる
    std::sort (spans.begin(), spans.end(),
                [] (const Span& a, const Span& b) { return a.start < b.start; });

    std::vector<Span> merged;

    for (const auto& span : spans)
    {
        if (! merged.empty() && span.start <= merged.back().end)
        {
            merged.back().end = juce::jmax (merged.back().end, span.end);
            continue;
        }

        merged.push_back (span);
    }

    //--------------------------------------------------------------------------
    // 描く

    const int rowY = getTrackRowY (trackIndex);
    const int rowHeight = getTrackAreaHeight (trackIndex);

    if (rowHeight <= 0)
        return;

    // **上下に余白を取って、行より細く**。トラック行のクリップと同じ高さで描くと、
    // 掴めるものに見えます
    const int inset = juce::jmax (3, rowHeight / 5);
    const auto colour = getTrackColour (trackIndex);

    for (const auto& span : merged)
    {
        const int x1 = timeToX (span.start);
        const int x2 = timeToX (span.end);

        auto bounds = juce::Rectangle<int> (x1, rowY + inset,
                                             juce::jmax (2, x2 - x1), rowHeight - inset * 2);

        // ヘッダーの下と、画面の外へは描かない
        if (bounds.getRight() < trackHeaderWidth || bounds.getX() > getWidth())
            continue;

        bounds = bounds.getIntersection ({ trackHeaderWidth, rowY,
                                            getWidth() - trackHeaderWidth, rowHeight });

        if (bounds.isEmpty())
            continue;

        // **薄く塗って、枠は少し濃く。** クリップより控えめにして、
        // 「これは中身の写しであって、クリップそのものではない」ことを見た目で分ける
        g.setColour (colour.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds.toFloat(), AppColours::corner (3.0f));

        g.setColour (colour.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), AppColours::corner (3.0f), 1.0f);
    }
}
void TimelineComponent::drawChordRegionsForTrack (juce::Graphics& g, int trackIndex)
{
    auto track = project.getTrack (trackIndex);

    // 8.44：**畳んでいるときは中身を描かない**（Phase 84／C12）。
    // 低い行に押し込めて描くと、読めないうえに掴めてしまう
    if (track.isCollapsed())
        return;

    for (int r = 0; r < track.getNumChordRegions(); ++r)
    {
        // ドラッグ中の区間は、モデルではなくプレビューの位置に描く（Phase 45）。
        // モデルを書き換えるのは離したときなので、ここを分けないと動いて見えない。
        if (chordDragMode != ChordDragMode::none
             && trackIndex == chordDragTrackIndex && r == chordDragRegionIndex)
        {
            const int x = timeToX (chordDragPreviewStart);
            const int width = juce::jmax (4, (int) (chordDragPreviewLength * pixelsPerSecond));

            drawChordRegion (g, { x, getTrackRowY (trackIndex) + 4, width, getTrackAreaHeight (trackIndex) - 8 },
                              track.getChordRegion (r));
            continue;
        }

        drawChordRegion (g, getChordRegionBounds (trackIndex, r), track.getChordRegion (r));
    }
}

void TimelineComponent::drawPinnedChordRow (juce::Graphics& g)
{
    const int pinned = getPinnedRowIndex();

    if (pinned < 0)
        return;

    auto track = project.getTrack (pinned);
    auto rowBounds = juce::Rectangle<int> (0, getTrackRowY (pinned), getWidth(), getTrackAreaHeight (pinned));

    // **地を塗り直してから描く。** 上へスクロールしてきた行がこの帯まで来ているので、
    // 塗らないとクリップの端が透けて見える（ルーラーが同じことをしているのと同じ理由）
    g.setColour (AppColours::canvas);
    g.fillRect (rowBounds);

    auto headerBounds = rowBounds.removeFromLeft (trackHeaderWidth);

    const bool isSelectedTrack = (pinned == selectedTrackIndex);

    g.setColour (isSelectedTrack ? AppColours::purple.withAlpha (0.18f) : AppColours::panel);
    g.fillRect (headerBounds);
    g.setColour (isSelectedTrack ? AppColours::purple : AppColours::border);
    g.drawRect (headerBounds, isSelectedTrack ? 2 : 1);

    auto colourBand = headerBounds.removeFromLeft (headerColourBandWidth).reduced (0, 2);
    g.setColour (juce::Colour::fromString (track.getColourString()));
    g.fillRect (colourBand);

    drawTrackHeaderContents (g, pinned, track);

    // コード区間は、タイムライン部分だけに描く（ヘッダーへ食い込ませない）
    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (getTimelineArea().withY (getTrackRowY (pinned))
                                              .withHeight (getTrackAreaHeight (pinned)));
        drawChordRegionsForTrack (g, pinned);
    }

    // **プレイヘッドを引き直す。** 本体のプレイヘッドはこの帯より前に描かれているので、
    // 塗り直したぶんが消えている（1本の線が固定行のところだけ途切れて見える）
    const int playheadX = timeToX (playheadSeconds);

    if (playheadX >= trackHeaderWidth)
    {
        g.setColour (AppColours::orange);
        g.drawLine ((float) playheadX, (float) getTrackRowY (pinned),
                     (float) playheadX, (float) (getTrackRowY (pinned) + getTrackAreaHeight (pinned)), 2.0f);
    }

    g.setColour (AppColours::border);
    g.drawRect (juce::Rectangle<int> (0, getTrackRowY (pinned), getWidth(), getTrackAreaHeight (pinned)));
}

juce::Rectangle<int> TimelineComponent::getCollapseTriangleBounds (int rowIndex) const
{
    // 8.44：畳む／開くの三角（Phase 84／C12）。**行の左端、名前の手前**に置く。
    // 8.50：フォルダトラックも同じ場所を使う（Phase 89／D2）
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return {};

    const auto type = project.getTrack (rowIndex).getType();

    if (type != TrackType::Chord && type != TrackType::Folder)
        return {};

    const int rowY = getTrackRowY (rowIndex);
    const int height = getTrackAreaHeight (rowIndex);

    if (height <= 0)
        return {};   // 畳んだフォルダの中（居ないのと同じ）

    return { headerColourBandWidth + 3 + getHeaderIndent (rowIndex),
             rowY + (height - collapseTriangleSize) / 2,
             collapseTriangleSize, collapseTriangleSize };
}

int TimelineComponent::getHeaderIndent (int rowIndex) const
{
    // 8.50：**フォルダの中のトラックは右へ寄せる**（Phase 89／D2。設計書2.4）。
    // 色帯は動かさない——**どのトラックかを示すもの**なので、
    // 階層で位置が変わると目で追いにくい
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return 0;

    return project.getTrackFolderDepth (project.getTrack (rowIndex)) * headerIndentPerLevel;
}

void TimelineComponent::drawCollapseTriangle (juce::Graphics& g, juce::Rectangle<int> bounds,
                                               bool isCollapsed) const
{
    if (bounds.isEmpty())
        return;

    // **文字ではなく形で描く**（フォントに字が無いことがある。1.30）。
    // 畳んでいるときは右向き（「この先に何かある」）、開いているときは下向き
    juce::Path triangle;
    const auto area = bounds.toFloat();

    if (isCollapsed)
    {
        triangle.startNewSubPath (area.getX(), area.getY());
        triangle.lineTo (area.getRight(), area.getCentreY());
        triangle.lineTo (area.getX(), area.getBottom());
    }
    else
    {
        triangle.startNewSubPath (area.getX(), area.getY());
        triangle.lineTo (area.getRight(), area.getY());
        triangle.lineTo (area.getCentreX(), area.getBottom());
    }

    triangle.closeSubPath();

    g.setColour (AppColours::textSecondary);
    g.fillPath (triangle);
}
void TimelineComponent::drawTrackHeaderContents (juce::Graphics& g, int rowIndex, const Track& track)
{
    const auto type = track.getType();
    const bool hasControlRow = ! getHeaderControlRow (rowIndex).isEmpty();

    //==========================================================================
    // 上段：トラック名と、「i」・録音待機中だけ出るINボタン

    // 8.60：**「i」でインスペクタを開く**（Phase 97／改善案①）。
    //
    // それまでは、トラックの設定を触るのに**フッターのInspectorボタンまで目線を運ぶ**
    // 必要がありました。用があるのはヘッダーを見ているときなので、そこに置いています。
    // **選択も同時に移す**ので、押したトラックの設定がそのまま出ます
    drawHeaderChip (g, getInspectorButtonBounds (rowIndex), "i", false, AppColours::purple);

    if (type == TrackType::Audio && track.isArmed())
    {
        // 仕様書5.4：入力モニタリング（Phase 26でヘッダーへ、Phase 58で上段へ）。
        // **録音待機中のトラックにだけ出す。** モニターは「今これから録る音を聞く」
        // ためのものなので、録り先が決まっていないトラックに出しても意味がない
        const bool monitoring = isInputMonitoringEnabled != nullptr && isInputMonitoringEnabled();

        drawHeaderChip (g, getMonitorButtonBounds (rowIndex), "IN", monitoring, AppColours::orange);
    }

    // 8.44：畳む／開くの三角（Phase 84／C12）。**名前の手前**に置き、そのぶん名前を寄せる
    auto triangleBounds = getCollapseTriangleBounds (rowIndex);

    if (! triangleBounds.isEmpty())
        drawCollapseTriangle (g, triangleBounds, track.isCollapsed());

    // 8.61：**名前の場所は`getTrackNameBounds()`が決める**（Phase 99／改善案⑤）。
    // その場で編集する入力欄も同じ関数を通すので、開いたときにずれない
    g.setColour (AppColours::textPrimary);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (track.getName() + "  [" + trackTypeToString (type) + "]",
                 getTrackNameBounds (rowIndex), juce::Justification::centredLeft, false);

    if (! hasControlRow)
        return;   // 低い行はここまで（下段が入らない）

    //==========================================================================
    // 2段目の左半分：Rec / A / S / M
    //
    // 8.61：**パン・音量・メーターは`TrackHeaderControls`が持っている**（Phase 99）。
    // あちらは`setInterceptsMouseClicks(false, true)`なので、この4つは透けて見え、
    // クリックも今までどおりここへ届く

    // 仕様書5.4：録音待機（レコードアーム）。設計書2.6にならい録音関連はオレンジ。
    // 8.146：**MIDIトラックにも出す**（Phase 184／改善案⑬a）
    if (trackTypeCanArm (type))
    {
        auto armBounds = getArmButtonBounds (rowIndex).toFloat();

        g.setColour (track.isArmed() ? AppColours::orange : AppColours::background);
        g.fillEllipse (armBounds);
        g.setColour (track.isArmed() ? AppColours::orange : AppColours::border);
        g.drawEllipse (armBounds, 1.5f);
    }

    // 仕様書5.6：オートメーションのレーンを開く「A」ボタン（Phase 26）
    if (trackTypeHasAudioPath (type))
        drawHeaderChip (g, getAutomationButtonBounds (rowIndex), "A",
                         track.getNumVisibleAutomationLanes() > 0, AppColours::purple);

    // 仕様書5.2.1：ソロ／ミュート（Phase 58）。**Consoleと同じ配色**にしてある
    // （ミュート＝オレンジ、ソロ＝パープル）ので、どちらの画面でも同じに見える
    if (trackTypeHasSoloMute (type))
    {
        drawHeaderChip (g, getSoloButtonBounds (rowIndex), "S", track.isSoloed(), AppColours::purple);
        drawHeaderChip (g, getMuteButtonBounds (rowIndex), "M", track.isMuted(), AppColours::orange);
    }
}

juce::Point<int> TimelineComponent::getFadeHandlePosition (juce::Rectangle<int> bounds, double fadeInSeconds,
                                                             double fadeOutSeconds, bool fadeIn) const
{
    if (fadeIn)
    {
        const int x = bounds.getX() + (int) (fadeInSeconds * pixelsPerSecond);
        return { x, bounds.getY() };
    }

    const int x = bounds.getRight() - (int) (fadeOutSeconds * pixelsPerSecond);
    return { x, bounds.getY() };
}

bool TimelineComponent::hitTestClip (juce::Point<int> position, int& trackIndexOut, int& clipIndexOut,
                                       bool& isMidiOut) const
{
    // ヘッダーやスクロールバーの上をクリップとして判定しないようにする
    if (! getTimelineArea().contains (position))
        return false;

    const int trackIndex = getTrackIndexForY (position.y);

    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return false;

    auto track = project.getTrack (trackIndex);

    // 8.91：**MIDIは「ノートの塊」で当たりを取る**（Phase 131）。
    // `clipIndexOut`は塊の番号です（データの番号ではない）
    if (track.getType() == TrackType::Midi)
    {
        const auto blocks = getNoteBlocksFor (trackIndex);

        for (size_t b = 0; b < blocks.size(); ++b)
        {
            if (getNoteBlockBounds (trackIndex, blocks[b]).contains (position))
            {
                trackIndexOut = trackIndex;
                clipIndexOut = (int) b;
                isMidiOut = true;
                return true;
            }
        }

        return false;
    }

    if (track.getType() != TrackType::Audio)
        return false;

    for (int c = 0; c < track.getNumClips(); ++c)
    {
        if (getClipBounds (trackIndex, c).contains (position))
        {
            trackIndexOut = trackIndex;
            clipIndexOut = c;
            isMidiOut = false;
            return true;
        }
    }

    return false;
}

void TimelineComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    // 8.129：**マーカーで選んだ区間は、外を押したら解除する**（Phase 165／本人の要望）。
    //
    // Phase 164までは**解除する手立てがありませんでした**。1トラックの範囲は
    // 空いている場所を押すと引き直しが始まって畳まれますが（`clearTimeRange()`）、
    // 全トラックの区間は**どの行の上にも枠がある**ので、その経路を通りません。
    //
    // **枠の中を押したときだけ残すこと。** 中は掴んで動かすための場所なので、
    // ここで消すと動かせなくなります。
    //
    // **いちばん先に置いてよい。** 消すだけで、押した先の処理はそのまま続きます
    // （旗をもう一度押したときは、この後の分岐が選び直します）
    if (hasTimeRange && timeRangeAllTracks && ! draggingTimeRange)
    {
        const int rowUnderMouse = getTrackIndexForY (e.y);

        if (! isTimeRangeAt (rowUnderMouse, xToTime (e.x)))
            clearTimeRange();
    }

    // 8.125：**ヘッダーとアレンジの境目を掴んで幅を変える**（Phase 161／改善案38）。
    //
    // **いちばん先に見ること。** この4pxの帯は、上はルーラー・下はヘッダーと
    // タイムラインの両方にまたがっているので、後ろに置くとどれかに先に取られます。
    // 細いので、他の操作を邪魔することはありません
    if (! e.mods.isPopupMenu() && isOnHeaderResizeEdge (e.getPosition()))
    {
        headerResizing = true;
        headerResizeStartScreenX = e.getScreenPosition().x;
        headerResizeStartWidth = trackHeaderWidth;
        return;
    }

    // 仕様書5.9：ルーラー（小節バー）の右クリックメニュー（Phase 50）。
    // **ループの帯とマーカーの旗も含めたルーラー全体で出す。**
    // 「どこを右クリックしたか」で出たり出なかったりすると、
    // メニューがあること自体に気づけない。旗の上だけは旗のメニューが優先される。
    //
    // **Phase 142：拍子とテンポのレーンも先に外す**（そこは専用のメニューが出る）
    if (e.mods.isPopupMenu() && e.y < rulerHeight && e.x >= trackHeaderWidth
         && findMarkerAt (e.getPosition()) < 0
         && ! getSignatureStripArea().contains (e.getPosition()))
    {
        showRulerMenu (e);
        return;
    }

    // 仕様書5.1・5.9・5.11.1：小節バーのレーン（Phase 142・144／改善案㉒㉓㉔㉕）。
    // **シークより先に判定すること**（レーンはルーラーの中にある）
    if (getSignatureStripArea().contains (e.getPosition()))
    {
        const auto ref = findSignatureMarkerAt (e.getPosition());

        if (e.mods.isPopupMenu())
        {
            if (ref.isValid())
                showSignatureMarkerMenu (ref, e.getScreenPosition());
            else
                showSignatureLaneMenu (getTempoLaneArea().contains (e.getPosition())
                                         ? SignatureMarkerRef::Kind::tempo
                                         : SignatureMarkerRef::Kind::meterKey,
                                        juce::jmax (0.0, xToTime (e.x)), e.getScreenPosition());

            return;
        }

        if (ref.isValid())
        {
            // 左クリックは掴んで移動（マーカーの旗と同じ操作感）。
            // **ここでシークしない**：札は「その位置から効く」印であって、
            // 再生位置を指すものではありません
            signatureDragRef = ref;
            signatureDragPreviewTime = getSignatureMarkerTime (ref);
            repaint();
        }

        return;
    }


    // 仕様書5.9：ループ範囲の帯（Phase 48）。
    // **シークの判定より先に見ること。** 帯はルーラーの中にあるので、
    // 順番が逆だとここを掴んでも再生位置が動くだけになる。
    if (getLoopStripArea().contains (e.getPosition()))
    {
        const double startTime = project.getLoopStartTime();
        const double endTime = project.getLoopEndTime();
        const bool hasRange = (endTime > startTime);

        loopDragPreviewStart = startTime;
        loopDragPreviewEnd = endTime;

        // 既にある範囲の端を掴んだら伸縮、それ以外は引き直し（クリップと同じ操作感）
        if (hasRange && std::abs (e.x - timeToX (startTime)) <= edgeGrabMargin)
        {
            loopDragMode = LoopDragMode::moveStart;
        }
        else if (hasRange && std::abs (e.x - timeToX (endTime)) <= edgeGrabMargin)
        {
            loopDragMode = LoopDragMode::moveEnd;
        }
        else
        {
            loopDragMode = LoopDragMode::create;

            // **掴んだ側の端も寄せること**（Phase 54）。ここを寄せ忘れると、
            // 引いた範囲の片方だけが目盛りから外れる（8.14）
            loopDragAnchorTime = project.snapTime (xToTime (e.x));
            loopDragPreviewStart = loopDragAnchorTime;
            loopDragPreviewEnd = loopDragAnchorTime;
        }

        updateLoopDrag (e);
        return;
    }

    // 仕様書5.9：マーカーの旗（Phase 49）。
    // **ループの帯と同じく、シークより先に判定すること。**
    {
        const int markerIndex = findMarkerAt (e.getPosition());

        if (markerIndex >= 0)
        {
            if (e.mods.isPopupMenu())
            {
                showMarkerMenu (markerIndex, e.getScreenPosition());
                return;
            }

            // 左クリックはその位置へジャンプ。掴んだままならドラッグで移動する
            markerDragIndex = markerIndex;
            markerDragPreviewTime = project.getMarker (markerIndex).getTime();

            // **マーカーの時刻そのものへ動かす**（Phase 54）。
            // ここで画面の座標を経由すると、スナップが効いて旗と再生位置がずれる
            seekToTime (markerDragPreviewTime);

            // 8.124：**旗から次の旗までを、全トラックで選ぶ**（Phase 159／改善案5）。
            //
            // 飛ぶのと選ぶのを1回のクリックでやっています。曲の区切りへ行くのと、
            // その区切りを触るのは、たいてい続けてやることなので。
            //
            // **Deleteで区間ぜんぶが消えます**が、`beginAction`で1つに
            // まとめてあるのでCtrl+Zで1回戻せます
            selectRangeFromMarker (markerIndex);

            repaint();
            return;
        }
    }

    // 仕様書5.9：ルーラーのクリックで再生位置を決める（Phase 18）。
    // クリップのヒットテストより先に判定する（ルーラーはクリップの上にあるため）。
    if (e.y < rulerHeight && e.x >= trackHeaderWidth)
    {
        dragMode = DragMode::Seek;
        seekToX (e.x);
        return;
    }

    // 仕様書5.4：トラックヘッダーの録音待機ボタン。
    // クリップのヒットテストより先に判定する（ヘッダー領域はクリップと重ならないが、
    // 判定順を明示しておくほうが後から読んだときに迷わない）。
    if (e.x < trackHeaderWidth)
    {
        const int headerTrackIndex = getTrackIndexForY (e.y);

        // 設計書2.3.1：末尾の「+ 新しいトラック」（Phase 31）。
        // **他のヘッダー判定より先に見ること。** この行の行番号は、オートメーションを
        // 開いていないときは`getNumTracks()`と一致し、下のマスター行の判定に化ける。
        if (getAddTrackRowBounds().contains (e.getPosition()))
        {
            if (onAddTrackClicked != nullptr)
                onAddTrackClicked (localAreaToGlobal (getAddTrackRowBounds()));

            return;
        }

        // 8.62：**行の高さを変える（下端のドラッグ）**（Phase 100）。
        //
        // **レーンの判定より先に見ること。** レーンを開いていると、
        // トラックの領域のすぐ下がレーンのヘッダーになる。掴める帯は
        // **トラックの領域の内側**に取ってあるので重なりはしないが、
        // 順番を先にしておくほうが「どちらが優先か」を読み違えない
        if (! e.mods.isPopupMenu()
             && getTrackResizeGrabBounds (headerTrackIndex).contains (e.getPosition()))
        {
            dragMode = DragMode::ResizeTrack;
            resizeTrackIndex = headerTrackIndex;
            resizeTrackStartY = e.y;

            // **元の高さを控える**（いまの高さに足すと、ドラッグのたびに二重に動く）
            resizeTrackStartHeight = getTrackAreaHeight (headerTrackIndex);
            return;
        }

        // 8.125：**左端の色帯を左クリックでカラーパレット**（Phase 161／改善案26）。
        //
        // **レーンの判定より先に見ること。** 色帯はトラックの行にしか無く、
        // レーンの行の左端は別のもの（レーンの色は行の右クリックから変える）。
        //
        // **右クリックは通すこと**：ヘッダーの右クリックメニューは
        // 帯の上でも出てほしい（出たり出なかったりするほうが分かりにくい）
        if (! e.mods.isPopupMenu()
             && juce::isPositiveAndBelow (headerTrackIndex, project.getNumTracks())
             && getHeaderColourBandBounds (headerTrackIndex).contains (e.getPosition()))
        {
            showTrackColourPalette (headerTrackIndex);
            return;
        }

        // 8.56：**オートメーションの行のヘッダー**（Phase 94／D3）。
        //
        // **トラックヘッダーの判定より先に見ること。** レーンの行はトラック行の
        // 「中」にあるので（`getRowHeight()`が含んでいる）、`getTrackIndexForY()`は
        // 親のトラックを返す。先に見ないと、レーンのヘッダーを押したつもりで
        // トラックのソロ／ミュートに当たる
        if (const auto laneRow = findAutomationRowAtY (e.y); laneRow.isValid())
        {
            const auto targetId = getAutomationTargetFor (laneRow);

            // 8.59：**右クリックはメニュー**（Phase 96）。8.29の表のとおり、
            // **ボタンの判定より先に見ること**（右クリックでボタンを押せてしまわないように）
            if (e.mods.isPopupMenu())
            {
                // **押した行を選んでから出す。** どの行に効くメニューなのかが
                // 見た目で決まっていないと、隣のレーンを消すことになる
                selectAutomationRow (laneRow);

                if (onAutomationRowRightClicked != nullptr)
                    onAutomationRowRightClicked (laneRow.rowIndex, laneRow.ordinal,
                                                  localAreaToGlobal (getAutomationHeaderBounds (laneRow)));

                return;
            }

            // 8.59：**「B」でバイパス**（Phase 96）。点は消さずに効かせるのをやめる
            if (getAutomationBypassButtonBounds (laneRow).expanded (2).contains (e.getPosition()))
            {
                auto lane = getAutomationLaneFor (laneRow);

                if (lane.state.isValid())
                {
                    project.beginAction (utf8 ("オートメーションのバイパス"));
                    lane.setBypassed (! lane.isBypassed(), &project.getUndoManager());

                    // **音の側へも知らせる。** 写し取りは再生開始時なので（8.56）、
                    // ここで知らせておかないと次のPlayまで効かない
                    if (onModelChanged != nullptr)
                        onModelChanged();

                    repaint();
                }

                return;
            }

            if (getAutomationCloseButtonBounds (laneRow).expanded (2).contains (e.getPosition()))
            {
                project.beginAction (utf8 ("オートメーション表示の切り替え"));

                if (isMasterRow (laneRow.rowIndex))
                    project.setMasterAutomationLaneVisible (targetId, false, &project.getUndoManager());
                else if (juce::isPositiveAndBelow (laneRow.rowIndex, project.getNumTracks()))
                    project.getTrack (laneRow.rowIndex)
                           .setAutomationLaneVisible (targetId, false, &project.getUndoManager());

                refresh();
                return;
            }

            // 8.59：**左クリックでその行を選ぶ**（Phase 96）。
            // トラックと同じ扱いにして、インスペクタから色などを触れるようにする。
            // **トラックのほうは選び直さない**（押した行と選ばれる行が違うと、
            // 次の操作の行き先が読めなくなる）
            selectAutomationRow (laneRow);
            return;
        }

        // 仕様書5.6：マスター行のオートメーションボタン（Phase 26）。
        // トラックの範囲外なので、下のトラック用の判定より先に見る。
        // **`isShowingAutomation()`も見ること**：マスター行が出ていないときの
        // `isMasterRow()`は「最後のトラックの1つ下」を指してしまう。
        if (isShowingAutomation() && isMasterRow (headerTrackIndex)
             && getAutomationButtonBounds (headerTrackIndex).contains (e.getPosition()))
        {
            if (onAutomationButtonClicked != nullptr)
                onAutomationButtonClicked (headerTrackIndex,
                                            localAreaToGlobal (getAutomationButtonBounds (headerTrackIndex)));

            return;
        }

        if (juce::isPositiveAndBelow (headerTrackIndex, project.getNumTracks()))
        {
            auto headerTrack = project.getTrack (headerTrackIndex);

            // Phase 33：右クリックで削除・並べ替えのメニュー。
            // **ボタンの判定より先に見ること**（右クリックでボタンを押せてしまわないように）。
            if (e.mods.isPopupMenu())
            {
                auto headerBounds = juce::Rectangle<int> (0, getTrackRowY (headerTrackIndex),
                                                           trackHeaderWidth, getTrackAreaHeight (headerTrackIndex));

                if (onTrackHeaderRightClicked != nullptr)
                    onTrackHeaderRightClicked (headerTrackIndex, localAreaToGlobal (headerBounds));

                return;
            }

            // 8.60：**「i」でインスペクタを開く／閉じる**（Phase 97／改善案①）。
            // **選択も同時に移す**ので、押したトラックの設定がそのまま出る
            if (getInspectorButtonBounds (headerTrackIndex).expanded (2).contains (e.getPosition()))
            {
                // **選択を移す前に見ること。** いま出ているのが押したトラックなら
                // 開閉の切り替え、違うトラックなら開いたまま中身だけ差し替える。
                // クリップやレーンを選んでいるときも`getTrackId()`は親のトラックを返すので、
                // 「このトラックの設定が出ている」の判定はこれで足りる
                const bool alreadyShowingThisTrack =
                    (selection.getTrackId() == headerTrack.getId());

                selectedTrackIndex = headerTrackIndex;
                selectedClipIndex = -1;
                selectedIsMidi = false;
                publishSelection();

                if (onInspectorRequested != nullptr)
                    onInspectorRequested (alreadyShowingThisTrack);

                repaint();
                return;
            }

            // 8.44：**畳む／開くの三角**（Phase 84／C12）。
            // **他のヘッダー判定より先に見ること**：畳んだ行は低く、
            // 下段（S／M等）の判定と重なることがある
            if (! getCollapseTriangleBounds (headerTrackIndex).isEmpty()
                 && getCollapseTriangleBounds (headerTrackIndex).expanded (3)
                        .contains (e.getPosition()))
            {
                headerTrack.setCollapsed (! headerTrack.isCollapsed());

                // 行の高さが変わる＝下の行の位置も変わる（メーター等の子も置き直す）
                resized();
                repaint();
                return;
            }

            // 仕様書5.6：オートメーションのレーンを開く／対象を選ぶ（Phase 26）
            if (trackTypeHasAudioPath (headerTrack.getType())
                 && getAutomationButtonBounds (headerTrackIndex).contains (e.getPosition()))
            {
                if (onAutomationButtonClicked != nullptr)
                    onAutomationButtonClicked (headerTrackIndex,
                                            localAreaToGlobal (getAutomationButtonBounds (headerTrackIndex)));

                return;
            }

            // 仕様書5.4：入力モニタリング（Phase 26でここへ移動）。
            // 録音待機中のオーディオトラックにだけ出しているので、判定も同じ条件にする。
            if (headerTrack.getType() == TrackType::Audio && headerTrack.isArmed()
                 && getMonitorButtonBounds (headerTrackIndex).contains (e.getPosition()))
            {
                if (onInputMonitoringToggled != nullptr && isInputMonitoringEnabled != nullptr)
                    onInputMonitoringToggled (! isInputMonitoringEnabled());

                repaint();
                return;
            }

            // 仕様書5.2.1：ソロ／ミュート（Phase 58／8.1のC16）。
            // **判定はConsoleと同じくモデルを反転させるだけ。** 「今どちらが鳴るか」の
            // 解釈は`ProjectModel::isTrackAudible()`が持っている（8.13のA4）
            if (trackTypeHasSoloMute (headerTrack.getType())
                 && getSoloButtonBounds (headerTrackIndex).contains (e.getPosition()))
            {
                const bool shouldBeSoloed = ! headerTrack.isSoloed();

                project.beginAction (utf8 ("ソロの切り替え"));
                headerTrack.setSoloed (shouldBeSoloed, &project.getUndoManager());

                // 8.126：まとめて選んでいるなら、そちらも揃える（Phase 162／改善案35）
                applySoloToSelection (headerTrack.getId(), shouldBeSoloed);

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
                return;
            }

            if (trackTypeHasSoloMute (headerTrack.getType())
                 && getMuteButtonBounds (headerTrackIndex).contains (e.getPosition()))
            {
                const bool shouldBeMuted = ! headerTrack.isMuted();

                project.beginAction (utf8 ("ミュートの切り替え"));
                headerTrack.setMuted (shouldBeMuted, &project.getUndoManager());

                // 8.126：まとめて選んでいるなら、そちらも揃える（Phase 162／改善案35）
                applyMuteToSelection (headerTrack.getId(), shouldBeMuted);

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
                return;
            }

            // 8.61：**パンと音量は`TrackHeaderControls`が受け取る**（Phase 99／改善案⑨）。
            // Consoleと同じ`ValueEntrySlider`になったので、ここでの当たり判定は要らない
            // （Phase 98までは横棒を自前で描き、`DragMode::HeaderPan`で動かしていた）

            if (trackTypeCanArm (headerTrack.getType())
                 && getArmButtonBounds (headerTrackIndex).contains (e.getPosition()))
            {
                const bool shouldBeArmed = ! headerTrack.isArmed();
                const bool isMidi = (headerTrack.getType() == TrackType::Midi);

                project.beginAction (utf8 ("録音待機の切り替え"));

                if (isMidi)
                {
                    // 8.146：**MIDIは何本でも同時にアームできる**（Phase 184／改善案⑬a）。
                    // 入力は1本のMIDIストリームなので、増えても取り合いになりません
                    // （届いたメッセージが、アームした全部へ配られる。8.83）。
                    // **レイヤー（同じ演奏を複数の音源で録る）が普通に要る**ので、
                    // オーディオのような排他にはしていません
                    headerTrack.setArmed (shouldBeArmed, &project.getUndoManager());
                }
                else
                {
                    // 録音先を1トラックに限定するため、他のトラックの録音待機は解除する
                    // （複数トラック同時録音は入力ルーティングの実装が必要なため、今後のフェーズ）。
                    // **MIDIトラックには触らないこと**——オーディオを録るたびに
                    // MIDIのアームが落ちると、レイヤーの用意がやり直しになります
                    for (int i = 0; i < project.getNumTracks(); ++i)
                    {
                        auto t = project.getTrack (i);
                        if (t.getType() == TrackType::Audio)
                            t.setArmed (shouldBeArmed && i == headerTrackIndex, &project.getUndoManager());
                    }
                }

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
                return;
            }

            // 設計書2.3.7：録音待機ボタン以外の場所なら、そのトラックを選択する（Phase 17）。
            // インスペクタでトラックのプロパティを編集できるようにするため。
            if (juce::isPositiveAndBelow (headerTrackIndex, project.getNumTracks()))
            {
                // 8.126：**Ctrl＋クリックで出し入れ**（Phase 162／改善案35）。
                //
                // **並べ替えのドラッグは始めません。** Ctrlは選択の出し入れに使うので、
                // そのまま動かして並べ替えが始まると、押し間違えが取り返しにくい
                if (e.mods.isCommandDown())
                {
                    toggleTrackInSelection (headerTrack.getId());

                    // インスペクタが見るのは**最後に押した1本**。
                    // 外したときは、残っているものの先頭へ寄せる
                    if (isTrackInSelection (headerTrack.getId()))
                        selectedTrackIndex = headerTrackIndex;
                    else if (! selectedTrackIds.empty())
                        selectedTrackIndex = getTrackIndexById (selectedTrackIds.front());

                    // 8.154：**次のShift＋クリックの起点はここ**（Phase 192）
                    trackSelectionAnchorId = headerTrack.getId();

                    selectedClipIndex = -1;
                    selectedIsMidi = false;

                    publishSelection();
                    repaint();
                    return;
                }

                // 8.154：**Shift＋クリックで、起点からここまでをまとめて選ぶ**
                // （Phase 192／本人の要望）。
                //
                // **並べ替えのドラッグは始めません**（Ctrlのときと同じ理由）。
                // **起点は動かしません**——動かすと、Shift＋クリックのたびに
                // 範囲が作り直され、1本ずつしか伸ばせなくなります
                if (e.mods.isShiftDown() && trackSelectionAnchorId.isNotEmpty()
                     && getTrackIndexById (trackSelectionAnchorId) >= 0)
                {
                    selectTrackRangeTo (headerTrackIndex);

                    // インスペクタは**いま押した1本**を出す（起点ではない）
                    selectedTrackIndex = headerTrackIndex;
                    selectedClipIndex = -1;
                    selectedIsMidi = false;

                    publishSelection();
                    repaint();
                    return;
                }

                // 8.202：**すでに選ばれている行を押したときは、選択を保ちます**
                //         （Phase 236／本人の報告）。
                //
                // Phase 235で「選んだぶん全部をフォルダへ落とす」を入れたのに、
                // **入るのは1本のまま**でした。原因はここ——
                // **掴んだ瞬間に`setSingleTrackSelection()`が他を消していた**ので、
                // 離すころには選択が1本しか残っていません。
                //
                // ファイル一覧やDAWで普通にできている動きに合わせます：
                //
                // | 押した行 | すること |
                // |---|---|
                // | 選ばれていない | いつもどおり、その1本だけにする |
                // | **すでに選ばれている** | **そのまま保つ**（掴んで全部動かせる） |
                //
                // **ただのクリックだったときは、離すときに1本へ畳みます**——
                // 保ったままだと「選び直したのに減らない」ことになります。
                // 畳むのは`mouseUp`（動かしたかどうかは、そこで初めて分かる）
                const bool keepSelection = isTrackInSelection (headerTrack.getId())
                                            && selectedTrackIds.size() > 1;

                if (keepSelection)
                    collapseSelectionOnMouseUpId = headerTrack.getId();
                else
                {
                    collapseSelectionOnMouseUpId.clear();
                    setSingleTrackSelection (headerTrack.getId());
                }

                trackSelectionAnchorId = headerTrack.getId();   // 8.154：起点を置き直す

                selectedTrackIndex = headerTrackIndex;
                selectedClipIndex = -1; // クリップではなくトラックそのものの選択
                selectedIsMidi = false;

                // Phase 34：そのまま上下へドラッグすると並べ替えになる。
                // クリックだけなら何も起きない（mouseDragが呼ばれないので、
                // 挿入位置が決まらないまま mouseUp を迎える）ので、
                // 「選択」と「並べ替え」を同じ操作の入口にできる。
                //
                // **固定行（コードトラック）は掴ませない**（Phase 60／8.20）。
                // `ProjectModel::moveTrack()`が拒むので動きはしないが、
                // ドラッグ中の予告だけ出て「動かせそうに見える」状態になる
                if (headerTrackIndex != getPinnedRowIndex())
                {
                    dragMode = DragMode::ReorderTrack;
                    dragStartMousePosition = e.getPosition();
                    reorderSourceIndex = headerTrackIndex;
                    reorderTargetSlot = -1; // まだ動かしていない
                    reorderTargetFolderId.clear();   // Phase 90（D2）

                    // 8.70：掴んだヘッダーの写しを撮る（Phase 109）。
                    // **`reorderTargetSlot`を-1にした後で撮ること**——
                    // 先に撮ると、前のドラッグの予告線が写り込みます
                    captureReorderDragImage (headerTrackIndex, e.getPosition());
                }

                publishSelection();
                repaint();
                return;
            }
        }
    }

    // 8.56：**オートメーションの行の上でだけ**、点の編集を受け付ける（Phase 94／D3）。
    //
    // Phase 93までは「レーンを開いているあいだ、その行はクリップを触れない」形だった。
    // 専用の行になったので、**トラック行のクリップは開いていても今までどおり触れる**。
    // 判定の入口も「表示中か」ではなく「レーンの行の上か」に変わっている
    const auto clickedAutomationRow = getTimelineArea().contains (e.getPosition())
                                        ? findAutomationRowAtY (e.y)
                                        : AutomationRowRef();

    if (clickedAutomationRow.isValid())
    {
        AutomationRowRef automationRow;
        int pointIndex = -1;
        const bool hitPoint = hitTestAutomationPoint (e.getPosition(), automationRow, pointIndex);

        // 8.56：**貼り付け先はここで覚える**（Phase 94）。選択中のトラックから決めると、
        // 1トラックに複数のレーンが並んだときにどれか分からない
        lastEditedAutomationRow = clickedAutomationRow;

        //----------------------------------------------------------------------
        // 8.57：ツールごとの振る舞い（Phase 95／D14）。**ピアノロールと同じ**（8.38）。
        // **右クリックはツールに関係なくメニュー**なので、先に弾いておく

        if (! e.mods.isPopupMenu() && editTool == EditTool::pencil)
        {
            // なぞり1回ぶんをUndoの1ステップにする（区切りはここで1回だけ。3.1）
            project.beginAction (utf8 ("レーンのなぞり書き"));

            dragMode = DragMode::AutomationPaint;
            automationPaintRow = clickedAutomationRow;
            automationPaintStarted = false;
            clearAutomationSelection();

            paintAutomationPointAt (clickedAutomationRow, e.getPosition());
            return;
        }

        if (! e.mods.isPopupMenu() && editTool == EditTool::eraser)
        {
            // 8.43：**なぞった1回ぶんをUndoの1ステップ**にするため、区切りはここで作る
            project.beginAction (utf8 ("オートメーション点の削除"));
            eraseAutomationPointAt (e.getPosition());
            dragMode = DragMode::None;
            return;
        }

        if (! e.mods.isPopupMenu() && editTool == EditTool::cut)
            return;   // レーンには「割る」対象が無い（何も起きないのが正しい）

        if (hitPoint && e.mods.isPopupMenu())
        {
            // 右クリックでカーブ種別の変更と削除（仕様書5.6。Phase 20）
            auto lane = getAutomationLaneFor (automationRow);

            if (! lane.state.isValid())
                return;

            juce::PopupMenu menu;
            menu.addItem (5, utf8 ("数値を入力..."));   // 8.29の表（Phase 70）
            menu.addSeparator();

            // 8.37：**ピアノロール下のレーンと同じ項目**（Phase 77）。
            // 項目を並べるのは`AutomationCurveUI`の仕事にしてある（片方だけ増えるのを防ぐ）
            AutomationCurveUI::addCurveItems (menu, lane.getPoint (pointIndex).getCurve(),
                                               lane.getPoint (pointIndex).getCurveAmount());
            menu.addSeparator();
            menu.addItem (6, utf8 ("カット"));       // 8.29の表（Phase 71）
            menu.addItem (7, utf8 ("コピー"));
            menu.addSeparator();
            menu.addItem (4, utf8 ("この点を削除"));

            const auto ref = automationRow;

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                           .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                [this, ref, pointIndex] (int result)
                {
                    if (result <= 0)
                        return;

                    auto targetLane = getAutomationLaneFor (ref);

                    if (! targetLane.state.isValid()
                         || ! juce::isPositiveAndBelow (pointIndex, targetLane.getNumPoints()))
                        return;

                    if (result == 5)
                    {
                        // **入力欄は非同期で開く**ので、ここから先はそちらの仕事
                        showAutomationValueEntry (ref, pointIndex);
                        return;
                    }

                    if (result == 6 || result == 7)
                    {
                        copyAutomationPoint (ref, pointIndex, result == 6);   // 6＝カット
                        return;
                    }

                    if (result == 4)
                    {
                        project.beginAction (utf8 ("オートメーション点の削除"));
                        targetLane.removePoint (pointIndex, &project.getUndoManager());
                    }
                    else if (AutomationCurveUI::isCurveItem (result))
                    {
                        // 8.37：**種別と曲がり具合はいっしょに書く**（Phase 77）。
                        // 「直線」を選んだときは曲がり具合も0へ戻る（`amountForItem()`）
                        auto point = targetLane.getPoint (pointIndex);

                        project.beginAction (utf8 ("カーブ種別の変更"));
                        point.setCurve (AutomationCurveUI::curveForItem (result), &project.getUndoManager());
                        point.setCurveAmount (AutomationCurveUI::amountForItem (result, point.getCurveAmount()),
                                               &project.getUndoManager());
                    }
                    else
                    {
                        return;   // 知らない項目（増やしたときの取りこぼしを黙って通さない）
                    }

                    if (onModelChanged != nullptr)
                        onModelChanged();

                    repaint();
                });

            dragMode = DragMode::None;
            return;
        }

        // 8.37：**曲がり具合のつまみ**（Phase 77）。
        // **点よりも後に判定する**（重なったときは点を優先。動かせるほうを取る）
        AutomationRowRef curveRow;
        int curveFromPoint = -1;

        if (! hitPoint && ! e.mods.isPopupMenu()
             && hitTestCurveHandle (e.getPosition(), curveRow, curveFromPoint))
        {
            auto lane = getAutomationLaneFor (curveRow);

            if (lane.state.isValid() && curveFromPoint + 1 < lane.getNumPoints())
            {
                // ドラッグ1回ぶんをUndoの1ステップにまとめる（3.1）
                project.beginAction (utf8 ("曲がり具合の変更"));

                dragMode = DragMode::CurveHandle;
                curveDragRow = curveRow;
                curvePointIndex = curveFromPoint;
                curveDragFromY = (float) automationValueToY (curveRow,
                                                              lane.getPoint (curveFromPoint).getValue());
                curveDragToY   = (float) automationValueToY (curveRow,
                                                              lane.getPoint (curveFromPoint + 1).getValue());
                repaint();
                return;
            }
        }

        if (e.mods.isPopupMenu())
        {
            dragMode = DragMode::None;
            return;
        }

        //----------------------------------------------------------------------
        // 8.57：矢印ツール（Phase 95／D14）。ピアノロールと同じ形（8.38）

        if (! hitPoint)
        {
            // 8.57：**押しただけでは置きません。**
            // 矢印ツールのドラッグは点の範囲選択になったので、ここで置くと
            // 「選ぶつもりのドラッグ」でも点が増える。置くのは
            // 「動かさずに離した」と分かる`mouseUp()`
            automationPendingAdd = true;
            automationPendingPosition = e.getPosition();
            automationPendingRow = clickedAutomationRow;
            automationRangeAnchor = e.getPosition();
            automationRangeBounds = { e.x, e.y, 0, 0 };
            dragMode = DragMode::None;
            return;
        }

        auto lane = getAutomationLaneFor (automationRow);

        if (! lane.state.isValid())
        {
            dragMode = DragMode::None;
            return;
        }

        auto point = lane.getPoint (pointIndex);

        // 8.57：**Shift＋クリックは選択に足す／外す**（クリップ・ノートと同じ）
        if (e.mods.isShiftDown())
        {
            if (isAutomationPointSelected (point.state))
                selectedAutomationPoints.erase (std::remove (selectedAutomationPoints.begin(),
                                                              selectedAutomationPoints.end(),
                                                              point.state),
                                                 selectedAutomationPoints.end());
            else
                selectedAutomationPoints.push_back (point.state);

            dragMode = DragMode::None;
            repaint();
            return;
        }

        // **既に選ばれているなら選び直さない**（まとめて動かすため。8.13のA2）
        if (! isAutomationPointSelected (point.state))
        {
            selectedAutomationPoints.clear();
            selectedAutomationPoints.push_back (point.state);
        }

        // ドラッグ1回ぶんをUndoの1ステップにまとめる（HANDOVER 3.1）。
        // 点はドラッグ中に直接モデルへ書くので、区切りはここで作っておく。
        project.beginAction (utf8 ("オートメーション点の移動"));

        dragMode = DragMode::AutomationPoint;
        automationDragRow = automationRow;
        automationPointIndex = pointIndex;

        // 8.57：**いっしょに動く点の、掴んだ時点の位置を控えておく**（8.38と同じ）
        pruneAutomationSelection();
        automationDragOthers.clear();
        automationDragAnchorTime = point.getTime();
        automationDragAnchorValue = point.getValue();

        for (const auto& state : selectedAutomationPoints)
            if (state != point.state)
                automationDragOthers.push_back ({ state, AutomationPoint (state).getTime(),
                                                   AutomationPoint (state).getValue() });

        repaint();
        return;
    }

    // 仕様書5.2.3：コード区間の右クリックで削除（Phase 42）。
    // クリップのヒットテストより先に見る。コードトラックはクリップを持たないので
    // hitTestClip()は必ずfalseを返し、ここを通り越すと何も起きなくなる。
    {
        int chordTrackIndex = -1;
        int regionIndex = -1;

        if (e.mods.isPopupMenu()
             && hitTestChordRegion (e.getPosition(), chordTrackIndex, regionIndex))
        {
            juce::PopupMenu menu;
            menu.addItem (1, utf8 ("このコードを削除"));

            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                           .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                [this, chordTrackIndex, regionIndex] (int result)
                {
                    if (result != 1)
                        return;

                    auto track = project.getTrack (chordTrackIndex);

                    // メニューは非同期に閉じるので、その間に区間が減っている可能性がある
                    if (! juce::isPositiveAndBelow (regionIndex, track.getNumChordRegions()))
                        return;

                    project.beginAction (utf8 ("コード区間の削除"));
                    track.removeChordRegion (track.getChordRegion (regionIndex), &project.getUndoManager());

                    if (onModelChanged != nullptr)
                        onModelChanged();

                    repaint();
                });

            dragMode = DragMode::None;
            return;
        }

        // 仕様書5.2.3：左クリックでコード区間を掴む（Phase 45）。
        // 端を掴んだら伸縮、それ以外は移動（クリップと同じ操作感）。
        if (! e.mods.isPopupMenu()
             && hitTestChordRegion (e.getPosition(), chordTrackIndex, regionIndex))
        {
            auto track = project.getTrack (chordTrackIndex);
            auto region = track.getChordRegion (regionIndex);

            // 8.128：**伸縮は無くなりました**（Phase 164／改善案13）。
            //
            // 旗で表すことにしたので、長さは「次の旗まで」で決まります。
            // 端を掴ませると、**動かした長さが次の旗で上書きされる**ことになり、
            // 「掴めるのに何も起きない」という一番たちの悪い形になります
            chordDragMode = ChordDragMode::move;

            chordDragTrackIndex = chordTrackIndex;
            chordDragRegionIndex = regionIndex;
            chordDragOriginalStart = region.getStartTime();
            chordDragOriginalLength = region.getLength();
            chordDragPreviewStart = chordDragOriginalStart;
            chordDragPreviewLength = chordDragOriginalLength;
            chordDragStartMousePosition = e.getPosition();

            // 8.29の表：**掴んだ区間を選択する**（Phase 70）。
            // 選んでおくと Delete で消せる（右クリックメニューまで行かずに済む）。
            // **ValueTreeで覚えること**：番号で持つと、他の区間が増減したときに
            // 別のコードを指す（1.32。ノートの選択と同じ理由）
            setSelectedChordRegion (region.state);

            // クリップ側のドラッグ状態には触らせない（別々に持っている。宣言のコメント参照）
            dragMode = DragMode::None;
            repaint();
            return;
        }
    }

    int trackIndex = -1;
    int clipIndex = -1;
    bool isMidiClip = false;

    //==========================================================================
    // 仕様書6.2：ツールごとの振る舞い（Phase 51）。
    // **矢印以外は、ここで完結して返す。** 下の「選択して掴む」処理へ落とすと、
    // 割ったり描いたりした直後にドラッグが始まってしまう。

    if (editTool != EditTool::arrow && getTimelineArea().contains (e.getPosition())
         && ! e.mods.isPopupMenu())
    {
        const bool onClip = hitTestClip (e.getPosition(), trackIndex, clipIndex, isMidiClip);

        if (editTool == EditTool::cut)
        {
            if (onClip)
                cutClipAt (trackIndex, clipIndex, isMidiClip, e.x);

            return;
        }

        // 8.94：**ペンでは何も作りません**（Phase 134）。
        // MIDIクリップという入れ物が無くなり（8.91）、打ち込みはピアノロールの仕事です。
        //
        // 8.120：**何もしないまま返さず、選択ツールへ戻す**（Phase 155／改善案29・37）。
        //
        // ピアノロールでペンに持ち替えたまま上のトラック行を触ると、
        // クリップを選ぼうとしても**押しても何も起きない**状態でした。
        // ここはペンの用が無いと分かっている場所なので、
        // **ツールを戻したうえで、このクリックはそのまま選択として扱います**
        // （下へ落とす＝矢印の処理をそのまま通す）。
        //
        // **オートメーション行はここへ来ません。** なぞり書きは上で処理して返しており
        // （「レーンのなぞり書き」）、アレンジ画面でもペンはそちらで使います。
        //
        // **自分で`setEditTool()`を呼ばないこと。** ツールはピアノロールにも効くので、
        // 値を配るのは`MainComponent`の仕事です（宣言のコメント／Phase 52）。
        // 呼び出しは同期なので、この行を抜けた時点で`editTool`は`arrow`になっています
        if (editTool == EditTool::pencil)
        {
            if (onEditToolSelected != nullptr)
                onEditToolSelected (EditTool::arrow);

            // 配り終わっても矢印になっていないとき（配線が無いなど）は、
            // ペンのまま下へ落とさない——描く先が無いのに掴む動きだけが始まる
            if (editTool != EditTool::arrow)
                return;
        }

        // 8.43：消しゴム（Phase 83／C11）。**なぞった1回ぶんをUndoの1ステップ**にするため、
        // 区切りはここで作る（`mouseDrag`側では作らない。3.1）
        if (editTool == EditTool::eraser)
        {
            project.beginAction (utf8 ("クリップの削除"));
            eraseClipAt (e.getPosition());
            dragMode = DragMode::None;
            return;
        }
    }

    // 8.93：**選んである時間範囲は、塊より先に見る**（Phase 133）。
    //
    // 後に置くと、**範囲の中にノートがあるところは塊として掴まれる**ので、
    // 範囲を掴んで動かせるのが「音の無い隙間だけ」になります——
    // たいていの範囲は音で埋まっているので、実質動かせません。
    //
    // **「上に乗っているものを先に見る」**（8.20・8.58と同じ話）。
    // 範囲は自分で引いた窓なので、塊より手前にあります。
    if (handleTimeRangeMouseDown (e, false))
        return;

    if (hitTestClip (e.getPosition(), trackIndex, clipIndex, isMidiClip))
    {
        // 8.29の表：右クリックはメニュー（Phase 68で「選ぶだけ」、Phase 71でメニュー）。
        //
        // それまでは、オーディオクリップの右クリックが**その場でヒットポイントを削除**し、
        // MIDIクリップの右クリックは**移動のドラッグを始めて**いました。どちらも
        // 「触れただけで何かが起きる」形で、ノートの右クリックをメニューに変えたとき
        // （Phase 52／8.11）と同じ理由でやめています。
        //
        // **押したクリップが選択に入っていなければ、それだけを選び直してから**出すこと
        // （選んでいないものを消してしまわないように。ノートのメニューと同じ）。
        if (e.mods.isPopupMenu())
        {
            // 8.94：**MIDIはここへ来ません**（Phase 134）。塊の選択・移動・メニューは
            // `handleTimeRangeMouseDown()`が上で引き受けています（矢印ツールのとき）。
            // ここへ落ちるのはペン等で押した場合だけなので、何もしません
            if (isMidiClip)
                return;

            if (! isClipSelected (project.getTrack (trackIndex).getId(),
                                   makeClipRef (trackIndex, clipIndex, isMidiClip).clipId, isMidiClip))
                setSingleClipSelection (trackIndex, clipIndex, isMidiClip);

            selectedTrackIndex = trackIndex;
            selectedClipIndex = clipIndex;
            selectedIsMidi = isMidiClip;
            dragMode = DragMode::None;
            publishSelection();
            repaint();

            showClipMenu (e);
            return;
        }

        // Shift＋クリックは選択に足す／外す。
        // **Ctrl＋クリックも同じ**だが、そちらは「動かさずに離したとき」だけ
        // （Ctrlはドラッグでの複製にも使うので、掴んだ時点では区別できない。mouseUpで振り分ける）。
        //
        // 8.92：**MIDIの塊は複数選択の対象になりません**（Phase 132）。
        // 選択は「クリップのID」で覚えているのに（1.32）、塊にはIDがありません
        // ——計算で出てくるものなので、ノートを1つ足すだけで別の塊になります。
        // まとめて動かしたいときはPhase 133の**時間範囲の選択**を使ってください
        if (e.mods.isShiftDown())
        {
            if (! isMidiClip)
                toggleClipSelection (trackIndex, clipIndex, isMidiClip);

            return;
        }

        // 8.94：**MIDIはここへ来ません**（Phase 134。上のメニューの分岐と同じ理由）
        if (isMidiClip)
            return;

        const bool alreadySelected = isClipSelected (project.getTrack (trackIndex).getId(),
                                                      makeClipRef (trackIndex, clipIndex, isMidiClip).clipId,
                                                      isMidiClip);

        // 8.154：**Ctrlを押しているあいだは、選択を畳まない**（Phase 192／本人の要望）。
        //
        // Phase 52でCtrl＋クリックは「選択に足す／外す」のはずでしたが、
        // **ここで1本だけの選択に上書きしていた**ので、`mouseUp`が足す前に
        // 他が全部外れていました——**足したつもりが選び直しになっていた**、が実際の動きです。
        //
        // 畳むのは「動かす」と決まってからにします（`mouseDrag`の頭）
        if (! alreadySelected && ! e.mods.isCommandDown())
            setSingleClipSelection (trackIndex, clipIndex, isMidiClip);

        // 選んでいないクリップをCtrlで掴んだ。**動かし始めたら1本だけの選択へ**
        ctrlClickPendingSelection = (! alreadySelected && e.mods.isCommandDown());

        // Ctrlを押しながらのドラッグは複製（Phase 52）。離すときに置く
        dragIsCopy = e.mods.isCommandDown();

        selectedTrackIndex = trackIndex;
        selectedClipIndex = clipIndex;
        selectedIsMidi = isMidiClip;
        publishSelection();

        auto clip = project.getTrack (trackIndex).getClip (clipIndex);
        auto bounds = getClipBounds (trackIndex, clipIndex);

        // **ヒットポイントの手動編集はやめました**（Phase 68／8.29の表の備考
        // 「ヒットポイントは現在使用しない」）。Alt＋クリックで追加・右クリックで削除、
        // という割り当てでした。
        //
        // **モデル側（`addHitPoint()`／`removeHitPointNear()`・`HitPointDetector`）は
        // 残してあります。** 仕様書5.5.1のワープ／タイムストレッチで使う予定のもので、
        // 消すと作り直しになります（6.3）。**入口だけを外した**状態です。

        auto fadeInHandle = getFadeHandlePosition (bounds, clip.getFadeInSeconds(), clip.getFadeOutSeconds(), true);
        auto fadeOutHandle = getFadeHandlePosition (bounds, clip.getFadeInSeconds(), clip.getFadeOutSeconds(), false);

        // 8.150：**ワープマーカーの取っ手がいちばん先**（Phase 188／8.48）。
        // 上端の細い帯で、しかも端の掴みしろは避けてあるので、
        // フェードやトリムを取り上げることはありません
        if (auto markerSource = findWarpMarkerHandleAt (bounds, clip, e.getPosition()))
        {
            dragMode = DragMode::WarpMarker;
            dragWarpMarkerSource = *markerSource;

            dragOriginalWarpClip = clip.sourceTimeToTimeline (*markerSource) - clip.getStartTime();
            dragPreviewWarpClip = dragOriginalWarpClip;

            dragOriginalStartTime = clip.getStartTime();
            dragOriginalLength = clip.getLength();
            dragOriginalOffset = clip.getOffset();
            dragOriginalStretch = clip.getStretch();
            dragPreviewStartTime = dragOriginalStartTime;
            dragPreviewLength = dragOriginalLength;
            dragPreviewOffset = dragOriginalOffset;
            dragPreviewStretch = dragOriginalStretch;
            dragPreviewTrackIndex = trackIndex;
            dragStartMousePosition = e.getPosition();
            return;
        }

        // ヒットテストの優先順位：フェードハンドル → 端のトリム → 本体の移動
        if (e.getPosition().getDistanceFrom (fadeInHandle) <= fadeHandleHitRadius)
            dragMode = DragMode::FadeIn;
        else if (e.getPosition().getDistanceFrom (fadeOutHandle) <= fadeHandleHitRadius)
            dragMode = DragMode::FadeOut;
        else if (e.x <= bounds.getX() + edgeGrabMargin)
            dragMode = DragMode::TrimLeft;
        else if (e.x >= bounds.getRight() - edgeGrabMargin)
            // 8.149：**Altを押しながら右端を掴むと伸縮**（Phase 187/8.48）。
            // トリムは「窓を狭める」、伸縮は「中身ごと引き伸ばす」——
            // **同じ端を掴むので、修飾キーで分ける**（他のDAWと同じ形）
            dragMode = e.mods.isAltDown() ? DragMode::StretchRight : DragMode::TrimRight;
        else
            dragMode = DragMode::Move;

        dragOriginalStartTime = clip.getStartTime();
        dragOriginalLength = clip.getLength();
        dragOriginalOffset = clip.getOffset();
        dragOriginalFadeIn = clip.getFadeInSeconds();
        dragOriginalFadeOut = clip.getFadeOutSeconds();
        dragOriginalStretch = clip.getStretch();   // 8.149（Phase 187／ここへ来るのはオーディオだけ）
        dragPreviewStretch = dragOriginalStretch;
        dragPreviewStartTime = dragOriginalStartTime;
        dragPreviewLength = dragOriginalLength;
        dragPreviewOffset = dragOriginalOffset;
        dragPreviewFadeIn = dragOriginalFadeIn;
        dragPreviewFadeOut = dragOriginalFadeOut;
        dragPreviewTrackIndex = trackIndex;
        dragStartMousePosition = e.getPosition();
    }
    else
    {
        // 8.93：**MIDIトラックの空いている場所は「時間範囲」**（Phase 133）。
        //
        // クリップという掴めるものが無くなったぶん（8.91）、
        // **まとめて動かす手段はこれになります**（Abilityと同じ形）。
        // **クリップの右クリック（ツールのメニュー）より先に見ること**——
        // 範囲の上での右クリックは、範囲のメニューを出したい
        if (handleTimeRangeMouseDown (e, true))
            return;

        // 仕様書6.2（8.29の表）：右クリックはツールの選択（Phase 68）。
        // **ツールバーはアレンジ画面にしかない**ので、ポップアウトしたエディタや
        // 画面の下のほうで作業しているときは、ここが一番近い入口になる
        if (e.mods.isPopupMenu())
        {
            showToolMenu (e);
            return;
        }

        // 8.96：**トラックの上の空きを押したら、そのトラックを選ぶ**（Phase 136）。
        //
        // Phase 135までは選択をまるごと外していました。ヘッダーを押さないと選べないので、
        // 「そのトラックへ貼り付けたい」「インスペクタで見たい」のたびに
        // 端まで戻ることになります。**押した場所がそのまま行き先**として読めるほうが自然です
        // （8.60で「＋ 新しいトラック」の行に同じ判断をしたのと同じ話）。
        //
        // **クリップの選択は外します**：クリップを選んでいない状態にはしたいので
        const int rowTrackIndex = getTimelineArea().contains (e.getPosition())
                                     ? getTrackIndexForY (e.y) : -1;

        selectedTrackIndex = juce::isPositiveAndBelow (rowTrackIndex, project.getNumTracks())
                                 ? rowTrackIndex : -1;
        selectedClipIndex = -1;
        selectedIsMidi = false;
        setSelectedChordRegion ({});   // Phase 70：コード区間の選択も外す
        dragMode = DragMode::None;
        publishSelection();

        // **複数選択も一緒に外すこと**（Phase 52で直した）。
        // ここを忘れると、Ctrl+Aで全選択したあと空いている場所をクリックしても
        // ハイライトが残り続け、「選択を解除できない」ように見える。
        if (! selectedClips.empty())
        {
            selectedClips.clear();

            if (onClipSelectionChanged != nullptr)
                onClipSelectionChanged();
        }

        // 8.163：**時間範囲（MIDIの塊の選択）も一緒に畳む**（Phase 201／本人の報告）。
        //
        // 本人の言葉は「MIDI塊・オーディオクリップ選択時、トラックのない空スペースを
        // クリックでも選択を解除できるようにしてほしい」。
        //
        // クリップのほうは前から畳んでいました（すぐ上）。**範囲だけが残っていた**のは、
        // 畳む道が2つしか無かったためです：
        //
        // | 畳まれる場面 | 経路 |
        // |---|---|
        // | MIDIトラックの空きを押した | `handleTimeRangeMouseDown()`が引き直す |
        // | 全トラックの区間の外を押した | `mouseDown()`の先頭（8.129） |
        //
        // **トラックの無い場所（一覧の下）とオーディオトラックの空きは、どちらも通りません。**
        // 「選んでいるものを外したい」ときにいちばん押しやすい場所が、
        // 唯一外せない場所になっていました。
        //
        // **ここへ置いてよい理由**：この行まで来ているのは
        // 「クリップでもなく、範囲の引き直し・移動でもない場所を押した」ときだけです
        // （どちらも上で`return`しています）。**クリップと同じ扱いに揃えます。**
        clearTimeRange();

        // 8.29の表：空いている場所は**クリック＝選択の解除とカーソル移動、
        // ドラッグ＝範囲選択**（Phase 68でカーソル移動、Phase 69で範囲選択）。
        //
        // **掴んだ時点では区別できない**ので、ここでは範囲選択の構えだけ取り、
        // `mouseUp`で「動かしたか」を見て振り分けます（8.10と同じ形）。
        // Phase 69で範囲選択ツールを畳んだので、**矢印のままで範囲が選べます**。
        //
        // **タイムライン部分だけ**（ヘッダーの空きや「+ 新しいトラック」の行で
        // 再生位置が動くと、押し間違えたときに演奏中の位置を失う）。
        if (getTimelineArea().contains (e.getPosition()))
        {
            rangeSelecting = true;
            rangeSelectAnchor = e.getPosition();
            rangeSelectBounds = { e.x, e.y, 0, 0 };
        }
    }

    repaint();
}

void TimelineComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 8.29の表どおりの割り当て（Phase 68）。
    //
    // **判定の順番は`mouseDown()`と同じにすること。** 片方だけ順番を変えると、
    // 「1回目で反応したものと2回目で反応するものが違う」という状態になる。
    //
    // **ダブルクリックの前に必ずmouseDownが2回走っている。** 掴みかけの状態
    // （ドラッグ・ループの引き直し）をここで畳んでおかないと、離した後も
    // 掴んだままの扱いが残る。

    // 仕様書5.9：ループの帯 → ループの有効/無効（Phase 68）。
    // **ルーラーの判定より先に見ること**（帯はルーラーの中にある）
    if (getLoopStripArea().contains (e.getPosition()))
    {
        loopDragMode = LoopDragMode::none;   // 1回目で始まった引き直しを畳む

        project.beginAction (utf8 ("ループの入切"));
        project.setLoopEnabled (! project.isLoopEnabled(), &project.getUndoManager());

        if (onLoopChanged != nullptr)
            onLoopChanged();

        repaint();
        return;
    }

    // 8.107：小節バーのレーン（Phase 142・144）。**テンポの札だけ、ダブルクリックで打ち込む。**
    // **ルーラーの判定より先に見ること**（レーンはルーラーの中にある）
    if (getSignatureStripArea().contains (e.getPosition()))
    {
        signatureDragRef = {};   // 1回目で始まったドラッグを畳む

        const auto ref = findSignatureMarkerAt (e.getPosition());

        // **拍子とキーの札には割り当てません。** 1つの札に2つの値が乗っているので、
        // ダブルクリックだけでは「どちらを変えるのか」を決められない（8.107）。
        // 空いているところも同じ理由——足すのも変えるのも右クリックのメニューから
        if (ref.kind == SignatureMarkerRef::Kind::tempo)
            editTempoChange (ref.beatPosition);

        return;
    }

    // 8.162：**旗のダブルクリック → その場で名前を打ち直す**（Phase 200／本人の要望）。
    //
    // Phase 199まで、ここは「ルーラーとマーカーの旗には割り当てない」でした（8.29の表）。
    // 名前を変える道が右クリック→メニュー→ダイアログの3手だけだったので、
    // **旗を並べているあいだ、名前を付ける手数がいちばん多い**状態でした。
    //
    // **1回目のmouseDownで始まっている旗のドラッグを畳んでから出すこと**——
    // 畳まないと、入力欄を出したまま旗が動きます
    if (getMarkerStripArea().contains (e.getPosition()))
    {
        const int markerIndex = findMarkerAt (e.getPosition());

        if (markerIndex >= 0)
        {
            markerDragIndex = -1;
            showMarkerNameEditor (markerIndex);
        }

        return;
    }

    // ルーラーにはダブルクリックを割り当てない（表の「—」）。
    // **ここで返しておくこと。** 返さないと、下のクリップ判定へ落ちて
    // 「ルーラーをダブルクリックしたらクリップが増える」ことになる
    if (e.y < rulerHeight && e.x >= trackHeaderWidth)
        return;

    // 設計書2.3.1：トラックヘッダーの名前 → 名前の変更（Phase 68）。
    // **下段（Rec・S・M・A・Pan）は対象外**（表では「—」）。
    // 切り替えボタンの上でダブルクリックすると2回切り替わるだけなので、
    // そこへ名前の入力欄を出すと「押したのに戻った」ように見える
    if (e.x < trackHeaderWidth)
    {
        const int headerTrackIndex = getTrackIndexForY (e.y);

        if (! juce::isPositiveAndBelow (headerTrackIndex, project.getNumTracks())
             || getAddTrackRowBounds().contains (e.getPosition()))
            return;

        // 8.56：**レーンの行のヘッダーでは名前を変えない**（Phase 94／D3）。
        // レーンの行はトラック行の「中」にあるので、`getTrackIndexForY()`は
        // 親のトラックを返す。弾かないと、レーンを叩いてトラック名の入力欄が出る
        if (findAutomationRowAtY (e.y).isValid())
            return;

        auto controlRow = getHeaderControlRow (headerTrackIndex);

        if (! controlRow.isEmpty() && controlRow.contains (e.getPosition()))
            return;

        dragMode = DragMode::None;   // 1回目で始まった並べ替えを畳む

        // 8.61：**その場で編集する**（Phase 99／改善案⑤）。
        // Phase 98までは`NameEntry`（別ウィンドウ）だったが、
        // 名前を1つ直すのにダイアログが開いて閉じるのは重かった
        showTrackNameEditor (headerTrackIndex);
        return;
    }

    // 仕様書5.6：オートメーションの点 → 削除（Phase 68）。
    // **点の上だけ。** 空いている場所は1回目のmouseDownで点が増えているので、
    // ここではその点が消えて元に戻る（増えて消えるので、見た目は何も起きない）
    // 8.56：**レーンの行の上だけ**（Phase 94／D3）。トラック行のクリップは今までどおり
    if (getTimelineArea().contains (e.getPosition()) && findAutomationRowAtY (e.y).isValid())
    {
        // 8.37：**つまみのダブルクリックで直線に戻す**（Phase 77。ピアノロールと同じ）
        AutomationRowRef curveRow;
        int curveFromPoint = -1;

        if (hitTestCurveHandle (e.getPosition(), curveRow, curveFromPoint))
        {
            auto lane = getAutomationLaneFor (curveRow);

            if (lane.state.isValid() && juce::isPositiveAndBelow (curveFromPoint, lane.getNumPoints()))
            {
                auto point = lane.getPoint (curveFromPoint);

                project.beginAction (utf8 ("直線に戻す"));
                point.setCurve (AutomationCurve::Linear, &project.getUndoManager());
                point.setCurveAmount (0.0f, &project.getUndoManager());

                dragMode = DragMode::None;

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
            }

            return;
        }

        AutomationRowRef automationRow;
        int pointIndex = -1;

        if (hitTestAutomationPoint (e.getPosition(), automationRow, pointIndex))
        {
            auto lane = getAutomationLaneFor (automationRow);

            if (lane.state.isValid() && juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
            {
                project.beginAction (utf8 ("オートメーション点の削除"));
                lane.removePoint (pointIndex, &project.getUndoManager());

                // 掴んだままの番号を残さない（消えた点を動かし続けることになる）
                automationDragRow = {};
                automationPointIndex = -1;
                automationDragOthers.clear();
                pruneAutomationSelection();   // 8.57：消えた点を選択に残さない（Phase 95）
                dragMode = DragMode::None;

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
            }
        }

        return;   // レーンの行ではクリップを触らない（mouseDownと同じ扱い）
    }

    // 仕様書5.2.3：コードトラックの空いている場所をダブルクリックしたら、
    // そこに1小節ぶんのコード区間を足す（Phase 42）。
    // 既にある区間の上ならコードパッドを開く。
    {
        const int chordTrackIndex = getTrackIndexForY (e.y);

        if (getTimelineArea().contains (e.getPosition())
             && juce::isPositiveAndBelow (chordTrackIndex, project.getNumTracks())
             && project.getTrack (chordTrackIndex).getType() == TrackType::Chord)
        {
            dragMode = DragMode::None;

            int hitTrack = -1;
            int hitRegion = -1;

            // 8.128：**振り分けは「旗の上か」で行う**（Phase 164／改善案13）。
            //
            // Phase 163までは「区間の上か」で分けていました。旗にしたことで
            // **区間は隙間なく並ぶ**ので、その判定だと曲じゅうが「区間の上」になり、
            // **新しい旗をどこにも立てられなくなります**。
            //
            // 旗＝そのコードそのもの、帯＝そのコードが続いている場所、と読めるので、
            // **旗をダブルクリック＝そのコードを編む／帯をダブルクリック＝ここで変える**
            if (hitTestChordFlag (e.getPosition(), hitTrack, hitRegion))
            {
                // 旗の上なら、そこを挿入位置にしてコードパッドを開く
                if (onChordRegionDoubleClicked != nullptr)
                    onChordRegionDoubleClicked (project.getTrack (hitTrack)
                                                       .getChordRegion (hitRegion).getStartTime());
            }
            else
            {
                addChordRegionAt (chordTrackIndex, e.x);
            }

            return;
        }
    }

    int trackIndex = -1;
    int clipIndex = -1;
    bool isMidiClip = false;

    if (hitTestClip (e.getPosition(), trackIndex, clipIndex, isMidiClip))
    {
        // ダブルクリックの前にmouseDownが走っているため、ドラッグ状態が残っている。
        // ここで畳んでおかないと、画面が切り替わった後もドラッグ中扱いのままになる。
        dragMode = DragMode::None;

        // 設計書4.2：MIDIはピアノロールを開く（Phase 15）。
        // 8.91：**渡すのは「押した時刻」**（Phase 131）。クリップ番号という指し方が無くなった
        if (isMidiClip)
        {
            if (onMidiClipDoubleClicked != nullptr)
                onMidiClipDoubleClicked (trackIndex, xToTime (e.x));

            return;
        }

        // 仕様書5.5：オーディオクリップは**その位置で分割**（Phase 68）。
        // オーディオエディタ（D1）ができるまでの割り当てではなく、表で決めた仕様。
        // 位置はスナップに従う（`cutClipAt()`の中で寄せる。8.14）
        cutClipAt (trackIndex, clipIndex, false, e.x);
        return;
    }

    // 8.94：**空いている場所のダブルクリックでは何も作りません**（Phase 134）。
    //
    // Phase 133まではMIDIクリップを置いていましたが、その入れ物が無くなりました（8.91）。
    // 打ち込みはピアノロールで、まとめて動かすのは範囲の選択（8.93）で行います。
    // **オーディオも作れません**：「どのファイルか」が決まらないと器だけ置いても
    // 何もできないためです（読み込みはドラッグ&ドロップとブラウザから。10.1）。
}

//==============================================================================
// 仕様書6.2：カット／コピー／貼り付け（Phase 71／8.29の表）
//==============================================================================

bool TimelineComponent::copySelectedClips (bool alsoDelete)
{
    // 単数の選択しかしていない場合も、複数選択の形へ寄せてから扱う
    // （選択の持ち方が2つあるのは8.10の経緯。ここで1本にまとめる）
    auto refs = selectedClips;

    if (refs.empty() && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
        refs.push_back (makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi));

    if (refs.empty())
        return false;

    double referenceTime = std::numeric_limits<double>::max();
    int referenceTrack = std::numeric_limits<int>::max();

    struct Found { juce::ValueTree state; int trackIndex = -1; double startTime = 0.0; };
    juce::Array<Found> found;

    for (const auto& ref : refs)
    {
        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getId() != ref.trackId)
                continue;

            juce::ValueTree state;

            if (ref.isMidi)
                continue;   // 8.94：クリップはオーディオだけ（Phase 134）

            {
                for (int c = 0; c < track.getNumClips(); ++c)
                    if (track.getClip (c).getId() == ref.clipId)
                        state = track.getClip (c).state;
            }

            if (! state.isValid())
                break;

            Found f;
            f.state = state;
            f.trackIndex = t;
            f.startTime = AudioClip (state).getStartTime();   // 8.137：アクセサを通す（Phase 175）
            found.add (f);

            referenceTime = juce::jmin (referenceTime, f.startTime);
            referenceTrack = juce::jmin (referenceTrack, t);
            break;
        }
    }

    if (found.isEmpty())
        return false;

    juce::Array<EditClipboard::Item> items;

    for (const auto& f : found)
    {
        EditClipboard::Item item;

        // **複製を入れること**（元が消えたり動いたりすると貼る中身まで変わる）
        item.state = f.state.createCopy();
        item.timeOffset = f.startTime - referenceTime;

        // 8.139：拍のずれも覚える（Phase 177）。**オーディオは秒のほうを使います**が、
        // 揃えておかないと「どちらが入っているか」を読む側が気にすることになります
        item.beatOffset = project.getBeatPositionAt (f.startTime)
                            - project.getBeatPositionAt (referenceTime);

        // **トラックのずれも覚える。** 3本にまたがってコピーしたものを
        // 1本へ落とすと、重なって元の形が分からなくなる
        item.rowOffset = f.trackIndex - referenceTrack;
        items.add (item);
    }

    EditClipboard::set (EditClipboard::Kind::clips, std::move (items));

    if (alsoDelete)
    {
        project.beginAction (utf8 ("クリップの削除"));

        for (const auto& f : found)
        {
            auto track = project.getTrack (f.trackIndex);

            // **ValueTreeで引き直すこと。** 1つ消すたびに番号がずれる（1.32）
            for (int c = track.getNumClips(); --c >= 0;)
                if (track.getClip (c).state == f.state)
                    track.removeClip (track.getClip (c), &project.getUndoManager());

        }

        clearSelection();
        clearClipSelection();

        if (onModelChanged != nullptr)
            onModelChanged();

        refresh();
    }

    return true;
}

bool TimelineComponent::pasteClipsAt (double timeSeconds)
{
    if (EditClipboard::getKind() != EditClipboard::Kind::clips)
        return false;

    // **貼り先の先頭は「選んでいるトラック」。** 選んでいなければ先頭のトラック
    const int baseTrack = juce::jmax (0, selectedTrackIndex);
    const double startTime = project.snapTime (juce::jmax (0.0, timeSeconds));

    project.beginAction (utf8 ("クリップの貼り付け"));

    clearClipSelection();

    for (const auto& item : EditClipboard::getItems())
    {
        const int targetTrack = baseTrack + item.rowOffset;

        // はみ出したぶんは飛ばす（無いトラックへは置けない）
        if (! juce::isPositiveAndBelow (targetTrack, project.getNumTracks()))
            continue;

        auto track = project.getTrack (targetTrack);

        // **種別の合わないトラックへは置かない。** オーディオクリップをMIDIトラックへ
        // 置くと、再生も書き出しも通らないものが画面にだけ残る
        const bool isMidiClip = item.state.hasType (IDs::MIDICLIP);
        const bool trackTakesMidi = (track.getType() == TrackType::Midi);

        if (isMidiClip != trackTakesMidi)
            continue;

        track.addClipCopy (item.state, startTime + item.timeOffset, &project.getUndoManager());
    }

    if (onModelChanged != nullptr)
        onModelChanged();

    refresh();
    return true;
}

bool TimelineComponent::copyAutomationPoint (AutomationRowRef ref, int pointIndex, bool alsoDelete)
{
    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid() || ! juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
        return false;

    EditClipboard::Item item;
    item.state = lane.getPoint (pointIndex).state.createCopy();
    item.timeOffset = 0.0;
    item.beatOffset = 0.0;   // 1つだけなので基準そのもの（8.139）

    juce::Array<EditClipboard::Item> items;
    items.add (item);

    EditClipboard::set (EditClipboard::Kind::automationPoints, std::move (items));

    if (alsoDelete)
    {
        project.beginAction (utf8 ("オートメーション点の削除"));
        lane.removePoint (pointIndex, &project.getUndoManager());

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
    }

    return true;
}

bool TimelineComponent::pasteAutomationPointsAt (double timeSeconds)
{
    if (EditClipboard::getKind() != EditClipboard::Kind::automationPoints)
        return false;

    // 8.56：**最後に触ったレーンの行へ入れる**（Phase 94／D3）。
    //
    // 貼り付け先を中身側（コピー元のレーン）で決めると、別のトラックのパラメータへ
    // 勝手に書き込むことになる。かといって選択中のトラックから決めると、
    // **1トラックに複数のレーンが並んだときにどれか決まらない**
    auto lane = getAutomationLaneFor (lastEditedAutomationRow);

    if (! lane.state.isValid())
        return false;

    const double startTime = project.snapTime (juce::jmax (0.0, timeSeconds));

    project.beginAction (utf8 ("オートメーション点の貼り付け"));

    // 8.57：**貼った点を選んでおく**（Phase 95／D14）。
    // 続けて動かす・消すことが多いので、置いた側が選ばれているほうが素直
    clearAutomationSelection();

    for (const auto& item : EditClipboard::getItems())
    {
        // 8.139：**位置は拍で**（Phase 177）
        auto added = lane.addPointBeats (juce::jmax (0.0, project.getBeatPositionAt (startTime)
                                                            + item.beatOffset),
                                          (float) item.state[IDs::pointValue],
                                          &project.getUndoManager());

        // 8.37：**繋ぎ方と曲がり具合もいっしょに運ぶ**（Phase 77）。
        // 値だけ貼ると、曲げた線をコピーしたのに直線で入る
        if (added.state.isValid())
        {
            added.setCurve (automationCurveFromString (item.state[IDs::pointCurve].toString()),
                             &project.getUndoManager());
            added.setCurveAmount ((float) item.state.getProperty (IDs::pointCurveAmount, 0.0f),
                                   &project.getUndoManager());

            selectedAutomationPoints.push_back (added.state);
        }
    }

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

bool TimelineComponent::cutSelection()
{
    // 8.57：**レーンの点を選んでいるならそちら**（Phase 95／D14。Deleteキーと同じ振り分け）。
    //
    // 8.56：レーンが専用の行になったので、**開いていてもクリップは切れる**（Phase 94）。
    // Phase 93までは「レーンを開いているあいだは何も切れない」形だった
    pruneAutomationSelection();

    if (! selectedAutomationPoints.empty())
        return copyAutomationSelection (true);

    // 8.95：**時間範囲を選んでいるならそれ**（Phase 135）。
    // クリップの選択より先に見ること——MIDIはクリップの選択に入らないので、
    // 後に置くと「何も選んでいない」と判定されます（8.92）
    if (hasTimeRange)
        return copyTimeRange (true);

    return copySelectedClips (true);
}

bool TimelineComponent::copySelection()
{
    pruneAutomationSelection();

    if (! selectedAutomationPoints.empty())
        return copyAutomationSelection (false);

    if (hasTimeRange)   // 8.95（Phase 135）
        return copyTimeRange (false);

    return copySelectedClips (false);
}

bool TimelineComponent::pasteAt (double timeSeconds)
{
    // **種別で振り分ける**（ノートをアレンジ画面へ貼るような取り違えを起こさない）
    if (EditClipboard::getKind() == EditClipboard::Kind::clips)
        return pasteClipsAt (timeSeconds);

    if (EditClipboard::getKind() == EditClipboard::Kind::automationPoints)
        return pasteAutomationPointsAt (timeSeconds);

    // 8.124：区間まるごと（Phase 160／改善案5）
    if (EditClipboard::getKind() == EditClipboard::Kind::trackRange)
        return pasteRangeAllTracks (timeSeconds);

    // 8.95：**MIDIの中身は、選んでいるMIDIトラックの再生カーソル位置へ**（Phase 135）
    if (EditClipboard::getKind() == EditClipboard::Kind::notes)
        return pasteNotesAt (timeSeconds);

    return false;
}

void TimelineComponent::showAutomationValueEntry (AutomationRowRef ref, int pointIndex)
{
    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid() || ! juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
        return;

    const auto targetId = lane.getTargetId();
    const auto point = lane.getPoint (pointIndex);

    // **入力するのは「実際の値」**（VolumeならdB、Panなら-1〜+1）。
    // モデルは0〜1の正規化値で持っているが（`AutomationModel.h`）、
    // 打ち込むときに正規化値を要求されても何を入れればよいか分からない。
    // 換算は`AutomationTargets`にあるものを通すこと（式をここに書かない）
    const float currentValue = AutomationTargets::toParameterValue (targetId, point.getValue());

    auto* window = new juce::AlertWindow (AutomationTargets::getDisplayName (targetId),
                                           utf8 ("値を入力してください。"),
                                           juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("value", juce::String (currentValue, 2), utf8 ("値"));
    window->addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (utf8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, window, ref, pointIndex, targetId] (int result) mutable
        {
            if (result == 1)
            {
                auto targetLane = getAutomationLaneFor (ref);

                // メニューと入力欄を開いているあいだに、点が消えている可能性がある
                if (targetLane.state.isValid()
                     && juce::isPositiveAndBelow (pointIndex, targetLane.getNumPoints()))
                {
                    const auto text = window->getTextEditorContents ("value").trim();

                    if (text.isNotEmpty())
                    {
                        // **範囲外は丸める**（`fromParameterValue()`が0〜1へ収める）
                        const float normalised = AutomationTargets::fromParameterValue (targetId,
                                                                                          (float) text.getDoubleValue());

                        project.beginAction (utf8 ("オートメーション点の値の変更"));
                        targetLane.getPoint (pointIndex).setValue (normalised, &project.getUndoManager());

                        if (onModelChanged != nullptr)
                            onModelChanged();

                        repaint();
                    }
                }
            }

            delete window;
        }),
        false);
}

//==============================================================================
// 8.61：トラック名をその場で編集する（Phase 99／改善案⑤）

void TimelineComponent::showTrackNameEditor (int rowIndex)
{
    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return;

    auto bounds = getTrackNameBounds (rowIndex);

    if (bounds.isEmpty())
        return;

    // 開き直しは、いま開いているぶんを確定してから
    if (trackNameEditor != nullptr)
        commitTrackNameEditor();

    auto track = project.getTrack (rowIndex);

    // 8.52：**打ち始めた相手を覚える**（Phase 91と同じ話）。
    // 開いているあいだに並べ替えが起きても、別のトラックの名前を書き換えない
    trackNameEditorTrackId = track.getId();

    trackNameEditor = std::make_unique<juce::TextEditor>();

    // ヘッダーの上に重ねるので、**地を塗ること**（半透明だと下の文字が透けて読めない）
    trackNameEditor->setColour (juce::TextEditor::backgroundColourId, AppColours::background);
    trackNameEditor->setColour (juce::TextEditor::textColourId, AppColours::textPrimary);
    trackNameEditor->setColour (juce::TextEditor::outlineColourId, AppColours::purple);
    trackNameEditor->setColour (juce::TextEditor::focusedOutlineColourId, AppColours::purple);
    trackNameEditor->setColour (juce::TextEditor::highlightColourId, AppColours::purple.withAlpha (0.35f));

    trackNameEditor->setFont (juce::FontOptions (13.0f));
    trackNameEditor->setMultiLine (false);
    trackNameEditor->setReturnKeyStartsNewLine (false);
    trackNameEditor->setSelectAllWhenFocused (true);

    // **種別の「[Audio]」は入れない。** そこまで消してから打ち直すことになる
    trackNameEditor->setText (track.getName(), juce::dontSendNotification);

    // 高さは文字が入るぶんだけにして、名前の位置へ縦中央で重ねる
    const int height = juce::jmin (bounds.getHeight(), 20);
    trackNameEditor->setBounds (bounds.withHeight (height)
                                       .withY (bounds.getY() + (bounds.getHeight() - height) / 2));

    trackNameEditor->onReturnKey = [this] { commitTrackNameEditor(); };
    trackNameEditor->onEscapeKey = [this] { dismissTrackNameEditor(); };
    trackNameEditor->onFocusLost = [this] { commitTrackNameEditor(); };

    addAndMakeVisible (*trackNameEditor);
    trackNameEditor->grabKeyboardFocus();
}

void TimelineComponent::commitTrackNameEditor()
{
    if (trackNameEditor == nullptr)
        return;

    const auto newName = trackNameEditor->getText().trim();
    const auto trackId = trackNameEditorTrackId;

    dismissTrackNameEditor();

    // **空欄は無視する。** 名前の無いトラックが並ぶと、どれがどれか分からなくなる
    if (newName.isEmpty() || trackId.isEmpty())
        return;

    auto track = project.findTrackById (trackId);

    if (! track.state.getParent().isValid() || track.getName() == newName)
        return;

    project.beginAction (utf8 ("トラック名の変更"));
    track.setName (newName, &project.getUndoManager());

    // 名前はヘッダーだけでなく、インスペクタ・Console・
    // ピアノロールのトラック一覧にも出ている（1.27）
    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::dismissTrackNameEditor()
{
    if (trackNameEditor == nullptr)
        return;

    // **コールバックの中から直接deleteしない**（`ValueEntrySlider`と同じ形）
    auto* editor = trackNameEditor.release();
    editor->setVisible (false);

    juce::MessageManager::callAsync ([editor] { delete editor; });

    trackNameEditorTrackId.clear();
    grabKeyboardFocus();   // ショートカットが効く状態へ戻す
    repaint();
}

void TimelineComponent::mouseMove (const juce::MouseEvent& e)
{
    // 8.62：**掴める場所ではカーソルを変える**（Phase 100）。
    // 下端の帯は4pxしかなく、見た目には何も無いので、
    // カーソルが変わらないと掴めることに気づけない
    // 8.125：ヘッダーとアレンジの境目（Phase 161／改善案38）。
    // **行の下端より先に見ること**：右下の角では2つの掴みしろが重なるので、
    // 先に見たほうが勝ちます。**幅のほうが端まで通っている**ので、そちらを優先
    if (isOnHeaderResizeEdge (e.getPosition()))
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    const bool overResizeGrab = (e.x < trackHeaderWidth)
                                  && getTrackResizeGrabBounds (getTrackIndexForY (e.y))
                                         .contains (e.getPosition());

    setMouseCursor (overResizeGrab ? juce::MouseCursor::UpDownResizeCursor
                                    : juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseDrag (const juce::MouseEvent& e)
{
    // 8.154：**Ctrl＋クリックの行き先が「動かす」だと決まった瞬間**（Phase 192／本人の要望）。
    //
    // Ctrlは「選択に足す」と「ドラッグで複製」の両方に使うので、
    // 掴んだ時点では区別できません（Phase 52）。**選んでいないクリップを
    // Ctrlで掴んだときだけ**、動き出したここで1本だけの選択に切り替えます
    // ——足すつもりだったなら`mouseUp`が受けるので、ここへは来ません
    if (ctrlClickPendingSelection && e.mouseWasDraggedSinceMouseDown())
    {
        ctrlClickPendingSelection = false;

        if (selectedTrackIndex >= 0 && selectedClipIndex >= 0)
            setSingleClipSelection (selectedTrackIndex, selectedClipIndex, selectedIsMidi);
    }

    // 8.125：ヘッダーの幅（Phase 161／改善案38）。
    //
    // 測るのは**画面座標の差**：幅を変えると中身を配りなおすので、
    // 部品の座標だと掴んでいる場所がずれ得ます（レーンの高さと同じ話。8.122）
    if (headerResizing)
    {
        setTrackHeaderWidth (headerResizeStartWidth
                              + (e.getScreenPosition().x - headerResizeStartScreenX));
        return;
    }

    // 8.103：拍子とテンポのレーン（Phase 142）。**掴んでいるあいだは他より先**
    if (signatureDragRef.isValid())
    {
        updateSignatureDrag (e);
        return;
    }

    // 8.93：時間範囲（Phase 133）。塊と同じ理由で先頭
    if (creatingTimeRange || draggingTimeRange)
    {
        dragTimeRange (e.getPosition());
        return;
    }

    // 8.43：消しゴムでなぞる（Phase 83／C11）。**通ったぶんだけ消える。**
    // 区切りは`mouseDown`で作ってあるので、**なぞり1回ぶんがUndoの1ステップ**になる（3.1）
    if (editTool == EditTool::eraser && ! e.mods.isPopupMenu())
    {
        if (getTimelineArea().contains (e.getPosition()))
        {
            // 8.57：**レーンの行では点を消す**（Phase 95／D14）。
            // クリップと同じ消しゴムが、その行にあるものへ効く形にしてある
            if (findAutomationRowAtY (e.y).isValid())
                eraseAutomationPointAt (e.getPosition());
            else
                eraseClipAt (e.getPosition());
        }

        return;
    }

    // 8.62：行の高さを変えるドラッグ（Phase 100）。
    // **X座標は見ない。** ヘッダーの外へ出ても掴んだままにしておかないと、
    // 大きく広げようとしたときに途中で外れる（Phase 98までのパンと同じ考え方）
    if (dragMode == DragMode::ResizeTrack)
    {
        if (juce::isPositiveAndBelow (resizeTrackIndex, project.getNumTracks()))
        {
            const int wanted = juce::jlimit (minimumTrackRowHeight, maximumTrackRowHeight,
                                              resizeTrackStartHeight + (e.y - resizeTrackStartY));

            auto track = project.getTrack (resizeTrackIndex);

            if (track.getCustomRowHeight() != wanted)
            {
                track.setCustomRowHeight (wanted);

                // 行の高さが変わる＝下の行の位置も変わる（つまみ等の子も置き直す）
                resized();
                repaint();
            }
        }

        return;
    }

    // 仕様書6.2：範囲選択（ラバーバンド）のドラッグ（Phase 51）
    if (rangeSelecting)
    {
        rangeSelectBounds = juce::Rectangle<int>::leftTopRightBottom (
                                juce::jmin (rangeSelectAnchor.x, e.x),
                                juce::jmin (rangeSelectAnchor.y, e.y),
                                juce::jmax (rangeSelectAnchor.x, e.x),
                                juce::jmax (rangeSelectAnchor.y, e.y));
        repaint();
        return;
    }

    // 仕様書5.9：マーカーのドラッグ（Phase 49）。Phase 54で共通のスナップへ（8.14）
    if (markerDragIndex >= 0)
    {
        markerDragPreviewTime = project.snapTime (xToTime (e.x));

        repaint();
        return;
    }

    // 仕様書5.9：ループ範囲のドラッグ（Phase 48）
    if (loopDragMode != LoopDragMode::none)
    {
        updateLoopDrag (e);
        return;
    }

    // 仕様書5.2.3：コード区間のドラッグ（Phase 45）。クリップの選択とは無関係
    if (chordDragMode != ChordDragMode::none)
    {
        updateChordDrag (e);
        return;
    }

    // シークとオートメーションはクリップの選択と無関係なので、
    // 下の「選択が要る」判定より前に処理する
    if (dragMode == DragMode::Seek)
    {
        seekToX (e.x);
        return;
    }

    // 8.57：ペンでのなぞり書き（Phase 95／D14）。
    // なぞり1回ぶんがUndoの1ステップになる（区切りはmouseDownで作ってある。3.1）
    if (dragMode == DragMode::AutomationPaint)
    {
        paintAutomationPointAt (automationPaintRow, e.getPosition());
        return;
    }

    // 8.57：矢印ツールでレーンを押したあとのドラッグ（Phase 95／D14）。
    // **動かしたと分かった時点で範囲選択に切り替える**（押しただけなら点を置く）
    if (automationPendingAdd || automationRangeSelecting)
    {
        automationPendingAdd = false;
        automationRangeSelecting = true;

        automationRangeBounds = juce::Rectangle<int>::leftTopRightBottom (
                                    juce::jmin (automationRangeAnchor.x, e.x),
                                    juce::jmin (automationRangeAnchor.y, e.y),
                                    juce::jmax (automationRangeAnchor.x, e.x),
                                    juce::jmax (automationRangeAnchor.y, e.y));
        repaint();
        return;
    }

    // 8.37：曲がり具合のつまみのドラッグ（Phase 77）。
    // **上下だけ見る。** ピアノロール下のレーンと同じ操作にしてある
    if (dragMode == DragMode::CurveHandle)
    {
        auto lane = getAutomationLaneFor (curveDragRow);

        if (! lane.state.isValid() || ! juce::isPositiveAndBelow (curvePointIndex, lane.getNumPoints()))
            return;

        auto point = lane.getPoint (curvePointIndex);

        // S字の区間を曲げたときは「曲線」へ変わる（動かしたとおりの形になるほうが分かりやすい）
        point.setCurve (AutomationCurve::Linear, &project.getUndoManager());
        point.setCurveAmount (AutomationCurveUI::amountForHandleDrag (curveDragFromY, curveDragToY,
                                                                       (float) e.getPosition().y),
                               &project.getUndoManager());

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    if (dragMode == DragMode::AutomationPoint)
    {
        auto lane = getAutomationLaneFor (automationDragRow);

        if (! lane.state.isValid() || ! juce::isPositiveAndBelow (automationPointIndex, lane.getNumPoints()))
            return;

        // 掴んでいる点をそのまま動かす。区切りはmouseDownで作ってあるので、
        // ドラッグ全体が1回のUndoにまとまる
        auto point = lane.getPoint (automationPointIndex);
        point.setTime (juce::jmax (0.0, xToTime (e.x)), &project.getUndoManager());
        point.setValue (yToAutomationValue (automationDragRow, e.y), &project.getUndoManager());

        repaint();
        return;
    }

    // Phase 34／36：トラックヘッダーを掴んでの並べ替え。
    // **ここではモデルを触らず、挿入位置を覚えて線を引き直すだけ。**
    // 実際に動かすのはドロップ時（mouseUp）で、Undoも1ステップで済む。
    if (dragMode == DragMode::ReorderTrack)
    {
        if (! juce::isPositiveAndBelow (reorderSourceIndex, project.getNumTracks()))
            return;

        // 8.51：行だけでなく**入れ先のフォルダも決める**（Phase 90／D2）
        const int previousSlot = reorderTargetSlot;
        const auto previousFolder = reorderTargetFolderId;
        const auto previousImageBounds = getReorderDragImageBounds();

        updateReorderTarget (e.getPosition());

        if (reorderTargetSlot != previousSlot || reorderTargetFolderId != previousFolder)
        {
            repaint();   // 予告線と、入れ先のフォルダの縁取りが動く
            return;
        }

        // 8.70：**写しは指について来るので、行をまたがなくても描き直します**（Phase 109）。
        // ただし全体ではなく、**写しが居た場所と行く場所だけ**——
        // アレンジ画面は広いので、指を動かすたびに全部描き直すと重くなります
        const auto imageBounds = getReorderDragImageBounds();

        if (imageBounds != previousImageBounds)
            repaint (previousImageBounds.getUnion (imageBounds).expanded (2));

        return;
    }

    if (dragMode == DragMode::None || selectedTrackIndex < 0)
        return;

    const int deltaXPixels = e.getPosition().x - dragStartMousePosition.x;
    const double deltaSeconds = deltaXPixels / pixelsPerSecond;

    if (dragMode == DragMode::Move)
    {
        // Phase 54：クリップもスナップに従う（8.14）。Phase 53まではどこにでも置けた。
        // **寄せるのは「移動後の開始位置」で、移動量ではない。**
        // 移動量を寄せると、もともと拍から外れているクリップは外れたまま動く
        dragPreviewStartTime = project.snapTime (dragOriginalStartTime + deltaSeconds);

        // Y座標からドラッグ先のトラックを求める。移動先は**同じ種別のトラック**に限る
        // （オーディオクリップをMIDIトラックへ、あるいはその逆へは落とせない）。
        const auto requiredType = selectedIsMidi ? TrackType::Midi : TrackType::Audio;
        const int targetTrack = getTrackIndexForY (e.getPosition().y);

        if (juce::isPositiveAndBelow (targetTrack, project.getNumTracks())
            && project.getTrack (targetTrack).getType() == requiredType)
        {
            dragPreviewTrackIndex = targetTrack;
        }
    }
    else if (dragMode == DragMode::TrimRight)
    {
        // **寄せるのは終端の位置**（長さではない）。長さを寄せると、
        // 開始が拍から外れているクリップの終端がいつまでも拍に乗らない
        const double end = project.snapTime (dragOriginalStartTime + dragOriginalLength + deltaSeconds);
        dragPreviewLength = juce::jmax (minClipLength, end - dragOriginalStartTime);
    }
    else if (dragMode == DragMode::WarpMarker)
    {
        // 8.150：**掴んだ音が鳴る時刻を動かす**（Phase 188／8.48）。
        // 寄せるのは**タイムライン上の位置**——「この打点を、この拍へ」が目的なので
        const double wanted = project.snapTime (dragOriginalStartTime
                                                  + dragOriginalWarpClip + deltaSeconds);

        dragPreviewWarpClip = wanted - dragOriginalStartTime;
    }
    else if (dragMode == DragMode::StretchRight)
    {
        // 8.149：**掴んだ時点のソースの長さを保ったまま、タイムラインの長さを変える**
        // （Phase 187/8.48）。倍率はその比。TrimRightと同じく**終端の位置を寄せる**
        const double end = project.snapTime (dragOriginalStartTime + dragOriginalLength + deltaSeconds);
        const double wanted = juce::jmax (minClipLength, end - dragOriginalStartTime);

        // 倍率の上下限に当たったら、長さのほうを合わせる（見えている絵と結果を揃える）
        const double sourceLength = dragOriginalLength / juce::jmax (1.0e-9, dragOriginalStretch);
        const double ratio = juce::jlimit (AudioTransform::minStretch, AudioTransform::maxStretch,
                                            wanted / juce::jmax (1.0e-9, sourceLength));

        dragPreviewStretch = ratio;
        dragPreviewLength = sourceLength * ratio;
    }
    else if (dragMode == DragMode::TrimLeft)
    {
        // 左端も、動かした先の位置を寄せてから移動量に直す（TrimRightと同じ考え方）。
        // **オフセットも同じ量だけ動かすこと**：中身をずらさずに窓だけを縮める操作なので、
        // 開始位置と中身の頭は必ず同じ量だけ動く（1.14）
        const double wantedStart = project.snapTime (dragOriginalStartTime + deltaSeconds);

        // 左端の移動量は、オフセットが0を下回らず、長さが最小値を下回らない範囲に制限する。
        //
        // 8.149：**`delta`はタイムラインの秒、オフセットはソースの秒**（Phase 187／8.48）。
        // 伸縮ぶんで割ってから足すこと——下限も、ソースの秒をタイムラインへ直して比べる
        const double stretch = juce::jmax (1.0e-9, dragOriginalStretch);

        const double delta = juce::jlimit (-dragOriginalOffset * stretch,
                                            dragOriginalLength - minClipLength,
                                            wantedStart - dragOriginalStartTime);
        dragPreviewStartTime = dragOriginalStartTime + delta;
        dragPreviewOffset = dragOriginalOffset + delta / stretch;
        dragPreviewLength = dragOriginalLength - delta;
    }
    else if (dragMode == DragMode::FadeIn)
    {
        dragPreviewFadeIn = juce::jlimit (0.0, dragOriginalLength, dragOriginalFadeIn + deltaSeconds);
    }
    else if (dragMode == DragMode::FadeOut)
    {
        dragPreviewFadeOut = juce::jlimit (0.0, dragOriginalLength, dragOriginalFadeOut - deltaSeconds);
    }

    repaint();
}

void TimelineComponent::mouseUp (const juce::MouseEvent& e)
{
    // 8.125：ヘッダーの幅（Phase 161／改善案38）。
    // **覚えるのは離したときだけ**：ドラッグ中に呼ぶと、1回の操作で
    // 設定ファイルへ何十回も書くことになります（レーンの高さと同じ。8.122）
    if (headerResizing)
    {
        headerResizing = false;

        if (onTrackHeaderWidthChanged != nullptr)
            onTrackHeaderWidthChanged();

        return;
    }

    // 8.103：拍子とテンポのレーン（Phase 142）
    if (signatureDragRef.isValid())
    {
        commitSignatureDrag();
        return;
    }

    // 8.93：時間範囲（Phase 133）
    if (creatingTimeRange || draggingTimeRange)
    {
        finishTimeRangeDrag (e);
        return;
    }

    // 8.62：行の高さのドラッグ（Phase 100）。モデルへはドラッグ中に書いてあるので畳むだけ
    if (dragMode == DragMode::ResizeTrack)
    {
        dragMode = DragMode::None;
        resizeTrackIndex = -1;
        return;
    }

    // 仕様書6.2：**Ctrl＋クリック（動かさずに離した）は、選択に足す／外す**（Phase 52）。
    // Ctrlはドラッグでの複製にも使うので、掴んだ時点では区別できず、
    // 「動かしたかどうか」が分かるここで振り分ける。
    if (dragIsCopy && ! e.mouseWasDraggedSinceMouseDown())
    {
        dragIsCopy = false;
        ctrlClickPendingSelection = false;   // 8.154：動かさなかったので畳まない
        dragMode = DragMode::None;

        if (selectedTrackIndex >= 0 && selectedClipIndex >= 0)
            toggleClipSelection (selectedTrackIndex, selectedClipIndex, selectedIsMidi);

        return;
    }

    // 仕様書6.2：範囲選択は、離した時点の矩形で選び直す（Phase 51）。
    //
    // **動かさずに離したなら、範囲選択ではなくカーソル移動**（Phase 69／8.29の表）。
    // 空いている場所のクリックとドラッグは同じ`mouseDown`から始まるので、
    // ここで振り分けます（8.10と同じ形）。
    if (rangeSelecting)
    {
        rangeSelecting = false;

        if (e.mouseWasDraggedSinceMouseDown())
            applyRangeSelection();
        else if (getTimelineArea().contains (e.getPosition()))
            seekToX (e.x);

        repaint();
        return;
    }

    // 仕様書5.9：マーカーの移動も、離したときに1回だけモデルへ書く（Phase 49）
    if (markerDragIndex >= 0)
    {
        if (juce::isPositiveAndBelow (markerDragIndex, project.getNumMarkers()))
        {
            auto marker = project.getMarker (markerDragIndex);

            if (! juce::approximatelyEqual (marker.getTime(), markerDragPreviewTime))
            {
                project.beginAction (utf8 ("マーカーの移動"));
                marker.setTime (markerDragPreviewTime, &project.getUndoManager());

                // **時刻順の並びを保つ。** 崩れると「次のマーカーへ」が飛び飛びになる
                project.sortMarkers (&project.getUndoManager());
            }
        }

        markerDragIndex = -1;
        repaint();
        return;
    }

    // 仕様書5.9：ループ範囲も、離したときに1回だけモデルへ書く（Phase 48）
    if (loopDragMode != LoopDragMode::none)
    {
        commitLoopDrag();
        return;
    }

    // 仕様書5.2.3：コード区間のドラッグは、ここで初めてモデルへ反映する（Phase 45）。
    // ドラッグ中に書くと、途中の位置が全部Undoに積まれる。
    if (chordDragMode != ChordDragMode::none)
    {
        commitChordDrag();
        return;
    }

    // シークはモデルを一切変えないので、ここで畳んで終わる。
    // 下の処理へ落とすと、以前選んでいたクリップに対して移動が適用されてしまう。
    if (dragMode == DragMode::Seek)
    {
        dragMode = DragMode::None;
        return;
    }

    // Phase 36：並べ替えはここで初めてモデルへ反映する（線で予告していた位置へ動かす）。
    // 下の処理へ落とすと、選択中のクリップに対して移動が適用されてしまう。
    if (dragMode == DragMode::ReorderTrack)
    {
        const int from = reorderSourceIndex;
        const int to = reorderSlotToTrackIndex (reorderTargetSlot);
        const auto targetFolderId = reorderTargetFolderId;

        // 8.202：**動かさなかったなら、ここで1本へ畳みます**（Phase 236）。
        //
        // 掴むときは選択を保ちました（複数まとめて動かせるように）。
        // **ただのクリックだった場合**は「選び直したのに減らない」ことになるので、
        // ここで畳みます——**動かしたかどうかは、離すときに初めて分かります。**
        if (collapseSelectionOnMouseUpId.isNotEmpty())
        {
            const auto id = collapseSelectionOnMouseUpId;
            collapseSelectionOnMouseUpId.clear();

            if (to < 0)   // -1のまま＝ドラッグしていない
            {
                setSingleTrackSelection (id);
                publishSelection();
            }
        }

        dragMode = DragMode::None;
        reorderSourceIndex = -1;
        reorderTargetSlot = -1;
        reorderTargetFolderId.clear();

        // 8.70：写しを捨てる（Phase 109）。**行の高さぶんの画像**なので、
        // 持ったままにする理由がありません（次に掴んだときに撮り直します）
        reorderDragImage = {};

        // reorderTargetSlotが-1のまま＝ドラッグしていない（ただのクリック）
        if (to >= 0 && juce::isPositiveAndBelow (from, project.getNumTracks()))
        {
            // 8.200：**選んでいるぶん全部を動かします**（Phase 235／改善案5の9）。
            //
            // Phase 234まで、動くのは**掴んだ1本だけ**でした。
            // 5本選んでフォルダへ落としても、入るのは1本。
            // メニューの「フォルダへ入れる」は**Phase 197から全部入れていた**ので、
            // **同じ操作なのに入口で結果が違う**状態でした（8.159）。
            //
            // 数え方はメニュー側と揃えます（`ArrangeView::getTrackIdsForHeaderAction()`）：
            // **掴んだ行が選択に入っていなければ、その1本だけ。**
            const auto ids = getTrackIdsForReorder (from);

            // 8.51：**動かすのと入れ先を変えるのは1回の操作**（Phase 90／D2）。
            // 別々に呼ぶと、Ctrl+Zが2回要ることになる（3.1）
            project.beginAction (utf8 ("トラックを移動"));

            // **上から順に、置き先を1つずつ進めること。** 同じ`to`へ全部入れると
            // 並びが逆さまになります（後から入れたものが上へ来る）
            int slot = to;

            for (const auto& id : ids)
            {
                auto track = project.findTrackById (id);

                if (! track.state.getParent().isValid() || id == targetFolderId)
                    continue;   // フォルダ自身は自分の中へ入れない

                // 8.203：**区切りは上で1回開いています**（Phase 237）。
                // ここで`true`のままだと、**1本ごとにCtrl+Zが要ります**
                project.moveTrackToSlot (track, slot, targetFolderId, false);
                ++slot;
            }

            if (onModelChanged != nullptr)
                onModelChanged();
        }

        repaint(); // 線を消す
        return;
    }

    // 8.57：なぞり書き（Phase 95／D14）。モデルへはドラッグ中に書いてあるので畳むだけ。
    // **並べ直しは必須**：右から左へなぞると時刻が逆順に入る（8.38）
    if (dragMode == DragMode::AutomationPaint)
    {
        auto lane = getAutomationLaneFor (automationPaintRow);

        if (lane.state.isValid())
            lane.sortPoints (&project.getUndoManager());

        dragMode = DragMode::None;
        automationPaintStarted = false;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    // 8.57：矢印ツールでレーンを押したときの振り分け（Phase 95／D14）。
    //
    // **動かさずに離したなら点を1つ置く。動かしたなら範囲選択。**
    // どちらも同じmouseDownから始まるので、離した時点で分ける（8.38と同じ形）
    if (automationPendingAdd || automationRangeSelecting)
    {
        const bool wasDragged = automationRangeSelecting;
        const auto ref = automationPendingRow;

        automationPendingAdd = false;
        automationRangeSelecting = false;

        if (wasDragged)
        {
            applyAutomationRangeSelection (ref);
        }
        else
        {
            clearAutomationSelection();

            auto lane = getAutomationLaneFor (ref);
            float lineValue = 0.0f;

            // 8.59：**点が増えるのは「線の上」を押したときだけ**（Phase 96）。
            //
            // それまでは行のどこを押しても、押した高さに点が増えていました。
            // レーンの行を**選ぶ・掴む**操作が増えたことで、
            // 「触ったつもりが書き換わっていた」が起きやすくなったためです。
            //
            // **置く高さも線の上**（押した高さではない）。線をなぞって形を作るときに、
            // 押すたびに段差ができるのを防ぎます。高さを変えたければ、
            // 置いた点をそのままドラッグしてください
            if (lane.state.isValid()
                 && hitTestAutomationLine (ref, automationPendingPosition, lineValue))
            {
                project.beginAction (utf8 ("オートメーション点の追加"));

                // **寄せません**（8.14の「寄せないもの」と同じ扱い）
                auto added = lane.addPoint (juce::jmax (0.0, xToTime (automationPendingPosition.x)),
                                             lineValue, &project.getUndoManager());

                // **置いた点は選んでおく**（続けて動かす・消すことが多い。8.29）
                if (added.state.isValid())
                    selectedAutomationPoints.push_back (added.state);

                if (onModelChanged != nullptr)
                    onModelChanged();
            }
            else
            {
                // 線から外れたところ＝**その行を選ぶだけ**（トラック行の空きと同じ扱い）
                selectAutomationRow (ref);
            }
        }

        dragMode = DragMode::None;
        repaint();
        return;
    }

    // 8.37：曲がり具合のつまみ（Phase 77）。モデルへはドラッグ中に書いてあるので畳むだけ
    if (dragMode == DragMode::CurveHandle)
    {
        dragMode = DragMode::None;
        curveDragRow = {};
        curvePointIndex = -1;
        repaint();
        return;
    }

    if (dragMode == DragMode::AutomationPoint)
    {
        // ドラッグで点の前後関係が入れ替わっていることがあるので、時刻順に直す。
        // 順序が崩れたままだと、値の補間（AutomationLane::getValueAt）が破綻する。
        {
            auto lane = getAutomationLaneFor (automationDragRow);

            if (lane.state.isValid())
            {
                // 8.57：**選んでいる他の点も同じだけ動かす**（Phase 95／D14）。
                // **掴んだ点を書いた後に呼ぶこと**（先に呼ぶと、残りの移動だけが
                // 前のUndoステップに入る。ノートと同じ。8.13のA2）
                if (! automationDragOthers.empty()
                     && juce::isPositiveAndBelow (automationPointIndex, lane.getNumPoints()))
                {
                    auto dragged = lane.getPoint (automationPointIndex);

                    moveOtherSelectedAutomationPoints (dragged.getTime() - automationDragAnchorTime,
                                                        dragged.getValue() - automationDragAnchorValue);
                }

                lane.sortPoints (&project.getUndoManager());
            }
        }

        automationDragOthers.clear();
        dragMode = DragMode::None;
        automationDragRow = {};
        automationPointIndex = -1;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    if (dragMode != DragMode::None && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
    {
        auto& undoManager = project.getUndoManager();

        // ドラッグ1回ぶんをUndoの1ステップにする。ここで区切らないと、
        // 起動以降の全編集がひとつのステップにまとまってしまう
        // （juce::UndoManagerは自動では区切らない）。
        switch (dragMode)
        {
            case DragMode::Move:      project.beginAction (utf8 ("クリップの移動")); break;
            case DragMode::TrimLeft:
            case DragMode::TrimRight: project.beginAction (utf8 ("クリップのトリム")); break;
            case DragMode::FadeIn:
            case DragMode::FadeOut:   project.beginAction (utf8 ("フェードの変更")); break;
            case DragMode::None:      break;
        }

        auto sourceTrack = project.getTrack (selectedTrackIndex);

        // 仕様書6.2：Ctrl＋ドラッグは「移動」ではなく「複製」（Phase 52）。
        // **元のクリップは動かさず、落とした位置へ新しく置く。**
        // 掴んだ時点でCtrlが押されていたかで決まる（途中で離しても複製のまま）。
        if (dragIsCopy && dragMode == DragMode::Move)
        {
            dragIsCopy = false;

            const bool moved = (dragPreviewTrackIndex != selectedTrackIndex)
                            || ! juce::approximatelyEqual (dragPreviewStartTime, dragOriginalStartTime);

            if (moved && juce::isPositiveAndBelow (dragPreviewTrackIndex, project.getNumTracks()))
            {
                auto targetTrack = project.getTrack (dragPreviewTrackIndex);

                auto sourceState = sourceTrack.getClip (selectedClipIndex).state;   // 8.94

                // 中身ごとの複製はモデル側の1箇所に集めてある（8.9）。
                // 置く場所だけ、落とした位置へ直す
                project.beginAction (utf8 ("クリップの複製"));
                auto copy = targetTrack.duplicateClip (sourceState, &undoManager);

                // 8.137：**アクセサを通すこと**（Phase 175／1.27）
                if (copy.isValid())
                    AudioClip (copy).setStartTime (dragPreviewStartTime, &undoManager);

                // 複製したものを選び直す。**元を選んだままにすると、
                // 続けてもう一度ドラッグしたときに、複製ではなく元が動く**
                clearClipSelection();

                if (onModelChanged != nullptr)
                    onModelChanged();
            }

            dragMode = DragMode::None;
            repaint();
            return;
        }

        dragIsCopy = false;

        // 8.94：**MIDIはここへ来ません**（Phase 134）。クリップの移動とトリムは、
        // 塊／範囲のドラッグ（8.92・8.93）に置き換わりました
        if (selectedIsMidi)
        {
            dragMode = DragMode::None;
            repaint();
            return;
        }

        auto clip = sourceTrack.getClip (selectedClipIndex);
        bool changed = false;

        if (dragMode == DragMode::Move)
        {
            if (dragPreviewTrackIndex != selectedTrackIndex)
            {
                // トラックをまたいだ移動：新トラックへクリップを作り直し、元のクリップを削除する
                // （区切りは上のswitchで済ませているので、ここでは作らない）
                auto targetTrack = project.getTrack (dragPreviewTrackIndex);
                auto newClip = targetTrack.addAudioClip (clip.getSourceFilePath(), dragPreviewStartTime,
                                                         dragPreviewLength, &undoManager, dragPreviewOffset);
                newClip.setFadeInSeconds (dragPreviewFadeIn, &undoManager);
                newClip.setFadeOutSeconds (dragPreviewFadeOut, &undoManager);
                newClip.setHitPoints (clip.getHitPoints(), &undoManager);
                newClip.setGainDb (clip.getGainDb(), &undoManager);   // Phase 80：ゲインも運ぶ（8.40）
                newClip.setReversed (clip.isReversed(), &undoManager);   // Phase 86：向きも運ぶ（8.46）
                sourceTrack.removeClip (clip, &undoManager);

                selectedTrackIndex = dragPreviewTrackIndex;
                selectedClipIndex = project.getTrack (dragPreviewTrackIndex).getNumClips() - 1;
                changed = true;
            }
            else if (dragPreviewStartTime != dragOriginalStartTime)
            {
                clip.setStartTime (dragPreviewStartTime, &undoManager);
                changed = true;
            }

            // 8.42：**選んでいる他のクリップも同じだけ動かす**（Phase 82）。
            // **掴んだものを書いた後に呼ぶこと**（3.1）
            moveOtherSelectedClipsByDrag (dragPreviewStartTime - dragOriginalStartTime);
        }
        else if (dragMode == DragMode::TrimRight)
        {
            if (dragPreviewLength != dragOriginalLength)
            {
                clip.setLength (dragPreviewLength, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::WarpMarker)
        {
            // 8.150：**両隣を追い越させないのはモデルの仕事**（Phase 188）。
            // ここで先に丸めると、モデル側の上下限と2箇所が同じ判断を持ちます（1.27）
            if (dragPreviewWarpClip != dragOriginalWarpClip)
            {
                undoManager.beginNewTransaction();
                changed = clip.moveWarpMarker (dragWarpMarkerSource, dragPreviewWarpClip, &undoManager);
            }
        }
        else if (dragMode == DragMode::StretchRight)
        {
            // 8.149：**長さと倍率は必ず対で書くこと**（Phase 187）。
            // 片方だけだと、ソースの使う範囲が勝手に変わります
            if (dragPreviewLength != dragOriginalLength)
            {
                undoManager.beginNewTransaction();
                clip.setLength (dragPreviewLength, &undoManager);
                clip.setStretch (dragPreviewStretch, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::TrimLeft)
        {
            if (dragPreviewStartTime != dragOriginalStartTime)
            {
                undoManager.beginNewTransaction();
                clip.setStartTime (dragPreviewStartTime, &undoManager);
                clip.setOffset (dragPreviewOffset, &undoManager);
                clip.setLength (dragPreviewLength, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::FadeIn)
        {
            if (dragPreviewFadeIn != dragOriginalFadeIn)
            {
                clip.setFadeInSeconds (dragPreviewFadeIn, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::FadeOut)
        {
            if (dragPreviewFadeOut != dragOriginalFadeOut)
            {
                clip.setFadeOutSeconds (dragPreviewFadeOut, &undoManager);
                changed = true;
            }
        }

        if (changed && onModelChanged != nullptr)
            onModelChanged();
    }

    dragMode = DragMode::None;
    repaint();
}

bool TimelineComponent::keyPressed (const juce::KeyPress& key)
{
    // 8.57：**レーンの点を選んでいるならそちらを消す**（Phase 95／D14）。
    // **いちばん先に見ること。** 点を選んでいる間もクリップの選択は残っているので、
    // 後に置くとクリップのほうが消える
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        pruneAutomationSelection();

        if (! selectedAutomationPoints.empty())
        {
            deleteSelectedAutomationPoints();
            return true;
        }
    }

    // 8.129：**Escapeで選択を解く**（Phase 165／本人の要望）。
    // マウスで外を押すのと同じことを、手を動かさずにできる入口
    if (key == juce::KeyPress::escapeKey && hasTimeRange)
    {
        clearTimeRange();
        return true;
    }

    // 8.93：**時間範囲を選んでいるなら、その中のノートを消す**（Phase 133）。
    // レーンの点と同じ理由で、クリップの削除より先に見ること
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && hasTimeRange)
    {
        // 8.124：全トラックの範囲なら、全トラックから消す（Phase 159／改善案5）。
        // **1つのUndoにまとめてある**ので、Ctrl+Zで1回戻せます
        if (timeRangeAllTracks)
        {
            deleteRangeAllTracks (timeRangeStart, timeRangeEnd);
        }
        else
        {
            // 8.158：選んでいるトラック全部から消す（Phase 196）。
            // **1つのUndoにまとめてある**ので、Ctrl+Zで1回戻せます
            for (const int t : getTimeRangeTrackIndices())
                deleteNotesInRange (t, timeRangeStart, timeRangeEnd);
        }

        return true;
    }

    // 8.29の表：選択したコード区間を Delete で消す（Phase 70）。
    // **クリップの削除より先に見ること。** コード区間を選んでいるあいだは
    // クリップの選択が残っていないので順番の争いは起きないが、
    // 先に置いておくほうが「何が消えるか」を読み比べやすい
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
         && selectedChordRegion.isValid())
    {
        if (deleteSelectedChordRegion())
            return true;
    }

    // 仕様書6.2：複数選択しているときは、まとめて消す（Phase 51）。
    // **IDで引き直してから消すこと**：1つ消すたびに他の番号がずれる（1.32）
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedClips.size() > 1)
    {
        project.beginAction (utf8 ("クリップの削除"));

        for (const auto& ref : selectedClips)
        {
            auto track = project.findTrackById (ref.trackId);

            if (! track.state.getParent().isValid())
                continue;

            if (ref.isMidi)
                continue;   // 8.94：クリップはオーディオだけ（Phase 134）

            {
                for (int c = track.getNumClips(); --c >= 0;)
                    if (track.getClip (c).getId() == ref.clipId)
                        track.removeClip (track.getClip (c), &project.getUndoManager());
            }
        }

        clearClipSelection();

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return true;
    }

    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
    {
        auto track = project.getTrack (selectedTrackIndex);

        project.beginAction (utf8 ("クリップの削除"));

        if (! selectedIsMidi)   // 8.94：クリップはオーディオだけ（Phase 134）
            track.removeClip (track.getClip (selectedClipIndex), &project.getUndoManager());

        selectedTrackIndex = -1;
        selectedClipIndex = -1;
        selectedIsMidi = false;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return true;
    }

    return false;
}

bool TimelineComponent::eraseClipAt (juce::Point<int> position)
{
    // 8.43：消しゴム（Phase 83／C11）。**触れた1つだけを消す。**
    // 選択中かどうかは見ません——消しゴムは「触れたものを消す」道具なので、
    // 選択に入っていない別のクリップまで巻き込むと、狙って消せなくなります
    int trackIndex = -1;
    int clipIndex = -1;
    bool isMidiClip = false;

    if (! hitTestClip (position, trackIndex, clipIndex, isMidiClip))
        return false;

    auto track = project.getTrack (trackIndex);

    if (isMidiClip)
    {
        // 8.92：**触れた塊のノートを消す**（Phase 132）。
        // 消す入れ物が無いので、範囲でノートを消します（8.91）
        const auto blocks = getNoteBlocksFor (trackIndex);

        if (! juce::isPositiveAndBelow (clipIndex, (int) blocks.size()))
            return false;

        const auto& block = blocks[(size_t) clipIndex];
        deleteNotesInRange (trackIndex, block.startTime, block.endTime);
        return true;
    }
    else
    {
        if (! juce::isPositiveAndBelow (clipIndex, track.getNumClips()))
            return false;

        track.removeClip (track.getClip (clipIndex), &project.getUndoManager());
    }

    // 掴んだままの番号を残さない（消えたクリップを動かし続けることになる）
    if (trackIndex == selectedTrackIndex && clipIndex == selectedClipIndex)
    {
        selectedTrackIndex = -1;
        selectedClipIndex = -1;
    }

    clearClipSelection();

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}
void TimelineComponent::drawClipDragPreview (juce::Graphics& g)
{
    // 8.42：移動中の行き先（Phase 82／C17）。**ノートのプレビューと同じ見た目**にしてある
    // （半透明の塗り＋はっきりした枠。塗りだけだと元と行き先が同じ濃さになる。8.13）
    auto drawPreview = [&g] (juce::Rectangle<int> bounds)
    {
        if (bounds.getWidth() <= 0)
            return;

        g.setColour (AppColours::orange.withAlpha (0.35f));
        g.fillRect (bounds);
        g.setColour (AppColours::orange);
        g.drawRect (bounds, 2);
    };

    auto boundsFor = [this] (int trackIndex, double startTime, double lengthSeconds)
    {
        return juce::Rectangle<int> (timeToX (startTime), getTrackRowY (trackIndex) + 4,
                                      juce::jmax (4, (int) (lengthSeconds * pixelsPerSecond)),
                                      getTrackAreaHeight (trackIndex) - 8);
    };

    // 8.150：**ワープマーカーを掴んでいるときは、行き先の線だけ描く**（Phase 188／8.48）。
    //
    // クリップの枠を出す形（移動やトリムのプレビュー）にはしません——
    // **クリップ自体は動かない**ので、枠が出ると「何が動くのか」が読めなくなります。
    // 動かした結果の音は、離すまで作られません（`ClipAudioRenderer`）
    if (dragMode == DragMode::WarpMarker
         && juce::isPositiveAndBelow (dragPreviewTrackIndex, project.getNumTracks()))
    {
        auto bounds = boundsFor (dragPreviewTrackIndex, dragOriginalStartTime, dragOriginalLength);
        const int x = bounds.getX() + (int) std::round (dragPreviewWarpClip * pixelsPerSecond);

        g.setColour (AppColours::orange);
        g.drawLine ((float) x, (float) bounds.getY(), (float) x, (float) bounds.getBottom(), 2.0f);
        g.fillRect (x - warpHandleGrabWidth / 2, bounds.getY(),
                     warpHandleGrabWidth, warpHandleHeight);
        return;
    }

    // 掴んでいるクリップ。**行き先のトラックは変わり得る**（縦にドラッグしたとき）
    drawPreview (boundsFor (dragPreviewTrackIndex, dragPreviewStartTime, dragPreviewLength));

    // 8.42：**選んでいる他のクリップも同じだけずらして描く**（Phase 82）。
    // ずらす量は掴んだクリップの移動量から求める（1つずつ「落とした位置」で計算すると、
    // 全部が同じ場所に重なる。ノートと同じ考え方。8.13）
    const double deltaTime = dragPreviewStartTime - dragOriginalStartTime;

    const auto draggedRef = makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi);

    for (const auto& ref : selectedClips)
    {
        if (ref.clipId == draggedRef.clipId)
            continue;

        int trackIndex = -1;

        for (int t = 0; t < project.getNumTracks(); ++t)
            if (project.getTrack (t).getId() == ref.trackId)
            {
                trackIndex = t;
                break;
            }

        if (trackIndex < 0)
            continue;

        auto track = project.getTrack (trackIndex);

        // **IDで引き直す**（番号は増減でずれる。1.32）
        if (ref.isMidi)
            continue;   // 8.94：クリップはオーディオだけ（Phase 134）

        {
            for (int c = 0; c < track.getNumClips(); ++c)
            {
                auto other = track.getClip (c);

                if (other.getId() != ref.clipId)
                    continue;

                drawPreview (boundsFor (trackIndex, juce::jmax (0.0, other.getStartTime() + deltaTime),
                                         other.getLength()));
                break;
            }
        }
    }
}

void TimelineComponent::moveOtherSelectedClipsByDrag (double deltaTime)
{
    // 8.42：**選んでいる他のクリップも同じだけ動かす**（Phase 82）。
    //
    // **掴んだクリップを書いた後に呼ぶこと**（先に呼ぶと、残りの移動だけが
    // 前のUndoステップに入る。ノートと同じ。8.13のA2）。
    //
    // **動かすのは時間方向だけ。** 掴んだものがトラックをまたいでも、
    // 他は自分のトラックに残ります（まとめて別トラックへ移すのは、
    // 「どのトラックへ何本ぶんずらすか」を決める話になるので別の宿題）。
    if (deltaTime == 0.0 || selectedClips.size() < 2)
        return;

    auto& undoManager = project.getUndoManager();
    const auto draggedRef = makeClipRef (selectedTrackIndex, selectedClipIndex, selectedIsMidi);

    for (const auto& ref : selectedClips)
    {
        if (ref.clipId == draggedRef.clipId)
            continue;

        auto track = project.findTrackById (ref.trackId);

        if (! track.state.getParent().isValid())
            continue;

        // **IDで引き直す**（番号は増減でずれる。1.32）
        if (ref.isMidi)
            continue;   // 8.94：クリップはオーディオだけ（Phase 134）

        {
            for (int c = 0; c < track.getNumClips(); ++c)
            {
                auto other = track.getClip (c);

                if (other.getId() != ref.clipId)
                    continue;

                other.setStartTime (juce::jmax (0.0, other.getStartTime() + deltaTime), &undoManager);
                break;
            }
        }
    }
}
void TimelineComponent::drawClip (juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& sourceFilePath,
                                   double offsetSeconds, double lengthSeconds, double fadeInSeconds,
                                   double fadeOutSeconds, const juce::Array<double>& hitPoints, bool isSelected,
                                   float gainLinear, bool isReversed, juce::Colour trackColour,
                                   const WarpMap& clipMap)
{
    auto& thumbnail = waveformCache.getThumbnail (sourceFilePath, this);

    // 8.149：**この`lengthSeconds`はタイムラインの長さ**（Phase 187／8.48）。
    // 波形とヒットポイントは**ソースの秒**で並んでいるので、表を通してから使う。
    // フェードはタイムラインの秒なので、そのままでよい
    //
    // 8.150：**表は折れていることがあります**（Phase 188）。マーカーを動かした結果は
    // 「クリップの中で音が配り直された」形なので、**波形もそのとおりに描かないと、
    // 打点が拍に乗ったかどうかが見えません**（それがワープの目的です）
    const double sourceLengthSeconds = clipMap.points.size() >= 2
                                         ? clipMap.points.back().sourceSeconds
                                             - clipMap.points.front().sourceSeconds
                                         : lengthSeconds;

    // 8.61：**地と波形はトラックの色**（Phase 99／改善案⑫。設計書2.4）。
    //
    // 8.159：**選んだら地の色を変える**（Phase 197／本人の要望）。
    // Phase 196までは「トラックの色のまま濃くして、パープルの太い枠」でしたが、
    // **細いクリップだと枠だけになって中身が見えません**
    const auto fillColour = (isSelected && useSelectedClipFill) ? getSelectedClipColour()
                                                                : trackColour;

    g.setColour (fillColour.withAlpha (isSelected ? selectedClipFillAlpha : clipFillAlpha));
    g.fillRect (bounds);

    g.setColour (trackColour);
    // 8.40：**クリップゲインぶん縦に伸ばして描く**（Phase 80）。
    // オーディオエディタと同じ見え方にしておく（別々だと、上げたのに変わらなく見える）
    //
    // 8.46：**逆再生のクリップは波形も左右反転**（Phase 86）。
    // アレンジ画面は曲の時刻で並んでいるので、**鳴る順に見えるのが正しい**
    {
        juce::Graphics::ScopedSaveState saved (g);

        if (isReversed)
            g.addTransform (juce::AffineTransform::translation ((float) -bounds.getCentreX(), 0.0f)
                                .scaled (-1.0f, 1.0f)
                                .translated ((float) bounds.getCentreX(), 0.0f));

        // 8.150：**表の区間ごとに描く**（Phase 188／8.48）。
        // マーカーが無ければ区間は1つで、Phase 187までとまったく同じ1回の呼び出しです
        if (clipMap.points.size() >= 2)
        {
            for (size_t i = 0; i + 1 < clipMap.points.size(); ++i)
            {
                const auto& from = clipMap.points[i];
                const auto& to = clipMap.points[i + 1];

                const int x0 = bounds.getX() + (int) std::round (from.warpedSeconds * pixelsPerSecond);
                const int x1 = bounds.getX() + (int) std::round (to.warpedSeconds * pixelsPerSecond);

                if (x1 <= x0)
                    continue;

                thumbnail.drawChannels (g, bounds.withX (x0).withWidth (x1 - x0),
                                         from.sourceSeconds, to.sourceSeconds, gainLinear);
            }
        }
        else
        {
            thumbnail.drawChannels (g, bounds, offsetSeconds, offsetSeconds + sourceLengthSeconds,
                                     gainLinear);
        }
    }

    // 仕様書5.5.1：ヒットポイント（トランジェント）を細い縦線で表示する。
    //
    // 8.49：**選んでいるクリップにだけ出す**（Phase 88）。フェードの丸と同じ扱いです
    // （Phase 87で色を直して見えるようになったところ、**触っていないクリップにまで
    // 線が並んで「枠が付いた」ように見えた**ため）。
    //
    // 保存されている値はソースファイル先頭からの秒数なので、クリップのoffsetを引いて
    // クリップ内の相対位置に直してから描く。
    if (isSelected)
    {
        g.setColour (AppColours::textPrimary.withAlpha (0.75f));

        for (auto hitTime : hitPoints)
        {
            if (hitTime < offsetSeconds || hitTime > offsetSeconds + sourceLengthSeconds)
                continue; // トリムで隠れている範囲のヒットポイントは描かない

            // 8.149／8.150：ソースの秒 → クリップの中の秒（表を通す）→ ピクセル
            double clipTime = (clipMap.points.size() >= 2) ? clipMap.sourceToWarped (hitTime)
                                                           : hitTime - offsetSeconds;

            // 8.49：**逆再生では波形と同じように折り返すこと**（Phase 88）。
            // Phase 86で波形だけひっくり返したので、線が音の形から離れていました
            if (isReversed)
                clipTime = lengthSeconds - clipTime;

            const int x = bounds.getX() + (int) (clipTime * pixelsPerSecond);
            g.drawLine ((float) x, (float) bounds.getY(), (float) x, (float) bounds.getBottom(), 1.0f);
        }
    }

    // フェード区間を、黒い三角形の半透明オーバーレイで可視化する
    const int fadeInPixels = juce::jlimit (0, bounds.getWidth(), (int) (fadeInSeconds * pixelsPerSecond));
    const int fadeOutPixels = juce::jlimit (0, bounds.getWidth(), (int) (fadeOutSeconds * pixelsPerSecond));

    if (fadeInPixels > 0)
    {
        juce::Path fadePath;
        fadePath.startNewSubPath ((float) bounds.getX(), (float) bounds.getBottom());
        fadePath.lineTo ((float) bounds.getX(), (float) bounds.getY());
        fadePath.lineTo ((float) (bounds.getX() + fadeInPixels), (float) bounds.getY());
        fadePath.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadePath);
    }

    if (fadeOutPixels > 0)
    {
        juce::Path fadePath;
        fadePath.startNewSubPath ((float) bounds.getRight(), (float) bounds.getBottom());
        fadePath.lineTo ((float) bounds.getRight(), (float) bounds.getY());
        fadePath.lineTo ((float) (bounds.getRight() - fadeOutPixels), (float) bounds.getY());
        fadePath.closeSubPath();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (fadePath);
    }

    // 8.159：**枠は細いまま**（Phase 197）。地の色で選択が分かるようになったので、
    // 太い枠は要りません（**旗を`false`にしたときだけ**Phase 196までの太枠に戻ります）
    const bool useFrameForSelection = (isSelected && ! useSelectedClipFill);

    g.setColour (isSelected ? (useSelectedClipFill ? getSelectedClipColour()
                                                    : AppColours::purple)
                            : trackColour.darker (0.4f));
    g.drawRect (bounds, useFrameForSelection ? 3 : (isSelected ? 2 : 1));

    // フェードハンドル（丸）。選択中のクリップだけに表示し、UIが煩雑になりすぎないようにする
    if (isSelected)
    {
        auto fadeInHandle = getFadeHandlePosition (bounds, fadeInSeconds, fadeOutSeconds, true);
        auto fadeOutHandle = getFadeHandlePosition (bounds, fadeInSeconds, fadeOutSeconds, false);

        g.setColour (AppColours::orange);
        g.fillEllipse ((float) fadeInHandle.x - fadeHandleRadius, (float) fadeInHandle.y - fadeHandleRadius,
                        fadeHandleRadius * 2.0f, fadeHandleRadius * 2.0f);
        g.fillEllipse ((float) fadeOutHandle.x - fadeHandleRadius, (float) fadeOutHandle.y - fadeHandleRadius,
                        fadeHandleRadius * 2.0f, fadeHandleRadius * 2.0f);
    }
}

//==============================================================================
// 8.91：MIDIは「ノートの塊」で描く（Phase 131）
//
// **四角はデータではありません。** そのつどノートから計算した見え方です。
// だから**重なりようがなく、伸縮という操作が存在せず、空白が埋まれば勝手に1つ**になります。
//==============================================================================

std::vector<Track::NoteBlock> TimelineComponent::getNoteBlocksFor (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return {};

    // 塊の切れ目は**1小節ぶんの空き**。**決めるのはProjectModelの1箇所**（8.98／Phase 138）——
    // 別々に持つと、同じ曲がアレンジとピアノロールで違う塊に見える（8.2）
    return project.getTrack (trackIndex).getNoteBlocks (project.getNoteBlockGapSeconds());
}

juce::Rectangle<int> TimelineComponent::getNoteBlockBounds (int trackIndex,
                                                             const Track::NoteBlock& block) const
{
    const int rowY = getTrackRowY (trackIndex);
    const int blockX = timeToX (block.startTime);
    const int blockWidth = juce::jmax (4, (int) ((block.endTime - block.startTime) * pixelsPerSecond));

    return { blockX, rowY + 4, blockWidth, getTrackAreaHeight (trackIndex) - 8 };
}

//==============================================================================
// 8.93：時間範囲の選択と、範囲の移動・複製・削除（Phase 133）
//
// **クリップのドラッグの代わりです。** クリップという掴めるものが無くなったので
// （8.91）、「ここからここまでを4小節後ろへ」をやる手段が要ります。
// Abilityと同じく、**トラックの上をなぞって範囲を決め、その中身を動かす**形です。
//
// **塊のドラッグ（8.92）とは役割が違います**：塊は「まとまりを1つ動かす」、
// 範囲は「区切りを自分で決めて動かす」。**範囲は重なりを止めません**——
// 窓を自分で選んでいるので、既にある音の上へ重ねるのも意図のうちです
// （音が2つ同時に鳴るのは、ノートの世界では普通のこと）。
//==============================================================================

//==============================================================================
// 8.158：範囲のかかっているトラック（Phase 196／本人の要望）

/** 8.159：この行が「いま範囲の対象か」（Phase 197）。

    旗から選んだ区間（`timeRangeAllTracks`）なら**どの行も対象**、
    枠やCtrlで選んだのなら**選んだトラックだけ**。
    **コピー・カット・削除がこの1箇所を見ます**（それぞれで判断すると必ずずれる。8.2）。 */
bool TimelineComponent::isTrackInTimeRange (int trackIndex) const
{
    if (! hasTimeRange)
        return false;

    if (timeRangeAllTracks)
        return juce::isPositiveAndBelow (trackIndex, project.getNumTracks());

    return isTimeRangeOnTrackIndex (trackIndex);
}

bool TimelineComponent::isTimeRangeOnTrackIndex (int trackIndex) const
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return false;

    const auto id = project.getTrack (trackIndex).getId();

    return std::find (timeRangeTrackIds.begin(), timeRangeTrackIds.end(), id)
             != timeRangeTrackIds.end();
}

std::vector<int> TimelineComponent::getTimeRangeTrackIndices() const
{
    std::vector<int> indices;

    // **並び順で返す**（IDの並びではなく画面の並び）。まとめて動かすときに
    // 「上から順に」で読めるほうが、後から追いやすい
    for (int t = 0; t < project.getNumTracks(); ++t)
        if (isTimeRangeOnTrackIndex (t))
            indices.push_back (t);

    return indices;
}

int TimelineComponent::getPrimaryTimeRangeTrackIndex() const
{
    const auto indices = getTimeRangeTrackIndices();
    return indices.empty() ? -1 : indices.front();
}

void TimelineComponent::setTimeRangeToTrack (int trackIndex)
{
    timeRangeTrackIds.clear();

    if (juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        timeRangeTrackIds.push_back (project.getTrack (trackIndex).getId());
}

void TimelineComponent::toggleTimeRangeTrack (int trackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    const auto id = project.getTrack (trackIndex).getId();
    auto found = std::find (timeRangeTrackIds.begin(), timeRangeTrackIds.end(), id);

    if (found == timeRangeTrackIds.end())
    {
        timeRangeTrackIds.push_back (id);
        return;
    }

    // **最後の1本は外さない。** 空の範囲は「選んでいない」と見分けが付かず、
    // 押しても何も起きないように見えます（トラックの複数選択と同じ決まり。8.126）
    if (timeRangeTrackIds.size() > 1)
        timeRangeTrackIds.erase (found);
}

void TimelineComponent::updateTimeRangeTracksForDrag (int currentRowIndex)
{
    const int anchor = timeRangeCreateAnchorTrackIndex;

    if (! juce::isPositiveAndBelow (anchor, project.getNumTracks()))
        return;

    // 8.162：**トラックの無い空きへ出ても、囲ったぶんは畳まない**（Phase 200／本人の報告）。
    //
    // 本人の言葉は「トラックのない空きスペースに触れると範囲が戻ってしまう」。
    //
    // Phase 196は、行が取れないとき（`-1`）に**起点へ戻して**いました。
    // 一覧の下の空きは**いちばん下のトラックのすぐ隣**なので、
    // 下まで囲おうとして少し行き過ぎるだけで、範囲が1行へ縮みます。
    //
    // **最後に通った行を覚えておいて、そこで止める。**
    // 「行の外へ出たら、直前の行のまま」——掴んでいる指の感覚と合います
    if (juce::isPositiveAndBelow (currentRowIndex, project.getNumTracks()))
        timeRangeCreateLastRowIndex = currentRowIndex;

    const int other = juce::isPositiveAndBelow (timeRangeCreateLastRowIndex, project.getNumTracks())
                          ? timeRangeCreateLastRowIndex : anchor;

    const int from = juce::jmin (anchor, other);
    const int to   = juce::jmax (anchor, other);

    timeRangeTrackIds.clear();

    // **MIDIトラックだけ入れます。** 範囲が動かすのはノートなので、
    // 途中にオーディオやフォルダの行があっても飛ばします
    // ——「囲ったのに何も起きない行」を選択に入れないため
    for (int t = from; t <= to; ++t)
        if (project.getTrack (t).getType() == TrackType::Midi)
            timeRangeTrackIds.push_back (project.getTrack (t).getId());

    // 起点はMIDIのはずですが、念のため空にはしない
    if (timeRangeTrackIds.empty())
        setTimeRangeToTrack (anchor);
}

void TimelineComponent::clearTimeRange()
{
    if (! hasTimeRange)
        return;

    hasTimeRange = false;
    timeRangeTrackIds.clear();
    timeRangeAllTracks = false;   // 8.124（Phase 159／改善案5）
    repaint();
}

void TimelineComponent::selectRangeFromMarker (int markerIndex)
{
    if (! juce::isPositiveAndBelow (markerIndex, project.getNumMarkers()))
        return;

    const double start = project.getMarker (markerIndex).getTime();

    // 8.124：**次の旗は「時刻で次」**（Phase 159／改善案5）。
    //
    // **番号順で隣を見ないこと。** 旗はドラッグで動かせるので、
    // 番号の並びと時刻の並びは一致しません（後ろへ動かした旗が「次」になる）。
    double end = -1.0;

    for (int i = 0; i < project.getNumMarkers(); ++i)
    {
        const double time = project.getMarker (i).getTime();

        if (time > start + 1.0e-6 && (end < 0.0 || time < end))
            end = time;
    }

    // 次の旗が無ければ曲の終わりまで（本人の指定）。
    // **中身が何も無いときのために、最低でも1小節ぶんは取る**——
    // 幅0の範囲は掴めず、選んだことも見えない
    if (end < 0.0)
    {
        // **最低でも1小節ぶんは取ること。** 曲の終わりより後に旗を置いていると
        // 幅0の範囲になり、掴めないうえ選んだことも見えません
        const double oneBarLater = project.getBarStartTime (project.getBarIndexAt (start) + 1);

        end = juce::jmax (oneBarLater, getContentLengthSeconds());
    }

    clearClipSelection();

    hasTimeRange = true;
    timeRangeAllTracks = true;
    timeRangeTrackIds.clear();   // 8.158：旗の範囲は全トラック（Phase 196）
    timeRangeStart = start;
    timeRangeEnd = end;
}

bool TimelineComponent::isTimeRangeAt (int trackIndex, double timeSeconds) const

{
    if (! hasTimeRange || timeSeconds < timeRangeStart || timeSeconds >= timeRangeEnd)
        return false;

    // 8.124：全トラックの範囲は**どの行でも中**（Phase 159／改善案5）
    if (timeRangeAllTracks)
        return juce::isPositiveAndBelow (trackIndex, project.getNumTracks());

    return isTimeRangeOnTrackIndex (trackIndex);   // 8.158（Phase 196）
}

/** 8.158：**行ごとに返す**（Phase 196）。範囲は複数のトラックにかかり得ます */
juce::Rectangle<int> TimelineComponent::getTimeRangeBoundsFor (int trackIndex) const
{
    if (! hasTimeRange || ! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return {};

    const int rowY = getTrackRowY (trackIndex);
    const int startX = timeToX (timeRangeStart);
    const int endX = timeToX (timeRangeEnd);

    return { startX, rowY + 4, juce::jmax (2, endX - startX), getTrackAreaHeight (trackIndex) - 8 };
}
bool TimelineComponent::handleTimeRangeMouseDown (const juce::MouseEvent& e, bool allowCreate)
{
    if (editTool != EditTool::arrow || ! getTimelineArea().contains (e.getPosition()))
        return false;

    const int trackIndex = getTrackIndexForY (e.y);

    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return false;

    // **オートメーションの行はここでは扱いません**（行の中の点はレーンの仕事。8.57）
    if (findAutomationRowAtY (e.y).isValid())
        return false;

    const double time = xToTime (e.x);

    // 8.124：**全トラックの範囲は、どの種類のトラックの上でも掴める**
    // （Phase 159／改善案5）。下のMIDI限定の判定より先に見ること——
    // オーディオトラックの行で掴めないと、**見えている枠の半分が掴めない**
    if (timeRangeAllTracks && isTimeRangeAt (trackIndex, time))
    {
        if (e.mods.isPopupMenu())
        {
            showTimeRangeMenu (e.getScreenPosition());
            return true;
        }

        startTimeRangeDrag (e);
        return true;
    }

    // ここから下は**1トラックの範囲**（Phase 133〜136）。MIDIトラックだけが対象
    if (project.getTrack (trackIndex).getType() != TrackType::Midi)
        return false;

    // 既にある範囲の上：右クリック＝メニュー、左＝掴んで動かす（Ctrlで複製）
    if (isTimeRangeAt (trackIndex, time))
    {
        if (e.mods.isPopupMenu())
        {
            showTimeRangeMenu (e.getScreenPosition());
            return true;
        }

        startTimeRangeDrag (e);
        return true;
    }

    // 8.94：**塊を押したら、その塊ぶんの範囲を選ぶ**（Phase 134）。
    //
    // Phase 133までは塊とドラッグの仕組みが別立てで、そのせいで
    // **塊の上ではCtrl＋ドラッグの複製が効かず、押しても枠も出ません**でした。
    // 「掴めるもの」を範囲ひとつに寄せて、塊は**その範囲の決まり方の1つ**にしています。
    const int blockIndex = findNoteBlockAt (trackIndex, time);

    if (blockIndex >= 0)
    {
        const auto blocks = getNoteBlocksFor (trackIndex);
        const auto& block = blocks[(size_t) blockIndex];

        clearClipSelection();

        selectedTrackIndex = trackIndex;
        selectedClipIndex = -1;   // 塊にIDが無いので「トラックを選んだ」扱い（8.92）
        selectedIsMidi = true;
        publishSelection();

        // 8.158：**Ctrl＋クリックは「この塊も足す」**（Phase 196／本人の要望）。
        //
        // 掴んで動かすほうへは進みません——Ctrlは足す合図なので、
        // そのままドラッグが始まると押し間違えが取り返しにくい
        // （トラックヘッダーのCtrl＋クリックと同じ決まり。8.126）。
        //
        // **既に範囲の中にある塊をCtrlで掴んだときは、ここへ来ません**
        // （上の`isTimeRangeAt()`が先に受けて、Ctrl＋ドラッグの複製になります）。
        if (e.mods.isCommandDown() && ! e.mods.isPopupMenu())
        {
            if (hasTimeRange && ! timeRangeAllTracks)
            {
                toggleTimeRangeTrack (trackIndex);

                // **時間のほうは広げる。** 塊ごとに始まりと終わりが違うので、
                // 足したぶんが入る幅まで伸ばします
                //
                // > 段違いの塊を足すと、あいだの静かなところも範囲に入ります。
                // > 範囲は「1つの窓」で、行ごとに別の窓は持っていません
                timeRangeStart = juce::jmin (timeRangeStart, block.startTime);
                timeRangeEnd   = juce::jmax (timeRangeEnd,   block.endTime);
            }
            else
            {
                hasTimeRange = true;
                timeRangeAllTracks = false;
                setTimeRangeToTrack (trackIndex);
                timeRangeStart = block.startTime;
                timeRangeEnd = block.endTime;
            }

            repaint();
            return true;
        }

        hasTimeRange = true;
        setTimeRangeToTrack (trackIndex);   // 8.158（Phase 196）
        timeRangeStart = block.startTime;
        timeRangeEnd = block.endTime;

        if (e.mods.isPopupMenu())
        {
            repaint();
            showTimeRangeMenu (e.getScreenPosition());
            return true;
        }

        startTimeRangeDrag (e);
        return true;
    }

    if (! allowCreate || e.mods.isPopupMenu())
        return false;   // 範囲の外：作れるのは空いている場所だけ（右クリックはツールのメニュー）

    // 8.96：**MIDIトラックの空きを押したときも、そのトラックを選ぶ**（Phase 136）。
    // 非MIDIのトラックは`mouseDown`の空き処理が同じことをしています
    clearClipSelection();

    selectedTrackIndex = trackIndex;
    selectedClipIndex = -1;
    selectedIsMidi = true;
    publishSelection();

    // 新しく引き始める。**掴んだ時点ではクリックと区別できない**ので、
    // 構えだけ取って`mouseUp`で振り分けます（8.10と同じ形）
    clearTimeRange();

    creatingTimeRange = true;
    setTimeRangeToTrack (trackIndex);   // 8.158（Phase 196）。囲うあいだに増えていきます
    timeRangeCreateAnchorTrackIndex = trackIndex;
    timeRangeCreateLastRowIndex = trackIndex;   // 8.162（Phase 200）。空きへ出たときの戻り先
    timeRangeAnchorTime = time;
    timeRangeStart = time;
    timeRangeEnd = time;

    return true;
}

void TimelineComponent::startTimeRangeDrag (const juce::MouseEvent& e)
{
    draggingTimeRange = true;
    timeRangeDragIsCopy = e.mods.isCommandDown();   // Ctrl＋ドラッグは複製（8.29の表と同じ）
    timeRangeDragOriginalStart = timeRangeStart;
    timeRangeDragPreviewStart = timeRangeStart;
    dragStartMousePosition = e.getPosition();
    repaint();
}

int TimelineComponent::findNoteBlockAt (int trackIndex, double timeSeconds) const
{
    const auto blocks = getNoteBlocksFor (trackIndex);

    for (size_t b = 0; b < blocks.size(); ++b)
        if (timeSeconds >= blocks[b].startTime && timeSeconds < blocks[b].endTime)
            return (int) b;

    return -1;
}

void TimelineComponent::dragTimeRange (juce::Point<int> mousePosition)
{
    if (creatingTimeRange)
    {
        // **寄せるのは両端**（8.14）。目分量で引くと、後で並べたときに合わない
        const double current = xToTime (mousePosition.x);

        timeRangeStart = project.snapTime (juce::jmin (timeRangeAnchorTime, current));
        timeRangeEnd = project.snapTime (juce::jmax (timeRangeAnchorTime, current));

        // 8.158：**縦に囲ったぶんのMIDIトラックを入れる**（Phase 196／本人の要望）。
        // 横は前から寄せてあります（8.14）ので、これで「拍で囲って複数トラック」になります
        updateTimeRangeTracksForDrag (getTrackIndexForY (mousePosition.y));

        hasTimeRange = (timeRangeEnd > timeRangeStart);
        repaint();
        return;
    }

    if (draggingTimeRange)
    {
        const double delta = xToTime (mousePosition.x) - xToTime (dragStartMousePosition.x);

        // 8.95：**もう止めません**（Phase 135）。
        //
        // Phase 132〜134は、塊を掴んだときだけ隣にぶつかると止めていました
        // （重ねると1つに化けるため）。ただ**複製のときは止めない**ので、
        // 「Ctrlを押すと動かせる範囲が変わる」という読みにくい挙動になっていました。
        // **止めないほうが分かりやすい**——重なって1つになるのは、
        // ノートの世界では「隣り合った」というだけのことです。
        timeRangeDragPreviewStart = juce::jmax (0.0, project.snapTime (timeRangeDragOriginalStart + delta));

        // 8.124：**全トラックの範囲は縦に動かさない**（Phase 159／改善案5。本人の指定）。
        // 「どの行がどの行へ行ったのか」を全トラックぶん追うことになるため
        if (timeRangeAllTracks)
        {
            repaint();
            return;
        }

        // 8.95：**縦に動かすと、別のMIDIトラックへ移る**（Phase 135）。
        // 行き先がMIDIトラックでなければ、元のトラックのままにする
        // 8.158：**縦に動かせるのは1本のときだけ**（Phase 196）。
        // 複数選んでいるときに縦へ動かすと、「どの行がどの行へ行ったのか」を
        // 全部ぶん追うことになります（旗の範囲を縦に動かさないのと同じ理由。8.124）
        timeRangeDragTargetTrack = getPrimaryTimeRangeTrackIndex();

        if (timeRangeTrackIds.size() > 1)
        {
            repaint();
            return;
        }

        const int overTrack = getTrackIndexForY (mousePosition.y);

        if (juce::isPositiveAndBelow (overTrack, project.getNumTracks())
             && project.getTrack (overTrack).getType() == TrackType::Midi
             && ! findAutomationRowAtY (mousePosition.y).isValid())
            timeRangeDragTargetTrack = overTrack;

        repaint();
    }
}

void TimelineComponent::finishTimeRangeDrag (const juce::MouseEvent& e)
{
    if (creatingTimeRange)
    {
        creatingTimeRange = false;

        // **動かさずに離したなら、範囲ではなくカーソル移動**（8.29の表と同じ振り分け）
        if (! e.mouseWasDraggedSinceMouseDown() || timeRangeEnd <= timeRangeStart)
        {
            clearTimeRange();
            seekToX (e.x);
        }

        repaint();
        return;
    }

    if (! draggingTimeRange)
        return;

    draggingTimeRange = false;

    const double delta = timeRangeDragPreviewStart - timeRangeDragOriginalStart;
    const bool isCopy = timeRangeDragIsCopy;
    const int targetTrack = timeRangeDragTargetTrack;

    timeRangeDragIsCopy = false;
    timeRangeDragTargetTrack = -1;

    const bool movedTracks = juce::isPositiveAndBelow (targetTrack, project.getNumTracks())
                              && timeRangeTrackIds.size() == 1
                              && targetTrack != getPrimaryTimeRangeTrackIndex();

    // 8.124：全トラックの範囲（Phase 159／改善案5）。**縦には動かないので横だけ見る**
    if (timeRangeAllTracks)
    {
        if (! juce::approximatelyEqual (delta, 0.0))
        {
            moveOrCopyRangeAllTracks (timeRangeStart, timeRangeEnd, delta, isCopy);

            const double length = timeRangeEnd - timeRangeStart;

            timeRangeStart = juce::jmax (0.0, timeRangeStart + delta);
            timeRangeEnd = timeRangeStart + length;
        }

        repaint();
        return;
    }

    if (! juce::approximatelyEqual (delta, 0.0) || movedTracks)
    {
        // 8.158：**選んでいるトラック全部を、同じだけ動かす**（Phase 196）
        for (const int t : getTimeRangeTrackIndices())
            moveOrCopyNotesInRange (t, timeRangeStart, timeRangeEnd, delta, isCopy,
                                     movedTracks ? targetTrack : -1);

        // **範囲も一緒に動かす**（複製のときは複製先へ、トラックをまたいだなら行き先へ）。
        // 置いていくと、続けて動かそうとしたときに**さっき動かしたものが入っていない**
        const double length = timeRangeEnd - timeRangeStart;

        timeRangeStart = juce::jmax (0.0, timeRangeStart + delta);
        timeRangeEnd = timeRangeStart + length;

        if (movedTracks)
            setTimeRangeToTrack (targetTrack);
    }

    repaint();
}

void TimelineComponent::moveOrCopyNotesInRange (int trackIndex, double fromSeconds, double toSeconds,
                                                 double deltaSeconds, bool copy, int targetTrackIndex)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    // 8.95：**行き先のトラック**（Phase 135）。-1なら同じトラック
    if (! juce::isPositiveAndBelow (targetTrackIndex, project.getNumTracks()))
        targetTrackIndex = trackIndex;

    auto track = project.getTrack (trackIndex);
    auto target = project.getTrack (targetTrackIndex);
    const bool crossesTracks = (targetTrackIndex != trackIndex);

    project.beginAction (copy ? utf8 ("範囲の複製") : utf8 ("範囲の移動"));

    applyRangeMoveToMidiTrack (track, target, fromSeconds, toSeconds, deltaSeconds, copy, crossesTracks);

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

//==============================================================================
// 8.124：全トラックにまたがる範囲（Phase 159／改善案5）

void TimelineComponent::splitClipsAtRangeEdges (Track& track, double fromSeconds, double toSeconds)
{
    auto& undoManager = project.getUndoManager();

    // **境目ごとに集め直すこと。** 1回割るとクリップが1つ増えるので、
    // 先に集めた番号のままでは2つ目の境目でずれます（1.32）。
    //
    // **後ろから回すこと。** 割ってできた新しいクリップは後ろに入るので、
    // 前から回すと**割ったばかりのものをまた見に行きます**
    for (const double edge : { fromSeconds, toSeconds })
    {
        for (int c = track.getNumClips(); --c >= 0;)
        {
            auto clip = track.getClip (c);
            const double start = clip.getStartTime();
            const double end = start + clip.getLength();

            // 端ちょうどのものは割らない（割っても長さ0のかけらができるだけ）
            if (edge > start + 1.0e-6 && edge < end - 1.0e-6)
                track.splitClipAt (clip.state, edge, &undoManager);
        }
    }
}

void TimelineComponent::applyRangeMoveToAudioTrack (Track& track, double fromSeconds, double toSeconds,
                                                     double deltaSeconds, bool copy)
{
    auto& undoManager = project.getUndoManager();

    // **先に割る**（本人の指定）。曲全体に1本のステムがあっても、
    // 区間の中側だけが運ばれます
    splitClipsAtRangeEdges (track, fromSeconds, toSeconds);

    // **集めてから書くこと**（MIDIと同じ理由。複製すると子が増える）。
    // 割った後なので、始まりが区間の中にあるクリップは**終わりも区間の中**です
    std::vector<juce::ValueTree> clips;

    for (int c = 0; c < track.getNumClips(); ++c)
    {
        auto clip = track.getClip (c);

        if (clip.getStartTime() >= fromSeconds - 1.0e-6 && clip.getStartTime() < toSeconds - 1.0e-6)
            clips.push_back (clip.state);
    }

    for (const auto& state : clips)
    {
        AudioClip clip { juce::ValueTree (state) };
        const double newStart = juce::jmax (0.0, clip.getStartTime() + deltaSeconds);

        // **複製はIDを振り直す`addClipCopy()`で**。同じIDが2つあると、
        // 片方を消したつもりが両方に効きます（1.32）
        if (copy)
            track.addClipCopy (state, newStart, &undoManager);
        else
            clip.setStartTime (newStart, &undoManager);
    }
}

juce::String TimelineComponent::makeCopiedMarkerName (const juce::String& original)
{
    // 8.127：**複製した印は`'`**（Phase 163／本人の指定）。
    // 何度複製しても増えていくので、何代目かも読めます（`Chorus''`）
    return original + "'";
}

void TimelineComponent::moveOrCopyMarkersInRange (double fromSeconds, double toSeconds,
                                                   double deltaSeconds, bool copy)
{
    auto& undoManager = project.getUndoManager();

    // **先に集めてから書くこと。** 複製はマーカーを増やすので、
    // 走りながら足すと**足したものをまた複製し続けます**（ノートと同じ話）
    std::vector<juce::ValueTree> markers;

    for (int m = 0; m < project.getNumMarkers(); ++m)
    {
        auto marker = project.getMarker (m);

        if (marker.getTime() >= fromSeconds - 1.0e-6 && marker.getTime() < toSeconds - 1.0e-6)
            markers.push_back (marker.state);
    }

    for (const auto& state : markers)
    {
        Marker marker { juce::ValueTree (state) };
        const double newTime = juce::jmax (0.0, marker.getTime() + deltaSeconds);

        if (copy)
            project.addMarker (newTime, makeCopiedMarkerName (marker.getName()), &undoManager);
        else
            marker.setTime (newTime, &undoManager);
    }
}

void TimelineComponent::applyRangeMoveToChordTrack (Track& track, double fromSeconds, double toSeconds,
                                                     double deltaSeconds, bool copy)
{
    auto& undoManager = project.getUndoManager();

    // **先に集めてから書くこと**（複製すると子が増える。ノート・マーカーと同じ話）
    std::vector<juce::ValueTree> regions;

    for (int r = 0; r < track.getNumChordRegions(); ++r)
    {
        auto region = track.getChordRegion (r);

        if (region.getStartTime() >= fromSeconds - 1.0e-6 && region.getStartTime() < toSeconds - 1.0e-6)
            regions.push_back (region.state);
    }

    for (const auto& state : regions)
    {
        ChordRegion region { juce::ValueTree (state) };
        const double newStart = juce::jmax (0.0, region.getStartTime() + deltaSeconds);

        if (copy)
            track.addChordRegion (region.getChord(), newStart, region.getLength(), &undoManager);
        else
            track.setChordRegionTime (region, newStart, region.getLength(), &undoManager);
    }

    // **最後に並べ直す。** 旗が増えた／動いたので、前後の長さがずれています
    track.normaliseChordRegions (&undoManager);
}

void TimelineComponent::moveOrCopyRangeAllTracks (double fromSeconds, double toSeconds,


                                                   double deltaSeconds, bool copy)
{
    // **区切りは1つだけ。** ここで作らずに各トラックで作ると、
    // 「区間を1つ動かした」のにCtrl+Zがトラックの数だけ要ることになります（3.1）
    project.beginAction (copy ? utf8 ("区間の複製") : utf8 ("区間の移動"));

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        switch (track.getType())
        {
            case TrackType::Midi:
                applyRangeMoveToMidiTrack (track, track, fromSeconds, toSeconds, deltaSeconds, copy, false);
                break;

            case TrackType::Audio:
                applyRangeMoveToAudioTrack (track, fromSeconds, toSeconds, deltaSeconds, copy);
                break;

            // 8.128：**コード区間も運ぶ**（Phase 164／本人の要望）。
            //
            // Phase 163までは意図して外していました（別扱いの持ちものなので）。
            // 旗にして「この旗から次まで」に揃えたので、**マーカーと同じ形**で運べます
            case TrackType::Chord:
                applyRangeMoveToChordTrack (track, fromSeconds, toSeconds, deltaSeconds, copy);
                break;

            // フォルダ・センド・VCA・パラアウトの受け皿には、区間で運ぶ中身がありません
            // （8.143／Phase 181：受け皿は音源から音が入ってくるだけの行）
            default:
                break;
        }
    }

    // 8.127：**マーカーも区間の一部**（Phase 163／本人の要望）。
    // 名前だけ元の場所に残ると、「Chorus」が何も無いところを指すことになります
    moveOrCopyMarkersInRange (fromSeconds, toSeconds, deltaSeconds, copy);

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::deleteRangeAllTracks (double fromSeconds, double toSeconds)
{
    project.beginAction (utf8 ("区間の削除"));

    auto& undoManager = project.getUndoManager();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        // 8.159：**選んだ行だけ**（Phase 197。`copyRangeAllTracks()`と同じ判断）
        if (! isTrackInTimeRange (t))
            continue;

        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi)
        {
            // **後ろから消すこと**（前から消すと以降の番号がずれる）
            for (int n = track.getNumNotes(); --n >= 0;)
            {
                auto note = track.getNote (n);

                if (note.getStartTime() >= fromSeconds - 1.0e-6 && note.getStartTime() < toSeconds - 1.0e-6)
                    track.removeNote (note, &undoManager);
            }

            for (int c = track.getNumCCEvents(); --c >= 0;)
            {
                auto event = track.getCCEvent (c);

                if (event.getTime() >= fromSeconds - 1.0e-6 && event.getTime() < toSeconds - 1.0e-6)
                    track.removeCCEvent (event, &undoManager);
            }
        }
        else if (track.getType() == TrackType::Audio)
        {
            // **消す前に割る**（移動と同じ扱い）。またいでいるクリップを丸ごと消すと、
            // 区間の外の音まで無くなります
            splitClipsAtRangeEdges (track, fromSeconds, toSeconds);

            for (int c = track.getNumClips(); --c >= 0;)
            {
                auto clip = track.getClip (c);

                if (clip.getStartTime() >= fromSeconds - 1.0e-6 && clip.getStartTime() < toSeconds - 1.0e-6)
                    track.removeClip (clip, &undoManager);
            }
        }
    }

    // 8.128：**コード区間も一緒に消す**（Phase 164／本人の要望）。
    // **後ろから消すこと**（前から消すと以降の番号がずれる）
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Chord)
            continue;

        for (int r = track.getNumChordRegions(); --r >= 0;)
        {
            auto region = track.getChordRegion (r);

            if (region.getStartTime() >= fromSeconds - 1.0e-6 && region.getStartTime() < toSeconds - 1.0e-6)
                track.removeChordRegion (region, &undoManager);
        }

        track.normaliseChordRegions (&undoManager);
    }

    // 8.127：**マーカーも一緒に消す**（Phase 163／本人の要望）。
    //
    // 区間の中身を消したのに名前だけ残ると、**何も無いところを指す旗**になります。
    // 移動・複製でマーカーを連れていくと決めたので、消すときも揃えました。
    // **後ろから消すこと**（前から消すと以降の番号がずれる）
    for (int m = project.getNumMarkers(); --m >= 0;)
    {
        auto marker = project.getMarker (m);

        if (marker.getTime() >= fromSeconds - 1.0e-6 && marker.getTime() < toSeconds - 1.0e-6)
            project.removeMarker (marker, &undoManager);
    }

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

// 8.124：1トラックぶん（Phase 159／改善案5）。**区切りはここでは作らない**——
// 全トラックぶんを1つのUndoにまとめる側が作る（3.1）
void TimelineComponent::applyRangeMoveToMidiTrack (Track& track, Track& target,
                                                    double fromSeconds, double toSeconds,
                                                    double deltaSeconds, bool copy, bool crossesTracks)
{
    auto& undoManager = project.getUndoManager();

    // **先に集めてから書くこと。** 複製は`addNote()`で子が増えるので、
    // 走りながら足すと**足したものをまた複製し続けます**（無限に増える）
    std::vector<juce::ValueTree> notes;
    std::vector<juce::ValueTree> ccEvents;

    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() >= fromSeconds - 1.0e-6 && note.getStartTime() < toSeconds - 1.0e-6)
            notes.push_back (note.state);
    }

    for (int c = 0; c < track.getNumCCEvents(); ++c)
    {
        auto event = track.getCCEvent (c);

        if (event.getTime() >= fromSeconds - 1.0e-6 && event.getTime() < toSeconds - 1.0e-6)
            ccEvents.push_back (event.state);
    }

    for (const auto& state : notes)
    {
        Note note { juce::ValueTree (state) };
        const double newStart = juce::jmax (0.0, note.getStartTime() + deltaSeconds);

        // 8.95：**トラックをまたぐときは、作って消す**（Phase 135）。
        // ValueTreeは親を1つしか持てないので、付け替えるなら外してから足すことになる
        // ——作り直すほうが、途中で例外が出ても片方だけ消える形になりません
        if (copy || crossesTracks)
            target.addNote (note.getPitch(), note.getVelocity(), newStart, note.getLength(), &undoManager);

        if (! copy && crossesTracks)
            track.removeNote (note, &undoManager);
        else if (! copy)
            note.setStartTime (newStart, &undoManager);
    }

    // 8.92と同じく、**CCも一緒に**（置いていくと表情だけ元の場所に残る）
    for (const auto& state : ccEvents)
    {
        CCEvent event { juce::ValueTree (state) };
        const double newTime = juce::jmax (0.0, event.getTime() + deltaSeconds);

        if (copy || crossesTracks)
            target.addCCEvent (event.getControllerNumber(), event.getValue(), newTime, &undoManager);

        if (! copy && crossesTracks)
            track.removeCCEvent (event, &undoManager);
        else if (! copy)
            event.setTime (newTime, &undoManager);
    }

    track.sortCCEvents (&undoManager);

    if (crossesTracks)
        target.sortCCEvents (&undoManager);
}

//==============================================================================
// 8.124：区間まるごとのコピー＆ペースト（Phase 160／改善案5）

bool TimelineComponent::copyRangeAllTracks (bool alsoDelete)
{
    juce::Array<EditClipboard::Item> items;

    // 8.139：**基準の拍**（Phase 177）。中身のずれを拍でも覚えておくので、
    // 貼り付け先のテンポが違っても音楽的な間隔が保たれます（`EditClipboard.h`）
    const double rangeStartBeats = project.getBeatPositionAt (timeRangeStart);

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        // 8.159：**選んだ行だけ**（Phase 197）。旗の区間なら全部、
        // 枠やCtrlで選んだのなら、選んだトラックだけが対象です
        if (! isTrackInTimeRange (t))
            continue;

        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi)
        {
            for (int n = 0; n < track.getNumNotes(); ++n)
            {
                auto note = track.getNote (n);

                if (note.getStartTime() < timeRangeStart - 1.0e-6
                     || note.getStartTime() >= timeRangeEnd - 1.0e-6)
                    continue;

                EditClipboard::Item item;
                item.state = note.state.createCopy();   // **複製を入れること**（元が動くと中身も変わる）
                item.rowOffset = t;                     // トラックの番号をそのまま
                item.timeOffset = note.getStartTime() - timeRangeStart;
                item.beatOffset = note.getStartBeats() - rangeStartBeats;   // 8.139
                items.add (item);
            }

            for (int c = 0; c < track.getNumCCEvents(); ++c)
            {
                auto event = track.getCCEvent (c);

                if (event.getTime() < timeRangeStart - 1.0e-6
                     || event.getTime() >= timeRangeEnd - 1.0e-6)
                    continue;

                EditClipboard::Item item;
                item.state = event.state.createCopy();
                item.rowOffset = t;
                item.timeOffset = event.getTime() - timeRangeStart;
                item.beatOffset = event.getTimeBeats() - rangeStartBeats;   // 8.139
                items.add (item);
            }
        }
        else if (track.getType() == TrackType::Audio)
        {
            for (int c = 0; c < track.getNumClips(); ++c)
            {
                auto clip = track.getClip (c);

                const double start = clip.getStartTime();
                const double end = start + clip.getLength();

                // 区間と重なっているぶんだけ
                const double visibleStart = juce::jmax (start, timeRangeStart);
                const double visibleEnd = juce::jmin (end, timeRangeEnd);

                if (visibleEnd <= visibleStart + 1.0e-6)
                    continue;

                // 8.124：**元は割りません**（Phase 160／改善案5）。
                //
                // 移動のときは割りますが、**コピーしただけでプロジェクトが変わる**のは
                // 予想外です。入れ物の中で窓（オフセットと長さ）を詰めた複製を持ちます——
                // 中身のファイルは同じものを指したままなので、これで足ります（1.14）
                EditClipboard::Item item;
                item.state = clip.state.createCopy();

                AudioClip copied { item.state };
                // 8.149：**`visibleStart - start`はタイムラインの秒**（Phase 187／8.48）。
                // オフセットはソースの秒なので、伸縮ぶんで割ってから足すこと
                copied.setOffset (clip.getOffset() + (visibleStart - start) / clip.getStretch(), nullptr);
                copied.setLength (visibleEnd - visibleStart, nullptr);

                item.rowOffset = t;
                item.timeOffset = visibleStart - timeRangeStart;   // **オーディオはこちらを使う**
                item.beatOffset = project.getBeatPositionAt (visibleStart) - rangeStartBeats;
                items.add (item);
            }
        }
    }

    // 8.136：**コード区間も入れる**（Phase 174／本人の報告）。
    //
    // **Phase 164で入れ忘れていました。** ドラッグでの移動・複製にはコード区間を
    // 足したのに（8.128）、**クリップボード側は Phase 160 のまま**だったので、
    // Ctrl+C → Ctrl+V では旗が付いてきませんでした。
    //
    // **同じ「区間を運ぶ」でも入口が2つある**という、まさに1.27の形です。
    // 片方に足したら、もう片方も見ること。
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Chord)
            continue;

        for (int r = 0; r < track.getNumChordRegions(); ++r)
        {
            auto region = track.getChordRegion (r);

            if (region.getStartTime() < timeRangeStart - 1.0e-6
                 || region.getStartTime() >= timeRangeEnd - 1.0e-6)
                continue;

            EditClipboard::Item item;
            item.state = region.state.createCopy();
            item.rowOffset = t;
            item.timeOffset = region.getStartTime() - timeRangeStart;
            item.beatOffset = region.getStartBeats() - rangeStartBeats;   // 8.139
            items.add (item);
        }
    }

    // 8.127：**マーカーも入れる**（Phase 163／本人の要望）。
    //
    // **`rowOffset`は-1**にしてあります。トラックの番号ではなく
    // 「どのトラックにも属さない」という印です——貼り付ける側が振り分けに使います
    for (int m = 0; m < project.getNumMarkers(); ++m)
    {
        auto marker = project.getMarker (m);

        if (marker.getTime() < timeRangeStart - 1.0e-6 || marker.getTime() >= timeRangeEnd - 1.0e-6)
            continue;

        EditClipboard::Item item;
        item.state = marker.state.createCopy();
        item.rowOffset = -1;
        item.timeOffset = marker.getTime() - timeRangeStart;
        item.beatOffset = marker.getTimeBeats() - rangeStartBeats;   // 8.139
        items.add (item);
    }

    if (items.isEmpty())
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("選んだ区間に中身がありません。"));

        return false;
    }

    EditClipboard::set (EditClipboard::Kind::trackRange, std::move (items));

    if (alsoDelete)
        deleteRangeAllTracks (timeRangeStart, timeRangeEnd);

    return true;
}

bool TimelineComponent::pasteRangeAllTracks (double timeSeconds)
{
    if (EditClipboard::getKind() != EditClipboard::Kind::trackRange)
        return false;

    auto& undoManager = project.getUndoManager();
    const double startTime = juce::jmax (0.0, project.snapTime (timeSeconds));

    project.beginAction (utf8 ("区間の貼り付け"));

    double lastEnd = startTime;
    bool pastedAnything = false;

    // 8.139：**貼り付け位置の拍**（Phase 177）。中身のずれは拍で持っています
    const double pasteStartBeats = project.getBeatPositionAt (startTime);

    for (const auto& item : EditClipboard::getItems())
    {
        // 8.127：マーカー（Phase 163／本人の要望）。**トラックには属さない**ので、
        // 番号での振り分けより先に見ます
        if (item.state.hasType (IDs::MARKER))
        {
            Marker marker { juce::ValueTree (item.state) };

            // 8.139：**位置は拍で**（Phase 177）
            project.addMarkerBeats (juce::jmax (0.0, pasteStartBeats + item.beatOffset),
                                     makeCopiedMarkerName (marker.getName()), &undoManager);
            pastedAnything = true;
            continue;
        }

        // 8.124：**同じ番号のトラックへ戻す**（Phase 160／改善案5）。
        // 上下を跨がない決まりなので、貼り付けも跨がせません
        if (! juce::isPositiveAndBelow (item.rowOffset, project.getNumTracks()))
            continue;   // トラックが減っていた。そのぶんは捨てる

        auto track = project.getTrack (item.rowOffset);

        // **オーディオは秒、それ以外は拍**（8.139）。オーディオは伸び縮みできないので、
        // 貼り付け先のテンポで間隔が変わってはいけません（`EditClipboard.h`）
        const double time = juce::jmax (0.0, startTime + item.timeOffset);
        const double beats = juce::jmax (0.0, pasteStartBeats + item.beatOffset);

        // **種別で振り分けること**（8.95）。見ずに`notePitch`を読むと、
        // CCから音程0のノートができます
        if (item.state.hasType (IDs::NOTE) && track.getType() == TrackType::Midi)
        {
            Note note { juce::ValueTree (item.state) };

            // 8.138：**長さは拍のまま運ぶこと**（Phase 176／8.105の宿題3）。
            //
            // クリップボードの中身は`createCopy()`した**切り離されたツリー**なので、
            // `getLength()`を訊いてもテンポの表へ辿り着けず、**既定の120BPMで答えます**
            // （`MusicalTime.h`）。拍ならそのまま持ち運べます。
            //
            // **音楽的にもこちらが正しい**：4分音符をコピーしたら、
            // テンポの違うところへ貼っても4分音符です
            auto added = track.addNoteBeats (note.getPitch(), note.getVelocity(),
                                              beats, note.getLengthBeats(), &undoManager);

            lastEnd = juce::jmax (lastEnd, added.getStartTime() + added.getLength());
            pastedAnything = true;
        }
        else if (item.state.hasType (IDs::CC) && track.getType() == TrackType::Midi)
        {
            CCEvent event { juce::ValueTree (item.state) };

            track.addCCEventBeats (event.getControllerNumber(), event.getValue(), beats, &undoManager);
            pastedAnything = true;
        }
        else if (item.state.hasType (IDs::CHORDREGION) && track.getType() == TrackType::Chord)
        {
            // 8.136：コード区間（Phase 174／本人の報告）。
            // **長さもそのまま渡す**——後で`normaliseChordRegions()`が
            // 「次の旗まで」に揃え直します（8.128）
            // 8.139：**位置も長さも拍で**（Phase 177）。切り離されたツリーなので、
            // `getLength()`（秒）を訊くと既定の120BPMで答えます（`MusicalTime.h`）
            ChordRegion region { juce::ValueTree (item.state) };

            auto added = track.addChordRegionBeats (region.getChord(), beats,
                                                     region.getLengthBeats(), &undoManager);

            lastEnd = juce::jmax (lastEnd, added.getStartTime() + added.getLength());
            pastedAnything = true;
        }
        else if (item.state.hasType (IDs::AUDIOCLIP) && track.getType() == TrackType::Audio)
        {
            AudioClip clip { juce::ValueTree (item.state) };

            // **IDは`addClipCopy()`が振り直します**（同じIDが2つあると、
            // 片方を消したつもりが両方に効く。1.32）
            track.addClipCopy (item.state, time, &undoManager);
            lastEnd = juce::jmax (lastEnd, time + clip.getLength());
            pastedAnything = true;
        }
        // 種類が食い違うとき（トラックを入れ替えた後など）は、そのぶんを捨てる。
        // **無理に近いトラックへ入れないこと**——どこへ入ったか分からない貼り付けは、
        // 無いほうがましです（8.95と同じ考え方）
    }

    if (! pastedAnything)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("貼り付け先のトラックが見つかりません。"));

        return false;
    }

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi)
            track.sortCCEvents (&undoManager);

        // 8.136：**貼り付けた旗の前後を並べ直す**（Phase 174）。
        // 呼ばないと、旗と旗のあいだに「コードの無い隙間」が残ります（8.128）
        else if (track.getType() == TrackType::Chord)
            track.normaliseChordRegions (&undoManager);
    }

    // **貼り付けたところを選んでおく**（続けて動かしたいことが多い）
    hasTimeRange = true;
    timeRangeAllTracks = true;
    timeRangeTrackIds.clear();   // 8.158（Phase 196）
    timeRangeStart = startTime;
    timeRangeEnd = juce::jmax (lastEnd, startTime + 0.01);

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

bool TimelineComponent::copyTimeRange (bool alsoDelete)
{
    // 8.124：全トラックの区間は専用の経路へ（Phase 160／改善案5）。
    //
    // 8.159：**複数トラックを選んでいるときも同じ経路**（Phase 197／本人の報告）。
    //
    // Phase 196は「コピーは代表の1本だけ」にしていました——`Kind::notes`が
    // **ノートの平たい列**で、行の区別を持てないためです。
    // ところが**`Kind::trackRange`は前から持っていました**（`rowOffset`にトラックの番号。
    // 8.124の旗の区間がそれで動いている）。**新しく作る必要はなく、そちらへ通すだけ**でした。
    if (hasTimeRange && (timeRangeAllTracks || timeRangeTrackIds.size() > 1))
        return copyRangeAllTracks (alsoDelete);

    const int primaryTrackIndex = getPrimaryTimeRangeTrackIndex();

    if (! hasTimeRange || ! juce::isPositiveAndBelow (primaryTrackIndex, project.getNumTracks()))
        return false;

    auto track = project.getTrack (primaryTrackIndex);

    // 8.95：**ノートとCCを1つの入れ物へ**（Phase 135）。
    // ドラッグでの複製はCCも運ぶので（8.92）、Ctrl+Cだけ運ばないのは食い違います。
    // **基準は範囲の頭**（貼り付け先の時刻に足せば、間隔がそのまま保たれる）
    juce::Array<EditClipboard::Item> items;

    // 8.139：**基準の拍**（Phase 177）。`copyRangeAllTracks()`と同じ理由
    const double rangeStartBeats = project.getBeatPositionAt (timeRangeStart);

    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() < timeRangeStart - 1.0e-6 || note.getStartTime() >= timeRangeEnd - 1.0e-6)
            continue;

        EditClipboard::Item item;
        item.state = note.state.createCopy();   // **複製を入れること**（元が動くと中身も変わる）
        item.timeOffset = note.getStartTime() - timeRangeStart;
        item.beatOffset = note.getStartBeats() - rangeStartBeats;   // 8.139
        items.add (item);
    }

    for (int c = 0; c < track.getNumCCEvents(); ++c)
    {
        auto event = track.getCCEvent (c);

        if (event.getTime() < timeRangeStart - 1.0e-6 || event.getTime() >= timeRangeEnd - 1.0e-6)
            continue;

        EditClipboard::Item item;
        item.state = event.state.createCopy();
        item.timeOffset = event.getTime() - timeRangeStart;
        item.beatOffset = event.getTimeBeats() - rangeStartBeats;   // 8.139
        items.add (item);
    }

    if (items.isEmpty())
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("選んだ範囲にノートがありません。"));

        return false;
    }

    EditClipboard::set (EditClipboard::Kind::notes, std::move (items));

    if (alsoDelete)
        deleteNotesInRange (primaryTrackIndex, timeRangeStart, timeRangeEnd);

    return true;
}

bool TimelineComponent::pasteNotesAt (double timeSeconds)
{
    if (EditClipboard::getKind() != EditClipboard::Kind::notes)
        return false;

    // 8.95：**貼り付け先は「いま選んでいるMIDIトラック」**（Phase 135）。
    //
    // **`SelectionState`から引くこと。** `selectedTrackIndex`はタイムラインの中だけの
    // 覚えで、Console・インスペクタ・ピアノロールの一覧で選んだぶんが入りません
    // （入口が複数ある値は、持ち主を1つに決める。8.12）。
    //
    // 見つからなければ、範囲を引いてあるトラックへ。どちらも無ければ何もしません
    // ——**どこへ入ったか分からない貼り付けは、無いほうがまし**です。
    int trackIndex = -1;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto candidate = project.getTrack (t);

        if (candidate.getType() == TrackType::Midi && candidate.getId() == selection.getTrackId())
        {
            trackIndex = t;
            break;
        }
    }

    if (const int primary = getPrimaryTimeRangeTrackIndex();
         trackIndex < 0 && juce::isPositiveAndBelow (primary, project.getNumTracks())
          && project.getTrack (primary).getType() == TrackType::Midi)
        trackIndex = primary;

    if (trackIndex < 0)
    {
        if (onStatusMessage != nullptr)
            onStatusMessage (utf8 ("貼り付け先のMIDIトラックを選んでください。"));

        return false;
    }

    auto track = project.getTrack (trackIndex);
    auto& undoManager = project.getUndoManager();

    const double startTime = juce::jmax (0.0, project.snapTime (timeSeconds));

    project.beginAction (utf8 ("貼り付け"));

    double lastEnd = startTime;

    // 8.139：**貼り付け位置の拍**（Phase 177）
    const double pasteStartBeats = project.getBeatPositionAt (startTime);

    for (const auto& item : EditClipboard::getItems())
    {
        const double beats = juce::jmax (0.0, pasteStartBeats + item.beatOffset);

        // **種別で振り分けること**（8.95）。見ずに`notePitch`を読むと、
        // CCから音程0のノートができます
        if (item.state.hasType (IDs::NOTE))
        {
            Note note { juce::ValueTree (item.state) };

            // 8.138：**長さは拍のまま運ぶこと**（Phase 176）。クリップボードの中身は
            // 切り離されたツリーなので、`getLength()`は既定の120BPMで答えます
            // （`MusicalTime.h`）。8.139：**位置も拍で**（Phase 177）
            auto added = track.addNoteBeats (note.getPitch(), note.getVelocity(),
                                              beats, note.getLengthBeats(), &undoManager);

            lastEnd = juce::jmax (lastEnd, added.getStartTime() + added.getLength());
        }
        else if (item.state.hasType (IDs::CC))
        {
            CCEvent event { juce::ValueTree (item.state) };

            track.addCCEventBeats (event.getControllerNumber(), event.getValue(), beats, &undoManager);
        }
    }

    track.sortCCEvents (&undoManager);

    // **貼り付けたところを選んでおく。** 続けて動かしたいことが多く、
    // 選ばれていないと「どこへ入ったのか」も分かりにくい
    hasTimeRange = true;
    setTimeRangeToTrack (trackIndex);   // 8.158（Phase 196）
    timeRangeStart = startTime;
    timeRangeEnd = juce::jmax (lastEnd, startTime + 0.01);

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

void TimelineComponent::showTimeRangeMenu (juce::Point<int> screenPosition)
{
    if (! hasTimeRange)
        return;

    const int trackIndex = getPrimaryTimeRangeTrackIndex();   // 8.158（Phase 196）
    const double from = timeRangeStart;
    const double to = timeRangeEnd;
    const double length = to - from;

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("範囲のノートを削除"));
    menu.addItem (2, utf8 ("すぐ後ろへ複製"));
    menu.addSeparator();
    menu.addItem (3, utf8 ("1半音上げる"));
    menu.addItem (4, utf8 ("1半音下げる"));
    menu.addItem (5, utf8 ("1オクターブ上げる"));
    menu.addItem (6, utf8 ("1オクターブ下げる"));
    menu.addSeparator();
    menu.addItem (7, utf8 ("範囲の選択を解除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, trackIndex, from, to, length] (int result)
        {
            if (result == 1)
            {
                deleteNotesInRange (trackIndex, from, to);
            }
            else if (result == 2)
            {
                // **「すぐ後ろ」＝範囲1つぶん先。** 4小節ぶんを選んで押せば4小節後ろへ増える
                moveOrCopyNotesInRange (trackIndex, from, to, length, true);

                // 複製したほうを選び直す（続けて押せば、そのまま繰り返し増やせる）
                timeRangeStart = from + length;
                timeRangeEnd = to + length;
                repaint();
            }
            else if (result >= 3 && result <= 6)
            {
                const int semitones = (result == 3) ? 1 : (result == 4) ? -1 : (result == 5) ? 12 : -12;
                transposeNotesInRange (trackIndex, from, to, semitones);
            }
            else if (result == 7)
            {
                clearTimeRange();
            }
        });
}

void TimelineComponent::deleteNotesInRange (int trackIndex, double fromSeconds, double toSeconds)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (trackIndex);
    auto& undoManager = project.getUndoManager();

    project.beginAction (utf8 ("ノートの削除"));

    // **後ろから消す**（前から消すと、消したぶんだけ後続の番号がずれる）
    for (int n = track.getNumNotes(); --n >= 0;)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() >= fromSeconds - 1.0e-6 && note.getStartTime() < toSeconds - 1.0e-6)
            track.removeNote (note, &undoManager);
    }

    // 8.96：**中身が無くなった範囲は外す**（Phase 136）。
    // 残すと**枠だけが宙に浮きます**——選んでいるつもりのものが、もう無い
    clearTimeRangeIfEmpty();

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void TimelineComponent::clearTimeRangeIfEmpty()
{
    // 8.124：**全トラックの範囲は空でも残す**（Phase 159／改善案5）。
    // マーカーで選んだ区間は「曲のこの部分」という枠で、
    // 中身が無くても意味があります（そこへ貼り付ける、など）
    if (timeRangeAllTracks)
        return;

    const int primaryTrackIndex = getPrimaryTimeRangeTrackIndex();

    if (! hasTimeRange || ! juce::isPositiveAndBelow (primaryTrackIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (primaryTrackIndex);

    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() >= timeRangeStart - 1.0e-6 && note.getStartTime() < timeRangeEnd - 1.0e-6)
            return;   // まだ中身がある
    }

    clearTimeRange();
}

void TimelineComponent::transposeNotesInRange (int trackIndex, double fromSeconds, double toSeconds,
                                                int semitones)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()) || semitones == 0)
        return;

    auto track = project.getTrack (trackIndex);

    // 8.77：**1つでもMIDIの範囲を外れるなら何もしない**（和音の形が崩れる）。
    // 外れるものだけ止めると、12半音上げて戻したときに元へ戻らない
    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() < fromSeconds - 1.0e-6 || note.getStartTime() >= toSeconds - 1.0e-6)
            continue;

        const int moved = note.getPitch() + semitones;

        if (moved < 0 || moved > 127)
        {
            if (onStatusMessage != nullptr)
                onStatusMessage (utf8 ("これ以上は動かせません（MIDIの音域の端です）。"));

            return;
        }
    }

    auto& undoManager = project.getUndoManager();
    project.beginAction (utf8 ("MIDIのトランスポーズ"));

    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() < fromSeconds - 1.0e-6 || note.getStartTime() >= toSeconds - 1.0e-6)
            continue;

        note.setPitch (note.getPitch() + semitones, &undoManager);
    }

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

/** 8.159：選んだときの地の色（Phase 197/本人の要望）。

    8.160：**オレンジからグレーへ**（Phase 198/本人の指定）。

    **トラックの色と喧嘩しない**のが選ぶ条件です。オレンジは見本にも入っている色なので、
    オレンジのトラックだけ「選んでいるのが分からない」状態でした。
    無彩色なら、どの見本の上に乗っても「色が抜けた」ように見えます。

    > 見本の1色目（グレー `ff808080`）とは**明るさを離してあります**。 */
juce::Colour TimelineComponent::getSelectedClipColour()
{
    return juce::Colour (0xffd0d0d0);
}

void TimelineComponent::drawNoteBlock (juce::Graphics& g, juce::Rectangle<int> bounds,
                                        const Track& track, const Track::NoteBlock& block,
                                        juce::Colour trackColour, juce::Rectangle<int> selectedPart)
{
    // 8.61：**地とノートはトラックの色**（Phase 99／改善案⑫）。
    // オーディオクリップより少しだけ濃くして、種別も見分けられるようにしてある
    g.setColour (trackColour.withAlpha (clipFillAlpha + 0.04f));
    g.fillRect (bounds);

    // 8.160：**選んだところ「だけ」地の色を変える**（Phase 198／本人の要望）。
    //
    // Phase 197は**塊まるごと**を塗っていました。範囲は拍（スナップ）で切れるので、
    // **塊の途中まで選んでも、塊が丸ごと選ばれたように見えて**いました。
    // 範囲と重なっているぶんだけ塗れば、**どこからどこまでを選んだのかが見えます**。
    //
    // **重なりの計算は呼ぶ側**（`paint()`）です——範囲の持ち主はあちらなので、
    // ここへ持ち込むと、塊を描くたびに範囲を訊きに行くことになります
    if (useSelectedClipFill && ! selectedPart.isEmpty())
    {
        g.setColour (getSelectedClipColour().withAlpha (selectedClipFillAlpha));
        g.fillRect (selectedPart.getIntersection (bounds));
    }

    // 音域は「その塊に入っているノートの範囲」に合わせる。
    // 固定の音域（0〜127）にすると、数音しか無い塊では線が潰れて見えなくなる。
    int lowestPitch = 127;
    int highestPitch = 0;
    bool haveAny = false;

    for (int n = 0; n < track.getNumNotes(); ++n)
    {
        auto note = track.getNote (n);

        if (note.getStartTime() >= block.endTime || note.getStartTime() + note.getLength() <= block.startTime)
            continue;

        lowestPitch = juce::jmin (lowestPitch, note.getPitch());
        highestPitch = juce::jmax (highestPitch, note.getPitch());
        haveAny = true;
    }

    if (haveAny)
    {
        // 1音だけ・狭い音域のときに帯が極端に太くならないよう、最低1オクターブぶんは確保する
        const int minimumSpan = 12;
        int span = highestPitch - lowestPitch + 1;

        if (span < minimumSpan)
        {
            const int padding = (minimumSpan - span + 1) / 2;
            lowestPitch = juce::jmax (0, lowestPitch - padding);
            span = minimumSpan;
        }

        g.saveState();
        g.reduceClipRegion (bounds);

        const auto noteArea = bounds.reduced (0, 3);

        // 8.162：**行の間隔に下限を持たせない**（Phase 200／本人の報告）。
        //
        // 本人の言葉は「MIDI塊上に移るMIDIノートの写しが見切れている場合がある」。
        //
        // Phase 99からここは`jmax (1.5f, 高さ ÷ 音域)`でした。
        // 音域が広く、行が低いとき——たとえば高さ30pxに40音——は
        // 下限の1.5pxが勝ち、**積み上げた合計（40×1.5＝60px）が塊からはみ出します。**
        // 積むのは下端からなので、**はみ出すのはいちばん高い音**——
        // メロディの頭が消える、といういちばん困る消え方でした。
        //
        // **位置は割り切った値で決め、下限は「描く太さ」だけに持たせる。**
        // こうすると、音域がいくら広くても必ず塊に収まります
        // （細かすぎて重なることはありますが、**消えるよりは見えるほうがよい**）
        const float rowHeight = (float) noteArea.getHeight() / (float) span;
        const float barHeight = juce::jmax (1.0f, rowHeight - 1.0f);

        // 8.61：ノートもトラックの色（Phase 99／改善案⑫）。地より濃くして読めるようにする
        g.setColour (trackColour.brighter (0.25f));

        for (int n = 0; n < track.getNumNotes(); ++n)
        {
            auto note = track.getNote (n);

            if (note.getStartTime() >= block.endTime
                 || note.getStartTime() + note.getLength() <= block.startTime)
                continue;

            // 8.91：**ノートの時刻はそのまま曲の時刻**（Phase 131）。
            // オフセットを引く必要が無くなった
            const float x = (float) timeToX (note.getStartTime());
            const float width = juce::jmax (2.0f, (float) (note.getLength() * pixelsPerSecond));

            // 高い音ほど上に来るよう、下端から積み上げる
            const float y = (float) noteArea.getBottom()
                              - (float) (note.getPitch() - lowestPitch + 1) * rowHeight;

            g.fillRect (x, y, width, barHeight);
        }

        g.restoreState();
    }

    g.setColour (trackColour.darker (0.4f));
    g.drawRect (bounds, 1);
}

namespace
{
    /** 秒を「分:秒」に整える。細かい目盛りのときだけ小数第1位まで出す。 */
    juce::String formatTimecode (double seconds, bool withTenths)
    {
        const int totalSeconds = (int) seconds;
        juce::String text;

        text << (totalSeconds / 60) << ":"
             << juce::String (totalSeconds % 60).paddedLeft ('0', 2);

        if (withTenths)
            text << "." << (int) ((seconds - (double) totalSeconds) * 10.0);

        return text;
    }
}

void TimelineComponent::drawTimelineGrid (juce::Graphics& g)
{
    // 仕様書5.9：小節頭の縦ライン（Phase 62／8.1のC7）。
    //
    // **クリップより先に描く**（地の一部なので、クリップに隠れてよい）。
    // ルーラーの目盛りと同じ計算で、**同じ位置に線が来る**ようにしてある。
    // ここを独自に計算すると、拡大縮小したときにルーラーと1pxずれる。
    // **小節の長さは「画面の左端の小節」で測る**（8.98／Phase 138）。
    // 間引きの判断に使う目安なので、変化点をまたいでも1本ぶん濃さが変わるだけで済む
    const double secondsPerBeat = project.getBeatSecondsAt (scrollStartSeconds);
    const double secondsPerBar = project.getBarSecondsAt (scrollStartSeconds);
    const double pixelsPerBar = secondsPerBar * pixelsPerSecond;

    if (pixelsPerBar <= 0.0)
        return;

    // 縮小して小節が詰まってきたら間引く（線で埋まると、かえって位置が分からない）。
    // **ルーラーの間引き（44px）より細かくてよい**：数字が入らないだけで、線は読める
    int barStep = 1;

    while (pixelsPerBar * barStep < 8.0)
        barStep *= 2;

    // 拍の線は、十分に離れているときだけ。小節頭より薄くして序列を付ける
    const bool showBeats = (secondsPerBeat * pixelsPerSecond) >= 24.0;

    auto area = getTimelineArea();

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area);

    const int firstBar = project.getBarIndexAt (scrollStartSeconds);

    for (int bar = firstBar - (firstBar % barStep); ; bar += barStep)
    {
        // **`小節番号 × 小節の長さ` と書かないこと**（8.98／Phase 138）。
        // 小節ごとに長さが違う形にした瞬間に、後ろの小節が全部ずれます
        const double barStart = project.getBarStartTime (bar);
        const int x = timeToX (barStart);

        if (x > area.getRight())
            break;

        if (showBeats)
        {
            g.setColour (AppColours::border.withAlpha (0.30f));

            for (int beat = 1; beat < project.getBeatsPerBarAt (barStart); ++beat)
                g.drawVerticalLine (timeToX (project.getBeatStartTime (bar, beat)),
                                     (float) area.getY(), (float) area.getBottom());
        }

        g.setColour (AppColours::border.withAlpha (0.75f));
        g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
    }
}

void TimelineComponent::drawRuler (juce::Graphics& g)
{
    // ルーラーはヘッダーの上まで含めて1本の帯にする（角に表示切替ボタンが載る）
    g.setColour (AppColours::panel);
    g.fillRect (0, 0, getWidth(), rulerHeight);

    auto rulerArea = getRulerArea();

    g.saveState();
    g.reduceClipRegion (rulerArea);

    const int rightEdge = rulerArea.getRight();
    g.setFont (juce::FontOptions (10.0f));

    if (showBarsAndBeats)
    {
        // 仕様書5.9：小節/拍表示。**長さはProjectModelに訊く**（8.98／Phase 138）。
        // 間引きの目安は「画面の左端の小節」で測る
        const double secondsPerBeat = project.getBeatSecondsAt (scrollStartSeconds);
        const double secondsPerBar = project.getBarSecondsAt (scrollStartSeconds);
        const double pixelsPerBar = secondsPerBar * pixelsPerSecond;

        // 縮小して小節が詰まってきたら、目盛りを間引く（数字が重ならないように）
        int barStep = 1;
        while (pixelsPerBar * barStep < 44.0)
            barStep *= 2;

        const bool showBeats = (secondsPerBeat * pixelsPerSecond) >= 10.0;

        const int firstBar = project.getBarIndexAt (scrollStartSeconds);

        for (int bar = firstBar - (firstBar % barStep); ; bar += barStep)
        {
            // **`小節番号 × 小節の長さ` と書かないこと**（8.98／Phase 138）
            const double barStart = project.getBarStartTime (bar);
            const int x = timeToX (barStart);

            if (x > rightEdge)
                break;

            g.setColour (AppColours::textSecondary);
            g.drawLine ((float) x, (float) rulerContentTop, (float) x, (float) rulerNumbersBottom, 1.0f);
            g.drawText (juce::String (bar + 1), x + 3, rulerContentTop, 46,
                         rulerNumbersBottom - rulerContentTop - 2,
                         juce::Justification::centredLeft, false);

            // 拍の目盛りは、間引きしていないときだけ（間引き中は意味を持たないため）
            if (showBeats && barStep == 1)
            {
                for (int beat = 1; beat < project.getBeatsPerBarAt (barStart); ++beat)
                {
                    const int beatX = timeToX (project.getBeatStartTime (bar, beat));

                    g.setColour (AppColours::border);
                    g.drawLine ((float) beatX, (float) rulerNumbersBottom - 6.0f,
                                 (float) beatX, (float) rulerNumbersBottom, 1.0f);
                }
            }
        }
    }
    else
    {
        // 仕様書5.9：タイムコード表示。ズームに応じて「きりのよい間隔」を選ぶ
        static const double candidateSteps[] = { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0,
                                                  60.0, 120.0, 300.0, 600.0 };
        double step = candidateSteps[juce::numElementsInArray (candidateSteps) - 1];

        for (auto candidate : candidateSteps)
        {
            if (candidate * pixelsPerSecond >= 60.0)
            {
                step = candidate;
                break;
            }
        }

        const bool withTenths = (step < 1.0);
        const int firstIndex = juce::jmax (0, (int) std::floor (scrollStartSeconds / step));

        for (int i = firstIndex; ; ++i)
        {
            const double time = i * step;
            const int x = timeToX (time);

            if (x > rightEdge)
                break;

            g.setColour (AppColours::textSecondary);
            g.drawLine ((float) x, (float) rulerContentTop, (float) x, (float) rulerNumbersBottom, 1.0f);
            g.drawText (formatTimecode (time, withTenths), x + 3, rulerContentTop, 56,
                         rulerNumbersBottom - rulerContentTop - 2,
                         juce::Justification::centredLeft, false);
        }
    }

    // ルーラー上のプレイヘッド。クリップ側の縦線はルーラーまで届かないので別に描く。
    // **目盛りの下端まで**（Phase 145）——レーンは後から地を塗るので、
    // ルーラーの下端まで引いても2行ぶんが隠れるだけでした
    const int playheadX = timeToX (playheadSeconds);
    g.setColour (AppColours::orange);
    g.fillRect (playheadX - 1, rulerContentTop, 3, rulerNumbersBottom - rulerContentTop);

    g.restoreState();

    g.setColour (AppColours::border);
    g.drawLine (0.0f, (float) rulerHeight, (float) getWidth(), (float) rulerHeight, 1.0f);
}

//==============================================================================
// 仕様書5.6：オートメーション（Phase 19）
//==============================================================================

AutomationLane TimelineComponent::getAutomationLaneFor (AutomationRowRef ref) const
{
    if (ref.ordinal < 0)
        return AutomationLane (juce::ValueTree());

    // 8.56：**トラックとマスターの違いはここだけで吸収する**（Phase 94／D3）。
    // 呼び出し側に`isMasterRow()`を書かせると、必ずどこかで書き忘れる
    if (isMasterRow (ref.rowIndex))
        return project.getVisibleMasterAutomationLane (ref.ordinal);

    if (! juce::isPositiveAndBelow (ref.rowIndex, project.getNumTracks()))
        return AutomationLane (juce::ValueTree());

    return project.getTrack (ref.rowIndex).getVisibleAutomationLane (ref.ordinal);
}

juce::String TimelineComponent::getAutomationTargetFor (AutomationRowRef ref) const
{
    auto lane = getAutomationLaneFor (ref);

    return lane.state.isValid() ? lane.getTargetId() : juce::String();
}

juce::Colour TimelineComponent::getAutomationRowColour (AutomationRowRef ref) const
{
    // 8.59：**レーンが色を持っていなければ親トラックの色**（Phase 96）。
    // 落とし先の判断はここ1箇所（呼ぶ側に「持っているか」を書かせない）
    auto lane = getAutomationLaneFor (ref);

    if (lane.state.isValid() && lane.hasCustomColour())
        return juce::Colour::fromString (lane.getColourString());

    if (isMasterRow (ref.rowIndex) || ! juce::isPositiveAndBelow (ref.rowIndex, project.getNumTracks()))
        return AppColours::orange;   // マスターは他と性質が違うので固定（トラック行と同じ扱い）

    auto track = project.getTrack (ref.rowIndex);

    // 設計書2.4／2.6：VCAは固定でオレンジ（トラック行の色の決め方と揃える）
    return track.getType() == TrackType::VCA ? AppColours::orange
                                              : juce::Colour::fromString (track.getColourString());
}

bool TimelineComponent::isAutomationRowSelected (AutomationRowRef ref) const
{
    if (selectedLaneTargetId.isEmpty() || ! ref.isValid())
        return false;

    if (getAutomationTargetFor (ref) != selectedLaneTargetId)
        return false;

    // **マスターは空文字のtrackId**で表す（SelectionStateと同じ約束）
    const juce::String rowTrackId = isMasterRow (ref.rowIndex)
                                       ? juce::String()
                                       : (juce::isPositiveAndBelow (ref.rowIndex, project.getNumTracks())
                                              ? project.getTrack (ref.rowIndex).getId()
                                              : juce::String ("?"));

    return rowTrackId == selectedLaneTrackId;
}

void TimelineComponent::selectAutomationRow (AutomationRowRef ref)
{
    const auto targetId = getAutomationTargetFor (ref);

    if (targetId.isEmpty())
        return;

    // **先にクリップの選択を解く。** 選んでいるものは常に1つ、という形を崩さない
    // （残しておくと、Deleteがどちらに効くのか読めなくなる）。
    //
    // **順番が要点です。** `clearClipSelection()`は`publishSelection()`まで辿り着き、
    // そこで**レーンの選択も消される**（そう作ってある）。あとから設定すること
    selectedClipIndex = -1;
    selectedIsMidi = false;
    clearClipSelection();

    selectedLaneTrackId = isMasterRow (ref.rowIndex)
                             ? juce::String()
                             : project.getTrack (ref.rowIndex).getId();
    selectedLaneTargetId = targetId;

    selection.selectAutomationLane (selectedLaneTrackId, selectedLaneTargetId);
    repaint();
}

bool TimelineComponent::isShowingAutomation() const
{
    // マスター行を出すかどうかの判断に使う（判断はモデル側に1つ。8.56）
    return project.isAnyAutomationLaneVisible();
}

int TimelineComponent::getNumAutomationRows (int rowIndex) const
{
    if (isMasterRow (rowIndex))
        return project.getNumVisibleMasterAutomationLanes();

    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return 0;

    return project.getTrack (rowIndex).getNumVisibleAutomationLanes();
}

int TimelineComponent::getAutomationRowsHeight (int rowIndex) const
{
    int total = 0;

    for (int i = 0, n = getNumAutomationRows (rowIndex); i < n; ++i)
        total += getAutomationLaneFor ({ rowIndex, i }).getRowHeight();

    return total;
}

int TimelineComponent::getNumRows() const
{
    // マスター行は「どこかのトラックがレーンを開いている間」ずっと出す。
    // マスターのレーンが開いているときだけ出すと、**開くためのボタンが無い**という
    // 堂々巡りになるため（行が無ければボタンも描かれない）。
    return project.getNumTracks() + (isShowingAutomation() ? 1 : 0);
}

int TimelineComponent::getContentHeightPixels() const
{
    // Phase 31：末尾の「+ 新しいトラック」も、スクロールで届く必要がある。
    // Phase 58：行ごとに高さが違うので、掛け算ではなく足し上げる（8.18）。
    // Phase 60：**固定行はスクロールしないので数に入れない**（8.20）
    const int pinned = getPinnedRowIndex();
    int total = 0;

    for (int i = 0, numRows = getNumRows(); i < numRows; ++i)
        if (i != pinned)
            total += getRowHeight (i);

    return total + addTrackRowHeight;
}

void TimelineComponent::updateReorderTarget (juce::Point<int> position)
{
    // 8.51：**掴んだヘッダーの落とし先を決める**（Phase 90／D2）。
    //
    // 決めるものは2つ：**どこへ差し込むか（行）**と、**どのフォルダに入れるか**。
    //
    // - **フォルダの行の上にいるなら、その中へ**（いちばん分かりやすい入れ方）
    // - それ以外は、**差し込む位置のすぐ上の行から受け継ぐ**
    // - **深さは横の位置で決める**：右へ寄せると深く、左へ寄せると浅くなる。
    //   **左端まで運べばフォルダから出る**（出す道が要るので、ここが効く）
    reorderTargetSlot = getReorderSlotForY (position.y);
    reorderTargetFolderId.clear();

    // 8.70：写しを描く位置（Phase 109）。**深さを決めている横の位置も覚えておく**
    reorderDragPosition = position;

    if (! juce::isPositiveAndBelow (reorderSourceIndex, project.getNumTracks()))
        return;

    auto dragged = project.getTrack (reorderSourceIndex);

    //--------------------------------------------------------------------------
    // フォルダの行の上にいるか

    const int overRow = getTrackIndexForY (position.y);

    if (juce::isPositiveAndBelow (overRow, project.getNumTracks()) && overRow != reorderSourceIndex)
    {
        auto overTrack = project.getTrack (overRow);

        if (overTrack.getType() == TrackType::Folder
             && project.canMoveTrackIntoFolder (dragged, overTrack.getId()))
        {
            reorderTargetFolderId = overTrack.getId();
            reorderTargetSlot = project.getLastRowOfFolder (overTrack.getId()) + 1;
            return;
        }
    }

    //--------------------------------------------------------------------------
    // すぐ上の行から受け継ぐ

    int above = reorderTargetSlot - 1;

    while (above >= 0 && above == reorderSourceIndex)
        --above;   // 掴んでいる行は「上の行」に数えない（自分を親にしてしまう）

    if (! juce::isPositiveAndBelow (above, project.getNumTracks()))
        return;

    auto aboveTrack = project.getTrack (above);

    juce::String parentId = (aboveTrack.getType() == TrackType::Folder)
                                ? aboveTrack.getId()
                                : aboveTrack.getParentFolderId();

    // 横の位置から「何階層目に置きたいか」を読む
    const int wantedDepth = juce::jmax (0, (position.x - headerColourBandWidth) / headerIndentPerLevel);

    // 深すぎる指定は、上の行が許す深さまでで止める
    for (int guard = 0; guard < ProjectModel::maxFolderDepth && parentId.isNotEmpty(); ++guard)
    {
        auto parent = project.findTrackById (parentId);

        if (! parent.state.getParent().isValid())
        {
            parentId.clear();
            break;
        }

        // このフォルダの中に置いたときの深さ
        const int depthHere = project.getTrackFolderDepth (parent) + 1;

        if (depthHere <= wantedDepth)
            break;   // ちょうどよい深さ

        parentId = parent.getParentFolderId();   // 1つ浅くする
    }

    if (parentId.isNotEmpty() && ! project.canMoveTrackIntoFolder (dragged, parentId))
        parentId.clear();

    reorderTargetFolderId = parentId;
}
juce::StringArray TimelineComponent::getTrackIdsForReorder (int draggedIndex) const
{
    // 8.200：**掴んだ行が選択に入っていなければ、その1本だけ**（Phase 235/改善案5の9）。
    // ヘッダーのメニューと同じ数え方です（`ArrangeView::getTrackIdsForHeaderAction()`）——
    // **同じ操作の結果が入口で違うのが、いちばん困ります**（1.27）
    juce::StringArray ids;

    if (! juce::isPositiveAndBelow (draggedIndex, project.getNumTracks()))
        return ids;

    const auto draggedId = project.getTrack (draggedIndex).getId();

    if (std::find (selectedTrackIds.begin(), selectedTrackIds.end(), draggedId)
         == selectedTrackIds.end())
    {
        ids.add (draggedId);
        return ids;
    }

    // **並び順で返す**（画面の上から）。置き先を1つずつ進めるので、
    // ここが崩れると**上下が入れ替わって着地します**
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto id = project.getTrack (t).getId();

        if (std::find (selectedTrackIds.begin(), selectedTrackIds.end(), id)
             != selectedTrackIds.end())
            ids.add (id);
    }

    return ids;
}

int TimelineComponent::getReorderSlotForY (int y) const
{
    // 行の**中央**を境目にする。行の上半分にいれば「その行の上」、下半分なら「下」。
    // getTrackIndexForY()のような切り捨てだと、行の下端まで運ばないと
    // 次の位置へ移らず、狙った場所へ落としにくい。
    //
    // Phase 58：高さが揃っていないので割り算では出せない（8.18）。
    // **いちばん近い境目を探す**と、高さが違っても「中央が境目」の手応えになる。
    //
    // Phase 60：固定行（コードトラック）の上へは入れない（8.20）。
    // **候補から外すだけでよい**：モデル側も`moveTrack()`で同じことを守っている
    const int firstSlot = getPinnedRowIndex() >= 0 ? 1 : 0;

    int bestSlot = firstSlot;
    int bestDistance = std::abs (y - getTrackRowY (firstSlot));

    for (int slot = firstSlot + 1; slot <= project.getNumTracks(); ++slot)
    {
        const int distance = std::abs (y - getTrackRowY (slot));

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestSlot = slot;
        }
    }

    return bestSlot;
}

int TimelineComponent::reorderSlotToTrackIndex (int slot) const
{
    if (slot < 0 || ! juce::isPositiveAndBelow (reorderSourceIndex, project.getNumTracks()))
        return -1;

    // 掴んだ行を抜いてから入れ直すので、**自分より下へ動かすときは1つ詰まる**。
    // ここを間違えると、1つ下へ落としたつもりが元の位置に戻る（動かないように見える）。
    return slot > reorderSourceIndex ? slot - 1 : slot;
}

juce::Rectangle<int> TimelineComponent::getAddTrackRowBounds() const
{
    // 最後の行のさらに下。getTrackRowY()はスクロール量を織り込んでいるので、
    // 行番号として`getNumRows()`を渡すだけでよい。
    return { 0, getTrackRowY (getNumRows()), trackHeaderWidth, addTrackRowHeight };
}

juce::Rectangle<int> TimelineComponent::getAutomationRowBounds (AutomationRowRef ref) const
{
    if (! ref.isValid() || ref.ordinal >= getNumAutomationRows (ref.rowIndex))
        return {};

    // 8.56：**レーンはトラックの領域の下から積む**（Phase 94／D3）。
    // 行そのものの位置は`getTrackRowY()`が今までどおり答えてくれる
    int y = getTrackRowY (ref.rowIndex) + getTrackAreaHeight (ref.rowIndex);

    for (int i = 0; i < ref.ordinal; ++i)
        y += getAutomationLaneFor ({ ref.rowIndex, i }).getRowHeight();

    return { 0, y, getWidth(), getAutomationLaneFor (ref).getRowHeight() };
}

juce::Rectangle<int> TimelineComponent::getAutomationHeaderBounds (AutomationRowRef ref) const
{
    auto row = getAutomationRowBounds (ref);

    if (row.isEmpty())
        return {};

    // 8.59：**字下げは親トラックのヘッダーと同じだけ**（Phase 96）。
    //
    // フォルダの中のトラックは枠ごと右へ縮まる（8.51）。レーンだけ左端から
    // 始まっていると、**どのトラックにぶら下がっているのかが並びで読めません**。
    // 8.50と同じく「中身をずらす」ではなく「枠ごと縮める」形に揃えてあります
    return row.withWidth (trackHeaderWidth).withTrimmedLeft (getHeaderIndent (ref.rowIndex));
}

juce::Rectangle<int> TimelineComponent::getAutomationCloseButtonBounds (AutomationRowRef ref) const
{
    auto header = getAutomationHeaderBounds (ref);

    if (header.isEmpty())
        return {};

    // **右端**に置く。トラックヘッダーの「A」（左寄り）と離しておかないと、
    // 開いたつもりで閉じることになる
    return { header.getRight() - automationCloseButtonSize - 4,
             header.getCentreY() - automationCloseButtonSize / 2,
             automationCloseButtonSize, automationCloseButtonSize };
}

juce::Rectangle<int> TimelineComponent::getAutomationBypassButtonBounds (AutomationRowRef ref) const
{
    auto closeBounds = getAutomationCloseButtonBounds (ref);

    if (closeBounds.isEmpty())
        return {};

    // 8.59：「x」のすぐ左（Phase 96）。**位置は「x」から求める**ので、
    // 片方を動かしたときにもう片方が重なることがない
    return closeBounds.withX (closeBounds.getX() - automationCloseButtonSize - 3);
}

juce::Rectangle<int> TimelineComponent::getAutomationLaneArea (AutomationRowRef ref) const
{
    auto row = getAutomationRowBounds (ref);

    if (row.isEmpty())
        return {};

    return { trackHeaderWidth, row.getY() + automationVerticalMargin,
             juce::jmax (0, getWidth() - trackHeaderWidth - scrollBarThickness),
             juce::jmax (1, row.getHeight() - automationVerticalMargin * 2) };
}

TimelineComponent::AutomationRowRef TimelineComponent::findAutomationRowAtY (int y) const
{
    // **行番号は今までどおり`getTrackIndexForY()`が答える**（レーンは行の中にある）。
    // そのうえで「トラックの領域より下か」を見て、何番目のレーンかを数える
    const int rowIndex = getTrackIndexForY (y);

    if (rowIndex < 0 || rowIndex > getMasterRowIndex())
        return {};

    const int numLanes = getNumAutomationRows (rowIndex);

    if (numLanes <= 0)
        return {};

    for (int i = 0; i < numLanes; ++i)
    {
        auto bounds = getAutomationRowBounds ({ rowIndex, i });

        if (! bounds.isEmpty() && y >= bounds.getY() && y < bounds.getBottom())
            return { rowIndex, i };
    }

    return {};
}

float TimelineComponent::getCurrentAutomationValueFor (AutomationRowRef ref) const
{
    const auto targetId = getAutomationTargetFor (ref);

    if (targetId.isEmpty())
        return 0.0f;

    if (isMasterRow (ref.rowIndex))
        return AutomationTargets::fromParameterValue (targetId, project.getMasterVolumeDb());

    if (! juce::isPositiveAndBelow (ref.rowIndex, project.getNumTracks()))
        return 0.0f;

    auto track = project.getTrack (ref.rowIndex);

    return AutomationTargets::fromParameterValue (
        targetId,
        targetId == AutomationTargets::pan ? track.getPan() : track.getVolumeDb());
}

int TimelineComponent::automationValueToY (AutomationRowRef ref, float value) const
{
    auto area = getAutomationLaneArea (ref);

    // 値1.0が上端、0.0が下端
    return area.getBottom() - (int) (juce::jlimit (0.0f, 1.0f, value) * (float) area.getHeight());
}

float TimelineComponent::yToAutomationValue (AutomationRowRef ref, int y) const
{
    auto area = getAutomationLaneArea (ref);

    if (area.getHeight() <= 0)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - y) / (float) area.getHeight());
}

void TimelineComponent::drawAutomationRowHeader (juce::Graphics& g, AutomationRowRef ref)
{
    auto header = getAutomationHeaderBounds (ref);

    if (header.isEmpty())
        return;

    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid())
        return;

    // 8.59：**字下げのぶんには縦線を1本**（Phase 96）。
    // トラックヘッダー（8.51）と同じ描き方にしておくと、
    // 「フォルダから伸びている」ことが並びの形で読める
    const int indent = getHeaderIndent (ref.rowIndex);

    if (indent > 0)
    {
        g.setColour (AppColours::border);
        g.drawLine ((float) header.getX() - 1.0f, (float) header.getY(),
                     (float) header.getX() - 1.0f, (float) header.getBottom(), 1.0f);
    }

    // 8.56：**トラックヘッダーより少し沈ませる**（Phase 94／D3）。
    // 「トラックにぶら下がっているもの」だと、並びの見た目で分かるようにする。
    // 8.59：**選択中はトラック行と同じパープル**（Phase 96。設計書2.6）
    const bool isSelected = isAutomationRowSelected (ref);

    g.setColour (isSelected ? AppColours::purple.withAlpha (0.18f) : AppColours::background);
    g.fillRect (header);
    g.setColour (isSelected ? AppColours::purple : AppColours::border);
    g.drawRect (header, isSelected ? 2 : 1);

    // 8.59：色の帯（Phase 96）。**レーンが色を持っていなければ親トラックの色**。
    // 判断は`getAutomationRowColour()`1箇所（呼ぶ側では分岐しない）
    g.setColour (getAutomationRowColour (ref));
    g.fillRect (header.withWidth (headerColourBandWidth).reduced (0, 2));

    auto textArea = header.withTrimmedLeft (headerColourBandWidth + headerControlLeftMargin + 4)
                          .withTrimmedRight (automationCloseButtonSize * 2 + 12);

    // 8.59：**バイパス中は文字も沈める**（Phase 96）。ボタンだけだと、
    // 行が何本も並んだときに「どれを切ってあるか」が一目で分からない
    g.setColour (lane.isBypassed() ? AppColours::textSecondary.withAlpha (0.5f)
                                    : AppColours::textSecondary);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (AutomationTargets::getDisplayName (lane.getTargetId()), textArea,
                 juce::Justification::centredLeft, true);

    // 8.59：**「B」でバイパス**（Phase 96）。点は消さずに効かせるのをやめる。
    // 入っているときはオレンジ（インサートのバイパスと同じ意味の色）
    drawHeaderChip (g, getAutomationBypassButtonBounds (ref), "B", lane.isBypassed(),
                     AppColours::orange);

    // 8.56：**行の右端の「x」で閉じる**（Phase 94）。
    // ヘッダーの「A」まで戻らずに畳めるようにしておく。
    // 1.30：**記号文字は使わない**（この環境のフォントに無い）ので、小文字のxで描く
    drawHeaderChip (g, getAutomationCloseButtonBounds (ref), "x", false, AppColours::purple);
}

void TimelineComponent::drawAutomationCurve (juce::Graphics& g, AutomationRowRef ref)
{
    auto lane = getAutomationLaneFor (ref);
    auto area = getAutomationLaneArea (ref);

    if (! lane.state.isValid() || area.isEmpty())
        return;

    // 8.59：**線の色はレーンの色**（Phase 96）。持っていなければ親トラックの色。
    // **バイパス中は薄くする**：点は残っているのに鳴っていない、という状態が
    // 一目で分かる必要がある（ボタンだけだと行が並んだときに見落とす）
    const auto laneColour = getAutomationRowColour (ref);
    const float laneAlpha = lane.isBypassed() ? 0.3f : 1.0f;

    // 地を少し染めて、クリップの並びとは別のものであることを示す
    g.setColour (laneColour.withAlpha (0.06f * laneAlpha));
    g.fillRect (area);

    // 点が無いレーンは、現在のフェーダー値のところに水平線を出す。
    // 「まだオートメーションが無い」ことと「今どの値か」が同時に分かる。
    if (lane.isEmpty())
    {
        const int y = automationValueToY (ref, getCurrentAutomationValueFor (ref));

        g.setColour (laneColour.withAlpha (0.35f * laneAlpha));
        g.drawLine ((float) area.getX(), (float) y, (float) area.getRight(), (float) y, 1.0f);
        return;
    }

    juce::Path curve;
    const int numPoints = lane.getNumPoints();

    for (int i = 0; i < numPoints; ++i)
    {
        auto point = lane.getPoint (i);
        const float x = (float) timeToX (point.getTime());
        const float y = (float) automationValueToY (ref, point.getValue());

        if (i == 0)
        {
            // 最初の点より前は値が一定（ProjectModel::getValueAtと同じ扱い）
            curve.startNewSubPath ((float) area.getX(), y);
            curve.lineTo (x, y);
        }
        else
        {
            // 区間の形は「手前の点」が持つ（仕様書5.6）。
            // 8.37：**線を引くのはピアノロールと共用**（Phase 77）。
            // ここに同じ式を書き写していたせいで、Phase 76で繋ぎ方を足したときに
            // アレンジ画面だけ直線のまま残りかけた
            auto previous = lane.getPoint (i - 1);
            const float previousX = (float) timeToX (previous.getTime());
            const float previousY = (float) automationValueToY (ref, previous.getValue());

            AutomationCurveUI::appendCurve (curve, previousX, previousY, x, y,
                                             previous.getCurve(), previous.getCurveAmount());
        }

        if (i == numPoints - 1)
            curve.lineTo ((float) area.getRight(), y); // 最後の点より後も一定
    }

    g.setColour (laneColour.withAlpha (laneAlpha));
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    for (int i = 0; i < numPoints; ++i)
    {
        auto point = lane.getPoint (i);
        const float x = (float) timeToX (point.getTime());
        const float y = (float) automationValueToY (ref, point.getValue());

        const bool isBeingDragged = (dragMode == DragMode::AutomationPoint
                                      && automationDragRow == ref && i == automationPointIndex);

        g.setColour (isBeingDragged ? AppColours::orange : laneColour.withAlpha (laneAlpha));
        g.fillEllipse (x - automationPointRadius, y - automationPointRadius,
                        automationPointRadius * 2.0f, automationPointRadius * 2.0f);

        // 8.57：**選んでいる点は輪で囲む**（Phase 95／D14）。
        // 塗り色を変えるだけだと、掴んでいる点（オレンジ）と見分けが付かない
        if (isAutomationPointSelected (point.state))
        {
            g.setColour (AppColours::orange);
            g.drawEllipse (x - automationPointRadius - 2.0f, y - automationPointRadius - 2.0f,
                            (automationPointRadius + 2.0f) * 2.0f, (automationPointRadius + 2.0f) * 2.0f,
                            1.5f);
        }
    }

    // 8.37：**曲がり具合のつまみ**（Phase 77）。点の後に描く（重なったら上に出す）。
    // **点を動かしている最中は出さない**（位置がズレて見えるため）
    if (! (dragMode == DragMode::AutomationPoint && automationDragRow == ref))
    {
        for (int i = 1; i < numPoints; ++i)
        {
            auto previous = lane.getPoint (i - 1);
            auto next = lane.getPoint (i);

            const float fromX = (float) timeToX (previous.getTime());
            const float fromY = (float) automationValueToY (ref, previous.getValue());
            const float toX   = (float) timeToX (next.getTime());
            const float toY   = (float) automationValueToY (ref, next.getValue());

            if (! AutomationCurveUI::segmentHasHandle (fromX, fromY, toX, toY, previous.getCurve()))
                continue;

            const bool isDragged = (dragMode == DragMode::CurveHandle
                                     && curveDragRow == ref && i - 1 == curvePointIndex);

            AutomationCurveUI::drawHandle (g,
                                            AutomationCurveUI::getHandlePosition (fromX, fromY, toX, toY,
                                                                                   previous.getCurve(),
                                                                                   previous.getCurveAmount()),
                                            laneColour.withAlpha (laneAlpha), isDragged);
        }
    }
}

bool TimelineComponent::hitTestCurveHandle (juce::Point<int> position,
                                              AutomationRowRef& rowOut, int& pointIndexOut) const
{
    if (! getTimelineArea().contains (position))
        return false;

    const auto ref = findAutomationRowAtY (position.y);

    if (! ref.isValid())
        return false;

    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid())
        return false;

    for (int i = 1; i < lane.getNumPoints(); ++i)
    {
        auto previous = lane.getPoint (i - 1);
        auto next = lane.getPoint (i);

        const float fromX = (float) timeToX (previous.getTime());
        const float fromY = (float) automationValueToY (ref, previous.getValue());
        const float toX   = (float) timeToX (next.getTime());
        const float toY   = (float) automationValueToY (ref, next.getValue());

        if (! AutomationCurveUI::segmentHasHandle (fromX, fromY, toX, toY, previous.getCurve()))
            continue;

        const auto handle = AutomationCurveUI::getHandlePosition (fromX, fromY, toX, toY,
                                                                   previous.getCurve(),
                                                                   previous.getCurveAmount());

        if (position.toFloat().getDistanceFrom (handle) <= AutomationCurveUI::handleHitRadius)
        {
            rowOut = ref;
            pointIndexOut = i - 1;   // 区間の形を持っているのは手前の点
            return true;
        }
    }

    return false;
}

bool TimelineComponent::hitTestAutomationLine (AutomationRowRef ref, juce::Point<int> position,
                                                 float& valueOut) const
{
    auto lane = getAutomationLaneFor (ref);

    // **行そのもので判定する**（`getAutomationLaneArea()`ではなく）。
    // 線が上端・下端まで振り切れているとき、その少し外を押しても
    // 「線の上」として扱いたい（余白のぶんだけ押せない帯ができるのを防ぐ）
    if (! lane.state.isValid() || position.x < trackHeaderWidth
         || ! getAutomationRowBounds (ref).contains (position))
        return false;

    // **値は`getValueAt()`から引く。** 描いている線（`drawAutomationCurve()`）も
    // 同じ関数と同じ`applyAutomationCurve()`を通しているので、
    // 曲げた区間でも見えている線と当たり判定がずれない（8.37）
    const float fallback = getCurrentAutomationValueFor (ref);
    const float value = lane.getValueAt (xToTime (position.x), fallback);
    const int lineY = automationValueToY (ref, value);

    if (std::abs (position.y - lineY) > automationLineHitTolerance)
        return false;

    valueOut = value;
    return true;
}

bool TimelineComponent::hitTestAutomationPoint (juce::Point<int> position,
                                                  AutomationRowRef& rowOut, int& pointIndexOut) const
{
    if (! getTimelineArea().contains (position))
        return false;

    const auto ref = findAutomationRowAtY (position.y);

    if (! ref.isValid())
        return false;

    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid())
        return false;

    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        auto point = lane.getPoint (i);
        const juce::Point<int> pointPosition (timeToX (point.getTime()),
                                               automationValueToY (ref, point.getValue()));

        if (position.getDistanceFrom (pointPosition) <= automationHitRadius)
        {
            rowOut = ref;
            pointIndexOut = i;
            return true;
        }
    }

    return false;
}

//==============================================================================
// 8.57：レーンでのツールと複数選択（Phase 95／D14）
//
// **ピアノロール下のレーン（8.38／Phase 78）と同じ形**にしてある。
// あちらで踏んだ落とし穴（なぞり書きの間引き・ならし、左へなぞったときに
// 置いたそばから消える件、離したときの並べ直し）は、ここでも同じように効く。
//==============================================================================

bool TimelineComponent::isAutomationPointSelected (const juce::ValueTree& pointState) const
{
    return std::find (selectedAutomationPoints.begin(), selectedAutomationPoints.end(), pointState)
             != selectedAutomationPoints.end();
}

void TimelineComponent::clearAutomationSelection()
{
    if (selectedAutomationPoints.empty())
        return;

    selectedAutomationPoints.clear();
    repaint();
}

void TimelineComponent::pruneAutomationSelection()
{
    // Undoや削除で消えた点を残さない（消えた点を動かし続けることになる）
    selectedAutomationPoints.erase (
        std::remove_if (selectedAutomationPoints.begin(), selectedAutomationPoints.end(),
                         [] (const juce::ValueTree& state)
                         {
                             return ! state.isValid() || ! state.getParent().isValid();
                         }),
        selectedAutomationPoints.end());
}

void TimelineComponent::applyAutomationRangeSelection (AutomationRowRef ref)
{
    selectedAutomationPoints.clear();

    auto lane = getAutomationLaneFor (ref);

    if (lane.state.isValid())
    {
        for (int i = 0; i < lane.getNumPoints(); ++i)
        {
            auto point = lane.getPoint (i);
            const juce::Point<int> position (timeToX (point.getTime()),
                                              automationValueToY (ref, point.getValue()));

            if (automationRangeBounds.contains (position))
                selectedAutomationPoints.push_back (point.state);
        }
    }

    repaint();
}

void TimelineComponent::deleteSelectedAutomationPoints()
{
    pruneAutomationSelection();

    if (selectedAutomationPoints.empty())
        return;

    project.beginAction (utf8 ("オートメーション点の削除"));

    auto& undoManager = project.getUndoManager();

    // **控えを取ってから消すこと。** 消しながら選択を触ると、
    // 途中で入れ物が変わって残りを取りこぼす
    const auto points = selectedAutomationPoints;
    selectedAutomationPoints.clear();

    for (const auto& state : points)
        if (state.isValid() && state.getParent().isValid())
            state.getParent().removeChild (state, &undoManager);

    // 掴んだままの参照を残さない
    automationDragRow = {};
    automationPointIndex = -1;
    automationDragOthers.clear();
    dragMode = DragMode::None;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

bool TimelineComponent::copyAutomationSelection (bool alsoDelete)
{
    pruneAutomationSelection();

    if (selectedAutomationPoints.empty())
        return false;

    // **基準はいちばん早い点**（クリップ・ノートと同じ。8.29）。
    // 貼り付け先の時刻にここからの差を足せば、選んだときの間隔がそのまま保たれる
    double referenceTime = std::numeric_limits<double>::max();

    for (const auto& state : selectedAutomationPoints)
        referenceTime = juce::jmin (referenceTime, AutomationPoint (state).getTime());

    juce::Array<EditClipboard::Item> items;

    for (const auto& state : selectedAutomationPoints)
    {
        EditClipboard::Item item;
        item.state = state.createCopy();   // **複製を入れること**（8.29）
        item.timeOffset = AutomationPoint (state).getTime() - referenceTime;
        item.beatOffset = AutomationPoint (state).getTimeBeats()
                            - project.getBeatPositionAt (referenceTime);   // 8.139
        items.add (item);
    }

    EditClipboard::set (EditClipboard::Kind::automationPoints, std::move (items));

    if (alsoDelete)
        deleteSelectedAutomationPoints();

    return true;
}

void TimelineComponent::moveOtherSelectedAutomationPoints (double deltaTime, float deltaValue)
{
    auto& undoManager = project.getUndoManager();

    for (const auto& origin : automationDragOthers)
    {
        if (! origin.state.isValid() || ! origin.state.getParent().isValid())
            continue;

        // **元の位置＋ずらし量**で書く（今の位置に足すと、ドラッグのたびに二重に動く）
        AutomationPoint point (origin.state);
        point.setTime (juce::jmax (0.0, origin.time + deltaTime), &undoManager);
        point.setValue (juce::jlimit (0.0f, 1.0f, origin.value + deltaValue), &undoManager);
    }
}

void TimelineComponent::removeAutomationPointsInTimeRange (AutomationRowRef ref,
                                                             double fromTime, double toTime)
{
    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid())
        return;

    const double low = juce::jmin (fromTime, toTime);
    const double high = juce::jmax (fromTime, toTime);

    auto& undoManager = project.getUndoManager();

    // 8.38の落とし穴：**両端を含めないこと。** 含めると、左へなぞったときに
    // 置いたそばから前の点が消えて、最後の1点しか残らない
    for (int i = lane.getNumPoints(); --i >= 0;)
    {
        const double time = lane.getPoint (i).getTime();

        if (time > low && time < high)
            lane.removePoint (i, &undoManager);
    }
}

void TimelineComponent::paintAutomationPointAt (AutomationRowRef ref, juce::Point<int> position)
{
    if (! ref.isValid())
        return;

    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid())
        return;

    const int x = juce::jmax (trackHeaderWidth + 1, position.x);

    // **細かすぎる点は置かない。** なぞった距離ぶんだけ点が増えると、
    // 見た目も鳴らす量も無駄に重くなる（8.38）
    if (automationPaintStarted && std::abs (x - automationPaintLastX) < automationPaintMinPixels)
        return;

    // **手ぶれをならす。** 生の値をそのまま置くと、線が細かくギザギザになる
    const float raw = yToAutomationValue (ref, position.y);

    automationPaintValue = automationPaintStarted
                              ? automationPaintValue + (raw - automationPaintValue) * automationPaintSmoothing
                              : raw;

    const double time = juce::jmax (0.0, xToTime (x));

    // なぞった範囲にあった点は消す。**残したまま足すと、元の形と混ざって暴れる**
    // （`AutomationLane::writeValue()`と同じ考え方）
    removeAutomationPointsInTimeRange (ref, automationPaintStarted ? automationPaintLastTime : time,
                                        time);

    lane.addPoint (time, juce::jlimit (0.0f, 1.0f, automationPaintValue), &project.getUndoManager());

    automationPaintStarted = true;
    automationPaintLastX = x;
    automationPaintLastTime = time;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

bool TimelineComponent::eraseAutomationPointAt (juce::Point<int> position)
{
    AutomationRowRef ref;
    int pointIndex = -1;

    if (! hitTestAutomationPoint (position, ref, pointIndex))
        return false;

    auto lane = getAutomationLaneFor (ref);

    if (! lane.state.isValid() || ! juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
        return false;

    // **選択からも外す**（消えた点を掴んだままにしない）
    const auto state = lane.getPoint (pointIndex).state;

    selectedAutomationPoints.erase (std::remove (selectedAutomationPoints.begin(),
                                                  selectedAutomationPoints.end(), state),
                                     selectedAutomationPoints.end());

    lane.removePoint (pointIndex, &project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

//==============================================================================
// 仕様書4.4・6章：ドラッグ&ドロップの受け口（Phase 21）
//==============================================================================

bool TimelineComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return DragAndDropIds::isPluginDrag (details.description)
            || DragAndDropIds::isFileDrag (details.description);
}

void TimelineComponent::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void TimelineComponent::itemDragMove (const SourceDetails& details)
{
    // どのトラック行の上にいるかを覚えて、ハイライトで示す。
    // プラグインは「そのトラックへ挿す」、ファイルは「そのトラックへ置く」ので、
    // どちらも行が決まらないと落とし先が決まらない。
    const int row = getTrackIndexForY (details.localPosition.y);
    const bool isTrackRow = juce::isPositiveAndBelow (row, project.getNumTracks());

    int newRow = -1;

    if (isTrackRow)
    {
        const auto type = project.getTrack (row).getType();

        if (DragAndDropIds::isFileDrag (details.description))
            newRow = (type == TrackType::Audio) ? row : -1; // 音声ファイルはAudioトラックのみ
        else
            newRow = trackTypeHasAudioPath (type) ? row : -1;
    }

    // 8.123：**空いている場所も落とし先**（Phase 158／改善案7）。
    //
    // 音源プラグインなら「トラックごと作る」ので、行が無くても受けます。
    // **どのプラグインかはここでは分からない**（識別子しか来ない）ので、
    // 「行の上ではない」ことだけを覚えて、種類の判断は落としたときに任せます
    // **ルーラーの上は除くこと。** `getTrackIndexForY()`はそこでも-1を返すので、
    // 「行ではない」だけで判断すると、時間の物差しへ落としてトラックが増える
    const bool overEmpty = ! isTrackRow
                             && details.localPosition.y >= rulerHeight
                             && DragAndDropIds::isPluginDrag (details.description);

    if (newRow == dragOverRowIndex && overEmpty == dragOverEmptyArea)
        return;

    dragOverRowIndex = newRow;
    dragOverEmptyArea = overEmpty;
    repaint();
}

void TimelineComponent::itemDragExit (const SourceDetails&)
{
    dragOverEmptyArea = false;
    dragOverRowIndex = -1;
    repaint();
}

void TimelineComponent::itemDropped (const SourceDetails& details)
{
    const int row = dragOverRowIndex;
    const bool hasValidRow = juce::isPositiveAndBelow (row, project.getNumTracks());
    const auto trackId = hasValidRow ? project.getTrack (row).getId() : juce::String();

    dragOverRowIndex = -1;
    dragOverEmptyArea = false;
    repaint();

    if (DragAndDropIds::isPluginDrag (details.description))
    {
        // 8.123：**行が無くても渡す**（Phase 158／改善案7）。
        //
        // それまでは「挿し先が決まらないと意味が無い」として捨てていました。
        // いまは`trackId`が空＝**受け手が作る**という合図です
        // （音声ファイルの落とし込みが先に使っている決まり。すぐ下を参照）。
        //
        // **音源かエフェクトかの判断はここでしない。** ここに来るのは識別子だけで、
        // 一覧を引けるのは`ArrangeView`から先です
        // ルーラーの上で離したときは何もしない（`itemDragMove()`の判断と揃える）
        if (! hasValidRow && details.localPosition.y < rulerHeight)
            return;

        if (onPluginDropped != nullptr)
            onPluginDropped (trackId, DragAndDropIds::getPluginIdentifier (details.description));

        return;
    }

    if (DragAndDropIds::isFileDrag (details.description) && onFilesDropped != nullptr)
    {
        // 落とした横位置がそのままクリップの開始位置になる（トラックヘッダーの上なら0秒）。
        // オーディオトラックの上でなくても受け取り、trackIdを空で渡す。
        // 受け手側が「最初のオーディオトラック、無ければ新規作成」に振り分ける。
        // Phase 54：落とす位置もスナップに従う（8.14）
        const double startTime = project.snapTime (xToTime (details.localPosition.x));

        // 8.154：**1つでも配列で渡す**（Phase 192）。外からのドラッグと同じ口
        onFilesDropped ({ DragAndDropIds::getFile (details.description).getFullPathName() },
                         trackId, startTime);
    }
}

//==============================================================================
// 8.154：**DAWの外からのドラッグ**（Phase 192／本人の要望）

bool TimelineComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    // **読める音声ファイルが1つでもあれば受けます。** 全部が音声である必要は
    // ありません——フォルダごと掴んだときに、混ざっているだけで何も置けないのは不便です
    for (const auto& path : files)
        if (isReadableAudioFile (juce::File (path)))
            return true;

    return false;
}

void TimelineComponent::fileDragEnter (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
}

void TimelineComponent::fileDragMove (const juce::StringArray&, int, int y)
{
    // **中からのドラッグと同じ判断を通します**（`updateFileDragRow()`）。
    // 2つ書くと、片方だけ「音声以外の行にも置ける」といった食い違いになります
    updateFileDragRow (y);
}

void TimelineComponent::fileDragExit (const juce::StringArray&)
{
    dragOverEmptyArea = false;
    dragOverRowIndex = -1;
    repaint();
}

void TimelineComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    updateFileDragRow (y);

    const int row = dragOverRowIndex;
    const bool hasValidRow = juce::isPositiveAndBelow (row, project.getNumTracks());
    const auto trackId = hasValidRow ? project.getTrack (row).getId() : juce::String();

    dragOverRowIndex = -1;
    dragOverEmptyArea = false;
    repaint();

    if (onFilesDropped == nullptr)
        return;

    // **音声として読めないものは、ここで落とす。** 受け手まで持っていくと、
    // 「読めませんでした」が置けなかったファイルの数だけ出ます
    juce::StringArray audioFiles;

    for (const auto& path : files)
        if (isReadableAudioFile (juce::File (path)))
            audioFiles.add (path);

    if (audioFiles.isEmpty())
        return;

    // ルーラーの上で離したときは、位置だけ受けて行は指定しない
    // （中からのドラッグと揃える。`itemDropped()`）
    const double startTime = project.snapTime (xToTime (x));

    onFilesDropped (audioFiles, trackId, startTime);
}

bool TimelineComponent::isReadableAudioFile (const juce::File& file) const
{
    if (! file.existsAsFile())
        return false;

    // **拡張子で判断します。** 中身を開くと、ドラッグでマウスを動かすたびに
    // ファイルを開くことになります（`findFormatForFileExtension()`は表を引くだけ）
    return waveformCache.getFormatManager().findFormatForFileExtension (file.getFileExtension())
             != nullptr;
}

void TimelineComponent::updateFileDragRow (int y)
{
    const int row = getTrackIndexForY (y);
    const bool isTrackRow = juce::isPositiveAndBelow (row, project.getNumTracks());

    // 音声ファイルはAudioトラックにだけ置けます（`itemDragMove()`と同じ判断）
    const int newRow = (isTrackRow && project.getTrack (row).getType() == TrackType::Audio)
                           ? row : -1;

    if (newRow == dragOverRowIndex && ! dragOverEmptyArea)
        return;

    dragOverRowIndex = newRow;
    dragOverEmptyArea = false;
    repaint();
}

void TimelineComponent::seekToX (int x)
{
    // Phase 54：**画面上の位置を指して動かすシークは、スナップに従う**（8.14）。
    // マーカーへのジャンプのように「決まった時刻へ動かす」ものは
    // `seekToTime()`を直接呼ぶこと（寄せると、マーカーの位置と再生位置がずれる）。
    seekToTime (project.snapTime (xToTime (x)));
}

void TimelineComponent::seekToTime (double seconds)
{
    const double time = juce::jmax (0.0, seconds);

    // 手応えのため、エンジンからの折り返しを待たずに自分の表示も動かす
    playheadSeconds = time;

    if (onSeek != nullptr)
        onSeek (time);

    repaint();
}

void TimelineComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::canvas);

    // 仕様書5.9：小節頭の縦ライン（Phase 62／8.1のC7）。
    // **行より先に描く**：ヘッダーの下や、行の境目の線より奥に沈ませる
    drawTimelineGrid (g);

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // 8.56：**ここで使うのはトラックの領域だけ**（Phase 94／D3）。
        // `getRowHeight()`はレーンのぶんを含むので、それで枠を描くと
        // ヘッダーがレーンの高さまで伸びてしまう
        auto rowBounds = juce::Rectangle<int> (0, getTrackRowY (t), getWidth(), getTrackAreaHeight (t));

        // 縦スクロールで画面外に出た行は描かない（**レーンも含めた高さ**で判断する）
        if (getTrackRowY (t) + getRowHeight (t) < 0 || rowBounds.getY() > getHeight())
            continue;

        // 8.50：**畳んだフォルダの中は高さ0**（Phase 89／D2）。
        // 0のまま枠を描くと、線だけが残って「潰れた行」に見える
        if (rowBounds.getHeight() <= 0)
            continue;

        // 8.56：この行にぶら下がっているレーンのヘッダー（Phase 94／D3）
        for (int i = 0, n = getNumAutomationRows (t); i < n; ++i)
            drawAutomationRowHeader (g, { t, i });

        // 仕様書5.2.2：センドトラックは行の地を少し変えて見分けやすくする
        // （Phase 62／8.1のC6）。**まずタイムライン側**に重ねる。
        // クリップの並んでいない右側でも「これはセンド」と分かるように、行全体を塗る
        // 8.143：**パラアウトの受け皿も同じ塗り**（Phase 181／改善案⑮）。
        // どちらも「クリップが置けない、音が外から入ってくる行」なので、
        // **見分け方も同じにしておく**ほうが覚えることが増えません
        const bool isSendTrack = (track.getType() == TrackType::Send
                                   || track.getType() == TrackType::DrumOut);

        if (isSendTrack)
        {
            g.setColour (AppColours::sendTrackTint);
            g.fillRect (rowBounds);
        }

        auto headerBounds = rowBounds.removeFromLeft (trackHeaderWidth);

        // 8.51：**フォルダの中のトラックは、ヘッダーの左端を削って小さくする**（Phase 90）。
        // 字下げを「中身をずらす」ではなく「枠ごと縮める」にすると、
        // **どこまでが中身なのかが枠の形で分かります**（設計書2.4の「インデント表示」）
        {
            const int indent = getHeaderIndent (t);

            if (indent > 0)
            {
                auto spine = headerBounds.removeFromLeft (indent);

                // 削ったぶんには縦線を1本引く。**フォルダから伸びている**ことを示す
                g.setColour (AppColours::border);
                g.drawLine ((float) spine.getRight() - 1.0f, (float) spine.getY(),
                             (float) spine.getRight() - 1.0f, (float) spine.getBottom(), 1.0f);
            }
        }

        // 設計書2.6：選択中のトラックはパープルで示す（Phase 17）。
        // 8.126：**まとめて選んだぶんも同じ見た目**（Phase 162／改善案35）——
        // 「効く先」が見た目で分かれていないと、まとめて動かしたときに驚きます
        const bool isSelectedTrack = (t == selectedTrackIndex)
                                       || isTrackInSelection (track.getId());
        const auto trackColour = getTrackColour (t);

        g.setColour (AppColours::panel);
        g.fillRect (headerBounds);

        // 8.61：**トラックの色をヘッダーの地にも薄く敷く**（Phase 99／改善案⑫。設計書2.4）。
        //
        // 色帯だけだと5pxしかなく、**縦に並んだときに追いにくい**ものでした。
        // **薄く（0.12）にとどめること**：濃く塗ると、下に重ねる選択のパープルが
        // 色によって見えたり見えなかったりします（設計書2.6の「選択の見え方を壊さない」）
        g.setColour (trackColour.withAlpha (headerTintAlpha));
        g.fillRect (headerBounds);

        // **ヘッダーは地を塗り直したので、もう一度重ねる**（選択中はパープルが優先）
        if (isSendTrack && ! isSelectedTrack)
        {
            g.setColour (AppColours::sendTrackTint);
            g.fillRect (headerBounds);
        }

        // **選択のパープルはいちばん上に。** トラックの色より後に塗ることで、
        // どの色のトラックでも「選ばれている」ことが同じ濃さで分かる
        if (isSelectedTrack)
        {
            g.setColour (AppColours::purple.withAlpha (0.18f));
            g.fillRect (headerBounds);
        }

        g.setColour (isSelectedTrack ? AppColours::purple : AppColours::border);
        g.drawRect (headerBounds, isSelectedTrack ? 2 : 1);

        // 設計書2.4：トラックカラーの色帯を左端に出す（Phase 17）。
        // 色はデータ層がARGBのhex文字列で持っている（ProjectModel.cppのTrack::create参照）
        auto colourBand = headerBounds.removeFromLeft (headerColourBandWidth).reduced (0, 2);

        g.setColour (trackColour);
        g.fillRect (colourBand);

        drawTrackHeaderContents (g, t, track);

        g.setColour (AppColours::border);
        g.drawRect (rowBounds);
    }

    // 仕様書5.6：マスター行のヘッダー（Phase 20）。
    // どこかのトラックがレーンを開いている間だけ現れる（getNumRows()参照）
    if (isShowingAutomation())
    {
        const int masterRowIndex = getMasterRowIndex();

        // 8.56：ヘッダーはトラックの領域だけ（レーンのぶんは含めない。Phase 94）
        auto masterRow = juce::Rectangle<int> (0, getTrackRowY (masterRowIndex), getWidth(),
                                                getTrackAreaHeight (masterRowIndex));

        if (masterRow.getBottom() >= rulerHeight && masterRow.getY() <= getHeight())
        {
            // 8.56：マスターにぶら下がっているレーンのヘッダー（Phase 94／D3）
            for (int i = 0, n = getNumAutomationRows (masterRowIndex); i < n; ++i)
                drawAutomationRowHeader (g, { masterRowIndex, i });

            auto headerBounds = masterRow.removeFromLeft (trackHeaderWidth);

            g.setColour (AppColours::panel);
            g.fillRect (headerBounds);
            g.setColour (AppColours::border);
            g.drawRect (headerBounds);

            // マスターは他のトラックと性質が違うので、オレンジの帯で見分けられるようにする
            g.setColour (AppColours::orange);
            g.fillRect (headerBounds.removeFromLeft (headerColourBandWidth).reduced (0, 2));

            // 名前は上段（トラックと同じ位置に揃える。Phase 58）
            g.setColour (AppColours::textPrimary);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (utf8 ("Master"),
                         headerBounds.withHeight (headerNameRowHeight).withTrimmedLeft (4),
                         juce::Justification::centredLeft, false);

            // 仕様書5.6：マスターのレーンを開く「A」ボタン（Phase 26）。
            // **トラックと同じ位置に置く**（下段の左から2つめ）ので、
            // マスターだけ探し直さずに済む
            drawHeaderChip (g, getAutomationButtonBounds (masterRowIndex), "A",
                             project.getNumVisibleMasterAutomationLanes() > 0, AppColours::purple);

            g.setColour (AppColours::border);
            g.drawRect (masterRow);
        }
    }

    // 設計書2.3.1：トラック一覧の末尾の「+ 新しいトラック」（Phase 31）。
    // トラックを足す操作は「一覧の続き」なので、ツールバーより一覧の下のほうが素直。
    // ツールバーの「+ Track」も残してあり、どちらも同じメニューを出す。
    {
        auto addRow = getAddTrackRowBounds();

        if (addRow.getBottom() >= rulerHeight && addRow.getY() <= getHeight())
        {
            g.setColour (AppColours::background);
            g.fillRect (addRow);

            // トラック行のような塗り＋実線ではなく、角丸の細い枠にして
            // 「まだ中身が無い場所」であることが並びで分かるようにする
            g.setColour (AppColours::border);
            g.drawRoundedRectangle (addRow.reduced (5).toFloat(), AppColours::corner (4.0f), 1.0f);

            g.setColour (AppColours::textSecondary);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (utf8 ("＋ 新しいトラック"), addRow, juce::Justification::centred);
        }
    }

    // ここから先はクリップの描画。横スクロールすると、クリップがトラックヘッダーの
    // 位置まで来てしまうため、描画範囲をタイムライン部分に限定する。
    g.saveState();
    g.reduceClipRegion (getTimelineArea());

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // 8.91：**MIDIトラックは「ノートの塊」で描く**（Phase 131）。
        // クリップという入れ物が無くなったので、四角はノートから計算します
        if (track.getType() == TrackType::Midi)
        {
            for (const auto& block : getNoteBlocksFor (t))
                // 8.159：**範囲に入っている塊は「選んでいる」ように描く**（Phase 197）。
                // 範囲は時間の窓なので、**塊が窓と重なっていれば中**とみなします
                {
                    // 8.160：**範囲と重なっているぶんだけ**を渡す（Phase 198/本人の要望）。
                    // 範囲の端は拍（スナップ）に乗っているので、**拍単位で色が変わります**
                    juce::Rectangle<int> selectedPart;

                    if (isTrackInTimeRange (t))
                    {
                        const double from = juce::jmax (block.startTime, timeRangeStart);
                        const double to   = juce::jmin (block.endTime,   timeRangeEnd);

                        if (to > from + 1.0e-6)
                        {
                            const auto blockBounds = getNoteBlockBounds (t, block);

                            selectedPart = { timeToX (from), blockBounds.getY(),
                                              juce::jmax (1, timeToX (to) - timeToX (from)),
                                              blockBounds.getHeight() };
                        }
                    }

                    drawNoteBlock (g, getNoteBlockBounds (t, block), track, block,
                                    getTrackColour (t), selectedPart);
                }

            // 8.93：**時間範囲**（Phase 133）。塊より後に描く——範囲は「その上に
            // かぶせた窓」なので、塊に隠れると何を選んでいるのか分からない。
            //
            // 8.124：全トラックの範囲は**この中では描きません**（Phase 159／改善案5）。
            // ここはMIDIトラックのループの中なので、オーディオの行に出ません。
            // 種類を問わず描くため、ループの外にもう1つ置いてあります
            if (hasTimeRange && ! timeRangeAllTracks && isTimeRangeOnTrackIndex (t))
            {
                auto bounds = getTimeRangeBoundsFor (t);   // 8.158（Phase 196）

                g.setColour (AppColours::purple.withAlpha (0.22f));
                g.fillRect (bounds);
                g.setColour (AppColours::purple);
                g.drawRect (bounds, 2);
            }

            // 8.95：**行き先は「行き先のトラックの行」に描く**（Phase 135）。
            //
            // **元の範囲を描くループとは分けてあります**：縦に動かすと行き先は
            // 別のトラックの行になるので、`isTimeRangeOnTrackIndex (t)`の中に置くと
            // **移った先には何も出ません**（掴んだものが消えたように見える）
            if (draggingTimeRange && timeRangeDragTargetTrack == t)
            {
                const int startX = timeToX (timeRangeDragPreviewStart);
                const int endX = timeToX (timeRangeDragPreviewStart + (timeRangeEnd - timeRangeStart));

                juce::Rectangle<int> preview (startX, getTrackRowY (t) + 4,
                                               juce::jmax (2, endX - startX), getTrackAreaHeight (t) - 8);

                g.setColour (AppColours::orange.withAlpha (0.22f));
                g.fillRect (preview);
                g.setColour (AppColours::orange);
                g.drawRect (preview, 2);
            }

            continue;
        }

        // 仕様書5.2.3：コードトラックは、コード名を書いたブロックで描く（Phase 42）
        if (track.getType() == TrackType::Chord)
        {
            drawChordRegionsForTrack (g, t);
            continue;
        }

        // 8.201：**フォルダの行に、中身のまとまりを描く**（Phase 235／改善案5の10）。
        //
        // **見た目だけです**（本人の指定：「機能面はいらない」）。掴めませんし、
        // 動かせません——動かせるように見せると、押して初めて分かることになります。
        //
        // これが要るのは、**畳んだフォルダの行が空っぽに見える**ためです。
        // 中に何本入っていても、行はただの帯でした。
        if (track.getType() == TrackType::Folder)
        {
            drawFolderSummaryBlock (g, t);
            continue;
        }

        if (track.getType() != TrackType::Audio)
            continue;

        for (int c = 0; c < track.getNumClips(); ++c)
        {
            // 8.42：移動中は元をその場に残す（Phase 82/C17）。理由はMIDIクリップ側と同じ
            // 8.150：**ワープマーカーのドラッグ中も、クリップは普通に描くこと**
            // （Phase 188／8.48）。形が変わらない操作なので、
            // ここで飛ばすと**掴んでいる間だけクリップが消えます**
            const bool isDraggedClip = (dragMode != DragMode::None
                                         && dragMode != DragMode::Move
                                         && dragMode != DragMode::WarpMarker
                                         && t == selectedTrackIndex && c == selectedClipIndex);
            if (isDraggedClip)
                continue; // トリム・フェードは、後でプレビュー位置に描く

            auto clip = track.getClip (c);

            // Phase 51：複数選択されているものも「選択中」として描く
            const bool isSelected = (! selectedIsMidi && t == selectedTrackIndex && c == selectedClipIndex)
                                  || isClipSelected (track.getId(), clip.getId(), false);

            drawClip (g, getClipBounds (t, c), clip.getSourceFilePath(), clip.getOffset(), clip.getLength(),
                      clip.getFadeInSeconds(), clip.getFadeOutSeconds(), clip.getHitPoints(), isSelected,
                      clip.getGainLinear(), clip.isReversed(), getTrackColour (t), clip.getWarpMap());

            // 8.78：グループに入っている印（Phase 118/改善案㊱）
            drawClipGroupMarker (g, getClipBounds (t, c), clip.state);

            // 8.148：上下させた印（Phase 186/改善案㉞）
            drawClipTransposeMarker (g, getClipBounds (t, c), clip.getTranspose(), clip.getStretch());

            // 8.150：ワープマーカー（Phase 188/8.48）
            drawWarpMarkers (g, getClipBounds (t, c), clip);
        }
    }

    // 8.42：**移動中の行き先は、半透明を重ねて示す**（Phase 82／C17）。
    //
    // ノート側はPhase 53で同じ形にしてあります（8.13）：**元は消さず、行き先を上に描く。**
    // 「掴んだものが消えて、別の場所に現れる」形だと、**どこから動かしたのかが分からず**、
    // 少し戻したいときに元の位置を狙えません。
    //
    // トリム・フェードは**同じ場所で形が変わる**だけなので、今までどおり
    // プレビューの値でクリップそのものを描きます。
    // 8.150：**ワープマーカーもここ**（Phase 188／8.48）。クリップ自体は動かないので、
    // 下の「プレビューの値でクリップを描き直す」ほうではなく、線だけを重ねます
    if ((dragMode == DragMode::Move || dragMode == DragMode::WarpMarker)
         && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
    {
        drawClipDragPreview (g);
    }
    else if (dragMode != DragMode::None && selectedIsMidi)
    {
        // 8.94：**MIDIのドラッグ中プレビューはここには無い**（Phase 134）。
        // 塊と範囲は`hasTimeRange`の枠として、ノートを描いた直後に出しています
    }
    else if (dragMode != DragMode::None && selectedTrackIndex >= 0 && selectedClipIndex >= 0)
    {
        auto clip = project.getTrack (selectedTrackIndex).getClip (selectedClipIndex);

        const int clipX = timeToX (dragPreviewStartTime);
        const int clipWidth = juce::jmax (4, (int) (dragPreviewLength * pixelsPerSecond));
        juce::Rectangle<int> clipBounds (clipX, getTrackRowY (selectedTrackIndex) + 4, clipWidth,
                                          getTrackAreaHeight (selectedTrackIndex) - 8);

        drawClip (g, clipBounds, clip.getSourceFilePath(), dragPreviewOffset, dragPreviewLength,
                  dragPreviewFadeIn, dragPreviewFadeOut, clip.getHitPoints(), true, clip.getGainLinear(),
                  clip.isReversed(), getTrackColour (selectedTrackIndex),
                  makeDragPreviewWarpMap (clip));
    }

    // 8.56：オートメーションの行の中身（Phase 94／D3）。
    // **クリップと同じ切り抜きの中**にいるので、ヘッダーへは食い込まない。
    // ヘッダー側（パラメータ名と「×」）は行の描画といっしょに済ませてある
    for (int t = 0; t < getNumRows(); ++t)
    {
        auto rowBounds = juce::Rectangle<int> (0, getTrackRowY (t), getWidth(), getRowHeight (t));

        if (rowBounds.getBottom() < rulerHeight || rowBounds.getY() > getHeight())
            continue;

        for (int i = 0, n = getNumAutomationRows (t); i < n; ++i)
            drawAutomationCurve (g, { t, i });
    }

    // プレイヘッド（再生位置）を縦線で表示する。
    // クリップと同じくタイムライン部分に限定された描画範囲の内側で描く。
    const int playheadX = timeToX (playheadSeconds);
    g.setColour (AppColours::orange);
    g.drawLine ((float) playheadX, (float) rulerHeight, (float) playheadX, (float) getHeight(), 2.0f);

    g.restoreState();

    // 8.124：**全トラックにまたがる範囲**（Phase 159／改善案5）。
    //
    // **トラックのループの外に置いてあります。** 中に置くと、
    // 種類ごとの分岐（MIDIは塊を描く／オーディオはクリップを描く）のどちらかに
    // 入ってしまい、**片方の行にしか出ません**。
    //
    // **ルーラーの下で切ること。** 上へスクロールしていると行のY座標は
    // ルーラーより上に来るので、切らないと目盛りの上に紫の帯が乗ります
    if (hasTimeRange && timeRangeAllTracks)
    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (0, rulerHeight, getWidth(), juce::jmax (0, getHeight() - rulerHeight));

        const int startX = timeToX (timeRangeStart);
        const int endX = timeToX (timeRangeEnd);
        const int previewStartX = timeToX (timeRangeDragPreviewStart);
        const int previewEndX = timeToX (timeRangeDragPreviewStart + (timeRangeEnd - timeRangeStart));

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            const int rowY = getTrackRowY (t);
            const int rowHeight = getTrackAreaHeight (t);

            if (rowHeight <= 0 || rowY > getHeight() || rowY + rowHeight < rulerHeight)
                continue;   // 畳んだフォルダと、画面の外

            juce::Rectangle<int> bounds (startX, rowY + 4,
                                          juce::jmax (2, endX - startX), rowHeight - 8);

            g.setColour (AppColours::purple.withAlpha (0.22f));
            g.fillRect (bounds);
            g.setColour (AppColours::purple);
            g.drawRect (bounds, 2);

            // 掴んで動かしている最中の行き先。**横だけ**（縦には動かない）
            if (draggingTimeRange)
            {
                juce::Rectangle<int> preview (previewStartX, rowY + 4,
                                               juce::jmax (2, previewEndX - previewStartX), rowHeight - 8);

                g.setColour (AppColours::orange.withAlpha (0.22f));
                g.fillRect (preview);
                g.setColour (AppColours::orange);
                g.drawRect (preview, 2);
            }
        }
    }

    // 仕様書4.4：ドラッグ中の落とし先を示す（Phase 21）。
    // クリップより後・ルーラーより前に描いて、行全体を縁取る。
    if (juce::isPositiveAndBelow (dragOverRowIndex, project.getNumTracks()))
    {
        auto rowBounds = juce::Rectangle<int> (0, getTrackRowY (dragOverRowIndex),
                                                getWidth() - scrollBarThickness, getTrackAreaHeight (dragOverRowIndex));

        g.setColour (AppColours::purple.withAlpha (0.18f));
        g.fillRect (rowBounds);
        g.setColour (AppColours::purple);
        g.drawRect (rowBounds, 2);
    }

    // 8.123：**空いている場所も落とし先**（Phase 158／改善案7）。
    // 行と同じ縁取りだと「どの行に入るのか」と読めてしまうので、
    // **最後の行の下に1行ぶんの枠**を描いて、そこへ増えることを示す
    if (dragOverEmptyArea)
    {
        const int y = (project.getNumTracks() > 0)
                         ? getTrackRowY (project.getNumTracks() - 1)
                             + getRowHeight (project.getNumTracks() - 1)
                         : rulerHeight;

        auto newRowBounds = juce::Rectangle<int> (0, y, getWidth() - scrollBarThickness, trackRowHeight);

        g.setColour (AppColours::purple.withAlpha (0.12f));
        g.fillRect (newRowBounds);
        g.setColour (AppColours::purple);
        g.drawRect (newRowBounds, 2);

        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("音源ならトラックごと作ります"), newRowBounds.reduced (10, 0),
                     juce::Justification::centredLeft, false);
    }

    // Phase 36：トラックの並べ替え中の予告。
    // 掴んでいる行を薄く伏せ、**入る位置に線を引く**。
    // Phase 34では行そのものを動かしていたが、指の下から逃げるうえ、
    // どこへ入るのかが動いた後にしか分からなかった。
    if (dragMode == DragMode::ReorderTrack
         && juce::isPositiveAndBelow (reorderSourceIndex, project.getNumTracks())
         && reorderTargetSlot >= 0)
    {
        const int rowWidth = getWidth() - scrollBarThickness;

        // 掴んでいる行（「これが動く」ことを示す）
        auto sourceBounds = juce::Rectangle<int> (0, getTrackRowY (reorderSourceIndex),
                                                   rowWidth, getRowHeight (reorderSourceIndex));
        g.setColour (AppColours::purple.withAlpha (0.12f));
        g.fillRect (sourceBounds);

        // 8.51：**入れ先のフォルダを縁取る**（Phase 90／D2）。
        // 線だけだと「その行の下に入る」ことは分かっても、
        // **どのフォルダの中に入るのか**が分かりません
        if (reorderTargetFolderId.isNotEmpty())
        {
            for (int t = 0; t < project.getNumTracks(); ++t)
            {
                if (project.getTrack (t).getId() != reorderTargetFolderId)
                    continue;

                auto folderBounds = juce::Rectangle<int> (0, getTrackRowY (t),
                                                           rowWidth, getRowHeight (t));

                g.setColour (AppColours::purple.withAlpha (0.22f));
                g.fillRect (folderBounds);
                g.setColour (AppColours::purple);
                g.drawRect (folderBounds, 2);
                break;
            }
        }

        // 8.70：**予告線と写しは`paintOverChildren()`へ移しました**（Phase 109）。
        // ヘッダーのフェーダーとメーターは本物の子なので、ここに描くと下に隠れます
    }

    // 仕様書5.2.3：固定表示のコードトラック（Phase 60／8.20）。
    // **ルーラーと同じ理由でここに描く**：縦スクロールで上がってきた行に
    // 隠されないよう、行より後に描く必要がある。
    drawPinnedChordRow (g);

    // 仕様書5.9：時間目盛り（Phase 18）。トラック行より後に描いて、
    // 縦スクロールで上がってきた行に隠されないようにする。
    drawRuler (g);

    // 仕様書5.9：ループ範囲（Phase 48）。ルーラーの上に重ねる
    drawLoopRange (g);

    // 仕様書5.9：マーカー（Phase 49）。ループの帯の下に並べる
    drawMarkers (g);

    // 8.103：拍子とテンポのレーン（Phase 142／改善案㉒㉓）。
    // **ルーラーの後**——小節番号の下の帯に、変化点の札を重ねる
    drawSignatureLanes (g);

    // 仕様書6.2：範囲選択の矩形（Phase 51）。**いちばん上に描く**
    if (rangeSelecting && ! rangeSelectBounds.isEmpty())
    {
        g.setColour (AppColours::purple.withAlpha (0.15f));
        g.fillRect (rangeSelectBounds);
        g.setColour (AppColours::purple);
        g.drawRect (rangeSelectBounds, 1);
    }

    // 8.57：レーンの点の範囲選択（Phase 95／D14）。**クリップの範囲選択と同じ見た目**にする
    if (automationRangeSelecting && ! automationRangeBounds.isEmpty())
    {
        g.setColour (AppColours::orange.withAlpha (0.15f));
        g.fillRect (automationRangeBounds);
        g.setColour (AppColours::orange);
        g.drawRect (automationRangeBounds, 1);
    }
}

void TimelineComponent::captureReorderDragImage (int rowIndex, juce::Point<int> mousePosition)
{
    // 8.70：掴んだヘッダーの写しを撮る（Phase 109）
    reorderDragImage = {};

    if (! juce::isPositiveAndBelow (rowIndex, project.getNumTracks()))
        return;

    const int rowY = getTrackRowY (rowIndex);

    // **オートメーションの行は含めません**（`getRowHeight()`ではなく`getTrackAreaHeight()`）。
    // 一緒に動くのは確かですが、写しが縦に長くなるほど「何を掴んでいるか」は
    // かえって読みにくくなります。掴んだのはヘッダーそのもの、という見え方にする
    auto grabArea = juce::Rectangle<int> (0, rowY, trackHeaderWidth, getTrackAreaHeight (rowIndex));

    if (grabArea.isEmpty())
        return;

    // `createComponentSnapshot()`は**子まで含めて**描き直します。
    // フェーダーもメーターも本物の子なので、これで写しにも入ります
    reorderDragImage = createComponentSnapshot (grabArea);

    reorderDragGrabOffset = mousePosition - grabArea.getPosition();
    reorderDragPosition = mousePosition;
}

juce::Rectangle<int> TimelineComponent::getReorderDragImageBounds() const
{
    if (! reorderDragImage.isValid())
        return {};

    // **横にも付いて来ます。** 横の位置はフォルダの深さを決めているので（8.51）、
    // 付いて来ないと「右へ寄せると入れ子になる」ことが伝わりません。
    // ヘッダーの外へは出しません（タイムライン側へ流れていくと、何を指しているのか読めない）
    const int imageX = juce::jlimit (0, trackHeaderWidth - reorderDragImage.getWidth() / 2,
                                      reorderDragPosition.x - reorderDragGrabOffset.x);

    return { imageX, reorderDragPosition.y - reorderDragGrabOffset.y,
             reorderDragImage.getWidth(), reorderDragImage.getHeight() };
}

void TimelineComponent::paintOverChildren (juce::Graphics& g)
{
    // 8.70：並べ替えの予告線と、掴んだヘッダーの写し（Phase 109）
    if (dragMode != DragMode::ReorderTrack
         || ! juce::isPositiveAndBelow (reorderSourceIndex, project.getNumTracks())
         || reorderTargetSlot < 0)
        return;

    const int rowWidth = getWidth() - scrollBarThickness;

    // **ルーラーと固定行には食い込ませない。** ここは子より後に描くので、
    // 何もしないとルーラーの上まで写しが乗ります
    // （Phase 60：固定行の上へは入れられないので、そもそも出す意味も無い）
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion ({ 0, getScrollableTop(), rowWidth, juce::jmax (0, getHeight() - getScrollableTop()) });

    // 挿入位置の線。行と行のあいだなので、slotの位置がそのままY座標になる
    const int lineY = getTrackRowY (reorderTargetSlot);

    // 8.70：**オレンジ**にしました（Phase 109）。
    // Consoleの並べ替え（8.67）・ラックのスロットの並べ替え（8.66）と同じ色です。
    // **同じ「ここへ入る」という予告が、画面ごとに違う色**なのは覚えることが増えるだけでした
    // （設計書2.6の「操作中はオレンジ」にも寄せています）
    g.setColour (AppColours::orange);
    g.fillRect (0, lineY - 1, rowWidth, 3);

    // 左端に印を付けて、細い線でも「ここに入る」ことが分かるようにする
    g.fillRect (0, lineY - 4, 4, 9);

    // 掴んだヘッダーの写し（Consoleのドラッグと同じ手応えにする）
    const auto imageBounds = getReorderDragImageBounds();

    if (imageBounds.isEmpty())
        return;

    g.setOpacity (0.75f);
    g.drawImageAt (reorderDragImage, imageBounds.getX(), imageBounds.getY());
    g.setOpacity (1.0f);

    // 縁取りで「浮いているもの」だと分かるようにする（地と同じ色の行が続くため）
    g.setColour (AppColours::orange);
    g.drawRect (imageBounds, 1);
}
