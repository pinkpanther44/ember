#include "PianoRollView.h"
#include "AppColours.h"
#include "GrooveQuantise.h" // 仕様書5.3.4：グルーヴの抽出
#include "Utf8.h"
#include "AppSettings.h" // 設計書2.5：構成音カラーリングの設定を覚える（Phase 46）
#include "ToolbarLayout.h" // 8.184：狭い画面でツールバーを2段に折り返す（Phase 223）

PianoRollView::PianoRollView (ProjectModel& projectToUse, AudioEngine& engineToUse)
    : project (projectToUse), engine (engineToUse)
{
    // 8.1のG4／D5：編集対象のMIDIトラックは**左の一覧**で選ぶ（Phase 73）。
    // **Phase 72までツールバーのコンボボックスでした。** コンボは「いま何を編集しているか」
    // しか出せず、他のトラックのソロ／ミュートを見るにはアレンジ画面へ戻る必要がありました。
    trackList.onTrackSelected = [this] (const juce::String& trackId)
    {
        trackSelectionChanged (trackId);
    };

    // ソロ／ミュートは**音にも効かせる必要がある**（モデルを見ているのは画面だけ。8.18）
    trackList.onMixerValueChanged = [this] { engine.updateMixerSettings(); };

    addAndMakeVisible (trackList);

    // **透かしのボタンはここにありません**（Phase 74）：トラックごとの指定になったので、
    // 一覧の行の丸（○）が入口です（8.34）。

    // 8.91：**「Clip」のコンボと「+ Clip」は廃止しました**（Phase 131／改善案⑳）。
    // MIDIクリップという入れ物が無くなったので、選ぶものも作るものもありません

    // 仕様書5.3：クオンタイズのグリッド指定
    gridBox.addItem ("1/4 (beat)", 1);
    gridBox.addItem ("1/8", 2);
    gridBox.addItem ("1/16", 3);
    gridBox.addItem ("1/32", 4);
    gridBox.setSelectedId (3); // 既定は1/16
    addAndMakeVisible (gridBox);

    swingLabel.setText ("Swing", juce::dontSendNotification);
    swingLabel.setFont (juce::FontOptions (12.0f));
    swingLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (swingLabel);

    // 8.60：**0〜100で見せる**（Phase 97／改善案⑥）。すぐ下のGrooveの「効き具合」も
    // 0〜100なので、揃えておかないと同じ画面で2つの流儀が並ぶ。
    // クオンタイズへ渡すときに100で割る（`applyQuantise()`。Grooveと同じ形）
    swingSlider.setRange (0.0, 100.0, 1.0);
    swingSlider.setValue (0.0);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 45, 20);
    swingSlider.setTextValueSuffix ("%");

    // 8.118：ダブルクリックで「均等」へ戻す（Phase 153／改善案24）。
    // Console・インスペクタのつまみと同じ扱いに揃える
    swingSlider.setDoubleClickReturnValue (true, 0.0);
    swingSlider.setDefaultValueDescription (utf8 ("0%（均等）"));
    swingSlider.setValueEntryDecimals (0);

    // 8.60：**色を付ける**（Phase 97／改善案㉑）。
    // それまでは地と同じ色で、**つまみがどこにあるのか見えませんでした**。
    // 他のフェーダーと同じ紫にしてあります（1.34：色は必ず両テーマで確かめること）
    swingSlider.setColour (juce::Slider::trackColourId, AppColours::purple);
    swingSlider.setTooltip (utf8 ("スイング（0＝均等、100＝最大）。クオンタイズのときに裏拍をずらします"));
    addAndMakeVisible (swingSlider);

    addAndMakeVisible (selectedOnlyToggle);

    quantiseButton.onClick = [this] { quantiseClicked(); };
    addAndMakeVisible (quantiseButton);

    // 仕様書5.3：音源スロット（Phase 32で1つのボタンに畳んだ）。
    // 設計書3.7のGUI開き直しも、このメニューの中に入っている。
    instrumentButton.onClick = [this] { instrumentButtonClicked(); };
    addAndMakeVisible (instrumentButton);

    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (statusLabel);

    // 仕様書5.3.3：CCレーン（Phase 23）

    // 仕様書5.3.2・設計書2.3.3：ドラムエディターへの切り替え（Phase 25）
    drumEditorButton.setClickingTogglesState (true);
    drumEditorButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    drumEditorButton.onClick = [this] { drumEditorToggled(); };
    addAndMakeVisible (drumEditorButton);

    // 仕様書5.3.1・設計書2.3.3：構成音カラーリング（Phase 46）。
    // 3状態あるのでトグルボタンではなく、押すたびに回る作りにしている。
    colouringButton.onClick = [this] { cycleNoteColouring(); };
    addAndMakeVisible (colouringButton);

    // 設計書2.5：前回の見え方を復元する（どのプロジェクトを開いても同じ設定で始まる）
    {
        const int saved = juce::jlimit (0, 2, AppSettings::getInt (noteColouringKey, 1));
        pianoRoll.setNoteColouring ((PianoRollComponent::NoteColouring) saved);
        updateColouringButton();
    }

    // 8.29の表：鍵盤の音名／階名も同じく覚えておく（Phase 70）
    {
        const int saved = juce::jlimit (0, 1, AppSettings::getInt (keyboardLabelsKey, 0));
        pianoRoll.setKeyboardLabels ((PianoRollComponent::KeyboardLabels) saved);
    }

    // **切り替えの入口はピアノロールの右クリックメニュー**なので、
    // 覚えるのは通知を受けてから（入口が増えても書き足さずに済む）
    pianoRoll.onKeyboardLabelsChanged = [this]
    {
        AppSettings::setInt (keyboardLabelsKey, (int) pianoRoll.getKeyboardLabels());   // 設計書2.5
    };

    // 8.122：レーンの高さも覚えておく（Phase 157／改善案36。設計書2.5）
    {
        // **既定値は聞かずに、いまの値を既定として渡す。** 定数を外へ出さなくて済むし、
        // 既定を変えたときに2箇所直す必要も無くなる
        pianoRoll.setLaneHeight (AppSettings::getInt (laneHeightKey, pianoRoll.getLaneHeight()));
    }

    pianoRoll.onLaneHeightChanged = [this]
    {
        // **丸めた後の値を書くこと。** 打ち込まれた値をそのまま書くと、
        // 次に開いたときに上下限の外の値から始まる
        AppSettings::setInt (laneHeightKey, pianoRoll.getLaneHeight());   // 設計書2.5
    };

    // 8.1のG5：下部のレーンに何を出していたかも覚えておく（Phase 75。設計書2.5）
    {
        PianoRollComponent::LaneTarget target;

        target.kind = (PianoRollComponent::LaneTarget::Kind)
                          juce::jlimit (0, 2, AppSettings::getInt (laneTargetKindKey, 0));
        target.automationTargetId = AppSettings::getString (laneTargetAutomationKey,
                                                            AutomationTargets::volume);
        target.controllerNumber = AppSettings::getInt (laneTargetControllerKey,
                                                       MidiControllers::pitchBend);

        pianoRoll.setLaneTarget (target);
    }

    // **覚えるのは通知を受けてから**（切り替えの入口はレーンの見出しと右クリック。
    // 入口が増えても書き足さずに済む）
    pianoRoll.onLaneTargetChanged = [this]
    {
        const auto target = pianoRoll.getLaneTarget();

        AppSettings::setInt (laneTargetKindKey, (int) target.kind);
        AppSettings::setString (laneTargetAutomationKey, target.automationTargetId);
        AppSettings::setInt (laneTargetControllerKey, target.controllerNumber);

        updateStatusLabel();   // 説明文にレーンの中身を出している
    };

    // 仕様書5.5・5.9：編集の刻み（Phase 55）。**アレンジ画面にも同じものがある**ので、
    // 自分で決めずMainComponentへ返して、両方へ配ってもらう（1.27・8.15）
    snapSelector.onSnapGridChanged = [this] (SnapGrid grid)
    {
        if (onSnapGridSelected != nullptr)
            onSnapGridSelected (grid);
        else
            project.setSnapGrid (grid);   // 繋がっていない場合の保険

        // 縦のグリッド線は刻みで引いている（8.14）。ポップアウトしている場合、
        // ここを描き直すのはこちらの仕事になる
        pianoRoll.repaint();
    };

    snapSelector.setSnapGrid (project.getSnapGrid());
    addAndMakeVisible (snapSelector);

    // 仕様書5.3.4：グルーヴクオンタイズ（Phase 24／Phase 32でパネルへ畳んだ）。
    // **一式はgroovePanelの子にする。** パネルごとsetVisible()すれば中身も一緒に
    // 出入りするので、開閉のたびに4つ分の表示切り替えを書かずに済む。
    grooveButton.setClickingTogglesState (true);
    grooveButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    grooveButton.onClick = [this] { setGroovePanelOpen (grooveButton.getToggleState()); };
    addAndMakeVisible (grooveButton);

    addChildComponent (groovePanel); // 既定は閉じた状態

    extractGrooveButton.onClick = [this] { extractGrooveClicked(); };
    groovePanel.addAndMakeVisible (extractGrooveButton);

    grooveBox.onChange = [this]
    {
        const int index = grooveBox.getSelectedId() - 1;

        if (juce::isPositiveAndBelow (index, project.getNumGrooveTemplates()))
            selectedGrooveId = project.getGrooveTemplate (index).getId();
    };
    groovePanel.addAndMakeVisible (grooveBox);

    grooveStrengthLabel.setText (utf8 ("強さ"), juce::dontSendNotification);
    grooveStrengthLabel.setFont (juce::FontOptions (12.0f));
    grooveStrengthLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    groovePanel.addAndMakeVisible (grooveStrengthLabel);

    // 0〜100%（仕様書5.3.4）。既定は100%＝テンプレートどおりに寄せる
    grooveStrengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 45, 20);
    grooveStrengthSlider.setRange (0.0, 100.0, 1.0);
    grooveStrengthSlider.setValue (100.0, juce::dontSendNotification);
    grooveStrengthSlider.setTextValueSuffix ("%");

    // 8.118：ダブルクリックで「そのまま当てる」へ戻す（Phase 153／改善案24）
    grooveStrengthSlider.setDoubleClickReturnValue (true, 100.0);
    grooveStrengthSlider.setDefaultValueDescription (utf8 ("100%（そのまま当てる）"));
    grooveStrengthSlider.setValueEntryDecimals (0);
    grooveStrengthSlider.setColour (juce::Slider::trackColourId, AppColours::purple);
    groovePanel.addAndMakeVisible (grooveStrengthSlider);

    applyGrooveButton.onClick = [this] { applyGrooveClicked(); };
    groovePanel.addAndMakeVisible (applyGrooveButton);

    refreshGrooveList();

    // ノートが増減するとスクロールできる長さも変わる（Phase 67）
    pianoRoll.onModelChanged = [this]
    {
        updateStatusLabel();
        updateHorizontalScrollBar();
    };

    // 仕様書5.9：倍率・スクロール量が変わったら、ルーラーとコード帯を描き直し、
    // スクロールバーの範囲も付け替える（Phase 67）。
    // **本体からの通知1本にまとめてある**：ズーム・ホイール・クリップ切り替えの
    // それぞれから別々に呼ぶと、必ずどれかを足し忘れる（1.15）。
    pianoRoll.onViewChanged = [this]
    {
        header.repaint();
        updateHorizontalScrollBar();
    };

    // 仕様書6.2：ツールのボタン（Phase 69）。**文字はここで入れること**（1.2）。
    // **押しても自分では切り替えない**：MainComponentへ返し、
    // アレンジ画面とピアノロールの両方へ同じ値を配ってもらう（8.11）
    {
        struct ToolButtonSetup { IconAssets::SvgButton* button; juce::String label; juce::String tooltip;
                                 EditTool tool; const char* iconResource; };   // 8.133（Phase 169）

        const ToolButtonSetup toolSetups[] =
        {
            { &arrowToolButton,  utf8 ("選択"),   utf8 ("選択・移動・伸縮／空きのドラッグで範囲選択（1）"), EditTool::arrow, "tool_select_svg" },
            { &pencilToolButton, utf8 ("ペン"),   utf8 ("空きをドラッグしたぶんノートを追加／ノートは選択・移動・伸縮／ベロシティのなぞり書き（2）"), EditTool::pencil, "tool_pen_svg" },
            { &cutToolButton,    utf8 ("カット"), utf8 ("ノートをクリックした位置で分割（4）"), EditTool::cut, "tool_cut_svg" },
            { &eraserToolButton, utf8 ("消しゴム"), utf8 ("触れたノート・レーンの点を消す（なぞると続けて消える。5）"), EditTool::eraser, "tool_eraser_svg" },
        };

        for (const auto& setup : toolSetups)
        {
            setup.button->setButtonText (setup.label);
            setup.button->setTooltip (setup.tooltip);
            setup.button->setIconResource (setup.iconResource);   // 8.133（Phase 169）
            setup.button->setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
            setup.button->setClickingTogglesState (false);

            const auto tool = setup.tool;

            setup.button->onClick = [this, tool]
            {
                if (onEditToolSelected != nullptr)
                    onEditToolSelected (tool);
                else
                    setEditTool (tool);   // 繋がっていない場合の保険
            };

            addAndMakeVisible (*setup.button);
        }

        updateToolButtons();
    }

    // 仕様書6.2：ピアノロールの中で選ばれたツールも、ボタンと同じ経路を通す（Phase 69）
    pianoRoll.onEditToolSelected = [this] (EditTool tool)
    {
        if (onEditToolSelected != nullptr)
            onEditToolSelected (tool);
        else
            pianoRoll.setEditTool (tool);   // 繋がっていない場合の保険
    };

    // 8.29の表：ノートの発音（Phase 71）。**音源を持っているのはエンジン**なので、
    // 鳴らすのはここの仕事。編集中のトラックの音源で鳴らす
    pianoRoll.onPreviewNoteOn = [this] (int pitch, int velocity)
    {
        if (selectedTrackId.isNotEmpty())
            engine.previewNoteOn (selectedTrackId, pitch, velocity);
    };

    pianoRoll.onPreviewNoteOff = [this] (int pitch)
    {
        if (selectedTrackId.isNotEmpty())
            engine.previewNoteOff (selectedTrackId, pitch);
    };

    // 仕様書5.9：ルーラー・ループ帯・マーカー・コード帯からの要求（Phase 72／8.33）。
    // **どれもMainComponentが持っている入口へ返す**（アレンジ画面と同じものを通るので、
    // 「片方の画面からだけ効かない」が起きない）
    header.onSeek = [this] (double timelineSeconds)
    {
        if (onSeekRequested != nullptr)
            onSeekRequested (timelineSeconds);
    };

    header.onInsertMarkerRequested = [this] (double timeSeconds, bool askForName)
    {
        if (onInsertMarkerRequested != nullptr)
            onInsertMarkerRequested (timeSeconds, askForName);
    };

    header.onLoopChanged = [this]
    {
        if (onLoopChanged != nullptr)
            onLoopChanged();
    };

    header.onChordRegionDoubleClicked = [this] (double startTimeSeconds)
    {
        if (onChordRegionDoubleClicked != nullptr)
            onChordRegionDoubleClicked (startTimeSeconds);
    };

    // 空いているグリッドのクリックでも再生位置が動く（8.29の表）
    pianoRoll.onSeek = [this] (double timelineSeconds)
    {
        if (onSeekRequested != nullptr)
            onSeekRequested (timelineSeconds);
    };

    addAndMakeVisible (header);

    // ピアノロールは縦に長いためスクロールできるようにする。
    // **高さはピアノロール自身に決めさせる**：CCレーン（仕様書5.3.3）の本数で変わるため、
    // ここで固定値を入れると、レーンを足しても表示領域が増えない。
    //
    // **横はViewportにスクロールさせない**（Phase 67）。させると鍵盤も左へ流れて
    // 消えてしまう。横スクロールは`horizontalScrollBar`とピアノロール本体が持つ。
    pianoRoll.setSize (600, pianoRoll.getRequiredHeight());
    viewport.setViewedComponent (&pianoRoll, false);
    viewport.setScrollBarsShown (true, false);

    // 8.1のG5：**レーンを画面の下端に固定する**ために、見えている範囲を渡す（Phase 76）
    viewport.onVisibleAreaChanged = [this]
    {
        pianoRoll.setVisibleVerticalRange (viewport.getViewPositionY(),
                                            viewport.getMaximumVisibleHeight());
    };
    addAndMakeVisible (viewport);

    horizontalScrollBar.setRangeLimits ({ 0.0, 60.0 });
    horizontalScrollBar.addListener (this);
    addAndMakeVisible (horizontalScrollBar);

    refreshTrackList(); // 中でrefreshClipSelection()とupdateInstrumentLabel()まで行う
}

