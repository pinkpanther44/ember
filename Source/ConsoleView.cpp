#include "ConsoleView.h"
#include "AppColours.h"
#include "AudioEngine.h"
#include "Utf8.h"
#include "DragAndDropIds.h"

ConsoleView::ConsoleView (ProjectModel& projectToUse, AudioEngine& audioEngineToUse)
    : project (projectToUse), audioEngine (audioEngineToUse)
{
    emptyLabel.setJustificationType (juce::Justification::centredTop);
    emptyLabel.setFont (juce::FontOptions (14.0f));
    emptyLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    emptyLabel.setText (utf8 ("トラックがありません。アレンジ画面の「+ Track」で追加してください。"),
                         juce::dontSendNotification);
    addAndMakeVisible (emptyLabel);

    // トラックが増えると横に伸びるため、横スクロールできるようにしておく
    viewport.setViewedComponent (&stripContainer, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);

    // マスターは常に一番右へ固定表示する（トラックが増えてもスクロールで隠れないように）
    masterStrip.onMixerValueChanged = [this] { audioEngine.updateMixerSettings(); };
    addAndMakeVisible (masterStrip);

    updateProjectSubscription();
    rebuildStrips();
}

ConsoleView::~ConsoleView()
{
    stopTimer();

    // 8.67：待っている作り直しを取り消す（Phase 106）。
    // 残したまま消えると、居なくなった自分に対して呼ばれる
    cancelPendingUpdate();

    if (subscribedState.isValid())
        subscribedState.removeListener (this);
}

void ConsoleView::updateProjectSubscription()
{
    auto currentState = project.getState();

    if (currentState == subscribedState)
        return;

    if (subscribedState.isValid())
        subscribedState.removeListener (this);

    subscribedState = currentState;

    if (subscribedState.isValid())
        subscribedState.addListener (this);
}

void ConsoleView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    // ルートを購読しているのでノートの打ち込みまで通知が飛んでくる。
    // ここで種別を絞っているので、実質的な負荷は比較1回ぶんしかない。

    // 仕様書5.2.4：VCAのリンクが変わったとき（Phase 12d-2）
    if (property == IDs::vcaLinkedTrackIds)
    {
        for (auto* strip : strips)
            strip->refreshVcaAssignment();

        // リンクの増減はゲインに直結するので、エンジンへも反映する
        // （Undo/Redoでリンクが戻った場合もここを通る）
        audioEngine.updateMixerSettings();
        return;
    }

    // 仕様書5.7.2：サイドチェインの割り当てが変わったとき（Phase 12d-3）。
    // 設定操作そのものはAudioEngine側で配線まで済ませているが、
    // **Undo/Redoはモデルだけを戻す**ため、ここでグラフを追従させる必要がある。
    if (property == IDs::sidechainSourceTrackId)
    {
        audioEngine.rewireAllTrackConnections();

        for (auto* strip : strips)
            strip->refreshInsertSlots();
    }
}

void ConsoleView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // 8.67：トラックの並びが変わったら並べ直す（Phase 106／改善案㉛）。
    // **ルートを購読しているので、クリップの並べ替えまで通知が来る**。
    // 器の型で絞っているので、実質の負荷は比較1回ぶん
    if (parent.hasType (IDs::TRACKS))
        triggerAsyncUpdate();
}

void ConsoleView::handleAsyncUpdate()
{
    rebuildStrips();
}

void ConsoleView::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

//==============================================================================
// 8.67：ストリップを掴んでトラックを並べ替える（Phase 106／改善案㉛）
//==============================================================================

int ConsoleView::getReorderSlotForPosition (juce::Point<int> localPosition) const
{
    if (strips.isEmpty())
        return -1;

    // マスターの上は落とし先にしない（マスターは並びの外側で、動かせない）
    if (localPosition.x >= masterStrip.getX())
        return -1;

    // ストリップは`stripContainer`の中にいて、横スクロールでずれる。
    // **必ず座標を移し替えてから測ること**（見えている位置と中身の位置は別物）
    const int x = stripContainer.getLocalPoint (this, localPosition).x;

    // **ストリップの中央を境目にする。** 左半分にいれば「その前」、右半分なら「その後」
    for (int i = 0; i < strips.size(); ++i)
        if (x < strips[i]->getBounds().getCentreX())
            return i;

    return strips.size();
}

int ConsoleView::getReorderLineX (int slot) const
{
    if (strips.isEmpty())
        return 0;

    const int containerX = juce::isPositiveAndBelow (slot, strips.size())
                               ? strips[slot]->getX()
                               : strips.getLast()->getRight();

    return getLocalPoint (&stripContainer, juce::Point<int> (containerX, 0)).x;
}

