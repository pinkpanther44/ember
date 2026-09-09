#include "PianoRollComponent.h"
#include "AppColours.h"
#include "GrooveQuantise.h" // 仕様書5.3.4：グルーヴの計算部分
#include "EditClipboard.h"   // 仕様書6.2：カット／コピー／貼り付け（Phase 71）
#include "NameEntry.h"       // 名前を入れるダイアログ（Phase 72で1つにまとめた）
#include "Utf8.h"
#include <cmath>
#include <limits>

PianoRollComponent::PianoRollComponent (ProjectModel& projectToUse)
    : project (projectToUse)
{
    setWantsKeyboardFocus (true);

    // 仕様書5.3.1：コードトラックの変化に追従するための購読（Phase 46）
    updateChordSubscription();
}

PianoRollComponent::~PianoRollComponent()
{
    // 破棄後に通知が届かないよう、購読を外してから壊れる
    editedTrack.state.removeListener (this);   // Phase 127：購読はトラック1本
    drumMap.state.removeListener (this); // 仕様書5.3.2：ドラムマップも購読している（Phase 25）
    subscribedProjectState.removeListener (&chordWatcher); // 仕様書5.3.1（Phase 46）
}

void PianoRollComponent::setTrack (const Track& trackToEdit)
{
    // **同じトラックで呼び直されたか**を先に見る。`setTrack()`はモデルを読み直すたびに
    // 呼ばれる（`refreshFromModel()`／Undo／パネルを開いたとき）ので、
    // 見ずに選択やスクロールを捨てると、Undoのたびに選択が外れて左端へ飛ぶ
    if (trackToEdit.state == editedTrack.state)
        return;

    // **購読はトラック1本で足りる**（Phase 127）。ValueTreeの通知は親へ上がるので、
    // その下のノート・CCの変化はここへ全部届く。
    // 付け替えは必ず「外してから足す」こと（外し忘れると、もう編集していない
    // トラックの変更でも描き直してしまう）
    if (editedTrack.state.isValid())
        editedTrack.state.removeListener (this);

    editedTrack = trackToEdit;

    if (editedTrack.state.isValid())
        editedTrack.state.addListener (this);

    // トラックが変われば、そこに載っていたノートの選択は意味を持たない
    selectedNotes.clear();
    selectedLanePoints.clear();
    activeNoteState = juce::ValueTree();

    rangeSelecting = false;
    keyboardSelecting = false;   // Phase 69
    pencilPendingAdd = false;    // Phase 70
    lanePendingAdd = false;      // Phase 78
    laneRangeSelecting = false;
    lanePaintStarted = false;
    dragStartedFromPencil = false;
    dragIsCopy = false;
    dragMode = DragMode::None;
    draggedCCEvent = CCEvent (juce::ValueTree());
    draggedAutomationPoint = -1;

    // 仕様書5.3.1：プロジェクトを読み込むとルートが差し替わる。`setTrack()`は
    // その後で必ず呼ばれる（`PianoRollView::refreshAfterProjectChanged`）ので、ここで張り直す
    updateChordSubscription();

    // 行数が変わると必要な高さも変わる（Phase 23）
    updateSizeForLanes();

    // 8.91：**見えている範囲に1つもノートが無いときだけ、最初のノートへ寄せる**（Phase 131）。
    //
    // Phase 130までは「開いたクリップの先頭へ寄せる」でした。クリップが無くなり、
    // ピアノロールが曲を通しで見せるようになったので、**むやみに跳ばさない**ほうが
    // 読んでいる場所を見失いません。ただし「開いたのに真っ白」も困るので、
    // **何も映っていないときだけ**寄せます。
    if (! hasNoteInVisibleRange())
    {
        const auto blocks = getNoteBlocks();

        if (! blocks.empty())
            setScrollStartSeconds (juce::jmax (0.0, blocks.front().startTime));
    }

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

std::vector<Track::NoteBlock> PianoRollComponent::getNoteBlocks() const
{
    // 塊の切れ目は**1小節ぶんの空き**（8.91で決めた既定値）。
    // **決めるのはProjectModelの1箇所**（8.98／Phase 138）——別々に持つと、
    // 同じ曲がアレンジとピアノロールで違う塊に見える（8.2）
    return editedTrack.getNoteBlocks (project.getNoteBlockGapSeconds());
}

bool PianoRollComponent::hasNoteInVisibleRange() const
{
    const double visible = getVisibleSeconds();

    if (visible <= 0.0)
        return true;   // まだ大きさが決まっていない。ここで寄せると当てにならない

    const double from = scrollStartSeconds;
    const double to = scrollStartSeconds + visible;

    for (int n = 0; n < editedTrack.getNumNotes(); ++n)
    {
        auto note = editedTrack.getNote (n);

        if (note.getStartTime() < to && note.getStartTime() + note.getLength() > from)
            return true;
    }

    return false;
}

//==============================================================================
// 編集中のクリップの変更に追従する（Phase 22a）
//==============================================================================

void PianoRollComponent::notifyViewChanged()
{
    // 8.88：**描き直すだけでは足りない**（Phase 128）。
    //
    // 横スクロールバーとルーラーは**ビュー側**にあり、`onViewChanged`でしか
    // 付け替わりません（8.28で「通知は1本にまとめる」と決めた口）。
    // Phase 127までここを呼んでいたのは**自分の操作の経路だけ**だったので、
    // **アレンジ画面でクリップを動かす／伸ばすと、スクロールできる範囲が古いまま**でした
    // ——長いクリップを足してもスクロールバーが出ない、の正体。
    //
    // トラックを購読するようになった（8.87）ので、外からの変更もここへ届きます。
    //
    // 8.89：**知らせる前に、スクロール量を今の範囲へ入れ直すこと**（Phase 129）。
    // クリップが短くなった／消えたときに、**見える範囲より先を指したまま**になると、
    // スクロールバーは端を指しているのに画面はもっと先を出す、という食い違いになる。
    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - getVisibleSeconds());   // 8.162（Phase 200）
    scrollStartSeconds = juce::jlimit (0.0, maxStart, scrollStartSeconds);

    if (onViewChanged != nullptr)
        onViewChanged();
}

void PianoRollComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    notifyViewChanged();

    // アレンジ画面でのトリム（オフセット・長さの変更）もここへ届く。
    // 伏せ表示の範囲が変わるので描き直す。
    //
    // **Phase 75でレーンの本数による高さの変化は無くなりました**（レーンは1本だけ）。
    // それまでは`ccLaneVisible`を見て`updateSizeForLanes()`を呼んでいました
    repaint();
}

void PianoRollComponent::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    notifyViewChanged();   // クリップが増えると、見渡せる範囲も変わる（Phase 128）
    repaint();
}

void PianoRollComponent::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    notifyViewChanged();

    if (child.hasType (IDs::CCLANE))
    {
        // 掴んでいた点が消えた場合に、無効な参照を持ったままにしない
        draggedCCEvent = CCEvent (juce::ValueTree());
        dragMode = DragMode::None;
        repaint();
        return;
    }

    // 消えたノートを掴んだままにしない。
    //
    // **Phase 127：消えたのが「掴んでいるノートそのもの」のときだけ外す。**
    // 購読がトラック1本になったので、ここへは**このトラックの下で起きた全部**が
    // 届く（別のクリップのノート、インサート、オートメーションの点……）。
    // 無条件に外すと、**ドラッグの最中に別の変更が来ただけで掴んでいたものが外れる**
    if (child == activeNoteState)
        activeNoteState = juce::ValueTree();

    repaint();
}

void PianoRollComponent::valueTreeParentChanged (juce::ValueTree& tree)
{
    // トラックごと消えた／別の場所へ移されたとき（Phase 127）。
    // 親を失ったツリーを編集し続けると、打ち込んでも音にも表示にも反映されない
    if (tree == editedTrack.state && ! editedTrack.state.getParent().isValid())
    {
        selectedNotes.clear();
        activeNoteState = juce::ValueTree();
        dragMode = DragMode::None;
    }

    repaint();
}

//==============================================================================
// 仕様書5.3.2：ドラムエディター（Phase 25）
//==============================================================================

void PianoRollComponent::setDrumMode (bool shouldUseDrumEditor, const DrumMap& mapToUse)
{
    // マップが無いとどの行に何を置くか決まらないので、ドラムモードにはできない
    const bool canUseDrumMode = mapToUse.state.isValid() && mapToUse.getNumEntries() > 0;

    // ドラムマップはクリップではなくプロジェクト直下にあるため、クリップの購読では
    // 変化に気づけない。**行ミュートやチョークをUndoしたときにも表示を合わせる**ために、
    // マップ側も購読する（HANDOVER 1.19と同じ考え方）。付け替え忘れに注意。
    if (drumMap.state.isValid())
        drumMap.state.removeListener (this);

    drumMap = mapToUse;
    drumMode = shouldUseDrumEditor && canUseDrumMode;

    if (drumMap.state.isValid())
        drumMap.state.addListener (this);

    // 行の高さも並ぶ本数も変わるので、必要な高さを取り直す（1.21）
    activeNoteState = juce::ValueTree();
    dragMode = DragMode::None;
    updateSizeForLanes();
}

int PianoRollComponent::getRowHeight() const
{
    return drumMode ? drumRowHeight : noteRowHeight;
}

int PianoRollComponent::getNoteAreaHeight() const
{
    if (drumMode)
        return drumMap.getNumEntries() * drumRowHeight;
    return (highestPitch - lowestPitch + 1) * noteRowHeight;
}

int PianoRollComponent::getDrumRowForPitch (int pitch) const
{
    for (int i = 0; i < drumMap.getNumEntries(); ++i)
        if (drumMap.getEntry (i).getMidiNote() == pitch)
            return i;

    return -1;
}

// 8.161：**行の並びを裏返すのはここだけ**（Phase 199）。
//
// ドラムマップは音程順（行0＝Acoustic Bass Drum、最後＝Low Wood Block）に
// 並んでいて、Phase 25からその順に**上から**描いていました。
// ピアノロールは上へ行くほど高音なので、**同じ画面で向きが逆**になっていて、
// 表示を切り替えるたびに読み替えが要りました。
//
// **並び自体は裏返しません。** マップの順番はミュートグループや保存済みの
// プロジェクトに関わるので、触るのは**画面の向きだけ**にします。
int PianoRollComponent::getYForDrumRow (int row) const
{
    return (drumMap.getNumEntries() - 1 - row) * drumRowHeight;
}

int PianoRollComponent::getDrumRowAtY (int y) const
{
    const int numEntries = drumMap.getNumEntries();

    if (numEntries == 0)
        return -1;

    // 上から数えた行を求めてから裏返す。**先に丸めること**：
    // 裏返してから丸めると、境目が半行ぶんずれる
    const int fromTop = juce::jlimit (0, numEntries - 1, y / drumRowHeight);

    return numEntries - 1 - fromTop;
}

int PianoRollComponent::countNotesOutsideDrumMap() const
{
    if (! drumMode || ! hasTrack())
        return 0;

    int count = 0;

    forEachNote ([this, &count] (const juce::ValueTree& noteState)
    {
        if (getDrumRowForPitch (Note (noteState).getPitch()) < 0)
            ++count;
    });

    return count;
}

int PianoRollComponent::yToPitch (int y) const
{
    if (drumMode)
    {
        // 8.161：**下ほど低い音**（Phase 199）。裏返しは`getDrumRowAtY()`が持っている
        const int row = getDrumRowAtY (y);

        if (row < 0)
            return lowestPitch;

        return drumMap.getEntry (row).getMidiNote();
    }

    // 上へ行くほど高音になるよう反転させる
    const int row = y / noteRowHeight;
    return juce::jlimit (lowestPitch, highestPitch, highestPitch - row);
}

int PianoRollComponent::pitchToY (int pitch) const
{
    if (drumMode)
    {
        const int row = getDrumRowForPitch (pitch);

        // マップに無い音は行を持たない。呼び出し側が描画・判定から外せるよう、
        // 画面の外を指す値を返す（負の値にすると上端付近と紛らわしいため下へ逃がす）
        if (row < 0)
            return getNoteAreaHeight() + drumRowHeight;

        return getYForDrumRow (row);
    }

    return (highestPitch - pitch) * noteRowHeight;
}

//==============================================================================
// 仕様書5.9：座標変換・ズーム・横スクロール（Phase 67／8.1のG3）
//
// **X座標は「タイムライン上の時刻」で、スクロール量を差し引いたもの**（Phase 126）。
// アレンジ画面（`TimelineComponent::timeToX`）と同じ式にしてある。
// ここ以外で `時刻 * pixelsPerSecond` と書かないこと（スクロール量の加味漏れが起きる）。
//
// **Phase 125までは「クリップの中身の時刻」が基準だった。** クリップをまたいで
// 1枚に見せるには（8.86）、画面の横軸が曲全体で1本でなければならない。
// 中身の時刻はノートとCCを読み書きするときだけ使い、換算は
// `clipContentToTimeline()`／`clipTimelineToContent()`の対を通す。
//==============================================================================

double PianoRollComponent::xToTimelineTime (int x) const
{
    return juce::jmax (0.0, scrollStartSeconds + (double) (x - keyboardWidth) / pixelsPerSecond);
}

int PianoRollComponent::timelineTimeToX (double timelineSeconds) const
{
    return keyboardWidth + (int) ((timelineSeconds - scrollStartSeconds) * pixelsPerSecond);
}

//==============================================================================
// 8.91：MIDIはトラックが直接持つ（Phase 131）
//
// **Phase 126〜130でここにあった仕掛けは、まるごと消えました。**
// クリップの窓・オフセット・持ち主の付け替え・伸ばし直し——どれも
// 「1つのノートが3通りの時刻で表される」ことへの後始末でした（1.14）。
// **時刻が1つしか無ければ、どれも要りません。**
//
//   getEditableClips() / getClipAtTimelineTime() / getOrCreateClipAtTimelineTime()
//   clipContentToTimeline() / clipTimelineToContent() / getClipForClipEvent()
//   growClipForNote() / countNotesOutsideClipWindow()
//   drawClipBoundaries() / fillOutsideClips() / getClipXRanges()
//   replaceInSelection()
//
// **アレンジ画面に出る四角は`Track::getNoteBlocks()`で計算します**（データではない）。
// ピアノロールは塊の区別すら描きません——通しの1枚として見せるのが約束です。
//==============================================================================

void PianoRollComponent::forEachNote (const std::function<void (const juce::ValueTree&)>& fn) const
{
    if (fn == nullptr)
        return;

    for (int n = 0; n < editedTrack.getNumNotes(); ++n)
        fn (editedTrack.getNote (n).state);
}

double PianoRollComponent::getNoteTimelineStart (const juce::ValueTree& noteState)
{
    // Phase 131から**そのまま曲の時刻**。名前は残してある——呼び出し側が
    // 「これはタイムライン基準だ」と読めるほうが、`getStartTime()`より安全
    return Note (noteState).getStartTime();
}

void PianoRollComponent::setNoteTimelineStart (const juce::ValueTree& noteState, double timelineSeconds,
                                                juce::UndoManager* undoManager)
{
    Note note { juce::ValueTree (noteState) };
    note.setStartTime (juce::jmax (0.0, timelineSeconds), undoManager);
}

double PianoRollComponent::getVisibleSeconds() const
{
    const int timelineWidth = juce::jmax (0, getWidth() - keyboardWidth);

    return pixelsPerSecond > 0.0 ? (double) timelineWidth / pixelsPerSecond : 0.0;
}

double PianoRollComponent::getTimelineLengthSeconds() const
{
    // 最低でも8小節ぶんは横に動かせるようにしておく。
    // **「この先に何も無い」ことが見える**ほうが、打ち込みの続きを足しやすい
    // （最後のノートでスクロールが止まると、その先に置けないように見える）。
    // **8小節先は`getBarStartTime(8)`で訊く**（8.98／Phase 138）——
    // 「1小節の長さ×8」と書くと、小節ごとに長さが違う形にしたときに合わなくなる
    double lengthSeconds = project.getBarStartTime (8);

    // 8.91：**いちばん後ろのノートの2小節先まで**（Phase 131）。
    // クリップの長さという概念が無くなったので、端はノートが決める
    forEachNote ([&] (const juce::ValueTree& noteState)
    {
        Note note { juce::ValueTree (noteState) };
        const double noteEnd = note.getStartTime() + note.getLength();

        lengthSeconds = juce::jmax (lengthSeconds,
                                     noteEnd + project.getBarSecondsAt (noteEnd) * 2.0);
    });

    return lengthSeconds;
}

double PianoRollComponent::getScrollableLengthSeconds() const
{
    // 8.162：**右はどこまでも行ける**（Phase 200／本人の要望）。
    //
    // 本人の言葉は「画面を右にスクロールすると制限がありスクロールができなくなる。
    // どこまでも右にスクロールできるようにできる？」。
    //
    // Phase 199まで、上限は**中身の長さ**でした。打ち込み終わりの少し先で止まるので、
    // 「この先に置きたい」と思って動かした手が壁に当たります。
    //
    // **いまの位置から1画面ぶん先までは必ず行ける**ようにします。
    // 1回のスクロールで進めるのは1画面ぶんまでですが、
    // 進むたびに先も伸びるので、**押し続けるかぎり止まりません。**
    //
    // **行った先を覚えないこと。** 覚えると、戻ってきてもスクロールバーの
    // つまみが小さいままになり、「どこにいるか」が読めなくなります
    return juce::jmax (getTimelineLengthSeconds(), scrollStartSeconds + getVisibleSeconds() * 2.0);
}

void PianoRollComponent::setScrollStartSeconds (double newStartSeconds)
{
    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - getVisibleSeconds());
    const double limited = juce::jlimit (0.0, maxStart, newStartSeconds);

    if (limited == scrollStartSeconds)
        return;

    scrollStartSeconds = limited;

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void PianoRollComponent::setZoom (double newPixelsPerSecond, int anchorX)
{
    const double limited = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, newPixelsPerSecond);

    if (limited == pixelsPerSecond)
        return;

    // 拡大縮小の軸になる位置の時刻を先に覚えておき、倍率を変えた後も同じ位置に来るよう
    // スクロール量を調整する。これが無いと、拡大するたびに見ていた場所が画面外へ逃げる
    // （`TimelineComponent::setZoom`と同じ）。
    const double anchorTime = xToTimelineTime (anchorX);
    const int anchorOffsetPixels = anchorX - keyboardWidth;

    pixelsPerSecond = limited;

    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - getVisibleSeconds());   // 8.162（Phase 200）
    scrollStartSeconds = juce::jlimit (0.0, maxStart, anchorTime - anchorOffsetPixels / pixelsPerSecond);

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void PianoRollComponent::zoomIn()
{
    zoomIn (getWidth() / 2);
}

void PianoRollComponent::zoomOut()
{
    zoomOut (getWidth() / 2);
}

// 8.123：軸を指定する版（Phase 158／改善案20）。**刻みはここにしかない**
void PianoRollComponent::zoomIn (int anchorX)
{
    setZoom (pixelsPerSecond * zoomStepFactor, anchorX);
}

void PianoRollComponent::zoomOut (int anchorX)
{
    setZoom (pixelsPerSecond / zoomStepFactor, anchorX);
}

void PianoRollComponent::zoomToFit()
{
    const int timelineWidth = juce::jmax (0, getWidth() - keyboardWidth);

    // **Phase 126：合わせるのは「クリップの範囲」**（曲の先頭からではない）。
    // 横軸が曲の時刻になったので、曲の頭から合わせると、後ろのほうにある
    // 短いクリップを開いたときに、点のように潰れて見えなくなる。
    //
    // **8.91：合わせるのはトラックのノート全体**（最初の音から最後の音まで。Phase 131）。
    // ノートが1つも無いときだけ、曲の頭から見渡せる倍率にする。
    double fitStart = 0.0;
    double fitEnd = 0.0;
    bool haveAny = false;

    for (int n = 0; n < editedTrack.getNumNotes(); ++n)
    {
        auto note = editedTrack.getNote (n);
        const double start = juce::jmax (0.0, note.getStartTime());
        const double end = start + juce::jmax (0.0, note.getLength());

        fitStart = haveAny ? juce::jmin (fitStart, start) : start;
        fitEnd = haveAny ? juce::jmax (fitEnd, end) : end;
        haveAny = true;
    }

    const double fitSeconds = haveAny ? juce::jmax (0.1, fitEnd - fitStart)
                                      : getTimelineLengthSeconds();

    if (! haveAny)
        fitStart = 0.0;

    if (timelineWidth <= 0 || fitSeconds <= 0.0)
        return;

    pixelsPerSecond = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond,
                                     (double) timelineWidth / fitSeconds);
    scrollStartSeconds = fitStart;

    if (onViewChanged != nullptr)
        onViewChanged();

    repaint();
}

void PianoRollComponent::resized()
{
    // 幅が変わるとスクロールの上限も変わる。**はみ出したままにしないこと**
    // （広げたときに、右端から先の何も無い場所が出たままになる）
    const double maxStart = juce::jmax (0.0, getScrollableLengthSeconds() - getVisibleSeconds());   // 8.162（Phase 200）
    scrollStartSeconds = juce::jlimit (0.0, maxStart, scrollStartSeconds);

    if (onViewChanged != nullptr)
        onViewChanged();
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Ctrl（Macではcommand）＋ホイールで拡大縮小。アレンジ画面と同じ操作
    if (e.mods.isCommandDown())
    {
        const double factor = std::pow (zoomStepFactor, wheel.deltaY > 0.0f ? 1.0 : -1.0);
        setZoom (pixelsPerSecond * factor, e.x);
        return;
    }

    // Shift＋ホイール、またはホイールの横方向成分で横スクロール
    if (e.mods.isShiftDown() || wheel.deltaX != 0.0f)
    {
        const float delta = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
        setScrollStartSeconds (scrollStartSeconds - delta * getVisibleSeconds() * 0.25);
        return;
    }

    // 縦は親のViewportの仕事。**基底へ返す**（握ると上下にスクロールできなくなる）
    Component::mouseWheelMove (e, wheel);
}

//==============================================================================
// 仕様書5.5・5.9：スナップ（Phase 67）
//
// **Phase 126で`snapContentTime()`を廃止した。** あれは「タイムラインへ直して
// 寄せて戻す」ためのもので、基準がタイムラインになった時点で`project.snapTime()`
// そのものになる。中身の時刻を寄せたいときは、寄せてから
// `clipTimelineToContent()`で直すこと（順番を逆にすると小節線とずれる。8.28）。
//==============================================================================

//==============================================================================
// 仕様書6.2：ツールとノートの複数選択（Phase 52）

void PianoRollComponent::setEditTool (EditTool newTool)
{
    if (editTool == newTool)
        return;

    editTool = newTool;

    // 掴んでいる途中でツールが変わると、離したときに別の意味で処理される
    dragMode = DragMode::None;
    rangeSelecting = false;
    keyboardSelecting = false;       // Phase 69：見出しをなぞっている途中も畳む
    pencilPendingAdd = false;        // Phase 70：置きかけの構えも畳む
    dragStartedFromPencil = false;

    repaint();
}

bool PianoRollComponent::isNoteSelected (const juce::ValueTree& noteState) const
{
    for (const auto& selected : selectedNotes)
        if (selected == noteState)
            return true;

    return false;
}

void PianoRollComponent::toggleNoteSelection (const juce::ValueTree& noteState)
{
    auto found = std::find (selectedNotes.begin(), selectedNotes.end(), noteState);

    if (found != selectedNotes.end())
        selectedNotes.erase (found);
    else
        selectedNotes.push_back (noteState);

    repaint();
}

void PianoRollComponent::setSingleNoteSelection (const juce::ValueTree& noteState)
{
    selectedNotes.clear();

    if (noteState.isValid())
        selectedNotes.push_back (noteState);
}

void PianoRollComponent::pruneNoteSelection()
{
    // 消えたノート（親を失ったValueTree）を選択から外す。
    // 残したままだと、まとめて消すときに何も起きない項目が混ざる
    selectedNotes.erase (std::remove_if (selectedNotes.begin(), selectedNotes.end(),
                                          [] (const juce::ValueTree& note)
                                          {
                                              return ! note.getParent().isValid();
                                          }),
                          selectedNotes.end());
}

void PianoRollComponent::selectAllNotes()
{
    selectedNotes.clear();

    // **Phase 127：トラックの全クリップから選ぶ**（画面に出ているもの全部）
    forEachNote ([this] (const juce::ValueTree& noteState) { selectedNotes.push_back (noteState); });

    repaint();
}

