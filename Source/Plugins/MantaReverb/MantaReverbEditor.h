#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaReverbDisplay.h"
#include "MantaReverbProcessor.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

//==============================================================================
/**
    Manta Reverb の画面（リバーブ設計書2章の`PluginEditor`）。

    ```
    ┌──────────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets                Manta Reverb │ ← 共有ツールバー
    ├──────────────────────────────────────────────────────────┤
    │ Algorithm [Hall ▾] [A][B]  Routing [Cascade ▾] A into B  │
    │ ┌──────────────────────────────────────────────────────┐ │
    │ │            響きの形（`MantaReverbDisplay`）           │ │
    │ └──────────────────────────────────────────────────────┘ │
    │ ┌ Hall ─ 長く広い ─┐┌ Tail ───────────┐┌ Output ───────┐ │
    │ │ Predly Decay Size││ HFFrq HFDmp Mod ││ Early Wid Lvl │ │
    │ │ Shape  Sprd  Diff││ LFFrq LFDmp     ││ Mix  Out  Sat │ │
    │ └──────────────────┘└─────────────────┘└───────────────┘ │
    └──────────────────────────────────────────────────────────┘
    ```

    ### 8.241：大きさは **900×680**（Phase 254／本人の判断）

    **ディレイと同じ**にしました。中身の量が近く、**大きいエフェクトが2つとも
    同じ大きさで開く**ほうが、並べて使うときに落ち着きます。

    **Phase 1からPhase 6まで、一度も動かしていません。**
    ディレイでは Phase 4 で 820×560 から広げました（8.216）が、
    **今回は最初から最終形の量で決めてあった**ので、その必要がありませんでした。

    | 段階 | 増えたもの | 入った場所 |
    |---|---|---|
    | Phase 2 | `Algorithm`のコンボ | 見出しの行 |
    | Phase 3 | Shape／Spread | 箱の幅を配り直し |
    | Phase 4 | Twin Delays／Panorama固有 | **同じ升目に重ねて出し分け** |
    | Phase 5 | `[A][B]`と`Routing`、`Level` | 見出しの行の右／`Output`の箱 |
    | Phase 6 | Modulation／Saturation | `Tail`と`Output`の箱 |

    **伸縮はできません**（8.172）。`setResizable()`を呼んでいません。

    ### 8.253：いちばん下の行は**消しました**（Phase 261）

    Phase 1から「これから何が入るか」を薄く出していた行です。
    **設計書のスコープが全部入ったので、書くことがありません**——
    ディレイで8.222に書いたのと同じで、**空になった行は残さず片付けること**。
    残すと「まだ何か来る」と読めます。空いた26pxはディスプレイへ回しました。

    ### 色（8.253／Phase 261・本人の指定）

    `MantaReverbTheme`（＝`Branding.h`のリバーブ専用アクセント2色）。

    | | 主 | 副 |
    |---|---|---|
    | Manta Reverb | パープル | オレンジ（**アプリと同じ**） |
    | Bf Owl Reverb | **パープル** | **ブルー** |

    **主はどちらもパープル**です。`Hawkbill Delay`が**ブルー＋イエロー**なので、
    Ember側で並べたときに**主の色でどちらのプラグインか分かります**。

    | 弧の色 | どこ |
    |---|---|
    | 主 | Predelay・Decay・Size・Shape・Spread・Diffusion・Modulation——**空間そのもの** |
    | 副 | Early・Width・Damping・Level・Mix・Output・Saturation——**掛かり具合** |
*/
class MantaReverbEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit MantaReverbEditor (MantaReverbProcessor& processorToUse);
    ~MantaReverbEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    /** 8.249：**見た目だけ**（Phase 259）。繋ぎは`rebuildEngineAttachments()`。

        Phase 4までは、ここで`SliderAttachment`も作っていました——
        エンジンが1つなら、作ったら外さないので済みます。
        **2つになると繋ぎ直しが要る**ので、表（`engineKnobs`）へ分けました。 */
    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                     juce::Colour colour);

    void setupSectionLabel (juce::Label& label, const juce::String& text);

    /** 8.243：アルゴリズムで効かなくなるつまみをグレーアウト（Phase 255）。

        **判断は`MantaReverbAlgorithm::getCapabilities()`だけ**（1.27）——
        画面と音で別々に決めると、**触れるのに効かないつまみ**ができます
        （ディレイの`refreshCharacterControls()`と同じ形。8.208）。 */
    void refreshAlgorithmControls();

    /** グレーアウトの掛け方（ディレイの`setControlActive()`と同じ）。 */
    void setControlActive (juce::Component& control, juce::Label& caption, bool active);

    /** 8.249：選んでいるエンジンへ、**つまみを丸ごと繋ぎ直す**（Phase 259）。

        **表に出ているのは1エンジンぶんだけ**で、切り替えるのは人が押したときだけ——
        だから繋ぎ替えてよい、という判断です（ディレイの`rebuildEngineAttachments()`と
        同じ。8.217。8.207で「付け替えない」と書いた例外にあたります）。

        **いったん全部外してから作ること**（同じつまみに2本ぶら下がると、
        片方が前のエンジンへ書き戻します）。 */
    void rebuildEngineAttachments();

    /** エンジンの選び直し（押されたとき・開いたとき）。 */
    void setSelectedEngine (int engine);

    /** 8.249：ルーティングで効かなくなるものをグレーアウト（Phase 259）。 */
    void refreshRoutingControls();

    /** 下の3つの箱（Room・Damping・Output）。

        **`paint()`と`resized()`が同じものを見ます**（1.27）——
        別々に数えると、**枠と中身がずれます**（ディレイの`getPhase3Columns()`と同じ）。 */
    juce::Array<juce::Rectangle<int>> getControlColumns (juce::Rectangle<int> band) const;

    //==========================================================================
    /** **いちばん最初に宣言すること**（つまみより後に壊れるように。8.168）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaReverbProcessor& processor;

    MantaPluginToolbar toolbar;

    MantaReverbDisplay display;

    /** 8.243：アルゴリズムの選択（Phase 255）。

        Phase 1では`Room`しか無かったので文字だけでした——
        **1つしか無いものを選ばせない**（選べるように見えて選べないものは、
        壊れているように見えます）。 */
    juce::Label algorithmCaption;
    juce::ComboBox algorithmBox;

    /** 8.249：どちらのエンジンを映しているか（Phase 259）。 */
    juce::TextButton engineAButton, engineBButton;

    juce::Label routingCaption;
    juce::ComboBox routingBox;

    /** つまみが映しているエンジン（0＝A）。**`getUiState()`に残ります。** */
    int selectedEngine = 0;

    /** 選んでいるものの一行説明（`MantaReverbAlgorithm::getDescription()`）。

        **名前だけでは何が違うか分かりません**——「Plate」と書いてあっても、
        Roomとどう違うのかは読めない（ディレイのRoutingと同じ理由。8.217）。

        8.249：**置き場所を下の箱の見出しへ移しました**（Phase 259）——
        見出しの行にルーティングが入ったためです。
        **説明はそのつまみの隣**にあるほうが読まれます。 */
    juce::Label algorithmDescription;

    /** ルーティングの一行説明（`MantaReverbRouting::getModeDescription()`）。 */
    juce::Label routingDescription;

    juce::Label roomTitle, dampingTitle, outputTitle;

    ValueEntrySlider predelaySlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider decaySlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider sizeSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider diffusionSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider earlySlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider widthSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    ValueEntrySlider highFreqSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider highAmountSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider lowFreqSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider lowAmountSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    ValueEntrySlider mixSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outputSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    /** 8.249：そのエンジンの出口（Phase 259／仕様書5章の`Level`）。 */
    ValueEntrySlider levelSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label levelCaption;

    /** 8.250〜8.251：Phase 6（Phase 260）。 */
    ValueEntrySlider modulationSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider saturationSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label modulationCaption, saturationCaption;

    /** 8.246：初期反射の立ち上がり（Phase 256）。**`Size`の隣に置くこと**——
        3つとも「空間の寸法」を決めるもので、離すと関係が読めません。 */
    ValueEntrySlider shapeSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider spreadSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    /** 8.247：`Twin Delays`のつまみ（Phase 257）。

        **同じ場所に重ねて置いて、出し分けます**（ディレイの`timeSlider`／
        `divisionSlider`と同じ形。8.207）。グレーアウトではなく入れ替えなのは、
        **同じつまみが効かないのではなく、別のつまみだから**です。

        | 升目 | リバーブのとき | Twin Delaysのとき |
        |---|---|---|
        | 1段目の2 | `Decay` | `Time` |
        | 1段目の3 | `Size` | `Feedback` |
        | 2段目の1 | `Shape` | `Cross` |

        `Predelay`・`Spread`・`Diffusion`はどちらでも同じ場所に居ます
        （`Diffusion`だけ、Twin Delaysではグレーアウト）。 */
    ValueEntrySlider twinTimeSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider twinFeedbackSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider twinCrossSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

    juce::Label twinTimeCaption, twinFeedbackCaption, twinCrossCaption;

    /** 8.248：`Panorama`の3つ（Phase 258）。**つまみではなくボタン**——
        入るか入らないかしかないものに、つまみの見た目を与えないこと。

        置き場所は2段目（`Shape`・`Spread`・`Diffusion`の升目）。 */
    juce::TextButton monoSumButton, invertButton, swapButton;

    /** 1段目の空きに出す案内。**空きは黙って空けない**（8.222）——
        `Panorama`は操作子が4つしかないので、素のままだと大きな灰色の面が残ります。 */
    juce::Label panoramaHint;

    juce::Label predelayCaption, decayCaption, sizeCaption, diffusionCaption,
                earlyCaption, widthCaption,
                highFreqCaption, highAmountCaption, lowFreqCaption, lowAmountCaption,
                mixCaption, outputCaption, shapeCaption, spreadCaption;


    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** 8.249：**エンジンごとのつなぎ先の表**（Phase 259）。

        `rebuildEngineAttachments()`がこれを回します。
        **つまみを足したら、ここへ1行足すこと**——足し忘れると、
        そのつまみだけAに繋がったままBを映します。
        `setupKnob()`の呼び出しと同じ場所で登録するようにして、忘れにくくしてあります。 */
    struct EngineKnob { ValueEntrySlider* slider; const char* id; };
    struct EngineToggle { juce::TextButton* button; const char* id; };

    std::vector<EngineKnob> engineKnobs;
    std::vector<EngineToggle> engineToggles;

    /** エンジンごと。**A/Bを切り替えるたびに全部作り直します。**

        まとめて持っているのは、**外し忘れを作らないため**——
        1本ずつ`std::unique_ptr`で持つと、足したときにリセットを書き忘れます。 */
    juce::OwnedArray<SliderAttachment> engineSliderAttachments;
    juce::OwnedArray<ButtonAttachment> engineButtonAttachments;
    std::unique_ptr<ComboAttachment> algorithmAttachment;

    /** エンジン共通（`Mix`・`Output`・`Routing`）。**作ったら外しません。** */
    juce::OwnedArray<SliderAttachment> globalAttachments;
    std::unique_ptr<ComboAttachment> routingAttachment;

    //==========================================================================
    /** 画面の大きさ。**固定です**（8.172）。 */
    static constexpr int fixedWidth = 900;
    static constexpr int fixedHeight = 680;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    static constexpr int headerHeight = 26;
    // 8.253：**26px増えました**（Phase 261）。いちばん下の行を消したぶんです
    static constexpr int displayHeight = 386;
    static constexpr int controlAreaHeight = 206;

    static constexpr int knobWidth = 76;
    static constexpr int knobHeight = 84;
    static constexpr int sectionTitleHeight = 18;

    /** 下の3つの箱の幅。**足すと`getControlColumns()`の中身と合うこと。**

        8.246：Phase 3で`Shape`と`Spread`が増えたので配り直しました（Phase 256）。
        **どの箱も2段のまま**です——1つだけ3段にすると、
        他の箱の下半分が空きます（箱の高さは帯1本ぶんで揃っているため）。

        8.250：Phase 6で`Modulation`と`Saturation`が増えたので、**3つとも
        1段3つ**になりました（Phase 260）。**16升のうち15を使っています。**

        | 箱 | 中身 |
        |---|---|
        | 空間（見出しはアルゴリズム名） | Predelay Decay Size ／ Shape Spread Diffusion |
        | **Tail** | HF Freq HF Damp Modulation ／ LF Freq LF Damp |
        | Output | Early Width Level ／ Mix Output Saturation |

        `Early`が`Output`の箱に居るのは、**空間の箱が6つで埋まった**ためです。
        4つとも「どれだけ出すか」なので、置き場所としては外していません。

        2つめの箱の見出しを`Damping`から**`Tail`**へ変えました——
        `Modulation`が入って「減衰」だけの箱ではなくなったためです。
        **どちらもテールの形を決めるもの**なので、1語で言えます。 */
    // 8.250：Phase 6（Phase 260）。3つとも1段3つになったので、幅を揃えました
    static constexpr int roomColumnWidth = 284;
    static constexpr int dampingColumnWidth = 284;
    static constexpr int columnGap = 12;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaReverbEditor)
};
