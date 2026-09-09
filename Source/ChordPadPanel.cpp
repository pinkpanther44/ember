#include "ChordPadPanel.h"
#include "AppColours.h"
#include "AppSettings.h"
#include "Utf8.h"
#include <cmath>

namespace
{
    // 設計書2.5：発音パラメータの保存先キー（Phase 53／8.1のA3）。
    //
    // **接頭辞を付けてあるのは、settings.xmlが1枚の平たいツリーだから。**
    // "voicing"のような短い名前で置くと、後から別の画面が同じ名前を使ったときに
    // 黙って上書きし合う。
    const juce::String chordPadVoicingKey   { "chordPadVoicing" };
    const juce::String chordPadOctaveKey    { "chordPadOctave" };
    const juce::String chordPadLengthKey    { "chordPadLength" };
    const juce::String chordPadStrokeKey    { "chordPadStroke" };
    const juce::String chordPadGateKey      { "chordPadGate" };
    const juce::String chordPadVelocityKey  { "chordPadVelocity" };
    const juce::String chordPadDeviationKey { "chordPadDeviation" };
}

//==============================================================================
ChordPadPanel::ChordPadPanel (ProjectModel& projectToUse, SelectionState& selectionToUse,
                               AudioEngine& audioEngineToUse)
    : project (projectToUse), selection (selectionToUse), audioEngine (audioEngineToUse)
{
    // 設計書2.3.5：上段の見出し（Phase 65）。何のコンボボックスか分かるようにする
    keyCaption.setText ("Key", juce::dontSendNotification);
    keyCaption.setFont (juce::FontOptions (11.0f));
    keyCaption.setJustificationType (juce::Justification::centredRight);
    keyCaption.setColour (juce::Label::textColourId, AppColours::textSecondary);
    addAndMakeVisible (keyCaption);

    // 配色はAppColoursから取る。直接juce::Coloursを書くと、ダークテーマで沈む（1.34）
    for (auto* box : { &keyRootBox, &keyModeBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, AppColours::background);
        box->setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
        box->setColour (juce::ComboBox::outlineColourId, AppColours::border);
        box->setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);
    }

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        keyRootBox.addItem (pitchClassName (pitchClass), pitchClass + 1);

    keyRootBox.setSelectedId (1, juce::dontSendNotification);
    keyRootBox.onChange = [this]
    {
        auto track = getChordTrack();

        if (! track.state.getParent().isValid())
            return;

        auto key = track.getChordKey();
        key.root = keyRootBox.getSelectedId() - 1;

        project.beginAction (utf8 ("キーの変更"));
        track.setChordKey (key, &project.getUndoManager());

        rebuildGrid();
        repaint();
    };
    addAndMakeVisible (keyRootBox);

    keyModeBox.addItem ("Major", 1);
    keyModeBox.addItem ("Minor", 2);
    keyModeBox.setSelectedId (1, juce::dontSendNotification);
    keyModeBox.onChange = [this]
    {
        auto track = getChordTrack();

        if (! track.state.getParent().isValid())
            return;

        auto key = track.getChordKey();
        key.minor = (keyModeBox.getSelectedId() == 2);

        project.beginAction (utf8 ("キーの変更"));
        track.setChordKey (key, &project.getUndoManager());

        rebuildGrid();
        repaint();
    };
    addAndMakeVisible (keyModeBox);

    pageButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    pageButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    pageButton.onClick = [this]
    {
        page = (page == ChordGridPage::main) ? ChordGridPage::related : ChordGridPage::main;
        pageButton.setButtonText (page == ChordGridPage::main ? "Main" : "Related");

        rebuildGrid();
        repaint();
    };
    addAndMakeVisible (pageButton);

    triadButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    triadButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    triadButton.onClick = [this]
    {
        triadMode = ! triadMode;
        triadButton.setButtonText (triadMode ? "Triad" : "7th");

        rebuildGrid();
        repaint();
    };
    addAndMakeVisible (triadButton);

    // 8.74：直前のコードの枠（Phase 114／仕様書5.11.2）
    previousCaption.setText (utf8 ("直前"), juce::dontSendNotification);
    previousCaption.setFont (juce::FontOptions (11.0f));
    previousCaption.setJustificationType (juce::Justification::centredRight);
    previousCaption.setColour (juce::Label::textColourId, AppColours::textSecondary);

    // **ラベルにマウスを食べさせない。** 枠の当たり判定は`mouseDown()`で見ているので、
    // 見出しがクリックを取ると、そこだけ右クリックで鳴らせなくなる（8.67と同じ話）
    previousCaption.setInterceptsMouseClicks (false, false);
    addChildComponent (previousCaption);

    positionLabel.setFont (juce::FontOptions (12.0f));
    positionLabel.setColour (juce::Label::textColourId, AppColours::textSecondary);
    positionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (positionLabel);

    //==========================================================================
    // 仕様書5.11.3：下段の発音パラメータ（Phase 44）

    for (auto* box : { &targetTrackBox, &voicingBox, &octaveBox, &lengthBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, AppColours::background);
        box->setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
        box->setColour (juce::ComboBox::outlineColourId, AppColours::border);
        box->setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);
        addAndMakeVisible (*box);
    }

    targetTrackBox.setTextWhenNoChoicesAvailable (utf8 ("MIDIトラックなし"));
    targetTrackBox.setTooltip (utf8 ("書き込み先のMIDIトラック"));

    // 仕様書5.11.3のVoicing。ピアノは転回形、ギターはロー／ハイコード
    voicingBox.addItem (utf8 ("Piano 基本形"), 1);
    voicingBox.addItem (utf8 ("Piano 第1転回"), 2);
    voicingBox.addItem (utf8 ("Piano 第2転回"), 3);
    voicingBox.addItem (utf8 ("Piano 第3転回"), 4);
    voicingBox.addItem ("Guitar Low", 5);
    voicingBox.addItem ("Guitar High", 6);
    voicingBox.setSelectedId (1, juce::dontSendNotification);
    voicingBox.setTooltip (utf8 ("Voicing：ピアノは転回形、ギターはロー／ハイコード"));

    for (int octave = -2; octave <= 2; ++octave)
        octaveBox.addItem ((octave > 0 ? "+" : "") + juce::String (octave) + " oct", octave + 3);

    octaveBox.setSelectedId (3, juce::dontSendNotification);   // 0
    octaveBox.setTooltip (utf8 ("Octave：ボイシング全体のオクターブ移動"));

    // 仕様書5.11.3のNote（音価）。IDの順は getPerformance() の表と対にすること
    lengthBox.addItem (utf8 ("コード区間"), 1);
    lengthBox.addItem (utf8 ("全音符"), 2);
    lengthBox.addItem (utf8 ("2分音符"), 3);
    lengthBox.addItem (utf8 ("4分音符"), 4);
    lengthBox.addItem (utf8 ("8分音符"), 5);
    lengthBox.addItem (utf8 ("16分音符"), 6);
    lengthBox.addItem (utf8 ("32分音符"), 7);
    lengthBox.setSelectedId (1, juce::dontSendNotification);
    lengthBox.setTooltip (utf8 ("Note：1音の音価。「コード区間」は区間いっぱいのロングトーン"));

    // 8.73：ストロークは**トグル3つ**（Phase 113／仕様書5.11.3）。
    // いちばんよく切り替えるものなので、開いて選ぶ2手を無くし、
    // **いまどれなのかがひと目で分かる**ようにする
    strokeNoneButton.setButtonText (utf8 ("なし"));
    strokeNoneButton.setTooltip (utf8 ("ストロークなし（同時に鳴らす）"));
    strokeDownButton.setTooltip (utf8 ("ダウンストローク（低い音から順に鳴らす）"));
    strokeUpButton.setTooltip (utf8 ("アップストローク（高い音から順に鳴らす）"));

    struct { LeftClickOnlyButton* button; ChordStroke value; } strokeToggles[] =
    {
        { &strokeNoneButton, ChordStroke::none },
        { &strokeDownButton, ChordStroke::down },
        { &strokeUpButton,   ChordStroke::up   },
    };

    for (auto& t : strokeToggles)
    {
        // **見た目はこちらで手動制御する**（3つで1つの選択なので、
        // `setClickingTogglesState(true)`だと押すたびに勝手に反転してしまう）
        t.button->setClickingTogglesState (false);
        t.button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        t.button->setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
        t.button->setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
        t.button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);

        const auto value = t.value;
        t.button->onClick = [this, value] { setStroke (value); };

        addAndMakeVisible (*t.button);
    }

    auto setUpSlider = [this] (juce::Slider& slider, double minimum, double maximum,
                                double interval, double initial, const juce::String& suffix,
                                const juce::String& tooltip)
    {
        slider.setSliderStyle (juce::Slider::LinearBar);
        slider.setRange (minimum, maximum, interval);
        slider.setValue (initial, juce::dontSendNotification);
        slider.setTextValueSuffix (suffix);
        slider.setColour (juce::Slider::trackColourId, AppColours::purple.withAlpha (0.45f));
        slider.setColour (juce::Slider::backgroundColourId, AppColours::background);
        slider.setColour (juce::Slider::textBoxTextColourId, AppColours::textPrimary);
        slider.setTooltip (tooltip);
        addAndMakeVisible (slider);
    };

    setUpSlider (gateSlider, 5.0, 100.0, 1.0, 80.0, "%",
                 utf8 ("GT%：音価に対する実際の発音長の割合"));
    setUpSlider (velocitySlider, 1.0, 127.0, 1.0, 100.0, "",
                 utf8 ("Vel：MIDIベロシティ"));
    setUpSlider (deviationSlider, 0.0, 0.25, 0.01, 0.03, utf8 ("拍"),
                 utf8 ("Dev：ストローク時の、音同士のずらし幅"));

    writeButton.setColour (juce::TextButton::buttonColourId, AppColours::purple);
    writeButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    // 8.73：**次にWriteで出るコード**（Phase 113）。Writeボタンのすぐ左に置く
    writeTargetLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    writeTargetLabel.setJustificationType (juce::Justification::centredRight);
    writeTargetLabel.setColour (juce::Label::textColourId, AppColours::orange);
    addAndMakeVisible (writeTargetLabel);

    // 8.72：**押すのは「カーソル上の1コード」だけ**（Phase 112／D12）。
    // 進行まるごとは右クリックのメニューに残してある
    writeButton.setTooltip (utf8 ("再生位置のコードをMIDIノートにして、選んだトラックへ書き込みます"
                                   "（音価を選んでいればその1個ぶん）。"
                                   "書き出したぶんだけカーソルが進むので、押し続けると刻みが組めます。"
                                   "右クリックで「進行をまとめて書き込む」"));
    writeButton.onClick = [this] { writeChordAtCursorToTrack(); };

    writeButton.onRightClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, utf8 ("進行をまとめて書き込む（前のクリップは置き換え）"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&writeButton),
            [this] (int result)
            {
                if (result == 1)
                    writeNotesToTrack();
            });
    };

    addAndMakeVisible (writeButton);

    //==========================================================================
    // 仕様書5.11.2：カスタムコード入力（Phase 45）。
    // グリッドに無いコード（テンション付き・分数コード）を組み立てるための欄。

    for (auto* box : { &customRootBox, &customTypeBox, &customBassBox })
    {
        box->setColour (juce::ComboBox::backgroundColourId, AppColours::background);
        box->setColour (juce::ComboBox::textColourId, AppColours::textPrimary);
        box->setColour (juce::ComboBox::outlineColourId, AppColours::border);
        box->setColour (juce::ComboBox::arrowColourId, AppColours::textSecondary);
        box->onChange = [this] { repaint(); };   // プレビューの名前と色が変わる
        addAndMakeVisible (*box);
    }

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        customRootBox.addItem (pitchClassName (pitchClass), pitchClass + 1);

    customRootBox.setSelectedId (1, juce::dontSendNotification);
    customRootBox.setTooltip (utf8 ("ルート音"));

    // 表示名が空（Maj）のままだと選べないので、無印は "maj" と出す
    for (int type = 0; type < (int) ChordType::NumTypes; ++type)
    {
        const auto suffix = chordTypeSuffix ((ChordType) type);
        customTypeBox.addItem (suffix.isEmpty() ? "maj" : suffix, type + 1);
    }

    customTypeBox.setSelectedId (1, juce::dontSendNotification);
    customTypeBox.setTooltip (utf8 ("コードタイプ（全22種）"));

    customBassBox.addItem (utf8 ("ベース指定なし"), 1);

    for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        customBassBox.addItem ("/" + pitchClassName (pitchClass), pitchClass + 2);

    customBassBox.setSelectedId (1, juce::dontSendNotification);
    customBassBox.setTooltip (utf8 ("分数コードのベース音"));

    // テンションは7種を同時に持てるので、トグルを並べる（仕様書5.11.1のビットマスク）
    struct TensionButton { const char* label; int bit; };
    static const TensionButton tensionLayout[] =
    {
        { "9",   Tension::Nine },        { "b9",  Tension::FlatNine },
        { "#9",  Tension::SharpNine },   { "11",  Tension::Eleven },
        { "#11", Tension::SharpEleven }, { "13",  Tension::Thirteen },
        { "b13", Tension::FlatThirteen }
    };

    for (const auto& entry : tensionLayout)
    {
        auto* button = tensionButtons.add (new LeftClickOnlyButton (entry.label));
        button->setClickingTogglesState (true);
        button->setColour (juce::TextButton::buttonColourId, AppColours::background);
        button->setColour (juce::TextButton::buttonOnColourId, AppColours::purple);
        button->setColour (juce::TextButton::textColourOffId, AppColours::textSecondary);
        button->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        button->onClick = [this] { repaint(); };
        addAndMakeVisible (button);
    }

    customAddButton.setColour (juce::TextButton::buttonColourId, AppColours::background);
    customAddButton.setColour (juce::TextButton::textColourOffId, AppColours::textPrimary);
    customAddButton.setTooltip (utf8 ("組み立てたコードを挿入位置へ入れます"
                                       "（左の枠を直接クリックしても同じ。右クリックで試聴）"));
    customAddButton.onClick = [this] { insertChord (buildCustomChord()); };
    addAndMakeVisible (customAddButton);

    // 8.1のA3：前回の発音パラメータに戻す。**コントロールを作り終えた後で呼ぶこと**
    setUpPerformanceSettingsPersistence();

    selection.addChangeListener (this);
    updateProjectSubscription();

    refreshTargetTrackList();
    rebuildGrid();
}