void PianoRollView::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::background);
}

void PianoRollView::resized()
{
    auto area = getLocalBounds().reduced (16);

    // Phase 32（8.1 ①）：4段あった操作行を2段に畳んだ。
    // 狭いエディタパネルではツールバーが場所を食い、ノートグリッドが潰れていた。
    //
    // 1段目：編集対象（どのトラックのどのクリップか）と、その音源
    // 2段目：ノートへの操作（クオンタイズ・グルーヴ・CC）と表示モード

    auto trackRow = area.removeFromTop (30);

    // 8.1のG4：**トラック選択は左の一覧へ移りました**（Phase 73）。
    // 8.91：**クリップの選択も無くなりました**（Phase 131／⑳）。1段目は音源だけです。

    // 仕様書5.3：音源はトラックごとの割り当てなので、トラック選択と同じ行の右側に置く
    // 音源名は長くなりがちだが、幅いっぱいに伸ばすと1段目がボタン1つに見えてしまう。
    // 上限を決めて、はみ出す名前はボタン側に省略させる。
    // 8.185：**表示モードの2つを、この行の右端へ移しました**（Phase 224／本人の指定）。
    //
    // この行は音源ボタンだけで、右が大きく空いていました。2段目は逆に詰まっていたので、
    // **空いている側へ寄せます。** 折り返しの起きる幅が1346px→1088pxまで下がるので、
    // **13インチでも2段のまま収まります**（それより狭いと、今までどおり折り返します）。
    //
    // どちらも「**画面の見せ方**」を決めるものなので、
    // 「これから置くもの」を決める2段目（刻み・ツール）とは性格が違います。分けたほうが素直です。
    drumEditorButton.setBounds (trackRow.removeFromRight (120));
    trackRow.removeFromRight (6);
    colouringButton.setBounds (trackRow.removeFromRight (juce::jmin (120, trackRow.getWidth())));
    trackRow.removeFromRight (12);

    // 8.185：**左の余白を外しました**（Phase 224／本人の指定）。
    // 16px入っていたので、**真下のクオンタイズボタンと左端が揃っていませんでした**
    instrumentButton.setBounds (trackRow.removeFromLeft (juce::jmin (240, trackRow.getWidth())));

    area.removeFromTop (8);

    // クオンタイズ操作の行（仕様書5.3）
    //
    // 8.184：**幅が足りないときは2段に折り返します**（Phase 223／`ToolbarLayout.h`）。
    // 左詰めの一群（クオンタイズ〜グルーヴ）と右詰めの一群（刻み〜表示モード）を
    // 別の段に分けます。**広い画面では、いままでと1pxも変わりません。**
    constexpr int rowHeight = 28;

    // 下の`removeFromLeft`／`removeFromRight`の合計。**片方を変えたらここも直すこと**
    constexpr int leftGroupWidth  = 100 + 8 + 110 + 12 + 45 + 160 + 12 + 150 + 12 + 100;
    constexpr int rightGroupWidth = 12 + SnapGridSelector::preferredWidth
                                     + 12 + (52 + 3 + 52 + 3 + 62 + 3 + 68);

    const bool wrapToolbar = ToolbarLayout::needsWrap (area.getWidth(),
                                                        leftGroupWidth, rightGroupWidth);

    auto quantiseRow = area.removeFromTop (rowHeight);

    // 折り返さないときは、2つ目の段は1つ目と同じ矩形。**以降のコードは共通です**
    auto secondRow = quantiseRow;

    if (wrapToolbar)
        secondRow = area.removeFromTop (ToolbarLayout::gap + rowHeight)
                        .withTrimmedTop (ToolbarLayout::gap);

    quantiseButton.setBounds (quantiseRow.removeFromLeft (100));
    quantiseRow.removeFromLeft (8);
    gridBox.setBounds (quantiseRow.removeFromLeft (110));
    quantiseRow.removeFromLeft (12);
    swingLabel.setBounds (quantiseRow.removeFromLeft (45));
    swingSlider.setBounds (quantiseRow.removeFromLeft (160));
    quantiseRow.removeFromLeft (12);
    selectedOnlyToggle.setBounds (quantiseRow.removeFromLeft (150));

    // 仕様書5.3.4：グルーヴはクオンタイズの隣（同じ「ノートの位置を直す」操作なので）。
    // 一式は下に重ねて出す（Phase 32）
    quantiseRow.removeFromLeft (12);
    auto grooveButtonBounds = quantiseRow.removeFromLeft (100);
    grooveButton.setBounds (grooveButtonBounds);

    // **「CC Lanes」ボタンはPhase 75で廃止しました**（8.35）：レーンは1本になり、
    // 中身の切り替えはレーンの見出し（左端）と右クリックが入口です

    // 設計書2.3.3：表示モードの切り替え（Phase 25／Phase 46）は、
    // **8.185で1段目の右端へ移しました**（Phase 224）。ここにはもうありません。

    // 仕様書6.2：ツールのボタン（Phase 69）。**この段の右端**に置く。
    //
    // 8.184：**ここから下は`secondRow`から取ります**（Phase 223）。
    // 折り返していないときは`quantiseRow`と同じ矩形なので、並びは変わりません。
    // **アレンジ画面と同じ並び**（左から 刻み → ツール）にしてあるので、
    // 画面を行き来しても同じ位置に同じものがある。
    // **Phase 68まで、ボタンはアレンジ画面にしかありませんでした**：
    // ノートを置くのがペンツールの仕事になったので、ここにも入口が要ります
    {
        secondRow.removeFromRight (12);

        auto place = [&secondRow] (juce::TextButton& button, int width)
        {
            button.setBounds (secondRow.removeFromRight (juce::jmin (width, secondRow.getWidth())));
            secondRow.removeFromRight (3);
        };

        // **右から置いていく**ので、並びは逆順に書く（左から 選択・ペン・カット・消しゴム）
        place (eraserToolButton, 68);
        place (cutToolButton, 62);
        place (pencilToolButton, 52);
        place (arrowToolButton, 52);
    }

    // 仕様書5.5・5.9：編集の刻み（Phase 55）。**ツールの左隣**に置く。
    // どちらも「これから置くもの」を決める設定なので、まとめて手の届く場所にある
    secondRow.removeFromRight (12);
    snapSelector.setBounds (secondRow.removeFromRight (juce::jmin (SnapGridSelector::preferredWidth,
                                                                    secondRow.getWidth())));

    area.removeFromTop (8);
    statusLabel.setBounds (area.removeFromTop (22));

    // 仕様書5.3.4：グルーヴのパネル（Phase 32）。
    // **areaから場所を取らずに重ねて置く。** 段として取ると、閉じているときも
    // 高さが残るか、開閉のたびにノートグリッドの高さが跳ねることになる。
    {
        const int panelWidth = 700;
        const int panelHeight = 40;

        // ボタンの真下。右端がはみ出す場合は左へ寄せる
        const int panelX = juce::jmax (0, juce::jmin (grooveButtonBounds.getX(),
                                                       getWidth() - panelWidth - 8));

        groovePanel.setBounds (panelX, grooveButtonBounds.getBottom() + 4, panelWidth, panelHeight);

        auto inner = groovePanel.getLocalBounds().reduced (8, 6);
        extractGrooveButton.setBounds (inner.removeFromLeft (120));
        inner.removeFromLeft (8);
        grooveBox.setBounds (inner.removeFromLeft (170));
        inner.removeFromLeft (10);
        grooveStrengthLabel.setBounds (inner.removeFromLeft (32));
        grooveStrengthSlider.setBounds (inner.removeFromLeft (150));
        inner.removeFromLeft (8);
        applyGrooveButton.setBounds (inner.removeFromLeft (110));
    }

    area.removeFromTop (8);

    // 仕様書5.9：ルーラー／コード帯 → ノートグリッド → 横スクロールバー（Phase 67）。
    //
    // **ルーラーとViewportは、左端と幅を揃えること。** ピアノロール本体の
    // X座標はViewportの左端が基準なので、ずらすと目盛りとグリッドがずれる。
    // 幅も、縦スクロールバーのぶんを引いた「中身の幅」に合わせる
    // （引かないと、ルーラーだけがスクロールバーの下まで伸びる）。

    // 8.1のG4：**左のMIDIトラック一覧**（Phase 73）。ルーラーもグリッドも
    // このぶんだけ右へ寄る（一覧はルーラーの高さも含めて縦いっぱい）
    trackList.setBounds (area.removeFromLeft (PianoRollTrackList::preferredWidth));
    area.removeFromLeft (8);
    horizontalScrollBar.setBounds (area.removeFromBottom (10));

    auto headerArea = area.removeFromTop (PianoRollHeaderComponent::totalHeight);

    viewport.setBounds (area);
    updatePianoRollSize();

    header.setBounds (headerArea.withWidth (juce::jmax (0, viewport.getMaximumVisibleWidth())));
}