void ConsoleView::paintOverChildren (juce::Graphics& g)
{
    if (reorderSlot < 0)
        return;

    // 設計書2.6：操作中の合図はオレンジ（アレンジ画面の並べ替えの予告線と同じ扱い）
    g.setColour (AppColours::orange);
    g.fillRect (getReorderLineX (reorderSlot) - 1, viewport.getY(), 2, viewport.getHeight());
}

bool ConsoleView::isInterestedInDragSource (const SourceDetails& details)
{
    return DragAndDropIds::isTrackDrag (details.description);
}

void ConsoleView::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void ConsoleView::itemDragMove (const SourceDetails& details)
{
    const int newSlot = getReorderSlotForPosition (details.localPosition);

    // 変わったときだけ描き直す（ドラッグ中は毎ピクセル呼ばれる）
    if (newSlot == reorderSlot)
        return;

    reorderSlot = newSlot;
    repaint();
}

void ConsoleView::itemDragExit (const SourceDetails&)
{
    if (reorderSlot < 0)
        return;

    reorderSlot = -1;
    repaint();
}

void ConsoleView::itemDropped (const SourceDetails& details)
{
    const int slot = getReorderSlotForPosition (details.localPosition);

    reorderSlot = -1;
    repaint();

    if (slot < 0 || strips.isEmpty())
        return;

    auto dragged = project.findTrackById (DragAndDropIds::getTrackId (details.description));

    if (! dragged.state.getParent().isValid())
        return;

    // Consoleの並び（一部のトラックしか出ない）から、**プロジェクトの並びへ移し替える**。
    // 予告線は「ここの**前**へ入る」を指しているので、
    // 落とし先のストリップが持っているトラックの位置がそのまま差し込み位置になる
    int insertBefore = project.getNumTracks();

    if (juce::isPositiveAndBelow (slot, strips.size()))
    {
        auto target = project.findTrackById (strips[slot]->getTrackId());

        for (int t = 0; t < project.getNumTracks(); ++t)
            if (project.getTrack (t).getId() == target.getId())
            {
                insertBefore = t;
                break;
            }
    }

    // 入れ先（フォルダ）は**すぐ左のトラックから受け継ぐ**（8.51と同じ決まり。
    // 深さを選ぶ余地はConsoleには無いので、そこだけアレンジ画面と違う）
    juce::String newParentFolderId;

    for (int above = insertBefore - 1; above >= 0; --above)
    {
        auto aboveTrack = project.getTrack (above);

        if (aboveTrack.getId() == dragged.getId())
            continue;   // 掴んでいるトラックは「左のトラック」に数えない

        newParentFolderId = (aboveTrack.getType() == TrackType::Folder)
                                ? aboveTrack.getId()
                                : aboveTrack.getParentFolderId();
        break;
    }

    // 行き先が自分の子孫なら入れない（輪になる。8.51）
    if (newParentFolderId.isNotEmpty() && ! project.canMoveTrackIntoFolder (dragged, newParentFolderId))
        newParentFolderId.clear();

    int from = -1;

    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getId() == dragged.getId())
        {
            from = t;
            break;
        }

    if (from < 0)
        return;

    // 「ここの前へ」を`moveChild`の最終位置へ直す。
    // **自分より右へ動かすときは1つ詰まる**（自分が抜けたぶん。8.66と同じ話）
    const int to = (insertBefore > from) ? insertBefore - 1 : insertBefore;

    project.moveTrackToSlot (dragged, to, newParentFolderId);

    // 並べ直しは`valueTreeChildOrderChanged()`が拾う
}
void ConsoleView::resized()
{
    // Phase 71：**見出しと上下の余白を詰めて、そのぶんストリップを高くしました**（8.32）。
    // 「Console」はパネルのヘッダー（`EditorPanel`）に出ているので、
    // 中にもう一度出す必要がありません。下部パネルは縦が限られるので
    // （8.27のC14と同じ話）、見出し28px＋間隔12pxは大きすぎました。
    auto area = getLocalBounds().reduced (16, 8);

    // 説明ラベルは、ストリップが1本も無いときだけ場所を取る
    if (strips.isEmpty())
        emptyLabel.setBounds (area.removeFromTop (24));

    masterStrip.setBounds (area.removeFromRight (MasterStripComponent::stripWidth));
    area.removeFromRight (8);

    viewport.setBounds (area);

    // ストリップは固定幅で横に並べる。器の幅を中身に合わせるとViewportが
    // 横スクロールを出してくれる。
    stripContainer.setSize (juce::jmax (viewport.getWidth(), strips.size() * ChannelStripComponent::stripWidth),
                             viewport.getHeight());

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStripComponent::stripWidth, 0,
                               ChannelStripComponent::stripWidth, stripContainer.getHeight());
}

