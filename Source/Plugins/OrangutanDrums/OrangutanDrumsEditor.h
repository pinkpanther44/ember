#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "OrangutanDrumsProcessor.h"
#include "OrangutanDrumsTheme.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <array>
#include <map>
#include <memory>

//==============================================================================
/**
    8.288：**画像の上に載るつまみ**（Phase 281）。

    `RaccoFieldLookAndFeel`（8.256）・`BassFieldLookAndFeel`（8.257）と同じ考えで、
    色だけ`OrangutanDrumsTheme`から引きます——**画像は常に同じ明るさ**なので、
    ここだけテーマに従いません。

    > **3つ目でも共通にしていません**（8.257の言い分のまま）。違うのは引く色だけですが、
    > 共通にすると「どの絵の上か」を呼び出し側から渡すことになります。**絵ごとに1つ**。
*/
class DrumsFieldLookAndFeel : public MantaKnobLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider& slider) override;

    /** つまみの見出しと数値を**太字・縁なしの一色で**描く（8.274と同じ扱い）。

        **Ember（7セグ）はそのまま**基底へ渡します。 */
    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};

//==============================================================================
/**
    画像の上のつまみ1つ（見出し／つまみ／数値）。

    8.289：**高さと文字の色を渡せるようにしました**（Phase 282）。
    マスターのつまみは**72pxの枠**に入るので、見出しと数値が固定だと円が潰れます。

    > 8.290：**文字の色は、いまは全部`fieldInk()`です**（Phase 283）。
    > Phase 282ではマスターだけ帯の上にあったので`MantaTheme::text()`を渡していました
    > ——**絵がマスターまで広がったので、渡す理由がなくなりました。**
    > 引数は残してあります（帯の上へ置きたくなったときのため）。
*/
class DrumsFieldKnob : public juce::Component
{
public:
    DrumsFieldKnob (const juce::String& name, juce::Colour arcColour,
                     int labelHeightToUse = 15, int valueHeightToUse = 17,
                     juce::Colour inkColour = OrangutanDrumsTheme::fieldInk());

    void resized() override;

    /** 見出しを差し替える（**エンジンで意味が変わるつまみ**のため。8.288）。 */
    void setLabelText (const juce::String& text);

    ValueEntrySlider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
    juce::Label label;

private:
    const int labelHeight, valueHeight;
};

//==============================================================================
/**
    パッド1つ。**8.289で正方形になりました**（Phase 282／本人の指定）。

    ```
    ┌────────┐
    │ 01   D │ ← パッド番号と、DIRECTの目印
    │  KICK  │ ← 割り当ててあるエンジン
    │   C2   │ ← ノート
    └────────┘
    ```

    | | |
    |---|---|
    | 押す | **選び、そのまま鳴らします**（叩いて選ぶ、が実機の作法） |
    | 押す場所 | **上ほど強く**（`1.0 - y * 0.62`、下限0.30。`MAGAZINE`と同じ） |
    | 光り方 | 鳴っているあいだ明るくなります（`getPadActivity()`） |

    **鳴らすのは鍵盤と同じ道**です（`triggerPadFromUI()`）。
*/
class DrumPadButton : public juce::Component
{
public:
    DrumPadButton (int padIndex);

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    void setSelected (bool shouldBeSelected);
    void setEngineName (const juce::String& name);

    /** 8.289：**このパッドがDIRECTか**（Phase 282）。 */
    void setDirectOut (bool isDirect);

    /** 0〜1。**変わったときだけ描き直します**（20Hzで全部描き直すと重い）。 */
    void setGlow (float newGlow);

    std::function<void (int, float)> onHit;     ///< (パッド番号, ベロシティ)
    std::function<void (int)> onSelect;

private:
    const int index;
    juce::String engineName { "----" };
    juce::String noteName;
    bool selected = false;
    bool highlighted = false;
    bool directOut = false;
    float glow = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPadButton)
};

