#include "TrackRackComponent.h"
#include "AppColours.h"
#include "Utf8.h"
#include "AudioEngine.h"
#include "DragAndDropIds.h"

namespace
{
    /** 行の高さ。ストリップは幅が狭いぶん縦も詰め、インスペクタは押しやすさを優先する。 */
    struct RowMetrics
    {
        int caption;    // 見出し（0＝出さない）
        int slot;       // スロット1行
        int smallSlot;  // センドの送り先名（送り量スライダーと2行で1組になる）
        int slider;     // 送り量スライダー
        int gap;        // 種類の変わり目に空ける間隔
    };

    RowMetrics metricsFor (TrackRackComponent::Layout layout)
    {
        if (layout == TrackRackComponent::Layout::Inspector)
            return { 16, 22, 20, 18, 8 };

        return { 0, 20, 16, 14, 6 };
    }
}

//==============================================================================
std::unique_ptr<TrackRackComponent> TrackRackComponent::createForMasterBus (ProjectModel& projectToUse,
                                                                             AudioEngine& audioEngineToUse,
                                                                             Layout layoutToUse)
{
    // 8.69：マスターも「`<INSERTS>`を持つノード」という点ではトラックと同じ（Phase 108／D6）
    return std::make_unique<TrackRackComponent> (projectToUse.getMasterBusInsertHost(),
                                                  projectToUse, audioEngineToUse, layoutToUse, true);
}

TrackRackComponent::TrackRackComponent (const Track& trackToControl, ProjectModel& projectToUse,
                                          AudioEngine& audioEngineToUse, Layout layoutToUse,
                                          bool isMasterBusToUse)
    : track (trackToControl), project (projectToUse), audioEngine (audioEngineToUse),
      layout (layoutToUse), isVca (! isMasterBusToUse && trackToControl.getType() == TrackType::VCA),
      isMasterBus (isMasterBusToUse)
{
    auto setUpCaption = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.0f));
        label.setColour (juce::Label::textColourId, AppColours::textSecondary);
        addChildComponent (label);
    };

    setUpCaption (instrumentCaption, utf8 ("音源"));
    setUpCaption (insertCaption, utf8 ("インサート"));
    setUpCaption (sendCaption, utf8 ("センド"));

    // 仕様書5.6：オートメーションの書き込みモード（Phase 20）
    automationModeBox.addItem ("Read", 1);
    automationModeBox.addItem ("Touch", 2);
    automationModeBox.addItem ("Latch", 3);
    automationModeBox.addItem ("Write", 4);
    automationModeBox.onChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        const auto mode = static_cast<AutomationMode> (juce::jlimit (0, 3, automationModeBox.getSelectedId() - 1));

        project.beginAction (utf8 ("オートメーションモードの変更"));
        track.setAutomationMode (mode, &project.getUndoManager());
    };
    addChildComponent (automationModeBox);

    // 仕様書5.6：VCAのフェーダーはオートメーション対象外（Phase 12d-2）。
    // 値の適用先となるプロセッサがグラフ上に無いため、記録しても効かない。
    // 8.69：マスターの書き込みモードは`MasterStripComponent`が出す（Phase 108/D6。重ねない）
    automationModeBox.setVisible (! isVca && ! isMasterBus);

    // 仕様書5.2.4：割り当て先を選ぶボタン。VCAは音声を通さないので、VCA同士は繋げない
    // （VCAのVCA＝多段構成は仕様書5.2.4の範囲外）。
    vcaButton.onClick = [this] { vcaButtonClicked(); };
    addChildComponent (vcaButton);
    vcaButton.setVisible (! isVca && ! isMasterBus && (track.getType() == TrackType::Audio
                                       || track.getType() == TrackType::Midi
                                       || track.getType() == TrackType::Send
                                       || track.getType() == TrackType::DrumOut));   // 8.143

    vcaLinkLabel.setJustificationType (juce::Justification::centred);
    vcaLinkLabel.setFont (juce::FontOptions (11.0f));
    vcaLinkLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addChildComponent (vcaLinkLabel);
    vcaLinkLabel.setVisible (isVca);

    // 仕様書5.3：音源スロット。MIDIトラックだけが持つ
    // Phase 61（8.1のC1）：左クリックでGUI、右クリックでメニュー（8.21）
    instrumentButton.onClick = [this] { instrumentSlotClicked(); };
    instrumentButton.onRightClick = [this] { showInstrumentSlotMenu(); };
    instrumentButton.setTooltip (utf8 ("クリックでGUIを開く／右クリックで変更・解除"));
    addChildComponent (instrumentButton);
    instrumentButton.setVisible (track.getType() == TrackType::Midi);

    // 仕様書5.7：インサートスロット
    addInsertButton.onClick = [this] { addInsertClicked(); };
    addChildComponent (addInsertButton);
    addInsertButton.setVisible (! isVca);

    // 仕様書5.2.2：センド。センドトラック自身には送りを付けない
    // （センド同士を繋ぐと信号がループし得るため、送り元は自分で音を出すトラックに限る）。
    //
    // 8.53：**フォルダ（バス）からも送れる**（Phase 92）。
    // まとめた音をリバーブへ送る、という使い方が普通にあります
    addSendButton.onClick = [this] { addSendClicked(); };
    addChildComponent (addSendButton);
    // 8.69：マスターは送りを持たない（Phase 108/D6。最終段なので送り先が無い）
    //
    // 8.143：**パラアウトの受け皿からも送れる**（Phase 181／改善案⑮）。
    // **スネアだけリバーブへ**、がパラアウトのいちばんの用途です。
    // センド同士のループにはなりません——受け皿の音の出所は「音源の出力バス」で、
    // 送りを受けているわけではないためです
    addSendButton.setVisible (! isMasterBus
                               && (track.getType() == TrackType::Audio
                                    || track.getType() == TrackType::Midi
                                    || track.getType() == TrackType::Folder
                                    || track.getType() == TrackType::DrumOut));

    // 仕様書5.2.2：プリ／ポストフェーダーの切り替え。センドトラック側の設定なので、
    // センドトラックにだけ表示する。
    prePostButton.onClick = [this] { prePostClicked(); };
    addChildComponent (prePostButton);
    prePostButton.setVisible (track.getType() == TrackType::Send);

    // 8.63：**インサートをまとめて通す／通さない**（Phase 101／改善案㉘）。
    // ミックスの確認で「全部外して聴く」がいちばんよく使う操作
    bypassAllInsertsButton.setButtonText ("B");
    bypassAllInsertsButton.setTooltip (utf8 ("インサートをまとめて通す／通さない（設定は残ります）"));
    bypassAllInsertsButton.setColour (juce::TextButton::buttonOnColourId, AppColours::orange);
    bypassAllInsertsButton.setClickingTogglesState (false);
    bypassAllInsertsButton.onClick = [this] { toggleAllInsertsBypassed(); };
    addChildComponent (bypassAllInsertsButton);

    // 見出しは、幅に余裕があり、かつその種類のスロットを持つときだけ出す。
    // レイアウト側は`isVisible()`だけを見るので、条件はここ1箇所で決まる。
    instrumentCaption.setVisible (showsCaptions() && instrumentButton.isVisible());
    insertCaption.setVisible (showsCaptions() && addInsertButton.isVisible());
    sendCaption.setVisible (showsCaptions() && addSendButton.isVisible());

    track.state.addListener (this);

    updateControlsFromModel();
    refreshInstrumentSlot();
    refreshInsertSlots();
    refreshSendSlots();
    refreshVcaAssignment();
}

