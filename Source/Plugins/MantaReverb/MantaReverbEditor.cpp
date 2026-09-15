#include "MantaReverbEditor.h"

#include "../../Branding.h"
#include "../../Utf8.h"
#include "MantaReverbAlgorithm.h"
#include "MantaReverbRouting.h"
#include "MantaReverbTheme.h"

//==============================================================================

MantaReverbEditor::MantaReverbEditor (MantaReverbProcessor& processorToUse)
    : juce::AudioProcessorEditor (&processorToUse),
      processor (processorToUse),
      toolbar (processorToUse, processorToUse.getValueTreeState(), Branding::reverbPresetFolder)
{
    addAndMakeVisible (toolbar);
    addAndMakeVisible (display);

    //--------------------------------------------------------------------------
    // 8.243：アルゴリズムの選択（Phase 255）

    setupSectionLabel (algorithmCaption, "Algorithm");

    algorithmBox.addItemList (MantaReverbAlgorithm::getKindNames(), 1);
    algorithmBox.setTooltip (utf8 ("響きの種類。切り替えると、いま鳴っている響きは止まります"));
    addAndMakeVisible (algorithmBox);

    // 8.249：**繋ぐのは`rebuildEngineAttachments()`**（Phase 259）——
    // アルゴリズムもエンジンごとのものになりました

    algorithmDescription.setColour (juce::Label::textColourId, MantaTheme::textDim());
    algorithmDescription.setFont (juce::Font (juce::FontOptions (10.0f)));
    addAndMakeVisible (algorithmDescription);

    // **`ComboBoxAttachment`は`onChange`より先に値を入れます**（8.213で
    // `filterPositionButton`が同じ形で空のまま出ました）。**1回は自分で呼ぶこと**
    algorithmBox.onChange = [this] { refreshAlgorithmControls(); };

    //--------------------------------------------------------------------------
    // 8.249：デュアルエンジンとルーティング（Phase 259／仕様書3-1）

    for (auto* button : { &engineAButton, &engineBButton })
    {
        MantaPluginToolbar::styleButton (*button, button == &engineAButton ? "A" : "B");
        button->setClickingTogglesState (true);
        button->setRadioGroupId (1);
        button->setColour (juce::TextButton::buttonOnColourId, MantaReverbTheme::accent());
        addAndMakeVisible (*button);
    }

    engineAButton.setTooltip (utf8 ("つまみを Engine A に向けます"));
    engineBButton.setTooltip (utf8 ("つまみを Engine B に向けます（Single では鳴りません）"));

    engineAButton.onClick = [this] { setSelectedEngine (0); };
    engineBButton.onClick = [this] { setSelectedEngine (1); };

    setupSectionLabel (routingCaption, "Routing");

    routingBox.addItemList (MantaReverbRouting::getModeNames(), 1);
    // 8.249：**AとBが同じだと Stereo Split は大きいだけ**（測ってあります）。
    // 8.225でCrossに書いたのと同じ形——コードが正しくても「何も起きない」が起きます
    routingBox.setTooltip (utf8 ("AとBのつなぎ方。切り替えると、いま鳴っている響きは止まります。"
                                  "Stereo SplitはAとBを違う設定にして使うものです"
                                  "（同じだと3dB大きいだけになります）"));
    addAndMakeVisible (routingBox);

    routingAttachment = std::make_unique<ComboAttachment> (
        processor.getValueTreeState(), MantaReverbParams::routingMode, routingBox);

    routingDescription.setColour (juce::Label::textColourId, MantaTheme::textDim());
    routingDescription.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (routingDescription);

    routingBox.onChange = [this] { refreshRoutingControls(); };

    setupSectionLabel (roomTitle, "Room");
    setupSectionLabel (dampingTitle, "Tail");
    setupSectionLabel (outputTitle, "Output");

    //--------------------------------------------------------------------------
    // **パープルは「空間そのものを決めるところ」**（ヘッダの表）

    setupKnob (predelaySlider, predelayCaption, "Predelay", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &predelaySlider, MantaReverbParams::predelay });
    setupKnob (decaySlider, decayCaption, "Decay", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &decaySlider, MantaReverbParams::decay });
    setupKnob (sizeSlider, sizeCaption, "Size", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &sizeSlider, MantaReverbParams::size });
    setupKnob (diffusionSlider, diffusionCaption, "Diffusion", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &diffusionSlider, MantaReverbParams::diffusion });

    // 8.246：Phase 3（Phase 256）。**Sizeと同じ色**——3つとも空間の寸法です
    setupKnob (shapeSlider, shapeCaption, "Shape", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &shapeSlider, MantaReverbParams::shape });
    setupKnob (spreadSlider, spreadCaption, "Spread", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &spreadSlider, MantaReverbParams::spread });

    // 8.247：Phase 4a（Phase 257）。**同じ升目に重ねます**（ヘッダの表）
    setupKnob (twinTimeSlider, twinTimeCaption, "Time", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &twinTimeSlider, MantaReverbParams::twinTime });
    setupKnob (twinFeedbackSlider, twinFeedbackCaption, "Feedback", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &twinFeedbackSlider, MantaReverbParams::twinFeedback });
    setupKnob (twinCrossSlider, twinCrossCaption, "Cross", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &twinCrossSlider, MantaReverbParams::twinCross });

    //--------------------------------------------------------------------------
    // 8.248：Phase 4b（Phase 258）。**入るか入らないかしかないものはボタン**

    {
        const struct { juce::TextButton* button; const char* text; const char* id; } toggles[]
        {
            { &monoSumButton, "Mono Sum", MantaReverbParams::panMonoSum },
            { &invertButton,  "Invert R", MantaReverbParams::panInvertRight },
            { &swapButton,    "Swap L/R", MantaReverbParams::panSwap }
        };

        for (const auto& entry : toggles)
        {
            MantaPluginToolbar::styleButton (*entry.button, entry.text);
            entry.button->setClickingTogglesState (true);
            entry.button->setColour (juce::TextButton::buttonOnColourId, MantaReverbTheme::highlight());
            addAndMakeVisible (*entry.button);

            // 8.249：**繋ぐのは`rebuildEngineAttachments()`**（Phase 259）
            engineToggles.push_back ({ entry.button, entry.id });
        }

        // 9.7：**説明は表の外で、`utf8 (…)`の中に日本語を直接書くこと。**
        //
        // **見本を引用符付きで書かないこと。** `Update-Translations.ps1`は
        // 呼び出しの書き方をそのまま探すので、**コメントの中の見本まで拾います**
        // ——ここに書いてあった見本が、ずっと「訳の無い文字列」として1件数えられていました
        // 上の表へ入れて`const char*`で持ち回すと、**訳の表の道具から見えません**
        monoSumButton.setTooltip (utf8 ("左右を足して真ん中へまとめます（Widthを0%にするのと同じ）"));
        invertButton.setTooltip (utf8 ("右チャンネルの位相を反転します。"
                                        "左右の打ち消しを確かめるのに使います"));
        swapButton.setTooltip (utf8 ("左右を入れ替えます"));

        // **`Mono Sum`が入ったら`Width`を畳みます**（`refreshAlgorithmControls()`）。
        // 押したときにも掛け直すこと——アルゴリズムを変えたときだけでは足りません
        monoSumButton.onStateChange = [this] { refreshAlgorithmControls(); };
    }

    // 1段目の空きに出す案内（ヘッダの説明）
    panoramaHint.setColour (juce::Label::textColourId, MantaTheme::textDim().withAlpha (0.75f));
    panoramaHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    panoramaHint.setJustificationType (juce::Justification::centred);
    panoramaHint.setText (utf8 ("幅は Output の Width、掛かり具合は Mix です"),
                           juce::dontSendNotification);
    addAndMakeVisible (panoramaHint);

    // **オレンジは「掛かり具合」**
    setupKnob (earlySlider, earlyCaption, "Early", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &earlySlider, MantaReverbParams::earlyLevel });
    setupKnob (widthSlider, widthCaption, "Width", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &widthSlider, MantaReverbParams::width });

    setupKnob (highFreqSlider, highFreqCaption, "HF Freq", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &highFreqSlider, MantaReverbParams::highDampFreq });
    setupKnob (highAmountSlider, highAmountCaption, "HF Damp", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &highAmountSlider, MantaReverbParams::highDampAmount });
    setupKnob (lowFreqSlider, lowFreqCaption, "LF Freq", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &lowFreqSlider, MantaReverbParams::lowDampFreq });
    setupKnob (lowAmountSlider, lowAmountCaption, "LF Damp", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &lowAmountSlider, MantaReverbParams::lowDampAmount });

    // 8.249：そのエンジンの出口（Phase 259）
    setupKnob (levelSlider, levelCaption, "Level", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &levelSlider, MantaReverbParams::engineLevel });

    // 8.250〜8.251：Phase 6（Phase 260）。**揺れはテールの形なのでパープル**、
    // **歪みは掛かり具合なのでオレンジ**
    setupKnob (modulationSlider, modulationCaption, "Modulation", MantaReverbTheme::accent());
    engineKnobs.push_back ({ &modulationSlider, MantaReverbParams::modulation });
    setupKnob (saturationSlider, saturationCaption, "Saturation", MantaReverbTheme::highlight());
    engineKnobs.push_back ({ &saturationSlider, MantaReverbParams::saturation });

    // **この2つだけエンジン共通**（作ったところで繋いで、そのまま外しません）
    setupKnob (mixSlider, mixCaption, "Mix", MantaReverbTheme::highlight());
    setupKnob (outputSlider, outputCaption, "Output", MantaReverbTheme::highlight());

    globalAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  MantaReverbParams::mix, mixSlider));
    globalAttachments.add (new SliderAttachment (processor.getValueTreeState(),
                                                  MantaReverbParams::outputGain, outputSlider));

    //--------------------------------------------------------------------------
    // 説明。**つまみの名前だけでは何が変わるか分からないもの**に付けます

    predelaySlider.setTooltip (utf8 ("原音が鳴ってから響きが始まるまで。空けるほど原音が前に出ます"));
    // 8.242：**Dampingを上げると、実際はこれより早く消えます**（Phase 254）。
    //
    // 8.225で引いた線：**説明するしかないものは、画面に出す。**
    // 測ってあります——Dampingを切れば設定どおり（12秒で12.75秒）、
    // 既定のDampingでは12秒の設定が6.7秒でした。**どちらもそういうもの**です
    decaySlider.setTooltip (utf8 ("響きが60dB落ちるまでの時間。"
                                   "Dampingを上げると、削られた帯域はこれより早く消えます"));
    sizeSlider.setTooltip (utf8 ("部屋の大きさ。初期反射とテールの両方が伸び縮みします。"
                                   "減衰時間（Decay）は変わりません"));
    // `Diffusion`の説明は`refreshAlgorithmControls()`が入れます（アルゴリズムで変わるため）
    earlySlider.setTooltip (utf8 ("初期反射の量。上げると部屋の形が近く聞こえます"));

    // 8.246：**どちらも初期反射だけに効きます**（Phase 256）。
    // そう書いておかないと、Sizeとの違いが分かりません
    shapeSlider.setTooltip (utf8 ("初期反射の山の位置。下げると近くの壁がはっきり返り、"
                                   "上げると遠くからゆっくり湧いてきます（50%で素）"));
    // `Spread`の説明は`refreshAlgorithmControls()`が入れます（意味が変わるため）

    twinTimeSlider.setTooltip (utf8 ("左のディレイの間隔。右はSpreadで決まる比率だけ短くなります"));
    twinFeedbackSlider.setTooltip (utf8 ("返ってきた音をどれだけ戻すか。上げるほど長く反復します"));

    // 8.225と同じ注意書き：**AとBが同じなら入れ替えても何も起きません**
    twinCrossSlider.setTooltip (utf8 ("左右の戻りを入れ替えます。上げると反復が左右に跳ねます"
                                       "（Spreadが0だと2本が同じ長さになり、違いが出ません）"));
    widthSlider.setTooltip (utf8 ("響きの左右の広がり。100%がそのままで、0%で真ん中に寄ります"));

    highAmountSlider.setTooltip (utf8 ("高い音から先に消えるようにします（実際の部屋と同じ）"));
    lowAmountSlider.setTooltip (utf8 ("低い側を削って、響きが濁らないようにします"));

    mixSlider.setTooltip (utf8 ("原音と響きの混ぜ具合"));
    outputSlider.setTooltip (utf8 ("出口の音量"));
    levelSlider.setTooltip (utf8 ("このエンジンの音量。AとBの釣り合いを取ります"));

    // 8.250〜8.251：Phase 6（Phase 260）
    modulationSlider.setTooltip (utf8 ("テールをゆっくり揺らします（Random Hall専用）。"
                                        "止まって聞こえる響きに動きが出ます"));
    saturationSlider.setTooltip (utf8 ("響きが大きいところだけを潰して、ざらつきを足します。"
                                        "0%で何もしません"));

    // 8.253：**いちばん下の行は消しました**（Phase 261／本人の指定）。
    //
    // Phase 1から「これから何が入るか」を薄く出していた行です。
    // **設計書のスコープが全部入ったので、書くことがありません**
    // （ディレイで8.222に書いたのと同じ——**空になった行は残さず片付けること**。
    // 残すと「まだ何か来る」と読めます）。空いた26pxはディスプレイへ回しました。

    //--------------------------------------------------------------------------
    // 8.249：**覚えてあるエンジンへ向けて開きます**（Phase 259）。
    //
    // `setSelectedEngine()`が繋ぎを作るので、**これより前につまみを触らないこと**

    setSelectedEngine ((int) processor.getUiState()
                           .getProperty (MantaReverbUiState::selectedEngine, 0));

    // **開いたときに1回**（上の`onChange`の説明）。
    // プリセットやUndoで値が戻ったときも、ここを通します
    toolbar.onStateRestored = [this]
    {
        refreshRoutingControls();
        refreshAlgorithmControls();
    };

    refreshRoutingControls();
    refreshAlgorithmControls();

    setSize (fixedWidth, fixedHeight);

    // 8.172：**伸縮させません**（`setResizable()`を呼ばないこと）

    startTimerHz (20);
}