void PianoRollView::updatePianoRollSize()
{
    // **横はViewportにスクロールさせない**ので、幅は見えている幅そのもの。
    // 縦だけはピアノロール自身が決める（1.21）
    const int contentWidth = juce::jmax (0, viewport.getMaximumVisibleWidth());

    pianoRoll.setSize (contentWidth, pianoRoll.getRequiredHeight());

    // 8.1のG5：**見えている範囲も渡し直す**（Phase 76）。
    // `visibleAreaChanged()`はスクロールしたときにしか来ないので、
    // 大きさを変えただけのときはここで教える（渡し忘れるとレーンの位置が古いまま）
    pianoRoll.setVisibleVerticalRange (viewport.getViewPositionY(),
                                        viewport.getMaximumVisibleHeight());

    updateHorizontalScrollBar();
}

void PianoRollView::updateHorizontalScrollBar()
{
    const double visibleSeconds = pianoRoll.getVisibleSeconds();
    // 8.162：**スクロールバーの範囲も「行ける先」で作る**（Phase 200／本人の要望）。
    // 中身の長さで作ると、つまみが右端で止まって**その先へ運べません**
    const double contentSeconds = juce::jmax (pianoRoll.getScrollableLengthSeconds(), visibleSeconds);
    const double start = pianoRoll.getScrollStartSeconds();

    // **通知を出さないこと**（`dontSendNotification`）。ここから`scrollBarMoved()`が
    // 呼ばれると、ピアノロール→ビュー→ピアノロールと往復する
    horizontalScrollBar.setRangeLimits ({ 0.0, contentSeconds }, juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange ({ start, start + visibleSeconds }, juce::dontSendNotification);
}

void PianoRollView::scrollBarMoved (juce::ScrollBar* scrollBarThatHasMoved, double newRangeStart)
{
    if (scrollBarThatHasMoved == &horizontalScrollBar)
        pianoRoll.setScrollStartSeconds (newRangeStart);
}

//==============================================================================
// 仕様書5.9：ズーム（Phase 67／8.1のG3）

void PianoRollView::zoomIn()    { pianoRoll.zoomIn(); }
void PianoRollView::zoomOut()   { pianoRoll.zoomOut(); }
void PianoRollView::zoomToFit() { pianoRoll.zoomToFit(); }

//==============================================================================
// 仕様書6.2：ツールのボタン（Phase 69／8.29）

void PianoRollView::setPlayheadSeconds (double timelineSeconds)
{
    // **両方へ配ること。** 本体（グリッドの縦線）とヘッダー（ルーラーの上の印）は
    // 別のコンポーネントなので、片方だけだと線が途中で切れて見える（8.20と同じ話）
    pianoRoll.setPlayheadSeconds (timelineSeconds);
    header.setPlayheadSeconds (timelineSeconds);
}

void PianoRollView::setEditTool (EditTool tool)
{
    pianoRoll.setEditTool (tool);
    updateToolButtons();
    updateStatusLabel();   // 説明文もツールで変わる
}

void PianoRollView::updateToolButtons()
{
    // アレンジ画面のツールボタンと**同じ見せ方**にしてある（`ArrangeView::updateToolButtons()`）。
    // 2つの画面で選ばれ方の見え方が違うと、どちらが今のツールか読み取れない
    auto apply = [] (juce::TextButton& button, bool selected)
    {
        button.setColour (juce::TextButton::buttonColourId,
                           selected ? AppColours::purple : AppColours::background);
        button.setColour (juce::TextButton::textColourOffId,
                           selected ? juce::Colours::white : AppColours::textPrimary);
    };

    const auto tool = pianoRoll.getEditTool();

    apply (arrowToolButton,  tool == EditTool::arrow);
    apply (pencilToolButton, tool == EditTool::pencil);
    apply (cutToolButton,    tool == EditTool::cut);
    apply (eraserToolButton, tool == EditTool::eraser);

    repaint();
}

void PianoRollView::visibilityChanged()
{
    if (! isVisible())
        return;

    refreshFromModel();

    // 8.89：**開いたときに、横スクロールの範囲も引き直すこと**（Phase 129）。
    //
    // 閉じているあいだにアレンジ画面でクリップを動かす／伸ばすことがあり、
    // その後で開くと**スクロールできる範囲が古いまま**になっていた
    // （`resized()`は大きさが変わったときしか来ないので、当てにできない）。
    updateHorizontalScrollBar();
    header.repaint();
}

void PianoRollView::setSnapGrid (SnapGrid grid)
{
    snapSelector.setSnapGrid (grid);

    // 縦のグリッド線は刻みで引いている（8.14）ので、描き直しまでがここの仕事
    pianoRoll.repaint();
}

void PianoRollView::refreshFromModel()
{
    // 他のタブでトラックや音源が変わっている可能性があるため、開くたびに引き直す。
    // refreshTrackList()は選択中のトラック・クリップを維持するので、
    // 何度呼んでも編集対象がすり替わることはない。
    refreshTrackList();
}

//==============================================================================
// 仕様書5.3：編集対象のトラック選択（Phase 14）
//==============================================================================

void PianoRollView::refreshTrackList()
{
    // 選択の記憶はtrackId側で持つ（番号で覚えると、トラックが増減したときにずれる。1.32）。
    // **一覧はPhase 73で左の`PianoRollTrackList`へ移りました**（コンボボックスは廃止）。
    juce::StringArray trackIds;

    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getType() == TrackType::Midi)
            trackIds.add (project.getTrack (t).getId());

    if (trackIds.isEmpty())
        selectedTrackId.clear();
    else if (! trackIds.contains (selectedTrackId))
        selectedTrackId = trackIds[0];   // 覚えていたトラックが消えていたら先頭へ

    trackList.refresh();
    trackList.setSelectedTrackId (selectedTrackId);

    // 8.91：**クリップの一覧は廃止しました**（Phase 131／改善案⑳）。
    // 選ぶのはトラック1本だけで、その中身は通しで見えます
    refreshClipSelection();
    updateInstrumentLabel();
}