TrackRackComponent::~TrackRackComponent()
{
    track.state.removeListener (this);
}

//==============================================================================
void TrackRackComponent::notifyMixerChanged()
{
    if (onMixerValueChanged != nullptr)
        onMixerValueChanged();
}

void TrackRackComponent::notifyHeightChanged()
{
    resized();

    if (onPreferredHeightChanged != nullptr)
        onPreferredHeightChanged();
}

void TrackRackComponent::refreshAll()
{
    refreshInstrumentSlot();
    refreshInsertSlots();
    refreshSendSlots();
    refreshVcaAssignment();
    updateControlsFromModel();
}

void TrackRackComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // Undo/Redoや、他のUIからの変更にも追従する
    updateControlsFromModel();
}

void TrackRackComponent::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    refreshSlotsForChangedChild (parent, child);
}

void TrackRackComponent::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    refreshSlotsForChangedChild (parent, child);
}

void TrackRackComponent::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    // 8.66：並べ替えに追従する（Phase 104）。器の型で見分けるのは増減と同じ
    if (parent.hasType (IDs::INSERTS))
        refreshInsertSlots();
    else if (parent.hasType (IDs::SENDS))
        refreshSendSlots();
}

void TrackRackComponent::refreshSlotsForChangedChild (const juce::ValueTree& parent,
                                                        const juce::ValueTree& child)
{
    // インサートとセンドは器（<INSERTS> / <SENDS>）の子として並ぶので、親の型で見分ける
    if (parent.hasType (IDs::INSERTS))
        refreshInsertSlots();
    else if (parent.hasType (IDs::SENDS))
        refreshSendSlots();

    // 8.190：**`parent`の側も見ること**（Phase 228／本人の報告）。
    //
    // 「音源を別のものへ差し替えると、**中身は変わるのに名前が古いまま**」という
    // 報告でした。**足すときと差し替えるときで、飛んでくる通知が違います**：
    //
    // | | `Track::setInstrument()`がすること | ここへ来る形 |
    // |---|---|---|
    // | **足す**（`<INSTRUMENT>`が無い） | `state`へ`<INSTRUMENT>`を足す | `child` = `<INSTRUMENT>` ← **拾えていた** |
    // | **差し替え**（すでに有る） | `<INSTRUMENT>`の中の`<PLUGININSTANCE>`を入れ替える | `parent` = `<INSTRUMENT>`、`child` = `<PLUGININSTANCE>` ← **どれにも当たらない** |
    //
    // 差し替えでは`<INSTRUMENT>`そのものは動かないので、`child`を見ている限り
    // 永久に当たりません。**器の側（`parent`）で見分けます**——
    // インサートとセンドを親の型で見分けているのと同じ形です（上の2行）。
    //
    // 音が変わって名前だけ古いのは、**一番たちの悪い出方**です：
    // 画面は正しく見えるので、間違っていることに気づけません。
    else if (child.hasType (IDs::INSTRUMENT) || parent.hasType (IDs::INSTRUMENT))
        refreshInstrumentSlot();
    else if (child.hasType (IDs::INSERTS))
        refreshInsertSlots();  // 器そのものが後から作られる場合がある（古いプロジェクト）
    else if (child.hasType (IDs::SENDS))
        refreshSendSlots();
}

void TrackRackComponent::updateControlsFromModel()
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    // 仕様書5.6：書き込みモード（Phase 20）。Read以外は色を変えて、
    // 「今動かすと記録される」ことがひと目で分かるようにする
    const auto automationMode = track.getAutomationMode();
    automationModeBox.setSelectedId ((int) automationMode + 1, juce::dontSendNotification);
    automationModeBox.setColour (juce::ComboBox::backgroundColourId,
                                  automationMode == AutomationMode::Read ? AppColours::background
                                                                          : AppColours::orange);

    // 仕様書5.2.4：VCAのリンク表示（Phase 12d-2）。
    // VCAトラック自身は自分のValueTreeを購読しているので、ここでUndo/Redoにも追従する。
    refreshVcaAssignment();

    // 仕様書5.2.2：送り量。Consoleとインスペクタに同じトラックが同時に出るので、
    // 片方で動かしたらもう片方も追従させる（Undo/Redoもここを通る）。
    for (int i = 0; i < sendSliders.size() && i < track.getNumSends(); ++i)
        sendSliders[i]->setValue (track.getSend (i).getLevelDb(), juce::dontSendNotification);

    // 仕様書5.2.2：センドトラックのプリ／ポスト表示。Undo/Redoでも追従させる
    if (prePostButton.isVisible())
    {
        const bool preFader = track.isPreFader();

        prePostButton.setButtonText (preFader ? "Pre" : "Post");
        prePostButton.setColour (juce::TextButton::buttonColourId,
                                  preFader ? AppColours::orange : AppColours::background);
        prePostButton.setColour (juce::TextButton::textColourOffId,
                                  preFader ? juce::Colours::white : AppColours::textSecondary);
    }
}

