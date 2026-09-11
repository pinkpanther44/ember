#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaDelayDisplay.h"
#include "MantaDelayDuckMeter.h"   // 8.212（Phase 242）
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
    │  Character [combo]  Drive Tone Wow … （Phase 2）   │
    │  ┌ Filter ──────┐┌ Modulation ─┐┌ Ducking ─────┐  │
    │  │ [Type][Pre/Po]││ [Shape]     ││  ▭▬▬▬▬       │  │
    │  │ Freq  Q  Gain ││ Rate  Depth ││ Duck Atk Rel │  │
    │  └───────────────┘└─────────────┘└──────────────┘  │
    │  ▁▁▁▁▁▁▁ Phase 4以降の場所 ▁▁▁▁▁▁▁               │
    └────────────────────────────────────────────────────┘
    ```

    ### 大きさは最初から最終形

    **820×560**（Manta EQと同じ）。本人の指定です——
    「最初から最終形の大きさで作り、ケースバイケースで微調整」。

    段階ごとに寸法を変えると**開くたびに大きさが違う**ことになります
    （8.172：画面は固定）。Phase 3で**中身がほぼ埋まりました**——
    Phase 1のときに空いていた下半分が、Phase 2（キャラクター）と
    Phase 3（フィルター・LFO・ダッキング）で埋まった形です。

    ### 前の段階のものは動かさない

    **左のつまみの列（Time・Sync・Mix・Output）はPhase 1のまま**、
    キャラクターの並びもPhase 2のままです。
    **段階を進めるたびに前の段階のものが動くと、覚え直しになります。**

    Phase 3のぶんは**下に帯を1本足して**、そこに3つ並べてあります。
    ディスプレイの高さだけは縮みました（そこ以外に取れる場所がない）。

    ### 空きは黙って空けない

    いちばん下の細い行に**これから何が入るか**を薄く出してあります。
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
    void refreshCharacterControls();   // 8.208：効かないつまみをグレーアウト（Phase 240）

    void refreshTimeControls();

    /** 8.210：フィルターの形で効かなくなるつまみをグレーアウト（Phase 242）。

        **判断は`MantaDelayFilter`の`isActive()`／`usesGain()`だけ**（1.27）——
        画面と音で別々に決めると、触れるのに効かないつまみができます。 */
    void refreshFilterControls();

    /** グレーアウトの掛け方。**Phase 2とPhase 3で同じものを使います**（1.27）。 */
    void setControlActive (juce::Component& control, juce::Label& caption, bool active);

    /** 8.210：Phase 3の3つの箱（Filter・Modulation・Ducking。Phase 242）。

        **`paint()`と`resized()`が同じものを見ます**（1.27）——
        別々に数えると、**枠と中身がずれます。** */
    juce::Array<juce::Rectangle<int>> getPhase3Columns (juce::Rectangle<int> band) const;

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
    // 8.208：Phase 2（キャラクター。Phase 240）

    juce::Label characterTitle;
    juce::ComboBox characterBox;

    ValueEntrySlider driveSlider        { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider toneSlider         { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider wowDepthSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider wowRateSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider flutterDepthSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider flutterRateSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label driveCaption, toneCaption, wowDepthCaption, wowRateCaption,
                flutterDepthCaption, flutterRateCaption;

    //==========================================================================
    // 8.210〜8.212：Phase 3（Phase 242）

    juce::Label filterTitle, modulationTitle, duckingTitle;

    juce::ComboBox filterTypeBox;

    /** 仕様書5-2の`Position`。**押すと表の文字が変わります**（Pre ⇄ Post）——
        「Post」と書いたボタンが押されている／いないの2通りより、
        **いまどちらなのか**がそのまま読めるほうがよい。 */
    juce::TextButton filterPositionButton;

    ValueEntrySlider filterFreqSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider filterQSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider filterGainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label filterFreqCaption, filterQCaption, filterGainCaption;

    juce::ComboBox lfoShapeBox;

    ValueEntrySlider lfoRateSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider lfoDepthSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label lfoRateCaption, lfoDepthCaption;

    MantaDelayDuckMeter duckMeter;

    ValueEntrySlider duckAmountSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider duckAttackSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider duckReleaseSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label duckAmountCaption, duckAttackCaption, duckReleaseCaption;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    std::unique_ptr<ButtonAttachment> syncAttachment;
    std::unique_ptr<SliderAttachment> divisionAttachment;   // 8.207（Phase 239）
    std::unique_ptr<ComboAttachment> characterAttachment;   // 8.208（Phase 240）

    // 8.210〜8.211（Phase 242）
    std::unique_ptr<ComboAttachment> filterTypeAttachment;
    std::unique_ptr<ButtonAttachment> filterPositionAttachment;
    std::unique_ptr<ComboAttachment> lfoShapeAttachment;

    /** 画面の大きさ。**固定です**（8.172）。 */
    static constexpr int fixedWidth = 820;
    static constexpr int fixedHeight = 560;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    static constexpr int knobPanelWidth = 200;
    static constexpr int knobWidth = 76;
    static constexpr int knobHeight = 84;
    static constexpr int sectionTitleHeight = 18;

    /** 8.210〜8.212：Phase 3の3つ（Filter・Modulation・Ducking）が入る帯（Phase 242）。

        `sectionTitleHeight`＋コンボの行＋つまみ1段ぶん。**寸法を直に書かない**（1.27）。 */
    static constexpr int phase3AreaHeight = 154;
    static constexpr int phase3ComboRowHeight = 24;

    /** 8.210：**細い1行だけ**に縮めました（Phase 242）。
        Phase 2までは74pxの箱でしたが、その場所はPhase 3の帯が使います。 */
    static constexpr int futureAreaHeight = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayEditor)
};
