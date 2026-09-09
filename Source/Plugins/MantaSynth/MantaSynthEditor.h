#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "MantaSynthDisplays.h"
#include "MantaSynthPresets.h"
#include "MantaSynthProcessor.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <array>
#include <memory>

//==============================================================================
/**
    見出しの縦線を兼ねたON/OFFボタン（EAGLE type0の`BarToggle`）。

    **見出しの左に立っている棒がそのままボタン**です。
    切ると灰色になります——「このセクションは効いていない」が、
    どこにも文字を足さずに読めます。
*/
class SynthSectionToggle : public juce::Button
{
public:
    explicit SynthSectionToggle (juce::Colour onColourToUse)
        : juce::Button ("section"), onColour (onColourToUse)
    {
        setClickingTogglesState (true);
        setToggleState (true, juce::dontSendNotification);
    }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool) override
    {
        auto area = getLocalBounds().toFloat();
        const float barWidth = 3.5f;

        const juce::Rectangle<float> bar (area.getCentreX() - barWidth * 0.5f, area.getY() + 2.0f,
                                           barWidth, area.getHeight() - 4.0f);

        auto colour = getToggleState() ? onColour : MantaTheme::border();
        if (isMouseOver) colour = colour.brighter (0.35f);

        g.setColour (colour);
        g.fillRoundedRectangle (bar, barWidth * 0.5f);
    }

private:
    juce::Colour onColour;
};

//==============================================================================
/**
    見出し付きのつまみ1つ。

    **数値は出しっぱなしにしていません**（EAGLE type0と同じ）。
    このシンセは1画面に40個以上つまみがあり、全部に数字を添えると
    **数字のほうが目立って、どれがどのセクションか読めなくなります。**

    代わりに、

    - **掴んでいるあいだだけ**吹き出しに出る（`setPopupDisplayEnabled`）
    - **右クリックで打ち込める**（`ValueEntrySlider`。本体のつまみと同じ操作）

    としてあります。単位は`MantaSynthParameters.cpp`が作っています。
*/
class SynthKnob : public juce::Component
{
public:
    explicit SynthKnob (const juce::String& name)
        : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
    {
        addAndMakeVisible (slider);

        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, MantaTheme::textDim());
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (14));
        slider.setBounds (area);
    }

    ValueEntrySlider slider;
    juce::Label label;
};