//==============================================================================
// 仕様書5.2.4：VCA（Phase 12d-2）
//==============================================================================

void TrackRackComponent::refreshVcaAssignment()
{
    if (isVca)
    {
        const int numLinked = track.getLinkedTrackIds().size();

        vcaLinkLabel.setText (numLinked == 0 ? utf8 ("リンクなし")
                                              : utf8 ("リンク ") + juce::String (numLinked) + utf8 ("本"),
                               juce::dontSendNotification);
        return;
    }

    if (! vcaButton.isVisible())
        return;

    auto vca = project.findVcaForTrack (track.getId());
    const bool linked = vca.state.getParent().isValid();

    // 設計書2.3.2：リンク中は「VCA適用中」が分かるようにする。
    // 設計書2.6にならい、VCA関連の識別色はオレンジ。
    vcaButton.setButtonText (linked ? vca.getName() : "VCA: -");
    vcaButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    vcaButton.setColour (juce::TextButton::textColourOffId,
                          linked ? AppColours::orange : AppColours::textSecondary);
}

void TrackRackComponent::vcaButtonClicked()
{
    juce::StringArray vcaIds;
    juce::PopupMenu menu;

    auto currentVca = project.findVcaForTrack (track.getId());
    const auto currentVcaId = currentVca.state.getParent().isValid() ? currentVca.getId() : juce::String();

    // 1番は常に「解除」。VCAが1つも無いときでも、この項目だけは出る
    menu.addItem (1, utf8 ("VCAなし"), true, currentVcaId.isEmpty());
    menu.addSeparator();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto candidate = project.getTrack (t);

        if (candidate.getType() != TrackType::VCA)
            continue;

        vcaIds.add (candidate.getId());
        // 項目番号は「1（解除）」の次から。チェックで今の割り当てが分かるようにする
        menu.addItem (vcaIds.size() + 1, candidate.getName(), true, candidate.getId() == currentVcaId);
    }

    if (vcaIds.isEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (utf8 ("VCAトラックがありません"))
                .withMessage (utf8 ("先にArrangeタブの「+ Track」→「VCAトラック」でVCAトラックを作ってください。"))
                .withButton ("OK"),
            nullptr);
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&vcaButton),
        [this, vcaIds] (int result)
        {
            if (result <= 0)
                return;

            // 1＝解除、2以降＝vcaIdsの並び順。1トラック1VCAの保証はモデル側が行う
            const auto newVcaId = (result == 1) ? juce::String() : vcaIds[result - 2];

            project.assignTrackToVca (track.getId(), newVcaId);

            // リンクの有無でゲインが変わるので、エンジンへ反映する
            notifyMixerChanged();
        });
}

//==============================================================================
// 仕様書5.3：音源スロット（Phase 14）
//==============================================================================

void TrackRackComponent::refreshInstrumentSlot()
{
    if (! instrumentButton.isVisible())
        return;

    // 表示名はエンジン側（実際にロードできた音源）を優先する。モデルにだけ記録が残り、
    // 読み込みに失敗している場合に「鳴っているはず」と誤解しないようにするため。
    const bool loaded = audioEngine.trackHasInstrument (track.getId());

    if (loaded)
    {
        instrumentButton.setButtonText (audioEngine.getTrackInstrumentName (track.getId()));
        instrumentButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::purple);
    }
    else if (track.hasInstrument())
    {
        instrumentButton.setButtonText (utf8 ("(読込失敗) ") + track.getInstrument().getDisplayName());
        instrumentButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::orange);
    }
    else
    {
        instrumentButton.setButtonText ("+ Instrument");
        instrumentButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
    }
}

void TrackRackComponent::instrumentSlotClicked()
{
    // 音源が未設定なら、いきなり選択ダイアログを出す（1手で済ませる）
    if (! audioEngine.trackHasInstrument (track.getId()) && ! track.hasInstrument())
    {
        chooseInstrument();
        return;
    }

    // Phase 61（8.1のC1）：**左クリックはGUIを開く**。インサートと同じ扱いにしてある
    // （隣り合うスロットで操作が違うと、どちらがどちらか分からなくなる）
    audioEngine.openTrackInstrumentEditor (track.getId());
}

void TrackRackComponent::showInstrumentSlotMenu()
{
    // 音源が未設定なら、メニューを出さずに選択ダイアログへ（項目が1つしか無いため）
    if (! audioEngine.trackHasInstrument (track.getId()) && ! track.hasInstrument())
    {
        chooseInstrument();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("GUIを開く"), audioEngine.trackHasInstrument (track.getId()));
    menu.addItem (2, utf8 ("音源を変更..."));
    menu.addSeparator();
    menu.addItem (3, utf8 ("音源を外す"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton),
        [this] (int result)
        {
            if (result == 1)
            {
                audioEngine.openTrackInstrumentEditor (track.getId());
            }
            else if (result == 2)
            {
                chooseInstrument();
            }
            else if (result == 3)
            {
                audioEngine.removeInstrumentFromTrack (track.getId());
                refreshInstrumentSlot();
            }
        });
}