void PianoRollView::showTrack (int trackIndex, double timelineSeconds)
{
    if (! juce::isPositiveAndBelow (trackIndex, project.getNumTracks()))
        return;

    auto track = project.getTrack (trackIndex);

    if (track.getType() != TrackType::Midi)
        return;

    selectedTrackId = track.getId();

    // トラック一覧から引き直す（他のタブでトラックが増減している可能性があるため）。
    // refreshTrackList()はselectedTrackIdを尊重するので、ここで指定した選択は保たれる。
    refreshTrackList();

    // 8.91：**見たい時刻が指定されていれば、そこへ寄せる**（Phase 131）。
    // アレンジ画面でノートの塊をダブルクリックしたときに、その場所が開いてほしい。
    // **既に見えているなら動かしません**（跳ねると読んでいる場所を見失う）
    if (timelineSeconds >= 0.0)
    {
        const double visible = pianoRoll.getVisibleSeconds();
        const double start = pianoRoll.getScrollStartSeconds();

        if (visible <= 0.0 || timelineSeconds < start || timelineSeconds >= start + visible)
            pianoRoll.setScrollStartSeconds (timelineSeconds);
    }
}

Track PianoRollView::getSelectedTrack() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() == TrackType::Midi && track.getId() == selectedTrackId)
            return track;
    }

    return Track (juce::ValueTree (IDs::TRACK)); // 見つからない場合の空トラック
}