//==============================================================================
/**
    **選んでいるパッドのエンジンと、その減衰のかたち**を出す小さな箱。

    `DECAY`を回すと曲線が伸び、エンジンを変えると**つまみを触らなくても変わります**
    ——エンジンごとに時間の範囲が違う（`engineDecayRange()`）ことが、ここで見えます。

    8.289：**つまみの頁の下**へ移りました（Phase 282）。窓が狭くなって、
    上の帯に置く場所が無くなったためです。**横に広い箱**になったので、
    曲線はかえって読みやすくなっています。
*/
class DrumEngineBadge : public juce::Component
{
public:
    void setState (int padIndex, int engine, float decayNormalised);

    void paint (juce::Graphics& g) override;

private:
    int pad = 0;
    int currentEngine = 0;
    float decay = 0.5f;
};

//==============================================================================
/**
    8.290：**鍵盤**（Phase 283／本人の指定）。

    Phase 282は36〜51でした。**51（D#3）は黒鍵**なので、右端が
    **白鍵の途中で切れて**見えます（黒鍵は白鍵の上にかぶさっているだけなので、
    最後の白鍵D3のぶんの幅が余ります）。

    **E3（52）まで出して、そこを灰色に塗ります**——
    「ここから先は鳴らない」と**見ればわかる**形にするためです。
    鳴らないのは`OrangutanDrumsProcessor`が36〜51しか拾わないからで、
    **ここは見た目だけ**です（押しても無音）。
*/
class OrangutanDrumsKeyboard : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    /** 鍵盤に出す右端。**鳴る音域より1つ上**（上の説明）。 */
    static constexpr int highestShownNote = OrangutanDrumsProcessor::padHighNote + 1;   // 52

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver,
                         juce::Colour lineColour, juce::Colour textColour) override;
};

