#include "ChannelStripComponent.h"
#include "AppColours.h"
#include "Utf8.h"
#include "AudioEngine.h"
#include "DragAndDropIds.h"

ChannelStripComponent::ChannelStripComponent (const Track& trackToControl, ProjectModel& projectToUse,
                                                AudioEngine& audioEngineToUse, Layout layoutToUse)
    : track (trackToControl), project (projectToUse), audioEngine (audioEngineToUse),
      // 8.296：**ラックの見た目は置き場所に合わせる**（Phase 289）。
      // インスペクタでは幅に余裕があるので、見出し（「インサート」「センド」）が出ます
      rack (trackToControl, projectToUse, audioEngineToUse,
             layoutToUse == Layout::Inspector ? TrackRackComponent::Layout::Inspector
                                               : TrackRackComponent::Layout::Strip),
      isVca (trackToControl.getType() == TrackType::VCA),
      layout (layoutToUse)
{
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);

    // 8.67：**名前を掴む＝そのトラックを掴む**（Phase 106／改善案㉛）。
    // ラベルがマウスを食べてしまうと、ストリップでいちばん掴みやすい場所が
    // 「掴めない場所」になってしまう
    //
    // 8.162：**子は受け取れるようにする**（Phase 200／本人の要望）。
    // 2つめの引数を`true`にしておかないと、**名前の入力欄にも触れません**
    // （欄はラベルの子として出るため）。ラベル自身は今までどおり透明なので、
    // 名前を掴んでの並べ替えはそのまま効きます
    nameLabel.setInterceptsMouseClicks (false, true);

    // 8.296：**インスペクタでは並べ替えません**（Phase 289。`mouseDrag()`）。
    // 出来ないことを説明に書かないこと——**押して初めて違うと分かる**のがいちばん困ります
    nameLabel.setTooltip (layoutToUse == Layout::Inspector
                            ? utf8 ("ダブルクリックで名前を変更")
                            : utf8 ("ドラッグでトラックを並べ替え／ダブルクリックで名前を変更"));

    // 8.162：確定したら書く（Phase 200）。**空なら書かない**——
    // 名前の無いトラックは一覧でも書き出しでも指し示せなくなる
    nameLabel.onTextChange = [this] { commitNameEdit(); };

    addAndMakeVisible (nameLabel);

    // パンは-1.0（左）〜+1.0（右）。
    //
    // **ダブルクリックは中央へ戻す**（JUCE標準の動き）。数値入力は右クリック。
    // Phase 61で逆にしてみたが、使ってみて戻した（8.22）。
    panSlider.setRange (-1.0, 1.0, 0.01);
    panSlider.setDoubleClickReturnValue (true, 0.0);
    panSlider.setDefaultValueDescription (utf8 ("中央"));

    // 8.60：**表記は-100〜0〜100**（Phase 97／改善案⑥）。
    // モデルは-1〜+1のまま（変えると保存済みのプロジェクトが壊れる）。
    // **インスペクタと同じ単位にすること**：同じパンが画面で違う数字に見えると、
    // どちらが正しいのか確かめようがない
    panSlider.setDisplayUnit (ValueEntrySlider::DisplayUnit::panPercent);
    panSlider.setTooltip (utf8 ("パン（-100＝左、0＝中央、100＝右）。ダブルクリックで中央へ／右クリックでメニュー"));
    panSlider.setColour (juce::Slider::rotarySliderFillColourId, AppColours::purple);
    panSlider.onDragStart = [this]
    {
        project.getUndoManager().beginNewTransaction();

        // 仕様書5.6：Touch/Latchは「つまみに触れたか」で記録の開始を決める（Phase 20）
        audioEngine.beginAutomationTouch (track.getId(), AutomationTargets::pan);
    };
    panSlider.onDragEnd = [this] { audioEngine.endAutomationTouch (track.getId(), AutomationTargets::pan); };
    panSlider.onValueChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        track.setPan ((float) panSlider.getValue(), &project.getUndoManager());
        notifyChanged();
    };
    addAndMakeVisible (panSlider);

    // 8.63：**ノブの下に数値**（Phase 101／改善案⑦）。dB表示と同じ扱い（8.24）
    panReadout.setJustificationType (juce::Justification::centred);
    panReadout.setFont (juce::FontOptions (11.0f));
    panReadout.setColour (juce::Label::textColourId, AppColours::textSecondary);
    panReadout.setTooltip (utf8 ("クリックしてパン（-100〜100）を打ち込む／右クリックでメニュー"));
    panReadout.onLeftClick = [this] { panReadout.showEditor(); };
    panReadout.onRightClick = [this] { panSlider.showValueMenu(); };

    panReadout.onEditorShow = [this]
    {
        if (auto* editor = panReadout.getCurrentTextEditor())
        {
            editor->setText (juce::String (juce::roundToInt (track.getPan() * 100.0f)), false);
            editor->selectAll();
        }
    };

    panReadout.onTextChange = [this]
    {
        // 適用はスライダー側へ通す（Undoの区切りとTouch/Latchが1箇所に揃う）
        panSlider.applyTextValue (panReadout.getText());
        updateControlsFromModel();
    };

    addAndMakeVisible (panReadout);

    // フェーダーの範囲は -60〜+6dB。-60dBを「無音」として扱う（ClipPlayerProcessor側と対応）。
    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);

    volumeSlider.setDefaultValueDescription ("0 dB");
    volumeSlider.setTooltip (utf8 ("音量（dB）。ダブルクリックで0dB／右クリックでメニュー。"
                                    "数値は下のdB表示をクリックしても打ち込めます"));
    volumeSlider.setColour (juce::Slider::trackColourId, AppColours::purple);
    // ドラッグ1回ぶんをUndoの1ステップにまとめる（動かすたびに履歴が増えるのを防ぐ）
    volumeSlider.onDragStart = [this]
    {
        project.getUndoManager().beginNewTransaction();
        audioEngine.beginAutomationTouch (track.getId(), AutomationTargets::volume);
    };
    volumeSlider.onDragEnd = [this] { audioEngine.endAutomationTouch (track.getId(), AutomationTargets::volume); };
    volumeSlider.onValueChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        track.setVolumeDb ((float) volumeSlider.getValue(), &project.getUndoManager());
        notifyChanged();
    };
    addAndMakeVisible (volumeSlider);

    // 仕様書5.7：レベルメーター。フェーダーのすぐ横に縦向きで置く
    meter.setVertical (true);

    // 仕様書5.7：ピークのdB表示（Phase 59／8.1のC3）。**クリックでリセットできる**
    meter.setShowPeakText (true);
    addAndMakeVisible (meter);

    volumeValueLabel.setJustificationType (juce::Justification::centred);
    volumeValueLabel.setFont (juce::FontOptions (11.0f));
    volumeValueLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);

    // 仕様書5.7：**dB表示が音量の入口**（Phase 62で新設、Phase 63で操作を揃えた。8.24）。
    //
    // フェーダーは細長いので、つまみの上で数値入力しようとすると
    // **クリックした瞬間に値が動いて**しまう（`setSliderSnapsToMousePosition`）。
    // 数字が出ているところをそのまま書き換えるほうが素直。
    //
    // **クリックで打ち込み、右クリックでつまみと同じメニュー**（Phase 64／8.25）。
    // `setEditable()`を使わないのは、右クリックでも編集が始まってしまい、
    // そちらをメニューに使えなくなるため。
    volumeValueLabel.setTooltip (utf8 ("クリックして音量（dB）を打ち込む／右クリックでメニュー"));
    volumeValueLabel.onLeftClick = [this] { volumeValueLabel.showEditor(); };
    volumeValueLabel.onRightClick = [this] { volumeSlider.showValueMenu(); };

    // 開いた瞬間は**単位を外した数字だけ**にする（"-6.0 dB"のまま出すと消してから打つことになる）
    volumeValueLabel.onEditorShow = [this]
    {
        if (auto* editor = volumeValueLabel.getCurrentTextEditor())
        {
            editor->setText (juce::String (track.getVolumeDb(), 1), false);
            editor->selectAll();
        }
    };

    volumeValueLabel.onTextChange = [this]
    {
        // 適用はスライダー側へ通す（Undoの区切りとTouch/Latchが1箇所に揃う）
        volumeSlider.applyTextValue (volumeValueLabel.getText());
        updateControlsFromModel();   // 単位付きの表示へ戻す
    };

    addAndMakeVisible (volumeValueLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonOnColourId, AppColours::orange);
    muteButton.onClick = [this]
    {
        if (isUpdatingFromModel)
            return;

        project.getUndoManager().beginNewTransaction();
        track.setMuted (muteButton.getToggleState(), &project.getUndoManager());
        notifyChanged();
    };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    soloButton.onClick = [this]
    {
        if (isUpdatingFromModel)
            return;

        project.getUndoManager().beginNewTransaction();
        track.setSoloed (soloButton.getToggleState(), &project.getUndoManager());
        notifyChanged();
    };
    addAndMakeVisible (soloButton);

    // 仕様書5.3/5.7/5.2.2/5.2.4/5.6：ラック（Phase 29）。
    // 中身の出し分け（音源はMIDIのみ、VCAは持たない等）はラック側が判断する。
    rack.onMixerValueChanged = [this] { notifyChanged(); };
    rack.onPreferredHeightChanged = [this]
    {
        resized();

        // 8.296：**インスペクタでは自分の高さも変わります**（Phase 289）。
        // Consoleは枠の中でスクロールするので、外へ知らせる必要がありません
        if (layout == Layout::Inspector && onPreferredHeightChanged != nullptr)
            onPreferredHeightChanged();
    };

    // 仕様書5.7：ラックはスクロールできる枠に入れる（Phase 66／8.1のC14）。
    // **横スクロールは出さない**：ストリップの幅は固定で、ラックはその幅に合わせて
    // 高さを決める作りなので、横に溢れることは無い（出すと幅を食うだけ）。
    if (layout == Layout::Inspector)
    {
        // 8.296：**枠に入れず、そのまま置きます**（Phase 289／本人の指定）。
        // インスペクタ自身が縦にスクロールするので、枠の中でもう一度
        // スクロールさせる理由がありません（スクロールが二重になると、
        // どちらが動くのか押すまで分かりません）。
        //
        // **いちばん後ろへ置くこと。** ラックの矩形は「属性（全幅）」と
        // 「インサート・センド（右列）」の**両方を含む**ので、
        // 左列（フェーダー）と重なります。前に居ると、
        // **地の部分がフェーダーへのクリックを飲み込みます**
        addAndMakeVisible (rack);
        rack.toBack();
    }
    else
    {
        rackViewport.setViewedComponent (&rack, false);
        rackViewport.setScrollBarsShown (true, false);
        rackViewport.setScrollBarThickness (8);
        addAndMakeVisible (rackViewport);

        // 8.283：ラックとフェーダーの境目（Phase 276／本人の要望）。
        // **決めるのはConsoleView**（全ストリップへ同時に効かせるため）。
        //
        // 8.302：**動かすのはフェーダーの高さ**（Phase 295／本人の指定）。
        // 窓を伸ばしてもフェーダーは動かず、伸びたぶんはラックへ行きます
        faderResizer.setTooltip (utf8 ("ドラッグすると、フェーダーとメーターの高さが変わります\n"
                                       "（すべてのストリップに同じ高さが効きます）"));

        // 8.300：**掴む起点は、いま出ている高さ**（Phase 293／本人の報告）。
        // 覚えている値から掴むと、届かない数字に足され続けて**掴んでも動かなく**なります
        faderResizer.getCurrentHeight = [this] { return volumeSlider.getHeight(); };

        faderResizer.onHeightDragged = [this] (int newHeight)
        {
            if (onFaderAreaHeightDragged != nullptr)
                onFaderAreaHeightDragged (newHeight);
        };

        addAndMakeVisible (faderResizer);
    }

    // 設計書2.3.2：VCAトラックはフェーダー以外の要素を持たない簡易表示にする。
    // 音声を通さないため、パンとメーターはどちらも意味を持たない。
    if (isVca)
    {
        panSlider.setVisible (false);
        panReadout.setVisible (false);   // 8.63：ノブを隠すなら数値も（Phase 101）
        meter.setVisible (false);
    }

    // 仕様書5.7.1：このトラックのプラグインのレイテンシ（Phase 12e）
    latencyLabel.setJustificationType (juce::Justification::centred);
    latencyLabel.setFont (juce::FontOptions (10.0f));
    latencyLabel.setColour (juce::Label::textColourId, AppColours::orange);
    addChildComponent (latencyLabel); // 表示するかはrefreshLatencyDisplay()が決める

    track.state.addListener (this);
    updateControlsFromModel();
    refreshLatencyDisplay();
}

