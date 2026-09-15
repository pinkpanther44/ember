#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "RaccoGuitarProcessor.h"
#include "RaccoGuitarTheme.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <array>
#include <map>
#include <memory>

//==============================================================================
/**
    8.256：**画像の上に載るつまみ**（Phase 264）。

    `MantaKnobLookAndFeel`から**弧と指針の描き方だけ**を差し替えたものです
    （数値の描き方＝Emberの7セグは、基底のまま使います）。

    ### なぜ共有のものをそのまま使えないか

    共有のつまみは、地・枠・目盛りを`MantaTheme`から引きます＝**テーマに従います**。
    こちらのつまみが乗るのは**常に明るい画像**なので、ダークテーマのときに
    地が暗くなると、**明るい面に黒い円が並ぶ**ことになります。

    色は`RaccoGuitarTheme::field*()`から引いた**固定の値**です（`RaccoGuitarTheme.h`）。
*/
class RaccoFieldLookAndFeel : public MantaKnobLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider& slider) override;
};

//==============================================================================
/**
    画像の上のつまみ1つ（見出し／つまみ／数値）。

    ```
     BRIGHT      ← 見出し（上）
      ╭──╮
      │ ╱│       ← つまみ
      ╰──╯
      0.55       ← 数値（下。**出しっぱなし**）
    ```

    `MantaSynthEditor`のつまみは数値を出していません（1画面に40個以上あるため）。
    **こちらは5つしか無い**ので、VINEの画面と同じく**常に数値を添えて**います。

    数値は読み取り専用の欄ですが、**右クリックで打ち込めます**
    （`ValueEntrySlider`。本体のつまみと同じ操作）。
*/
class RaccoFieldKnob : public juce::Component
{
public:
    RaccoFieldKnob (const juce::String& name, juce::Colour arcColour);

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
    │ C2       │ ← キースイッチのノート（小さく）
    │ NORMAL   │ ← 奏法の名前
    └──────────┘
    ```

    **キースイッチと同じことをします**——押すと`currentArticulation`が変わるだけで、
    音は鳴りません。逆に鍵盤のキースイッチを踏むと、こちらの見た目が追いつきます
    （`RaccoGuitarEditor`のタイマー）。
*/
class RaccoArticulationChip : public juce::Button
{
public:
    RaccoArticulationChip (const juce::String& noteNameToUse,
                            const juce::String& articulationName,
                            juce::Colour onColourToUse);

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isDown) override;

private:
    juce::String noteName;
    juce::Colour onColour;
};

//==============================================================================
/**
    画面右上の小さな箱（VINEの`ENV`にあたるもの）。

    **いまの奏法と、その減衰のかたち**を出します。Sustainを回すと曲線が伸び、
    パームミュートやブラッシングに切り替えると**つまみを触らなくても縮みます**
    ——奏法が音色に上限を掛けている（`RaccoGuitarVoice::applyEffectiveTone()`）ことが、
    ここで見えます。
*/
class RaccoArticulationBadge : public juce::Component
{
public:
    void setState (GuitarArticulation articulation, float sustainSeconds);

    void paint (juce::Graphics& g) override;

private:
    GuitarArticulation current = GuitarArticulation::normal;
    float sustain = 10.0f;
};

//==============================================================================
/**
    鍵盤。**キースイッチと音域を色で示します**。

    | | 見え方 |
    |---|---|
    | キースイッチ（A#1〜D#2） | 水色。**いま選ばれている1つだけピンク** |
    | 演奏できる音域（E2〜D6） | ふつうの白鍵・黒鍵 |
    | それ以外 | **薄く** ——実機で出ない音なので、押しても鳴りません |

    元（`KSGuitar`）は音域外を普通の鍵盤として描いていました。
    **押しても鳴らないものが、鳴るものと同じ見た目なのは間違いのもと**なので、
    薄くしてあります。
*/
class RaccoGuitarKeyboard : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    /** いま選ばれている奏法（そのキースイッチだけ色を変えます）。 */
    void setArticulation (GuitarArticulation articulation);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver,
                         juce::Colour lineColour, juce::Colour textColour) override;

    void drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    bool isOutOfRange (int midiNoteNumber) const;

    GuitarArticulation current = GuitarArticulation::normal;
};

//==============================================================================
/**
    8.256：**Racco Guitar の画面**（Phase 264／本人が指定したレイアウト）。

    ```
    ┌────────────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets                  Racco Guitar │ ← 共有ツールバー
    ├────────────────────────────────────────────────────────────┤
    │ RACCO GUITAR   [ 案内の帯 ]        [ARTIC] OUTPUT ──── 0.80 │ ← 上の帯
    ├────────────────────────────────────────────────────────────┤
    │ A PHYSICAL MODELING GUITAR              [RESET] [PICKUP ▾] │
    │ ┌─ STRING MODEL ─────────┐ ┌─ ARTICULATION ──────────────┐ │
    │ │ 648mm / 6 STRINGS / ‥ │ │ [A#1][B1][C2][C#2][D2][D#2] │ │
    │ └────────────────────────┘ └─────────────────────────────┘ │
    ├────────────────────────────────────────────────────────────┤
    │                                                            │
    │            SUSTAIN            HARDNESS         ← 画像の上に │
    │      BRIGHT        PICK POS          ATTACK      つまみ5つ  │
    │                                                            │
    ├────────────────────────────────────────────────────────────┤
    │ 鍵盤（A1〜D6。キースイッチは色つき）                        │
    └────────────────────────────────────────────────────────────┘
    ```

    ### つまみを枠で囲っていない

    ほかの5つ（EQ・Comp・シンセ・ディレイ・リバーブ）は、セクションの箱に
    つまみを詰めています。**この音源はつまみが5つしかありません**——
    箱で仕切ると、仕切りのほうが中身より多くなります。
    本人の指定どおり、**大きな1枚の絵の上へ散らして**置きました。

    位置は等間隔ではありません（**格子に見えると絵が死にます**）。
    `knobPlacement[]`が、絵の中の割合で持っています。

    ### 案内の帯（上の帯のまん中）

    **つまみにカーソルを合わせると、そこに説明が出ます。**
    ツールチップと同じ中身ですが、**出るまで待たなくてよい**のと、
    **出る場所が毎回同じ**なのが違います（探さなくて済む）。

    仕掛けは`addMouseListener (this, true)`1つです——子の全部の出入りがここへ来るので、
    **つまみを足すたびに配線を書き足す必要がありません**（`hints`へ1行足すだけ）。

    ### 大きさは固定（8.172）

    `setResizable()`を呼んでいません。**絵の上につまみを置いているので、
    伸ばすと絵と位置の関係が崩れます**（割合で置いてあるので崩れはしませんが、
    つまみだけ正円のまま取り残されます）。
*/
class RaccoGuitarEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit RaccoGuitarEditor (RaccoGuitarProcessor& processorToUse);
    ~RaccoGuitarEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

    /** 奏法の短い名前。**チップも、右上の箱も、ここを引きます**（1.27）。 */
    static juce::String articulationName (GuitarArticulation articulation);

private:
    void timerCallback() override;

    //==========================================================================
    void addKnob (std::unique_ptr<RaccoFieldKnob>& knob, const juce::String& name,
                   const juce::String& parameterId, juce::Colour colour,
                   const juce::String& hint);

    /** 案内の帯に出す文。**部品を足したらここへ1行**（`hints`）。 */
    void registerHint (juce::Component& component, const juce::String& text);

    void showHint (const juce::String& text);

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawBox (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const;
    void drawField (juce::Graphics& g) const;
    void drawWordmark (juce::Graphics& g) const;

    /** 字間を空けて描く（見出しの小文字大文字）。**JUCEに字間の指定はありません**ので、
        1文字ずつ置いています。 */
    static void drawTracked (juce::Graphics& g, const juce::String& text,
                              juce::Point<float> origin, const juce::Font& font, float tracking);

    //==========================================================================
    RaccoGuitarProcessor& processor;

    juce::SharedResourcePointer<RaccoFieldLookAndFeel> fieldLookAndFeel;

    MantaPluginToolbar toolbar;
    juce::Label titleLabel;

    juce::Image fieldImage;

    juce::Label hintLabel;
    std::map<juce::Component*, juce::String> hints;
    juce::String defaultHint;

    RaccoArticulationBadge badge;

    ValueEntrySlider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label outputValue;

    juce::ComboBox pickupBox;
    juce::TextButton resetButton;

    std::array<std::unique_ptr<RaccoArticulationChip>, numGuitarArticulations> chips;

    std::unique_ptr<RaccoFieldKnob> brightness, sustain, pickPos, hardness, attack;

    RaccoGuitarKeyboard keyboard;

    int lastVoiceCount = -1;
    GuitarArticulation lastArticulation = GuitarArticulation::normal;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** **部品より後ろに置くこと**（繋ぎ先より先に壊れると、壊れた部品を触りにいく。1.5）。 */
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ComboAttachment> comboAttachments;

    std::vector<juce::Slider*> styledSliders;

    //==========================================================================
    // 寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）

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

    // 上の帯の中身
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

    // 2つめの帯の中身
    static constexpr int rowY      = 144;
    static constexpr int rowH      = 28;
    static constexpr int pickupX   = 1014;
    static constexpr int pickupW   = 170;
    static constexpr int resetX    = 938;
    static constexpr int resetW    = 64;

    static constexpr int boxY      = 180;
    static constexpr int boxH      = 62;
    static constexpr int leftBoxX  = 32;
    static constexpr int leftBoxW  = 540;
    static constexpr int rightBoxX = 588;
    static constexpr int rightBoxW = 596;

    static constexpr int chipInset  = 10;
    static constexpr int chipGap    = 5;
    static constexpr int chipHeight = 30;

    // 画像の上のつまみ
    static constexpr int knobCellW = 96;
    static constexpr int knobCellH = 96;

    /** 絵の中の置き場所（**割合**。等間隔にしないこと。クラスの説明）。 */
    struct KnobPlacement { float x, y; };

    static constexpr KnobPlacement knobPlacement[5]
    {
        { 0.31f, 0.34f },   // SUSTAIN
        { 0.69f, 0.34f },   // HARDNESS
        { 0.21f, 0.66f },   // BRIGHTNESS
        { 0.50f, 0.68f },   // PICK POS
        { 0.79f, 0.66f },   // ATTACK
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RaccoGuitarEditor)
};