void ChordPadPanel::setUpPerformanceSettingsPersistence()
{
    // 保存済みの値へ戻す。**既定値はコントロールの現在値**にしてあるので、
    // 設定がまだ無ければ、これまでどおりの初期値のままになる。
    auto restoreBox = [] (juce::ComboBox& box, const juce::String& key)
    {
        const int savedId = AppSettings::getInt (key, box.getSelectedId());

        // 項目が減った後の設定ファイルを読むと、無いIDが入っていることがある。
        // `setSelectedId()`は無いIDを黙って無視するので、そのまま渡してよい
        box.setSelectedId (savedId, juce::dontSendNotification);

        // 選び直したら、その場で保存する（次に開いたときのため）。
        // **参照ではなくポインタで捕まえること。** `box`はこのラムダの引数なので、
        // 参照のまま捕まえると、抜けた時点で捕まえた先が無くなる
        box.onChange = [target = &box, key] { AppSettings::setInt (key, target->getSelectedId()); };
    };

    restoreBox (voicingBox, chordPadVoicingKey);
    restoreBox (octaveBox,  chordPadOctaveKey);
    restoreBox (lengthBox,  chordPadLengthKey);
    // 8.73：ストロークはトグル3つになった（Phase 113）。**キーはそのまま**使い、
    // 値の意味も変えていない（1=なし 2=Down 3=Up）。
    // 4（廃止したAlt）が保存されていたら「なし」へ落とす
    {
        const int savedStroke = AppSettings::getInt (chordPadStrokeKey, 1);

        setStroke (savedStroke == 2 ? ChordStroke::down
                                     : savedStroke == 3 ? ChordStroke::up
                                                        : ChordStroke::none);
    }

    auto restoreSlider = [] (juce::Slider& slider, const juce::String& key)
    {
        slider.setValue (AppSettings::getDouble (key, slider.getValue()),
                          juce::dontSendNotification);

        // **`onValueChange`ではなく`onDragEnd`で保存する。** 前者はドラッグ中の
        // 1ピクセルごとに飛んでくるので、設定ファイルを何十回も書き直すことになる
        // （`AppSettings`は読み書きのたびにファイルを開き直す作り）。
        slider.onDragEnd = [target = &slider, key] { AppSettings::setDouble (key, target->getValue()); };
    };

    restoreSlider (gateSlider,      chordPadGateKey);
    restoreSlider (velocitySlider,  chordPadVelocityKey);
    restoreSlider (deviationSlider, chordPadDeviationKey);
}