MantaReverbEditor::~MantaReverbEditor()
{
    stopTimer();

    // 8.168：**`LookAndFeel`より先に、つまみから外すこと**
    for (auto* slider : { &predelaySlider, &decaySlider, &sizeSlider, &diffusionSlider,
                           &shapeSlider, &spreadSlider, &earlySlider, &widthSlider,
                           &highFreqSlider, &highAmountSlider, &lowFreqSlider, &lowAmountSlider,
                           &mixSlider, &outputSlider, &levelSlider,
                           &modulationSlider, &saturationSlider,
                           &twinTimeSlider, &twinFeedbackSlider, &twinCrossSlider })
        slider->setLookAndFeel (nullptr);
}

//==============================================================================

void MantaReverbEditor::setupKnob (ValueEntrySlider& slider, juce::Label& caption,
                                    const juce::String& text, juce::Colour colour)
{
    slider.setLookAndFeel (&knobLookAndFeel.get());
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 15);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    slider.setColour (juce::Slider::textBoxTextColourId, MantaTheme::text());
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, MantaTheme::textDim());
    caption.setFont (juce::Font (juce::FontOptions (10.0f)));
    caption.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (caption);
}

void MantaReverbEditor::rebuildEngineAttachments()
{
    // **いったん全部外してから作ること**（ヘッダの説明）。
    // 同じつまみに2本ぶら下がると、片方が前のエンジンへ書き戻します
    engineSliderAttachments.clear();
    engineButtonAttachments.clear();
    algorithmAttachment.reset();

    auto& state = processor.getValueTreeState();

    const auto id = [this] (const char* suffix)
    {
        return MantaReverbParams::engineParamId (selectedEngine, suffix);
    };

    for (const auto& entry : engineKnobs)
        engineSliderAttachments.add (new SliderAttachment (state, id (entry.id), *entry.slider));

    for (const auto& entry : engineToggles)
        engineButtonAttachments.add (new ButtonAttachment (state, id (entry.id), *entry.button));

    algorithmAttachment = std::make_unique<ComboAttachment> (
        state, id (MantaReverbParams::algorithm), algorithmBox);
}