//==============================================================================
/**
    Manta Synthesizer の画面（Phase 213、並びはPhase 214で整えた）。

    ### 3列の目盛りに全部載せてある（Phase 214／本人の要望）

    横の位置は`col0`(12) / `col1`(421) / `col2`(830)の**3つだけ**で、
    幅はどれも`colWidth`(398)です。左の列は`halfX`(216)で半分に割ります。

    ```
    ┌────────────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets              Manta Synthesizer │ ← 共有ツールバー
    ├────────────────┬──────────┬─────┬───────┬────────┤
    │ OSC 1          │ FILTER   │DRIVE│ LFO 1 │ LFO 2  │ ← 1段目だけ4分割
    ├────────────────┼──────────┴─────┴───────┴────────┤
    │ OSC 2          │ AMP ENV        │ MOD ENV         │
    ├───────┬────────┤                │                 │
    │SUB OSC│NOISE   │                │                 │
    ├───────┼────────┼────────────────┼─────────────────┤
    │ FX 1  │ FX 2   │ MOD MATRIX     │ OSCILLOSCOPE    │
    ├───────┼────────┤ （4スロット）    ├─────────────────┤
    │ EQ    │ MASTER │                │ METER           │
    ├───────┴────────┴────────────────┴─────────────────┤
    │ 鍵盤                                                │
    └────────────────────────────────────────────────────┘
    ```

    Phase 213では3段目だけ「310 ＋ 489」という別の寸法で分けていました。
    **列が揃わないと、下の段だけ別の絵に見えます**——縦線が通らないためです。

    ### EAGLE type0からの持ち込みと、変えたところ

    元の並びをそのまま持ってきています（本人の指示）。変えたのは3つです。

    | | EAGLE type0 | ここ | なぜ |
    |---|---|---|---|
    | 上の帯 | 72pxのタイトル＋Preset/Save/Undo/Redo/倍率 | **32pxの共有ツールバー** | Manta EQ・Manta Compと同じものを使うため。A/Bも付いてくる |
    | マトリクス | 8スロット（2列） | **4スロット（1列）** | 本人の指定 |
    | 3段目の割り方 | 2列ぶんをマトリクスが占有 | **`col1`にマトリクス、`col2`にオシロ＋メーター** | 上の段と縦線が通る |

    ### マトリクスが減って空いた場所

    8スロットは横603pxを使っていました。4スロット1列なら足ります。
    **余ったぶんは、上の列に合わせて2つに割りました**——
    マトリクスは選択欄を140pxまで広げ、オシロスコープとメーターは
    `colWidth`いっぱいを使います（どちらも**横に長いほど読める**もの。
    波形は1周期が広がり、メーターは目盛りの間隔が空きます）。

    つまみを大きくする案は採っていません——正円なので、
    **余りは隙間になるだけ**です（8.172）。

    ### つまみのあいだの余白（Phase 214／本人の要望）

    FILTER・LFO・ENVは、**パネルのほうがつまみの列よりずっと広い**セクションです。
    Phase 213までは隙間なく並べてから塊ごと中央へ寄せていたので、
    真ん中にひとかたまり浮いて、左右に大きな空きが残っていました。

    `resized()`の`placeKnobRow()`が、**余りをつまみのあいだへ等分**します。
    `count + 1`で割っているので**端にも同じだけ残ります**——
    端まで詰めると、つまみが箱に貼り付いて見えます。

    OSC 1・OSC 2・FX・EQは**そのまま**です。
    右に波形プレビューやボタンが入っていて、もともと余っていません。

    ### 倍率のつまみは外した

    8.172：**画面の大きさは固定**という本人の方針です。
    EAGLE type0の50〜150%は、伸縮できる窓とセットの仕掛けでした。

    ### 色

    向こうのシアンは**パープル**（`MantaTheme::accent()`）にしてあります。
    オレンジは**「動かすもの」**——LFO・Mod Env・DRIVE・NOISE、
    つまり音を揺らしたり歪ませたりする側です。
    パープルは**音そのもの**（OSC・AMP ENV・EQ・MASTER）。
*/
class MantaSynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit MantaSynthEditor (MantaSynthProcessor& processorToUse);
    ~MantaSynthEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    void addKnob (std::unique_ptr<SynthKnob>& knob, const juce::String& name,
                   const juce::String& parameterId, juce::Colour colour, bool bipolar = false);

    void addCombo (juce::ComboBox& box, const juce::String& parameterId,
                    const juce::StringArray& items);

    void addToggle (std::unique_ptr<SynthSectionToggle>& toggle,
                     const juce::String& parameterId, juce::Colour colour);

    /** セクションの箱と見出しを描く。 */
    void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const;

    /** ON/OFFできないセクションに置く、灰色の縦線（見た目をそろえるため）。 */
    void drawStaticBar (juce::Graphics& g, int x, int y) const;

    MantaSynthProcessor& processor;

    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaPluginToolbar toolbar;
    juce::Label titleLabel;

    //==========================================================================
    // OSC 1 / OSC 2
    std::unique_ptr<SynthKnob> osc1Voices, osc1Detune, osc1Level, osc1Spread, glide;
    std::unique_ptr<SynthKnob> osc2Voices, osc2Detune, osc2Level, osc2Octave, osc2Spread;
    juce::ComboBox osc1Wave, osc2Wave;

    // SUB / NOISE
    std::unique_ptr<SynthKnob> subLevel, noiseLevel;
    juce::ComboBox subWave, subOctave, noiseType;

    // FILTER / DRIVE
    std::unique_ptr<SynthKnob> cutoff, resonance, modEnvCut, drive;
    juce::ComboBox filterType;

    // LFO 1 / LFO 2
    std::unique_ptr<SynthKnob> lfoRate, lfoDepth, lfo2Rate, lfo2Depth;
    juce::ComboBox lfoDest, lfo2Dest;

    // ENV
    std::unique_ptr<SynthKnob> ampA, ampD, ampS, ampR;
    std::unique_ptr<SynthKnob> modA, modD, modS, modR;

    // FX / EQ / MASTER
    std::unique_ptr<SynthKnob> fx1Rate, fx1Depth, fx1Fb, fx1Mix;
    std::unique_ptr<SynthKnob> fx2Rate, fx2Depth, fx2Fb, fx2Mix;
    std::unique_ptr<SynthKnob> eqLow, eqMid, eqMidF, eqHigh, gain;
    juce::ComboBox fx1Type, fx2Type;

    // 見るだけのもの
    std::unique_ptr<SynthWaveformDisplay> osc1Waveform, osc2Waveform;
    std::unique_ptr<SynthEnvelopeDisplay> ampEnvDisplay, modEnvDisplay;
    std::unique_ptr<SynthLevelMeter> meter;
    std::unique_ptr<SynthOscilloscope> scope;

    // セクションのON/OFF
    std::unique_ptr<SynthSectionToggle> toggleOsc1, toggleOsc2, toggleSub, toggleNoise,
                                         toggleFilter, toggleDrive, toggleLfo, toggleLfo2,
                                         toggleModEnv, toggleFx1, toggleFx2, toggleEq;

    // MOD MATRIX（4スロット）
    std::array<juce::ComboBox, MantaSynthParams::numMatrixSlots> matrixSourceBox, matrixDestinationBox;
    std::array<std::unique_ptr<ValueEntrySlider>, MantaSynthParams::numMatrixSlots> matrixAmount;

    juce::MidiKeyboardComponent keyboard;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** **部品より後ろに置くこと。** 繋ぎ先より先に壊れないと、
        壊れた部品を触りにいきます（1.5と同じ決まり）。 */
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ComboAttachment> comboAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;

    /** つまみに被せたLookAndFeelを外すために覚えておく。 */
    std::vector<juce::Slider*> styledSliders;

    //==========================================================================
    // 8.172：**画面の大きさは固定**（Phase 212の方針）
    static constexpr int fixedWidth = 1240;
    static constexpr int fixedHeight = 720;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    static constexpr int margin = 12;

    static constexpr int col0 = 12;
    static constexpr int col1 = 421;
    static constexpr int col2 = 830;
    static constexpr int colWidth = 398;
    static constexpr int halfX = 216;
    static constexpr int halfWidth = 194;

    // 1段目の内訳（FILTER / DRIVE / LFO1 / LFO2）
    static constexpr int filterWidth = 258;
    static constexpr int driveX = 689;
    static constexpr int driveWidth = 130;
    static constexpr int lfoX = 830;
    static constexpr int lfoWidth = 194;
    static constexpr int lfo2X = 1034;

    static constexpr int sectionHeight = 108;    ///< 1段ぶん
    static constexpr int tallSectionHeight = 224; ///< 2段ぶち抜き

    static constexpr int row1Y = 44;
    static constexpr int row2Y = 162;
    static constexpr int row2bY = 278;
    static constexpr int row3Y = 396;
    static constexpr int row3bY = 512;

    // 3段目。**2段目とまったく同じ3列に載せてあります**（Phase 214／本人の要望）。
    //
    // マトリクスが4スロットになって空いた場所を、Phase 213では
    // 「310 ＋ 489」という3段目だけの寸法で分けていました。
    // **列が揃わないので、下の段だけ別の絵に見えていました**——
    // `col1`・`col2`へ載せ替えると、OSC2／AMP ENV／MOD ENVの縦線がそのまま通ります
    static constexpr int matrixX = col1;
    static constexpr int matrixWidth = colWidth;
    static constexpr int scopeX = col2;
    static constexpr int scopeWidth = colWidth;

    // MOD MATRIXの1行の内訳。**`paint()`（列見出し）と`resized()`の両方が見ます**ので、
    // 数字を直に書かないこと（1.27）
    static constexpr int matrixInset = 28;      ///< パネルの左右に残す余白
    static constexpr int matrixBoxWidth = 140;  ///< SOURCE／DESTINATIONの選択欄
    static constexpr int matrixGap = 8;
    static constexpr int matrixAmountSize = 40; ///< AMOUNTのつまみ
    static constexpr int matrixRowHeight = 44;

    static constexpr int matrixSourceX = matrixX + matrixInset;
    static constexpr int matrixDestX = matrixSourceX + matrixBoxWidth + matrixGap;
    static constexpr int matrixAmountX = matrixDestX + matrixBoxWidth + matrixGap * 2;

    static constexpr int keyboardY = 632;
    static constexpr int keyboardHeight = 76;

    static constexpr int knobWidth = 44;
    static constexpr int knobHeight = 62;
    static constexpr int knobTop = 36;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaSynthEditor)
};
