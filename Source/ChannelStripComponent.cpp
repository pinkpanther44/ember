#include "ChannelStripComponent.h"
#include "AppColours.h"
#include "Utf8.h"
#include "AudioEngine.h"
#include "DragAndDropIds.h"

ChannelStripComponent::ChannelStripComponent (const Track& trackToControl, ProjectModel& projectToUse,
                                                AudioEngine& audioEngineToUse)
    : track (trackToControl), project (projectToUse), audioEngine (audioEngineToUse),
      rack (trackToControl, projectToUse, audioEngineToUse, TrackRackComponent::Layout::Strip),
      isVca (trackToControl.getType() == TrackType::VCA)
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
    nameLabel.setTooltip (utf8 ("ドラッグでトラックを並べ替え／ダブルクリックで名前を変更"));

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
    rack.onPreferredHeightChanged = [this] { resized(); };

    // 仕様書5.7：ラックはスクロールできる枠に入れる（Phase 66／8.1のC14）。
    // **横スクロールは出さない**：ストリップの幅は固定で、ラックはその幅に合わせて
    // 高さを決める作りなので、横に溢れることは無い（出すと幅を食うだけ）。
    rackViewport.setViewedComponent (&rack, false);
    rackViewport.setScrollBarsShown (true, false);
    rackViewport.setScrollBarThickness (8);
    addAndMakeVisible (rackViewport);

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

    const bool wasVisible = latencyLabel.isVisible();
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

    // 表示の有無が変わったときだけ並べ直す（出ていない間は場所を取らせないため）
    if (latencyLabel.isVisible() != wasVisible)
        resized();
}

//==============================================================================
// 仕様書4.4・6章：ブラウザからのドラッグ&ドロップ（Phase 21）
//==============================================================================

void ChannelStripComponent::mouseDrag (const juce::MouseEvent& e)
{
    // 8.67：少し動かしたら並べ替えのドラッグを始める（Phase 106／改善案㉛）。
    // **すぐには始めない**：空きを押しただけで線が出ると、
    // 「押しただけなのに何か起きた」に見える（ラックのスロットと同じ決まり。8.66）
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

    g.setColour (isDragOver ? AppColours::purple : AppColours::border);
    g.drawRoundedRectangle (area.reduced (0.5f), AppColours::corner (4.0f), isDragOver ? 2.0f : 1.0f);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced (6);

    // 設計書2.3.2：**トラック名はストリップの下端**（Phase 61／8.1のC5）。
    // Phase 60までは上端に置いていたが、**フェーダーとメーターを見ている目線から遠く**、
    // 「いまどのトラックを触っているか」を確かめるのに視線が往復していた。
    // 一般的なミキサーと同じく、下のdB表示のさらに下へ移してある。
    nameLabel.setBounds (area.removeFromBottom (20));

    // 仕様書5.7.1：レイテンシはトラック名のすぐ上（そのトラックの属性として並べて読める）
    if (latencyLabel.isVisible())
        latencyLabel.setBounds (area.removeFromBottom (13));

    // 仕様書5.2.4：VCAはパンを持たない（音声を通さないため）
    if (panSlider.isVisible())
    {
        panSlider.setBounds (area.removeFromTop (42));

        // 8.63：**ノブの下に数値**（Phase 101／改善案⑦）。
        // dB表示と同じで、**クリックで打ち込める**入口でもある
        panReadout.setBounds (area.removeFromTop (13));
        area.removeFromTop (4);
    }

    auto buttonRow = area.removeFromTop (24);
    muteButton.setBounds (buttonRow.removeFromLeft (buttonRow.getWidth() / 2).reduced (2, 0));
    soloButton.setBounds (buttonRow.reduced (2, 0));

    area.removeFromTop (4);

    // Phase 29：ラックには必要なぶんだけ渡し、残りをフェーダーへ回す。
    // 高さの計算はラック自身が持っているので、スロットが増えてもここは直さなくてよい。
    //
    // **Phase 66（8.1のC14）：フェーダーとメーターのぶんは必ず残す。**
    // 以前はラックが欲しいだけ取っていたので、インサートを何段か挿すと
    // メーターが数ピクセルまで縮んで読めなくなっていた。
    // 入り切らないラックは`rackViewport`の中でスクロールできる。
    // **狭いときは折半する。** 「フェーダーぶんを必ず引く」だけにすると、
    // ストリップが低いときにラックの取り分が0になり、スロットが1つも見えなくなる
    // （下部パネルを縮めたときに起きる）
    const int faderRoom = juce::jmin (minimumFaderAreaHeight, area.getHeight() / 2);
    const int roomForRack = juce::jmax (0, area.getHeight() - faderRoom);
    const int wantedRackHeight = rack.getPreferredHeight (area.getWidth());
    const int rackHeight = juce::jmin (wantedRackHeight, roomForRack);

    rackViewport.setBounds (area.removeFromTop (rackHeight));

    // **中身の高さは「欲しい高さ」のまま**にすること。枠に合わせて縮めると
    // スクロールしても下のスロットへ届かない（1.21と同じ話）。
    // 縦スクロールバーが出るぶん、幅はビューポートに聞く
    rack.setSize (rackViewport.getMaximumVisibleWidth(), wantedRackHeight);

    area.removeFromTop (6);
    volumeValueLabel.setBounds (area.removeFromBottom (16));

    // フェーダーとメーターを横に並べる（メーターは右側）。
    // VCAはメーターを持たないので、フェーダーが幅いっぱいに広がる。
    if (meter.isVisible())
    {
        // Phase 59：ピークのdB表示を入れるぶん少し広げた（22→28）
        meter.setBounds (area.removeFromRight (28));
        area.removeFromRight (4);
    }

    volumeSlider.setBounds (area);
}