void MantaReverbEditor::setSelectedEngine (int engine)
{
    selectedEngine = juce::jlimit (0, MantaReverbParams::numEngines - 1, engine);

    (selectedEngine == 0 ? engineAButton : engineBButton).setToggleState (true, juce::dontSendNotification);

    // **覚えておくのはここ**（`getUiState()`。音には関係しません）
    processor.getUiState().setProperty (MantaReverbUiState::selectedEngine, selectedEngine, nullptr);

    rebuildEngineAttachments();
    refreshAlgorithmControls();
}

void MantaReverbEditor::refreshRoutingControls()
{
    const auto mode = processor.getRoutingMode();
    const bool usesB = MantaReverbRouting::usesEngineB (mode);

    routingDescription.setText (MantaReverbRouting::getModeDescription (mode),
                                 juce::dontSendNotification);

    // 8.249：**`Single`ではBが鳴りません**（Phase 259）。
    // 鳴らないエンジンへつまみを向けられると、**回しても何も起きない**画面になります
    engineBButton.setEnabled (usesB);
    engineBButton.setAlpha (usesB ? 1.0f : 0.45f);

    if (! usesB && selectedEngine != 0)
        setSelectedEngine (0);

    // `Level`は**2つ以上動いているときだけ**意味を持ちます——
    // `Single`では`Output`と区別が付きません
    setControlActive (levelSlider, levelCaption, usesB);
}