void PianoRollView::trackSelectionChanged (const juce::String& trackId)
{
    if (trackId.isEmpty() || trackId == selectedTrackId)
        return;

    selectedTrackId = trackId;

    trackList.setSelectedTrackId (selectedTrackId);
    refreshClipSelection();
    updateInstrumentLabel();
}


int PianoRollView::getSelectedGridDivision() const
{
    switch (gridBox.getSelectedId())
    {
        case 1: return 1; // 1/4（4分音符 = 1拍）
        case 2: return 2; // 1/8
        case 3: return 4; // 1/16
        case 4: return 8; // 1/32
        default: break;
    }

    return 4;
}

//==============================================================================
// 仕様書5.3.2：ドラムエディター（Phase 25）
//==============================================================================

void PianoRollView::drumEditorToggled()
{
    auto track = getSelectedTrack();

    if (! track.state.getParent().isValid())
    {
        drumEditorButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText (utf8 ("MIDIトラックを選んでから切り替えてください。"), juce::dontSendNotification);
        return;
    }

    const bool shouldUseDrumEditor = drumEditorButton.getToggleState();

    project.beginAction (utf8 ("ドラムエディターの切り替え"));

    // 初めてドラムエディターにするトラックには、General MIDIの既定マップを割り当てる
    // （仕様書10.7。ユーザー定義マップの管理は今後の検討事項）
    if (shouldUseDrumEditor && track.getDrumMapId().isEmpty())
        track.setDrumMapId (project.getOrCreateDefaultDrumMap().getId(), &project.getUndoManager());

    track.setDrumEditorEnabled (shouldUseDrumEditor, &project.getUndoManager());

    updateDrumEditorMode();

    statusLabel.setText (shouldUseDrumEditor
                             ? utf8 ("ドラムエディターに切り替えました（行の見出しをクリックでミュート、右クリックでチョークグループ）")
                             : utf8 ("ピアノロールに戻しました。"),
                          juce::dontSendNotification);
}