void TrackRackComponent::chooseInstrument()
{
    // プラグイン一覧は環境設定でスキャン済みのものを使う
    auto plugins = audioEngine.getPluginManager().getKnownPlugins();

    juce::PopupMenu menu;

    for (int i = 0; i < plugins.size(); ++i)
        if (plugins.getReference (i).isInstrument) // 音源のみ。エフェクトはインサート側の担当
            menu.addItem (i + 1, plugins.getReference (i).name);

    if (menu.getNumItems() == 0)
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (utf8 ("音源プラグインがありません"))
                .withMessage (utf8 ("先に環境設定でプラグインをスキャンしてください。"))
                .withButton ("OK"),
            nullptr);
        return;
    }

    // **トラックはIDで捕まえ直す。** メニューが閉じるまでの間に選択が変わり得る
    // （ピアノロールの同じメニューが先にそうしている）
    const auto trackId = track.getId();

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton),
        [this, plugins, trackId] (int result)
        {
            if (result <= 0 || result > plugins.size())
                return;

            const auto error = audioEngine.loadInstrumentForTrack (trackId,
                                                                    plugins.getReference (result - 1));

            if (error.isNotEmpty())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("音源を割り当てられませんでした"))
                        .withMessage (error)
                        .withButton ("OK"),
                    nullptr);
                return;
            }

            refreshInstrumentSlot();

            // 8.119：**設計書3.7のとおり、挿したらGUIを開く**（Phase 154／改善案33）。
            //
            // 音源を挿す入口は4つある——ブラウザからのドラッグ（`MainComponent`と
            // このクラスのドロップ）、ピアノロールの「+ Instrument」、そしてここ。
            // **ここだけ開いていなかった**ので、Inspectorから挿したときだけ
            // 「挿さったのか分からない」という形になっていた（1.27）。
            audioEngine.openTrackInstrumentEditor (trackId);
        });
}

//==============================================================================
// 仕様書5.2.2：センド（Phase 13）
//==============================================================================

void TrackRackComponent::prePostClicked()
{
    if (isUpdatingFromModel)
        return;

    const bool shouldBePreFader = ! track.isPreFader();

    project.beginAction (utf8 ("プリ／ポストフェーダーの切り替え"));
    track.setPreFader (shouldBePreFader, &project.getUndoManager());

    // 送り元をフェーダーの前後どちらから取るかが変わるため、配線を張り直す必要がある
    // （送り量だけの変更と違い、updateMixerSettings()では反映されない）。
    audioEngine.rebuildSendConnections();

    notifyMixerChanged();
}

void TrackRackComponent::refreshSendSlots()
{
    sendButtons.clear();
    sendSliders.clear();

    for (int i = 0; i < track.getNumSends(); ++i)
    {
        auto send = track.getSend (i);

        // 送り先の名前を引く（見つからない場合はIDのままにせず、分かる表記にする）
        juce::String targetName = utf8 ("(不明な送り先)");

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto candidate = project.getTrack (t);

            if (candidate.getId() == send.getTargetTrackId())
            {
                targetName = candidate.getName();
                break;
            }
        }

        auto* button = sendButtons.add (new SlotButton (targetName));
        button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        button->setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
        button->onClick = [this, i] { sendSlotClicked (i); };

        // 8.66：**右クリックでも同じメニュー**（Phase 105）。
        // インサートは右クリック、センドは左クリック、では覚えることが増えるだけ
        button->onRightClick = [this, i] { sendSlotClicked (i); };

        // 8.66：**箱として運べるようにする**（Phase 105／改善案㉘㉙＋D7）
        button->dragDescription = DragAndDropIds::makeSendDescription (track.getId(), i);
        button->setTooltip (utf8 ("クリックでメニュー／ドラッグで並べ替え・他トラックへコピー"));

        addAndMakeVisible (button);

        auto* slider = sendSliders.add (new ValueEntrySlider (juce::Slider::LinearHorizontal,
                                                               juce::Slider::NoTextBox));
        slider->setRange (-60.0, 6.0, 0.1);
        slider->setValue (send.getLevelDb(), juce::dontSendNotification);
        slider->setColour (juce::Slider::trackColourId, AppColours::purple);

        // Phase 64：ダブルクリック＝0dB、右クリック＝メニュー（8.25の表）
        slider->setDoubleClickReturnValue (true, 0.0);
        slider->setDefaultValueDescription ("0 dB");
        slider->setTooltip (utf8 ("送り量（dB）。ダブルクリックで0dB／右クリックでメニュー"));
        slider->onDragStart = [this] { project.beginAction (utf8 ("センド量の変更")); };
        slider->onValueChange = [this, i]
        {
            if (isUpdatingFromModel || i >= track.getNumSends())
                return;

            track.getSend (i).setLevelDb ((float) sendSliders[i]->getValue(), &project.getUndoManager());
            notifyMixerChanged();
        };
        addAndMakeVisible (slider);
    }

    notifyHeightChanged();
}

void TrackRackComponent::sendSlotClicked (int sendIndex)
{
    if (! juce::isPositiveAndBelow (sendIndex, sendButtons.size()))
        return;

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("センドを削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sendButtons[sendIndex]),
        [this, sendIndex] (int result)
        {
            if (result != 1)
                return;

            audioEngine.removeSendFromTrack (track.getId(), sendIndex);
            refreshSendSlots();
        });
}

void TrackRackComponent::addSendClicked()
{
    juce::PopupMenu menu;
    juce::StringArray targetIds;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto candidate = project.getTrack (t);

        if (candidate.getType() != TrackType::Send)
            continue;

        targetIds.add (candidate.getId());
        menu.addItem (targetIds.size(), candidate.getName());
    }

    if (menu.getNumItems() == 0)
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (utf8 ("センドトラックがありません"))
                .withMessage (utf8 ("先にArrangeタブの「+ Track」→「センドトラック」でセンドトラックを作ってください。"))
                .withButton ("OK"),
            nullptr);
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSendButton),
        [this, targetIds] (int result)
        {
            if (result <= 0 || result > targetIds.size())
                return;

            audioEngine.addSendToTrack (track.getId(), targetIds[result - 1]);
            refreshSendSlots();
        });
}

