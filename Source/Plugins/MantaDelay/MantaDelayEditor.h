#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaDelayDisplay.h"
#include "MantaDelayProcessor.h"
#include "MantaDelayTheme.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../../ValueEntrySlider.h"

//==============================================================================
/**
    Manta Delay の画面（ディレイ設計書6章のたたき台＋本人の指定）。

    ```
    ┌────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets           Manta Delay │ ← 共有ツールバー
    ├────────────────────────────────────────────────────┤
    │  ECHO                                              │
    │  ┌──────────────┐  ┌──────────────────────────┐   │
    │  │ Time         │  │                          │   │
    │  │ (Sync/Free)  │  │  反復のタイムライン        │   │
    │  │ Feedback     │  │  （`MantaDelayDisplay`）  │   │
    │  │ Mix          │  │                          │   │
    │  └──────────────┘  └──────────────────────────┘   │
    │                                                    │
    │  ▁▁▁▁▁▁▁ Phase 2以降の場所 ▁▁▁▁▁▁▁               │
    └────────────────────────────────────────────────────┘
    ```

    ### 大きさは最初から最終形

    **820×560**（Manta EQと同じ）。本人の指定です——
    「最初から最終形の大きさで作り、ケースバイケースで微調整」。

    Phase 1では**下半分が空きます**が、段階ごとに寸法を変えると
    **開くたびに大きさが違う**ことになります（8.172：画面は固定）。

    ### 空きは黙って空けない

    下の帯には**これから何が入るか**を薄く出してあります。
    **何も無い灰色の面**は「壊れている」ようにも見えるので、
    「まだ作っていない」と分かる形にしてあります。

    ### 色

    `MantaDelayTheme`（＝`Branding.h`のディレイ専用アクセント2色）。
    **ディレイだけアプリの配色から外れます**（本人の指定。8.204）——
    Manta Delayはパープル＋オレンジ（従来どおり）、
    Hawkbill Delayは**ブルー＋イエロー**。

    | 弧の色 | どこ |
    |---|---|
    | 主（ブルー／パープル） | Time・Feedback——**反復そのものを決めるところ** |
    | 副（イエロー／オレンジ） | Mix・Output——**原音との混ぜ具合** |
*/
class MantaDelayEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit MantaDelayEditor (MantaDelayProcessor& processorToUse);
    ~MantaDelayEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                     const char* parameterId, juce::Colour colour);

    void setupSectionLabel (juce::Label& label, const juce::String& text);

    /** Syncの入り切りで、TimeのつまみとDivisionの出し分けを変える。 */
    void refreshTimeControls();

    //==========================================================================
    /** **いちばん最初に宣言すること**（つまみより後に壊れるように。8.168）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaDelayProcessor& processor;

    MantaPluginToolbar toolbar;
    juce::Label statusLabel;        ///< ツールバーの右（テンポが来ていないときの表示）

    MantaDelayDisplay display;

    juce::Label echoTitle;

    /** 8.207：**Timeのつまみは2つ重ねてあります**（Phase 239/本人の指定）。

        Syncの入り切りで**中身が変わる1つのつまみ**に見せますが、
        中身は別々のものです：

        | | 触るパラメータ | 回り方 |
        |---|---|---|
        | `timeSlider` | `timeMs` | なめらか |
        | `divisionSlider` | `syncDivision` | **段々**（1目盛りずつ） |

        **1つのつなぎ先を差し替えるのではなく、2つ置いて出し分けます。**
        `SliderAttachment`は付け替えを想定した作りではなく、
        差し替えるたびに**オートメーションとUndoの繋がりを組み直す**ことになります。
        置き場所は同じなので、**見た目は1つのつまみ**です。 */
    ValueEntrySlider timeSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider divisionSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider feedbackSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider mixSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outputSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label timeCaption, feedbackCaption, mixCaption, outputCaption;

    juce::TextButton syncButton;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<SliderAttachment> divisionAttachment;   // 8.207（Phase 239）

    /** 画面の大きさ。**固定です**（8.172）。 */
    static constexpr int fixedWidth = 820;
    static constexpr int fixedHeight = 560;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    static constexpr int knobPanelWidth = 200;
    static constexpr int knobWidth = 76;
    static constexpr int knobHeight = 84;
    static constexpr int sectionTitleHeight = 18;
    static constexpr int futureAreaHeight = 190;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayEditor)
};
