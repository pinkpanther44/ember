#include "TransportBarComponent.h"
#include "AppColours.h"
#include "ChordModel.h"   // 仕様書5.11.1：pitchClassName（Phase 63）
#include "Utf8.h"

namespace
{
    /** 設計書2.6：開いているパネルのボタンはパープルで示す。 */
    void applyPanelToggleColours (juce::TextButton& button, bool isOpen)
    {
        button.setColour (juce::TextButton::buttonColourId,
                           isOpen ? AppColours::purple : AppColours::background);
        button.setColour (juce::TextButton::textColourOffId,
                           isOpen ? juce::Colours::white : AppColours::textPrimary);
    }
}

//==============================================================================
TransportBarComponent::TransportBarComponent()
{
    // 仕様書5.7：マスター音量（Phase 37）。Consoleのフェーダーと範囲を揃える
    masterVolumeCaption.setText (utf8 ("Master"), juce::dontSendNotification);
    masterVolumeCaption.setFont (juce::FontOptions (10.0f));
    masterVolumeCaption.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (masterVolumeCaption);

    masterVolumeSlider.setRange (-60.0, 6.0, 0.1);
    masterVolumeSlider.setDoubleClickReturnValue (true, 0.0);
    masterVolumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
    masterVolumeSlider.setColour (juce::Slider::trackColourId, AppColours::purple);

    // 8.118：右クリックのメニューに添える説明（Phase 153／改善案24）。
    // インスペクタの音量と同じ文言にしてある
    masterVolumeSlider.setDefaultValueDescription ("0 dB");
    masterVolumeSlider.setTooltip (utf8 ("マスター音量（Consoleのマスターフェーダーと同じ値）。"
                                          "数値を直接打ち込めます。ダブルクリックで0dB／右クリックでメニュー"));

    masterVolumeSlider.onDragStart = [this]
    {
        if (onMasterVolumeDragStart != nullptr)
            onMasterVolumeDragStart();
    };
    masterVolumeSlider.onDragEnd = [this]
    {
        if (onMasterVolumeDragEnd != nullptr)
            onMasterVolumeDragEnd();
    };
    masterVolumeSlider.onValueChange = [this]
    {
        if (isUpdatingFromModel)
            return;

        if (onMasterVolumeChanged != nullptr)
            onMasterVolumeChanged ((float) masterVolumeSlider.getValue());
    };
    addAndMakeVisible (masterVolumeSlider);

    // 仕様書5.9：再生位置。mm:ss.d で出す（フレーム単位まではまだ扱っていない）
    timeLabel.setPlainFontHeight (22.0f);
    timeLabel.setTextColour (AppColours::textPrimary);
    addAndMakeVisible (timeLabel);
    setPlayheadSeconds (0.0);

    // 仕様書5.4・設計書2.6：録音関連はオレンジで示す
    recordButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    recordButton.setColour (juce::TextButton::textColourOffId, AppColours::orange);

    // 8.132：**最初の記号はここで決めること**（Phase 168／実験枠の44）。
    // 録音ボタンは`setRecordingState()`が**状態が変わったときにしか呼ばれない**ので、
    // 再生ボタン（`setPlayingState(false)`をここで呼んでいる）と違い、
    // 任せておくと最初だけ文字のままになります
    recordButton.setIcon (TransportIcons::Icon::record);
    recordButton.onClick = [this]
    {
        if (onRecordButtonClicked != nullptr)
            onRecordButtonClicked();
    };
    addAndMakeVisible (recordButton);

    recordTimeLabel.setFont (juce::FontOptions (12.0f));
    recordTimeLabel.setJustificationType (juce::Justification::centredLeft);
    recordTimeLabel.setColour (juce::Label::textColourId, AppColours::orange);
    addAndMakeVisible (recordTimeLabel);

    // 仕様書5.9：先頭へ戻す（Phase 30）。
    // Phase 29まではルーラーの左端を狙ってクリックするしか手が無かった。
    // 隣のRec/Playと同じ地色にして、演奏ボタンのひとまとまりに見えるようにする
    goToStartButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    goToStartButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    goToStartButton.setTooltip (utf8 ("先頭へ戻す"));
    goToStartButton.setIcon (TransportIcons::Icon::goToStart);   // 8.132（Phase 168／実験枠の44）
    goToStartButton.onClick = [this]
    {
        if (onGoToStartClicked != nullptr)
            onGoToStartClicked();
    };
    addAndMakeVisible (goToStartButton);

    playButton.onClick = [this]
    {
        if (onPlayButtonClicked != nullptr)
            onPlayButtonClicked();
    };
    addAndMakeVisible (playButton);
    setPlayingState (false);

    // 設計書2.2：パネルの開閉（Phase 17／Phase 30でここへ移した）。
    //
    // **ここは押されたときの繋ぎこみだけ**で、並びは`resized()`が決める
    // （8.118：Phase 153／改善案31で、左端＝Inspector・右端＝残り4つに分けた）。
    struct { juce::TextButton* button; std::function<void()>* callback; } toggles[] =
    {
        { &inspectorButton, &onInspectorToggled },
        { &editorButton,    &onEditorToggled },
        { &consoleButton,   &onConsoleToggled },
        { &chordPadButton,  &onChordPadToggled },   // 8.74（Phase 114/改善案⑭）
        { &browserButton,   &onBrowserToggled },
    };

    for (auto& t : toggles)
    {
        auto* callback = t.callback;

        t.button->setClickingTogglesState (false); // 見た目はこちらで手動制御する
        t.button->onClick = [callback]
        {
            if (*callback != nullptr)
                (*callback)();
        };
        addAndMakeVisible (t.button);
        applyPanelToggleColours (*t.button, false);
    }

    // 設計書2.2：テンポ・拍子（Phase 26）。
    // どちらもクリックしてその場で書き換えられる（Labelの編集機能を使う）。
    // 専用のダイアログを出さないのは、テンポは試行錯誤しながら何度も変える値だから。
    tempoCaption.setText ("BPM", juce::dontSendNotification);
    tempoCaption.setFont (juce::FontOptions (11.0f));
    tempoCaption.setJustificationType (juce::Justification::centredRight);
    tempoCaption.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (tempoCaption);

    tempoLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    tempoLabel.setJustificationType (juce::Justification::centred);
    tempoLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    tempoLabel.setColour (juce::Label::backgroundColourId, AppColours::background);
    tempoLabel.setEditable (true);
    tempoLabel.onTextChange = [this]
    {
        // 極端な値はルーラーやクオンタイズの計算が破綻するため、常識的な範囲へ収める
        const double newTempo = juce::jlimit (20.0, 300.0, tempoLabel.getText().getDoubleValue());

        if (onTempoChanged != nullptr)
            onTempoChanged (newTempo);

        // 丸めた結果を表示へ戻す（20未満を打っても20と表示されるように）
        tempoLabel.setText (juce::String (newTempo, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."),
                             juce::dontSendNotification);
    };
    addAndMakeVisible (tempoLabel);

    timeSignatureLabel.setFont (juce::FontOptions (13.0f));
    timeSignatureLabel.setJustificationType (juce::Justification::centred);
    timeSignatureLabel.setColour (juce::Label::textColourId, AppColours::textPrimary);
    timeSignatureLabel.setColour (juce::Label::backgroundColourId, AppColours::background);
    timeSignatureLabel.setEditable (true);
    timeSignatureLabel.onTextChange = [this]
    {
        if (onTimeSignatureChanged != nullptr)
            onTimeSignatureChanged (timeSignatureLabel.getText());
    };
    addAndMakeVisible (timeSignatureLabel);

    // 仕様書5.11.1：プロジェクトのキー（Phase 63／8.1のC9）。
    // **コードパッドの上段と同じ値**。値の実体はコードトラックが持っている
    keyCaption.setText ("Key", juce::dontSendNotification);
    keyCaption.setFont (juce::FontOptions (11.0f));
    keyCaption.setJustificationType (juce::Justification::centredRight);
    keyCaption.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (keyCaption);

    auto onKeyBoxChanged = [this]
    {
        if (isUpdatingFromModel || onProjectKeyChanged == nullptr)
            return;

        onProjectKeyChanged (keyRootBox.getSelectedId() - 1, keyModeBox.getSelectedId() == 2);
    };

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        keyRootBox.addItem (pitchClassName (pitchClass), pitchClass + 1);

    keyRootBox.setSelectedId (1, juce::dontSendNotification);
    keyRootBox.setTooltip (utf8 ("プロジェクトのキー（コードパッドと同じ値）"));
    keyRootBox.onChange = onKeyBoxChanged;
    addAndMakeVisible (keyRootBox);

    keyModeBox.addItem ("Major", 1);
    keyModeBox.addItem ("Minor", 2);
    keyModeBox.setSelectedId (1, juce::dontSendNotification);
    keyModeBox.setTooltip (utf8 ("メジャー／マイナー（コードパッドと同じ値）"));
    keyModeBox.onChange = onKeyBoxChanged;
    addAndMakeVisible (keyModeBox);

    // メトロノーム（Phase 38）。テンポ・拍子の隣に置く（どれも拍に関わる設定）。
    // **記号（♩など）は使わない**：この環境のフォントに無く化ける（HANDOVER 1.30）。
    metronomeButton.setButtonText (utf8 ("拍"));
    metronomeButton.setIconResource ("transport_metronome_svg");   // 8.133（Phase 169）
    metronomeButton.setTooltip (utf8 ("メトロノーム（再生・録音中にクリックを鳴らす）\n"
                                      "右クリックで音量とカウントインを設定"));
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
    metronomeButton.onClick = [this]
    {
        if (onMetronomeToggled != nullptr)
            onMetronomeToggled (metronomeButton.getToggleState());
    };
    metronomeButton.onRightClick = [this]
    {
        if (onMetronomeSettingsRequested != nullptr)
            onMetronomeSettingsRequested (localAreaToGlobal (metronomeButton.getBounds()));
    };
    addAndMakeVisible (metronomeButton);

    // 仕様書5.9：ループ再生（Phase 48）。**記号は使わない**（1.30）ので文字にする
    loopButton.setButtonText (utf8 ("ループ"));
    loopButton.setIconResource ("transport_loop_svg");             // 8.133（Phase 169）
    loopButton.setTooltip (utf8 ("ループ再生の入切（/）\n"
                                  "範囲はルーラー上端の帯をドラッグして決めます"));
    loopButton.setClickingTogglesState (true);
    loopButton.setColour (juce::TextButton::buttonOnColourId, AppColours::orange);
    loopButton.onClick = [this]
    {
        if (onLoopToggled != nullptr)
            onLoopToggled();
    };
    addAndMakeVisible (loopButton);
}

void TransportBarComponent::setMetronomeEnabled (bool isEnabled)
{
    metronomeButton.setToggleState (isEnabled, juce::dontSendNotification);
}

void TransportBarComponent::setLoopEnabled (bool isEnabled)
{
    loopButton.setToggleState (isEnabled, juce::dontSendNotification);
}

void TransportBarComponent::setCountingIn (bool isCountingIn)
{
    // 仕様書5.4：カウントイン中は「まだ録れていない」ことを出す（Phase 39）。
    // Recボタンは既にオレンジなので、時間表示のほうを使って区別する。
    timeLabel.setTextColour (isCountingIn ? AppColours::orange : AppColours::textPrimary);

    if (isCountingIn)
        timeLabel.setText (utf8 ("カウント中"));
    else
        lastShownTenths = -1; // 次のsetPlayheadSeconds()で必ず書き直させる
}


//==============================================================================
void TransportBarComponent::setMasterVolumeDb (float volumeDb)
{
    // Consoleのマスターフェーダーやオートメーションで動いた値も、ここへ流れてくる。
    // ガードが無いと、その反映がまたモデルへの書き戻しを呼ぶ。
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);
    masterVolumeSlider.setValue (volumeDb, juce::dontSendNotification);
}