//==============================================================================
// 仕様書5.7：インサート（Phase 12c-2）
//==============================================================================

void TrackRackComponent::refreshInsertSlots()
{
    insertButtons.clear();

    for (int i = 0; i < track.getNumInserts(); ++i)
    {
        auto insert = track.getInsert (i);
        auto name = insert.getDisplayName();

        // 仕様書5.7.2・設計書2.3.2：サイドチェインが設定されているスロットは
        // ひと目で分かるようにする。ストリップは幅が狭くアイコンを置く余地が無いので、
        // 短い印を名前に足し、設計書2.6にならってオレンジで示す。
        const bool hasSidechain = insert.getSidechainSourceTrackId().isNotEmpty();

        // 8.63：**通していないスロットも印で示す**（Phase 101／改善案㉘）。
        // **文字だけを薄くするのでは足りません**：暗い地の上では
        // 「読みにくい」のか「切ってある」のかが見分けられないので、印も付けます
        const bool bypassed = insert.isBypassed();

        if (hasSidechain)
            name += " [SC]";

        if (bypassed)
            name = "(" + name + ")";

        auto* button = insertButtons.add (new SlotButton (name));
        button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        button->setColour (juce::TextButton::textColourOffId,
                           bypassed ? AppColours::textSecondary.withAlpha (0.6f)
                                    : (hasSidechain ? AppColours::orange : AppColours::textPrimary));

        // Phase 61（8.1のC1）：左クリックでGUI、右クリックでメニュー（8.21）
        button->onClick = [this, i] { insertSlotClicked (i); };
        button->onRightClick = [this, i] { showInsertSlotMenu (i); };
        button->setTooltip (bypassed
                                ? utf8 ("通していません（右クリックのメニューから戻せます）")
                                : utf8 ("クリックでGUIを開く／右クリックでメニュー／ドラッグで並べ替え・他トラックへコピー"));

        // 8.66：**箱として運べるようにする**（Phase 104／改善案㉘㉙＋D7）
        button->dragDescription = DragAndDropIds::makeInsertDescription (track.getId(), i);

        addAndMakeVisible (button);
    }

    // 8.63：一括のボタンは、インサートが1つ以上あるときだけ（Phase 101）
    bypassAllInsertsButton.setVisible (insertCaption.isVisible() && ! insertButtons.isEmpty());
    bypassAllInsertsButton.setToggleState (areAllInsertsBypassed(), juce::dontSendNotification);

    notifyHeightChanged();
}

bool TrackRackComponent::areAllInsertsBypassed() const
{
    const int numInserts = track.getNumInserts();

    if (numInserts == 0)
        return false;

    for (int i = 0; i < numInserts; ++i)
        if (! track.getInsert (i).isBypassed())
            return false;

    return true;
}

void TrackRackComponent::toggleAllInsertsBypassed()
{
    // **1つでも生きていれば「全部止める」。** 全部止まっているときだけ戻す方向にする
    // （「半分だけ切ってある」状態から押したときに、何が起きるかが読める）
    const bool shouldBypass = ! areAllInsertsBypassed();

    project.beginAction (shouldBypass ? utf8 ("インサートをまとめて通さない")
                                       : utf8 ("インサートをまとめて通す"));

    for (int i = 0; i < track.getNumInserts(); ++i)
        track.getInsert (i).setBypassed (shouldBypass, &project.getUndoManager());

    // **配線を張り直す。** モデルを変えただけでは音は変わらない（8.63）
    audioEngine.rebuildGraphForBypassChange();

    refreshInsertSlots();
}

void TrackRackComponent::insertSlotClicked (int insertIndex)
{
    // Phase 61（8.1のC1）：**左クリックはGUIを開く**。
    // Phase 60まではここでメニューを出していたが、
    // 「メニューが出ている間はボタンへクリックが届かない」ため、
    // ダブルクリックでGUIを開くことができなかった（8.21）。
    if (! juce::isPositiveAndBelow (insertIndex, insertButtons.size()))
        return;

    audioEngine.openInsertEditor (track.getId(), insertIndex);
}

