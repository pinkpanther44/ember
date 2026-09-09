#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EQCurveComponent.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "MantaEQProcessor.h"
#include "../../ValueEntrySlider.h"

#include <vector>

//==============================================================================
/**
    設計書5.1のレイアウト（仕様書3章のPro-Q3/4準拠の並び）。

    ```
    ┌──────────────────────────────────────────────┐
    │ Undo Redo │ A/B Copy │ Presets              │ ← ツールバー
    ├──────────────────────────────────────────────┤
    │                                                │
    │        EQカーブ ＋ スペクトラムアナライザー        │ ← EQCurveComponent
    │                                                │
    ├──────────────────────────────────────────────┤
    │ Keys │ Pre Post Freeze │ Speed Tilt Range      │ ← アナライザーの帯
    ├───────────────────────────────┬──────────────┤
    │ バンドのつまみ（選んでいる1本ぶん）  │ Output       │
    └───────────────────────────────┴──────────────┘
    ```

    ### つまみは「選んでいる1本」ぶんだけ

    12本ぶんのつまみを並べると、1本あたりの幅が足りません。
    **グラフで選んだ1本だけ**を下に出し、選択は`EQCurveComponent`が持っています
    （Pro-Qと同じ）。

    ### Undo／Redoの粒度

    仕様書4.17。プラグインのパラメータは`juce::UndoManager`の対象外なので、
    **値の写しを積む**方式にしています。

    - 5回／秒で見比べて、**変わっていたら1つ積む**
    - **マウスのボタンが下りているあいだは積まない**
      （ドラッグの途中が刻まれると、Undoを何十回も押すことになります）

    つまり**「手を離すたびに1つ」**の粒度になります。

    ### A/B と Presets

    - **A/B**：値の写しを2つ持ち、ボタンで入れ替える
    - **Copy**：いま鳴っているほうを、もう片方へ写す（A/Bの出発点をそろえる）
    - **Presets**：`%APPDATA%\PersonalDAW\Presets\MantaEQ\*.xml`
      （プリセットには**アナライザーの見せ方も入ります**——`apvts.state`ごと保存するため）
*/
class MantaEQEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit MantaEQEditor (MantaEQProcessor& processorToUse);
    ~MantaEQEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    /** 出力レベルの小さなメーター（仕様書4.16のGain Scaleメーター）。 */
    class OutputMeter : public juce::Component,
                         private juce::Timer
    {
    public:
        explicit OutputMeter (MantaEQProcessor& processorToUse);
        ~OutputMeter() override;

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override;

        MantaEQProcessor& processor;
        float displayedDb[2] { -100.0f, -100.0f };
    };

    //==========================================================================
    void timerCallback() override;

    /** 選んでいるバンドが変わったので、つまみの繋ぎ先を張り替える。 */
    void rebuildBandAttachments();

    /** 形状に応じて、使わないつまみを伏せる（Gainを持たない形状など）。 */
    void updateControlVisibility();

    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                     bool bipolar = false);

    /** つまみの入切と色。

        **色も変えること**——`MantaKnobLookAndFeel`は弧も指針も
        `rotarySliderFillColourId`で描くので、`setEnabled(false)`だけでは
        **触れないのに触れそうに見えます**。

        `colour`はバンドのつまみなら**そのバンドの色**（＝ステレオ配置の色）、
        出力セクションならパープル（Phase 206）。 */
    void setKnobEnabled (ValueEntrySlider& slider, juce::Label& caption,
                          bool shouldBeEnabled, juce::Colour colour);
    void setupCombo (juce::ComboBox& box, const juce::StringArray& items);
    void setupToolbarButton (juce::TextButton& button, const juce::String& text);

    //==========================================================================

    //==========================================================================
    void refreshAnalyserButtons();

    /** 仕様書4.7：いまの遅れと、Linear Phaseでダイナミクスが止まっていることを出す。 */
    void refreshLatencyLabel();
    void writeUiProperty (const juce::Identifier& id, const juce::var& value);

    //==========================================================================
    /** Phase 206：**つまみの見た目**（弧付き。`MantaKnobLookAndFeel.h`）。

        **いちばん最初に宣言すること。** メンバーは宣言と逆順に壊れるので、
        ここが最後に壊れます——つまみより先に壊れると、
        壊れたLookAndFeelを指したままのつまみが残ります
        （`ValueEntrySlider`が`MixerLookAndFeel`で守っているのと同じ決まり。8.62）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaEQProcessor& processor;

    EQCurveComponent curve;

    /** 仕様書4.17：Undo/Redo・A/B・プリセット（`MantaPluginToolbar.h`）。
        **Manta Compと同じもの**です（Phase 208で切り出しました）。 */
    MantaPluginToolbar toolbar;

    /** 仕様書4.7：処理モードと、その遅れの表示。**ツールバーの空いている場所**へ
        置いてあります（曲づくりの途中で何度も触るものではないが、
        **いま何msか**は常に見えていること）。 */
    juce::ComboBox modeBox, resolutionBox;
    juce::Label latencyLabel;

    // アナライザーの帯
    juce::TextButton keyboardButton, preButton, postButton, freezeButton;
    juce::ComboBox speedBox, tiltBox, floorBox;
    juce::Label analyserCaption;

    // バンドのつまみ
    juce::Label bandTitle, emptyHint;
    juce::TextButton bandOnButton, bandSoloButton, bandDynButton;
    ValueEntrySlider freqSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider gainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider qSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider thresholdSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider rangeSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider attackSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider releaseSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label freqCaption, gainCaption, qCaption;
    juce::Label thresholdCaption, rangeCaption, attackCaption, releaseCaption;
    juce::ComboBox shapeBox, slopeBox, channelBox;
    juce::Label shapeCaption, slopeCaption, channelCaption;

    // 出力セクション
    juce::Label outputTitle;
    ValueEntrySlider outGainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outPanSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outMsSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label outGainCaption, outPanCaption, outMsCaption;
    juce::TextButton phaseButton, autoGainButton;
    OutputMeter meter { processor };

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<SliderAttachment> freqAttachment, gainAttachment, qAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment, rangeAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment, releaseAttachment;
    std::unique_ptr<ComboAttachment> shapeAttachment, slopeAttachment, channelAttachment;
    std::unique_ptr<ButtonAttachment> bandOnAttachment, bandDynAttachment;

    std::unique_ptr<SliderAttachment> outGainAttachment, outPanAttachment, outMsAttachment;
    std::unique_ptr<ButtonAttachment> phaseAttachment, autoGainAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, resolutionAttachment;

    //==========================================================================

    int attachedBand = -1;

    /** 画面の大きさ。**固定です**（Phase 212）。

        伸縮をやめたので、覚えて戻す仕組み（`layoutReady`と
        `editorWidth`/`editorHeight`）も一緒に外してあります——
        大きさが1つしか無いのに保存すると、
        **寸法を変えたときに古い値のまま開く**という壊れ方をします。 */
    static constexpr int fixedWidth = 820;
    static constexpr int fixedHeight = 560;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かないこと（1.27）
    static constexpr int toolbarHeight = 32;
    static constexpr int analyserBarHeight = 30;
    static constexpr int bottomStripHeight = 132;
    static constexpr int outputSectionWidth = 232;
    static constexpr int knobWidth = 60;
    static constexpr int comboColumnWidth = 110;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaEQEditor)
};