//==============================================================================
// 仕様書5.3.1：構成音カラーリング（Phase 46）

void PianoRollView::cycleNoteColouring()
{
    using Colouring = PianoRollComponent::NoteColouring;

    // コード構成音 → スケール音 → オフ → …
    const auto next = [current = pianoRoll.getNoteColouring()]
    {
        if (current == Colouring::chordTones) return Colouring::scaleTones;
        if (current == Colouring::scaleTones) return Colouring::off;
        return Colouring::chordTones;
    }();

    pianoRoll.setNoteColouring (next);
    AppSettings::setInt (noteColouringKey, (int) next);   // 設計書2.5
    updateColouringButton();

    if (next == Colouring::chordTones && ! project.findChordTrack().state.getParent().isValid())
        statusLabel.setText (utf8 ("コード構成音モードにしましたが、コードトラックがまだありません"
                                    "（「+ Track」→「コードトラック」で作れます）。"),
                              juce::dontSendNotification);
}
void PianoRollView::updateColouringButton()
{
    using Colouring = PianoRollComponent::NoteColouring;

    const auto mode = pianoRoll.getNoteColouring();

    // **記号文字は使わない**（この環境のフォントに無い。1.30）
    colouringButton.setButtonText (mode == Colouring::chordTones ? "Chord Tones"
                                     : (mode == Colouring::scaleTones ? "Scale Tones" : "Colour Off"));

    // オフのときだけ地の色にして、効いているかどうかがひと目で分かるようにする
    colouringButton.setColour (juce::TextButton::buttonColourId,
                                mode == Colouring::off ? AppColours::background : AppColours::purple);
    colouringButton.setColour (juce::TextButton::textColourOffId,
                                mode == Colouring::off ? AppColours::textSecondary : juce::Colours::white);
}

void PianoRollView::updateDrumEditorMode()
{
    auto track = getSelectedTrack();
    const bool enabled = track.state.getParent().isValid() && track.isDrumEditorEnabled();

    drumEditorButton.setToggleState (enabled, juce::dontSendNotification);

    // マップが未割り当てなら無効なDrumMapが渡り、ピアノロール表示のままになる
    pianoRoll.setDrumMode (enabled, project.getDrumMapForTrack (track));

    // 8.161：ドラム表示に**切り替わった瞬間だけ**、いちばん下まで送る（Phase 199）。
    //
    // `pianoRoll.isDrumMode()`を見ること：マップが無いトラックでは
    // `enabled`が立っていてもピアノロール表示のままなので、
    // ボタンの状態で判断すると送る必要のない場所で送ってしまう。
    //
    // **1フレーム遅らせる。** ここではまだ`resized()`が走っておらず、
    // 中身の高さが新しい行数に更新されていないことがある（送っても届かない）
    const bool nowDrum = pianoRoll.isDrumMode();

    if (nowDrum && ! drumModeWasEnabled)
        juce::MessageManager::callAsync ([safeThis = juce::Component::SafePointer<PianoRollView> (this)]
        {
            if (safeThis != nullptr && safeThis->pianoRoll.isDrumMode())
                safeThis->viewport.setViewPositionProportionately (0.0, 1.0);
        });

    drumModeWasEnabled = nowDrum;
}

//==============================================================================
// 仕様書5.3.4：グルーヴクオンタイズ（Phase 24）
//==============================================================================

void PianoRollView::refreshGrooveList()
{
    grooveBox.clear (juce::dontSendNotification);

    for (int i = 0; i < project.getNumGrooveTemplates(); ++i)
    {
        auto grooveTemplate = project.getGrooveTemplate (i);
        grooveBox.addItem (grooveTemplate.getName(), i + 1);

        if (grooveTemplate.getId() == selectedGrooveId)
            grooveBox.setSelectedId (i + 1, juce::dontSendNotification);
    }

    // 覚えていたテンプレートが消えている場合は、先頭へ寄せる
    if (grooveBox.getSelectedId() == 0 && project.getNumGrooveTemplates() > 0)
    {
        grooveBox.setSelectedId (1, juce::dontSendNotification);
        selectedGrooveId = project.getGrooveTemplate (0).getId();
    }

    if (project.getNumGrooveTemplates() == 0)
        selectedGrooveId = {};

    const bool hasTemplates = (project.getNumGrooveTemplates() > 0);
    applyGrooveButton.setEnabled (hasTemplates);
    grooveStrengthSlider.setEnabled (hasTemplates);
}