ChannelStripComponent::~ChannelStripComponent()
{
    track.state.removeListener (this);
}

void ChannelStripComponent::refreshLatencyDisplay()
{
    // VCAは音声を通さないので、レイテンシという概念が無い
    const int samples = isVca ? 0 : audioEngine.getTrackLatencySamples (track.getId());

    if (samples == lastShownLatencySamples)
        return; // 変わっていなければ何もしない（タイマーから毎回呼ばれるため）

    lastShownLatencySamples = samples;

    if (samples > 0)
    {
        const double sampleRate = audioEngine.getCurrentSampleRate();
        const double ms = sampleRate > 0.0 ? (samples * 1000.0 / sampleRate) : 0.0;

        latencyLabel.setText (juce::String (ms, 1) + " ms", juce::dontSendNotification);
        latencyLabel.setTooltip (utf8 ("このトラックのプラグインの遅延（仕様書5.7.1）: ")
                                   + juce::String (samples) + utf8 (" サンプル"));
    }

    latencyLabel.setVisible (samples > 0);

    // 8.295：**並べ直しは要りません**（Phase 288）。
    // 行は出ていても出ていなくても空けてあるので（`ConsoleLayout::latencyRowHeight`）、
    // 変わるのは文字の有無だけです——**出し入れでフェーダーの高さが動きません**
}