ChordPadPanel::~ChordPadPanel()
{
    // 鳴らしっぱなしのまま壊れない（1.4）
    stopAudition();

    // 破棄後に通知が届かないよう、購読は先に外す
    selection.removeChangeListener (this);
    subscribedProjectState.removeListener (this);
}

void ChordPadPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // 選択が別のコードトラックへ移ると、対象のキーも変わる
    refreshFromModel();
}

//==============================================================================
void ChordPadPanel::updateProjectSubscription()
{
    auto currentState = project.getState();

    if (subscribedProjectState == currentState)
        return; // 既に今のツリーを見ている

    subscribedProjectState.removeListener (this);
    subscribedProjectState = currentState;
    subscribedProjectState.addListener (this);
}

bool ChordPadPanel::affectsPads (const juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (IDs::CHORDREGION))
        return property == IDs::chordRoot || property == IDs::chordType
            || property == IDs::chordTensions || property == IDs::chordBass
            || property == IDs::chordRegionStartBeats || property == IDs::chordRegionLengthBeats;

    if (tree.hasType (IDs::TRACK))
        return property == IDs::chordKeyRoot || property == IDs::chordKeyMinor
            || property == IDs::trackName || property == IDs::trackType;

    // テンポと拍子は1小節の長さ＝挿入位置の刻みを変える
    if (tree.hasType (IDs::PROJECT))
        return property == IDs::tempo || property == IDs::timeSignature;

    // 8.106：**変化点の中身が変わったときも組み直す**（Phase 143）。
    //
    // **Phase 142で開けた穴です。** レーンで変化点を足せるようにしたのに、
    // ここを広げ忘れていたので、**テンポを途中で変えても「◯小節 ◯拍」の表示が
    // 古いまま**でした（8.12「購読を広げたら、配る先も見直す」）。
    return ::isSignatureLaneNode (tree);
}


void ChordPadPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (! affectsPads (tree, property))
        return;

    // 書き込み先の一覧はトラック名で作っているので、名前と種別の変化でも作り直す
    if (property == IDs::trackName || property == IDs::trackType)
        refreshTargetTrackList();

    rebuildGrid();
    updateHeaderText();
    repaint();
}

void ChordPadPanel::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    // ノートが1音増えるたびに組み直さない（書き込み1回で数百回になる）
    // 8.106：**変化点を足したときも組み直す**（Phase 143）。プロパティだけ見ていると、
    // 「テンポの変更を追加」でパッドの表示が古いまま残ります
    if (! child.hasType (IDs::CHORDREGION) && ! child.hasType (IDs::TRACK)
         && ! ::isSignatureLaneNode (child))
        return;

    if (child.hasType (IDs::TRACK))
        refreshTargetTrackList();

    rebuildGrid();
    updateHeaderText();
    repaint();
}

void ChordPadPanel::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (! child.hasType (IDs::CHORDREGION) && ! child.hasType (IDs::TRACK)
         && ! ::isSignatureLaneNode (child))
        return;

    if (child.hasType (IDs::TRACK))
        refreshTargetTrackList();

    rebuildGrid();
    updateHeaderText();
    repaint();
}

void ChordPadPanel::visibilityChanged()
{
    // HANDOVER 1.10：visibilityChanged()は親からは飛んでこないので、
    // EditorPanelが明示的に自分のsetVisible()を呼んでくれることを前提にしている。
    if (isVisible())
    {
        refreshFromModel();
        return;
    }

    // 試聴中にパネルが隠れると、離すマウスイベントが来ない（1.4）
    stopAudition();
}

void ChordPadPanel::refreshFromModel()
{
    updateProjectSubscription();   // プロジェクトを読み込むとルートが差し替わる

    auto track = getChordTrack();

    if (track.state.getParent().isValid())
    {
        const auto key = track.getChordKey();
        keyRootBox.setSelectedId (key.root + 1, juce::dontSendNotification);
        keyModeBox.setSelectedId (key.minor ? 2 : 1, juce::dontSendNotification);
    }

    refreshTargetTrackList();   // Phase 44：トラックが増減している可能性がある
    rebuildGrid();
    updateHeaderText();
    repaint();
}

void ChordPadPanel::setInsertPosition (double seconds)
{
    const double clamped = juce::jmax (0.0, seconds);

    if (juce::approximatelyEqual (clamped, insertPositionSeconds))
        return;

    insertPositionSeconds = clamped;

    // 直前のコードが変われば、パッドの色がすべて変わる
    rebuildGrid();
    updateHeaderText();
    repaint();
}

//==============================================================================
Track ChordPadPanel::getChordTrack() const
{
    // 選択されているのがコードトラックなら、それを対象にする。
    // 2本以上あるときに「見ているトラック」と「入る先」がずれないようにするため。
    if (selection.getTrackId().isNotEmpty())
    {
        auto selected = project.findTrackById (selection.getTrackId());

        if (selected.state.getParent().isValid() && selected.getType() == TrackType::Chord)
            return selected;
    }

    return project.findChordTrack();
}

double ChordPadPanel::getBarSeconds (double atTime) const
{
    return project.getBarSecondsAt (atTime);
}

double ChordPadPanel::getChordInsertPosition() const
{
    // 8.75：**寄せません。カーソルの位置そのものです**（Phase 115）。
    //
    // Phase 114まで小節線へ寄せていました（Phase 112は手前へ、113はいちばん近い方へ）。
    // どちらの寄せ方でも、**寄せた先が別のコード区間に入ってしまう**形が残ります：
    //
    //   Am7［2小節〜］→ D7［2小節の途中〜］　でカーソルをD7の頭に置くと、
    //   寄せた位置は3小節＝**D7の中**。「直前」を探す起点がD7になる。
    //
    // Phase 114で「その区間の頭より前を探す」という手当てもしましたが、
    // **寄せること自体が、画面のカーソルと中の位置を食い違わせる**元でした。
    //
    // **区間は小節の途中からでも作れます**（アレンジ画面で端をドラッグできる）。
    // 寄せる前提そのものが、この画面の作りと合っていません。
    //
    return juce::jmax (0.0, insertPositionSeconds);
}

const Chord* ChordPadPanel::getPreviousChord() const
{
    auto track = getChordTrack();

    if (! track.state.getParent().isValid())
        return nullptr;

    const double at = getChordInsertPosition();

    // 8.140：**カーソルより前にある区間を、そのまま探します**（Phase 178／本人の報告）。
    //
    // ### Phase 115で消えたはずの手当てが残っていました
    //
    // Phase 114は「いま乗っている区間の**頭より前**」を探していました（8.74）。
    // 当時は挿入位置を**小節線へ寄せていた**ので、区間が小節の途中から始まっていると
    // 寄せた位置がその区間の中へ入り、「手前」として**その区間自身**が返ったためです
    //   （例：Am7［2小節］→ D7［2小節の途中から］で、挿入位置が3小節のとき、
    //     直前がAm7ではなくD7になっていた）。
    //
    // **Phase 115で寄せるのをやめました**（8.75）。原因が消えたのに手当てだけが残り、
    // 今度はそれが**逆向きのズレ**を作っていました：
    //
    //   F［1小節］→ Em［2小節］→ Am［3小節］でカーソルが**2小節3拍**のとき、
    //   「乗っている区間（Em）の頭より前」＝ F が「直前」として出る。
    //   **鳴っているのはEmなのに、Fと書いてある。**
    //
    // 見た目には「**小節の頭に来るまで直前コードが変わらない**」＝1つ遅れて追いてくる、
    // という形で出ます（本人の報告どおり）。
    //
    // ### 寄せなくなった今は、素直に探すだけで両方とも正しくなります
    //
    // | カーソル | `findChordRegionBefore()`が返すもの |
    // |---|---|
    // | Emの**頭ちょうど**（＝Emを差し替える） | F（Emの1つ前）。8.74の意図どおり |
    // | Emの**途中** | **Em**（いま鳴っているコード） |
    // | 区間と区間の**隙間** | 手前の区間 |
    //
    // **1マイクロ秒ぶん手前から探すこと。** 区間の頭ちょうどに置いたつもりでも、
    // 拍と秒の往復でカーソルが髪の毛ほど後ろに落ちることがあります。
    // そのとき「差し替える対象そのもの」が直前として出てしまいます
    auto previous = track.findChordRegionBefore (at - 1.0e-6);

    // 8.74：枠に出すぶんも一緒に覚える（Phase 114）。**探し直しを2箇所に書かない**（8.2）
    previousChordRegion = previous;

    if (! previous.state.isValid())
        return nullptr;

    previousChord = previous.getChord();
    return &previousChord;
}