void TrackRackComponent::showInsertSlotMenu (int insertIndex)
{
    // スロットは幅が狭く、GUI表示と削除のボタンを並べては置けないので、
    // 変更系は右クリックのメニューにまとめてある（Phase 61）。
    if (! juce::isPositiveAndBelow (insertIndex, insertButtons.size()))
        return;

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("GUIを開く"));

    // 8.63：**このインサートを通さない**（Phase 101／改善案㉘）。
    // 外すのではなく通さないだけなので、設定も内部状態も残る
    menu.addItem (3, utf8 ("バイパス"), true, track.getInsert (insertIndex).isBypassed());
    menu.addSeparator();

    // 仕様書5.7.2・設計書3.9：サイドチェイン入力を持つプラグインにだけ、
    // ソースを選ぶ項目を出す。持っていないプラグインでは設定項目自体を出さない。
    juce::StringArray sourceIds;

    if (audioEngine.insertSupportsSidechain (track.getId(), insertIndex))
    {
        const auto currentSourceId = track.getInsert (insertIndex).getSidechainSourceTrackId();

        juce::PopupMenu sidechainMenu;

        // 100番台をサイドチェイン用に使う（101＝解除、102以降＝ソースの並び順）
        sidechainMenu.addItem (101, utf8 ("なし"), true, currentSourceId.isEmpty());
        sidechainMenu.addSeparator();

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto candidate = project.getTrack (t);

            // 音を出すトラックだけが候補。自分自身は信号が輪になるため外す
            // 8.52：フォルダ（バス）も送り元にできる（Phase 91。8.51）
            // 8.143：**パラアウトの受け皿からも送れる**（Phase 181／改善案⑮）——
            // スネアだけリバーブへ、という使い方がいちばんの目的です
            const bool canBeSource = (candidate.getType() == TrackType::Audio
                                       || candidate.getType() == TrackType::Midi
                                       || candidate.getType() == TrackType::Send
                                       || candidate.getType() == TrackType::Folder
                                       || candidate.getType() == TrackType::DrumOut);

            if (! canBeSource || candidate.getId() == track.getId())
                continue;

            sourceIds.add (candidate.getId());
            sidechainMenu.addItem (101 + sourceIds.size(), candidate.getName(),
                                    true, candidate.getId() == currentSourceId);
        }

        menu.addSubMenu (utf8 ("サイドチェイン入力"), sidechainMenu, sourceIds.size() > 0);
        menu.addSeparator();
    }

    menu.addItem (2, utf8 ("インサートを削除"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (insertButtons[insertIndex]),
        [this, insertIndex, sourceIds] (int result)
        {
            if (result == 1)
            {
                audioEngine.openInsertEditor (track.getId(), insertIndex);
            }
            else if (result == 2)
            {
                audioEngine.removeInsertFromTrack (track.getId(), insertIndex);
                refreshInsertSlots();
            }
            else if (result == 3)
            {
                // 8.63：バイパスの切り替え（Phase 101／改善案㉘）
                auto insert = track.getInsert (insertIndex);

                project.beginAction (utf8 ("インサートのバイパス"));
                insert.setBypassed (! insert.isBypassed(), &project.getUndoManager());

                // **配線を張り直す。** モデルを変えただけでは音は変わらない
                audioEngine.rebuildGraphForBypassChange();

                refreshInsertSlots();
            }
            else if (result >= 101)
            {
                // 101＝解除、102以降＝sourceIdsの並び順
                const auto sourceId = (result == 101) ? juce::String() : sourceIds[result - 102];

                const auto error = audioEngine.setInsertSidechainSource (track.getId(), insertIndex, sourceId);

                refreshInsertSlots();

                if (error.isNotEmpty())
                    juce::NativeMessageBox::showAsync (
                        juce::MessageBoxOptions()
                            .withIconType (juce::MessageBoxIconType::WarningIcon)
                            .withTitle (utf8 ("サイドチェインを設定できませんでした"))
                            .withMessage (error)
                            .withButton ("OK"),
                        nullptr);
            }
        });
}

void TrackRackComponent::addInsertClicked()
{
    // プラグイン一覧は環境設定でスキャン済みのものを使う
    auto plugins = audioEngine.getPluginManager().getKnownPlugins();

    juce::PopupMenu menu;

    for (int i = 0; i < plugins.size(); ++i)
    {
        // インサートはエフェクト用。音源はここには出さない（音源スロット側の担当）
        if (! plugins.getReference (i).isInstrument)
            menu.addItem (i + 1, plugins.getReference (i).name);
    }

    if (menu.getNumItems() == 0)
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle (utf8 ("プラグインがありません"))
                .withMessage (utf8 ("先に環境設定でプラグインをスキャンしてください。"))
                .withButton ("OK"),
            nullptr);
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addInsertButton),
        [this, plugins] (int result)
        {
            if (result <= 0 || result > plugins.size())
                return;

            const auto error = audioEngine.addInsertToTrack (track.getId(), plugins.getReference (result - 1));

            if (error.isNotEmpty())
            {
                juce::NativeMessageBox::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::WarningIcon)
                        .withTitle (utf8 ("インサートを追加できませんでした"))
                        .withMessage (error)
                        .withButton ("OK"),
                    nullptr);
                return;
            }

            refreshInsertSlots();

            // 8.52：**挿したらGUIを開く**（Phase 91。設計書3.7）。
            // ドラッグ＆ドロップで挿したときは前から開いていましたが、
            // **「＋」から選んだときだけ開いていませんでした**（同じことを2箇所に
            // 書いていて、片方だけ足りていない形。8.37と同じ落とし穴）
            if (track.getNumInserts() > 0)
                audioEngine.openInsertEditor (track.getId(), track.getNumInserts() - 1);
        });
}

//==============================================================================
// 仕様書4.4・6章：ドラッグ&ドロップの判定と実行（Phase 21／Phase 29でここへ集約）
//==============================================================================

bool TrackRackComponent::canAcceptPluginDrag (AudioEngine& audioEngine, const Track& track,
                                                const juce::var& dragDescription)
{
    // 仕様書5.2.4：VCAは音声を通さないので、プラグインは挿せない
    if (track.getType() == TrackType::VCA)
        return false;

    if (! DragAndDropIds::isPluginDrag (dragDescription))
        return false;

    juce::PluginDescription description;

    if (! DragAndDropIds::findPluginByIdentifier (audioEngine.getPluginManager().getKnownPlugins(),
                                                   DragAndDropIds::getPluginIdentifier (dragDescription),
                                                   description))
        return false;

    // 音源はMIDIトラックにしか挿せない。受け付けない組み合わせは
    // ハイライトも出さないようにして、落とせないことが分かるようにする（HANDOVER 1.13）
    if (description.isInstrument)
        return track.getType() == TrackType::Midi;

    return true; // エフェクトはどのチャンネルにも挿せる
}

void TrackRackComponent::handlePluginDrop (AudioEngine& audioEngine, const Track& track,
                                             const juce::var& dragDescription,
                                             std::function<void()> onInserted)
{
    juce::PluginDescription description;

    if (! DragAndDropIds::findPluginByIdentifier (audioEngine.getPluginManager().getKnownPlugins(),
                                                   DragAndDropIds::getPluginIdentifier (dragDescription),
                                                   description))
        return;

    const auto trackId = track.getId();

    const auto error = description.isInstrument
                          ? audioEngine.loadInstrumentForTrack (trackId, description)
                          : audioEngine.addInsertToTrack (trackId, description);

    if (error.isNotEmpty())
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle (utf8 ("プラグインを挿入できませんでした"))
                .withMessage (error)
                .withButton ("OK"),
            nullptr);
        return;
    }

    if (onInserted != nullptr)
        onInserted();

    // 設計書3.7：挿したらそのまま音作りに入れるようGUIを開く
    if (description.isInstrument)
        audioEngine.openTrackInstrumentEditor (trackId);
    else if (track.getNumInserts() > 0)
        audioEngine.openInsertEditor (trackId, track.getNumInserts() - 1);
}