void ChannelStripComponent::refreshInstrumentEditorState()
{
    // 8.295：**MIDIトラックだけ**（Phase 288）。他の種類では窓そのものがありません
    const bool open = track.getType() == TrackType::Midi
                       && audioEngine.isTrackInstrumentEditorOpen (track.getId());

    if (open == instrumentEditorOpen)
        return;   // 変わっていなければ何もしない（タイマーから毎回呼ばれるため）

    instrumentEditorOpen = open;

    // **絵のところだけ描き直す**。ストリップ全部を塗り直すと、
    // 開け閉めのたびにフェーダーやラックまで巻き込みます
    repaint (typeIconBounds.expanded (2));
}

//==============================================================================
// 仕様書4.4・6章：ブラウザからのドラッグ&ドロップ（Phase 21）
//==============================================================================

void ChannelStripComponent::setSelected (bool shouldBeSelected)
{
    if (isSelected == shouldBeSelected)
        return;

    isSelected = shouldBeSelected;
    repaint();
}

void ChannelStripComponent::mouseDown (const juce::MouseEvent& e)
{
    // 8.301：**押されたら選ぶ**（Phase 294／本人の要望）。
    //
    // 本人の言葉：「Console上のトラックを選択することで、**Arrangeのトラックと
    // 連動して選択される**。Inspectorにも内容が表示される仕様」。
    //
    // **選択を持っているのは`SelectionState`**（設計書2.3.7）で、
    // アレンジ画面もインスペクタも**そこを見ています**
    // ——ここが書けば、両方とも勝手に付いてきます。
    //
    // **絵を押したときも選びます**（下で早期に戻る前に）。
    // 音源のGUIを出すなら、そのトラックを選んでおくほうが自然です。
    if (onSelected != nullptr)
        onSelected();

    // 8.295：**dB表示の右の絵で、音源のGUIを出す／しまう**（Phase 288／改善案1）。
    //
    // アレンジのトラックヘッダーと同じ働きです（本人の指定は「Consoleにも表示」
    // でしたが、**同じ絵が片方でだけ押せる**ほうが分かりにくいと判断しました）。
    //
    // フォルダの絵は押せません——Consoleには畳む／開くという状態がないためです。
    if (track.getType() != TrackType::Midi || ! typeIconBounds.contains (e.getPosition()))
        return;

    if (audioEngine.isTrackInstrumentEditorOpen (track.getId()))
    {
        audioEngine.closeTrackInstrumentEditor (track.getId());
        refreshInstrumentEditorState();
        return;
    }

    // 音源が挿さっていなければ何もしない。**すぐ下のラックにスロットがある**ので、
    // ここから選択ダイアログを出す必要はありません（出口を2つ作らない。8.12）
    if (! audioEngine.trackHasInstrument (track.getId()))
        return;

    audioEngine.openTrackInstrumentEditor (track.getId());
    refreshInstrumentEditorState();
}

