#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "JavaRhinoBassProcessor.h"
#include "JavaRhinoBassTheme.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <array>
#include <map>
#include <memory>

//==============================================================================
/**
    8.257：**画像の上に載るつまみ**（Phase 265）。

    `RaccoFieldLookAndFeel`（8.256）と同じ考えで、色だけ`JavaRhinoBassTheme`から引きます
    ——**画像は常に同じ明るさ**なので、ここだけテーマに従いません。

    > **2つ目を作るときに、共通にしなかった理由**：
    > 違うのは引く色だけですが、**共通にすると「どちらの絵の上か」を
    > 呼び出し側から渡すことになります**。渡す値は結局この6色で、
    > 受け取る側で分岐が増えるだけでした。**絵ごとに1つ**にしてあります。
*/
class BassFieldLookAndFeel : public MantaKnobLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider& slider) override;

    /** つまみの見出しと数値を**太字で**描く。

        8.274：**縁は描きません**（Phase 273／本人の指定）。Phase 272までは
        **暗い縁＋白い字**でしたが、あれは濃い絵の上に文字を浮かせるための手当てで、
        絵を淡いものへ替えたので畳みました。**色は`fieldInk()`の一色**
        （`Racco Guitar`と同じ扱い）。

        **太字だけ残しているのは、つまみの下の小さい数値のため**です。

        **Ember（7セグ）はそのまま**——基底へ渡します。棒は元から太いので困りません。 */
    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};

//==============================================================================
/** 画像の上のつまみ1つ（見出し／つまみ／数値）。`RaccoFieldKnob`と同じ作りです。 */
class BassFieldKnob : public juce::Component
{
public:
    BassFieldKnob (const juce::String& name, juce::Colour arcColour);

    void resized() override;

    ValueEntrySlider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
    juce::Label label;

    static constexpr int labelHeight = 15;
    static constexpr int valueHeight = 17;
};

//==============================================================================
/**
    奏法のチップ（押せる）。

    ```
    ┌──────────┐
    │ C5       │ ← キースイッチのノート
    │ FINGER   │ ← 奏法の名前
    └──────────┘
    ```

    `Racco Guitar`との違いは、**押すとパラメータが動く**ことです
    （あちらは奏法がパラメータではありませんでした）。
    オートメーションやプリセットからも動くので、**見た目はタイマーで追いつかせます**。
*/
class BassStyleChip : public juce::Button
{
public:
    BassStyleChip (const juce::String& noteNameToUse, const juce::String& styleName,
                    juce::Colour onColourToUse);

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isDown) override;

private:
    juce::String noteName;
    juce::Colour onColour;
};

//==============================================================================
/**
    画面右上の小さな箱。**いまの奏法と、その減衰のかたち**を出します。

    Sustainを回すと曲線が伸び、MuteやGhostに切り替えると**つまみを触らなくても縮みます**
    ——奏法がT60を固定している（`getStyleTone()`の`t60Fixed`）ことが、ここで見えます。
*/
class BassStyleBadge : public juce::Component
{
public:
    void setState (BassStyle style, float sustainSeconds);

    void paint (juce::Graphics& g) override;

private:
    BassStyle current = BassStyle::Finger;
    float sustain = 9.0f;
};

//==============================================================================
/**
    鍵盤。**キースイッチと音域を色で示します**。

    | | 見え方 |
    |---|---|
    | キースイッチ（C5〜F5） | 濃いグリーン。**いま選ばれている1つだけ濃いブルー** |
    | 演奏できる音域（B0〜G4） | ふつうの白鍵・黒鍵 |
    | G#4〜B4 | **薄く**——5弦ベースに無い音域で、押しても鳴りません |

    `Racco Guitar`は音域の**下**が鳴りませんでしたが、こちらは**上**です
    （5弦ベースは低音側を使い切るので、キースイッチを上へ置いてあります）。
*/
class JavaRhinoBassKeyboard : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    /** いま選ばれている奏法（選択肢の番号）。 */
    void setStyleChoice (int choice);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver,
                         juce::Colour lineColour, juce::Colour textColour) override;

    void drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    static bool isOutOfRange (int midiNoteNumber);

    int currentChoice = 0;
};

//==============================================================================
/**
    8.257：**Java Rhino Bass の画面**（Phase 265）。

    **`Racco Guitar`とまったく同じ並びです**（本人の指定）。寸法も1200×790で同じで、
    違うのは**絵と色、そしてつまみの数**（5→7）だけです。

    ```
    ┌────────────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets               Java Rhino Bass │
    ├────────────────────────────────────────────────────────────┤
    │ JAVA RHINO   [ 案内の帯 ]        [STYLE] OUTPUT ──── 0.80  │
    ├────────────────────────────────────────────────────────────┤
    │ A PHYSICAL MODELING BASS      [RESET][LEGATO] BLEND ────── │
    │ ┌─ STRING MODEL ─────────┐ ┌─ ARTICULATION ──────────────┐ │
    │ │ 863.6mm / BEADG / ‥   │ │ [C5][C#5][D5][D#5][E5][F5]  │ │
    │ └────────────────────────┘ └─────────────────────────────┘ │
    ├────────────────────────────────────────────────────────────┤
    │        絵の上に、つまみ7つを散らして置いてある               │
    ├────────────────────────────────────────────────────────────┤
    │ 鍵盤（B0〜F5。キースイッチは色つき）                        │
    └────────────────────────────────────────────────────────────┘
    ```

    ### ギターと違うところ

    | | Racco Guitar | Java Rhino Bass |
    |---|---|---|
    | 絵の上のつまみ | 5つ | **7つ**（Clank・Toneが増える） |
    | 帯の右 | PICKUP（3択） | **BLEND**（連続。Neck〜Both〜Bridge） |
    | 帯の右（もう1つ） | — | **LEGATO**（切れる） |
    | 奏法 | パラメータではない | **パラメータ**（オートメーションできる） |

    ### つまみが7つでも、格子にしない

    `knobPlacement[]`は**絵の中の割合**で持っています。上段4つ・下段3つですが、
    **高さを少しずつ変えて**あるので、並びが格子に見えません（8.256と同じ）。
*/
class JavaRhinoBassEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit JavaRhinoBassEditor (JavaRhinoBassProcessor& processorToUse);
    ~JavaRhinoBassEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    /** 奏法の短い名前。**チップも、右上の箱も、ここを引きます**（1.27）。 */
    static juce::String styleName (BassStyle style);