void ChordPadPanel::rebuildGrid()
{
    auto track = getChordTrack();

    // 8.106：**パッドは「入れる場所のキー」で組む**（Phase 143／改善案㉔㉕）。
    // 転調の先へカーソルを移したら、そこで使えるコードが出てほしい——
    // 上段のKey欄が出しているのは**曲の頭のキー**（＝曲の設定）で、別のものです
    const Scale key = track.state.getParent().isValid()
                        ? project.getProjectKeyAt (getChordInsertPosition())
                        : Scale();

    grid = buildChordGrid (key, page, triadMode);
    layoutPads();
}

//==============================================================================
juce::Rectangle<int> ChordPadPanel::getGridArea() const
{
    // **段の並びは`resized()`と対。** 片方だけ直すと、パッドが段の下に潜る
    auto area = getLocalBounds();
    area.removeFromTop (controlRowHeight);     // キー・ページ・挿入位置
    area.removeFromTop (getWriteAreaHeight());  // 仕様書5.11.3の発音パラメータ（Phase 63で上段へ。8.73で2段になり得る）
    area.removeFromBottom (customRowHeight);   // 仕様書5.11.2のカスタムコード入力（Phase 45）
    return area;
}

void ChordPadPanel::layoutPads()
{
    pads.clear();
    rowAreas.clear();

    if (grid.rows.empty())
        return;

    auto area = getGridArea();

    if (area.getWidth() <= rowLabelWidth || area.getHeight() <= columnHeaderHeight)
        return;

    area.removeFromTop (columnHeaderHeight);   // 列見出し（ルートからの半音）

    const int rowHeight = juce::jmax (1, area.getHeight() / (int) grid.rows.size());
    const int padAreaWidth = area.getWidth() - rowLabelWidth;

    for (size_t r = 0; r < grid.rows.size(); ++r)
    {
        auto rowArea = area.withY (area.getY() + (int) r * rowHeight).withHeight (rowHeight);

        // **行そのものを覚える**（交互の地を塗るのに使う。Phase 65）。
        // 見出しの矩形は、そこから左端を切り出せば出せる
        rowAreas.push_back (rowArea);
        rowArea.removeFromLeft (rowLabelWidth);

        for (int col = 0; col < numColumns; ++col)
        {
            const auto& cell = grid.rows[r].cells[(size_t) col];

            if (! cell.hasChord)
                continue;   // 理論的に候補が無い度数は空のまま（歯抜けグリッド）

            // 端数を最後の列へ押し付けず、左端の位置から毎回計算する。
            // 列ごとに幅を足していくと、12列ぶんで数ピクセルずれる。
            const int x = rowArea.getX() + (col * padAreaWidth) / numColumns;
            const int right = rowArea.getX() + ((col + 1) * padAreaWidth) / numColumns;

            Pad pad;
            pad.chord = cell.chord;
            pad.area = juce::Rectangle<int> (x, rowArea.getY(), right - x, rowHeight).reduced (1);
            pads.push_back (pad);
        }
    }
}

int ChordPadPanel::getWriteAreaHeight() const
{
    // 8.73：狭いときは2段（Phase 113）。**`resized()`と同じ判断をここでする**
    return (getWidth() >= writeRowSingleLineWidth) ? writeRowHeight : writeRowHeight * 2;
}

void ChordPadPanel::resized()
{
    // 設計書2.3.5：上段は**キーと挿入位置だけ**（Phase 65）。
    // ページ切り替え（Main／7th）は、書き込み先と同じ段（下）へ移した。
    // 「曲全体の前提（キー）」と「これから何を入れるか」を段で分けている。
    auto controls = getLocalBounds().removeFromTop (controlRowHeight).reduced (6, 3);

    keyCaption.setBounds (controls.removeFromLeft (28));
    controls.removeFromLeft (4);
    keyRootBox.setBounds (controls.removeFromLeft (56));
    controls.removeFromLeft (4);
    keyModeBox.setBounds (controls.removeFromLeft (86));
    controls.removeFromLeft (10);

    // 8.74：**直前のコードの枠**（Phase 114）。キーの右、挿入位置の左。
    // 「いま何の続きを書いているか」なので、上段（曲全体の前提）の側に置く
    {
        auto previousArea = controls.removeFromLeft (juce::jmin (150, controls.getWidth()));

        previousCaption.setBounds (previousArea.removeFromLeft (juce::jmin (36, previousArea.getWidth())));
        previousArea.removeFromLeft (juce::jmin (4, previousArea.getWidth()));

        // 枠は矩形で覚える（パッドと同じ扱いにするため。子コンポーネントにしない）
        previousChordArea = previousChordRegion.state.isValid() ? previousArea : juce::Rectangle<int>();

        previousCaption.setVisible (previousChordRegion.state.isValid());
    }

    controls.removeFromLeft (juce::jmin (10, controls.getWidth()));
    positionLabel.setBounds (controls);

    // 仕様書5.11.2：カスタムコード入力の行（Phase 45）。**いちばん下**
    // （Phase 63で発音パラメータの段が上へ移ったので、下段はこれ1つになった）
    auto customRow = getLocalBounds().removeFromBottom (customRowHeight).reduced (6, 2);

    customAddButton.setBounds (customRow.removeFromRight (52));
    customRow.removeFromRight (6);
    customPreviewArea = customRow.removeFromRight (juce::jmin (120, customRow.getWidth()));
    customRow.removeFromRight (10);

    auto placeCustom = [&customRow] (juce::Component& component, int width)
    {
        component.setBounds (customRow.removeFromLeft (juce::jmin (width, customRow.getWidth())));
        customRow.removeFromLeft (juce::jmin (3, customRow.getWidth()));
    };

    placeCustom (customRootBox, 56);
    placeCustom (customTypeBox, 92);
    placeCustom (customBassBox, 108);
    customRow.removeFromLeft (juce::jmin (8, customRow.getWidth()));

    for (auto* button : tensionButtons)
        placeCustom (*button, 36);

    // 仕様書5.11.3：発音パラメータの段（Phase 44。**Phase 63でキーの段の下へ移した**。8.1のC10）。
    //
    // 下端に置いていたときは、**「どのトラックへ書くか」を確かめるのに
    // 画面の端から端まで目を動かす**ことになっていた。キー→書き込み先→パッド、と
    // 上から順に並ぶほうが、操作の流れどおり。
    //
    // 8.73：**狭いときは2段に折り返す**（Phase 113）。独立した窓（8.71）は
    // 狭くできるので、1段に詰めると**右のほうのつまみが幅0になって消えます**
    // （`jmin`で削られるため）。消えたことに気づけないので、入らないなら折り返す。
    auto writeArea = getLocalBounds().withTrimmedTop (controlRowHeight)
                                      .removeFromTop (getWriteAreaHeight());

    const bool twoRows = (getWriteAreaHeight() > writeRowHeight);

    auto firstRow = writeArea.removeFromTop (writeRowHeight).reduced (6, 3);

    // 8.99：**1段のときは`firstRow`そのものを指すこと**（Phase 137）。
    //
    // Phase 113からここは`auto secondRow = twoRows ? ... : firstRow;`でした。
    // `auto`は**値でコピー**するので、1段のときの`secondRow`は
    // **「まだ何も置いていない状態の`firstRow`」の写し**になります。
    // その結果、右のかたまり（Voicing以降）が**左端から置き直され、
    // 左のかたまり（Main／7th）の上に重なって隠していました**
    // ——後から`addAndMakeVisible`したものが上に来るので、**消えたように見えます**。
    //
    // **`removeFromLeft()`で場所を配っていく書き方は、参照でないと繋がりません。**
    juce::Rectangle<int> secondRowStorage;

    if (twoRows)
        secondRowStorage = writeArea.removeFromTop (writeRowHeight).reduced (6, 3);

    auto& secondRow = twoRows ? secondRowStorage : firstRow;

    // **Writeボタンを先に右端から取る。** 左から順に置くと、幅が足りないときに
    // いちばん押したいボタンが画面外へ出る。
    // 8.73：**「次に出るコード」はその左**（Phase 113）。押す前に何が出るか分かるように
    writeButton.setBounds (firstRow.removeFromRight (76));
    firstRow.removeFromRight (6);
    writeTargetLabel.setBounds (firstRow.removeFromRight (juce::jmin (86, firstRow.getWidth())));
    firstRow.removeFromRight (8);

    auto place = [] (juce::Rectangle<int>& row, juce::Component& component, int width)
    {
        component.setBounds (row.removeFromLeft (juce::jmin (width, row.getWidth())));
        row.removeFromLeft (juce::jmin (4, row.getWidth()));
    };

    // 左のかたまり：**どこへ書くか**と**どのページを見るか**（Phase 65）
    place (firstRow, targetTrackBox, 130);
    place (firstRow, pageButton, 66);
    place (firstRow, triadButton, 56);

    // 右のかたまり：**どう鳴らすか**。1段のときはあいだを空けて役割の切れ目を見せる
    if (! twoRows)
        firstRow.removeFromLeft (juce::jmin (18, firstRow.getWidth()));

    place (secondRow, voicingBox, 118);
    place (secondRow, octaveBox, 72);
    place (secondRow, lengthBox, 104);
    place (secondRow, gateSlider, 70);
    place (secondRow, velocitySlider, 62);

    // 8.73：ストロークはトグル3つ（Phase 113）。**間を詰めて1つのかたまりに見せる**
    secondRow.removeFromLeft (juce::jmin (8, secondRow.getWidth()));

    for (auto* button : { &strokeNoneButton, &strokeDownButton, &strokeUpButton })
    {
        button->setBounds (secondRow.removeFromLeft (juce::jmin (52, secondRow.getWidth())));
        secondRow.removeFromLeft (juce::jmin (1, secondRow.getWidth()));
    }

    secondRow.removeFromLeft (juce::jmin (4, secondRow.getWidth()));
    place (secondRow, deviationSlider, 76);

    layoutPads();
}