void PianoRollView::extractGrooveClicked()
{
    auto track = pianoRoll.getTrack();   // 8.91：抽出元はトラック（Phase 131）

    if (! track.state.getParent().isValid() || track.getNumNotes() == 0)
    {
        statusLabel.setText (utf8 ("抽出元にするノートがありません。先に打ち込んでください。"),
                              juce::dontSendNotification);
        return;
    }

    const int gridDivision = getSelectedGridDivision();

    // パターン1周は1小節ぶんにする（拍子の分子 × グリッドの分割数）。
    // 小節をまたぐグルーヴも作れるが、まずは「1小節の繰り返し」という
    // いちばん分かりやすい単位に固定しておく。
    const int patternLength = juce::jmax (1, project.getBeatsPerBar() * gridDivision);

    // 名前は後から見て分かるように、抽出条件を含めておく
    const auto name = utf8 ("グルーヴ ") + juce::String (project.getNumGrooveTemplates() + 1)
                        + " (1/" + juce::String (gridDivision * 4) + ", "
                        + juce::String (project.getTempo(), 0) + "BPM)";

    project.beginAction (utf8 ("グルーヴの抽出"));

    auto extracted = GrooveQuantise::extractFromTrack (project, track, gridDivision, patternLength, name);

    if (! extracted.state.isValid())
    {
        statusLabel.setText (utf8 ("グルーヴを抽出できませんでした。"), juce::dontSendNotification);
        return;
    }

    selectedGrooveId = extracted.getId();
    refreshGrooveList();

    statusLabel.setText (utf8 ("グルーヴを抽出しました: ") + extracted.getName()
                              + utf8 ("（") + juce::String (extracted.getNumPoints()) + utf8 (" マス）"),
                          juce::dontSendNotification);
}

void PianoRollView::applyGrooveClicked()
{
    if (! pianoRoll.hasTrack())
    {
        statusLabel.setText (utf8 ("MIDIトラックを選んでから実行してください。"), juce::dontSendNotification);
        return;
    }

    auto grooveTemplate = project.findGrooveTemplate (selectedGrooveId);

    if (! grooveTemplate.state.isValid())
    {
        statusLabel.setText (utf8 ("先に「Extract Groove」でグルーヴを抽出してください。"),
                              juce::dontSendNotification);
        return;
    }

    const bool selectedOnly = selectedOnlyToggle.getToggleState();

    if (selectedOnly && ! pianoRoll.hasSelectedNote())
    {
        statusLabel.setText (utf8 ("ノートを選択してから実行してください。"), juce::dontSendNotification);
        return;
    }

    const double strength = grooveStrengthSlider.getValue() / 100.0;

    pianoRoll.applyGroove (grooveTemplate, getSelectedGridDivision(), strength, selectedOnly);

    statusLabel.setText (utf8 ("グルーヴを適用しました: ") + grooveTemplate.getName()
                              + utf8 ("（強さ ") + juce::String (grooveStrengthSlider.getValue(), 0) + "%）",
                          juce::dontSendNotification);
}

void PianoRollView::quantiseClicked()
{
    if (! pianoRoll.hasTrack())
        return;

    const int gridDivision = getSelectedGridDivision();
    const bool selectedOnly = selectedOnlyToggle.getToggleState();

    if (selectedOnly && ! pianoRoll.hasSelectedNote())
    {
        statusLabel.setText (utf8 ("ノートを選択してから実行してください。"), juce::dontSendNotification);
        return;
    }

    // 8.60：**画面は0〜100、渡すのは0.0〜1.0**（Phase 97／改善案⑥）。
    // Grooveの「効き具合」と同じ形（見せ方だけを変え、中身の単位は変えない）
    pianoRoll.quantiseNotes (gridDivision, swingSlider.getValue() / 100.0, selectedOnly);

    statusLabel.setText (utf8 ("クオンタイズを適用しました（テンポ ")
                              + juce::String (project.getTempo(), 1) + utf8 (" BPM 基準）"),
                          juce::dontSendNotification);
}

void PianoRollView::instrumentButtonClicked()
{
    if (selectedTrackId.isEmpty())
    {
        statusLabel.setText (utf8 ("先に「+ Track」でMIDIトラックを作ってください。"),
                              juce::dontSendNotification);
        return;
    }

    // 音源が未設定なら、いきなり選択メニューを出す（1手で済ませる）。
    // Consoleのチャンネルストリップの音源スロットと同じ操作感にしてある。
    const bool loaded = engine.trackHasInstrument (selectedTrackId);

    if (! loaded && ! getSelectedTrack().hasInstrument())
    {
        chooseInstrument();
        return;
    }

    juce::PopupMenu menu;
    menu.addItem (1, utf8 ("GUIを開く"), loaded);
    menu.addItem (2, utf8 ("音源を変更..."));
    menu.addSeparator();
    menu.addItem (3, utf8 ("音源を外す"));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton),
        [this] (int result)
        {
            if (result == 1)
            {
                // 設計書3.7：プロジェクトを読み込んで音源が復元された直後は
                // GUIが開いていないので、ここから開き直せるようにしてある
                engine.openTrackInstrumentEditor (selectedTrackId);
            }
            else if (result == 2)
            {
                chooseInstrument();
            }
            else if (result == 3)
            {
                engine.removeInstrumentFromTrack (selectedTrackId);
                updateInstrumentLabel();
            }
        });
}