void ChannelStripComponent::mouseDrag (const juce::MouseEvent& e)
{
    // 8.67：少し動かしたら並べ替えのドラッグを始める（Phase 106／改善案㉛）。
    // **すぐには始めない**：空きを押しただけで線が出ると、
    // 「押しただけなのに何か起きた」に見える（ラックのスロットと同じ決まり。8.66）
    // 8.296：**インスペクタでは並べ替えません**（Phase 289）。
    // 並びを持っているのはConsole（とアレンジ画面）で、
    // ここには「どこへ動かしたか」を受け取る相手が居ません
    if (layout == Layout::Inspector)
        return;

    if (e.getDistanceFromDragStart() <= 4)
        return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);

    if (container == nullptr || container->isDragAndDropActive())
        return;

    container->startDragging (DragAndDropIds::makeTrackDescription (track.getId()), this);
}

void ChannelStripComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    // 8.162：**名前の上のダブルクリックで書き換える**（Phase 200／本人の要望）。
    //
    // 本人の言葉は「Console上のトラック名上でもトラック名を変更できるようにしてほしい。
    // トラック名をダブルクリックで入力可能」。
    //
    // ラベルは並べ替えのためにマウスを受けないので、**ここへ落ちてきます。**
    // 名前の外を叩いたときは何もしません（ストリップの地は掴む場所なので、
    // うっかり入力欄が出ると「押しただけなのに何か起きた」になる）
    if (! nameLabel.getBounds().contains (e.getPosition()))
        return;

    nameLabel.showEditor();
}

void ChannelStripComponent::commitNameEdit()
{
    // **モデルから流れてきた更新では書き戻さない**（`setText`でもここへ来るため）。
    // 書き戻すと、色を変えただけでも「トラック名の変更」がUndoに積まれる
    if (isUpdatingFromModel)
        return;

    if (! track.state.getParent().isValid())
        return;

    const auto newName = nameLabel.getText().trim();

    // **空・同じ名前なら何もしない**（インスペクタの名前欄と同じ決まり）
    if (newName.isEmpty() || newName == track.getName())
    {
        nameLabel.setText (track.getName(), juce::dontSendNotification);
        return;
    }

    project.beginAction (utf8 ("トラック名の変更"));
    track.setName (newName, &project.getUndoManager());
}

void ChannelStripComponent::updateNameColours()
{
    // 8.162：**名前の板をトラックの色にする**（Phase 200／本人の要望）。
    //
    // アレンジ画面のヘッダー・クリップ・ピアノロールのノートは前からトラックの色です
    // （8.61）。Consoleだけ色を持っていなかったので、
    // **画面を移ると、どれが同じトラックか目で追い直す**ことになっていました。
    // **色として解釈するのはUI層の仕事**（設計書1.1。モデルは文字列で持っている）
    const auto colourString = track.getColourString();
    const auto trackColour = colourString.isEmpty() ? AppColours::purple
                                                     : juce::Colour::fromString (colourString);

    nameLabel.setColour (juce::Label::backgroundColourId, trackColour);

    // **文字は明暗で決める**（`Colour::contrasting()`は使わない）。
    // 24色の中には明るい黄色も濃い紺もあるので、どちらでも読める側を選ぶ
    // ——ピアノロールのノート名と同じ決め方（8.121）
    const auto textColour = trackColour.getPerceivedBrightness() > 0.5f
                                ? juce::Colours::black.withAlpha (0.85f)
                                : juce::Colours::white.withAlpha (0.92f);

    nameLabel.setColour (juce::Label::textColourId, textColour);

    // 入力欄も同じ色にしておく（開いた瞬間に板だけ白くなると、別のものに見える）
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, trackColour.darker (0.4f));
    nameLabel.setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    nameLabel.setColour (juce::Label::outlineWhenEditingColourId, AppColours::orange);
}