//==============================================================================
// 8.66：インサートとセンドを箱として動かす（Phase 104・105／改善案㉘㉙＋D7）
//==============================================================================

bool TrackRackComponent::getSlotKindForDrag (const juce::var& description, SlotKind& kindOut)
{
    if (DragAndDropIds::isInsertDrag (description))
    {
        kindOut = SlotKind::insert;
        return true;
    }

    if (DragAndDropIds::isSendDrag (description))
    {
        kindOut = SlotKind::send;
        return true;
    }

    return false;
}

int TrackRackComponent::getSlotDropIndexForPosition (SlotKind kind, juce::Point<int> localPosition) const
{
    // その種類のスロットが並ぶ帯の中だけを落とし先にする。
    // 「+ ○○」まで含めるのは、末尾へ足したいときに狙う場所が無くなってしまうため
    const auto& addButton = (kind == SlotKind::insert) ? addInsertButton : addSendButton;

    if (! addButton.isVisible())
        return -1;

    const int numSlots = (kind == SlotKind::insert) ? insertButtons.size() : sendButtons.size();

    auto slotBounds = [this, kind] (int index)
    {
        if (kind == SlotKind::insert)
            return insertButtons[index]->getBounds();

        // センドは「送り先名」と「送り量」の2行で1組。
        // **組の全体で見ないと、スライダーの上が谷間になってしまう**
        auto bounds = sendButtons[index]->getBounds();

        if (index < sendSliders.size())
            bounds = bounds.getUnion (sendSliders[index]->getBounds());

        return bounds;
    };

    const int top = (numSlots == 0) ? addButton.getY() : slotBounds (0).getY();

    if (localPosition.y < top || localPosition.y > addButton.getBottom())
        return -1;

    // **真ん中で上下に分ける。** スロットの上半分なら「その前」、下半分なら「その後」
    for (int i = 0; i < numSlots; ++i)
        if (localPosition.y < slotBounds (i).getCentreY())
            return i;

    return numSlots;
}

int TrackRackComponent::getSlotDropLineY (SlotKind kind, int dropIndex) const
{
    if (kind == SlotKind::insert)
    {
        if (juce::isPositiveAndBelow (dropIndex, insertButtons.size()))
            return insertButtons[dropIndex]->getY();

        // 末尾＝最後のスロットの下（1本も無いときは「+ Insert」の上）
        return insertButtons.isEmpty() ? addInsertButton.getY()
                                        : insertButtons.getLast()->getBottom();
    }

    if (juce::isPositiveAndBelow (dropIndex, sendButtons.size()))
        return sendButtons[dropIndex]->getY();

    if (sendButtons.isEmpty())
        return addSendButton.getY();

    // センドは2行で1組なので、末尾は「送り量スライダーの下」
    return sendSliders.isEmpty() ? sendButtons.getLast()->getBottom()
                                  : sendSliders.getLast()->getBottom();
}

void TrackRackComponent::paintOverChildren (juce::Graphics& g)
{
    if (slotDropIndex < 0)
        return;

    // 設計書2.6：操作中の合図はオレンジ（トラックのドラッグの予告線と同じ扱い）
    g.setColour (AppColours::orange);
    g.fillRect (0, getSlotDropLineY (slotDropKind, slotDropIndex) - 1, getWidth(), 2);
}

bool TrackRackComponent::isInterestedInDragSource (const SourceDetails& details)
{
    SlotKind kind;

    // **プラグインのドラッグには興味を示さない**（ストリップ／インスペクタが受ける）
    if (! getSlotKindForDrag (details.description, kind))
        return false;

    // 仕様書5.2.4：VCAは音声を通さないので、インサートもセンドも置けない。
    // 「+ ○○」が出ていないラックも同じ扱い（置く場所が無い）
    if (isVca)
        return false;

    return (kind == SlotKind::insert) ? addInsertButton.isVisible()
                                       : addSendButton.isVisible();
}

void TrackRackComponent::itemDragEnter (const SourceDetails& details)
{
    itemDragMove (details);
}

void TrackRackComponent::itemDragMove (const SourceDetails& details)
{
    SlotKind kind;

    if (! getSlotKindForDrag (details.description, kind))
        return;

    const int newIndex = getSlotDropIndexForPosition (kind, details.localPosition);

    // 変わったときだけ描き直す（ドラッグ中は毎ピクセル呼ばれる）
    if (newIndex == slotDropIndex && kind == slotDropKind)
        return;

    slotDropIndex = newIndex;
    slotDropKind = kind;
    repaint();
}

void TrackRackComponent::itemDragExit (const SourceDetails&)
{
    if (slotDropIndex < 0)
        return;

    slotDropIndex = -1;
    repaint();
}