void MantaReverbEditor::setupSectionLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, MantaTheme::textDim());
    label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (label);
}

void MantaReverbEditor::setControlActive (juce::Component& control, juce::Label& caption, bool active)
{
    // **隠さずに薄くすること**（8.208）。消してしまうと、
    // そのアルゴリズムに「そのつまみが無い」のか「そもそも無い機能」なのか読めません
    control.setEnabled (active);
    control.setAlpha (active ? 1.0f : 0.4f);

    caption.setColour (juce::Label::textColourId,
                        active ? MantaTheme::textDim() : MantaTheme::textDim().withAlpha (0.4f));
    caption.repaint();
}

void MantaReverbEditor::refreshAlgorithmControls()
{
    const auto kind = (MantaReverbAlgorithm::Kind)
                          juce::jlimit (0, MantaReverbAlgorithm::getKindCount() - 1,
                                         algorithmBox.getSelectedItemIndex());

    algorithmDescription.setText (MantaReverbAlgorithm::getDescription (kind),
                                   juce::dontSendNotification);

    const auto capabilities = MantaReverbAlgorithm::getCapabilities (kind);

    setControlActive (earlySlider, earlyCaption, capabilities.early);
    setControlActive (diffusionSlider, diffusionCaption, capabilities.diffusion);
    setControlActive (shapeSlider, shapeCaption, capabilities.shape);
    setControlActive (spreadSlider, spreadCaption, capabilities.spread);

    setControlActive (modulationSlider, modulationCaption, capabilities.modulation);

    setControlActive (highFreqSlider, highFreqCaption, capabilities.damping);
    setControlActive (highAmountSlider, highAmountCaption, capabilities.damping);
    setControlActive (lowFreqSlider, lowFreqCaption, capabilities.damping);
    setControlActive (lowAmountSlider, lowAmountCaption, capabilities.damping);

    //--------------------------------------------------------------------------
    // 8.247：**升目の中身を入れ替えます**（Phase 257／ヘッダの表）。
    //
    // グレーアウトではなく入れ替えなのは、**同じつまみが効かないのではなく、
    // 別のつまみだから**です（8.208で「隠さずに薄く」と書いたのは前者の話）。
    //
    // 出ていないほうは触れないので、**隠れているつまみを掴んでしまう**ことはありません
    // （ディレイの`timeSlider`／`divisionSlider`と同じ。8.207）

    const bool twin = capabilities.twinDelay;
    const bool pano = capabilities.panorama;

    juce::Component* const reverbCells[]
    {
        &decaySlider, &decayCaption, &sizeSlider, &sizeCaption, &shapeSlider, &shapeCaption
    };

    juce::Component* const twinCells[]
    {
        &twinTimeSlider, &twinTimeCaption, &twinFeedbackSlider, &twinFeedbackCaption,
        &twinCrossSlider, &twinCrossCaption
    };

    // 8.248：`Panorama`は2段目をボタンで置き換えます（Phase 258）
    juce::Component* const panoramaCells[]
    {
        &monoSumButton, &invertButton, &swapButton, &panoramaHint
    };

    juce::Component* const secondRowCells[]
    {
        &spreadSlider, &spreadCaption, &diffusionSlider, &diffusionCaption
    };

    for (auto* component : reverbCells)
        component->setVisible (! twin && ! pano);

    for (auto* component : twinCells)
        component->setVisible (twin);

    for (auto* component : panoramaCells)
        component->setVisible (pano);

    // `Spread`と`Diffusion`は**Panoramaでは行き先がありません**（ボタンが座ります）
    for (auto* component : secondRowCells)
        component->setVisible (! pano);

    // 8.248：**`Mono Sum`が入っているあいだは`Width`を畳みます**（Phase 258）。
    // 2つのものが同じ1つを取り合っている状態を、画面に出さないこと
    setControlActive (widthSlider, widthCaption,
                       ! (pano && monoSumButton.getToggleState()));

    // **意味が変わるものは、説明を差し替えること**（`Diffusion`と同じ扱い）
    spreadSlider.setTooltip (twin
                                 ? utf8 ("2本のディレイの間隔。0%だと同じ長さになり、"
                                          "Crossを回しても違いが出ません")
                                 : utf8 ("初期反射が散らばる幅。テールは動きません"
                                          "（そちらはSize。50%で素）"));

    // 8.244：**Plateでは`Diffusion`の意味が変わります**（Phase 255）。
    // 効かなくなるわけではないので、グレーアウトではなく**説明を差し替えます**
    diffusionSlider.setTooltip (kind == MantaReverbAlgorithm::Kind::plate
                                    ? utf8 ("板の拡散。70%がDattorroの原典そのもので、"
                                             "それ以上は頭打ちになります")
                                    : utf8 ("響きの詰まり具合。下げると一発ずつが聞こえ、"
                                             "上げるとなめらかに溶けます"));

    // **`Room`の箱の見出しも変えること。** `Plate`を選んでいるのに
    // 「Room」と書いてあると、そこが効いていないように見えます
    roomTitle.setText (MantaReverbAlgorithm::getKindNames()[(int) kind],
                        juce::dontSendNotification);
}