void ConsoleView::visibilityChanged()
{
    // タブ切り替えで表示されるたびに、最新のトラック構成を反映する。
    // メーターの更新は、見えていないときに回しても無駄なので合わせて止める。
    if (isVisible())
    {
        rebuildStrips();
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void ConsoleView::timerCallback()
{
    // 並び順ではなくtrackIdでエンジンへ問い合わせる。UI側とエンジン側で
    // トラックの絞り込み条件がずれても、別トラックのメーターを表示してしまわない。
    for (auto* strip : strips)
    {
        strip->setLevels (audioEngine.getTrackLevel (strip->getTrackId(), 0),
                           audioEngine.getTrackLevel (strip->getTrackId(), 1));

        // 仕様書5.7.1：レイテンシもここで読み直す（Phase 12e）。
        // エンジンからのコールバックにしないのは、破棄済みのビューを掴む事故を
        // 持ち込まないため。値が変わらなければ中で早期に戻るので負荷は無い。
        strip->refreshLatencyDisplay();
    }

    masterStrip.setLevels (audioEngine.getMasterLevel (0), audioEngine.getMasterLevel (1));
    masterStrip.refreshLatencyDisplay();
}

void ConsoleView::refreshAfterProjectChanged()
{
    // プロジェクトを読み込むとルートのValueTreeが差し替わるので、購読も付け替える
    updateProjectSubscription();

    rebuildStrips();
    repaint();
}

void ConsoleView::mouseDown (const juce::MouseEvent& e)
{
    // 8.60：**空いているところの右クリックだけ**（Phase 97／改善案㉚）。
    //
    // ストリップの上はストリップ自身が受けるので、ここへは落ちてこない
    // （落ちてくるのは、並びの右側の余白やViewportの地の上を押したとき）
    if (! e.mods.isPopupMenu())
        return;

    if (onAddTrackRequested != nullptr)
        onAddTrackRequested ({ e.getScreenX(), e.getScreenY(), 1, 1 });
}

void ConsoleView::setPlayheadSeconds (double seconds)
{
    // 8.54：配るだけ（Phase 93）。**見えていなくても配る**——次に開いたときに
    // 古い位置の値のままだと、開いた瞬間だけフェーダーが飛んで見える
    for (auto* strip : strips)
        strip->setPlayheadSeconds (seconds);

    masterStrip.setPlayheadSeconds (seconds);
}

void ConsoleView::rebuildStrips()
{
    strips.clear();

    // プロジェクトが差し替わっている可能性があるので、マスターも繋ぎ直す
    masterStrip.refreshAfterProjectChanged();

    for (int i = 0; i < project.getNumTracks(); ++i)
    {
        auto track = project.getTrack (i);

        // オーディオを通すトラックだけを並べる。
        // MIDIトラックはPhase 14でトラックごとの音源割り当てに対応し、
        // フェーダー・インサート・センドが使えるようになったのでここに並ぶ
        // （AudioEngine::rebuildTrackNodes()の絞り込みと条件を合わせること）。
        const bool hasAudioPath = (track.getType() == TrackType::Audio
                                    || track.getType() == TrackType::Midi
                                    || track.getType() == TrackType::Send
                                    || track.getType() == TrackType::Folder     // 8.51（Phase 90）
                                    || track.getType() == TrackType::DrumOut);  // 8.143（Phase 181）

        // 仕様書5.2.4：VCAトラックは音声を通さないが、フェーダーを操作する必要があるので
        // ミキサーには並べる（設計書2.3.2のとおり、フェーダー以外を持たない簡易表示）。
        // エンジン側にノードが無いため、メーターは常に0のままになる点に注意。
        const bool isVcaTrack = (track.getType() == TrackType::VCA);

        if (! hasAudioPath && ! isVcaTrack)
            continue;

        auto* strip = strips.add (new ChannelStripComponent (track, project, audioEngine));
        strip->onMixerValueChanged = [this] { audioEngine.updateMixerSettings(); };
        stripContainer.addAndMakeVisible (strip);
    }

    emptyLabel.setVisible (strips.isEmpty());
    resized();
}