void PianoRollComponent::clearNoteSelection()
{
    selectedNotes.clear();
    activeNoteState = juce::ValueTree();
    repaint();
}

void PianoRollComponent::applyRangeSelection()
{
    selectedNotes.clear();

    forEachNote ([this] (const juce::ValueTree& noteState)
    {
        Note note { juce::ValueTree (noteState) };

        // **グリッドの矩形とベロシティの棒、どちらに触れても選ぶ**（Phase 69）。
        // 2つの領域は重ならないので、両方を見ても取り違えは起きない。
        // 領域ごとに別の関数を用意すると、呼び分けの条件を持ち回ることになる
        if (getNoteBounds (note).intersects (rangeSelectBounds)
             || getVelocityBarBounds (note).intersects (rangeSelectBounds))
            selectedNotes.push_back (noteState);
    });

    repaint();
}

void PianoRollComponent::deleteSelectedNotes()
{
    pruneNoteSelection();

    if (! hasTrack() || selectedNotes.empty())
        return;

    project.beginAction (utf8 ("ノートの削除"));

    // **ValueTreeで覚えているので、消す順番を気にしなくてよい。**
    // 番号で持っていたら、1つ消すたびに残りがずれる（1.32）
    //
    // **Phase 127：消す先はノート自身が知っている**（クリップをまたいで選べるので、
    // `clip`へ向けて消すと「選んだのに消えないノート」ができる）
    for (const auto& noteState : selectedNotes)
        if (noteState.getParent().isValid())
            editedTrack.removeNote (Note (noteState), &project.getUndoManager());

    selectedNotes.clear();
    activeNoteState = juce::ValueTree();

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void PianoRollComponent::showNoteMenu (const juce::ValueTree& noteState, juce::Point<int> screenPosition)
{
    // **Phase 127から、掴むのは番号ではなくValueTree**（1.32）。
    // クリップをまたぐと「何番目か」だけでは相手が決まらない
    if (! noteState.isValid() || ! noteState.getParent().isValid())
        return;

    // 右クリックしたノートが選択に入っていなければ、それだけを選び直す
    // （選んでいないものを消してしまわないように）
    if (! isNoteSelected (noteState))
    {
        setSingleNoteSelection (noteState);
        activeNoteState = noteState;
        repaint();
    }

    pruneNoteSelection();

    const int numSelected = (int) selectedNotes.size();
    const auto countSuffix = numSelected > 1 ? utf8 ("（") + juce::String (numSelected) + utf8 ("個）")
                                              : juce::String();

    juce::PopupMenu menu;
    menu.addItem (2, utf8 ("カット") + countSuffix);        // 8.29の表（Phase 71）
    menu.addItem (3, utf8 ("コピー") + countSuffix);
    menu.addSeparator();
    menu.addItem (1, numSelected > 1 ? utf8 ("選択したノートを削除") + countSuffix
                                      : utf8 ("このノートを削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                   .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this] (int result)
        {
            if (result == 1)
                deleteSelectedNotes();
            else if (result == 2)
                cutSelection();
            else if (result == 3)
                copySelection();
        });
}

void PianoRollComponent::duplicateSelectedNotesByDrag()
{
    pruneNoteSelection();

    if (! hasActiveNote())
        return;

    const bool moved = (dragPreviewStartTime != dragOriginalStartTime)
                    || (dragPreviewPitch != dragOriginalPitch);

    if (! moved)
        return;

    // **掴んだノートの動いた量を、選んでいる全部に同じだけ足す。**
    // 1つずつ「落とした位置」を計算すると、全部が同じ場所に重なる
    const double deltaTime = dragPreviewStartTime - dragOriginalStartTime;
    const int deltaPitch = dragPreviewPitch - dragOriginalPitch;

    // 掴んだノートが選択に入っていなければ、それだけを複製する
    std::vector<juce::ValueTree> sources = selectedNotes;

    if (sources.empty())
        sources.push_back (activeNoteState);

    project.beginAction (utf8 ("ノートの複製"));

    std::vector<juce::ValueTree> copies;

    for (const auto& sourceState : sources)
    {
        if (! sourceState.getParent().isValid())
            continue;

        Note source { juce::ValueTree (sourceState) };

        // 8.91：**そのまま曲の時刻へ置くだけ**（Phase 131）。行き先のクリップを
        // 探す・無ければ作る・伸ばす、が全部要らなくなった
        const double timelineStart = juce::jmax (0.0, source.getStartTime() + deltaTime);

        auto copy = editedTrack.addNote (juce::jlimit (lowestPitch, highestPitch,
                                                        source.getPitch() + deltaPitch),
                                          source.getVelocity(), timelineStart, source.getLength(),
                                          &project.getUndoManager());

        copies.push_back (copy.state);
    }

    // 複製したものを選び直す。**元を選んだままにすると、続けてドラッグしたときに
    // 元が動く**（複製したつもりの手応えと食い違う）
    selectedNotes = copies;
    activeNoteState = copies.empty() ? juce::ValueTree() : copies.back();

    if (onModelChanged != nullptr)
        onModelChanged();
}

void PianoRollComponent::moveOtherSelectedNotesByDrag()
{
    pruneNoteSelection();

    if (! hasActiveNote())
        return;

    const double deltaTime = dragPreviewStartTime - dragOriginalStartTime;
    const int deltaPitch = dragPreviewPitch - dragOriginalPitch;

    if (juce::approximatelyEqual (deltaTime, 0.0) && deltaPitch == 0)
        return;

    const auto draggedState = activeNoteState;

    // **区切り（beginAction）は呼び出し元が既に作っている。** ここで作ると、
    // 掴んだ1つと残りが別々のUndoステップになり、1回のUndoで戻り切らない。
    //
    // 8.91：**時刻と音程を書くだけ**（Phase 131）。持ち主の付け替えも、
    // ValueTreeの作り直しも、選択の差し替えも要らなくなった
    for (const auto& noteState : selectedNotes)
    {
        if (noteState == draggedState || ! noteState.getParent().isValid())
            continue;

        Note note { juce::ValueTree (noteState) };

        note.setStartTime (juce::jmax (0.0, note.getStartTime() + deltaTime), &project.getUndoManager());
        note.setPitch (juce::jlimit (lowestPitch, highestPitch, note.getPitch() + deltaPitch),
                        &project.getUndoManager());
    }
}

void PianoRollComponent::cutNoteAt (const juce::ValueTree& noteState, int x)
{
    if (! noteState.isValid() || ! noteState.getParent().isValid())
        return;

    Note note { juce::ValueTree (noteState) };

    // Phase 54：割る位置もスナップに従う（8.14）。
    // 寄せた結果が端に寄りすぎたときは、下の判定でそのまま「割らない」に落ちる。
    // 8.91：**寄せた値がそのまま曲の時刻**（Phase 131で換算が消えた）
    const double cutTime = juce::jmax (0.0, project.snapTime (xToTimelineTime (x)));
    const double startTime = note.getStartTime();
    const double length = note.getLength();

    // 端ちょうどでは割らない（長さ0のノートができる）
    if (cutTime <= startTime + minNoteLength || cutTime >= startTime + length - minNoteLength)
        return;

    project.beginAction (utf8 ("ノートの分割"));

    note.setLength (cutTime - startTime, &project.getUndoManager());
    editedTrack.addNote (note.getPitch(), note.getVelocity(), cutTime,
                          startTime + length - cutTime, &project.getUndoManager());

    // 割ったことで番号が変わる。中途半端な選択を残さない
    clearNoteSelection();

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void PianoRollComponent::setNoteColouring (NoteColouring newMode)
{
    if (noteColouring == newMode)
        return;

    noteColouring = newMode;
    repaint();
}

void PianoRollComponent::updateChordSubscription()
{
    auto currentState = project.getState();

    if (subscribedProjectState == currentState)
        return;

    subscribedProjectState.removeListener (&chordWatcher);
    subscribedProjectState = currentState;
    subscribedProjectState.addListener (&chordWatcher);
}

void PianoRollComponent::chordDataChanged (const juce::ValueTree& tree, const juce::Identifier& property)
{
    // **ノードの種別から見ること**（1.42）。コード区間の`startTime`は、
    // クリップやノートの`startTime`と同じ識別子なので、名前だけでは絞れない。
    const bool affectsChords =
        (tree.hasType (IDs::CHORDREGION)
            && (property == IDs::chordRoot || property == IDs::chordType
                || property == IDs::chordTensions || property == IDs::chordBass
                || property == IDs::chordRegionStartBeats
                || property == IDs::chordRegionLengthBeats))
        || (tree.hasType (IDs::TRACK)
            && (property == IDs::chordKeyRoot || property == IDs::chordKeyMinor));

    // 8.1のD5：**透かしの入切はグリッドの見た目**（Phase 74）。
    // 一覧の丸を押したときに、こちらも描き直す必要がある
    if (property == IDs::trackWatermark)
    {
        if (! drumMode)
            repaint();

        return;
    }

    // Phase 67：**テンポと拍子はルーラーの目盛りそのもの。** ここで拾わないと、
    // BPMを変えても小節線と小節番号が古いまま残る（1.15と同じ「更新のきっかけ」の話）。
    // **Phase 72でループとマーカーも足した**（8.33）：拾わないと、アレンジ画面で
    // 足したマーカーやループの帯が、ピアノロールのルーラーに出ない。
    // **Phase 145：小節バーのレーンの変化点も拾う**（8.109）。
    // ここを広げ忘れていたので、アレンジ画面でテンポの変化点を足しても
    // **ピアノロールの小節番号・小節線・グリッドだけが古いまま**残っていました。
    // **`<TEMPOCHANGE>`の値の変更だけは偶然拾えていた**（プロパティ名が`tempo`と同じ）
    // ——1.42「ノード種別まで見ること」がまさにこの形です
    const bool affectsRuler = (property == IDs::tempo || property == IDs::timeSignature
                                || property == IDs::loopStartBeats || property == IDs::loopEndBeats
                                || property == IDs::loopEnabled
                                || property == IDs::markerBeats || property == IDs::markerName
                                || ::isSignatureLaneNode (tree));

    if (! affectsChords && ! affectsRuler)
        return;

    // ルーラーとコード帯はビュー側にあるので、**塗りとは別に**知らせる
    // （構成音カラーリングを切っていても、コード帯は出したままになる）。
    if (onViewChanged != nullptr)
        onViewChanged();

    if (noteColouring != NoteColouring::off && ! drumMode && affectsChords)
        repaint();
    else if (affectsRuler)
        repaint();   // 縦のグリッド線もテンポで決まる
}

void PianoRollComponent::chordChildChanged (const juce::ValueTree& child)
{
    // 8.1のD5：**他のトラックのノートの増減にも追従する**（Phase 73／74）。
    // 透かしを出しているトラックが1本でもあるときだけ拾う：
    // いつも拾うと、打ち込むたびに全体を描き直すことになる
    if (! drumMode && (child.hasType (IDs::NOTE) || child.hasType (IDs::MIDICLIP))
         && hasAnyWatermarkTrack())
    {
        repaint();
        return;
    }

    // トラックの増減でも、コード帯と塗りの中身が変わる（コードトラックが増えた／消えた）。
    // マーカーの増減もルーラーの見た目に効く（Phase 72）。
    // **Phase 145：変化点の出し入れも**（8.109）——変化点は「足す・消す」が主な操作なので、
    // プロパティだけ見ていると、足した瞬間に気づけません
    if (! child.hasType (IDs::CHORDREGION) && ! child.hasType (IDs::TRACK)
         && ! child.hasType (IDs::MARKER) && ! ::isSignatureLaneNode (child))
        return;

    if (onViewChanged != nullptr)
        onViewChanged();

    if (noteColouring != NoteColouring::off && ! drumMode)
        repaint();
}

bool PianoRollComponent::hasAnyWatermarkTrack() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi && track.isWatermarkVisible())
            return true;
    }

    return false;
}

void PianoRollComponent::drawNoteWatermark (juce::Graphics& g)
{
    // 8.1のD5／G4：**透かしに指定したMIDIトラックのノートを薄く重ねる**
    // （Phase 73、**Phase 74でトラックごとの指定**に変えた。8.34）。
    //
    // **どのトラックを出すかはモデルが持っています**（`Track::isWatermarkVisible()`）。
    // ここに「全部出す／出さない」のスイッチを置くと、一覧の丸と食い違います。
    //
    // **ドラムモードでは出しません。** 行の決め方がドラムマップの並び順なので、
    // 他のトラックの音を同じ行に置くと、まったく別の楽器の位置に見えます（5.3.2）。
    if (drumMode || ! hasTrack())
        return;

    const auto editedTrackId = editedTrack.getId();   // Phase 127：トラックは自分で持っている

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (keyboardWidth, 0, juce::jmax (0, getWidth() - keyboardWidth), getGridHeight());

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        // **一覧で丸を点けたトラックだけ**（Phase 74）。編集中のトラックは
        // 本体のノートとして描かれるので、透かしからは外す
        if (track.getType() != TrackType::Midi || track.getId() == editedTrackId
             || ! track.isWatermarkVisible())
            continue;

        // **トラックの色で描く。** 全部同じ色にすると、透かしが何本ぶんなのか読めない
        const auto colour = juce::Colour::fromString (track.getColourString());

        // 8.91：**ノートはトラックが直接持つ**（Phase 131）。
        // 窓の判定も換算も要らなくなった——時刻がそのままX座標へ通る
        {
            for (int n = 0; n < track.getNumNotes(); ++n)
            {
                auto note = track.getNote (n);

                const double timelineStart = note.getStartTime();

                const int x = timelineTimeToX (timelineStart);
                const int endX = timelineTimeToX (timelineStart + note.getLength());

                if (endX <= keyboardWidth || x >= getWidth())
                    continue;

                const int y = pitchToY (note.getPitch());

                if (y < 0 || y >= getGridHeight())
                    continue;

                // **枠だけ・薄く。** 塗ると編集中のノートと見分けが付かなくなる
                juce::Rectangle<int> bounds (x, y + 1, juce::jmax (2, endX - x), getRowHeight() - 2);

                g.setColour (colour.withAlpha (watermarkAlpha));
                g.fillRect (bounds);
            }
        }
    }
}

void PianoRollComponent::drawNoteColouring (juce::Graphics& g)
{
    if (noteColouring == NoteColouring::chordTones)
        drawChordToneColouring (g);
    else if (noteColouring == NoteColouring::scaleTones)
        drawScaleToneColouring (g);
}

Scale PianoRollComponent::getKeyForDisplay() const
{
    // 8.106：**画面の左端のキー**（Phase 143）。ヘッダの説明を参照。
    // **色と階名が同じ関数を通ること**が肝で、どの時刻を選ぶかは二の次です
    return project.getProjectKeyAt (scrollStartSeconds);
}

void PianoRollComponent::drawScaleToneColouring (juce::Graphics& g)
{
    const auto key = getKeyForDisplay();

    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        const int pitchClass = pitch % 12;

        // 仕様書5.3.1：キー音（明るめ）／スケール内音（中間）／スケール外音（暗い）の3段階。
        // **スケール外は塗らない**ことで「暗い」を表している。塗り重ねないほうが、
        // 黒鍵の行かどうかも一緒に読めるため。
        if (pitchClass == key.root)
            g.setColour (AppColours::scaleRootTone.withAlpha (scaleRootAlpha));
        else if (key.contains (pitchClass))
            g.setColour (AppColours::scaleTone.withAlpha (scaleToneAlpha));
        else
            continue;

        g.fillRect (keyboardWidth, pitchToY (pitch), getWidth() - keyboardWidth, noteRowHeight);
    }
}

void PianoRollComponent::drawChordToneColouring (juce::Graphics& g)
{
    auto chordTrack = project.findChordTrack();

    if (! chordTrack.state.getParent().isValid())
        return;   // コードトラックが無ければ何も塗らない（スケール音モードとは別物）

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
    {
        auto region = chordTrack.getChordRegion (r);

        const int startX = timelineTimeToX (region.getStartTime());
        const int endX = timelineTimeToX (region.getEndTime());

        // 画面の外にある区間は飛ばす。進行が長いとここが効いてくる
        if (endX <= keyboardWidth || startX >= getWidth())
            continue;

        const int clippedStartX = juce::jmax (keyboardWidth, startX);
        const int clippedEndX = juce::jmin (getWidth(), endX);

        for (const auto& [pitchClass, degree] : region.getChord().getTones())
        {
            g.setColour (AppColours::chordDegreeColour ((int) degree).withAlpha (chordToneAlpha));

            // 同じピッチクラスの行はオクターブぶんすべて塗る
            for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
                if (pitch % 12 == pitchClass)
                    g.fillRect (clippedStartX, pitchToY (pitch),
                                 clippedEndX - clippedStartX, noteRowHeight);
        }
    }
}

juce::Rectangle<int> PianoRollComponent::getNoteBounds (const Note& note) const
{
    // **Phase 126：ノートの時刻は中身基準、画面はタイムライン基準。**
    // 換算はここ（と`getVelocityBarBounds()`）を通すこと
    return getNoteBoundsFor (getNoteTimelineStart (note.state), note.getPitch(), note.getLength());
}

juce::Rectangle<int> PianoRollComponent::getNoteBoundsFor (double timelineStartTime, int pitch,
                                                            double length) const
{
    const int x = timelineTimeToX (timelineStartTime);
    const int y = pitchToY (pitch);
    const int width = juce::jmax (4, (int) (length * pixelsPerSecond));

    return { x, y + 1, width, getRowHeight() - 2 };
}

juce::Colour PianoRollComponent::getTrackColour() const
{
    // **Phase 127：トラックは自分で持っている**（それまではクリップから2つ遡っていた）
    auto trackState = editedTrack.state;

    if (! trackState.hasType (IDs::TRACK))
        return AppColours::purple;

    const auto colourString = trackState[IDs::trackColor].toString();

    return colourString.isEmpty() ? AppColours::purple : juce::Colour::fromString (colourString);
}

void PianoRollComponent::drawDrumNote (juce::Graphics& g, juce::Rectangle<int> bounds,
                                        juce::Colour fillColour, juce::Colour outlineColour,
                                        float outlineThickness) const
{
    if (bounds.isEmpty())
        return;

    // ▶の大きさは**行の高さで決める**。長さで変えると、短いノートだけ
    // 小さな三角になって打点が見えなくなる
    const float size = (float) bounds.getHeight();
    const float left = (float) bounds.getX();
    const float top = (float) bounds.getCentreY() - size * 0.5f;

    // 8.121：**尾は引かない**（Phase 156／改善案30）。
    //
    // Phase 97では「長さの情報を落とさないため」▶の後ろに細い尾を描いていた。
    // ただしドラムは**叩いたら減衰するだけ**の音で、長さは音に出ない
    // （チョークグループで止める場合を除く）。
    // 出ない情報を描くと、**打点の位置**——ドラムで唯一読みたいもの——が
    // 尾に埋もれる。**表示は▶だけ。**
    //
    // **長さそのものは残っている**（モデルにもMIDI書き出しにも）。見せないだけ

    juce::Path head;
    head.addTriangle (left, top, left, top + size, left + size, (float) bounds.getCentreY());

    g.setColour (fillColour);
    g.fillPath (head);
    g.setColour (outlineColour);
    g.strokePath (head, juce::PathStrokeType (outlineThickness));
}

void PianoRollComponent::drawNoteDragPreview (juce::Graphics& g) const
{
    // 位置や長さが変わるドラッグだけ。ベロシティのドラッグは、
    // ノート本体の濃さがその場で変わるので、プレビューを重ねる必要がない。
    if (dragMode != DragMode::Move && dragMode != DragMode::ResizeRight
         && dragMode != DragMode::ResizeLeft)
        return;

    if (! hasActiveNote())
        return;

    const auto draggedState = activeNoteState;

    // 元のノートの上に重ねるので、**半透明の塗り＋はっきりした枠**にする。
    // 塗りだけだと、元と行き先が同じ濃さになって見分けがつかない。
    auto drawPreview = [this, &g] (juce::Rectangle<int> bounds)
    {
        // 8.60：**ドラムでは行き先も▶**（Phase 97／D13）。
        // 本体と形が違うと、掴んだものが別のものに化けたように見える
        if (drumMode)
        {
            drawDrumNote (g, bounds, AppColours::orange.withAlpha (0.35f), AppColours::orange, 2.0f);
            return;
        }

        g.setColour (AppColours::orange.withAlpha (0.35f));
        g.fillRect (bounds);
        g.setColour (AppColours::orange);
        g.drawRect (bounds, 2);
    };

    drawPreview (getNoteBoundsFor (dragPreviewStartTime, dragPreviewPitch, dragPreviewLength));

    if (dragMode != DragMode::Move)
        return; // 長さを変えているのは掴んだ1つだけ

    // Phase 52：選択中のノートは掴んだものと同じ量だけ動く。
    // **ずらす量は掴んだノートの移動量**（1つずつ「落とした位置」で計算すると、
    // 全部が同じ場所に重なる。`moveOtherSelectedNotesByDrag()`と同じ考え方）。
    const double deltaTime = dragPreviewStartTime - dragOriginalStartTime;
    const int deltaPitch = dragPreviewPitch - dragOriginalPitch;

    for (const auto& noteState : selectedNotes)
    {
        if (noteState == draggedState || ! noteState.getParent().isValid())
            continue;

        Note note { juce::ValueTree (noteState) };

        // **Phase 126：`getNoteBoundsFor()`はタイムライン基準**なので、
        // ずらす前にノートの時刻をタイムラインへ直しておく
        drawPreview (getNoteBoundsFor (juce::jmax (0.0, getNoteTimelineStart (noteState) + deltaTime),
                                        juce::jlimit (lowestPitch, highestPitch,
                                                       note.getPitch() + deltaPitch),
                                        note.getLength()));
    }

    drawDragReadout (g);
}

void PianoRollComponent::drawDragReadout (juce::Graphics& g) const
{
    // 8.121：**上下に動かしている最中だけ**（Phase 156／改善案22）。
    // 長さを変えているときは音が変わらないので、出しても読むものが無い
    if (dragMode != DragMode::Move || ! hasActiveNote())
        return;

    // 音名（C4＝60の流儀）とMIDIノート番号。**両方出すこと**：
    // 音名は譜面を書くとき、番号はドラム音源や外部機器を合わせるときに要る
    juce::String text = midiNoteName (dragPreviewPitch)
                          + "  (" + juce::String (dragPreviewPitch) + ")";

    // 仕様書5.3.2：ドラムでは行の名前のほうが手がかりになる（音名では何の楽器か分からない）
    if (drumMode)
        if (auto entry = drumMap.findEntry (dragPreviewPitch); entry.state.isValid())
            text = entry.getPartName() + "  (" + juce::String (dragPreviewPitch) + ")";

    const juce::Font font (juce::FontOptions (12.0f, juce::Font::bold));
    const int width  = juce::GlyphArrangement::getStringWidthInt (font, text) + 16;
    const int height = 22;

    // **カーソルの右上に置く。** 右下だとカーソルの矢印そのものに隠れる。
    // 画面からはみ出す側では反対へ寄せる——**端で読めなくなるのが
    // いちばん困る**（端は音域の上下限で、まさに確かめたい場所）
    int x = dragReadoutPosition.x + 14;
    int y = dragReadoutPosition.y - height - 8;

    if (x + width > getWidth())
        x = dragReadoutPosition.x - width - 14;

    if (y < 0)
        y = dragReadoutPosition.y + 16;

    const auto box = juce::Rectangle<int> (x, juce::jmax (0, y), width, height);

    // 下のノートが透けると読めないので、**地は塗り切る**
    g.setColour (AppColours::panel);
    g.fillRoundedRectangle (box.toFloat(), AppColours::corner (3.0f));
    g.setColour (AppColours::orange);
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), AppColours::corner (3.0f), 1.0f);

    g.setColour (AppColours::textPrimary);
    g.setFont (font);
    g.drawText (text, box, juce::Justification::centred, false);
}

juce::ValueTree PianoRollComponent::findNoteAt (juce::Point<int> position) const
{
    // **Phase 127：クリップ1つではなく、トラックの全クリップを見る**（8.87）。
    // 見えているのに触れないノートを作らないため
    juce::ValueTree found;

    forEachNote ([this, position, &found] (const juce::ValueTree& noteState)
    {
        if (! found.isValid() && getNoteBounds (Note (noteState)).contains (position))
            found = noteState;
    });

    return found;
}