void TransportBarComponent::setPlayheadSeconds (double seconds)
{
    // 再生中は毎フレーム呼ばれるので、表示が変わらないときは何もしない
    const int tenths = (int) (juce::jmax (0.0, seconds) * 10.0);

    if (tenths == lastShownTenths)
        return;

    lastShownTenths = tenths;

    const int totalSeconds = tenths / 10;

    juce::String text;
    text << juce::String (totalSeconds / 60).paddedLeft ('0', 2)
         << ":" << juce::String (totalSeconds % 60).paddedLeft ('0', 2)
         << "." << juce::String (tenths % 10);

    timeLabel.setText (text);
}

void TransportBarComponent::setTempoAndTimeSignature (double tempo, const juce::String& timeSignature)
{
    // 編集中は書き換えない（打っている途中で確定値へ戻ってしまうため）
    if (! tempoLabel.isBeingEdited())
        tempoLabel.setText (juce::String (tempo, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."),
                             juce::dontSendNotification);

    if (! timeSignatureLabel.isBeingEdited())
        timeSignatureLabel.setText (timeSignature, juce::dontSendNotification);
}

void TransportBarComponent::setProjectKey (int root, bool minor, bool hasChordTrack)
{
    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    keyRootBox.setSelectedId (juce::jlimit (0, 11, root) + 1, juce::dontSendNotification);
    keyModeBox.setSelectedId (minor ? 2 : 1, juce::dontSendNotification);

    // **コードトラックが無いときは押せなくする。** キーの置き場所が無いので、
    // 選んでも何も起きない（`ProjectModel::setProjectKey()`がfalseを返す）
    keyRootBox.setEnabled (hasChordTrack);
    keyModeBox.setEnabled (hasChordTrack);
    keyCaption.setEnabled (hasChordTrack);
}


void TransportBarComponent::setPlayingState (bool isPlaying)
{
    playButton.setButtonText (isPlaying ? "Stop" : "Play");

    // 8.132：**文字と記号は必ず一緒に変えること**（Phase 168／実験枠の44）。
    // 片方だけ変えると、記号へ切り替えたとき／文字へ戻したときに食い違います
    playButton.setIcon (isPlaying ? TransportIcons::Icon::stop : TransportIcons::Icon::play);
    playButton.setColour (juce::TextButton::buttonColourId, isPlaying ? AppColours::orange : AppColours::background);

    // 再生中はオレンジで塗るので、文字は白（Recボタンと同じ扱い）。
    // **既定任せにしないこと。** Phase 53で画面全体の既定色をパレットから
    // 作るようにしたため、指定しないとオレンジの上に濃い文字が載る。
    //
    // 8.133：**止まっているときの▶はパープル**（Phase 169／本人の指定）。
    // 記号の色は文字の色から取っている（8.132）ので、ここを変えれば絵にも効きます。
    // **再生中は白のまま**——オレンジの地の上でパープルは沈みます（1.34）
    playButton.setColour (juce::TextButton::textColourOffId,
                           isPlaying ? juce::Colours::white : AppColours::purple);
}

void TransportBarComponent::setRecordingState (bool isRecording, double recordedSeconds)
{
    // 録音中は塗りつぶし＋白文字にして、ひと目で状態が分かるようにする
    recordButton.setColour (juce::TextButton::buttonColourId, isRecording ? AppColours::orange : AppColours::background);
    recordButton.setColour (juce::TextButton::textColourOffId, isRecording ? juce::Colours::white : AppColours::orange);
    recordButton.setButtonText (isRecording ? "Stop" : "Rec");
    recordButton.setIcon (isRecording ? TransportIcons::Icon::stop : TransportIcons::Icon::record);

    if (isRecording)
    {
        const int totalSeconds = (int) recordedSeconds;
        juce::String text;
        text << juce::String (totalSeconds / 60).paddedLeft ('0', 2)
             << ":" << juce::String (totalSeconds % 60).paddedLeft ('0', 2);
        recordTimeLabel.setText (text, juce::dontSendNotification);
    }
    else
    {
        recordTimeLabel.setText ({}, juce::dontSendNotification);
    }

    repaint();
}

void TransportBarComponent::setInspectorPanelOpen (bool isOpen)
{
    applyPanelToggleColours (inspectorButton, isOpen);
    repaint();
}

void TransportBarComponent::setEditorPanelOpen (bool isOpen)
{
    applyPanelToggleColours (editorButton, isOpen);
    repaint();
}

void TransportBarComponent::setConsolePanelOpen (bool isOpen)
{
    applyPanelToggleColours (consoleButton, isOpen);
    repaint();
}

void TransportBarComponent::setChordPadPanelOpen (bool isOpen)
{
    applyPanelToggleColours (chordPadButton, isOpen);
    repaint();
}

void TransportBarComponent::setBrowserPanelOpen (bool isOpen)
{
    applyPanelToggleColours (browserButton, isOpen);
    repaint();
}

//==============================================================================
void TransportBarComponent::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::panel);

    // 上端に境目を引く（トップバーが下端に引いているのと対になる）
    g.setColour (AppColours::border);
    g.drawLine (0.0f, 0.5f, (float) getWidth(), 0.5f);
}

