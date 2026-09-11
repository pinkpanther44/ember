#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaDelayDisplay.h"
#include "MantaDelayDuckMeter.h"   // 8.212（Phase 242）
#include "MantaDelayProcessor.h"
#include "MantaDelayTapStrip.h"    // 8.215（Phase 243）
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
    │  Echo [A][B]              Routing [Single ▾] …     │
    │  ┌──────────────────┐  ┌──────────────────────┐   │
    │  │ Time  Fb   Level │  │                      │   │
    │  │      (Sync)      │  │  反復のタイムライン    │   │
    │  │ Mix   Out  Pan   │  │ （`MantaDelayDisplay`）│   │
    │  └──────────────────┘  └──────────────────────┘   │
    │                                                    │
    │  Character [combo]  Drive Tone Wow … （Phase 2）   │
    │  ┌ Taps ────────────────────────────────────────┐  │
    │  │ Taps Step Level Pan │ ▇ ▅ ▂ ▁ … （一覧）     │  │
    │  └──────────────────────────────────────────────┘  │
    │  ┌ Filter ──────┐┌ Modulation ─┐┌ Ducking ─────┐  │
    │  │ [Type][Pre/Po]││ [Shape]     ││  ▭▬▬▬▬       │  │
    │  │ Freq  Q  Gain ││ Rate  Depth ││ Duck Atk Rel │  │
    │  └───────────────┘└─────────────┘└──────────────┘  │
    │  ▁▁▁▁▁▁▁ Phase 5b以降の場所 ▁▁▁▁▁▁▁              │
    └────────────────────────────────────────────────────┘
    ```

    ### 8.217：映しているのは**1エンジンぶんだけ**（Phase 244）

    `[A]` `[B]`で切り替えます。**`Mix`と`Output`と`Routing`以外は全部**、
    そのエンジンのものです（`rebuildEngineAttachments()`が繋ぎ直します）。

    Manta EQが12バンドでやっているのと同じ形です——
    **2エンジンぶんのつまみを一度に出す場所はありません。**

    ### 8.216：大きさは **900×680**（Phase 243／本人の判断）

    Phase 1〜3は**820×560**（Manta EQと同じ）でした。
    本人の最初の指定は「最初から最終形の大きさで作り、**ケースバイケースで微調整**」で、
    **ここがその「ケース」**です。

    Phase 4のタップの帯（本数＋Step／Level／Pan＋一覧）を820×560へ入れると、
    **ディスプレイが164→85pxまで潰れました。** この先Phase 5の
    ルーティング図とPhase 6も来るので、**1回だけ広げて、あとは動かしません。**

    > **8.172（画面は固定）は変えていません。** 変えたのは寸法そのもので、
    > 「開くたびに大きさが違う」ことにはなりません。
    > EQ 820×560／Comp 780×500 とは違う大きさになります——
    > **中身の量が違うので、揃える理由のほうが薄い。**

    ### 前の段階のものは動かさない

    **左のつまみの列（Time・Sync・Mix・Output）はPhase 1のまま**、
    キャラクターの並びもPhase 2のまま、Phase 3の3つもそのままです。
    **段階を進めるたびに前の段階のものが動くと、覚え直しになります。**

    段階ごとに**下へ帯を1本ずつ足す**形にしてあります。
    広げたぶん（8.216）はディスプレイへ回ったので、**Phase 3のときより広くなりました。**

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

    /** 8.217：**見た目だけ**（Phase 244）。繋ぎは`rebuildEngineAttachments()`。 */
    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                     juce::Colour colour);

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

    /** 8.214：選んでいるタップへつまみを繋ぎ直す（Phase 243）。

        **Manta EQの`rebuildBandAttachments()`と同じ形**です。 */
    void rebuildTapAttachments();

    /** 8.217：選んでいるエンジンへ、**つまみを丸ごと繋ぎ直す**（Phase 244）。

        タップのときと同じ理由でこの形です（8.215）——
        **表に出ているのは1エンジンぶんだけ**で、切り替えるのは人が押したときだけ。 */
    void rebuildEngineAttachments();

    /** 8.217：エンジンの選び直し（押されたとき・開いたとき）。 */
    void setSelectedEngine (int engine);

    /** 8.217：ルーティングで効かなくなるものをグレーアウト（Phase 244）。 */
    void refreshRoutingControls();

    /** 8.214：本数の外のタップを選んでいたら中へ戻す＋グレーアウトの更新。 */
    void refreshTapControls();

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

    /** 8.217：そのエンジンの出口（Phase 244）。**Dualで釣り合いを取るところ**。 */
    ValueEntrySlider levelSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider panSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label timeCaption, feedbackCaption, mixCaption, outputCaption, levelCaption, panCaption;

    juce::TextButton syncButton;

    //==========================================================================
    // 8.217：Phase 5aのデュアルエンジン（Phase 244）

    /** どちらのエンジンを映しているか（`[A]` `[B]`）。 */
    juce::TextButton engineAButton, engineBButton;

    juce::Label routingCaption;
    juce::ComboBox routingBox;

    /** モードの説明（`MantaDelayRouting::getModeDescription()`）。

        **名前だけでは何が起きるか分かりません**——「Series」と書いてあっても、
        AとBのどちらが前か読めない。 */
    juce::Label routingDescription;

    /** つまみが映しているエンジン（0＝A）。**`getUiState()`に残ります。** */
    int selectedEngine = 0;

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
    // 8.214〜8.215：Phase 4（Phase 243）

    juce::Label tapsTitle;

    /** 全体が見える窓。**つまみは1本ぶんしか映さない**ので、これが要ります。 */
    MantaDelayTapStrip tapStrip;

    ValueEntrySlider tapCountSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider tapStepSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider tapLevelSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider tapPanSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label tapCountCaption, tapStepCaption, tapLevelCaption, tapPanCaption;

    /** つまみが指しているタップ（0起点）。**`getUiState()`に残ります**（8.214）。 */
    int selectedTap = 0;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    /** エンジン共通のつなぎ（`Mix`・`Output`・`Routing`）。**作ったら外しません。** */
    juce::OwnedArray<SliderAttachment> globalAttachments;
    std::unique_ptr<ComboAttachment> routingAttachment;

    /** 8.217：**エンジンごとのつなぎ**（Phase 244）。

        A/Bを切り替えるたびに**全部作り直します**（`rebuildEngineAttachments()`）。
        Manta EQのバンドと同じ考え方で、**表に出ているのは1エンジンぶんだけ**だからです。

        まとめて持っているのは、**外し忘れを作らないため**——
        1本ずつ`std::unique_ptr`で持つと、足したときにリセットを書き忘れます。 */
    juce::OwnedArray<SliderAttachment> engineSliderAttachments;
    juce::OwnedArray<ComboAttachment> engineComboAttachments;
    juce::OwnedArray<ButtonAttachment> engineButtonAttachments;

    /** 8.214：タップのつなぎ先（Phase 243）。

        **本数は繋ぎっぱなし**、**3つは選んだタップへ繋ぎ直します**。

        > 8.207では「`SliderAttachment`は付け替えない」と書きました。
        > **ここはその例外**です——Manta EQが12バンド×12個で同じことをしています
        > （`rebuildBandAttachments()`）。違いは**繋ぎ替える理由**：
        > 8.207はSyncの入り切り＝**オートメーションやプリセットでも動く**ので、
        > 繋ぎ替えが音の最中に起きます。**タップを選ぶのは人が押したときだけ**で、
        > 音にもオートメーションにも関係しません。
        >
        > **繋ぎ替えるときは、いったん全部外してから**（同じつまみに2本ぶら下がると、
        > 片方が前のタップへ書き戻します）。 */
    std::unique_ptr<SliderAttachment> tapStepAttachment;
    std::unique_ptr<SliderAttachment> tapLevelAttachment;
    std::unique_ptr<SliderAttachment> tapPanAttachment;

    /** いま繋いであるタップ。**-1なら繋いでいない。** */
    int attachedTap = -1;

    /** 画面の大きさ。**固定です**（8.172）。8.216でPhase 4のぶん広げました。 */
    static constexpr int fixedWidth = 900;
    static constexpr int fixedHeight = 680;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    //
    // 8.217：**200→300**（Phase 244）。Phase 5で`Level`と`Pan`が増えたので、
    // 段を足すのではなく**横に伸ばして1段3つ**にしました——
    // 縦はもう空いていません（左の列は304pxのうち208pxを使っています）
    static constexpr int knobPanelWidth = 300;

    /** 8.217：`Echo`／`[A][B]`／`Routing`が並ぶ行（Phase 244）。 */
    static constexpr int headerHeight = 26;
    static constexpr int knobWidth = 76;
    static constexpr int knobHeight = 84;
    static constexpr int sectionTitleHeight = 18;

    /** 8.210〜8.212：Phase 3の3つ（Filter・Modulation・Ducking）が入る帯（Phase 242）。

        `sectionTitleHeight`＋コンボの行＋つまみ1段ぶん。**寸法を直に書かない**（1.27）。 */
    static constexpr int phase3AreaHeight = 154;
    static constexpr int phase3ComboRowHeight = 24;

    /** 8.214：Phase 4のタップの帯（Phase 243）。見出し＋つまみ1段。

        Phase 3の帯と違ってコンボの行がありません——選ぶものが無いためです
        （どのタップを直すかは、右の一覧を押して決めます）。 */
    static constexpr int tapsAreaHeight = 128;

    /** 8.210：**細い1行だけ**に縮めました（Phase 242）。
        Phase 2までは74pxの箱でしたが、その場所はPhase 3の帯が使います。 */
    static constexpr int futureAreaHeight = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaDelayEditor)
};