void PianoRollComponent::setVisibleVerticalRange (int newScrollOffsetY, int newVisibleHeight)
{
    if (visibleScrollOffsetY == newScrollOffsetY && visibleHeight == newVisibleHeight)
        return;

    visibleScrollOffsetY = newScrollOffsetY;
    visibleHeight = newVisibleHeight;

    // レーンの位置が変わる＝描き直しが要る（固定行なので、画面の下端に貼り付く）
    repaint();
}

juce::Rectangle<int> PianoRollComponent::getLaneBounds() const
{
    // 8.1のG5：**レーンは見えている範囲の下端に固定**（Phase 76／8.36）。
    //
    // Phase 75まではコンポーネントの下端（`getHeight() - laneHeight`）に置いていたので、
    // 縦にスクロールすると一緒に流れて消えていました。アレンジ画面のコードトラックと
    // 同じ「固定行」の形にしてあります（8.20）：**中身はレーンの下へ潜り**、
    // レーンは常に同じ場所に出ます。
    //
    // ビューから見えている範囲を教わらないと位置が決まらないので、
    // まだ来ていない（0）ときはコンポーネントの下端へ置きます。
    const int height = (visibleHeight > 0) ? visibleHeight : getHeight();

    // 8.162：**自分の下端より下へは行かせない**（Phase 200／本人の報告）。
    //
    // 本人の言葉は「ドラムエディターでPop out時にレーンが表示されない」。
    //
    // ここが返していたのは「**見えている範囲**の下端」だけでした。
    // 中身より窓のほうが大きいと、その位置は**コンポーネント自身の下端より下**になり、
    // レーンははみ出して1本も見えなくなります。
    //
    // **ドラム表示で先に出たのは、中身が小さいから**です：
    // ピアノロールは128音×12px＝1500px超で窓に収まりませんが、
    // ドラムは37行×16px＝約600pxなので、ポップアウトの窓には**収まってしまう**。
    // ドックに入っているあいだは窓が低くて収まらないため、
    // **「Pop outしたときだけ消える」**という出方になっていました。
    const int stuckToVisibleBottom = visibleScrollOffsetY + juce::jmax (0, height - laneHeight);
    const int ownBottom = juce::jmax (0, getHeight() - laneHeight);

    return { 0, juce::jmin (stuckToVisibleBottom, ownBottom), getWidth(), laneHeight };
}

void PianoRollComponent::setLaneHeight (int newHeight)
{
    // 8.122：**画面に入る範囲で丸める**（Phase 157／改善案36）。
    //
    // 上限を決めておかないと、レーンが編集画面を埋めてノートが1行も見えなくなる。
    // 見えている高さを教わっている（`visibleHeight`）ときは、
    // **ノート4行ぶんは必ず残す**——「レーンだけになって戻せない」を作らないため
    const int roomLimit = (visibleHeight > 0)
                             ? juce::jmax (minLaneHeight, visibleHeight - noteRowHeight * 4)
                             : maxLaneHeight;

    const int clamped = juce::jlimit (minLaneHeight, juce::jmin (maxLaneHeight, roomLimit), newHeight);

    if (laneHeight == clamped)
        return;

    laneHeight = clamped;

    // 中身の高さが変わるので、ビューのスクロール範囲を作り直させる
    updateSizeForLanes();
    repaint();
}

bool PianoRollComponent::isOnLaneResizeEdge (juce::Point<int> position) const
{
    const int edgeY = getLaneBounds().getY();

    return position.y >= edgeY - laneResizeGrabMargin
            && position.y <= edgeY + laneResizeGrabMargin;
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    // 8.122：**掴めることをカーソルで見せる**（Phase 157／改善案36）。
    // 4pxの帯は狙って当てるには細いので、手がかりが要る
    setMouseCursor (isOnLaneResizeEdge (e.getPosition())
                        ? juce::MouseCursor::UpDownResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

int PianoRollComponent::getGridHeight() const
{
    // **「レーンより上か」を測るためのもの。** 当たり判定と縦グリッド線が使う
    return juce::jmax (0, getLaneBounds().getY());
}

juce::Rectangle<int> PianoRollComponent::getVelocityLaneBounds() const
{
    // ベロシティを出しているときの呼び名。**同じ1本**を指す（Phase 75）
    return getLaneBounds();
}

juce::Rectangle<int> PianoRollComponent::getLaneHeaderBounds() const
{
    return getLaneBounds().withWidth (keyboardWidth);
}
//==============================================================================
// 8.1のG5：レーンの中身の切り替え（Phase 75／8.35）
//==============================================================================

int PianoRollComponent::getRequiredHeight() const
{
    // 仕様書5.3.2：ドラムモードでは行数も行の高さも変わる（Phase 25）
    return getNoteAreaHeight() + laneHeight;
}

void PianoRollComponent::updateSizeForLanes()
{
    // 幅はビュー側が決めているので触らない（横スクロール範囲が変わってしまう）
    setSize (getWidth(), getRequiredHeight());
    repaint();
}

void PianoRollComponent::setLaneTarget (LaneTarget newTarget)
{
    if (laneTarget == newTarget)
        return;

    laneTarget = newTarget;

    // 掴んでいる途中で中身が変わると、離したときに別のものへ書き込む
    dragMode = DragMode::None;
    draggedCCEvent = CCEvent (juce::ValueTree());
    draggedAutomationPoint = -1;

    // 8.38：**選んでいた点も捨てる**（Phase 78）。
    // 中身が変われば画面から消えるので、見えない点がDeleteで消えることになる
    selectedLanePoints.clear();
    laneDragOthers.clear();
    lanePendingAdd = false;
    laneRangeSelecting = false;

    if (onLaneTargetChanged != nullptr)
        onLaneTargetChanged();

    repaint();
}

juce::String PianoRollComponent::getLaneTargetName() const
{
    switch (laneTarget.kind)
    {
        case LaneTarget::Kind::automation:
            return AutomationTargets::getDisplayName (laneTarget.automationTargetId);

        case LaneTarget::Kind::cc:
            return MidiControllers::getDisplayName (laneTarget.controllerNumber);

        case LaneTarget::Kind::velocity:
        default:
            return "Velocity";
    }
}

void PianoRollComponent::showLaneTargetMenu (juce::Point<int> screenPosition)
{
    // **1つのメニューに全部を並べます**（Phase 75）。ベロシティ・オートメーション・CCは
    // 「下のレーンに何を出すか」という同じ選択なので、入口を分けると
    // 「どれがいま出ているのか」が読めなくなります
    juce::PopupMenu menu;

    menu.addItem (1, "Velocity", true, laneTarget.kind == LaneTarget::Kind::velocity);
    menu.addSeparator();

    menu.addSectionHeader (utf8 ("オートメーション"));
    menu.addItem (2, AutomationTargets::getDisplayName (AutomationTargets::volume), true,
                   laneTarget.kind == LaneTarget::Kind::automation
                    && laneTarget.automationTargetId == AutomationTargets::volume);
    menu.addItem (3, AutomationTargets::getDisplayName (AutomationTargets::pan), true,
                   laneTarget.kind == LaneTarget::Kind::automation
                    && laneTarget.automationTargetId == AutomationTargets::pan);

    menu.addSeparator();
    menu.addSectionHeader (utf8 ("MIDI CC"));

    // **既にイベントがあるものが先**：いま何が入っているかを探しに行かずに済む
    auto controllers = MidiControllers::getCommonControllers();
    int itemId = 10;

    for (auto controller : controllers)
    {
        const bool hasEvents = hasTrack() && editedTrack.getNumCCEventsFor (controller) > 0;
        const bool isCurrent = (laneTarget.kind == LaneTarget::Kind::cc
                                 && laneTarget.controllerNumber == controller);

        menu.addItem (itemId, MidiControllers::getDisplayName (controller)
                                + (hasEvents ? utf8 ("  ●") : juce::String()),
                       true, isCurrent);
        ++itemId;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, controllers] (int result)
        {
            if (result <= 0)
                return;

            LaneTarget target;

            if (result == 1)
            {
                target.kind = LaneTarget::Kind::velocity;
            }
            else if (result == 2 || result == 3)
            {
                target.kind = LaneTarget::Kind::automation;
                target.automationTargetId = (result == 2) ? AutomationTargets::volume
                                                          : AutomationTargets::pan;
            }
            else
            {
                const int index = result - 10;

                if (! juce::isPositiveAndBelow (index, controllers.size()))
                    return;

                target.kind = LaneTarget::Kind::cc;
                target.controllerNumber = controllers[index];
            }

            setLaneTarget (target);
        });
}

Track PianoRollComponent::getEditedTrack() const
{
    // **Phase 127：トラックは自分で持っている。** それまではクリップから
    // 2つ遡って引いていたが、クリップが1つも無いトラックでは引けない
    return editedTrack;
}

AutomationLane PianoRollComponent::getLaneForAutomation (bool createIfMissing)
{
    auto track = getEditedTrack();

    if (! track.state.getParent().isValid() || laneTarget.kind != LaneTarget::Kind::automation)
        return AutomationLane (juce::ValueTree());

    if (createIfMissing)
        return track.getOrCreateAutomationLane (laneTarget.automationTargetId, nullptr);

    return track.findAutomationLane (laneTarget.automationTargetId);
}

int PianoRollComponent::automationValueToY (const juce::Rectangle<int>& laneBounds, float value) const
{
    const auto inner = laneBounds.reduced (0, 6);
    const double ratio = juce::jlimit (0.0, 1.0, (double) value);

    // 値が大きいほど上へ（Y座標は下向きが正なので反転させる）
    return inner.getBottom() - (int) (ratio * inner.getHeight());
}

float PianoRollComponent::yToAutomationValue (const juce::Rectangle<int>& laneBounds, int y) const
{
    const auto inner = laneBounds.reduced (0, 6);

    if (inner.getHeight() <= 0)
        return 0.0f;

    return juce::jlimit (0.0f, 1.0f, (float) (inner.getBottom() - y) / (float) inner.getHeight());
}

int PianoRollComponent::findAutomationPointAt (juce::Point<int> position)
{
    auto lane = getLaneForAutomation (false);

    if (! lane.state.isValid())
        return -1;

    auto bounds = getLaneBounds();

    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        auto point = lane.getPoint (i);

        // オートメーションの点は**タイムライン上の時刻**（トラックの持ち物）。
        // CCイベント（クリップの中身の時刻）と取り違えないこと（8.28）
        const int x = timelineTimeToX (point.getTime());
        const int y = automationValueToY (bounds, point.getValue());

        if (std::abs (position.x - x) <= ccGrabMargin && std::abs (position.y - y) <= ccGrabMargin)
            return i;
    }

    return -1;
}

int PianoRollComponent::ccValueToY (const juce::Rectangle<int>& laneBounds, int controllerNumber, int value) const
{
    const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (controllerNumber));
    const auto inner = laneBounds.reduced (0, 6);

    // 値が大きいほど上へ（Y座標は下向きが正なので反転させる）
    const double ratio = juce::jlimit (0.0, 1.0, (double) value / (double) maxValue);

    return inner.getBottom() - (int) (ratio * inner.getHeight());
}

int PianoRollComponent::yToCCValue (const juce::Rectangle<int>& laneBounds, int controllerNumber, int y) const
{
    const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (controllerNumber));
    const auto inner = laneBounds.reduced (0, 6);

    if (inner.getHeight() <= 0)
        return 0;

    const double ratio = juce::jlimit (0.0, 1.0, (double) (inner.getBottom() - y) / (double) inner.getHeight());

    return (int) std::lround (ratio * maxValue);
}

CCEvent PianoRollComponent::findCCEventAt (juce::Point<int> position) const
{
    // 8.1のG5：**レーンに出しているコントローラーだけを見ます**（Phase 75）。
    // Phase 74までは「何番目のレーンか」を受け取っていましたが、
    // レーンが1本になったので、対象は`laneTarget`が持っています
    if (! hasTrack() || laneTarget.kind != LaneTarget::Kind::cc)
        return CCEvent (juce::ValueTree());

    const int controllerNumber = laneTarget.controllerNumber;
    auto laneBounds = getLaneBounds();

    for (int i = 0; i < editedTrack.getNumCCEventsFor (controllerNumber); ++i)
    {
        auto event = editedTrack.getCCEventFor (controllerNumber, i);

        if (! event.state.isValid())
            continue;

        const int x = timelineTimeToX (getLanePointTime (event.state));   // Phase 126
        const int y = ccValueToY (laneBounds, controllerNumber, event.getValue());

        // 縦は多少ずれても掴めるようにする（点が小さく、値を狙って合わせるのは難しいため）
        if (std::abs (position.x - x) <= ccGrabMargin && std::abs (position.y - y) <= ccGrabMargin * 2)
            return event;
    }

    return CCEvent (juce::ValueTree());
}


juce::Rectangle<int> PianoRollComponent::getVelocityBarBounds (const Note& note) const
{
    auto lane = getVelocityLaneBounds();
    const int x = timelineTimeToX (getNoteTimelineStart (note.state));   // Phase 126

    // ベロシティ（0〜127）を棒の高さへ変換する
    const int barHeight = (int) ((double) note.getVelocity() / 127.0 * (lane.getHeight() - 8));

    return { x, lane.getBottom() - barHeight - 4, 8, barHeight };
}

juce::ValueTree PianoRollComponent::findVelocityBarAt (juce::Point<int> position) const
{
    if (! getVelocityLaneBounds().contains (position))
        return {};

    juce::ValueTree found;

    forEachNote ([this, position, &found] (const juce::ValueTree& noteState)
    {
        if (found.isValid())
            return;

        // 棒の高さに関係なく、X方向が合っていれば掴めるようにする（操作しやすさ優先）
        auto bar = getVelocityBarBounds (Note (noteState));

        if (position.x >= bar.getX() && position.x < bar.getRight())
            found = noteState;
    });

    return found;
}

void PianoRollComponent::quantiseNotes (int gridDivision, double swingAmount, bool selectedOnly)
{
    if (! hasTrack() || gridDivision <= 0)
        return;

    // 8.98／Phase 139：**クオンタイズも拍の座標で行う。**
    // 目盛りは「1拍を`gridDivision`で割ったもの」。秒で持つと、テンポが
    // 曲の途中で変わった先で拍から外れます（グリッド線・スナップと同じ話）
    const double gridBeats = 1.0 / gridDivision;

    auto& undoManager = project.getUndoManager();
    project.beginAction (utf8 ("クオンタイズ"));

    // **Phase 127：トラックの全クリップに効かせる**（それまでは開いていた1つだけ）。
    //
    // **「選択中のみ」の判定も直しました**：それまで`selectedNoteIndex`（掴んでいる1つ）
    // と比べていたので、**複数選んでも1つしか掛かりませんでした**。
    // 選択は`selectedNotes`が持っています（Phase 52）
    pruneNoteSelection();


    forEachNote ([&] (const juce::ValueTree& noteState)
    {
        if (selectedOnly && ! isNoteSelected (noteState))
            return;

        Note note { juce::ValueTree (noteState) };

        // 8.137：**拍はノート自身に訊く**（Phase 175／8.105の宿題3）。
        //
        // Phase 147までは「秒をもらって、プロジェクトで拍へ直して、また秒へ戻す」と
        // 3行かけて書いていました。**その換算はノートの側の仕事**です——
        // 宿題3で保存が拍になったとき、ここは1文字も変わりません。
        //
        // 寄せ先が**タイムライン上の目盛り**であることは変わりません（8.28）
        const double gridIndex = std::floor (note.getStartBeats() / gridBeats + 0.5);

        double newBeats = gridIndex * gridBeats;

        // スイング：奇数番目のグリッド（裏拍）を後ろへずらす
        if (swingAmount > 0.0 && ((juce::int64) gridIndex % 2) != 0)
            newBeats += gridBeats * swingAmount * 0.5;

        note.setStartBeats (newBeats, &undoManager);
    });

    if (onModelChanged != nullptr)
        onModelChanged();
}


//==============================================================================
// 8.29の表：左端の見出し（鍵盤／ドラム行）とベロシティの書き込み（Phase 69）
//==============================================================================

bool PianoRollComponent::handleKeyboardClick (const juce::MouseEvent& e)
{
    // 8.36：**固定表示のレーンが乗っている場所は、鍵盤の見出しではない**（Phase 76）。
    //
    // Phase 76でレーンを「見えている範囲の下端に固定」にしたので、
    // **レーンは鍵盤の行と重なります**（前は下端に置いていたので重ならなかった）。
    // ここで先に外しておかないと、レーンの見出し＝切り替えメニューの入口を押しても
    // この関数が先に食べてしまい、**メニューが出ません**。
    //
    // **上に乗っている行は、隠れている行より先に判定すること**（8.20と同じ話）。
    if (getLaneBounds().contains (e.getPosition()))
        return false;

    if (e.x >= keyboardWidth || e.y >= getNoteAreaHeight())
        return false;

    // ドラムモードでは行がドラムマップの並び順。マップの外はそもそも行が無い
    if (drumMode)
    {
        const int row = getDrumRowAtY (e.y);

        if (! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
            return false;

        if (e.mods.isPopupMenu())
        {
            showDrumRowMenu (row, e.getScreenPosition());
            return true;
        }
    }
    else if (e.mods.isPopupMenu())
    {
        // 8.29の表：鍵盤の右クリックは表示の切り替え（Phase 70）。
        // **trueを返してノート側へ落とさないこと**
        // （鍵盤の上でノートのメニューが出ると、押した場所と出るものが噛み合わない）
        showKeyboardMenu (e.getScreenPosition());
        return true;
    }

    // 8.29の表：**ペンで押すと音が鳴る**（Phase 71）。ドラッグで通った行も鳴る
    // （`mouseDrag`の`keyboardPreviewing`）。カットはこの見出しでは何もしない
    if (editTool == EditTool::pencil)
    {
        keyboardPreviewing = true;
        startPreview (yToPitch (e.y), 100);
        return true;
    }

    if (editTool != EditTool::arrow)
        return true;

    // 矢印：**その音のノートを選ぶ。** そのままドラッグすれば、通った行ぶんまとめて選ぶ
    keyboardSelecting = true;
    keyboardSelectAnchorY = e.y;
    keyboardSelectCurrentY = e.y;
    activeNoteState = juce::ValueTree();
    applyKeyboardSelection();

    return true;
}

void PianoRollComponent::showDrumRowMenu (int row, juce::Point<int> screenPosition)
{
    if (! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
        return;

    auto entry = drumMap.getEntry (row);

    // 仕様書5.3.2：楽器名・行ミュート・チョークグループ。
    // **行ミュートはPhase 69でここへ移しました**（それまでは見出しの左クリック）。
    // 左クリックは「その音のノートを選ぶ」になったので、置き場所がここしか無い（8.29）。
    juce::PopupMenu menu;
    menu.addSectionHeader (entry.getPartName());
    menu.addItem (10, utf8 ("楽器名を変更..."));
    menu.addItem (11, utf8 ("この行をミュート"), true, entry.isMuted());
    menu.addSeparator();
    menu.addSectionHeader (utf8 ("チョークグループ"));
    menu.addItem (1, utf8 ("グループなし"), true, entry.getMuteGroup() == 0);

    for (int group = 1; group <= 4; ++group)
        menu.addItem (1 + group, utf8 ("グループ ") + juce::String (group),
                       true, entry.getMuteGroup() == group);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, row] (int result)
        {
            if (result <= 0 || ! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
                return;

            if (result == 10)
            {
                renameDrumPart (row);
                return;
            }

            auto targetEntry = drumMap.getEntry (row);

            if (result == 11)
            {
                project.beginAction (utf8 ("ドラム行のミュート切り替え"));
                targetEntry.setMuted (! targetEntry.isMuted(), &project.getUndoManager());
            }
            else
            {
                project.beginAction (utf8 ("チョークグループの変更"));
                targetEntry.setMuteGroup (result - 1, &project.getUndoManager());
            }

            if (onModelChanged != nullptr)
                onModelChanged();

            repaint();
        });
}

void PianoRollComponent::collectPitchesBetweenY (int y1, int y2, juce::SortedSet<int>& pitches) const
{
    const int top = juce::jmin (y1, y2);
    const int bottom = juce::jmax (y1, y2);
    const int rowHeight = getRowHeight();

    if (rowHeight <= 0)
        return;

    // **行ごとに拾うこと。** ドラムモードでは行の並びが音程順ではないので、
    // 「上の音程から下の音程まで」で範囲を作ると、間に関係のない音が混ざる
    for (int y = top; y <= bottom; y += rowHeight)
        pitches.add (yToPitch (y));

    pitches.add (yToPitch (bottom));
}

void PianoRollComponent::applyKeyboardSelection()
{
    juce::SortedSet<int> pitches;
    collectPitchesBetweenY (keyboardSelectAnchorY, keyboardSelectCurrentY, pitches);

    selectedNotes.clear();

    // **トラックの全クリップから選ぶ**（画面に見えている範囲だけではない）。
    // 「この音を全部まとめて」が目的なので、スクロール位置で結果が変わってはいけない
    // （Phase 127で「クリップ全体」から「トラック全体」になった）
    forEachNote ([this, &pitches] (const juce::ValueTree& noteState)
    {
        if (pitches.contains (Note (noteState).getPitch()))
            selectedNotes.push_back (noteState);
    });

    repaint();
}

void PianoRollComponent::addNoteAt (int x, int y)
{
    if (! hasTrack())
        return;

    // 置く位置はスナップに従う（8.14）。**手前の目盛りへ寄せる**ので、
    // マスのどこをクリックしても、そのマスの頭から始まる
    const int pitch = yToPitch (y);

    // 寄せるのはタイムライン上。**8.91：これがそのまま置く時刻**（Phase 131）
    const double startTime = project.snapTimeDown (xToTimelineTime (x));

    // **長さも目盛り1つぶんにする**（Phase 54）。コード区間と違い、
    // ノートは「マス1つを埋める」のが打ち込みの単位。フリーのときは既定値。
    const double snapSeconds = project.getSnapSecondsAt (startTime);
    const double noteLength = (snapSeconds > 0.0) ? snapSeconds : defaultNoteLength;

    project.beginAction (utf8 ("ノートの追加"));

    // 8.91：**トラックへ直に置くだけ**（Phase 131）。行き先のクリップを探す・
    // 無ければ作る・伸ばす、という段取りがまるごと消えた
    auto added = editedTrack.addNote (pitch, 100, startTime, noteLength, &project.getUndoManager());

    // 8.29の表：**置いた音が鳴る**（Phase 71）
    startPreview (pitch, 100);

    activeNoteState = added.state;
    setSingleNoteSelection (added.state);

    // 8.29の表：**そのままドラッグすると長さが決まる**（Phase 69）。
    // 右端を掴んだのと同じ扱いにしておくと、伸縮の処理をもう1つ書かずに済む。
    // **区切りは今作ったので、離すときには作らない**（`dragStartedFromPencil`）。
    // 作ると「置いた」と「伸ばした」が別々の履歴になり、Undoが2回要る（3.1）
    dragMode = DragMode::ResizeRight;
    dragStartedFromPencil = true;
    dragStartPosition = { x, y };
    dragOriginalStartTime = startTime;
    dragOriginalLength = noteLength;
    dragOriginalPitch = pitch;
    dragPreviewStartTime = startTime;
    dragPreviewLength = noteLength;
    dragPreviewPitch = pitch;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void PianoRollComponent::paintVelocityAt (juce::Point<int> position)
{
    if (! hasTrack())
        return;

    auto lane = getVelocityLaneBounds();
    const auto inner = lane.reduced (0, 4);

    if (inner.getHeight() <= 0)
        return;

    // 上へ行くほど強い（Y座標は下向きが正なので反転させる）
    const double ratio = juce::jlimit (0.0, 1.0,
                                        (double) (inner.getBottom() - position.y) / (double) inner.getHeight());
    const int velocity = juce::jlimit (1, 127, (int) std::round (ratio * 127.0));

    bool changed = false;

    // **カーソルのX座標に棒がかかっているノートを全部書き換える。**
    // 和音は同じ位置に重なるので、1つだけ選ぶと「どれが変わったのか」が読めない
    // （重なっているときの選び分けは8.1のE3。まだ手を付けていない）
    forEachNote ([&] (const juce::ValueTree& noteState)
    {
        Note note { juce::ValueTree (noteState) };
        auto bar = getVelocityBarBounds (note);

        if (position.x < bar.getX() || position.x > bar.getRight())
            return;

        if (note.getVelocity() == velocity)
            return;

        // **区切りは呼び出し元が作っている**（なぞり1回ぶんで1つ）。ここでは作らない
        note.setVelocity (velocity, &project.getUndoManager());
        changed = true;
    });

    if (changed && onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

juce::String PianoRollComponent::getKeyboardLabel (int pitch) const
{
    if (keyboardLabels == KeyboardLabels::noteName)
    {
        // 音名は**Cの行だけ**（全部に出すと行が文字で埋まる）。
        // 8.121：**数え方は`midiNoteName()`に1本化**（Phase 156）。
        // ここで自分で数えていると、ノートの中の文字と1オクターブずれる（1.27）
        if (pitch % 12 != 0)
            return {};

        return midiNoteName (pitch);
    }

    // 8.29の表：階名表示（Phase 70）。
    //
    // **「ドはキーのルート」**（移動ド）。マイナーでもルートをドとして数えます：
    // ラから数える流儀もありますが、**スケールの7音と7つの音節が1対1で並ぶ**ほうが、
    // コードトラックの度数表示（I〜VII）や構成音カラーリング（5.3.1）と読みが揃います。
    // **スケールから外れた音には出しません**（黒鍵かどうかを塗りで読むため）。
    static const char* const syllables[7] = { "ド", "レ", "ミ", "ファ", "ソ", "ラ", "シ" };

    const auto key = getKeyForDisplay();
    const int pitchClass = ((pitch % 12) + 12) % 12;

    for (int degree = 0; degree < 7; ++degree)
        if (key.degreeRoot (degree) == pitchClass)
            return utf8 (syllables[degree]);

    return {};
}

void PianoRollComponent::setKeyboardLabels (KeyboardLabels newLabels)
{
    if (keyboardLabels == newLabels)
        return;

    keyboardLabels = newLabels;
    repaint();

    if (onKeyboardLabelsChanged != nullptr)
        onKeyboardLabelsChanged();
}

void PianoRollComponent::showKeyboardMenu (juce::Point<int> screenPosition)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (utf8 ("鍵盤の表示"));
    menu.addItem (1, utf8 ("音名（C4）"), true, keyboardLabels == KeyboardLabels::noteName);
    menu.addItem (2, utf8 ("階名（ド）"), true, keyboardLabels == KeyboardLabels::solfege);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this] (int result)
        {
            if (result == 1)
                setKeyboardLabels (KeyboardLabels::noteName);
            else if (result == 2)
                setKeyboardLabels (KeyboardLabels::solfege);
        });
}

void PianoRollComponent::showGridMenu (const juce::MouseEvent& e)
{
    const double timelineTime = xToTimelineTime (e.x);   // Phase 126：タイムライン基準
    const bool canPaste = (EditClipboard::getKind() == EditClipboard::Kind::notes
                            || EditClipboard::getKind() == EditClipboard::Kind::ccEvents);

    pruneNoteSelection();
    const int numSelected = (int) selectedNotes.size();

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("カット"), numSelected > 0);
    menu.addItem (2, utf8 ("コピー"), numSelected > 0);

    // **貼り付け先は「右クリックした位置」**（Phase 71）。
    // ショートカット（Ctrl+V）は再生カーソルの位置に貼るので、そちらとは基準が違う。
    // メニューは押した場所が見えているので、そこへ入るほうが分かりやすい（8.32）
    menu.addItem (3, utf8 ("ここに貼り付け"), canPaste);
    menu.addSeparator();
    menu.addItem (4, utf8 ("選択したノートを削除"), numSelected > 0);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
        [this, timelineTime] (int result)
        {
            if (result == 1)        cutSelection();
            else if (result == 2)   copySelection();
            else if (result == 3)   pasteAt (timelineTime);
            else if (result == 4)   deleteSelectedNotes();
        });
}