bool ChannelStripComponent::isInterestedInDragSource (const SourceDetails& details)
{
    return TrackRackComponent::canAcceptPluginDrag (audioEngine, track, details.description);
}

void ChannelStripComponent::itemDragEnter (const SourceDetails&)
{
    isDragOver = true;
    repaint();
}

void ChannelStripComponent::itemDragExit (const SourceDetails&)
{
    isDragOver = false;
    repaint();
}

void ChannelStripComponent::itemDropped (const SourceDetails& details)
{
    isDragOver = false;
    repaint();

    TrackRackComponent::handlePluginDrop (audioEngine, track, details.description,
                                           [this] { rack.refreshAll(); });
}

//==============================================================================
void ChannelStripComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // Undo/Redoや、他のUIからの変更でもフェーダー位置を合わせる
    updateControlsFromModel();
}

void ChannelStripComponent::updateControlsFromModel()
{
    // setValue()はonValueChangeを呼ぶため、そのままだとモデルへ書き戻してしまう。
    // dontSendNotificationでも良いが、ボタン側と扱いを揃えてフラグで一括して防ぐ。
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    nameLabel.setText (track.getName(), juce::dontSendNotification);
    updateNameColours();   // 8.162（Phase 200）。色を変えたときもここへ届く
    muteButton.setToggleState (track.isMuted(), juce::dontSendNotification);
    soloButton.setToggleState (track.isSoloed(), juce::dontSendNotification);

    // 8.54：フェーダーに出すのは**その再生位置で効いている値**（Phase 93）。
    // オートメーションが無いトラックでは`getVolumeDb()`と同じものが返る
    const float volumeDb = track.getEffectiveVolumeDbAt (playheadSeconds);
    const float pan = track.getEffectivePanAt (playheadSeconds);

    // 8.115：**オートメーションが握っているときは、それが分かるようにする**（Phase 150／改善案6）。
    //
    // 本人の報告「ConsoleのPanノブが効かない時がある。特にオーディオトラック」の正体は、
    // **Panのオートメーションレーンに点があること**でした。8.54の決めどおり、
    // つまみに出るのは「その再生位置で効いている値」なので、
    // **ノブを回してもレーンの値に戻されます**（音も同じ）。
    //
    // **動きは仕様どおりですが、画面にその手がかりが1つもありませんでした**（1.9）。
    // 「効かない」と見えるのはそのためです。
    const bool volumeAutomated = isParameterAutomated (AutomationTargets::volume);
    const bool panAutomated    = isParameterAutomated (AutomationTargets::pan);

    volumeSlider.setEnabled (! volumeAutomated);
    panSlider.setEnabled (! panAutomated);
    panReadout.setEnabled (! panAutomated);

    // **理由を出すこと。** 灰色にするだけだと「壊れている」と読めます
    const auto automatedTip = utf8 ("オートメーションが値を決めています。"
                                     "レーンの点を消すか、行の「B」でバイパスすると手で動かせます。");

    volumeSlider.setTooltip (volumeAutomated ? automatedTip : juce::String());
    panSlider.setTooltip (panAutomated ? automatedTip : juce::String());
    panReadout.setTooltip (panAutomated ? automatedTip : juce::String());

    // **掴んでいる最中のつまみは動かさない**（触っている人が優先）。
    // ここで書き換えると、指の下でつまみが逃げる
    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (volumeDb, juce::dontSendNotification);

    if (! panSlider.isMouseButtonDown())
        panSlider.setValue (pan, juce::dontSendNotification);

    volumeValueLabel.setText (volumeDb <= -60.0f ? utf8 ("-∞ dB")
                                                  : juce::String (volumeDb, 1) + " dB",
                               juce::dontSendNotification);

    // 8.63：パンの数値（Phase 101／改善案⑦）。
    // **打っている途中は書き換えない**（確定前の文字が消える。8.53と同じ話）
    if (panReadout.getCurrentTextEditor() == nullptr)
        panReadout.setText (juce::String (juce::roundToInt (pan * 100.0f)),
                             juce::dontSendNotification);

    repaint();
}


bool ChannelStripComponent::isParameterAutomated (const juce::String& targetId) const
{
    // 8.115：**「そのパラメータをレーンが握っているか」の判定はここ1箇所**（Phase 150）。
    //
    // 握っている＝**点が1つ以上あって、バイパスされていない**こと。
    // 点が0本のレーンは`Track::getEffectiveVolumeDbAt()`もフェーダーの値を返すので、
    // **レーンがあるだけでは握っていません**（作っただけのレーンで灰色にしない）。
    auto lane = track.findAutomationLane (targetId);

    return lane.state.isValid() && ! lane.isBypassed() && lane.getNumPoints() > 0;
}
void ChannelStripComponent::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (seconds, playheadSeconds))
        return;

    playheadSeconds = seconds;

    // **点が書かれていないトラックでは何もしない。**
    // 再生中は毎フレーム呼ばれるので、無関係なストリップまで作り直すと
    // トラックの本数ぶんだけ無駄が積み上がる。
    // 8.56：レーンは**開いただけで空のまま存在し得る**（Phase 94／D3）ので、
    // 「あるか」ではなく「点があるか」で見ること
    if (! track.findAutomationLane (AutomationTargets::volume).isEmpty()
         || ! track.findAutomationLane (AutomationTargets::pan).isEmpty())
        updateControlsFromModel();
}