void TransportBarComponent::resized()
{
    auto full = getLocalBounds().reduced (10, 6);

    //==========================================================================
    // 上段：パネルの開閉（Phase 66／8.27）。
    //
    // 8.118：**中央にまとめるのをやめ、左端と右端へ分けた**（Phase 153／改善案31）。
    //
    // Phase 66でこの段を作ったときは5つまとめて中央だったが、
    // **Inspectorだけは開く場所が画面の左**で、残りは下（Editor・Chord Pad・Console）と
    // 右（Browser）。ボタンの位置と、開いたパネルが出てくる向きを合わせてある。
    {
        auto toggleRow = full.removeFromTop (toggleRowHeight);
        constexpr int gap = 4;

        // 左端：Inspector（開く場所が画面の左なので、こちらだけ独立させる）
        inspectorButton.setBounds (toggleRow.removeFromLeft (juce::jmin (82, toggleRow.getWidth())));

        // 右端：Editor → Chord Pad → Console → Browser（改善案31の指定どおりの並び）。
        // **右端から積むのではなく、必要な幅を右から取ってから左詰めで置く**：
        // 逆順に積むと、幅が足りないときにどれが欠けるか読みにくくなる
        const int buttonWidths[] = { 72, 88, 78, 78 };
        juce::TextButton* buttons[] = { &editorButton, &chordPadButton,
                                         &consoleButton, &browserButton };

        int totalWidth = gap * (juce::numElementsInArray (buttonWidths) - 1);

        for (auto width : buttonWidths)
            totalWidth += width;

        auto row = toggleRow.removeFromRight (juce::jmin (totalWidth, toggleRow.getWidth()));

        for (int i = 0; i < juce::numElementsInArray (buttons); ++i)
        {
            buttons[i]->setBounds (row.removeFromLeft (juce::jmin (buttonWidths[i], row.getWidth())));
            row.removeFromLeft (juce::jmin (gap, row.getWidth()));
        }
    }

    full.removeFromTop (4);

    auto area = full;

    //==========================================================================
    // 下段・左：マスター音量（Phase 37）と時間表示
    {
        auto masterArea = area.removeFromLeft (170);
        masterVolumeCaption.setBounds (masterArea.removeFromTop (12));
        masterVolumeSlider.setBounds (masterArea.reduced (0, 1));
    }

    area.removeFromLeft (14);

    timeLabel.setBounds (area.removeFromLeft (96));
    area.removeFromLeft (10);

    //==========================================================================
    // 8.118：**演奏ボタンはバーの中央**（Phase 153／改善案45）。
    //
    // 並びは「先頭へ戻す→Rec→Play」。左から右へ「戻す・録る・鳴らす」と
    // 手順の順に並ぶ。
    //
    // **中央は`getWidth()`の中央**で、左右のブロックの真ん中ではない。
    // 左右で幅が違うので、残りの真ん中を取ると**バーの中央から常にずれる**。
    //
    // **録音時間のラベルは中央のまとまりに入れない。** 入れると録音の開始・停止で
    // まとまりの幅が変わり、Phase 66でこの段を分ける理由になった
    // 「録音時間の有無で位置がずれる」がそのまま戻ってくる（8.27）。
    // 8.133：**3つとも録音ボタンと同じ幅に揃えた**（Phase 169／本人の指定）。
    //
    // 文字だったころは幅がまちまちでよかった（"|<"と"Play"では要る幅が違う）が、
    // **絵にすると中身の大きさは同じ**なので、幅が違うと絵の間隔だけがばらつきます
    constexpr int transportButtonWidth = 56;
    constexpr int transportGap = 4;
    const int transportWidth = transportButtonWidth * 3 + transportGap * 2;

    auto transportRow = juce::Rectangle<int> (0, area.getY(), getWidth(), area.getHeight())
                            .withSizeKeepingCentre (transportWidth, area.getHeight());

    // 左のブロックに食い込むときだけ、その右へ逃がす（狭い窓での保険）
    if (transportRow.getX() < area.getX())
        transportRow.setX (area.getX());

    const auto transportBounds = transportRow;

    goToStartButton.setBounds (transportRow.removeFromLeft (transportButtonWidth));
    transportRow.removeFromLeft (transportGap);
    recordButton.setBounds (transportRow.removeFromLeft (transportButtonWidth));
    transportRow.removeFromLeft (transportGap);
    playButton.setBounds (transportRow.removeFromLeft (transportButtonWidth));

    // 録音時間は中央のまとまりのすぐ右（Recの並びから目が動かずに読める位置）
    recordTimeLabel.setBounds (transportBounds.getRight() + 6, area.getY(), 52, area.getHeight());

    //==========================================================================
    // 右：キー・テンポ・拍子・ループ・メトロノーム（どれも「曲全体の前提」）。
    //
    // **スナップはここには置いていない**（Phase 55で編集画面側へ移した）。
    // 刻みを変えるのは編集している最中なので、ツールの隣にあるほうが手が届く。
    // エディタをポップアウトすると、この帯は別ウィンドウの外に出てしまうのも理由（8.15）。
    //
    // 8.118：**幅が足りないときに詰めるのはこちら**（Phase 153／改善案45）。
    // 中央の演奏ボタンを動かさないと決めたので、余りを受け取るのは右側になる。
    //
    // **入りきらないものは細くせず、消す。** 細くすると、コンボが数pxの隙間になって
    // 「見えているのに選べない」状態になる。まとまり（キーなら見出し＋ルート＋メジャー）は
    // **まとめて消す**：片方だけ残ると、何の設定か読めなくなる
    area.setLeft (juce::jmax (area.getX(), recordTimeLabel.getRight() + 10));

    // 縦の詰めは項目ごとに違う（見出しは行いっぱい、地色を敷くラベルとコンボは少し内側）
    struct RightItem { juce::Component* component; int width; int verticalInset; };

    // 右端から順に置く。**よく触るものほど右**（メトロノームとループは演奏中に押す）
    const auto placeGroup = [&area] (int leadingGap, std::initializer_list<RightItem> items)
    {
        const int count = (int) items.size();
        int needed = leadingGap + 4 * (count - 1);

        for (const auto& item : items)
            needed += item.width;

        if (area.getWidth() < needed)
        {
            for (const auto& item : items)
                item.component->setVisible (false);

            return;
        }

        area.removeFromRight (leadingGap);

        // 右から幅を取るので、**まとまりの中は右端の項目から**置いていく
        for (int i = count - 1; i >= 0; --i)
        {
            const auto& item = items.begin()[i];

            item.component->setVisible (true);
            item.component->setBounds (area.removeFromRight (item.width).reduced (0, item.verticalInset));

            if (i > 0)
                area.removeFromRight (4);
        }
    };

    placeGroup (0,  { { &metronomeButton, 44, 3 } });
    placeGroup (6,  { { &loopButton, 62, 3 } });                       // 仕様書5.9（Phase 48）
    placeGroup (10, { { &timeSignatureLabel, 52, 4 } });
    placeGroup (6,  { { &tempoCaption, 32, 0 }, { &tempoLabel, 58, 4 } });

    // 仕様書5.11.1：キーはBPMの左（Phase 63／8.1のC9）。
    // テンポ・拍子と並べているのは、3つとも「曲全体の前提」だから
    placeGroup (10, { { &keyCaption, 28, 0 }, { &keyRootBox, 52, 4 }, { &keyModeBox, 66, 4 } });
}
