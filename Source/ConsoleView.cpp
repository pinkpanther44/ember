#include "ConsoleView.h"
#include "AppColours.h"
#include "AudioEngine.h"
#include "Utf8.h"
#include "DragAndDropIds.h"

ConsoleView::ConsoleView (ProjectModel& projectToUse, SelectionState& selectionToUse,
                           AudioEngine& audioEngineToUse)
    : project (projectToUse), selection (selectionToUse), audioEngine (audioEngineToUse)
{
    // 8.301：選択に追従する（Phase 294／本人の要望）
    selection.addChangeListener (this);

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

    // **購読は先に外すこと**（1.15）。残したまま消えると、居なくなった自分へ通知が来ます
    selection.removeChangeListener (this);

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

void ConsoleView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    // 8.299：**トラックが増えたら、その場でストリップを出す**（Phase 292／本人の報告）。
    //
    // **器の型で絞ること。** ルートを購読しているので、
    // クリップやノートが増えたときもここへ来ます——絞らないと、
    // 打ち込むたびにConsole全体を作り直すことになります。
    //
    // **作り直しは非同期**（`triggerAsyncUpdate`）です。通知の途中で
    // ストリップを捨てると、**いま通知を配っている相手を消す**ことになります（1.15）。
    if (parent.hasType (IDs::TRACKS))
        triggerAsyncUpdate();
}

void ConsoleView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    // 消えたときも同じ（**消したトラックのストリップが残ると、
    // 触れるのに何も起きない**という形になります）
    if (parent.hasType (IDs::TRACKS))
        triggerAsyncUpdate();
}

void ConsoleView::handleAsyncUpdate()
{
    rebuildStrips();
}

void ConsoleView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // 8.301：**選択が変わったら、印を付け直すだけ**（Phase 294）。
    // 作り直しは要りません（並びも中身も変わっていない）
    updateSelectedStrip();
}

void ConsoleView::updateSelectedStrip()
{
    // **クリップやレーンを選んでいるときも、親のトラックが光ります。**
    // `getTrackId()`はどの種類の選択でも「どのトラックの話か」を返すので、
    // アレンジ画面でクリップを選んだときも、Consoleの同じトラックに印が付きます
    const auto trackId = selection.getTrackId();

    for (auto* strip : strips)
        strip->setSelected (trackId.isNotEmpty() && strip->getTrackId() == trackId);
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
    auto area = getLocalBounds().reduced (16, verticalPadding);

    // 説明ラベルは、ストリップが1本も無いときだけ場所を取る
    if (strips.isEmpty())
        emptyLabel.setBounds (area.removeFromTop (24));

    masterStrip.setBounds (area.removeFromRight (MasterStripComponent::stripWidth));
    area.removeFromRight (8);

    viewport.setBounds (area);

    // 8.300・8.302：**覚えている位置が出せないなら、出せるところまで詰める**
    // （Phase 293／本人の報告）。
    //
    // 詰めないと、覚えている値と画面に出ている高さが食い違ったままになり、
    // **掴んでも動かない**という形になります。
    //
    // **出せる高さがそもそも無いとき（畳んだも同然）は触りません。**
    // パネルをいちばん低くしただけで覚えている値が消えると、
    // 戻したときに元へ戻りません。
    if (isVisible() && viewport.getHeight() >= ChannelStripComponent::minimumConsoleHeight)
    {
        const int usable = getUsableFaderAreaHeight();

        // 8.302：**Phase 294までの設定から引き継ぐ**（Phase 295）。
        //
        // あちらは**ラックの高さ**で覚えていました。**いま画面に出ている
        // フェーダーの高さ**をそのまま書き移すので、**開き直したら別の高さだった**、
        // が起きません。ここでしかできないのは、**ストリップの高さが要る**ためです。
        if (! ConsoleLayout::hasRememberedFaderAreaHeight())
        {
            const int carried = usable - juce::jmin (ConsoleLayout::getLegacyRackAreaHeight(), usable);

            ConsoleLayout::setFaderAreaHeight (juce::jmax (ConsoleLayout::minimumFaderAreaHeight,
                                                            carried));
        }
        else if (ConsoleLayout::getFaderAreaHeight() > usable)
        {
            ConsoleLayout::setFaderAreaHeight (usable);
        }
    }

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

        // 8.295：音源GUIが出ているかどうか（Phase 288／改善案1）。
        // **窓は画面の外で閉じられます**（GUIの「×」）ので、こちらから見に行きます。
        // レイテンシと同じで、変わらなければ中で早期に戻ります
        strip->refreshInstrumentEditorState();
    }

    masterStrip.setLevels (audioEngine.getMasterLevel (0), audioEngine.getMasterLevel (1));
    masterStrip.refreshLatencyDisplay();
}

void ConsoleView::applyFaderAreaHeight (int newHeight)
{
    // 8.283：**覚えるのと配るのはここ1箇所**（Phase 276／本人の要望。`ConsoleLayout.h`）。
    //
    // ストリップに自分で覚えさせると、**トラックを足したときに新しい1本だけ既定の高さ**
    // になります（1.27の形）。値は`AppSettings`（設計書2.5）。
    //
    // 8.300：**画面に無い位置は覚えません**（Phase 293／本人の報告）。
    //
    // 上限が無かったので、引くたびに**届かない数字**が積み上がっていました
    // （本人の設定は647pxまで育っていました）。こうなると**掴んでも動きません**
    // ——差のぶんだけ逆へ引かないと戻ってきません。
    ConsoleLayout::setFaderAreaHeight (juce::jmin (newHeight, getUsableFaderAreaHeight()));

    // **マスターも同じ高さにすること。** 隣に並んでいるので、
    // ここだけ違うとメーターの行がずれます
    for (auto* strip : strips)
        strip->resized();

    masterStrip.resized();
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

        // 8.301：**押されたらそのトラックを選ぶ**（Phase 294／本人の要望）。
        // **IDで覚えること**——ストリップは並べ替えや追加で作り直されます（1.32）
        strip->onSelected = [this, trackId = track.getId()] { selection.selectTrack (trackId); };

        // 8.283：**どのストリップの境目を掴んでも、全部が同時に動く**（Phase 276／本人の要望）
        strip->onFaderAreaHeightDragged = [this] (int newHeight) { applyFaderAreaHeight (newHeight); };

        stripContainer.addAndMakeVisible (strip);
    }

    emptyLabel.setVisible (strips.isEmpty());

    // 8.301：**作り直したら、印も付け直すこと**（Phase 294）。
    // 選択は変わっていなくても、ストリップは新しいので**印は消えています**
    updateSelectedStrip();

    resized();
}