void PianoRollComponent::showCCEventMenu (CCEvent event, juce::Point<int> screenPosition)
{
    if (! hasTrack() || ! event.state.isValid())
        return;

    const int controllerNumber = event.getControllerNumber();

    // 8.36：**オートメーションの点と同じ中身**（Phase 76）。
    // 「点や線の書き方をそろえる」ということは、触り方もそろえるということ
    juce::PopupMenu menu;
    menu.addSectionHeader (MidiControllers::getDisplayName (controllerNumber)
                            + " = " + juce::String (event.getValue()));
    menu.addItem (1, utf8 ("数値を入力..."));
    menu.addSeparator();
    AutomationCurveUI::addCurveItems (menu, event.getCurve(), event.getCurveAmount());
    menu.addSeparator();
    menu.addItem (3, utf8 ("カット"));       // 8.29の表（Phase 71）
    menu.addItem (4, utf8 ("コピー"));
    menu.addSeparator();
    menu.addItem (2, utf8 ("この点を削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, event] (int result) mutable
        {
            // メニューを開いているあいだに点が消えている可能性がある。
            // **ValueTreeで持っているので、番号のずれは起きない**（1.32）
            if (result <= 0 || ! event.state.getParent().isValid())
                return;

            if (result == 1)
            {
                showCCValueEntry (event);
                return;
            }

            if (result == 3 || result == 4)
            {
                copyCCEvent (event, result == 3);   // 3＝カット（コピーしてから消す）
                return;
            }

            if (AutomationCurveUI::isCurveItem (result))
            {
                // 8.37：**種別と曲がり具合はいっしょに書く**（Phase 77）。
                // 「直線」を選んだときは曲がり具合も0へ戻る（`amountForItem()`）
                project.beginAction (utf8 ("カーブ種別の変更"));
                event.setCurve (AutomationCurveUI::curveForItem (result), &project.getUndoManager());
                event.setCurveAmount (AutomationCurveUI::amountForItem (result, event.getCurveAmount()),
                                       &project.getUndoManager());

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
                return;
            }

            project.beginAction (utf8 ("CCイベントの削除"));
            editedTrack.removeCCEvent (event, &project.getUndoManager());

            if (onModelChanged != nullptr)
                onModelChanged();

            repaint();
        });
}

void PianoRollComponent::showAutomationPointMenu (int pointIndex, juce::Point<int> screenPosition)
{
    auto lane = getLaneForAutomation (false);

    if (! lane.state.isValid() || ! juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
        return;

    // 8.36：**CCの点のメニューと同じ中身**（Phase 76）。
    // アレンジ画面のオートメーションの点とも同じ並びにしてある（8.29）
    juce::PopupMenu menu;
    menu.addSectionHeader (AutomationTargets::getDisplayName (laneTarget.automationTargetId)
                            + " = "
                            + AutomationTargets::formatValue (laneTarget.automationTargetId,
                                                               lane.getPoint (pointIndex).getValue()));
    menu.addItem (1, utf8 ("数値を入力..."));
    menu.addSeparator();
    AutomationCurveUI::addCurveItems (menu, lane.getPoint (pointIndex).getCurve(),
                                       lane.getPoint (pointIndex).getCurveAmount());
    menu.addSeparator();
    menu.addItem (3, utf8 ("カット"));
    menu.addItem (4, utf8 ("コピー"));
    menu.addSeparator();
    menu.addItem (2, utf8 ("この点を削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                            .withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
        [this, pointIndex] (int result)
        {
            if (result <= 0)
                return;

            // メニューを開いているあいだに点が減っている可能性がある
            auto targetLane = getLaneForAutomation (false);

            if (! targetLane.state.isValid()
                 || ! juce::isPositiveAndBelow (pointIndex, targetLane.getNumPoints()))
                return;

            if (result == 1)
            {
                showAutomationValueEntry (pointIndex);
                return;
            }

            if (result == 3 || result == 4)
            {
                copyAutomationPoint (pointIndex, result == 3);   // 3＝カット
                return;
            }

            if (AutomationCurveUI::isCurveItem (result))
            {
                // 8.37：**種別と曲がり具合はいっしょに書く**（Phase 77）
                auto point = targetLane.getPoint (pointIndex);

                project.beginAction (utf8 ("カーブ種別の変更"));
                point.setCurve (AutomationCurveUI::curveForItem (result), &project.getUndoManager());
                point.setCurveAmount (AutomationCurveUI::amountForItem (result, point.getCurveAmount()),
                                       &project.getUndoManager());

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
                return;
            }

            project.beginAction (utf8 ("オートメーション点の削除"));
            targetLane.removePoint (pointIndex, &project.getUndoManager());

            if (onModelChanged != nullptr)
                onModelChanged();

            repaint();
        });
}

void PianoRollComponent::showAutomationValueEntry (int pointIndex)
{
    auto lane = getLaneForAutomation (false);

    if (! lane.state.isValid() || ! juce::isPositiveAndBelow (pointIndex, lane.getNumPoints()))
        return;

    const auto targetId = laneTarget.automationTargetId;

    // **入力するのは「実際の値」**（VolumeならdB）。モデルは0〜1の正規化値で持っているが、
    // 打ち込むときに正規化値を要求されても何を入れればよいか分からない。
    // 換算は`AutomationTargets`を通すこと（式をここに書かない。8.31と同じ）
    const float currentValue = AutomationTargets::toParameterValue (targetId,
                                                                     lane.getPoint (pointIndex).getValue());

    NameEntry::show (AutomationTargets::getDisplayName (targetId),
                      utf8 ("値を入力してください。"),
                      juce::String (currentValue, 2),
                      [this, pointIndex, targetId] (const juce::String& text)
                      {
                          auto targetLane = getLaneForAutomation (false);

                          if (! targetLane.state.isValid()
                               || ! juce::isPositiveAndBelow (pointIndex, targetLane.getNumPoints()))
                              return;

                          // **範囲外は丸める**（`fromParameterValue()`が0〜1へ収める）
                          const float normalised = AutomationTargets::fromParameterValue (
                                                       targetId, (float) text.getDoubleValue());

                          project.beginAction (utf8 ("オートメーション点の値の変更"));
                          targetLane.getPoint (pointIndex).setValue (normalised, &project.getUndoManager());

                          if (onModelChanged != nullptr)
                              onModelChanged();

                          repaint();
                      });
}

void PianoRollComponent::showCCValueEntry (CCEvent event)
{
    if (! event.state.getParent().isValid())
        return;

    auto* window = new juce::AlertWindow (utf8 ("CCの値"),
                                           utf8 ("値を入力してください。"),
                                           juce::MessageBoxIconType::NoIcon);

    window->addTextEditor ("value", juce::String (event.getValue()), utf8 ("値"));
    window->addButton (utf8 ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (utf8 ("キャンセル"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, window, event] (int result) mutable
        {
            if (result == 1 && event.state.getParent().isValid())
            {
                const auto text = window->getTextEditorContents ("value").trim();

                if (text.isNotEmpty())
                {
                    // **上限はコントローラーごとに違う**（ピッチベンドは16383）。
                    // 決め打ちの127で丸めると、ピッチベンドが端まで届かない
                    const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (event.getControllerNumber()));

                    project.beginAction (utf8 ("CCイベントの編集"));
                    event.setValue (juce::jlimit (0, maxValue, text.getIntValue()),
                                     &project.getUndoManager());

                    if (onModelChanged != nullptr)
                        onModelChanged();

                    repaint();
                }
            }

            delete window;
        }),
        false);
}

void PianoRollComponent::startPreview (int pitch, int velocity)
{
    if (onPreviewNoteOn == nullptr || previewedPitch == pitch)
        return;

    // **前の音を止めてから次を鳴らす。** 止め忘れると鳴りっぱなしになる
    // （Phase 14dで踏んだのと同じ形：ノートオフが誰にも送られない）
    stopPreview();

    previewedPitch = pitch;
    onPreviewNoteOn (pitch, velocity);
}

void PianoRollComponent::stopPreview()
{
    if (previewedPitch < 0)
        return;

    if (onPreviewNoteOff != nullptr)
        onPreviewNoteOff (previewedPitch);

    previewedPitch = -1;
}

void PianoRollComponent::setPlayheadSeconds (double timelineSeconds)
{
    if (playheadSeconds == timelineSeconds)
        return;

    // **X座標が変わらないなら描き直さない。** 再生中は毎フレーム呼ばれるので、
    // 秒の差だけで描き直すと、縮小表示のときに同じ絵を延々と描くことになる
    const int oldX = timelineTimeToX (playheadSeconds);
    playheadSeconds = timelineSeconds;

    if (timelineTimeToX (playheadSeconds) != oldX)
        repaint();
}

//==============================================================================
// 仕様書6.2：カット／コピー／貼り付け（Phase 71／8.29の表）
//==============================================================================

bool PianoRollComponent::copySelectedNotes (bool alsoDelete)
{
    pruneNoteSelection();

    if (! hasTrack() || selectedNotes.empty())
        return false;

    // **基準はいちばん early なノート。** 貼り付け先の時刻にここからの差を足せば、
    // 選んだときの間隔がそのまま保たれる
    double referenceTime = std::numeric_limits<double>::max();

    // **Phase 126：基準はタイムライン上の時刻。** 中身の時刻のまま引き算すると、
    // 別のクリップのノートを一緒に選んだときに間隔が狂う（8.86のPhase 127で効いてくる）
    for (const auto& noteState : selectedNotes)
        referenceTime = juce::jmin (referenceTime, getNoteTimelineStart (noteState));

    // 8.139：**基準の拍**（Phase 177）
    const double referenceBeats = project.getBeatPositionAt (referenceTime);

    juce::Array<EditClipboard::Item> items;

    for (const auto& noteState : selectedNotes)
    {
        EditClipboard::Item item;

        // **複製を入れること。** 元をそのまま入れると、消したり動かしたりしたときに
        // 貼り付ける中身まで変わる
        item.state = noteState.createCopy();
        item.timeOffset = getNoteTimelineStart (noteState) - referenceTime;

        // 8.139：**拍のずれも覚える**（Phase 177）。貼り付け先のテンポが違っても、
        // 音符どうしの間隔が音楽的に保たれます（`EditClipboard.h`）
        item.beatOffset = Note (noteState).getStartBeats() - referenceBeats;
        items.add (item);
    }

    EditClipboard::set (EditClipboard::Kind::notes, std::move (items));

    if (alsoDelete)
        deleteSelectedNotes();

    return true;
}

bool PianoRollComponent::pasteNotesAt (double timelineSeconds)
{
    if (! hasTrack() || EditClipboard::getKind() != EditClipboard::Kind::notes)
        return false;

    // **Phase 126：受け取るのはタイムライン上の時刻。**
    // 8.91：**そのままトラックへ置く**（Phase 131）
    const double startTime = project.snapTime (juce::jmax (0.0, timelineSeconds));

    project.beginAction (utf8 ("ノートの貼り付け"));

    selectedNotes.clear();
    activeNoteState = juce::ValueTree();

    // 8.139：**貼り付け位置の拍**（Phase 177）。中身のずれは拍で持っています
    const double pasteStartBeats = project.getBeatPositionAt (startTime);

    for (const auto& item : EditClipboard::getItems())
    {
        const double time = juce::jmax (0.0, startTime + item.timeOffset);

        // 8.95：**種別で振り分けること**（Phase 135）。
        //
        // アレンジ画面で時間範囲をコピーすると、**ノートとCCが混ざって入ります**。
        // 種別を見ずに`notePitch`を読むと、CCから音程0のノートができます
        if (item.state.hasType (IDs::CC))
        {
            CCEvent event { juce::ValueTree (item.state) };

            // 8.139：**位置は拍で**（Phase 177）
            editedTrack.addCCEventBeats (event.getControllerNumber(), event.getValue(),
                                          juce::jmax (0.0, pasteStartBeats + item.beatOffset),
                                          &project.getUndoManager());
            continue;
        }

        if (! item.state.hasType (IDs::NOTE))
            continue;

        // 8.137：**アクセサを通すこと**（Phase 175）。プロパティ名を直に読むと、
        // 保存の形が変わったとき（8.105の宿題3）にここだけ取り残されます（1.27）
        Note pasted { juce::ValueTree (item.state) };

        const int pitch = juce::jlimit (lowestPitch, highestPitch, pasted.getPitch());
        const int velocity = juce::jlimit (1, 127, pasted.getVelocity());

        // 8.138：**長さは拍のまま運ぶ**（Phase 176）。クリップボードの中身は
        // 切り離されたツリーなので、秒で訊くと既定の120BPMで答えます（`MusicalTime.h`）。
        // 最低の長さだけは、貼り付け先のテンポで測って拍へ直します。
        //
        // 8.139：**位置も拍で**（Phase 177）。Phase 176は長さだけ拍だったので、
        // テンポの変わる先へ貼ると**間隔だけ秒のまま**という食い違いが残っていました
        const double startBeats = juce::jmax (0.0, pasteStartBeats + item.beatOffset);
        const double minLengthBeats = MusicalTime::getBeatsFor (editedTrack.state,
                                                                 time + minNoteLength)
                                        - MusicalTime::getBeatsFor (editedTrack.state, time);
        const double lengthBeats = juce::jmax (minLengthBeats, pasted.getLengthBeats());

        auto added = editedTrack.addNoteBeats (pitch, velocity, startBeats, lengthBeats,
                                                &project.getUndoManager());

        // **貼り付けたものを選んでおく。** 続けて動かしたいことが多く、
        // 選ばれていないと「どこへ入ったのか」も分かりにくい
        if (added.state.isValid())
            selectedNotes.push_back (added.state);
    }

    editedTrack.sortCCEvents (&project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

bool PianoRollComponent::copyAutomationPoint (int pointIndex, bool alsoDelete)
{
    auto lane = getLaneForAutomation (false);

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

bool PianoRollComponent::pasteAutomationPointsAt (double timelineSeconds)
{
    if (EditClipboard::getKind() != EditClipboard::Kind::automationPoints)
        return false;

    auto lane = getLaneForAutomation (true);

    if (! lane.state.isValid())
        return false;

    // **点はタイムライン上の時刻**（8.28）。**寄せません**：
    // オートメーションとCCの点はスナップの対象外と決めてある（8.14の「寄らないもの」）
    const double startTime = juce::jmax (0.0, timelineSeconds);

    project.beginAction (utf8 ("オートメーション点の貼り付け"));

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
        }
    }

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

bool PianoRollComponent::copyCCEvent (CCEvent event, bool alsoDelete)
{
    if (! hasTrack() || ! event.state.getParent().isValid())
        return false;

    EditClipboard::Item item;
    item.state = event.state.createCopy();
    item.timeOffset = 0.0;
    item.beatOffset = 0.0;   // 1つだけなので基準そのもの（8.139）

    juce::Array<EditClipboard::Item> items;
    items.add (item);

    EditClipboard::set (EditClipboard::Kind::ccEvents, std::move (items));

    if (alsoDelete)
    {
        project.beginAction (utf8 ("CCイベントの削除"));
        editedTrack.removeCCEvent (event, &project.getUndoManager());

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
    }

    return true;
}

bool PianoRollComponent::pasteCCEventsAt (double timelineSeconds)
{
    if (! hasTrack() || EditClipboard::getKind() != EditClipboard::Kind::ccEvents)
        return false;

    // **Phase 126：受け取るのはタイムライン上の時刻**（ノートと揃えた）
    const double startTime = project.snapTime (juce::jmax (0.0, timelineSeconds));   // 8.91

    project.beginAction (utf8 ("CCイベントの貼り付け"));

    for (const auto& item : EditClipboard::getItems())
    {
        // **コントローラー番号は中身が持っている。** 貼り付け先のレーンを
        // 「いま見えているもの」で決めると、別のコントローラーの値として書き込まれる
        const int controllerNumber = (int) item.state[IDs::ccController];

        // 8.139：**位置は拍で**（Phase 177）
        auto added = editedTrack.addCCEventBeats (controllerNumber, (int) item.state[IDs::ccValue],
                                                   juce::jmax (0.0, project.getBeatPositionAt (startTime)
                                                                      + item.beatOffset),
                                                   &project.getUndoManager());

        // 8.37：**繋ぎ方と曲がり具合もいっしょに運ぶ**（Phase 77。オートメーションと同じ）
        if (added.state.isValid())
        {
            added.setCurve (automationCurveFromString (item.state[IDs::ccCurve].toString()),
                             &project.getUndoManager());
            added.setCurveAmount ((float) item.state.getProperty (IDs::ccCurveAmount, 0.0f),
                                   &project.getUndoManager());
        }
    }

    // 左右へ入ると時刻順が入れ替わる。並べ替えておかないと値の読み出しが意味を成さない
    editedTrack.sortCCEvents (&project.getUndoManager());

    // レーンがまだ無いコントローラーを貼ったときは、ここで作る

    if (onModelChanged != nullptr)
        onModelChanged();

    updateSizeForLanes();
    repaint();
    return true;
}

bool PianoRollComponent::cutSelection()
{
    // 8.38：**レーンの点を選んでいるならそちら**（Phase 78。Deleteキーと同じ振り分け）
    pruneLaneSelection();

    if (! selectedLanePoints.empty())
        return copyLaneSelection (true);

    return copySelectedNotes (true);
}

bool PianoRollComponent::copySelection()
{
    pruneLaneSelection();

    if (! selectedLanePoints.empty())
        return copyLaneSelection (false);

    return copySelectedNotes (false);
}

bool PianoRollComponent::pasteAt (double timelineSeconds)
{
    // **種別で振り分ける。** ノートをCCレーンへ貼るような取り違えを起こさない。
    // **Phase 126で3つとも「タイムライン上の時刻」で受けるようになった**ので、
    // ここで基準を直す必要はなくなった（中身の時刻へ直すのは各々の中）
    if (EditClipboard::getKind() == EditClipboard::Kind::notes)
        return pasteNotesAt (timelineSeconds);

    if (EditClipboard::getKind() == EditClipboard::Kind::ccEvents)
        return pasteCCEventsAt (timelineSeconds);

    if (EditClipboard::getKind() == EditClipboard::Kind::automationPoints)
        return pasteAutomationPointsAt (timelineSeconds);

    return false;
}

void PianoRollComponent::renameDrumPart (int row)
{
    if (! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
        return;

    NameEntry::show (utf8 ("楽器名"), utf8 ("この行の楽器名を入力してください。"),
                      drumMap.getEntry (row).getPartName(),
                      [this, row] (const juce::String& newName)
                      {
                          // 入力欄を開いているあいだにマップが変わっている可能性がある
                          if (! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
                              return;

                          project.beginAction (utf8 ("楽器名の変更"));
                          drumMap.getEntry (row).setPartName (newName, &project.getUndoManager());

                          if (onModelChanged != nullptr)
                              onModelChanged();

                          repaint();
                      });
}

void PianoRollComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 8.29の表どおりの割り当て（Phase 68）。
    // **判定の順番は`mouseDown()`と同じにすること**（アレンジ画面と同じ理由）。

    // 仕様書5.3.2：ドラム行の見出し → 楽器名を変える（Phase 68）。
    // 1回目のmouseDownでミュートが切り替わっているので、**戻してから**入力欄を出す
    // （ダブルクリックのつもりがミュートも切り替わっていた、という状態にしない）
    if (drumMode && e.x < keyboardWidth && e.y < getNoteAreaHeight()
         && ! getLaneBounds().contains (e.getPosition()))   // Phase 76：固定レーンが乗っている場所を除く
    {
        const int row = getDrumRowAtY (e.y);

        if (! juce::isPositiveAndBelow (row, drumMap.getNumEntries()))
            return;

        auto entry = drumMap.getEntry (row);

        project.beginAction (utf8 ("ドラム行のミュート切り替え"));
        entry.setMuted (! entry.isMuted(), &project.getUndoManager());

        renameDrumPart (row);
        return;
    }
    // 8.29の表：レーンの点 → 削除（Phase 68。Phase 75でオートメーションも）。
    // **点の上だけ。** 空いている場所は1回目のmouseDownで点が増えているので、
    // ここではその点が消えて元に戻る
    if (getLaneBounds().contains (e.getPosition()))
    {
        if (! hasTrack() || e.x <= keyboardWidth)
            return;

        // 8.37：**つまみのダブルクリックで直線に戻す**（Phase 77）。
        // 曲げるのがドラッグなら、戻すのはその場で済むほうがよい
        // （右クリックメニューの「直線に戻す」と同じことをしている）
        const int handleSegment = findCurveHandleAt (e.getPosition());

        if (handleSegment >= 0)
        {
            const auto segment = getLaneSegments (getLaneBounds())[(size_t) handleSegment];

            project.beginAction (utf8 ("直線に戻す"));
            setSegmentCurve (segment.owner, AutomationCurve::Linear, 0.0f);
            repaint();
            return;
        }

        if (laneTarget.kind == LaneTarget::Kind::cc)
        {
            auto existing = findCCEventAt (e.getPosition());

            if (existing.state.isValid())
            {
                project.beginAction (utf8 ("CCイベントの削除"));
                editedTrack.removeCCEvent (existing, &project.getUndoManager());

                // 掴んだままの参照を残さない（消えた点を動かし続けることになる）
                draggedCCEvent = CCEvent (juce::ValueTree());
                dragMode = DragMode::None;

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
            }

            return;
        }

        if (laneTarget.kind == LaneTarget::Kind::automation)
        {
            const int pointIndex = findAutomationPointAt (e.getPosition());
            auto lane = getLaneForAutomation (false);

            if (pointIndex >= 0 && lane.state.isValid())
            {
                project.beginAction (utf8 ("オートメーション点の削除"));
                lane.removePoint (pointIndex, &project.getUndoManager());

                draggedAutomationPoint = -1;
                dragMode = DragMode::None;

                if (onModelChanged != nullptr)
                    onModelChanged();

                repaint();
            }
        }
    }
}

void PianoRollComponent::applyGroove (const GrooveTemplate& grooveTemplate, int gridDivision,
                                       double strength, bool selectedOnly)
{
    if (! hasTrack() || ! grooveTemplate.state.isValid() || gridDivision <= 0)
        return;

    auto& undoManager = project.getUndoManager();
    project.beginAction (utf8 ("グルーヴクオンタイズ"));

    // 8.135：**グルーヴもテンポに追いつきました**（Phase 173／8.105の宿題2）。
    //
    // Phase 172までは「曲に1つのテンポ」を前提に、抽出時のテンポとの比で
    // ズレを伸縮させていました。**ズレを「マス何個ぶん」で持つようにしたので、
    // その計算ごと消えています**——テンポを渡す必要もありません。

    // **Phase 127：クオンタイズと同じく、トラックの全クリップ・タイムライン基準に。**
    // 「選択中のみ」も`selectedNotes`を見る（それまでは掴んでいる1つだけだった）
    pruneNoteSelection();


    forEachNote ([&] (const juce::ValueTree& noteState)
    {
        if (selectedOnly && ! isNoteSelected (noteState))
            return;

        Note note { juce::ValueTree (noteState) };

        const auto result = GrooveQuantise::applyToNote (project, grooveTemplate,
                                                          getNoteTimelineStart (noteState), note.getVelocity(),
                                                          gridDivision, strength);

        setNoteTimelineStart (noteState, result.startTime, &undoManager);
        note.setVelocity (result.velocity, &undoManager);
    });

    // 設計書1.3：どのグルーヴを当てたかを記録しておく（インスペクタでの表示用）
    editedTrack.setGrooveTemplateId (grooveTemplate.getId(), &undoManager);

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! hasTrack())
        return;

    grabKeyboardFocus();

    // 8.122：**レーンの上端を掴んで高さを変える**（Phase 157／改善案36）。
    //
    // **いちばん先に見ること。** この帯は鍵盤の見出しにもノートグリッドにも
    // またがっているので、後ろに置くとどちらかに先に取られる。
    // 4pxしかないので、他の操作を邪魔することはない
    if (! e.mods.isPopupMenu() && isOnLaneResizeEdge (e.getPosition()))
    {
        laneResizing = true;
        laneResizeStartScreenY = e.getScreenPosition().y;
        laneResizeStartHeight = laneHeight;
        return;
    }

    // 8.29の表：左端の見出し（鍵盤／ドラム行）の操作（Phase 69）
    if (handleKeyboardClick (e))
        return;

    //==========================================================================
    // 8.1のG5：下部のレーン（Phase 75／8.35）。
    //
    // **中身によって役割が変わるので、ここで振り分けます。**
    // ノートグリッドとは領域が重ならないが、判定を先にしておくほうが
    // 後から読んだときに迷わない（Phase 23からの並びを保っている）。

    auto laneBounds = getLaneBounds();

    if (laneBounds.contains (e.getPosition()))
    {
        // **見出し（左端）をクリックしたら中身の切り替えメニュー**（Phase 75）。
        // ツールバーの「CC Lanes」ボタンを廃止したぶんの入口
        if (getLaneHeaderBounds().contains (e.getPosition()))
        {
            showLaneTargetMenu (e.getScreenPosition());
            return;
        }

        if (! hasTrack())
            return;

        //----------------------------------------------------------------------
        // 8.37：**曲がり具合のつまみ**（Phase 77）。
        //
        // **点よりも後に判定すること。** つまみは区間のまんなかに出るので、
        // 点と重なることは少ないが、重なったときは**点のほうを優先**する
        // （点は動かせるが、つまみは曲げるだけ。取り違えたときの手戻りが大きい）。
        // **ペンのときは見ません**（Phase 78）。ペンはなぞって書く道具なので、
        // つまみの上から書き始められないと「線の上だけ描けない」ことになる
        if (! e.mods.isPopupMenu() && laneTarget.kind != LaneTarget::Kind::velocity
             && editTool != EditTool::pencil)
        {
            const bool isOnPoint = (laneTarget.kind == LaneTarget::Kind::cc)
                                      ? findCCEventAt (e.getPosition()).state.isValid()
                                      : (findAutomationPointAt (e.getPosition()) >= 0);

            const int segmentIndex = isOnPoint ? -1 : findCurveHandleAt (e.getPosition());

            if (segmentIndex >= 0)
            {
                const auto segment = getLaneSegments (laneBounds)[(size_t) segmentIndex];

                // ドラッグ1回ぶんをUndoの1ステップにする（3.1）。
                // つまみはドラッグ中に直接モデルへ書くので、区切りはここで作っておく
                project.beginAction (utf8 ("曲がり具合の変更"));

                curveDragOwner = segment.owner;
                curveDragFromY = segment.fromY;
                curveDragToY   = segment.toY;
                dragMode = DragMode::CurveHandle;
                dragStartPosition = e.getPosition();
                activeNoteState = juce::ValueTree();

                repaint();
                return;
            }
        }


        //----------------------------------------------------------------------
        // 8.43：消しゴム（Phase 83／C11）。**レーンの点も同じように消せる。**
        //
        // **矢印の処理より先に見ること。** 落とすと、消すつもりのクリックで
        // 点が増えます（矢印は空きをクリックすると点を置く。8.38）
        if (editTool == EditTool::eraser && ! e.mods.isPopupMenu()
             && laneTarget.kind != LaneTarget::Kind::velocity)
        {
            project.beginAction (utf8 ("レーンの点の削除"));
            eraseLanePointAt (e.getPosition());
            dragMode = DragMode::None;
            return;
        }

        //----------------------------------------------------------------------
        // 8.38：**ペンはなぞって書く**（Phase 78。8.29の表）。
        //
        // **区切りはここで1回だけ作る**（なぞるたびに履歴が積まれないように。3.1）。
        // ベロシティのなぞり書き（Phase 69）と同じ形にしてある
        if (editTool == EditTool::pencil && ! e.mods.isPopupMenu()
             && laneTarget.kind != LaneTarget::Kind::velocity)
        {
            project.beginAction (utf8 ("レーンのなぞり書き"));

            dragMode = DragMode::LanePaint;
            dragStartPosition = e.getPosition();
            lanePaintStarted = false;
            activeNoteState = juce::ValueTree();
            clearLaneSelection();

            paintLanePointAt (e.getPosition());
            return;
        }

        //----------------------------------------------------------------------
        // 仕様書5.3.3：CC（Phase 23）

        if (laneTarget.kind == LaneTarget::Kind::cc)
        {
            auto existing = findCCEventAt (e.getPosition());

            // 8.29の表：右クリックはメニュー（Phase 70でメニューにした）。
            // **それまでは「その場で削除」でした。** ノート（Phase 52）と同じ理由で、
            // 触れただけで消えるのをやめています
            if (e.mods.isPopupMenu())
            {
                // 点の上でなければ、レーンの中身を選び直すメニューを出す（Phase 75）
                if (! existing.state.isValid())
                    showLaneTargetMenu (e.getScreenPosition());
                else
                    showCCEventMenu (existing, e.getScreenPosition());

                return;
            }

            if (! existing.state.isValid())
            {
                // 8.38：**押しただけでは置きません**（Phase 78。8.29の表）。
                //
                // 矢印ツールのドラッグは**点の範囲選択**になったので、
                // ここで置いてしまうと、選ぶつもりのドラッグでも点が増える。
                // 置くのは「動かさずに離した」と分かる`mouseUp()`（ペンでのノート追加と同じ形）
                lanePendingAdd = true;
                lanePendingPosition = e.getPosition();
                laneRangeAnchor = e.getPosition();
                laneRangeBounds = { e.x, e.y, 0, 0 };
                dragMode = DragMode::None;
                activeNoteState = juce::ValueTree();
                return;
            }

            // 8.38：点の上を押したら、その点を選ぶ（Phase 78）。
            // **既に選ばれているなら選び直さない**：まとめて動かすつもりのドラッグで、
            // 掴んだ1つだけの選択に戻ってしまうのを防ぐ（ノートと同じ。8.13のA2）
            if (! isLanePointSelected (existing.state))
            {
                selectedLanePoints.clear();
                selectedLanePoints.push_back (existing.state);
            }

            draggedCCEvent = existing;
            dragMode = DragMode::CCPoint;
            dragStartPosition = e.getPosition();
            // **Phase 126：ドラッグ中の時刻はタイムライン基準**（ノートと揃えた）。
            // モデルへ書くのは離したときの1箇所だけ
            dragOriginalCCTime = getLanePointTime (existing.state);
            dragOriginalCCValue = existing.getValue();
            dragPreviewCCTime = dragOriginalCCTime;
            dragPreviewCCValue = dragOriginalCCValue;

            // 8.38：**いっしょに動く点の、掴んだ時点の位置を控えておく**（Phase 78）
            pruneLaneSelection();
            laneDragOthers.clear();
            laneDragAnchorTime = dragOriginalCCTime;
            laneDragAnchorValue = getLanePointNormalisedValue (existing.state);

            for (const auto& state : selectedLanePoints)
                if (state != existing.state)
                    laneDragOthers.push_back ({ state, getLanePointTime (state),
                                                 getLanePointNormalisedValue (state) });

            // ノート側の選択は解除する（Deleteキーの対象が紛らわしくなるため）
            activeNoteState = juce::ValueTree();

            repaint();
            return;
        }

        //----------------------------------------------------------------------
        // 仕様書5.6：オートメーション（Phase 75でピアノロールからも触れるようにした）

        if (laneTarget.kind == LaneTarget::Kind::automation)
        {
            const int pointIndex = findAutomationPointAt (e.getPosition());

            if (e.mods.isPopupMenu())
            {
                if (pointIndex >= 0)
                    showAutomationPointMenu (pointIndex, e.getScreenPosition());
                else
                    showLaneTargetMenu (e.getScreenPosition());

                return;
            }

            if (pointIndex < 0)
            {
                // 8.38：**押しただけでは置きません**（Phase 78。CCと同じ）。
                // 矢印ツールのドラッグは点の範囲選択になったので、
                // 置くのは「動かさずに離した」と分かる`mouseUp()`
                lanePendingAdd = true;
                lanePendingPosition = e.getPosition();
                laneRangeAnchor = e.getPosition();
                laneRangeBounds = { e.x, e.y, 0, 0 };
                dragMode = DragMode::None;
                activeNoteState = juce::ValueTree();
                return;
            }

            auto lane = getLaneForAutomation (false);   // 点があるのでレーンもある

            if (! lane.state.isValid())
                return;

            // 8.38：点の上を押したら、その点を選ぶ（Phase 78）。
            // **既に選ばれているなら選び直さない**（まとめて動かすため。8.13のA2）
            auto point = lane.getPoint (pointIndex);

            if (! isLanePointSelected (point.state))
            {
                selectedLanePoints.clear();
                selectedLanePoints.push_back (point.state);
            }

            // ドラッグ1回ぶんをUndoの1ステップにする（3.1）。
            // 点はドラッグ中に直接モデルへ書くので、区切りはここで作っておく
            project.beginAction (utf8 ("オートメーション点の移動"));

            draggedAutomationPoint = pointIndex;
            dragMode = DragMode::AutomationPoint;
            dragStartPosition = e.getPosition();

            // 点は**タイムライン上の時刻**（トラックの持ち物）。
            // CCイベント（クリップの中身の時刻）と取り違えないこと（8.28）
            dragPreviewAutomationTime = point.getTime();
            dragPreviewAutomationValue = point.getValue();

            // 8.38：**いっしょに動く点の、掴んだ時点の位置を控えておく**（Phase 78）
            pruneLaneSelection();
            laneDragOthers.clear();
            laneDragAnchorTime = point.getTime();
            laneDragAnchorValue = point.getValue();

            for (const auto& state : selectedLanePoints)
                if (state != point.state)
                    laneDragOthers.push_back ({ state, getLanePointTime (state),
                                                 getLanePointNormalisedValue (state) });

            activeNoteState = juce::ValueTree();

            repaint();
            return;
        }

        //----------------------------------------------------------------------
        // 8.29の表：ベロシティ（Phase 69で範囲選択となぞり書きを足した）

        // **右クリックはレーンの切り替えメニュー**（Phase 75）。
        // ベロシティそのものへの右クリックは表では「—」
        if (e.mods.isPopupMenu())
        {
            showLaneTargetMenu (e.getScreenPosition());
            return;
        }

        // ペン：**なぞって、通ったノートの強弱をまとめて書く**（Phase 69）。
        // **区切りはここで1回だけ作る**（なぞるたびに履歴が積まれないように。3.1）
        if (editTool == EditTool::pencil)
        {
            project.beginAction (utf8 ("ベロシティの変更"));
            dragMode = DragMode::VelocityPaint;
            dragStartPosition = e.getPosition();
            paintVelocityAt (e.getPosition());
            return;
        }

        const auto velocityNoteState = findVelocityBarAt (e.getPosition());

        if (velocityNoteState.isValid())
        {
            activeNoteState = velocityNoteState;
            dragMode = DragMode::Velocity;

            // **Phase 127：選んでいるノートは全部いっしょに動く**（ノートの移動と同じ）。
            // それまでは掴んだ1本しか変わらず、「全部選んで上下したのに1つだけ動く」
            // という状態でした。**掴んだものが選択に入っていなければ、それだけを選び直す**
            // （選んでいないものまで巻き込まないため。ノートの移動と同じ形）
            pruneNoteSelection();

            if (! isNoteSelected (velocityNoteState))
                setSingleNoteSelection (velocityNoteState);

            dragOriginalVelocity = Note (velocityNoteState).getVelocity();
            dragPreviewVelocity = dragOriginalVelocity;
            dragStartPosition = e.getPosition();

            repaint();
            return;
        }

        // 棒の無いところ（矢印ツール）はドラッグで範囲選択。
        // **選ぶ対象はノート**で、ベロシティの棒に触れたノートが選ばれる
        // （applyRangeSelection()がノートの矩形と棒の矩形の両方を見る）
        rangeSelecting = true;
        rangeSelectAnchor = e.getPosition();
        rangeSelectBounds = { e.x, e.y, 0, 0 };
        activeNoteState = juce::ValueTree();
        repaint();
        return;
    }

    const auto noteState = findNoteAt (e.getPosition());   // Phase 127：全クリップから探す
    const bool onGrid = (e.x > keyboardWidth && e.y < getGridHeight());

    //==========================================================================
    // 仕様書6.2：ツールごとの振る舞い（Phase 52）。
    // **矢印以外は、ここで完結して返す**（アレンジ画面と同じ方針。8.10）。
    // **Phase 69で範囲ツールを畳んだ**ので、残りはペンとカットだけ（8.29）。

    if (editTool != EditTool::arrow && ! e.mods.isPopupMenu() && onGrid)
    {
        if (editTool == EditTool::cut)
        {
            if (noteState.isValid())
                cutNoteAt (noteState, e.x);

            return;
        }

        // 8.43：消しゴム（Phase 83／C11）。**なぞった1回ぶんをUndoの1ステップ**にするため、
        // 区切りはここで作る（`mouseDrag`側では作らない。3.1）
        if (editTool == EditTool::eraser)
        {
            project.beginAction (utf8 ("ノートの削除"));
            eraseNoteAt (e.getPosition());
            dragMode = DragMode::None;
            return;
        }

        if (editTool == EditTool::pencil)
        {
            // 8.29の表：**ペンは「空いている場所をドラッグしたぶん」置く**（Phase 70で変更）。
            //
            // **クリックだけでは置きません。** Phase 69では1マスぶんを置いていましたが、
            // 「選ぶつもりで押したら増えていた」が起きるので、**動かしたときだけ**にしました。
            // 実際に置くのは`mouseDrag`（`pencilPendingAdd`）で、ここでは構えるだけです。
            if (! noteState.isValid())
            {
                // 8.121：**ドラムはクリックで置く**（Phase 156／改善案30）。
                //
                // 「動かしたときだけ置く」は、**置いたあと長さを決める**ための構えです。
                // ドラムに長さは要らない（▶しか描かない）ので、構える意味がありません。
                // 1音ずつ叩いて並べるのがドラムの打ち込みなので、**1クリック1音**にします。
                //
                // 置いたあとは掴んだ状態にしない（`DragMode::None`）——
                // 伸ばす先が無いのに、動かすと長さが変わってしまう
                if (drumMode)
                {
                    addNoteAt (e.x, e.y);
                    dragMode = DragMode::None;
                    return;
                }

                pencilPendingAdd = true;
                pencilPendingPosition = e.getPosition();
                activeNoteState = juce::ValueTree();
                dragMode = DragMode::None;
                return;
            }

            // **ノートの上では矢印と同じ**（選択・移動・左右の端で伸縮）。
            // ここでreturnせず下へ落とすので、処理は矢印と1本で済む
            // （ペン用にもう1つ書くと、片方だけ直し忘れる）
        }
    }

    // 8.29の表：空いているグリッドの右クリック（Phase 69・71）。
    //
    // **矢印ツール以外なら、まず矢印へ戻す。** ペンやカットのまま置き去りになると、
    // 次のクリックで意図しないものが増える／割れる。
    // **ツールバーはアレンジ画面にしかない**ので、ここが一番近い戻り道になる。
    if (e.mods.isPopupMenu() && ! noteState.isValid() && onGrid)
    {
        if (editTool != EditTool::arrow)
        {
            // **自分で切り替えない**（Phase 52の教訓）。アレンジ画面にも同じ値が流れる
            if (onEditToolSelected != nullptr)
                onEditToolSelected (EditTool::arrow);
            else
                setEditTool (EditTool::arrow);

            return;
        }

        showGridMenu (e);
        return;
    }

    if (noteState.isValid())
    {
        Note note { juce::ValueTree (noteState) };

        // 仕様書6.2：右クリックはメニュー（Phase 52）。
        // **その場で消さない。** 消すつもりが無いときに触れて消えるのを防ぐため、
        // メニューの「削除」を挟む
        if (e.mods.isPopupMenu())
        {
            showNoteMenu (noteState, e.getScreenPosition());
            return;
        }

        // Shift＋クリックは選択に足す／外す。
        // **Ctrl＋クリックも同じ**だが、そちらは「動かさずに離したとき」だけ。
        // Ctrlはドラッグでの複製にも使うので、掴んだ時点では決められない（mouseUpで判定）。
        if (e.mods.isShiftDown())
        {
            toggleNoteSelection (note.state);
            dragMode = DragMode::None;
            return;
        }

        // 既に複数選んでいて、その中の1つを掴んだときは選択を保つ
        // （まとめて動かす対象を、掴んだ瞬間に1つへ減らさない）
        if (! isNoteSelected (note.state))
            setSingleNoteSelection (note.state);

        activeNoteState = noteState;

        // 8.29の表：**ノートを掴むと、その音が鳴る**（Phase 71）。
        // どの音を触っているかが耳で分かる（ベロシティも実際の値で鳴らす）
        startPreview (note.getPitch(), note.getVelocity());

        // Ctrlを押しながらのドラッグは複製（Phase 52）。離すときに置く
        dragIsCopy = e.mods.isCommandDown();

        // 8.29の表：**左端でも伸縮できる**（Phase 69）。
        // **右端を先に見ること。** 短いノートでは両端の掴みしろが重なるので、
        // 先に見たほうが勝つ。右端（終わりを決める）のほうが使用頻度が高い
        auto bounds = getNoteBounds (note);

        // 8.161：**ドラムでは伸縮しない**（Phase 199）。
        //
        // Phase 156で▶の尾を落としてから、ドラム画面に**長さは描かれていません**。
        // それでも両端5pxは伸縮の掴みしろのままだったので、
        // ▶の先端あたり——ドラムでいちばん自然に掴む場所——を押すと
        // `ResizeLeft`に落ちていました。
        //
        // 掴んだ本人には長さが見えないので、
        // 「動かしたはずのノートが動かない」（始まりが`終わり-最短長`で止まる）
        // としか見えません。**Ctrl＋ドラッグの複製も`Move`のときだけ**なので、
        // まとめて複製したつもりが1つも増えない、という形でも出ます。
        //
        // **見えないものは掴ませない。** ドラムは常に移動にします
        // （長さはモデルに残っていて、ピアノロール表示で変えられます）
        if (drumMode)
            dragMode = DragMode::Move;
        else if (e.x >= bounds.getRight() - resizeGrabMargin)
            dragMode = DragMode::ResizeRight;
        else if (e.x <= bounds.getX() + resizeGrabMargin)
            dragMode = DragMode::ResizeLeft;
        else
            dragMode = DragMode::Move;

        // **Phase 126：ドラッグ中の時刻はタイムライン基準で持つ。**
        // 画面（`getNoteBoundsFor()`）も寄せ先（`snapTime()`）もタイムライン基準なので、
        // 中身の時刻のまま持つと、換算をあちこちに書き足すことになる。
        // モデルへ書くのは離したときの1箇所だけ（`mouseUp`）
        dragOriginalStartTime = getNoteTimelineStart (note.state);
        dragOriginalLength = note.getLength();
        dragOriginalPitch = note.getPitch();
        dragPreviewStartTime = dragOriginalStartTime;
        dragPreviewLength = dragOriginalLength;
        dragPreviewPitch = dragOriginalPitch;
        dragStartPosition = e.getPosition();
    }
    else if (! e.mods.isPopupMenu() && onGrid)
    {
        // 8.29の表：**矢印ツールの空きは、ドラッグで範囲選択**（Phase 69）。
        //
        // **Phase 68まで、ここは「クリックでノートを追加」でした。**
        // 追加はペンツールの仕事に寄せ、矢印は選ぶことに徹します
        // （範囲ツールを畳んだぶん、矢印でそのまま範囲が選べます）。
        //
        // 掴んだ時点ではクリックと区別できないので、構えだけ取ってmouseUpで振り分けます。
        activeNoteState = juce::ValueTree();
        dragMode = DragMode::None;
        rangeSelecting = true;
        rangeSelectAnchor = e.getPosition();
        rangeSelectBounds = { e.x, e.y, 0, 0 };
    }
    else
    {
        activeNoteState = juce::ValueTree();
        clearNoteSelection();   // Phase 52：空白をクリックしたら選択も外す
        dragMode = DragMode::None;
    }

    repaint();
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    // 8.122：レーンの高さ（Phase 157／改善案36）。
    //
    // **上へ引くと広がる**（掴んでいるのは上端なので、動かした向きと一致する）。
    // 測るのは**画面座標の差**：高さを変えると中身の高さも変わり、
    // ビューがスクロール位置を詰め直すことがあるので、部品の座標だとずれる
    if (laneResizing)
    {
        setLaneHeight (laneResizeStartHeight + (laneResizeStartScreenY - e.getScreenPosition().y));
        return;
    }

    // 8.43：消しゴムでなぞる（Phase 83／C11）。**通ったぶんだけ消える。**
    // 区切りは`mouseDown`で作ってあるので、**なぞり1回ぶんがUndoの1ステップ**になる（3.1）
    if (editTool == EditTool::eraser && ! e.mods.isPopupMenu())
    {
        if (getLaneBounds().contains (e.getPosition()))
            eraseLanePointAt (e.getPosition());
        else if (e.x > keyboardWidth && e.y < getGridHeight())
            eraseNoteAt (e.getPosition());

        return;
    }

    // 8.29の表：ペンで見出しをなぞると、通った行の音が鳴る（Phase 71）
    if (keyboardPreviewing)
    {
        if (e.x < keyboardWidth && e.y >= 0 && e.y < getNoteAreaHeight()
             && ! getLaneBounds().contains (e.getPosition()))   // Phase 76：固定レーンの下は見出しではない
            startPreview (yToPitch (e.y), 100);
        else
            stopPreview();   // 見出しの外へ出たら止める（鳴りっぱなしにしない）

        return;
    }

    // 8.29の表：ペンは**動かしてはじめてノートを置く**（Phase 70）。
    //
    // **置くのは1回だけ。** ここで`addNoteAt()`が右端を掴んだ状態にしてくれるので、
    // 以降は下の伸縮の処理がそのまま長さを決める（returnしないで落とすこと）。
    if (pencilPendingAdd)
    {
        pencilPendingAdd = false;
        addNoteAt (pencilPendingPosition.x, pencilPendingPosition.y);
    }

    // 8.29の表：鍵盤／ドラム行の見出しをなぞって、通った行のノートを選ぶ（Phase 69）
    if (keyboardSelecting)
    {
        keyboardSelectCurrentY = e.y;
        applyKeyboardSelection();
        return;
    }

    // 8.29の表：ペンでベロシティをなぞる（Phase 69）。
    // **通ったところを片端から書き換える。** 区切りはmouseDownで作ってあるので、
    // なぞり1回ぶんがUndoの1ステップになる（3.1）
    if (dragMode == DragMode::VelocityPaint)
    {
        paintVelocityAt (e.getPosition());
        return;
    }

    // 仕様書6.2：範囲選択（ラバーバンド）のドラッグ（Phase 52）
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

    // 8.38：ペンでのなぞり書き（Phase 78）。
    // **通ったところへ点を置いていく。** 区切りはmouseDownで作ってあるので、
    // なぞり1回ぶんがUndoの1ステップになる（3.1。ベロシティのなぞり書きと同じ）
    if (dragMode == DragMode::LanePaint)
    {
        paintLanePointAt (e.getPosition());
        return;
    }

    // 8.38：矢印ツールでレーンを押したあとのドラッグ（Phase 78）。
    // **動かしたと分かった時点で範囲選択に切り替える**（押しただけなら点を置く）
    if (lanePendingAdd || laneRangeSelecting)
    {
        lanePendingAdd = false;
        laneRangeSelecting = true;

        laneRangeBounds = juce::Rectangle<int>::leftTopRightBottom (
                              juce::jmin (laneRangeAnchor.x, e.x),
                              juce::jmin (laneRangeAnchor.y, e.y),
                              juce::jmax (laneRangeAnchor.x, e.x),
                              juce::jmax (laneRangeAnchor.y, e.y));
        repaint();
        return;
    }

    // 8.37：曲がり具合のつまみのドラッグ（Phase 77）。
    //
    // **上下だけ見ます**（横に動かしても曲がり具合は変わらない）。
    // 掴んだ時点の両端の高さで割って求めるので、**拡大していても同じ操作感**になる。
    // S字の区間を曲げたときは「曲線」へ変わる（動かしたとおりの形になるほうが分かりやすい）
    if (dragMode == DragMode::CurveHandle)
    {
        if (! curveDragOwner.isValid())
            return;

        setSegmentCurve (curveDragOwner, AutomationCurve::Linear,
                          AutomationCurveUI::amountForHandleDrag (curveDragFromY, curveDragToY,
                                                                   (float) e.getPosition().y));
        repaint();
        return;
    }

    // 仕様書5.3.3：CCの点のドラッグ（Phase 23）。
    // ノートの選択番号とは無関係なので、下の`activeNoteState`の判定より先に処理する。
    if (dragMode == DragMode::CCPoint)
    {
        if (! draggedCCEvent.state.isValid())
            return;

        const int controllerNumber = laneTarget.controllerNumber;
        auto laneBounds = getLaneBounds();

        dragPreviewCCTime = juce::jmax (0.0, xToTimelineTime (e.getPosition().x));
        dragPreviewCCValue = yToCCValue (laneBounds, controllerNumber, e.getPosition().y);

        repaint();
        return;
    }

    // 仕様書5.6：オートメーションの点のドラッグ（Phase 75）。
    // **点は掴んだ時点でモデルに置いてあります**（区切りも作ってある）ので、
    // ここでは直接書き換えます（アレンジ画面のオートメーションと同じ形）
    if (dragMode == DragMode::AutomationPoint)
    {
        auto lane = getLaneForAutomation (false);

        if (! lane.state.isValid() || ! juce::isPositiveAndBelow (draggedAutomationPoint, lane.getNumPoints()))
            return;

        auto laneBounds = getLaneBounds();

        // 点は**タイムライン上の時刻**（8.28）。**寄せません**：オートメーションと
        // CCの点はスナップの対象外と決めてある（8.14の「寄らないもの」）
        dragPreviewAutomationTime = juce::jmax (0.0, xToTimelineTime (e.getPosition().x));
        dragPreviewAutomationValue = yToAutomationValue (laneBounds, e.getPosition().y);

        auto point = lane.getPoint (draggedAutomationPoint);
        point.setTime (dragPreviewAutomationTime, &project.getUndoManager());
        point.setValue (dragPreviewAutomationValue, &project.getUndoManager());

        // **時刻を変えると並びが変わる**ので、番号を引き直す（1.32）
        draggedAutomationPoint = lane.state.indexOf (point.state);

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    if (dragMode == DragMode::None || ! hasActiveNote())
        return;

    const double deltaSeconds = (double) (e.getPosition().x - dragStartPosition.x) / pixelsPerSecond;

    if (dragMode == DragMode::Move)
    {
        // Phase 54：ノートもスナップに従う（8.14）。**寄せるのは移動後の位置**で、
        // 移動量ではない（移動量を寄せると、拍から外れたノートは外れたまま動く）。
        // まとめて動かすときのずらし量はここから計算されるので、
        // 選択中の他のノートも同じだけ動く（8.13のA2）
        dragPreviewStartTime = juce::jmax (0.0, project.snapTime (dragOriginalStartTime + deltaSeconds));
        dragPreviewPitch = yToPitch (e.getPosition().y);

        // 8.121：**動かしている最中も鳴らす**（Phase 156／改善案23）。
        //
        // 掴んだ瞬間には既に鳴らしています（8.29の表／Phase 71）。
        // **止まっていたのは行を移った後**で、耳では「どこへ動かしたか」が
        // 分かりませんでした。
        //
        // **鳴らしっぱなしにならないのは`startPreview()`のおかげ。**
        // 同じ音なら何もせず、違う音なら**前を止めてから**鳴らします。
        // 横だけ動かしているあいだは音が変わらないので、鳴り直しません
        {
            Note dragged { juce::ValueTree (activeNoteState) };
            startPreview (dragPreviewPitch, dragged.getVelocity());
        }

        // 8.121：**読み取りはカーソルの位置に出す**（Phase 156／改善案22）。
        // ノートの上に出すと、掴んでいる指の下（＝いちばん見えない場所）になる
        dragReadoutPosition = e.getPosition();
    }
    else if (dragMode == DragMode::ResizeRight)
    {
        // 伸縮も「終端の位置」を寄せる（クリップのトリムと同じ考え方）
        const double end = project.snapTime (dragOriginalStartTime + dragOriginalLength + deltaSeconds);
        dragPreviewLength = juce::jmax (minNoteLength, end - dragOriginalStartTime);
    }
    else if (dragMode == DragMode::ResizeLeft)
    {
        // 8.29の表：**左端の伸縮**（Phase 69）。**終わりは動かさない。**
        // 始まりだけを寄せて、そのぶん長さを増減させる（クリップのTrimLeftと同じ形）
        const double end = dragOriginalStartTime + dragOriginalLength;
        const double start = juce::jmin (project.snapTime (dragOriginalStartTime + deltaSeconds),
                                          end - minNoteLength);

        dragPreviewStartTime = juce::jmax (0.0, start);
        dragPreviewLength = end - dragPreviewStartTime;
    }
    else if (dragMode == DragMode::Velocity)
    {
        // 上へドラッグするとベロシティが上がる（Y座標は下向きが正のため符号を反転）
        const int deltaY = dragStartPosition.y - e.getPosition().y;
        const int deltaVelocity = (int) ((double) deltaY / (laneHeight - 8) * 127.0);
        dragPreviewVelocity = juce::jlimit (1, 127, dragOriginalVelocity + deltaVelocity);
    }

    repaint();
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& e)
{
    // 8.29の表：**離したら鳴っている音を止める**（Phase 71）。
    // **どの経路で離しても通るよう、いちばん先に置くこと。**
    // 下の分岐はどれもreturnで抜けるので、後ろに置くと止め損なう経路が出る
    stopPreview();
    keyboardPreviewing = false;

    // 8.122：レーンの高さ（Phase 157／改善案36）。
    // **覚えるのは離したときだけ**：ドラッグ中に呼ぶと、1回の操作で
    // 設定ファイルへ何十回も書くことになる
    if (laneResizing)
    {
        laneResizing = false;

        if (onLaneHeightChanged != nullptr)
            onLaneHeightChanged();

        return;
    }

    // 8.29の表：ペンで押しただけ（動かさなかった）なら**何も置かない**（Phase 70）。
    // 構えを畳むだけで、モデルには一度も触れていない
    if (pencilPendingAdd)
    {
        pencilPendingAdd = false;
        dragMode = DragMode::None;
        return;
    }

    // 8.38：ペンでのなぞり書き（Phase 78）。
    // 点は`paintLanePointAt()`が置いているので、ここでは**並べ直して**畳む。
    // **時刻順が崩れたままだと、値の読み出しも線の描画も意味を成さない**
    if (dragMode == DragMode::LanePaint)
    {
        if (laneTarget.kind == LaneTarget::Kind::cc)
        {
            editedTrack.sortCCEvents (&project.getUndoManager());
        }
        else
        {
            auto lane = getLaneForAutomation (false);

            if (lane.state.isValid())
                lane.sortPoints (&project.getUndoManager());
        }

        lanePaintStarted = false;
        dragMode = DragMode::None;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    // 8.38：矢印ツールでレーンを押したときの振り分け（Phase 78）。
    //
    // **動かさずに離したなら点を1つ置く。動かしたなら範囲選択。**
    // どちらも同じmouseDownから始まるので、離した時点で分ける（8.10と同じ形）
    if (lanePendingAdd || laneRangeSelecting)
    {
        const bool wasDragged = laneRangeSelecting;

        lanePendingAdd = false;
        laneRangeSelecting = false;

        if (wasDragged)
        {
            applyLaneRangeSelection();
        }
        else
        {
            auto laneBounds = getLaneBounds();
            clearLaneSelection();

            // **CCはクリップの持ち物なので、クリップが無ければ置けない**（Phase 127）。
            // オートメーションはトラックの持ち物なので、クリップが無くても置ける
            if (laneTarget.kind == LaneTarget::Kind::cc && hasTrack())
            {
                project.beginAction (utf8 ("CCイベントの追加"));

                // **Phase 126：CCの時刻は中身基準**なので、置く前に直す
                auto added = editedTrack.addCCEvent (laneTarget.controllerNumber,
                                               yToCCValue (laneBounds, laneTarget.controllerNumber,
                                                            lanePendingPosition.y),
                                               juce::jmax (0.0, xToTimelineTime (lanePendingPosition.x)),
                                               &project.getUndoManager());

                editedTrack.sortCCEvents (&project.getUndoManager());

                // **置いた点は選んでおく**（続けて動かす・消すことが多い。8.29）
                if (added.state.isValid())
                    selectedLanePoints.push_back (added.state);
            }
            else if (laneTarget.kind == LaneTarget::Kind::automation)
            {
                // **ここで初めてレーンを作ります。** 見るだけで作ると、
                // 触っていないパラメータのレーンが増えていく
                auto lane = getLaneForAutomation (true);

                if (lane.state.isValid())
                {
                    project.beginAction (utf8 ("オートメーション点の追加"));

                    // 点は**タイムライン上の時刻**（8.28）。**寄せません**（8.14）
                    auto added = lane.addPoint (juce::jmax (0.0, xToTimelineTime (lanePendingPosition.x)),
                                                 yToAutomationValue (laneBounds, lanePendingPosition.y),
                                                 &project.getUndoManager());

                    if (added.state.isValid())
                        selectedLanePoints.push_back (added.state);
                }
            }

            if (onModelChanged != nullptr)
                onModelChanged();
        }

        dragMode = DragMode::None;
        repaint();
        return;
    }

    // 8.37：曲がり具合のつまみ（Phase 77）。
    // モデルへはドラッグ中に書いてあるので、ここでは掴んだ状態を畳むだけ
    if (dragMode == DragMode::CurveHandle)
    {
        curveDragOwner = juce::ValueTree();
        dragMode = DragMode::None;
        repaint();
        return;
    }

    // 8.29の表：鍵盤／ドラム行の見出しでの選択（Phase 69）。
    // 選び終えているので、掴んだ状態を畳むだけ
    if (keyboardSelecting)
    {
        keyboardSelecting = false;
        repaint();
        return;
    }

    // 8.29の表：ペンでのベロシティのなぞり書き（Phase 69）。
    // モデルへは`paintVelocityAt()`が直接書いているので、ここでは畳むだけ
    if (dragMode == DragMode::VelocityPaint)
    {
        dragMode = DragMode::None;
        repaint();
        return;
    }

    // 仕様書6.2：範囲選択は、離した時点の矩形で選び直す（Phase 52）。
    //
    // **動かさずに離したなら、範囲選択ではなく「選択の解除＋カーソル移動」**
    // （Phase 69で振り分け、Phase 72でカーソル移動を足した。8.29の表）。
    // 空いている場所のクリックとドラッグは同じ`mouseDown`から始まるので、
    // ここで振り分けます（8.10と同じ形。アレンジ画面と同じ扱い）。
    if (rangeSelecting)
    {
        rangeSelecting = false;

        if (e.mouseWasDraggedSinceMouseDown())
        {
            applyRangeSelection();
        }
        else
        {
            clearNoteSelection();

            // 再生位置は曲の時刻。**Phase 126でX座標もタイムライン基準になった**ので、
            // 寄せてそのまま返せる
            if (onSeek != nullptr && e.x > keyboardWidth && e.y < getGridHeight())
                onSeek (project.snapTime (xToTimelineTime (e.x)));
        }

        repaint();
        return;
    }

    // 仕様書6.2：**Ctrl＋クリック（動かさずに離した）は、選択に足す／外す**。
    // Ctrlはドラッグでの複製にも使うので、掴んだ時点では区別できず、
    // 「動かしたかどうか」が分かるここで振り分ける。
    if (dragIsCopy && ! e.mouseWasDraggedSinceMouseDown()
         && hasActiveNote())
    {
        dragIsCopy = false;
        dragMode = DragMode::None;
        toggleNoteSelection (activeNoteState);
        return;
    }

    // 仕様書6.2：Ctrl＋ドラッグは「移動」ではなく「複製」（Phase 52）。
    // **元のノートは動かさず、落とした位置へ新しく置く。**
    // 掴んだ時点でCtrlが押されていたかで決まる（途中で離しても複製のまま）。
    if (dragIsCopy && dragMode == DragMode::Move && hasActiveNote())
    {
        dragIsCopy = false;
        duplicateSelectedNotesByDrag();
        dragMode = DragMode::None;
        repaint();
        return;
    }

    dragIsCopy = false;

    // 8.38：オートメーションの点を離したとき（Phase 78）。
    //
    // 掴んだ点はドラッグ中に書いてあるので、ここでは
    // **いっしょに動く点**と**並べ直し**をやる。
    // **並べ直しは必須**：時刻順が崩れたままだと、値の補間（`getValueAt`）が破綻する
    if (dragMode == DragMode::AutomationPoint)
    {
        auto lane = getLaneForAutomation (false);

        if (lane.state.isValid())
        {
            if (! laneDragOthers.empty())
                moveOtherSelectedLanePointsByDrag (dragPreviewAutomationTime - laneDragAnchorTime,
                                                    dragPreviewAutomationValue - laneDragAnchorValue);

            lane.sortPoints (&project.getUndoManager());
        }

        laneDragOthers.clear();
        draggedAutomationPoint = -1;
        dragMode = DragMode::None;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return;
    }

    // 仕様書5.3.3：CCの点を動かした結果をモデルへ書き戻す（Phase 23）
    if (dragMode == DragMode::CCPoint)
    {
        if (draggedCCEvent.state.isValid()
             && (dragPreviewCCTime != dragOriginalCCTime || dragPreviewCCValue != dragOriginalCCValue))
        {
            auto& undoManager = project.getUndoManager();

            // ドラッグ1回ぶんをUndoの1ステップにする（動かすたびに履歴が積まれないように）
            project.beginAction (utf8 ("CCイベントの編集"));

            // **Phase 126：控えている時刻はタイムライン基準**なので、中身の時刻へ直して書く
            draggedCCEvent.setTime (juce::jmax (0.0, dragPreviewCCTime), &undoManager);
            draggedCCEvent.setValue (dragPreviewCCValue, &undoManager);

            // 8.38：**選んでいる他の点も同じだけ動かす**（Phase 78）。
            // **掴んだ点を書いた後に呼ぶこと**（先に呼ぶと、残りの移動だけが
            // 前のUndoステップに入る。ノートと同じ。8.13のA2）
            moveOtherSelectedLanePointsByDrag (dragPreviewCCTime - laneDragAnchorTime,
                                                getLanePointNormalisedValue (draggedCCEvent.state)
                                                  - laneDragAnchorValue);

            // 左右へ動かすと時刻順が入れ替わり得る。並べ替えておかないと、
            // 値の読み出し（getCCValueAt）が意味を成さなくなる。
            editedTrack.sortCCEvents (&undoManager);

            if (onModelChanged != nullptr)
                onModelChanged();
        }

        draggedCCEvent = CCEvent (juce::ValueTree());
        draggedAutomationPoint = -1;
        laneDragOthers.clear();
        dragMode = DragMode::None;
        repaint();
        return;
    }

    if (dragMode != DragMode::None && hasActiveNote())
    {
        Note note { juce::ValueTree (activeNoteState) };
        auto& undoManager = project.getUndoManager();
        bool changed = false;

        if (dragMode == DragMode::Move)
        {
            if (dragPreviewStartTime != dragOriginalStartTime || dragPreviewPitch != dragOriginalPitch)
            {
                // ドラッグ1回ぶんをUndoの1ステップにする（区切らないと、
                // 起動以降の全編集がひとつにまとまってしまう）
                project.beginAction (utf8 ("ノートの移動"));

                // 8.91：**時刻を書くだけ**（Phase 131）。持ち主の付け替えも、
                // ValueTreeの作り直しも、選択の差し替えも要らなくなった
                setNoteTimelineStart (activeNoteState, dragPreviewStartTime, &undoManager);
                note.setPitch (dragPreviewPitch, &undoManager);

                // 仕様書6.2：**選んでいるノートは全部いっしょに動く**（Phase 52）。
                // **区切りを作った後・掴んだノートを動かした後に呼ぶこと。**
                // 先に呼ぶと、残りのノートの移動だけが前のUndoステップに入る
                if (selectedNotes.size() > 1)
                    moveOtherSelectedNotesByDrag();

                changed = true;
            }
        }
        else if (dragMode == DragMode::ResizeRight)
        {
            if (dragPreviewLength != dragOriginalLength)
            {
                // **ペンで置いた直後の伸ばしは、区切りを作らない**（Phase 69）。
                // 「置いた」と「伸ばした」が別々の履歴になると、
                // 1回の操作を戻すのにCtrl+Zが2回要る（3.1）
                if (! dragStartedFromPencil)
                    project.beginAction (utf8 ("ノートの長さ変更"));

                note.setLength (dragPreviewLength, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::ResizeLeft)
        {
            // 8.29の表：左端の伸縮（Phase 69）。**始まりと長さの両方**を書く
            if (dragPreviewStartTime != dragOriginalStartTime || dragPreviewLength != dragOriginalLength)
            {
                project.beginAction (utf8 ("ノートの長さ変更"));

                // 伸縮では持ち主を移さない（**始まりだけが動くので、掴んでいる感覚と合わせる**）
                setNoteTimelineStart (activeNoteState, dragPreviewStartTime, &undoManager);
                note.setLength (dragPreviewLength, &undoManager);
                changed = true;
            }
        }
        else if (dragMode == DragMode::Velocity)
        {
            if (dragPreviewVelocity != dragOriginalVelocity)
            {
                project.beginAction (utf8 ("ベロシティの変更"));
                note.setVelocity (dragPreviewVelocity, &undoManager);

                // **Phase 127：選んでいるノートも同じだけ上下する。**
                // それまで書いていたのは掴んだ1本だけで、
                // 「全部選んで上下したのに1つしか変わらない」状態でした。
                // **足すのは差分**（同じ値にすると、打ち込んだ強弱の形が潰れる）
                const int deltaVelocity = dragPreviewVelocity - dragOriginalVelocity;

                for (const auto& otherState : selectedNotes)
                {
                    if (otherState == activeNoteState || ! otherState.getParent().isValid())
                        continue;

                    Note other { juce::ValueTree (otherState) };
                    other.setVelocity (juce::jlimit (1, 127, other.getVelocity() + deltaVelocity), &undoManager);
                }

                changed = true;
            }
        }

        // 8.91：**クリップの長さを合わせ直す処理は要らなくなりました**（Phase 131）。
        // 「クリップの外へ出る」ということ自体が無い
        if (changed && onModelChanged != nullptr)
            onModelChanged();
    }

    dragMode = DragMode::None;
    dragStartedFromPencil = false;   // Phase 69：次のドラッグへ持ち越さない
    repaint();
}

bool PianoRollComponent::keyPressed (const juce::KeyPress& key)
{
    if (! hasTrack())
        return false;

    // 仕様書6.2：すべて選択／選択解除（Phase 52）。
    //
    // **ここで受けることに意味がある。** 同じCtrl+A／Ctrl+Dは
    // MainComponentにも登録してあり、そちらはクリップに効く。
    // JUCEはフォーカスのあるコンポーネントから順にキーを回すので、
    // **ピアノロールを触っている間はノートに、アレンジ画面を触っている間は
    // クリップに効く**という、期待どおりの振り分けが自動で起きる。
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAllNotes();
        return true;
    }

    if (key == juce::KeyPress ('d', juce::ModifierKeys::commandModifier, 0))
    {
        clearNoteSelection();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        // 8.38：**レーンの点を選んでいるならそちらを消す**（Phase 78）。
        // ノートより先に見る：レーンを触っているときにDeleteでノートが消えると、
        // 見ていない場所が壊れる（1.9の考え方）
        pruneLaneSelection();

        if (! selectedLanePoints.empty())
        {
            deleteSelectedLanePoints();
            return true;
        }

        // 複数選択されていればまとめて、1つだけならそれを消す
        pruneNoteSelection();

        if (! selectedNotes.empty())
        {
            deleteSelectedNotes();
            return true;
        }

        if (hasActiveNote())
        {
            project.beginAction (utf8 ("ノートの削除"));
            editedTrack.removeNote (Note (activeNoteState),
                                                               &project.getUndoManager());
            activeNoteState = juce::ValueTree();

            if (onModelChanged != nullptr)
                onModelChanged();

            repaint();
            return true;
        }
    }

    return false;
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::canvas);

    // 仕様書5.3.2：ドラムモードでは、鍵盤の代わりに名前付きの行を描く（Phase 25）
    if (drumMode)
    {
        for (int row = 0; row < drumMap.getNumEntries(); ++row)
        {
            auto entry = drumMap.getEntry (row);
            const int y = getYForDrumRow (row);   // 8.161：下ほど低い音（Phase 199）
            const bool isMuted = entry.isMuted();

            // ミュート中の行は、グリッド側もまとめて暗くして「鳴らない」ことを示す。
            // 見出しだけ変えると、打ち込んだのに音が出ない理由が分かりにくい（1.9の考え方）。
            if (isMuted)
            {
                g.setColour (AppColours::textSecondary.withAlpha (0.18f));
                g.fillRect (keyboardWidth, y, getWidth() - keyboardWidth, drumRowHeight);
            }
            else if ((row % 2) == 1)
            {
                // 行が多いので、1行おきに薄く塗って目で追えるようにする
                g.setColour (AppColours::canvasAlt.withAlpha (0.6f));
                g.fillRect (keyboardWidth, y, getWidth() - keyboardWidth, drumRowHeight);
            }

            // 見出し（パート名）
            g.setColour (isMuted ? AppColours::background : AppColours::panel);
            g.fillRect (0, y, keyboardWidth, drumRowHeight);
            g.setColour (AppColours::border);
            g.drawRect (0, y, keyboardWidth, drumRowHeight);

            g.setColour (isMuted ? AppColours::textSecondary : AppColours::textPrimary);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (entry.getPartName(), 3, y, keyboardWidth - 20, drumRowHeight,
                         juce::Justification::centredLeft);

            // チョークグループは番号だけ右端に小さく出す（設定されている行のみ）
            if (entry.getMuteGroup() > 0)
            {
                g.setColour (AppColours::orange);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText ("G" + juce::String (entry.getMuteGroup()),
                             keyboardWidth - 18, y, 16, drumRowHeight,
                             juce::Justification::centredRight);
            }

            g.setColour (AppColours::border.withAlpha (0.4f));
            g.drawHorizontalLine (y, (float) keyboardWidth, (float) getWidth());
        }
    }
    else
    {
    // 鍵盤と行の背景を描く
    for (int pitch = lowestPitch; pitch <= highestPitch; ++pitch)
    {
        const int y = pitchToY (pitch);
        const int noteInOctave = pitch % 12;

        // 黒鍵に当たる音（C#, D#, F#, G#, A#）は行の背景をわずかに暗くする
        const bool isBlackKey = (noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6
                                  || noteInOctave == 8 || noteInOctave == 10);

        if (isBlackKey)
        {
            g.setColour (AppColours::canvasAlt);
            g.fillRect (keyboardWidth, y, getWidth() - keyboardWidth, noteRowHeight);
        }

        // 左端の鍵盤表示
        g.setColour (isBlackKey ? AppColours::pianoBlackKey : AppColours::pianoWhiteKey);
        g.fillRect (0, y, keyboardWidth, noteRowHeight);
        g.setColour (AppColours::border);
        g.drawRect (0, y, keyboardWidth, noteRowHeight);

        // 8.29の表：音名／階名の切り替え（Phase 70。右クリックメニューから）。
        // **どの行に何を出すかは`getKeyboardLabel()`が決める**（音名はCの行だけ、
        // 階名はスケールの音だけ）。ここで条件を書き足さないこと
        if (const auto label = getKeyboardLabel (pitch); label.isNotEmpty())
        {
            g.setColour (AppColours::pianoBlackKey);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText (label, 2, y, keyboardWidth - 4, noteRowHeight,
                         juce::Justification::centredRight);
        }

        g.setColour (AppColours::border.withAlpha (0.4f));
        g.drawHorizontalLine (y, (float) keyboardWidth, (float) getWidth());
    }
    }

    // 仕様書5.3.1：構成音カラーリング（Phase 46）。
    // 行の地を描いた後、ノートより前に重ねる。
    if (! drumMode)
        drawNoteColouring (g);

    // 8.1のD5／G4：他トラックのノートの透かし（Phase 73）。
    // **塗りの後・自分のノートより前**に重ねる（自分のノートに隠れるのが正しい）
    drawNoteWatermark (g);

    // 8.29の表：鍵盤／ドラム行の見出しをなぞって選んでいる最中の目印（Phase 69）。
    // **なぞった行そのものを光らせる。** 選ばれたノートは画面の外にもあるので、
    // ノート側の枠だけでは「どこまで掴んでいるか」が分からない
    if (keyboardSelecting)
    {
        const int rowHeight = juce::jmax (1, getRowHeight());
        const int top = juce::jmin (keyboardSelectAnchorY, keyboardSelectCurrentY) / rowHeight * rowHeight;
        const int bottom = juce::jmax (keyboardSelectAnchorY, keyboardSelectCurrentY) / rowHeight * rowHeight + rowHeight;

        g.setColour (AppColours::purple.withAlpha (0.35f));
        g.fillRect (0, top, keyboardWidth, bottom - top);
        g.setColour (AppColours::purple);
        g.drawRect (0, top, keyboardWidth, bottom - top, 2);
    }

    // 縦グリッド線。**Phase 54でスナップの目盛りに合わせた**（8.14）。
    // それまでは1秒ごとで、拍ともスナップとも無関係な線だった。
    // **見えない刻みには寄せられない**ので、寄せ先そのものを線にしている。
    //
    // **Phase 67で、目盛りをタイムライン（曲の小節）基準へ移した。**
    // それまではクリップの中身の0秒から数えていたので、小節の頭から始まっていない
    // クリップでは、ルーラーの小節線と1つずつずれた場所に線が引かれることになる。
    // 寄せ先（`project.snapTime()`）も同じ基準（Phase 126で`snapContentTime()`は廃止）。
    //
    // フリーのときは拍で引く（線が消えると、どこに何があるか分からなくなる）。
    // 目盛りが細かすぎて線で埋まる場合は間引く（1/32を縮小表示したとき）。
    {
        // 8.98／Phase 139：**目盛りは拍の座標で刻む。**
        // 秒で刻むと、テンポが曲の途中で変わった先で拍から外れます。
        // **小節（bar）だけは別扱い**——拍で表せないため（`snapGridBeats()`）
        const auto snapGrid = project.getSnapGrid();
        const bool barGrid = (snapGrid == SnapGrid::bar);

        // フリーのときは拍で引く（線が消えると、どこに何があるか分からなくなる）
        const double gridBeats = barGrid ? 0.0
                                         : (snapGridBeats (snapGrid) > 0.0 ? snapGridBeats (snapGrid) : 1.0);

        const double leftTimeline = scrollStartSeconds;   // Phase 126：もともとタイムライン基準
        const double gridSeconds = barGrid ? project.getBarSecondsAt (leftTimeline)
                                           : project.getBeatSecondsAt (leftTimeline) * gridBeats;

        if (gridSeconds > 0.0 && gridSeconds * pixelsPerSecond >= 4.0)
        {
            const int lastX = getWidth();

            // 画面の左端に来ている目盛りから数え始める。
            // **0番から回さないこと**：拡大して先のほうを見ているとき、
            // 画面外の目盛りを何万回も空回りすることになる
            const int firstIndex = barGrid
                                     ? project.getBarIndexAt (leftTimeline)
                                     : juce::jmax (0, (int) std::floor (project.getBeatPositionAt (leftTimeline)
                                                                          / gridBeats));

            for (int i = firstIndex;; ++i)
            {
                const double time = barGrid ? project.getBarStartTime (i)
                                            : project.getTimeForBeatPosition (i * gridBeats);
                const int x = timelineTimeToX (time);

                if (x > lastX)
                    break;

                if (x < keyboardWidth)
                    continue;   // 鍵盤の下へは引かない

                // 小節の頭だけ濃くする。**小節の判定は誤差込みで行うこと**：
                // 1/32を24回足した値は、割り算では小節ちょうどにならない。
                // **半歩ぶん先へずらしてから小節・拍に分ける**（Phase 138までの
                // `fmod`と同じ考え方を、拍の座標へ移したもの）
                const auto shifted = project.getBarBeatAt (time + gridSeconds * 0.5);
                const bool isBarLine = barGrid
                                         || (shifted.beat == 0 && shifted.beatFraction < gridBeats);

                g.setColour (AppColours::border.withAlpha (isBarLine ? 0.8f : 0.35f));
                g.drawVerticalLine (x, 0.0f, (float) getGridHeight());
            }
        }
    }

    // **Phase 127：クリップが1つも無くてもグリッドは描く。**
    // ペンで置けばそこにクリップができる（8.87）ので、
    // 「クリップが無いから何も出さない」だと、作る手立てが画面から消える（1.9）
    if (! hasTrack())
    {
        g.setColour (AppColours::textSecondary);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (utf8 ("MIDIトラックが選ばれていません"), getLocalBounds(), juce::Justification::centred);
        return;
    }

    // 仕様書5.5：クリップの窓（オフセット〜オフセット+長さ）の外は鳴らない（Phase 22）。
    // ピアノロールは中身の全体を見せるので、**鳴らない範囲を灰色で伏せて**
    // 「ここに置いても音が出ない」ことが分かるようにする。
    // これが無いと、トリムした後に打ち込んでも音が出ない理由が分からない。
    //
    // **Phase 67：鍵盤の上へはみ出さないよう、必ず左端で切ること。**
    // 横スクロールできるようになったので、窓の境目が鍵盤より左へ来ることがある。
    // 切らずに描くと、灰色の膜と紫の線が鍵盤の上に乗る。
    // 8.91：**クリップの境目は描きません**（Phase 131）。
    // ピアノロールは曲を通しの1枚として見せるのが約束で、塊の区別はアレンジ画面の仕事

    // ノートを描く。
    //
    // **ドラッグ中も、元のノートはその場に描いたままにする**（Phase 53／8.1のA2）。
    // 以前はドラッグ中のノートだけをプレビュー位置へ描き替えていたため、
    // 掴んだ瞬間に元が消え、「どこから動かしたか」が分からなくなっていた。
    // モデルを書き換えるのは離したときなので、**描いている場所と実際の位置が
    // 食い違うのはプレビューのほうだけ**、という形に揃えている。
    //
    // **Phase 67：鍵盤の上に乗らないよう、ここから先は左端で切る。**
    // 横スクロールできるようになったので、左へはみ出したノートは
    // 鍵盤の上に描かれてしまう（それまでは鍵盤ごと横へ流れていたので起きなかった）。
    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (keyboardWidth, 0, juce::jmax (0, getWidth() - keyboardWidth), getHeight());

        // **Phase 127：トラックの全クリップのノートを1枚に描く**（8.87）
        forEachNote ([&] (const juce::ValueTree& noteState)
        {
            const bool isDragged = (dragMode != DragMode::None && noteState == activeNoteState);
            // Phase 52：複数選択されているものも「選択中」として描く
            const bool isSelected = (noteState == activeNoteState) || isNoteSelected (noteState);
            Note note { juce::ValueTree (noteState) };

            auto bounds = getNoteBounds (note);

            // ベロシティを色の濃さで表現する（仕様書5.3のベロシティ編集を視覚化）。
            // ベロシティのドラッグだけは**その場で濃さが変わる**のが手応えなので、
            // 位置は動かさずプレビュー値で描く。
            //
            // **Phase 127：選んでいるものは全部その場で濃さが変わる**
            // （離すまで1本しか変わらないと、まとめて動かした手応えが無い）
            const int velocity = (dragMode == DragMode::Velocity && (isDragged || isSelected))
                                     ? juce::jlimit (1, 127, note.getVelocity()
                                                              + (isDragged ? dragPreviewVelocity - note.getVelocity()
                                                                           : dragPreviewVelocity - dragOriginalVelocity))
                                     : note.getVelocity();
            const float alpha = 0.35f + 0.65f * ((float) velocity / 127.0f);

            // 8.61：**ノートはトラックの色**（Phase 99／改善案⑫。設計書2.4）。
            // アレンジ画面のクリップと同じ色になるので、どのトラックを開いているかが
            // 画面を切り替えても分かる。**選択の枠はパープルのまま**（設計書2.6）
            const auto trackColour = getTrackColour();
            const auto fillColour = trackColour.withAlpha (isSelected ? juce::jmin (1.0f, alpha + 0.15f)
                                                                       : alpha);
            const auto outlineColour = isSelected ? AppColours::purple : trackColour.darker (0.5f);

            // 8.60：**ドラムは▶で描く**（Phase 97／D13）。当たり判定は矩形のまま
            if (drumMode)
            {
                drawDrumNote (g, bounds, fillColour, outlineColour, isSelected ? 2.0f : 1.0f);
                return;
            }

            g.setColour (fillColour);
            g.fillRect (bounds);
            g.setColour (outlineColour);
            g.drawRect (bounds, isSelected ? 2 : 1);

            // 8.121：**ノートの中に音名を出す**（Phase 156／改善案21）。
            //
            // 鍵盤の見出しは**Cの行にしか**出ていないので、
            // 「いま見ているのが何の音か」を数えるのに目を左右させていました。
            //
            // **入りきらないときは出さない。** 小さく詰めると、
            // 読めない文字がノートの色を汚すだけになります。
            // 数え方は`midiNoteName()`に1本化してあります（1.27）
            if (bounds.getWidth() >= 24 && bounds.getHeight() >= 10)
            {
                // 塗りは半透明なので、**地に重ねた後の色**で明暗を決める。
                // 塗りだけで決めると、薄いノートで文字が沈む
                const auto flattened = AppColours::canvas.overlaidWith (fillColour);

                // **`Colour::contrasting()`は使わない。** 明るめの紫に対して
                // 白を返してきて、いちばん読みたい「選択中のノート」で読めなかった。
                // **黒か白かを明るさで決める**ほうが、どの色でも外さない
                g.setColour (flattened.getPerceivedBrightness() > 0.5f
                                 ? juce::Colours::black.withAlpha (0.85f)
                                 : juce::Colours::white.withAlpha (0.92f));

                g.setFont (juce::FontOptions ((float) juce::jlimit (8, 12, bounds.getHeight() - 2)));
                g.drawText (midiNoteName (note.getPitch()), bounds.reduced (3, 1),
                             juce::Justification::centredLeft, false);
            }
        });

        drawNoteDragPreview (g);
    }

    // 仕様書5.3.2：ドラムマップに無い音は行が無いので描かれない（Phase 25）。
    // **黙って消すと「打ち込んだはずの音が消えた」ように見える**ため、
    // 何音が隠れているかをその場で知らせる（1.9の「表示していない値」の考え方）。
    if (drumMode)
    {
        const int hidden = countNotesOutsideDrumMap();

        if (hidden > 0)
        {
            g.setColour (AppColours::orange);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (utf8 ("ドラムマップに無い音 ") + juce::String (hidden)
                           + utf8 (" 個は表示していません（ピアノロール表示で編集できます）"),
                         keyboardWidth + 6, getNoteAreaHeight() - 18, getWidth() - keyboardWidth - 12, 16,
                         juce::Justification::centredLeft);
        }
    }

    // 8.91：**「クリップの外のノート」という状態が無くなりました**（Phase 131）。
    // 伏せる理由も、数を知らせる必要も、まとめて消えています

    // 8.1のG5：下部のレーン（Phase 75）。**中身は1本ぶんだけ**（8.35）
    drawLane (g);

    // 仕様書5.9：再生カーソル（Phase 72）。**いちばん上に描く**
    // （ノートやレーンに隠れると、どこを鳴らしているのか分からない）。
    // **グリッドとレーンの全部を貫く**：ベロシティもCCも同じ時刻で読めるように
    {
        const int playheadX = timelineTimeToX (playheadSeconds);

        if (playheadX >= keyboardWidth && playheadX < getWidth())
        {
            g.setColour (AppColours::orange);
            g.fillRect (playheadX - 1, 0, 2, getHeight());
        }
    }

    // 仕様書6.2：範囲選択の矩形（Phase 52）。**いちばん上に描く**
    if (rangeSelecting && ! rangeSelectBounds.isEmpty())
    {
        g.setColour (AppColours::purple.withAlpha (0.15f));
        g.fillRect (rangeSelectBounds);
        g.setColour (AppColours::purple);
        g.drawRect (rangeSelectBounds, 1);
    }
}

//==============================================================================
// 仕様書5.3.3：CCレーンの描画（Phase 23）
//==============================================================================
//==============================================================================
// 8.1のG5：下部のレーンの描画（Phase 75／8.35）
//==============================================================================

void PianoRollComponent::drawLane (juce::Graphics& g)
{
    auto laneBounds = getLaneBounds();

    g.setColour (AppColours::panel);
    g.fillRect (laneBounds);
    g.setColour (AppColours::border);
    g.drawRect (laneBounds);

    // 8.122：**上端に握りの目印**（Phase 157／改善案36）。
    // カーソルは近づかないと変わらないので、**近づく理由**をここで出す。
    // 短い線を中央に3本——実機のつまみにならった形（`MixerLookAndFeel`と同じ考え方）
    {
        const int gripWidth = 26;
        const float centreX = (float) laneBounds.getCentreX();
        const float y = (float) laneBounds.getY();

        g.setColour (AppColours::textSecondary.withAlpha (0.55f));

        for (int i = -1; i <= 1; ++i)
            g.fillRect (centreX - (float) gripWidth * 0.5f,
                         y + (float) i * 2.0f - 0.5f,
                         (float) gripWidth, 1.0f);
    }

    // 仕様書5.5：ノートグリッドと同じく、鳴らない範囲を伏せる（Phase 22の考え方を踏襲）。
    // ここだけ伏せないと、「グリッドでは灰色なのにレーンには書ける」ように見えてしまう。
    //
    // 8.91：**伏せる範囲が無くなりました**（Phase 131）。CCもトラックの持ち物になり、
    // 「鳴らない場所」という区別が消えたため

    // 見出し（左端）。**押せるものだと分かるように「▾」を添える**（Phase 75）。
    // ここがレーンの中身を切り替える入口です（ツールバーの「CC Lanes」は廃止）
    {
        auto header = getLaneHeaderBounds();

        g.setColour (AppColours::background);
        g.fillRect (header);
        g.setColour (AppColours::border);
        g.drawRect (header);

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (9.0f));
        g.drawFittedText (getLaneTargetName() + " " + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")),
                           header.reduced (3, 2), juce::Justification::topLeft, 3);
    }

    // 中身。**時刻で位置が決まるものは、見出しの上に乗せない**（Phase 67と同じ）
    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (keyboardWidth, laneBounds.getY(),
                         juce::jmax (0, getWidth() - keyboardWidth), laneBounds.getHeight());

    if (! hasTrack())
        return;

    switch (laneTarget.kind)
    {
        case LaneTarget::Kind::automation: drawAutomationLaneContents (g, laneBounds); break;
        case LaneTarget::Kind::cc:         drawCCLaneContents (g, laneBounds);         break;
        case LaneTarget::Kind::velocity:
        default:                           drawVelocityLaneContents (g, laneBounds);   break;
    }

    // 8.38：矢印ツールでの範囲選択の枠（Phase 78）。**いちばん上に描く**
    if (laneRangeSelecting)
    {
        g.setColour (AppColours::orange.withAlpha (0.18f));
        g.fillRect (laneRangeBounds);
        g.setColour (AppColours::orange);
        g.drawRect (laneRangeBounds, 1);
    }
}

//==============================================================================
// 8.37：曲がり具合（Phase 77）
//==============================================================================

std::vector<PianoRollComponent::LaneSegment>
PianoRollComponent::getLaneSegments (const juce::Rectangle<int>& laneBounds)
{
    std::vector<LaneSegment> segments;

    if (! hasTrack())
        return segments;

    //--------------------------------------------------------------------------
    // 仕様書5.6：オートメーション。**点はタイムライン上の時刻**（8.28）

    if (laneTarget.kind == LaneTarget::Kind::automation)
    {
        auto lane = getLaneForAutomation (false);

        if (! lane.state.isValid())
            return segments;

        for (int i = 1; i < lane.getNumPoints(); ++i)
        {
            auto previous = lane.getPoint (i - 1);
            auto next = lane.getPoint (i);

            LaneSegment segment;
            segment.owner  = previous.state;   // 区間の形は手前の点が持つ
            segment.fromX  = (float) timelineTimeToX (previous.getTime());
            segment.fromY  = (float) automationValueToY (laneBounds, previous.getValue());
            segment.toX    = (float) timelineTimeToX (next.getTime());
            segment.toY    = (float) automationValueToY (laneBounds, next.getValue());
            segment.curve  = previous.getCurve();
            segment.amount = previous.getCurveAmount();

            segments.push_back (segment);
        }

        return segments;
    }

    //--------------------------------------------------------------------------
    // 仕様書5.3.3：MIDI CC。**こちらはクリップの中身の時刻**（8.28）。
    // **画面へ出すときは`getLanePointTime()`を通す**（Phase 126で基準を吸収させた）

    if (laneTarget.kind == LaneTarget::Kind::cc)
    {
        const int controllerNumber = laneTarget.controllerNumber;
        const int numEvents = editedTrack.getNumCCEventsFor (controllerNumber);

        for (int i = 1; i < numEvents; ++i)
        {
            auto previous = editedTrack.getCCEventFor (controllerNumber, i - 1);
            auto next = editedTrack.getCCEventFor (controllerNumber, i);

            if (! previous.state.isValid() || ! next.state.isValid())
                continue;

            LaneSegment segment;
            segment.owner  = previous.state;
            segment.fromX  = (float) timelineTimeToX (getLanePointTime (previous.state));
            segment.fromY  = (float) ccValueToY (laneBounds, controllerNumber, previous.getValue());
            segment.toX    = (float) timelineTimeToX (getLanePointTime (next.state));
            segment.toY    = (float) ccValueToY (laneBounds, controllerNumber, next.getValue());
            segment.curve  = previous.getCurve();
            segment.amount = previous.getCurveAmount();

            segments.push_back (segment);
        }
    }

    return segments;
}

int PianoRollComponent::findCurveHandleAt (juce::Point<int> position)
{
    auto segments = getLaneSegments (getLaneBounds());

    for (int i = 0; i < (int) segments.size(); ++i)
    {
        const auto& segment = segments[(size_t) i];

        if (! AutomationCurveUI::segmentHasHandle (segment.fromX, segment.fromY,
                                                    segment.toX, segment.toY, segment.curve))
            continue;

        const auto handle = AutomationCurveUI::getHandlePosition (segment.fromX, segment.fromY,
                                                                   segment.toX, segment.toY,
                                                                   segment.curve, segment.amount);

        if (position.toFloat().getDistanceFrom (handle) <= AutomationCurveUI::handleHitRadius)
            return i;
    }

    return -1;
}

void PianoRollComponent::drawCurveHandles (juce::Graphics& g, const juce::Rectangle<int>& laneBounds,
                                            juce::Colour colour)
{
    // **点を動かしている最中は出さない。** つまみはモデルの値から位置を決めているので、
    // 動かしている途中のプレビュー（まだ書いていない位置）とはズレる
    if (dragMode == DragMode::CCPoint || dragMode == DragMode::AutomationPoint)
        return;

    for (const auto& segment : getLaneSegments (laneBounds))
    {
        if (! AutomationCurveUI::segmentHasHandle (segment.fromX, segment.fromY,
                                                    segment.toX, segment.toY, segment.curve))
            continue;

        const auto handle = AutomationCurveUI::getHandlePosition (segment.fromX, segment.fromY,
                                                                   segment.toX, segment.toY,
                                                                   segment.curve, segment.amount);

        const bool isDragged = (dragMode == DragMode::CurveHandle
                                 && segment.owner == curveDragOwner);

        AutomationCurveUI::drawHandle (g, handle, colour, isDragged);
    }
}

void PianoRollComponent::setSegmentCurve (juce::ValueTree owner, AutomationCurve curve, float amount)
{
    if (! owner.isValid())
        return;

    auto& undoManager = project.getUndoManager();

    // **どちらの点かはValueTreeの種別で分かる。** 呼ぶ側に覚えさせない
    if (owner.hasType (IDs::CC))
    {
        CCEvent event (owner);
        event.setCurve (curve, &undoManager);
        event.setCurveAmount (amount, &undoManager);
    }
    else
    {
        AutomationPoint point (owner);
        point.setCurve (curve, &undoManager);
        point.setCurveAmount (amount, &undoManager);
    }

    if (onModelChanged != nullptr)
        onModelChanged();
}

//==============================================================================
// 8.38：レーンでのツール（Phase 78）
//==============================================================================

bool PianoRollComponent::eraseNoteAt (juce::Point<int> position)
{
    // 8.43：消しゴム（Phase 83／C11）。**触れた1つだけを消す。**
    // 選択中かどうかは見ません——消しゴムは「触れたものを消す」道具なので、
    // 選択に入っていない別のノートまで巻き込むと、狙って消せなくなります
    if (! hasTrack())
        return false;

    const auto noteState = findNoteAt (position);

    if (! noteState.isValid())
        return false;

    Note note { juce::ValueTree (noteState) };

    // 掴んだままのノートを残さない（消えたノートを動かし続けることになる）
    if (activeNoteState == noteState)
        activeNoteState = juce::ValueTree();

    // 選択にも残さない（`pruneNoteSelection()`が拾うが、その場で外しておく）
    if (isNoteSelected (noteState))
        toggleNoteSelection (noteState);

    editedTrack.removeNote (note, &project.getUndoManager());

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
    return true;
}

bool PianoRollComponent::eraseLanePointAt (juce::Point<int> position)
{
    // 8.43：レーンの点も同じように消せる（Phase 83）。
    // **ベロシティは消せません**（ノートの持ち物なので、消すという操作が無い）
    if (! hasTrack())
        return false;

    if (laneTarget.kind == LaneTarget::Kind::cc)
    {
        auto event = findCCEventAt (position);

        if (! event.state.isValid())
            return false;

        editedTrack.removeCCEvent (event, &project.getUndoManager());

        draggedCCEvent = CCEvent (juce::ValueTree());
        dragMode = DragMode::None;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return true;
    }

    if (laneTarget.kind == LaneTarget::Kind::automation)
    {
        const int pointIndex = findAutomationPointAt (position);
        auto lane = getLaneForAutomation (false);

        if (pointIndex < 0 || ! lane.state.isValid())
            return false;

        lane.removePoint (pointIndex, &project.getUndoManager());

        draggedAutomationPoint = -1;
        dragMode = DragMode::None;

        if (onModelChanged != nullptr)
            onModelChanged();

        repaint();
        return true;
    }

    return false;
}
void PianoRollComponent::clearLaneSelection()
{
    if (selectedLanePoints.empty())
        return;

    selectedLanePoints.clear();
    repaint();
}

bool PianoRollComponent::isLanePointSelected (const juce::ValueTree& pointState) const
{
    return std::find (selectedLanePoints.begin(), selectedLanePoints.end(), pointState)
             != selectedLanePoints.end();
}

void PianoRollComponent::pruneLaneSelection()
{
    // **親から外れたものは「消えた点」。** Undoやレーンの切り替えで起こる（1.32）
    selectedLanePoints.erase (std::remove_if (selectedLanePoints.begin(), selectedLanePoints.end(),
                                               [] (const juce::ValueTree& state)
                                               {
                                                   return ! state.isValid() || ! state.getParent().isValid();
                                               }),
                               selectedLanePoints.end());
}

double PianoRollComponent::getLanePointTime (const juce::ValueTree& pointState) const
{
    if (! pointState.isValid())
        return 0.0;

    // **CCはクリップの中身の時刻、オートメーションはタイムライン上の時刻**（8.28）。
    //
    // **Phase 126から、この対（`getLanePointTime`／`setLanePointTimeAndValue`）が
    // 基準の違いを吸収する。** Phase 125までは「持っている時刻をそのまま返す」形で、
    // 直すのは呼ぶ側の仕事だった——CCとオートメーションで直す／直さないが分かれるので、
    // 描画・当たり判定・選択・なぞり書きのそれぞれで判定を書くことになり、
    // 必ずどれかを取りこぼす（8.2）。**返す時刻はどちらもタイムライン基準**。
    // 8.137：**どちらもアクセサを通すこと**（Phase 175）。プロパティ名を直に読むと、
    // 保存の形が変わったとき（8.105の宿題3）にここだけ取り残されます（1.27）
    if (! pointState.hasType (IDs::CC))
        return AutomationPoint (pointState).getTime();

    return CCEvent (pointState).getTime();   // 8.91：CCも曲の時刻になった（Phase 131）
}

double PianoRollComponent::getLanePointBeats (const juce::ValueTree& pointState) const
{
    if (! pointState.isValid())
        return 0.0;

    // 8.139：`getLanePointTime()`の拍版（Phase 177）。**振り分けは同じ形**にしてあります
    // ——片方だけ種別を増やすと、必ず食い違います（1.27）
    if (! pointState.hasType (IDs::CC))
        return AutomationPoint (pointState).getTimeBeats();

    return CCEvent (pointState).getTimeBeats();
}

float PianoRollComponent::getLanePointNormalisedValue (const juce::ValueTree& pointState) const
{
    if (! pointState.isValid())
        return 0.0f;

    if (! pointState.hasType (IDs::CC))
        return (float) pointState[IDs::pointValue];

    // CCは0〜127（ピッチベンドは0〜16383）。**0〜1に直してから扱う**ので、
    // 「まとめて上げ下げ」がコントローラーの種類によらず同じ操作になる
    CCEvent event (pointState);
    const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (event.getControllerNumber()));

    return (float) event.getValue() / (float) maxValue;
}

void PianoRollComponent::setLanePointTimeAndValue (juce::ValueTree pointState, double timelineTime,
                                                    float normalisedValue)
{
    if (! pointState.isValid())
        return;

    auto& undoManager = project.getUndoManager();

    // **受け取るのはタイムライン上の時刻**（Phase 126）。CCだけ中身の時刻へ直す
    if (pointState.hasType (IDs::CC))
    {
        CCEvent event (pointState);
        const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (event.getControllerNumber()));
        event.setTime (juce::jmax (0.0, timelineTime), &undoManager);   // 8.91
        event.setValue ((int) std::lround (juce::jlimit (0.0f, 1.0f, normalisedValue) * (float) maxValue),
                         &undoManager);
        return;
    }

    AutomationPoint point (pointState);
    point.setTime (juce::jmax (0.0, timelineTime), &undoManager);
    point.setValue (juce::jlimit (0.0f, 1.0f, normalisedValue), &undoManager);
}

void PianoRollComponent::applyLaneRangeSelection()
{
    selectedLanePoints.clear();

    auto laneBounds = getLaneBounds();

    if (laneTarget.kind == LaneTarget::Kind::cc)
    {
        const int controllerNumber = laneTarget.controllerNumber;

        for (int i = 0; i < editedTrack.getNumCCEventsFor (controllerNumber); ++i)
        {
            auto event = editedTrack.getCCEventFor (controllerNumber, i);

            if (! event.state.isValid())
                continue;

            // **Phase 126：CCの時刻は中身基準**なので`getLanePointTime()`を通す
            const juce::Point<int> position (timelineTimeToX (getLanePointTime (event.state)),
                                              ccValueToY (laneBounds, controllerNumber, event.getValue()));

            if (laneRangeBounds.contains (position))
                selectedLanePoints.push_back (event.state);
        }
    }
    else if (laneTarget.kind == LaneTarget::Kind::automation)
    {
        auto lane = getLaneForAutomation (false);

        if (lane.state.isValid())
        {
            for (int i = 0; i < lane.getNumPoints(); ++i)
            {
                auto point = lane.getPoint (i);
                const juce::Point<int> position (timelineTimeToX (point.getTime()),
                                                  automationValueToY (laneBounds, point.getValue()));

                if (laneRangeBounds.contains (position))
                    selectedLanePoints.push_back (point.state);
            }
        }
    }

    repaint();
}

void PianoRollComponent::deleteSelectedLanePoints()
{
    pruneLaneSelection();

    if (selectedLanePoints.empty())
        return;

    project.beginAction (utf8 ("レーンの点の削除"));

    auto& undoManager = project.getUndoManager();

    // **控えを取ってから消すこと。** 消しながら選択を触ると、
    // 途中で入れ物が変わって残りを取りこぼす
    const auto points = selectedLanePoints;
    selectedLanePoints.clear();

    for (const auto& state : points)
    {
        if (! state.isValid() || ! state.getParent().isValid())
            continue;

        if (state.hasType (IDs::CC))
            editedTrack.removeCCEvent (CCEvent (state), &undoManager);
        else
            state.getParent().removeChild (state, &undoManager);
    }

    // 掴んだままの参照を残さない（消えた点を動かし続けることになる）
    draggedCCEvent = CCEvent (juce::ValueTree());
    draggedAutomationPoint = -1;
    dragMode = DragMode::None;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}

bool PianoRollComponent::copyLaneSelection (bool alsoDelete)
{
    pruneLaneSelection();

    if (selectedLanePoints.empty())
        return false;

    // **基準はいちばん early な点**（ノートと同じ。8.29）。
    // 貼り付け先の時刻にここからの差を足せば、選んだときの間隔がそのまま保たれる
    double referenceTime = std::numeric_limits<double>::max();

    for (const auto& state : selectedLanePoints)
        referenceTime = juce::jmin (referenceTime, getLanePointTime (state));

    // 8.139：**基準の拍**（Phase 177）
    const double referenceBeats = project.getBeatPositionAt (referenceTime);

    juce::Array<EditClipboard::Item> items;
    bool isCC = false;

    for (const auto& state : selectedLanePoints)
    {
        EditClipboard::Item item;
        item.state = state.createCopy();   // **複製を入れること**（8.29）
        item.timeOffset = getLanePointTime (state) - referenceTime;

        // 8.139：CCもオートメーションの点も拍で保存されています（Phase 177）
        item.beatOffset = getLanePointBeats (state) - referenceBeats;
        items.add (item);

        isCC = isCC || state.hasType (IDs::CC);
    }

    EditClipboard::set (isCC ? EditClipboard::Kind::ccEvents
                             : EditClipboard::Kind::automationPoints,
                         std::move (items));

    if (alsoDelete)
        deleteSelectedLanePoints();

    return true;
}

void PianoRollComponent::moveOtherSelectedLanePointsByDrag (double deltaTime, float deltaValue)
{
    for (const auto& origin : laneDragOthers)
    {
        if (! origin.state.isValid() || ! origin.state.getParent().isValid())
            continue;

        // **元の位置＋ずらし量**で書く（今の位置に足すと、ドラッグのたびに二重に動く）
        setLanePointTimeAndValue (origin.state, origin.time + deltaTime, origin.value + deltaValue);
    }
}

void PianoRollComponent::removeLanePointsInTimeRange (double previousTime, double newTime)
{
    // **ここだけは「モデルが持っている基準」の時刻を受け取る**（CCは中身、
    // オートメーションはタイムライン）。呼ぶのは`paintLanePointAt()`だけで、
    // そこで書き込む時刻と同じものを渡すため、揃えたほうが取り違えない。

    const double lowest  = juce::jmin (previousTime, newTime);
    const double highest = juce::jmax (previousTime, newTime);

    // **消すのは「あいだ」だけ。** 両端のうち手前側は、直前に自分で置いた点なので残す。
    // ここを「以上・以下」にすると、**左へなぞったときに置いたそばから消えていき**、
    // 最後の1点しか残らない（右へなぞるときだけ動いて見えるので、気づきにくい）。
    // 新しく置く位置に既にある点は、置き換えたいので消す。
    auto shouldRemove = [lowest, highest, newTime] (double time)
    {
        return (time > lowest && time < highest) || std::abs (time - newTime) < 1.0e-9;
    };

    auto& undoManager = project.getUndoManager();

    if (laneTarget.kind == LaneTarget::Kind::cc)
    {
        const int controllerNumber = laneTarget.controllerNumber;

        // 後ろから消す（前から消すと、消したぶんだけ後続の番号がずれる）
        for (int i = editedTrack.getNumCCEventsFor (controllerNumber) - 1; i >= 0; --i)
        {
            auto event = editedTrack.getCCEventFor (controllerNumber, i);

            if (event.state.isValid() && shouldRemove (event.getTime()))
                editedTrack.removeCCEvent (event, &undoManager);
        }

        return;
    }

    auto lane = getLaneForAutomation (false);

    if (! lane.state.isValid())
        return;

    for (int i = lane.getNumPoints() - 1; i >= 0; --i)
        if (shouldRemove (lane.getPoint (i).getTime()))
            lane.removePoint (i, &undoManager);
}

void PianoRollComponent::paintLanePointAt (juce::Point<int> position)
{
    auto laneBounds = getLaneBounds();
    const int x = juce::jmax (keyboardWidth + 1, position.x);

    // **細かすぎる点は置かない。** なぞった距離ぶんだけ点が増えると、
    // 見た目も鳴らす量も無駄に重くなる（8.38）
    if (lanePaintStarted && std::abs (x - lanePaintLastX) < lanePaintMinPixels)
        return;

    // **手ぶれをならす**（「滑らかな点」にするため）。
    // 生の値をそのまま置くと、線が細かくギザギザになる
    const float raw = (laneTarget.kind == LaneTarget::Kind::cc)
                         ? (float) yToCCValue (laneBounds, laneTarget.controllerNumber, position.y)
                             / (float) juce::jmax (1, MidiControllers::getMaxValue (laneTarget.controllerNumber))
                         : yToAutomationValue (laneBounds, position.y);

    lanePaintValue = lanePaintStarted ? lanePaintValue + (raw - lanePaintValue) * lanePaintSmoothing
                                       : raw;

    // **CCはクリップの中身の時刻、オートメーションはタイムライン上の時刻**（8.28）。
    // Phase 126でX座標がタイムライン基準になったので、直すのはCCのほうになった
    const double timelineTime = juce::jmax (0.0, xToTimelineTime (x));
    const double time = (laneTarget.kind == LaneTarget::Kind::cc)
                           ? timelineTime : timelineTime;   // 8.91：どちらも曲の時刻

    // なぞった範囲にあった点は消す。**残したまま足すと、元の形と混ざって暴れる**
    // （オートメーションの書き込み（`AutomationLane::writeValue`）と同じ考え方）
    // **最初の1点でも通す**：同じ場所に点があれば置き換えたい
    removeLanePointsInTimeRange (lanePaintStarted ? lanePaintLastTime : time, time);

    auto& undoManager = project.getUndoManager();

    if (laneTarget.kind == LaneTarget::Kind::cc)
    {
        if (! hasTrack())   // Phase 127：CCはクリップの持ち物
            return;

        const int maxValue = juce::jmax (1, MidiControllers::getMaxValue (laneTarget.controllerNumber));

        editedTrack.addCCEvent (laneTarget.controllerNumber,
                          (int) std::lround (juce::jlimit (0.0f, 1.0f, lanePaintValue) * (float) maxValue),
                          time, &undoManager);
    }
    else
    {
        auto lane = getLaneForAutomation (true);

        if (lane.state.isValid())
            lane.addPoint (time, juce::jlimit (0.0f, 1.0f, lanePaintValue), &undoManager);
    }

    lanePaintStarted = true;
    lanePaintLastX = x;
    lanePaintLastTime = time;

    if (onModelChanged != nullptr)
        onModelChanged();

    repaint();
}
void PianoRollComponent::drawVelocityLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds)
{
    // **Phase 127：トラックの全クリップのノートを並べる**（8.87）
    forEachNote ([&] (const juce::ValueTree& noteState)
    {
        Note note { juce::ValueTree (noteState) };
        // Phase 52：複数選択されているものも「選択中」として描く
        const bool isActive = (noteState == activeNoteState);
        const bool isSelected = isActive || isNoteSelected (noteState);
        const bool isDraggingThis = (isSelected && dragMode == DragMode::Velocity);

        auto bar = getVelocityBarBounds (note);

        if (isDraggingThis)
        {
            // **掴んだ1本は行き先の値、他の選択は同じ差分だけ動かして描く**（Phase 127）。
            // 離すまで1本しか動かないと、まとめて動かしている手応えが無い
            const int velocity = isActive
                                     ? dragPreviewVelocity
                                     : juce::jlimit (1, 127, note.getVelocity()
                                                              + (dragPreviewVelocity - dragOriginalVelocity));

            const int barHeight = (int) ((double) velocity / 127.0 * (laneBounds.getHeight() - 8));
            bar = { bar.getX(), laneBounds.getBottom() - barHeight - 4, bar.getWidth(), barHeight };
        }

        g.setColour (isSelected ? AppColours::orange : AppColours::purple);
        g.fillRect (bar);
    });
}

void PianoRollComponent::drawAutomationLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds)
{
    auto lane = getLaneForAutomation (false);

    if (! lane.state.isValid() || lane.getNumPoints() == 0)
    {
        // **空のレーンでも「何も無い」と分かるように書いておく**（1.9の考え方）。
        // オートメーションはトラックの持ち物なので、クリップが空でも点は置ける
        g.setColour (AppColours::textSecondary.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (utf8 ("クリックで点を置きます"),
                     laneBounds.reduced (keyboardWidth + 6, 0), juce::Justification::centredLeft, false);
        return;
    }

    // 8.36：**繋ぎ方（カーブ種別）に従って描きます**（Phase 76）。
    // それまでは直線で固定でしたが、右クリックメニューから種別を選べるようになったので、
    // 見た目も合わせています（線を引くのは`AutomationCurveUI`。Phase 77で3画面ぶん共用にした）
    juce::Path path;
    bool started = false;
    int previousX = keyboardWidth;
    int previousY = 0;

    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        auto point = lane.getPoint (i);
        const int x = timelineTimeToX (point.getTime());
        const int y = automationValueToY (laneBounds, point.getValue());

        if (! started)
        {
            path.startNewSubPath ((float) keyboardWidth, (float) y);
            started = true;
        }
        else
        {
            auto previous = lane.getPoint (i - 1);

            AutomationCurveUI::appendCurve (path, (float) previousX, (float) previousY,
                                             (float) x, (float) y,
                                             previous.getCurve(), previous.getCurveAmount());
        }

        previousX = x;
        previousY = y;
    }

    if (started)
    {
        // 最後の値は右端まで保たれる
        const int lastY = automationValueToY (laneBounds, lane.getPoint (lane.getNumPoints() - 1).getValue());
        path.lineTo ((float) getWidth(), (float) lastY);

        g.setColour (AppColours::purple.withAlpha (0.85f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        auto point = lane.getPoint (i);
        const int x = timelineTimeToX (point.getTime());
        const int y = automationValueToY (laneBounds, point.getValue());
        const bool isDragged = (dragMode == DragMode::AutomationPoint && i == draggedAutomationPoint);
        const bool isSelected = isLanePointSelected (point.state);   // Phase 78

        juce::Rectangle<int> dot (x - ccPointSize / 2, y - ccPointSize / 2, ccPointSize, ccPointSize);

        g.setColour (isDragged ? AppColours::orange : AppColours::purple);
        g.fillEllipse (dot.toFloat());

        // 8.38：**選んでいる点は輪郭を明るくする**（Phase 78）。
        // 塗りの色を変えると「ドラッグ中」と見分けがつかなくなる
        g.setColour (isSelected ? AppColours::textPrimary : AppColours::purple.darker (0.6f));
        g.drawEllipse (dot.toFloat(), isSelected ? 2.0f : 1.0f);
    }

    // 8.37：**曲がり具合のつまみ**（Phase 77）。点の後に描く（重なったら上に出す）
    drawCurveHandles (g, laneBounds, AppColours::purple);

    // ドラッグ中は値を数字でも出す（点の位置だけでは狙った値に合わせにくいため）。
    // **実際の値で出すこと**（VolumeならdB）。正規化値では何を合わせているのか分からない
    if (dragMode == DragMode::AutomationPoint)
    {
        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (AutomationTargets::formatValue (laneTarget.automationTargetId, dragPreviewAutomationValue),
                     timelineTimeToX (dragPreviewAutomationTime) + 8,
                     automationValueToY (laneBounds, dragPreviewAutomationValue) - 8,
                     70, 16, juce::Justification::centredLeft);
    }
}

void PianoRollComponent::drawCCLaneContents (juce::Graphics& g, const juce::Rectangle<int>& laneBounds)
{
    // 8.1のG5：**レーンに出しているコントローラーだけ**（Phase 75）。
    // 地・伏せ・見出し・クリップ領域の設定は`drawLane()`が済ませている
    const int controllerNumber = laneTarget.controllerNumber;
    const int numEvents = editedTrack.getNumCCEventsFor (controllerNumber);

    if (numEvents == 0)
    {
        g.setColour (AppColours::textSecondary.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (utf8 ("クリックで点を置きます"),
                     laneBounds.reduced (keyboardWidth + 6, 0), juce::Justification::centredLeft, false);
        return;
    }

    // 8.36：**オートメーションと同じ描き方**（Phase 76）。
    // Phase 75まではCCだけが階段状で固定でしたが、繋ぎ方を点が持つようになったので、
    // `AutomationCurveUI::appendCurve()`（オートメーションと共用）へ通します。
    // **再生側も同じ繋ぎ方で補間します**（`MidiPlayerProcessor`）。
    auto valueOf = [this] (const CCEvent& event)
    {
        return (dragMode == DragMode::CCPoint && event.state == draggedCCEvent.state)
                  ? dragPreviewCCValue : event.getValue();
    };

    // **返すのはタイムライン上の時刻**（Phase 126）。ドラッグ中の控えも同じ基準
    auto timeOf = [this] (const CCEvent& event)
    {
        return (dragMode == DragMode::CCPoint && event.state == draggedCCEvent.state)
                  ? dragPreviewCCTime : getLanePointTime (event.state);
    };

    juce::Path path;
    bool pathStarted = false;
    int previousX = keyboardWidth;
    int previousY = 0;

    for (int i = 0; i < numEvents; ++i)
    {
        auto event = editedTrack.getCCEventFor (controllerNumber, i);

        if (! event.state.isValid())
            continue;

        const int x = timelineTimeToX (timeOf (event));
        const int y = ccValueToY (laneBounds, controllerNumber, valueOf (event));

        if (! pathStarted)
        {
            // 8.96：**最初の点より前には線を引きません**（Phase 136）。
            //
            // Phase 135まで、左端（画面の端）からその値で引いていました。
            // これは**モデルとも再生とも食い違います**：
            // `Track::getCCValueAt()`は最初の点より前では**既定値**を返し、
            // `MidiPlayerProcessor`は**何も送りません**（プラグインは前の値のまま）。
            //
            // しかも起点が`keyboardWidth`＝**画面の座標**なので、
            // 横スクロールすると線の始まりが画面の端に貼り付いて動きます
            // ——「先頭の点が固定されているように見える」の正体。
            //
            // **オートメーションはこれで正しい**ので、そちらは変えていません：
            // `AutomationLane::getValueAt()`は最初の点より前を
            // 「その点の値のまま」と決めています（8.36）。**同じ見た目でも意味が違う**
            path.startNewSubPath ((float) x, (float) y);
            pathStarted = true;
        }
        else
        {
            auto previous = editedTrack.getCCEventFor (controllerNumber, i - 1);

            AutomationCurveUI::appendCurve (path, (float) previousX, (float) previousY,
                                             (float) x, (float) y,
                                             previous.getCurve(), previous.getCurveAmount());
        }

        previousX = x;
        previousY = y;
    }

    if (pathStarted)
    {
        path.lineTo ((float) getWidth(), (float) previousY); // 最後の値は右端まで保たれる

        g.setColour (AppColours::orange.withAlpha (0.85f));
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    // 点そのもの（掴める場所が分かるように、線より目立たせる）
    for (int i = 0; i < numEvents; ++i)
    {
        auto event = editedTrack.getCCEventFor (controllerNumber, i);

        if (! event.state.isValid())
            continue;

        const bool isDragged = (dragMode == DragMode::CCPoint && event.state == draggedCCEvent.state);
        const int x = timelineTimeToX (timeOf (event));
        const int y = ccValueToY (laneBounds, controllerNumber, valueOf (event));

        const bool isSelected = isLanePointSelected (event.state);   // Phase 78

        juce::Rectangle<int> point (x - ccPointSize / 2, y - ccPointSize / 2, ccPointSize, ccPointSize);

        g.setColour (isDragged ? AppColours::purple : AppColours::orange);
        g.fillEllipse (point.toFloat());

        // 8.38：**選んでいる点は輪郭を明るくする**（Phase 78。オートメーションと同じ）
        g.setColour (isSelected ? AppColours::textPrimary : AppColours::orange.darker (0.6f));
        g.drawEllipse (point.toFloat(), isSelected ? 2.0f : 1.0f);
    }

    // 8.37：**曲がり具合のつまみ**（Phase 77）。点の後に描く（重なったら上に出す）
    drawCurveHandles (g, laneBounds, AppColours::orange);

    // ドラッグ中は値を数字でも出す（点の位置だけでは狙った値に合わせにくいため）
    if (dragMode == DragMode::CCPoint)
    {
        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (dragPreviewCCValue),
                     timelineTimeToX (dragPreviewCCTime) + 8,
                     ccValueToY (laneBounds, controllerNumber, dragPreviewCCValue) - 8,
                     50, 16, juce::Justification::centredLeft);
    }
}