//==============================================================================
/**
    8.288：**Orangutan Drums の画面**（Phase 281）。
    8.289：**縦長の1枚に作り直しました**（Phase 282／本人の要望）。

    ```
    ┌── 400 ─────────────┐
    │ Undo Redo │A Copy│P│  ツールバー（**題名は入りません**。下）
    ├────────────────────┤
    │ ORANGUTAN  4 VOICES│  上の帯（ORANGUTAN DRUMS ／ 声の数）
    │ DRUMS              │
    │ OUTPUT ─────  0.80 │
    │ [ 案内（2〜3行） ] │
    ├────────────────────┤
    │ PAD 01   C2 (36)   │  選んでいるパッド
    │ [KICK ▾] [OUT:MAIN]│
    ├────────────────────┤
    │┌──────────────────┐│
    ││[ PADS ][ PAD 01 ]││  **頁の切り替え**（2枚）
    ││                  ││
    ││ 4×4のパッド       ││  ← 頁1
    ││ または つまみ7つ  ││  ← 頁2
    ││──────────────────││
    ││ MASTER           ││  **ここは固定**（切り替えません）
    ││ ◯ ◯ ◯ ◯ ◯      ││
    │└──────────────────┘│  ← **ここまで全部が1枚の絵の上**（8.290）
    ├────────────────────┤
    │ 鍵盤（36〜52。52は灰色）│
    └────────────────────┘
    ```

    ### なぜ2枚なのか（3枚ではなく）

    本人の指定は「左／中／右を切り替える3枚、**もしくはマスターが収まるなら2枚**」。
    **マスターは5つとも1行に収まりました**（72pxの枠×5＝360。幅は368）ので、
    **固定して2枚**にしてあります。

    > **マスターは「いま鳴っている音全体」の設定**です。パッドを叩きながら
    > 触るものなので、**切り替えの向こう側に置きたくありません。**

    ### ツールバーに題名が入りません

    `MantaPluginToolbar`のボタンだけで**312px**あり、残りは38pxです
    （`Racco Guitar`では1168pxあったので、右端に`Java Rhino Bass`と書けていました）。
    **上の帯のワードマークが同じ役目をします**ので、題名のラベルは置いていません。

    ### つまみは**繋ぎ替えます**（Phase 281から変わらず）

    パッドは16個あり、それぞれに9つ（全部で144）。画面に出ているつまみは7つだけで、
    **選んだパッドのものへ繋ぎ直します**。

    `juce::SliderAttachment`は**繋ぎ先を変えられません**（作るときに決まります）。
    `setSelectedPad()`が**作り直します**——`OwnedArray::clear()`で先に壊してから、
    新しいものを作ること。**壊す前に作ると、同じつまみに2つ繋がります。**

    > 8.290：**数値の出し方は`ValueEntrySlider::DisplayUnit::zeroToTen`へ移しました**
    > （Phase 283）。Phase 282までは繋ぎ直すたびに`textFromValueFunction`を
    > 入れ直していました——`SliderAttachment`が自前のものへ差し替えるためです
    > （8.256で1度踏んでいます）。**`getTextFromValue()`の側なら差し替えられません。**

    ### 選んでいるパッドと開いている頁は`<UI>`に入ります

    パラメータではありません（オートメーションする意味がないため）。
    `apvts.state`の`<UI>`へ入れてあるので、**プロジェクトに保存され、開き直すと戻ります**。
*/
class OrangutanDrumsEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit OrangutanDrumsEditor (OrangutanDrumsProcessor& processorToUse);
    ~OrangutanDrumsEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    //==========================================================================
    void addPadKnob (std::unique_ptr<DrumsFieldKnob>& knob, const juce::String& name,
                      juce::Colour colour, const juce::String& hint);

    void addMasterKnob (std::unique_ptr<DrumsFieldKnob>& knob, const juce::String& name,
                         const juce::String& parameterId, juce::Colour colour,
                         const juce::String& hint);

    void setSelectedPad (int pad);
    void setPage (int page);
    void rebuildPadAttachments();
    void updatePadLabels();
    void updateOutButton();

    void registerHint (juce::Component& component, const juce::String& text);
    void showHint (const juce::String& text);

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawField (juce::Graphics& g) const;
    void drawWordmark (juce::Graphics& g) const;

    static void drawTracked (juce::Graphics& g, const juce::String& text,
                              juce::Point<float> origin, const juce::Font& font, float tracking);

    /** そのエンジンでの`SNAP`／`TONE`の意味（**画面の見出しに出します**）。 */
    static juce::String snapLabelFor (int engine);
    static juce::String toneLabelFor (int engine);

    //==========================================================================
    OrangutanDrumsProcessor& processor;

    juce::SharedResourcePointer<DrumsFieldLookAndFeel> fieldLookAndFeel;

    MantaPluginToolbar toolbar;

    juce::Image fieldImage;

    juce::Label hintLabel;
    std::map<juce::Component*, juce::String> hints;
    juce::String defaultHint;

    DrumEngineBadge badge;

    ValueEntrySlider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label outputValue;

    juce::ComboBox engineBox;
    juce::TextButton outButton;

    /** 頁のタブ（**2枚**。クラスの説明）。 */
    juce::TextButton padsTabButton, knobsTabButton;

    std::array<std::unique_ptr<DrumPadButton>, (size_t) OrangutanDrumsProcessor::numPads> pads;

    // 選んだパッドのつまみ（**繋ぎ替えます**）
    std::unique_ptr<DrumsFieldKnob> tune, decay, tone, snap, level, pan, send;

    // マスター（**繋ぎ替えません／頁で隠れません**）
    std::unique_ptr<DrumsFieldKnob> drive, glue, reverb, size, damp;

    OrangutanDrumsKeyboard keyboard;

    //==========================================================================
    int selectedPad = 0;

    /** 0＝パッド／1＝選んだパッドのつまみ。 */
    int currentPage = 0;

    std::array<int, (size_t) OrangutanDrumsProcessor::numPads> lastEngines;
    std::array<bool, (size_t) OrangutanDrumsProcessor::numPads> lastDirectOuts;
    int lastVoiceCount = -1;
    bool lastDirectAvailable = false;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** **部品より後ろに置くこと**（1.5）。 */
    juce::OwnedArray<SliderAttachment> sliderAttachments;

    /** 選んだパッドのぶん。**`setSelectedPad()`が作り直します。** */
    juce::OwnedArray<SliderAttachment> padAttachments;
    std::unique_ptr<ComboBoxAttachment> engineAttachment;

    std::vector<juce::Slider*> styledSliders;
    std::vector<juce::Component*> styledComponents;

    //==========================================================================
    // 寸法。**幅は`Racco Guitar`の1/3**（本人の指定）。縦は据え置き

    static constexpr int fixedWidth  = 400;
    static constexpr int fixedHeight = 790;

    static constexpr int margin   = 16;
    static constexpr int contentX = margin;
    static constexpr int contentW = fixedWidth - margin * 2;   // 368

    static constexpr int toolbarY = 12;

    static constexpr int headerY = 54;
    static constexpr int headerH = 116;

    static constexpr int controlY = 178;
    static constexpr int controlH = 58;

    /** 8.290：**絵はタブからマスターの下まで**（Phase 283／本人の要望）。

        Phase 282は頁のところ（278〜618）にだけ敷いていました。
        **タブとマスターだけ帯の地**だったので、縦に3つに割れて見えます。
        いまは`imageY`から`imageY + imageH`までが1枚で、
        **タブも、頁も、マスターのつまみも、その上に載っています。** */
    static constexpr int imageY = 240;
    static constexpr int imageH = 472;   // 240 〜 712

    static constexpr int tabsY = 246;
    static constexpr int tabsH = 26;

    /** 頁の中身が入るところ（絵の一部）。 */
    static constexpr int fieldY = 278;
    static constexpr int fieldH = 340;

    /** 頁とマスターの区切り（**枠にはしない**——絵が切れて見えます）。 */
    static constexpr int masterDividerY = 620;

    static constexpr int masterY = 622;
    static constexpr int masterH = 90;

    static constexpr int keyboardY = 720;
    static constexpr int keyboardH = 54;

    // 上の帯の中
    static constexpr int wordmarkX = 32;
    static constexpr int outputLabelX  = 32;
    static constexpr int outputSliderX = 92;
    static constexpr int outputSliderW = 186;
    static constexpr int outputValueX  = 288;
    static constexpr int hintInset = 14;

    // 選んでいるパッドの帯
    static constexpr int engineBoxX = contentX + 12;
    static constexpr int engineBoxW = 186;
    static constexpr int engineBoxH = 26;
    static constexpr int outButtonX = contentX + 210;
    static constexpr int outButtonW = 134;

    // 頁のタブ
    static constexpr int tabInset = 8;
    static constexpr int tabGap = 6;
    static constexpr int tabW = (contentW - tabInset * 2 - tabGap) / 2;   // 173

    // 頁1：パッド（**正方形**。本人の指定）
    static constexpr int padCell = 76;
    static constexpr int padGap  = 8;
    static constexpr int padGridW = padCell * 4 + padGap * 3;   // 328
    static constexpr int padGridX = contentX + (contentW - padGridW) / 2;
    static constexpr int padGridY = fieldY + 6;

    // 頁2：つまみ7つ（4つ＋3つ）
    static constexpr int knobCellW = 88;
    static constexpr int knobCellH = 112;
    static constexpr int knobRow1Y = fieldY + 82;
    static constexpr int knobRow2Y = fieldY + 216;
    static constexpr int knobRow1X = contentX + 8 + knobCellW / 2;            // 68
    static constexpr int knobRow2X = contentX + 8 + knobCellW;                // 112
    static constexpr int badgeInset = 20;
    static constexpr int badgeY = fieldY + 288;
    static constexpr int badgeH = 40;

    // マスター（**固定**）
    static constexpr int masterCell = 72;
    static constexpr int masterKnobY = masterY + 18;
    static constexpr int masterX = contentX + (contentW - masterCell * 5) / 2 + masterCell / 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrangutanDrumsEditor)
};