//==============================================================================
// 仕様書5.11.3：コード進行をMIDIノートにする（Phase 44）

void ChordPadPanel::refreshTargetTrackList()
{
    // **いま選んでいるトラックを、番号ではなくIDで覚え直す。**
    // 項目のIDにはトラック番号+1を使っているので（0は「未選択」なので使えない）、
    // トラックを消したり並べ替えたりすると、同じ番号が別のトラックを指す（1.32）。
    juce::String selectedTrackId;

    if (targetTrackBox.getSelectedId() > 0)
        selectedTrackId = project.getTrack (targetTrackBox.getSelectedId() - 1).getId();

    targetTrackBox.clear (juce::dontSendNotification);

    int idToSelect = 0;
    int firstMidiTrackId = 0;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        auto track = project.getTrack (t);

        if (track.getType() != TrackType::Midi)
            continue;

        targetTrackBox.addItem (track.getName(), t + 1);

        if (firstMidiTrackId == 0)
            firstMidiTrackId = t + 1;

        if (track.getId() == selectedTrackId)
            idToSelect = t + 1;
    }

    // 覚えていたトラックが消えていたら、先頭のMIDIトラックへ戻す
    targetTrackBox.setSelectedId (idToSelect > 0 ? idToSelect : firstMidiTrackId,
                                   juce::dontSendNotification);

    // 書き込み先が無いのにボタンだけ押せると、押しても何も起きない理由が分からない
    writeButton.setEnabled (firstMidiTrackId > 0);
}

ChordPerformance ChordPadPanel::getPerformance() const
{
    ChordPerformance performance;

    const int voicingId = voicingBox.getSelectedId();
    performance.guitar = (voicingId >= 5);
    performance.inversion = performance.guitar ? (voicingId - 5) : juce::jmax (0, voicingId - 1);

    performance.octaveOffset = octaveBox.getSelectedId() - 3;

    // lengthBoxのIDと対。0は「コード区間ぶんのロングトーン」
    switch (lengthBox.getSelectedId())
    {
        case 2:  performance.hitLengthBeats = 4.0;   break;   // 全音符
        case 3:  performance.hitLengthBeats = 2.0;   break;   // 2分
        case 4:  performance.hitLengthBeats = 1.0;   break;   // 4分
        case 5:  performance.hitLengthBeats = 0.5;   break;   // 8分
        case 6:  performance.hitLengthBeats = 0.25;  break;   // 16分
        case 7:  performance.hitLengthBeats = 0.125; break;   // 32分
        default: performance.hitLengthBeats = 0.0;   break;   // コード区間
    }

    performance.gate = gateSlider.getValue() / 100.0;
    performance.velocity = (int) velocitySlider.getValue();

    // 8.73：トグル3つの結果を持っているのは`stroke`1箇所（Phase 113）
    performance.stroke = stroke;

    performance.strokeDeviationBeats = deviationSlider.getValue();

    return performance;
}

void ChordPadPanel::setStroke (ChordStroke newStroke)
{
    stroke = newStroke;

    // **3つで1つの選択**なので、押されたものだけを点ける
    strokeNoneButton.setToggleState (stroke == ChordStroke::none, juce::dontSendNotification);
    strokeDownButton.setToggleState (stroke == ChordStroke::down, juce::dontSendNotification);
    strokeUpButton.setToggleState   (stroke == ChordStroke::up,   juce::dontSendNotification);

    // 次に開いたときも同じ向きで始める（設計書2.5）
    AppSettings::setInt (chordPadStrokeKey,
                          stroke == ChordStroke::down ? 2 : stroke == ChordStroke::up ? 3 : 1);
}

double ChordPadPanel::getSingleHitLengthSeconds (double atTime) const
{
    const double secondsPerBeat = project.getBeatSecondsAt (atTime);

    // lengthBoxのIDと対（`getPerformance()`の表と同じ並びにすること）
    switch (lengthBox.getSelectedId())
    {
        case 2:  return 4.0   * secondsPerBeat;   // 全音符
        case 3:  return 2.0   * secondsPerBeat;   // 2分
        case 4:  return 1.0   * secondsPerBeat;   // 4分
        case 5:  return 0.5   * secondsPerBeat;   // 8分
        case 6:  return 0.25  * secondsPerBeat;   // 16分
        case 7:  return 0.125 * secondsPerBeat;   // 32分
        default: return 0.0;                      // コード区間ぶん
    }
}

ChordRegion ChordPadPanel::getWriteTargetRegion() const
{
    return findChordRegionAtCursor();
}

void ChordPadPanel::updateWriteTargetText()
{
    // 8.73：**押す前に何が出るか分かるようにする**（Phase 113）
    auto region = getWriteTargetRegion();

    if (! region.state.isValid())
    {
        writeTargetLabel.setText ({}, juce::dontSendNotification);
        writeTargetLabel.setTooltip (utf8 ("再生位置にコードがありません"));
        return;
    }

    const double hitSeconds = getSingleHitLengthSeconds (region.getStartTime());
    const auto name = region.getChord().getName();

    writeTargetLabel.setText (name, juce::dontSendNotification);
    writeTargetLabel.setTooltip (hitSeconds > 0.0
                                    ? utf8 ("Writeで「") + name + utf8 ("」を")
                                       + lengthBox.getText() + utf8 ("1個ぶん書き出します")
                                    : utf8 ("Writeで「") + name + utf8 ("」をコード区間いっぱいに書き出します"));
}