private:
    void timerCallback() override;

    //==========================================================================
    void addKnob (std::unique_ptr<BassFieldKnob>& knob, const juce::String& name,
                   const juce::String& parameterId, juce::Colour colour,
                   const juce::String& hint);

    void registerHint (juce::Component& component, const juce::String& text);
    void showHint (const juce::String& text);

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawBox (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const;
    void drawField (juce::Graphics& g) const;
    void drawWordmark (juce::Graphics& g) const;

    static void drawTracked (juce::Graphics& g, const juce::String& text,
                              juce::Point<float> origin, const juce::Font& font, float tracking);

    /** Blendの値を文字にする（`0.5`は**NECK+BRIDGE**＝両方フル）。 */
    static juce::String blendText (float blend);

    void setStyleChoice (int choice);

    //==========================================================================
    JavaRhinoBassProcessor& processor;

    juce::SharedResourcePointer<BassFieldLookAndFeel> fieldLookAndFeel;

    MantaPluginToolbar toolbar;
    juce::Label titleLabel;

    juce::Image fieldImage;

    juce::Label hintLabel;
    std::map<juce::Component*, juce::String> hints;
    juce::String defaultHint;

    BassStyleBadge badge;

    ValueEntrySlider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label outputValue;

    ValueEntrySlider blendSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label blendValue;

    juce::TextButton resetButton, legatoButton;

    std::array<std::unique_ptr<BassStyleChip>, numBassStyles> chips;

    std::unique_ptr<BassFieldKnob> brightness, sustain, pluckPos, tone,
                                    hardness, attack, clank;

    JavaRhinoBassKeyboard keyboard;

    int lastVoiceCount = -1;
    int lastStyleChoice = -1;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** **部品より後ろに置くこと**（1.5）。 */
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;

    std::vector<juce::Slider*> styledSliders;

    /** つまみの箱ごと`LookAndFeel`を被せたもの。

        **見出しのラベルにも縁を付けるため**です（`BassFieldLookAndFeel::drawLabel`）。
        ラベルは親からLookAndFeelを継ぐので、箱に被せれば届きます
        （`ValueEntrySlider`だけは自分で持っているので、そちらは別に被せます）。 */
    std::vector<juce::Component*> styledComponents;

    //==========================================================================
    // 寸法。**`Racco Guitar`と同じ値**（1.27の意味では別のプラグインなので写しています）

    static constexpr int fixedWidth  = 1200;
    static constexpr int fixedHeight = 790;

    static constexpr int margin   = 16;
    static constexpr int contentX = margin;
    static constexpr int contentW = fixedWidth - margin * 2;   // 1168

    static constexpr int toolbarY = 12;

    static constexpr int headerY = 54;
    static constexpr int headerH = 68;

    static constexpr int controlY = 132;
    static constexpr int controlH = 126;

    static constexpr int fieldY = 268;
    static constexpr int fieldH = 396;

    static constexpr int keyboardY = 676;
    static constexpr int keyboardH = 98;

    // 上の帯
    static constexpr int wordmarkX = 36;
    static constexpr int hintX     = 292;
    static constexpr int hintW     = 400;
    static constexpr int badgeX    = 706;
    static constexpr int badgeW    = 118;
    static constexpr int outputLabelX = 840;
    static constexpr int outputSliderX = 898;
    static constexpr int outputSliderW = 130;
    static constexpr int outputValueX = 1034;
    static constexpr int voicesX = 1086;

    // 2つめの帯
    static constexpr int rowY = 144;
    static constexpr int rowH = 28;

    static constexpr int resetX  = 700;
    static constexpr int resetW  = 64;
    static constexpr int legatoX = 772;
    static constexpr int legatoW = 76;

    static constexpr int blendLabelX  = 866;
    static constexpr int blendSliderX = 918;
    static constexpr int blendSliderW = 154;
    static constexpr int blendValueX  = 1080;
    static constexpr int blendValueW  = 104;

    static constexpr int boxY      = 180;
    static constexpr int boxH      = 62;
    static constexpr int leftBoxX  = 32;
    static constexpr int leftBoxW  = 540;
    static constexpr int rightBoxX = 588;
    static constexpr int rightBoxW = 596;

    static constexpr int chipInset  = 10;
    static constexpr int chipGap    = 5;
    static constexpr int chipHeight = 30;

    static constexpr int knobCellW = 96;
    static constexpr int knobCellH = 96;

    /** 絵の中の置き場所（**割合**。格子にしないこと）。 */
    struct KnobPlacement { float x, y; };

    static constexpr KnobPlacement knobPlacement[7]
    {
        { 0.15f, 0.32f },   // BRIGHT
        { 0.37f, 0.28f },   // SUSTAIN
        { 0.60f, 0.33f },   // PLUCK POS
        { 0.83f, 0.29f },   // TONE
        { 0.26f, 0.68f },   // HARDNESS
        { 0.50f, 0.72f },   // ATTACK
        { 0.74f, 0.67f },   // CLANK
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JavaRhinoBassEditor)
};