void PianoRollView::chooseInstrument()
{
    // プラグイン一覧は環境設定でスキャン済みのものを使う
    auto plugins = engine.getPluginManager().getKnownPlugins();

    if (plugins.isEmpty())
    {
        statusLabel.setText (utf8 ("先に環境設定でプラグインをスキャンしてください。"),
                              juce::dontSendNotification);
        return;
    }

    juce::PopupMenu menu;

    for (int i = 0; i < plugins.size(); ++i)
    {
        // インストゥルメント（音源）のみを一覧に出す
        if (plugins.getReference (i).isInstrument)
            menu.addItem (i + 1, plugins.getReference (i).name);
    }

    if (menu.getNumItems() == 0)
    {
        statusLabel.setText (utf8 ("インストゥルメント（音源）プラグインが見つかりません。"),
                              juce::dontSendNotification);
        return;
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentButton),
        [this, plugins] (int result)
        {
            if (result <= 0 || result > plugins.size())
                return;

            const auto trackId = selectedTrackId; // メニューを開いている間に選択が変わっても取り違えない
            const auto error = engine.loadInstrumentForTrack (trackId, plugins.getReference (result - 1));

            if (error.isNotEmpty())
            {
                statusLabel.setText (utf8 ("読み込み失敗: ") + error, juce::dontSendNotification);
            }
            else
            {
                engine.openTrackInstrumentEditor (trackId);
                updateInstrumentLabel();
                statusLabel.setText (utf8 ("音源を割り当てました。Playで打ち込んだノートが鳴ります。"),
                                      juce::dontSendNotification);
            }
        });
}

void PianoRollView::refreshClipSelection()
{
    // 8.91：**渡すのはトラック1本だけ**（Phase 131）
    auto track = getSelectedTrack();

    // 仕様書5.3.2：表示モードはトラックごとの設定なので、先に合わせる（Phase 25）。
    // 後にすると、行数が古いまま高さを計算してしまう。
    updateDrumEditorMode();

    pianoRoll.setTrack (track);

    updateStatusLabel();
}

void PianoRollView::updateStatusLabel()
{
    if (! pianoRoll.hasTrack())
    {
        statusLabel.setText (utf8 ("MIDIトラックを選んでください（左の一覧）。"),
                              juce::dontSendNotification);
        return;
    }

    // 8.29の表（Phase 69）：**ツールによって書いてあることが変わる。**
    // 「クリックでノート追加」と出したまま矢印ツールで押しても何も起きない、
    // という状態を作らないため（1.9の「表示していない値」と同じ考え方）
    juce::String text;

    switch (pianoRoll.getEditTool())
    {
        case EditTool::pencil:
            text << utf8 ("ペン：空きをドラッグしたぶんノートを追加 / ノートは選択・移動・伸縮 / ");
            text << utf8 ("ベロシティレーンをなぞって強弱をまとめて変更");
            break;

        case EditTool::cut:
            text << utf8 ("カット：ノートをクリックするとその位置で分割");
            break;

        case EditTool::eraser:
            text << utf8 ("消しゴム：触れたノートを消す（なぞると続けて消える）/ ");
            text << utf8 ("レーンの点も同じように消せる");
            break;

        case EditTool::arrow:
        default:
            text << utf8 ("ドラッグで移動 / 左右の端をドラッグで長さ変更 / 空きをドラッグで範囲選択 / ");
            text << utf8 ("鍵盤をクリックでその音を全部選択 / 右クリックまたはDeleteで削除");
            break;
    }

    // 8.1のG5：**下のレーンに何が出ているか**も書いておく（Phase 75）。
    // 見出しは50pxしかなく、切り替えられること自体に気づきにくい
    text << utf8 ("　｜　下のレーン：") << pianoRoll.getLaneTargetName()
         << utf8 ("（見出しをクリックで切り替え）");

    statusLabel.setText (text, juce::dontSendNotification);
}

void PianoRollView::updateInstrumentLabel()
{
    // 設計書3.8：プロジェクト読み込みで音源が復元された場合も、ここで表示を合わせる。
    // 表示はエンジン側（実際にロードできた音源）を正とする。
    const bool loaded = ! selectedTrackId.isEmpty() && engine.trackHasInstrument (selectedTrackId);

    // Phase 32：表示先はボタン自身。Consoleの音源スロットと同じ色分けにしてある
    // （読み込めているならパープル、失敗しているならオレンジ）。
    if (loaded)
    {
        instrumentButton.setButtonText (engine.getTrackInstrumentName (selectedTrackId));
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::purple);
    }
    else if (! selectedTrackId.isEmpty() && getSelectedTrack().hasInstrument())
    {
        instrumentButton.setButtonText (utf8 ("(読込失敗) ") + getSelectedTrack().getInstrument().getDisplayName());
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::orange);
    }
    else
    {
        instrumentButton.setButtonText ("+ Instrument");
        instrumentButton.setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
    }

    // トラックが1本も無いときに押しても何もできないので、押せないようにしておく
    instrumentButton.setEnabled (! selectedTrackId.isEmpty());
}

void PianoRollView::setGroovePanelOpen (bool shouldBeOpen)
{
    groovePanel.setVisible (shouldBeOpen);

    // **前面へ出すこと。** groovePanelはviewportより先に親へ付いているので、
    // そのままだとノートグリッドの後ろに隠れて何も見えない。
    if (shouldBeOpen)
        groovePanel.toFront (false);

    // ボタン側の見た目は、外から閉じられた場合にも合わせる
    grooveButton.setToggleState (shouldBeOpen, juce::dontSendNotification);
}

void PianoRollView::PanelBackground::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (AppColours::panel);
    g.fillRoundedRectangle (bounds, AppColours::corner (4.0f));

    g.setColour (AppColours::border);
    g.drawRoundedRectangle (bounds, AppColours::corner (4.0f), 1.0f);
}

void PianoRollView::refreshAfterProjectChanged (bool keepSelection)
{
    // **Undo/Redoは同じプロジェクトの中の話**なので、覚えているIDは有効なまま（Phase 68）。
    // ここを捨てると、ノートを消してCtrl+Zで戻しただけで、
    // ピアノロールが**先頭のMIDIトラックへ勝手に移ります**（`refreshTrackList()`が
    // 「覚えていたトラックが消えた」ときと同じ扱いで先頭へ寄せるため）。
    // アレンジ画面は同じ理由でPhase 34に直してあります（1.33）。
    if (! keepSelection)
    {
        // プロジェクトが入れ替わると、pianoRollが握っているトラック（＝古いValueTreeへの
        // 参照）はもう本体と繋がっていない。選び直さないと、打ち込んでも表示にも音にも
        // 反映されないトラックを編集し続けることになる。
        selectedTrackId.clear();

        // 仕様書5.3.4：グルーヴテンプレートもプロジェクトに属するので、
        // 覚えていたIDは別プロジェクトのものになる（Phase 24）
        selectedGrooveId.clear();
    }

    // 一覧そのものは**どちらでも作り直す**。Undoでトラックやクリップが
    // 増減していることがあるため（消えていれば`refreshTrackList()`が寄せ直す）
    refreshGrooveList();
    refreshTrackList(); // 中でrefreshClipSelection()とupdateInstrumentLabel()まで行う
    repaint();
}