void ChannelStripComponent::notifyChanged()
{
    if (onMixerValueChanged != nullptr)
        onMixerValueChanged();
}

void ChannelStripComponent::paint (juce::Graphics& g)
{
    // 8.296：**インスペクタでは板を敷きません**（Phase 289／本人の指定の絵）。
    //
    // Consoleでは**1本ずつの境目**を示すために板と枠が要りますが、
    // インスペクタに並ぶのは1本だけです。板を敷くと、
    // **上の「名前」「色」などと地の色が変わって、そこだけ別の画面に見えます**
    if (layout == Layout::Inspector)
    {
        drawTypeIcon (g);
        return;
    }

    auto area = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (AppColours::panel);
    g.fillRoundedRectangle (area, AppColours::corner (4.0f));

    // 仕様書5.2.2：センドトラックは地を少し変えて見分けやすくする（Phase 62／8.1のC6）。
    // **半透明を重ねる**ので、ライトでもダークでも隣との差が同じだけ付く（AppColours.h）
    // 8.143：**パラアウトの受け皿も同じ塗り**（Phase 181／改善案⑮。行の地と揃える）
    if (track.getType() == TrackType::Send || track.getType() == TrackType::DrumOut)
    {
        g.setColour (AppColours::sendTrackTint);
        g.fillRoundedRectangle (area, AppColours::corner (4.0f));
    }

    // 仕様書4.4：ドラッグ中の落とし先をパープルで示す（Phase 21）
    if (isDragOver)
    {
        g.setColour (AppColours::purple.withAlpha (0.20f));
        g.fillRoundedRectangle (area, AppColours::corner (4.0f));
    }

    // 8.301：**選ばれているストリップは、地をパープルに**（Phase 294／本人の要望）。
    //
    // **アレンジ画面のヘッダーと同じ塗り方**（`AppColours::purple`の0.18＋2pxの枠。
    // `drawTrackHeaderContents()`）——同じ「選ばれている」が、
    // 画面によって違う見え方をするのは避けます。
    //
    // **ドラッグ中の合図より下に塗ること。** 落とし先の合図は
    // 「いま何が起きているか」で、選択より優先して見えるべきです
    if (isSelected && ! isDragOver)
    {
        g.setColour (AppColours::purple.withAlpha (0.18f));
        g.fillRoundedRectangle (area, AppColours::corner (4.0f));
    }

    const bool outlined = isDragOver || isSelected;

    g.setColour (outlined ? AppColours::purple : AppColours::border);
    g.drawRoundedRectangle (area.reduced (0.5f), AppColours::corner (4.0f), outlined ? 2.0f : 1.0f);

    drawTypeIcon (g);
}

void ChannelStripComponent::drawTypeIcon (juce::Graphics& g)
{
    // 8.295：**トラックの種類を示す絵**（Phase 288／改善案1。本人の指定）。
    //
    // アレンジのトラックヘッダーと同じ絵・同じ色・同じ大きさです（`TrackTypeIcons`）。
    // **押せるのはMIDIだけ**ですが、**見た目では区別しません**（本人の指定）
    // ——Consoleにはフォルダを畳む場所もありません（`mouseDown()`）
    typeIcon.draw (g, typeIconBounds, track.getType(), instrumentEditorOpen);
}

int ChannelStripComponent::getPreferredHeight (int width)
{
    return layOutForInspector ({ 0, 0, width, 100000 }, false);
}