ChordRegion ChordPadPanel::findChordRegionAtCursor() const
{
    auto chordTrack = getChordTrack();

    if (! chordTrack.state.getParent().isValid())
        return ChordRegion (juce::ValueTree());

    ChordRegion best { juce::ValueTree() };   // ※ 丸括弧だと関数宣言に取られる（most vexing parse）
    double bestStart = -1.0;

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
    {
        auto region = chordTrack.getChordRegion (r);

        // 中に入っていれば、それで決まり
        if (insertPositionSeconds >= region.getStartTime()
             && insertPositionSeconds < region.getEndTime())
            return region;

        // 8.72：**入っていないときは、手前でいちばん近い区間**（Phase 112）。
        // パッドを押すと挿入位置は次の小節へ進むので（8.9）、
        // 「含む区間」だけを見ると**いま入れたコードが対象になりません**
        if (region.getStartTime() <= insertPositionSeconds && region.getStartTime() > bestStart)
        {
            best = region;
            bestStart = region.getStartTime();
        }
    }

    return best;
}

void ChordPadPanel::writeChordToTrack (const Chord& chord, double startSeconds, double lengthSeconds,
                                        Track& targetTrack, bool tile)
{
    // 拍の長さは**書き始める場所**で測る（8.98／Phase 138）
    const double secondsPerBeat = project.getBeatSecondsAt (startSeconds);

    if (secondsPerBeat <= 0.0 || lengthSeconds <= 0.0)
        return;

    const double endSeconds = startSeconds + lengthSeconds;

    // 8.91：**書き足し先はトラックそのもの**（Phase 131）。
    // Phase 130までは「その頭を含んでいるクリップ」を探して、無ければ作っていました
    // ——クリップという入れ物が無くなったので、探す手間ごと消えています。
    const double contentStart = startSeconds;
    const double contentEnd   = endSeconds;

    // 8.72：**書く範囲のノートだけ差し替える**（Phase 112）。
    // 消さないと、押し直すたびに同じコードが重なって積み上がります。
    // **範囲の外は触りません**——手で直した隣のコードが消えないように
    for (int n = targetTrack.getNumNotes(); --n >= 0;)
    {
        auto note = targetTrack.getNote (n);

        if (note.getStartTime() >= contentStart - 1.0e-6 && note.getStartTime() < contentEnd - 1.0e-6)
            targetTrack.removeNote (note, &project.getUndoManager());
    }

    auto performance = getPerformance();

    // 8.73：**敷き詰めるかどうかは呼び出し側が決める**（Phase 113）。
    //
    // `generateChordNotes()`は「`hitLengthBeats`が0より大きければ、渡した長さを
    // その音価で敷き詰める」作りです。1個だけ書きたいときは
    // **渡す長さを音価と同じにする**のではなく、**敷き詰めを止めます**——
    // 長さを合わせるやり方だと、GT%やストロークのずれで端が丸まったときに
    // 敷き詰めの回数が1回になったり2回になったりして、読めない挙動になります。
    if (! tile)
        performance.hitLengthBeats = 0.0;

    const double lengthBeats = lengthSeconds / secondsPerBeat;

    // **ここが拍と秒の境目。** エンジンは拍で計算し、モデルは秒で持つ（8.5）
    for (const auto& note : generateChordNotes (chord, 0.0, lengthBeats, performance))
        targetTrack.addNote (note.pitch, note.velocity,
                              contentStart + note.startBeats * secondsPerBeat,
                              note.lengthBeats * secondsPerBeat,
                              &project.getUndoManager());
}

void ChordPadPanel::writeChordRegionToTrack (const ChordRegion& region, Track& targetTrack)
{
    // まとめ書き（右クリック）から呼ばれる。**区間まるごとを、音価で敷き詰める**
    writeChordToTrack (region.getChord(), region.getStartTime(), region.getLength(), targetTrack, true);
}

void ChordPadPanel::writeChordAtCursorToTrack()
{
    auto targetTrack = getTargetTrack();

    if (! targetTrack.state.getParent().isValid() || targetTrack.getType() != TrackType::Midi)
        return;

    auto region = findChordRegionAtCursor();

    if (! region.state.isValid())
        return;

    // 8.73：**音価を選んだら、その音価1個だけ**（Phase 113）。
    //
    // | 音価の選択 | 出るもの | 書き出す位置 |
    // |---|---|---|
    // | コード区間 | 区間まるごとのロングトーン | **区間の頭から** |
    // | 全音符〜32分 | その音価1個 | **カーソルから** |
    //
    // 「コード区間」だけ区間の頭から書くのは、**区間の途中にカーソルがあっても
    // 区間まるごとが欲しい**ため（そこから半端に始まるロングトーンには使い道がない）。
    // 音価を選んだときは刻みを手で組み立てる操作なので、カーソルの位置がそのまま意味を持つ。
    const double hitSeconds = getSingleHitLengthSeconds (insertPositionSeconds);

    const double startSeconds  = (hitSeconds > 0.0) ? insertPositionSeconds : region.getStartTime();
    const double lengthSeconds = (hitSeconds > 0.0) ? hitSeconds : region.getLength();

    if (lengthSeconds <= 0.0)
        return;

    project.beginAction (utf8 ("コードをMIDIノートへ書き込み"));
    writeChordToTrack (region.getChord(), startSeconds, lengthSeconds, targetTrack, false);

    // 8.73：**カーソルを書き出したものの直後へ進める**（Phase 113）。
    // Writeを押すだけで次のコードが続けて出せるようにするため
    // （パッドの連打で進行が積み上がるのと同じ考え方。8.5）
    insertPositionSeconds = startSeconds + lengthSeconds;

    rebuildGrid();       // 直前のコードが変われば、パッドの色がすべて変わる
    updateHeaderText();
    repaint();

    if (onInsertPositionChanged != nullptr)
        onInsertPositionChanged (insertPositionSeconds);
}

void ChordPadPanel::writeNotesToTrack()
{
    auto chordTrack = getChordTrack();

    if (! chordTrack.state.getParent().isValid() || chordTrack.getNumChordRegions() == 0)
        return;

    if (targetTrackBox.getSelectedId() <= 0)
        return;

    auto targetTrack = project.getTrack (targetTrackBox.getSelectedId() - 1);

    if (! targetTrack.state.getParent().isValid() || targetTrack.getType() != TrackType::Midi)
        return;

    // 進行の全体を1つのクリップに収める
    const double clipStart = chordTrack.getChordRegion (0).getStartTime();
    double clipEnd = clipStart;

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
        clipEnd = juce::jmax (clipEnd, chordTrack.getChordRegion (r).getEndTime());

    // 拍の長さは**進行の頭**で測る（8.98／Phase 138）
    const double secondsPerBeat = project.getBeatSecondsAt (clipStart);

    if (secondsPerBeat <= 0.0)
        return;

    if (clipEnd <= clipStart)
        return;

    const auto performance = getPerformance();

    project.beginAction (utf8 ("コード進行をまとめてMIDIノートへ書き込み"));

    // 8.91：**書き込む範囲のノートを先に消してから書く**（Phase 131）。
    //
    // Phase 130までは「同じ位置に前回作ったクリップがあれば消す」でした。
    // クリップという印が無くなったので、**時間の範囲で消します**——
    // 消さないと、パラメータを変えて押し直すたびに同じコードが積み上がります。
    for (int n = targetTrack.getNumNotes(); --n >= 0;)
    {
        auto note = targetTrack.getNote (n);

        if (note.getStartTime() >= clipStart - 1.0e-6 && note.getStartTime() < clipEnd - 1.0e-6)
            targetTrack.removeNote (note, &project.getUndoManager());
    }

    for (int r = 0; r < chordTrack.getNumChordRegions(); ++r)
    {
        auto region = chordTrack.getChordRegion (r);

        // **ここが拍と秒の境目。** エンジンは拍で計算し、モデルは秒で持つ（8.5）。
        // 8.91：**書き込むのは曲の時刻**（Phase 131で相対位置ではなくなった）
        const double startBeats = (region.getStartTime() - clipStart) / secondsPerBeat;
        const double lengthBeats = region.getLength() / secondsPerBeat;

        for (const auto& note : generateChordNotes (region.getChord(), startBeats, lengthBeats, performance))
            targetTrack.addNote (note.pitch, note.velocity,
                                  clipStart + note.startBeats * secondsPerBeat,
                                  note.lengthBeats * secondsPerBeat,
                                  &project.getUndoManager());
    }
}