void TrackRackComponent::itemDropped (const SourceDetails& details)
{
    SlotKind kind;

    const bool isSlot = getSlotKindForDrag (details.description, kind);
    const int dropIndex = isSlot ? getSlotDropIndexForPosition (kind, details.localPosition) : -1;

    slotDropIndex = -1;
    repaint();

    if (dropIndex < 0)
        return;

    const bool isInsert = (kind == SlotKind::insert);

    const auto sourceTrackId = isInsert ? DragAndDropIds::getInsertSourceTrackId (details.description)
                                        : DragAndDropIds::getSendSourceTrackId (details.description);
    const int sourceIndex = isInsert ? DragAndDropIds::getInsertIndex (details.description)
                                     : DragAndDropIds::getSendIndex (details.description);

    // **トラックIDが空文字でも弾かないこと。** 8.69：空文字はマスターを指す（Phase 108／D6）
    if (sourceIndex < 0)
        return;

    // **後回しにする。** ここはドラッグ元のボタンのマウスイベントの途中で、
    // この中でスロットを作り直すと**イベントを配っている最中のボタンを消す**ことになる
    // （`refreshInsertSlots()`は`insertButtons`を丸ごと作り直す）。
    // 1.15と同じ「掴んだまま消える」形の事故なので、いったん抜けてから動かす。
    juce::Component::SafePointer<TrackRackComponent> safeThis (this);

    juce::MessageManager::callAsync ([safeThis, isInsert, sourceTrackId, sourceIndex, dropIndex]
    {
        if (safeThis == nullptr)
            return;

        auto& rack = *safeThis;
        const auto destTrackId = rack.track.getId();
        juce::String error;

        if (sourceTrackId == destTrackId)
        {
            // 同じトラック＝並べ替え。
            // 予告線は「ここの**前**へ入る」を指しているので、
            // **自分より下から動かすときは1つ詰まる**（自分が抜けたぶん）
            const int target = (dropIndex > sourceIndex) ? dropIndex - 1 : dropIndex;

            if (isInsert)
                rack.audioEngine.moveInsertInTrack (destTrackId, sourceIndex, target);
            else
                rack.audioEngine.moveSendInTrack (destTrackId, sourceIndex, target);
        }
        else
        {
            // 違うトラック＝コピー（インサートは設定ごと、センドは送り先と送り量）
            error = isInsert
                        ? rack.audioEngine.copyInsertToTrack (sourceTrackId, sourceIndex, destTrackId, dropIndex)
                        : rack.audioEngine.copySendToTrack (sourceTrackId, sourceIndex, destTrackId, dropIndex);
        }

        // 並べ替えも追加も購読で拾えるが（`valueTreeChildOrderChanged`）、
        // バイパスの印など**プロパティだけの違い**は拾えないので、ここでも作り直す
        if (isInsert)
            rack.refreshInsertSlots();
        else
            rack.refreshSendSlots();

        if (error.isNotEmpty())
            juce::NativeMessageBox::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle (utf8 ("コピーできませんでした"))
                    .withMessage (error)
                    .withButton ("OK"),
                nullptr);
    });
}

//==============================================================================
// レイアウト
//==============================================================================

int TrackRackComponent::getPreferredHeight (int width)
{
    // 高さは中身で決まるので、測るときの縦幅は「足りていれば何でもよい」大きな値でよい
    return layOutContents ({ 0, 0, width, 10000 }, false);
}

void TrackRackComponent::resized()
{
    layOutContents (getLocalBounds(), true);
}

int TrackRackComponent::layOutContents (juce::Rectangle<int> area, bool apply)
{
    const auto m = metricsFor (layout);
    const int top = area.getY();

    // `apply`がfalseのときは高さだけ測る。位置の計算を2つ書くと必ずずれるので、
    // 「置くかどうか」だけをここで分岐させている。
    auto place = [apply] (juce::Component& c, juce::Rectangle<int> bounds)
    {
        if (apply)
            c.setBounds (bounds);
    };

    auto takeRow = [&area] (int height) { return area.removeFromTop (height); };

    // 仕様書5.6：書き込みモード（Phase 20）
    if (automationModeBox.isVisible())
        place (automationModeBox, takeRow (m.slot));

    // 仕様書5.2.4：VCAの割り当て（Phase 12d-2）。
    // 信号経路とは別の話なので、インサートやセンドより上（トラックの属性側）に置く。
    if (vcaButton.isVisible())
    {
        area.removeFromTop (4);
        place (vcaButton, takeRow (m.slot));
    }

    if (vcaLinkLabel.isVisible())
    {
        area.removeFromTop (4);
        place (vcaLinkLabel, takeRow (m.slot));
    }

    // 仕様書5.3：音源スロットはインサートの上（信号の流れと同じ順に上から並べる）
    if (instrumentButton.isVisible())
    {
        area.removeFromTop (m.gap);

        if (instrumentCaption.isVisible())
            place (instrumentCaption, takeRow (m.caption));

        place (instrumentButton, takeRow (m.slot));
    }

    // 仕様書5.7・設計書2.3.2：インサートスロットは縦に並べる
    if (addInsertButton.isVisible())
    {
        area.removeFromTop (m.gap);

        if (insertCaption.isVisible())
        {
            auto captionRow = takeRow (m.caption);

            // 8.63：**一括のバイパスは見出しの行の右端**（Phase 101／改善案㉘）。
            // スロットの並びに混ぜると、1本ぶんのスロットと見分けが付かない
            if (bypassAllInsertsButton.isVisible())
                place (bypassAllInsertsButton, captionRow.removeFromRight (18));

            place (insertCaption, captionRow);
        }

        for (auto* button : insertButtons)
            place (*button, takeRow (m.slot).reduced (0, 1));

        place (addInsertButton, takeRow (m.slot).reduced (0, 1));
    }

    // 仕様書5.2.2：センドはインサートの下。1本につき「送り先名」＋「送り量」の2行
    if (addSendButton.isVisible())
    {
        area.removeFromTop (m.gap);

        if (sendCaption.isVisible())
            place (sendCaption, takeRow (m.caption));

        for (int i = 0; i < sendButtons.size(); ++i)
        {
            place (*sendButtons[i], takeRow (m.smallSlot).reduced (0, 1));

            if (i < sendSliders.size())
                place (*sendSliders[i], takeRow (m.slider));
        }

        place (addSendButton, takeRow (m.slot).reduced (0, 1));
    }

    if (prePostButton.isVisible())
    {
        area.removeFromTop (m.gap);
        place (prePostButton, takeRow (m.slot));
    }

    return area.getY() - top;
}
