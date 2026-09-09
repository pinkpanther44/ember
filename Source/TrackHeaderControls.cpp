#include "TrackHeaderControls.h"
#include "AppColours.h"
#include "Utf8.h"

namespace
{
    /** 音量を人が読む形にする。**ConsoleのdB表示と同じ文言にすること**
        （同じ値が画面で違う書き方をされると、どちらが正しいのか確かめようがない）。 */
    juce::String formatVolumeDb (float volumeDb)
    {
        return volumeDb <= -60.0f ? utf8 ("-∞") : juce::String (volumeDb, 1);
    }

}

//==============================================================================
TrackHeaderControls::TrackHeaderControls (ProjectModel& projectToUse)
    : project (projectToUse)
{
    // **この器自身はクリックを受け取らない**（上段の左半分は親が ● A S M を描いている）。
    // 第2引数がtrueなので、**中のつまみは今までどおり受け取る**
    setInterceptsMouseClicks (false, true);

    //==========================================================================
    // 仕様書5.7：音量フェーダー。**Consoleのものと同じ設定**（8.61）

    volumeSlider.setRange (-60.0, 6.0, 0.1);
    volumeSlider.setDoubleClickReturnValue (true, 0.0);
    volumeSlider.setDefaultValueDescription ("0 dB");
    volumeSlider.setTooltip (utf8 ("音量（dB）。ダブルクリックで0dB／右クリックでメニュー。"
                                    "数値は右のdB表示をクリックしても打ち込めます"));
    volumeSlider.setColour (juce::Slider::trackColourId, AppColours::purple);

    // 8.159：**ホイールでは動かさない**（Phase 197／本人の要望）。
    //
    // トラック一覧を縦にスクロールするとき、カーソルはたいていヘッダーの上にあります。
    // `juce::Slider`は既定でホイールを受けるので、**スクロールしたつもりで
    // 音量が動いて**いました。**掴んで動かすぶんは今までどおり**です。
    //
    // **Consoleのフェーダーは切っていません**（あちらは縦に並んでいて、
    // ホイールで動かすのが手になじんでいるため）。`ValueEntrySlider`側で
    // 切らずにここで切っているのは、そのためです
    volumeSlider.setScrollWheelEnabled (false);

    volumeSlider.onDragStart = [this]
    {
        project.beginAction (utf8 ("音量の変更"));

        if (onTouchStart != nullptr)
            onTouchStart (track.getId(), AutomationTargets::volume);
    };

    volumeSlider.onDragEnd = [this]
    {
        if (onTouchEnd != nullptr)
            onTouchEnd (track.getId(), AutomationTargets::volume);
    };

    volumeSlider.onValueChange = [this]
    {
        if (isUpdatingFromModel || ! track.state.getParent().isValid())
            return;

        // 8.126：**差は書く前に取ること**（Phase 162／改善案35）。
        // 書いた後だと、いつでも0になります
        const float newVolume = (float) volumeSlider.getValue();
        const float deltaDb = newVolume - track.getVolumeDb();

        track.setVolumeDb (newVolume, &project.getUndoManager());

        // まとめて選んでいるときは、他のトラックも同じだけ動かす。
        // **判断は呼び出し側**（誰が選ばれているかを知っているのは向こう）
        if (onVolumeNudged != nullptr)
            onVolumeNudged (track.getId(), deltaDb);

        if (onMixerValueChanged != nullptr)
            onMixerValueChanged();
    };

    addAndMakeVisible (volumeSlider);

    // 8.24：**dB表示が音量の入口**（クリックで打ち込み、右クリックでメニュー）。
    // フェーダーが細いので、つまみの上では打ち込めない
    volumeReadout.setJustificationType (juce::Justification::centredRight);
    volumeReadout.setFont (juce::FontOptions (10.0f));
    volumeReadout.setColour (juce::Label::textColourId, AppColours::textSecondary);
    volumeReadout.setTooltip (utf8 ("クリックして音量（dB）を打ち込む／右クリックでメニュー"));
    volumeReadout.onLeftClick = [this] { volumeReadout.showEditor(); };
    volumeReadout.onRightClick = [this] { volumeSlider.showValueMenu(); };

    // 開いた瞬間は**単位を外した数字だけ**にする
    volumeReadout.onEditorShow = [this]
    {
        if (auto* editor = volumeReadout.getCurrentTextEditor())
        {
            editor->setText (juce::String (track.getVolumeDb(), 1), false);
            editor->selectAll();
        }
    };

    volumeReadout.onTextChange = [this]
    {
        // 適用はスライダー側へ通す（Undoの区切りとTouch/Latchが1箇所に揃う）
        volumeSlider.applyTextValue (volumeReadout.getText());
        updateControlsFromModel();
    };

    addAndMakeVisible (volumeReadout);

    //==========================================================================
    // 仕様書5.7：レベルメーター。**縦にして右端へ**（Phase 99／改善案⑩）。
    //
    // 横向きだと、下段の幅をフェーダーと取り合っていました。縦にして右端へ寄せると
    // **上下2段ぶんの高さ**が使えるので、振れ幅が読み取れるようになります。
    // **数字は出しません**（8pxしかないので、印だけ。`setShowPeakText`のコメント参照）
    meter.setVertical (true);
    addAndMakeVisible (meter);
}