int ChannelStripComponent::layOutForInspector (juce::Rectangle<int> area, bool apply)
{
    // 8.296：**インスペクタの2列**（Phase 289／本人の指定の絵）。
    //
    //   ┌─────────────────┐
    //   │ Read                        │ ← ラックの「属性」側（全幅）
    //   │ VCA: -                      │
    //   │ 音源 ／ Orangutan Drums     │
    //   ├──────┬──────┤
    //   │  パン ◎      │ インサート  B │ ← 左＝フェーダー／右＝ラックの「信号」側
    //   │   0          │ Manta EQ     │
    //   │  [M] [S]     │ Manta Comp   │
    //   │  0.0 ms      │ + Insert     │
    //   │  ▮  ▮        │              │
    //   │  ▮  ▮        │ センド     B │
    //   │ -6.0 dB [絵] │ + Send       │
    //   ├──────┴──────┤
    //   │        トラック名            │ ← **1つだけ**（本人の指定）
    //   └─────────────────┘
    //
    // **測るときと置くときで同じ関数を通します**（`apply`だけが違う）。
    // 2つ書くと必ずずれます（`TrackRackComponent`と同じ作り）。
    const int top = area.getY();

    auto place = [apply] (juce::Component& c, juce::Rectangle<int> bounds)
    {
        if (apply)
            c.setBounds (bounds);
    };

    //--------------------------------------------------------------------------
    // ①ラックの「属性」側（書き込みモード・VCA・音源）を全幅で。
    //
    // **ラックの矩形は①と③の両方を含みます**（1つのコンポーネントなので）。
    // 中の置き場所は`setSplitAreas()`でローカル座標で渡します
    const int attributesHeight = rack.getAttributesHeight (area.getWidth());

    const int columnWidth = (area.getWidth() - inspectorColumnGap) / 2;
    const int chainHeight = rack.getChainHeight (columnWidth);

    //--------------------------------------------------------------------------
    // ②左の列に要る高さ。**出ていないものは数えません**（8.60と同じ）
    int leftFixed = 0;

    if (panSlider.isVisible())
        leftFixed += panKnobHeight + panReadoutHeight + rowGap;

    leftFixed += buttonRowHeight + rowGap;                // ミュート／ソロ
    leftFixed += ConsoleLayout::latencyRowHeight;         // 8.295：遅延（常に空ける）
    leftFixed += ConsoleLayout::volumeReadoutRowHeight;   // dB表示＋種類の絵

    // 8.297：**画面が低いときはフェーダーを縮めます**（Phase 290）。
    //
    // 下端に固定したので、**欲しい高さが必ず入るとは限りません**
    // （`InspectorPanel::resized()`が上へ残す高さを優先する）。
    //
    // `area`の高さは、測るときは十分大きく（10万）、置くときは実際の高さです。
    // **同じ式で両方を出せます**——測れば「欲しい高さ」、置けば「入る高さ」
    const int wantedColumnsHeight = juce::jmax (chainHeight, leftFixed + inspectorFaderHeight);

    const int availableForColumns = area.getHeight()
                                      - attributesHeight
                                      - (attributesHeight > 0 ? inspectorColumnGap : 0)
                                      - 6 - 20;   // 下の余白とトラック名

    const int columnsHeight = juce::jlimit (leftFixed + inspectorMinimumFaderHeight,
                                             wantedColumnsHeight,
                                             availableForColumns);

    //--------------------------------------------------------------------------
    // ③置く

    const int rackTop = area.getY();

    auto attributesArea = area.removeFromTop (attributesHeight);

    if (attributesHeight > 0)
        area.removeFromTop (inspectorColumnGap);

    auto columnsArea = area.removeFromTop (columnsHeight);
    auto leftColumn = columnsArea.removeFromLeft (columnWidth);
    auto chainArea = columnsArea.removeFromRight (columnWidth);

    if (apply)
    {
        // **ラックは①と③を含む矩形**。中の位置はローカル座標で渡す
        const juce::Rectangle<int> rackBounds { area.getX(), rackTop, area.getWidth(),
                                                 columnsArea.getBottom() - rackTop };

        rack.setBounds (rackBounds);
        rack.setSplitAreas (attributesArea.withPosition (attributesArea.getPosition()
                                                           - rackBounds.getPosition()),
                             chainArea.withPosition (chainArea.getPosition()
                                                       - rackBounds.getPosition()));
    }

    //--------------------------------------------------------------------------
    // ④左の列（Consoleと同じ順番）

    if (panSlider.isVisible())
    {
        place (panSlider, leftColumn.removeFromTop (panKnobHeight));
        place (panReadout, leftColumn.removeFromTop (panReadoutHeight));
        leftColumn.removeFromTop (rowGap);
    }

    auto buttonRow = leftColumn.removeFromTop (buttonRowHeight);
    place (muteButton, buttonRow.removeFromLeft (buttonRow.getWidth() / 2).reduced (2, 0));
    place (soloButton, buttonRow.reduced (2, 0));
    leftColumn.removeFromTop (rowGap);

    place (latencyLabel, leftColumn.removeFromTop (ConsoleLayout::latencyRowHeight));

    auto valueRow = leftColumn.removeFromBottom (ConsoleLayout::volumeReadoutRowHeight);

    auto iconBounds = valueRow.removeFromRight (typeIconSize)
                               .withSizeKeepingCentre (typeIconSize, typeIconSize);

    if (apply)
        typeIconBounds = iconBounds;

    valueRow.removeFromRight (3);
    place (volumeValueLabel, valueRow);

    if (meter.isVisible())
    {
        place (meter, leftColumn.removeFromRight (28));
        leftColumn.removeFromRight (4);
    }

    place (volumeSlider, leftColumn);

    //--------------------------------------------------------------------------
    // ⑤名前は**1つだけ**、いちばん下に全幅で（本人の指定「ラベルは1つに結合」）

    area.removeFromTop (6);
    place (nameLabel, area.removeFromTop (nameRowHeight));

    return area.getY() - top;
}