//==============================================================================
juce::Colour ChordPadPanel::padColourForScore (float score)
{
    // 仕様書5.11.2：4段階。暖色（濃いオレンジ）ほど繋がりやすい
    if (score >= 0.80f) return AppColours::orange.withAlpha (0.85f);
    if (score >= 0.62f) return AppColours::orange.withAlpha (0.55f);
    if (score >= 0.45f) return AppColours::orange.withAlpha (0.30f);

    return AppColours::orange.withAlpha (0.10f);
}

void ChordPadPanel::updateHeaderText()
{
    auto track = getChordTrack();

    // 8.73：Writeで出るコードの表示も、同じきっかけで更新する（Phase 113）
    updateWriteTargetText();

    if (! track.state.getParent().isValid())
    {
        positionLabel.setText (utf8 ("コードトラックがありません"), juce::dontSendNotification);
        return;
    }

    // 8.75：**小節だけでなく拍まで出す**（Phase 115）。
    // 寄せなくなったので（`getChordInsertPosition()`）、カーソルは小節の途中にも居ます。
    // 「3小節」とだけ出すと、**小節の頭に居るのか途中なのかが読めません**
    //
    // **小節・拍への分解はProjectModelの1箇所**（8.98／Phase 138）。
    // ここで割り算を書くと、テンポが途中で変わる形にしたときに合わなくなります
    const auto position = project.getBarBeatAt (getChordInsertPosition());

    juce::String positionText = utf8 ("挿入位置：")
                                  + juce::String (position.bar + 1) + utf8 ("小節 ")
                                  + juce::String (position.beat + 1) + utf8 ("拍");

    positionLabel.setText (positionText, juce::dontSendNotification);

    // 8.74：**直前のコードは枠にした**（Phase 114）。文字は`positionLabel`から外している
    // （同じものを2箇所に出すと、片方だけ古くなる。1.27）
    const bool hadPrevious = previousChordRegion.state.isValid();

    getPreviousChord();   // `previousChordRegion`をいまの位置に合わせる

    // **有る／無いが変わったときだけ並べ直す。** ここは再生位置が動くたびに呼ばれるので、
    // 毎回`resized()`を通すと、止まっているあいだも上段を組み直し続けることになる
    // （コード名が変わっただけなら、呼び出し側の`repaint()`で足りる）
    if (previousChordRegion.state.isValid() != hadPrevious)
        resized();
}

void ChordPadPanel::drawGridBackground (juce::Graphics& g)
{
    // 設計書2.3.5：グリッドの地（Phase 65／8.26）。
    //
    // **パッドより先に描くこと。** 歯抜けのグリッド（理論的に候補が無い度数）では
    // パッドが無い場所が広く空くので、行と列の並びが分かる地が無いと
    // 「どの度数の欄か」を目で追えない。
    if (rowAreas.empty())
        return;

    // 1行おきに薄く敷く。**`canvasAlt`はcanvasより暗い**と決めてあるので、
    // ライトでもダークでも「1つおきに沈む」見え方になる（AppColours.h）
    g.setColour (AppColours::canvasAlt);

    for (size_t r = 1; r < rowAreas.size(); r += 2)
        g.fillRect (rowAreas[r]);

    const auto top = rowAreas.front().getY();
    const auto bottom = rowAreas.back().getBottom();
    const int padAreaLeft = rowAreas.front().getX() + rowLabelWidth;
    const int padAreaWidth = rowAreas.front().getWidth() - rowLabelWidth;

    if (padAreaWidth <= 0)
        return;

    // 列の区切り。**位置はパッドの計算と同じ式で出すこと**（`layoutPads()`）。
    // 別々に計算すると、端数の寄せ方が違って線がパッドの縁からずれる
    g.setColour (AppColours::border.withAlpha (0.45f));

    for (int col = 1; col < numColumns; ++col)
        g.drawVerticalLine (padAreaLeft + (col * padAreaWidth) / numColumns,
                             (float) top, (float) bottom);

    // 見出しの列とパッドの境目、列見出しの下線。ここだけ少し濃くする
    g.setColour (AppColours::border);
    g.drawVerticalLine (padAreaLeft, (float) (top - columnHeaderHeight), (float) bottom);
    g.drawHorizontalLine (top, (float) rowAreas.front().getX(),
                           (float) rowAreas.front().getRight());
}

void ChordPadPanel::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::canvas);

    // 操作列の帯
    g.setColour (AppColours::panel);
    g.fillRect (0, 0, getWidth(), controlRowHeight);
    g.setColour (AppColours::border);
    g.drawLine (0.0f, (float) controlRowHeight, (float) getWidth(), (float) controlRowHeight, 1.0f);

    auto track = getChordTrack();

    if (! track.state.getParent().isValid())
    {
        g.setColour (AppColours::textSecondary);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (utf8 ("「+ Track」→「コードトラック」でコードトラックを作ってください"),
                     getGridArea(), juce::Justification::centred);
        return;
    }

    // 8.106：**見出しもグリッドと同じキーで**（`rebuildGrid()`と揃えること）。
    // 別々に引くと、列見出しのローマ数字とパッドの中身が食い違います（8.2）
    const Scale key = project.getProjectKeyAt (getChordInsertPosition());
    const Chord* previous = getPreviousChord();

    drawGridBackground (g);

    // 列見出し：キーのルートからの半音。ダイアトニックの度数はローマ数字で示す
    {
        auto header = getGridArea().removeFromTop (columnHeaderHeight);
        header.removeFromLeft (rowLabelWidth);

        static const char* romanNumerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));

        for (int col = 0; col < numColumns; ++col)
        {
            const int x = header.getX() + (col * header.getWidth()) / numColumns;
            const int right = header.getX() + ((col + 1) * header.getWidth()) / numColumns;

            // その半音がキーの何度にあたるか（スケール外なら音名で出す）
            juce::String label = pitchClassName ((key.root + col) % 12);
            bool isDiatonic = false;

            for (int degree = 0; degree < 7; ++degree)
                if (key.degreeRoot (degree) == (key.root + col) % 12)
                {
                    label = romanNumerals[degree];
                    isDiatonic = true;
                    break;
                }

            g.setColour (isDiatonic ? AppColours::textPrimary : AppColours::textSecondary);
            g.drawText (label, juce::Rectangle<int> (x, header.getY(), right - x, header.getHeight()),
                         juce::Justification::centred);
        }
    }

    // 行の見出し（カテゴリ名）
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (AppColours::textSecondary);

    for (size_t r = 0; r < rowAreas.size() && r < grid.rows.size(); ++r)
        g.drawText (grid.rows[r].label,
                     rowAreas[r].withWidth (rowLabelWidth).reduced (6, 0),
                     juce::Justification::centredLeft, false);

    // パッド本体
    for (const auto& pad : pads)
    {
        const float score = connectionScore (previous, pad.chord, key);

        g.setColour (padColourForScore (score));
        g.fillRoundedRectangle (pad.area.toFloat(), AppColours::corner (3.0f));

        if (pad.area.getWidth() < 20)
            continue;   // 名前が入らない幅では描かない（切れた名前は別のコードに見える）

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (pad.chord.getName(), pad.area.reduced (3, 0),
                     juce::Justification::centred, false);
    }

    // 仕様書5.11.2：カスタムコードのプレビュー（Phase 45）。
    // **背景色にはパッドと同じスコアリングを使う**ので、組み立てたコードが
    // いまの流れに合うかどうかが、グリッドと同じ見え方で分かる。
    if (! customPreviewArea.isEmpty())
    {
        const auto customChord = buildCustomChord();

        g.setColour (padColourForScore (connectionScore (previous, customChord, key)));
        g.fillRoundedRectangle (customPreviewArea.toFloat(), AppColours::corner (3.0f));
        g.setColour (AppColours::border);
        g.drawRoundedRectangle (customPreviewArea.toFloat(), AppColours::corner (3.0f), 1.0f);

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (customChord.getName(), customPreviewArea.reduced (4, 0),
                     juce::Justification::centred, false);
    }

    // 8.74：**直前のコードの枠**（Phase 114）。右クリックで鳴らせる。
    //
    // **色はスコアで塗りません。** ここは候補ではなく「もう置いてあるコード」なので、
    // 濃さで良し悪しを示す意味がありません（パッドと同じ色にすると、
    // 選べるものだと思って押してしまう）
    if (! previousChordArea.isEmpty() && previousChordRegion.state.isValid())
    {
        g.setColour (AppColours::panel);
        g.fillRoundedRectangle (previousChordArea.toFloat(), AppColours::corner (3.0f));
        g.setColour (AppColours::border);
        g.drawRoundedRectangle (previousChordArea.toFloat(), AppColours::corner (3.0f), 1.0f);

        g.setColour (AppColours::textPrimary);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (previousChordRegion.getChord().getName(), previousChordArea.reduced (4, 0),
                     juce::Justification::centred, false);
    }
}