void MantaReverbEditor::timerCallback()
{
    // **描く値は`getDisplaySettings()`から。** `processBlock()`が使うのと
    // 同じ`buildEngineSettings()`を通るので、絵と音がずれません（1.27）。
    //
    // 8.249：**映しているエンジンのぶん**（Phase 259）——
    // つまみがBを向いているのに絵がAのままだと、回しても動きません
    display.setSettings (processor.getDisplaySettings (selectedEngine));
}

//==============================================================================

juce::Array<juce::Rectangle<int>> MantaReverbEditor::getControlColumns (juce::Rectangle<int> band) const
{
    juce::Array<juce::Rectangle<int>> columns;

    auto row = band;

    columns.add (row.removeFromLeft (roomColumnWidth));
    row.removeFromLeft (columnGap);

    columns.add (row.removeFromLeft (dampingColumnWidth));
    row.removeFromLeft (columnGap);

    // **残り全部**（`Output`）。幅を直に書かないこと——
    // 3つ足して画面の幅とずれたときに、いちばん右だけがはみ出します
    columns.add (row);

    return columns;
}

void MantaReverbEditor::paint (juce::Graphics& g)
{
    g.fillAll (MantaTheme::windowBackground());

    auto area = getLocalBounds();
    area.removeFromTop (MantaPluginToolbar::preferredHeight);

    auto content = area.reduced (12, 8);

    content.removeFromTop (headerHeight);
    content.removeFromTop (4);
    content.removeFromTop (displayHeight);
    content.removeFromTop (10);

    // **箱にしておくこと。** つまみを12個並べただけだと、
    // どこからどこまでが1つの機能なのかが読めません（ディレイのPhase 3と同じ。8.210）
    const auto drawPanel = [&g] (juce::Rectangle<int> box)
    {
        g.setColour (MantaTheme::panelBackground());
        g.fillRoundedRectangle (box.toFloat(), 4.0f);

        g.setColour (MantaTheme::border());
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
    };

    for (auto& column : getControlColumns (content.removeFromTop (controlAreaHeight)))
        drawPanel (column);
}