void ChannelStripComponent::resized()
{
    // 8.296：インスペクタは2列（Phase 289／本人の指定）
    if (layout == Layout::Inspector)
    {
        layOutForInspector (getLocalBounds(), true);
        return;
    }

    auto area = getLocalBounds().reduced (stripMargin);

    // 設計書2.3.2：**トラック名はストリップの下端**（Phase 61／8.1のC5）。
    // Phase 60までは上端に置いていたが、**フェーダーとメーターを見ている目線から遠く**、
    // 「いまどのトラックを触っているか」を確かめるのに視線が往復していた。
    // 一般的なミキサーと同じく、下のdB表示のさらに下へ移してある。
    nameLabel.setBounds (area.removeFromBottom (nameRowHeight));

    // 8.295：**レイテンシは下から外しました**（Phase 288／本人の指定）。
    // 置くのは**フェーダーの真上**です（下の`latencyLabel.setBounds()`）

    // 仕様書5.2.4：VCAはパンを持たない（音声を通さないため）
    if (panSlider.isVisible())
    {
        panSlider.setBounds (area.removeFromTop (panKnobHeight));

        // 8.63：**ノブの下に数値**（Phase 101／改善案⑦）。
        // dB表示と同じで、**クリックで打ち込める**入口でもある
        panReadout.setBounds (area.removeFromTop (panReadoutHeight));
        area.removeFromTop (rowGap);
    }

    auto buttonRow = area.removeFromTop (buttonRowHeight);
    muteButton.setBounds (buttonRow.removeFromLeft (buttonRow.getWidth() / 2).reduced (2, 0));
    soloButton.setBounds (buttonRow.reduced (2, 0));

    area.removeFromTop (rowGap);

    //==========================================================================
    // 8.299：**下から順に取ります**（Phase 292／本人の指定）。
    //
    // Phase 291まではラックを先に取り、**残りをフェーダーへ**回していました。
    // つまりフェーダーの高さは「そのストリップの残り」で決まり、
    // **パンやミュート／ソロを持たないマスターとVCAでは長く**なっていました。
    //
    // いまは**フェーダーの高さを先に決め**（`getFaderAreaHeightFor()`——
    // 全ストリップ共通）、**ラックが残りを取ります**。
    // マスターとVCAではラックがそのぶん背が高くなります。
    //
    // **`getPreferredHeight()`は枠に合わせて縮めないこと**（1.21）。
    // 入り切らないぶんは`rackViewport`の中でスクロールします（8.283）。

    // 下端：トラック名（上で取ってある）→ dB表示 → フェーダー → 遅延 → 境目
    auto valueRow = area.removeFromBottom (ConsoleLayout::volumeReadoutRowHeight);
    auto faderArea = area.removeFromBottom (ConsoleLayout::getFaderAreaHeightFor (getHeight()));

    latencyLabel.setBounds (area.removeFromBottom (ConsoleLayout::latencyRowHeight));

    // 8.283：境目は**掴んで動かせます**（Phase 276）
    faderResizer.setBounds (area.removeFromBottom (ConsoleLayout::resizerHeight));

    const int rackHeight = juce::jmax (0, area.getHeight());
    const int wantedRackHeight = rack.getPreferredHeight (area.getWidth());

    rackViewport.setBounds (area.removeFromTop (rackHeight));

    // **中身の高さは「欲しい高さ」のまま**にすること。枠に合わせて縮めると
    // スクロールしても下のスロットへ届かない（1.21と同じ話）。
    //
    // 8.297：**縦スクロールバーのぶんは自分で引きます**（Phase 290）。
    //
    // `getMaximumVisibleWidth()`に聞いていましたが、あれは**いま出ているか**を
    // 答えるもので、これから出るぶんは入っていません——中身の高さを決めるのは
    // この次の行なので、**1回目は必ず「出ていない」と答えます**。
    // 結果、スクロールする行では**右端がバーの下に隠れて**いました
    // （インサートの一括バイパス（B）が見出しの右端に来たので、それが隠れました）。
    const bool willScroll = wantedRackHeight > rackHeight;
    const int rackWidth = rackViewport.getWidth()
                            - (willScroll ? rackViewport.getScrollBarThickness() : 0);

    rack.setSize (juce::jmax (1, rackWidth), wantedRackHeight);

    // 8.295：**dB表示の右に、種類の絵**（Phase 288／改善案1。本人の指定）。
    //
    // 「ボリュームメーターの下、ボリューム量数値の右」——メーターとフェーダーは
    // この行の**上**にあるので、ここがちょうどその場所になります。
    // 行を16pxから18pxへ上げてあるのは、絵が小さすぎて読めないため
    // （文字のdB表示は11pxのフォントなので、18pxの行でも詰まりません）
    //
    // **縦は中央へ。** 行は18px、絵は15pxなので、そのまま取ると上に寄ります
    typeIconBounds = valueRow.removeFromRight (typeIconSize)
                              .withSizeKeepingCentre (typeIconSize, typeIconSize);
    valueRow.removeFromRight (3);

    volumeValueLabel.setBounds (valueRow);

    // フェーダーとメーターを横に並べる（メーターは右側）。
    // VCAはメーターを持たないので、フェーダーが幅いっぱいに広がる。
    if (meter.isVisible())
    {
        // Phase 59：ピークのdB表示を入れるぶん少し広げた（22→28）
        meter.setBounds (faderArea.removeFromRight (28));
        faderArea.removeFromRight (4);
    }

    volumeSlider.setBounds (faderArea);
}