//==============================================================================
Chord ChordPadPanel::buildCustomChord() const
{
    Chord chord;
    chord.root = juce::jmax (0, customRootBox.getSelectedId() - 1);
    chord.type = (ChordType) juce::jlimit (0, (int) ChordType::NumTypes - 1,
                                            customTypeBox.getSelectedId() - 1);

    // 先頭の項目が「ベース指定なし」なので、2番目から0が始まる
    chord.bass = (customBassBox.getSelectedId() <= 1) ? -1 : customBassBox.getSelectedId() - 2;

    // 並びはコンストラクタのトグルの作り方と対。片方だけ変えないこと
    static const int tensionBits[7] =
    {
        Tension::Nine, Tension::FlatNine, Tension::SharpNine, Tension::Eleven,
        Tension::SharpEleven, Tension::Thirteen, Tension::FlatThirteen
    };

    chord.tensionMask = 0;

    for (int i = 0; i < juce::jmin (tensionButtons.size(), 7); ++i)
        if (tensionButtons[i]->getToggleState())
            chord.tensionMask |= tensionBits[i];

    return chord;
}

//==============================================================================
// 仕様書5.11.2：右クリックでの試聴（Phase 45）

Track ChordPadPanel::getTargetTrack() const
{
    if (targetTrackBox.getSelectedId() <= 0)
        return Track (juce::ValueTree (IDs::TRACK));

    return project.getTrack (targetTrackBox.getSelectedId() - 1);
}

void ChordPadPanel::startAudition (const Chord& chord)
{
    stopAudition();

    auto target = getTargetTrack();

    // 鳴らす先は書き込み先のトラック。音源が挿さっていなければ何も鳴らない
    if (! target.state.getParent().isValid() || target.getType() != TrackType::Midi)
        return;

    const auto performance = getPerformance();

    auditionPitches = performance.guitar
                        ? Voicing::guitarPitches (chord, performance.octaveOffset,
                                                   performance.inversion == 1)
                        : Voicing::piano (chord, performance.inversion, performance.octaveOffset);

    if (auditionPitches.empty())
        return;

    // **止めるときのために、鳴らした先のIDを覚えておく。** 鳴らしている間に
    // 書き込み先を変えられると、別のトラックへノートオフを送ることになる（1.31と同じ話）
    auditionTrackId = target.getId();

    for (int pitch : auditionPitches)
        audioEngine.previewNoteOn (auditionTrackId, pitch, performance.velocity);
}

void ChordPadPanel::stopAudition()
{
    if (auditionTrackId.isEmpty())
        return;

    for (int pitch : auditionPitches)
        audioEngine.previewNoteOff (auditionTrackId, pitch);

    auditionPitches.clear();
    auditionTrackId.clear();
}

//==============================================================================
void ChordPadPanel::mouseDown (const juce::MouseEvent& e)
{
    // 仕様書5.11.2：左クリックで入力、右クリックで試聴。
    // カスタムコードの枠も、パッドとまったく同じ扱いにしてある。
    Chord chord;
    bool found = false;

    // 8.74：**直前のコードの枠は右クリックだけ**（Phase 114）。
    // ここは「もう置いてあるコード」なので、左クリックで入力できると
    // **同じコードがもう1つ入る**ことになります
    if (! previousChordArea.isEmpty() && previousChordArea.contains (e.getPosition()))
    {
        if (e.mods.isPopupMenu() && previousChordRegion.state.isValid())
            startAudition (previousChordRegion.getChord());

        return;
    }

    if (customPreviewArea.contains (e.getPosition()))
    {
        chord = buildCustomChord();
        found = true;
    }
    else
    {
        for (const auto& pad : pads)
            if (pad.area.contains (e.getPosition()))
            {
                chord = pad.chord;
                found = true;
                break;
            }
    }

    if (! found)
        return;

    if (e.mods.isPopupMenu())
        startAudition (chord);
    else
        insertChord (chord);
}

void ChordPadPanel::mouseUp (const juce::MouseEvent&)
{
    stopAudition();
}

void ChordPadPanel::insertChord (const Chord& chord)
{
    auto track = getChordTrack();

    if (! track.state.getParent().isValid())
        return;

    const double startTime = getChordInsertPosition();

    // **長さは「入れる場所の小節」で測る**（8.98／Phase 138）
    const double barSeconds = getBarSeconds (startTime);

    if (barSeconds <= 0.0)
        return;

    // 8.141：**カーソルの位置に旗を立てます**（Phase 179／本人の指定）。
    //
    // Phase 178まで「カーソルが乗っている区間を差し替える」形でした。旗の考え方
    // （8.128）としては筋が通っていますが、**3拍目にカーソルを置いたのに変化が
    // 小節の頭に出る**ので、置いた場所と結果が食い違って見えます。
    //
    // **アレンジ画面の追加（`TimelineComponent::addChordRegionAt()`）と同じ決まり**に
    // 揃えました（1.27）——旗は好きなところに立てられ、**長さは
    // `normaliseChordRegions()`が「次の旗まで」に決めます**。
    //
    // 差し替えになるのは、**頭ちょうどに旗があるとき**だけです
    // （＝「この旗のコードを変えたい」と読める唯一の場合）。
    auto existing = track.findChordRegionAt (startTime);

    const bool onFlag = existing.state.isValid()
                         && std::abs (existing.getStartTime() - startTime) < 1.0e-6;

    // HANDOVER 8.5：パッドは連打する操作なので、1回ごとに区切らないと
    // 積み上げた進行が丸ごと1ステップのUndoになってしまう
    project.beginAction (utf8 ("コードの入力"));

    double length = barSeconds;

    if (onFlag)
    {
        existing.setChord (chord, &project.getUndoManager());   // その旗のコードを変える

        // 差し替えたときは、**その旗の終わりへ**進める（長さは旗ごとに違い得る）
        length = existing.getEndTime() - startTime;
    }
    else
    {
        auto added = track.addChordRegion (chord, startTime, barSeconds, &project.getUndoManager());

        // 8.128：**立てたら並べ直す**（Phase 164）。手前の旗が、新しい旗の手前まで縮みます。
        // **呼ばないと、旗と旗が重なったまま残ります**
        track.normaliseChordRegions (&project.getUndoManager());

        // 進む先は**並べ直した後の長さ**。次の旗があればそこまで、無ければ1小節ぶん
        if (added.state.isValid())
            length = added.getLength();
    }

    // 仕様書5.11.2：カーソルを自動で前進させる（連続クリックで進行が積み上がる）
    insertPositionSeconds = startTime + juce::jmax (0.0, length);

    rebuildGrid();
    updateHeaderText();
    repaint();

    if (onInsertPositionChanged != nullptr)
        onInsertPositionChanged (insertPositionSeconds);
}