TrackHeaderControls::~TrackHeaderControls()
{
    track.state.removeListener (this);
}

//==============================================================================
void TrackHeaderControls::setTrack (const Track& newTrack)
{
    if (track.state == newTrack.state)
        return;   // 並べ直しはタイマーから毎回来るので、素通りできるようにしてある

    // **古いほうの購読を先に外す**（外し忘れると、消したトラックのツリーを掴んだまま残る。1.15）
    track.state.removeListener (this);
    track = newTrack;
    track.state.addListener (this);

    updateVisibilityForType();
    updateControlsFromModel();
}

void TrackHeaderControls::setPlayheadSeconds (double seconds)
{
    if (juce::approximatelyEqual (seconds, playheadSeconds))
        return;

    playheadSeconds = seconds;

    // 8.54：**点が書かれているトラックだけ引き直す**（Phase 93）。
    // 再生中は毎フレーム来るので、無条件に流し込むと本数ぶんだけ無駄が積み上がる。
    // 8.64：**見ているのは音量だけ**（Phase 102でパンを外した）
    if (! track.findAutomationLane (AutomationTargets::volume).isEmpty())
        updateControlsFromModel();
}

void TrackHeaderControls::updateVisibilityForType()
{
    // **種別に無いものは出さない**（8.60／改善案⑪と同じ判断）。
    // `ChannelStripComponent`の出し分けと揃えること
    const auto type = track.getType();
    const bool hasAudioPath = (type == TrackType::Audio || type == TrackType::Midi
                                || type == TrackType::Send || type == TrackType::Folder
                                || type == TrackType::DrumOut);   // 8.145（Phase 183）

    // VCAはフェーダーを持つが、音声を通さないのでメーターは振れない
    const bool hasVolume = (type != TrackType::Chord);

    volumeSlider.setVisible (hasVolume);
    volumeReadout.setVisible (hasVolume);
    meter.setVisible (hasAudioPath);

    if (! hasAudioPath)
        meter.setLevels (0.0f, 0.0f);   // 1.28：止まっている間の値を残さない
}

void TrackHeaderControls::updateControlsFromModel()
{
    if (! track.state.getParent().isValid())
        return;

    const juce::ScopedValueSetter<bool> guard (isUpdatingFromModel, true);

    // 8.54：**その再生位置で効いている値**（Phase 93）。Console・インスペクタと同じ関数を通す
    const float volumeDb = track.getEffectiveVolumeDbAt (playheadSeconds);

    // **掴んでいる最中のつまみは動かさない**（触っている人が優先）
    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (volumeDb, juce::dontSendNotification);

    // **打っている途中は書き換えない**（確定前の文字が消える。8.53と同じ話）
    if (volumeReadout.getCurrentTextEditor() == nullptr)
        volumeReadout.setText (formatVolumeDb (volumeDb), juce::dontSendNotification);
}

void TrackHeaderControls::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    // 1.42：**プロパティ名だけを見て判定しない。** 種別が変わることもあるので、
    // 出し分けと値の両方を引き直す（どちらも安い）
    updateVisibilityForType();
    updateControlsFromModel();
}

//==============================================================================
void TrackHeaderControls::resized()
{
    auto area = getLocalBounds();

    // 8.65：**メーターは右端で、行の高さいっぱい**（Phase 103）。
    //
    // 上端も下端も**トラック行の端と揃います**（親が名前の行まで含めた領域を
    // 渡してくるようになったので、ここは全高を取るだけ。`getHeaderControlsBounds()`）。
    // 行を高くすれば、そのぶん振れ幅が読み取りやすくなります
    meter.setBounds (area.removeFromRight (meterWidth).reduced (0, 1));
    area.removeFromRight (4);

    // **名前の行は親が描く**ので、こちらは空けるだけ（8.65）
    area.removeFromTop (nameRowHeight);

    // 上段は親が ● A S M を描くので、こちらは何も置かない
    area.removeFromTop (buttonRowHeight);

    // 8.64：**フェーダーは大きさを固定**（Phase 102）。
    // 行の高さで伸び縮みさせると、**行ごとに操作量が変わって**しまいます。
    // 高くしたぶんは空けておく（そこはメーターを読む場所になります）
    auto faderRow = area.removeFromTop (juce::jmin (area.getHeight(), faderRowHeight));

    volumeReadout.setBounds (faderRow.removeFromRight (juce::jmin (readoutWidth, faderRow.getWidth())));
    faderRow.removeFromRight (2);
    volumeSlider.setBounds (faderRow);
}