void MantaReverbEditor::resized()
{
    auto area = getLocalBounds();

    toolbar.setBounds (area.removeFromTop (MantaPluginToolbar::preferredHeight));

    auto content = area.reduced (12, 8);

    //--------------------------------------------------------------------------

    // 8.249：`Algorithm [▾]  [A][B]   Routing [▾]  説明`（Phase 259）
    {
        auto header = content.removeFromTop (headerHeight);

        algorithmCaption.setBounds (header.removeFromLeft (70)
                                       .withSizeKeepingCentre (70, sectionTitleHeight));
        header.removeFromLeft (6);
        algorithmBox.setBounds (header.removeFromLeft (120).reduced (0, 2));

        header.removeFromLeft (12);
        engineAButton.setBounds (header.removeFromLeft (30).reduced (0, 2));
        header.removeFromLeft (4);
        engineBButton.setBounds (header.removeFromLeft (30).reduced (0, 2));

        header.removeFromLeft (16);
        routingCaption.setBounds (header.removeFromLeft (58)
                                     .withSizeKeepingCentre (58, sectionTitleHeight));
        header.removeFromLeft (6);
        routingBox.setBounds (header.removeFromLeft (128).reduced (0, 2));
        header.removeFromLeft (10);
        routingDescription.setBounds (header);
    }

    content.removeFromTop (4);

    display.setBounds (content.removeFromTop (displayHeight));

    content.removeFromTop (10);

    //--------------------------------------------------------------------------
    // 下の3つの箱。**`paint()`と同じ`getControlColumns()`を通すこと**（1.27）

    const auto placeKnobRow = [this] (juce::Rectangle<int> row,
                                       std::initializer_list<std::pair<ValueEntrySlider*, juce::Label*>> items)
    {
        const int count = (int) items.size();

        if (count <= 0)
            return;

        // 8.173：**余りは`count + 1`で割る**（Phase 214）。
        // `count - 1`で割ると端まで詰まり、**つまみが箱に貼り付いて見えます**
        const int spare = juce::jmax (0, row.getWidth() - knobWidth * count);
        const int gap = spare / (count + 1);

        row.removeFromLeft (gap);

        for (auto& item : items)
        {
            auto cell = row.removeFromLeft (knobWidth);

            item.second->setBounds (cell.removeFromTop (12));
            item.first->setBounds (cell);

            row.removeFromLeft (gap);
        }
    };

    auto columns = getControlColumns (content.removeFromTop (controlAreaHeight));

    {
        auto box = columns[0].reduced (12, 6);

        // 8.249：**見出しの行に、そのアルゴリズムの説明も出します**（Phase 259）。
        // ヘッダの行はルーティングに使ったので、**説明はつまみの隣**へ移しました
        {
            auto titleRow = box.removeFromTop (sectionTitleHeight);

            roomTitle.setBounds (titleRow.removeFromLeft (84));
            algorithmDescription.setBounds (titleRow);
        }

        placeKnobRow (box.removeFromTop (knobHeight), { { &predelaySlider, &predelayCaption },
                                                         { &decaySlider, &decayCaption },
                                                         { &sizeSlider, &sizeCaption } });
        box.removeFromTop (8);
        placeKnobRow (box.removeFromTop (knobHeight), { { &shapeSlider, &shapeCaption },
                                                         { &spreadSlider, &spreadCaption },
                                                         { &diffusionSlider, &diffusionCaption } });

        // 8.247：**Twin Delaysのつまみは、同じ升目へ重ねます**（Phase 257）。
        // **別の場所に置くと、切り替えるたびに画面が組み替わって見えます**（8.207）
        twinTimeSlider.setBounds (decaySlider.getBounds());
        twinTimeCaption.setBounds (decayCaption.getBounds());
        twinFeedbackSlider.setBounds (sizeSlider.getBounds());
        twinFeedbackCaption.setBounds (sizeCaption.getBounds());
        twinCrossSlider.setBounds (shapeSlider.getBounds());
        twinCrossCaption.setBounds (shapeCaption.getBounds());

        //----------------------------------------------------------------------
        // 8.248：`Panorama`（Phase 258）。
        //
        // 1段目の空き（`Decay`と`Size`の升目）に案内、2段目にボタン3つ。
        // **ボタンは3つまとめて真ん中へ**（`withSizeKeepingCentre`を入れ子にしない。8.172）

        panoramaHint.setBounds (decaySlider.getBounds()
                                  .getUnion (sizeSlider.getBounds())
                                  .withSizeKeepingCentre (
                                      decaySlider.getBounds().getUnion (sizeSlider.getBounds()).getWidth(),
                                      20));

        {
            auto row = shapeSlider.getBounds().getUnion (diffusionSlider.getBounds());

            constexpr int buttonWidth = 86;
            constexpr int buttonGap = 8;

            auto strip = row.withSizeKeepingCentre (buttonWidth * 3 + buttonGap * 2, 24);

            monoSumButton.setBounds (strip.removeFromLeft (buttonWidth));
            strip.removeFromLeft (buttonGap);
            invertButton.setBounds (strip.removeFromLeft (buttonWidth));
            strip.removeFromLeft (buttonGap);
            swapButton.setBounds (strip.removeFromLeft (buttonWidth));
        }
    }

    {
        auto box = columns[1].reduced (12, 6);

        dampingTitle.setBounds (box.removeFromTop (sectionTitleHeight));

        // 8.250：Phase 6で`Modulation`が入りました（Phase 260／ヘッダの表）。
        // **2段目は2つのまま**——`placeKnobRow()`が真ん中へ寄せます
        placeKnobRow (box.removeFromTop (knobHeight), { { &highFreqSlider, &highFreqCaption },
                                                         { &highAmountSlider, &highAmountCaption },
                                                         { &modulationSlider, &modulationCaption } });
        box.removeFromTop (8);
        placeKnobRow (box.removeFromTop (knobHeight), { { &lowFreqSlider, &lowFreqCaption },
                                                         { &lowAmountSlider, &lowAmountCaption } });
    }

    {
        auto box = columns[2].reduced (12, 6);

        outputTitle.setBounds (box.removeFromTop (sectionTitleHeight));

        // 8.249：Phase 5で`Level`が増えて1段3つになりました（Phase 259）。
        // **2段目は2つのまま**——`placeKnobRow()`が真ん中へ寄せるので、
        // 穴が空いたようには見えません（Phase 6のSaturationはここへ入ります）
        placeKnobRow (box.removeFromTop (knobHeight), { { &earlySlider, &earlyCaption },
                                                         { &widthSlider, &widthCaption },
                                                         { &levelSlider, &levelCaption } });
        box.removeFromTop (8);
        placeKnobRow (box.removeFromTop (knobHeight), { { &mixSlider, &mixCaption },
                                                         { &outputSlider, &outputCaption },
                                                         { &saturationSlider, &saturationCaption } });
    }

    //--------------------------------------------------------------------------

}
