#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CompressorDisplay.h"
#include "MantaCompProcessor.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../../ValueEntrySlider.h"

//==============================================================================
/**
    設計書4-1のレイアウト（Studio One付属Compressorの①〜⑥）。

    ```
    ┌──────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets      SC · 遅れ   │ ← 共有ツールバー
    ├────────┬─────────────────────────────┬──────┤
    │ ①      │ ② 伝達特性＋レベル履歴        │In Out│
    │ Thresh │   （1枚に重ねてある。         │  GR  │
    │ Ratio  │    Pro-C 2と同じ形。          │      │
    │ Knee   │    `CompressorDisplay.h`）    │      │
    │ GR値   │                             │      │
    ├──────┬─┴─────────────────────┬───────┴──────┤
    │③ Env │ ④ Gain        │ ⑤ Sidechain   │⑥ Mix │
    └──────┴───────────────┴───────────────┴───────┘
    ```

    ### ①は縦積み（Phase 209）

    設計書4-1の「左上：ノブ3つ縦並び」のとおりです。横に並べると、
    パネルの幅を200px取ったうえで**下が大きく余りました**——
    縦にすると幅は140pxで足り、余ったぶんはグラフへ回せます。

    空いた下には、仕様書2-2の**最重要メーター**であるゲインリダクションの数値を
    大きく置いてあります。

    ### つまみの大きさは、どこでも同じ

    `stackedKnobHeight`を**縦積みにも下の帯にも使っています**。
    帯ごとに「残り全部」を渡すと、**高さが違うだけで丸の大きさが変わり**、
    同じ役割のつまみが場所によって違って見えます。

    ### 色はManta EQと同じ

    `MantaTheme`（＝本体の`AppColours`）をそのまま引いています。
    **パープルが主、オレンジは「いま効いている」もの**——
    ゲインリダクション、0dBを超えたメーター、Listen中の表示。
    Manta EQで「オレンジ＝掛かっているぶん」にしてあるのと同じ役割です。

    ### つまみの弧の色

    - ① Threshold/Ratio/Knee … **オレンジ**（圧縮の効きを決めるところ）
    - ③〜⑥ … パープル

    どこを触ると音が潰れるのかが、色で分かれます。
*/
class MantaCompEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit MantaCompEditor (MantaCompProcessor& processorToUse);
    ~MantaCompEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    /** つまみ1つ分を作る。`colour`は弧と指針の色。 */
    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text,
                     const char* parameterId, juce::Colour colour, bool bipolar = false);

    void setupButton (juce::TextButton& button, const juce::String& text, const char* parameterId);

    /** 見出し（①〜⑥のブロック名）。 */
    void setupSectionLabel (juce::Label& label, const juce::String& text);

    /** Autoで動いているAttack/Releaseや、サイドチェインの状態を出し直す。 */
    void refreshReadouts();

    //==========================================================================
    /** **いちばん最初に宣言すること**（つまみより後に壊れるように。8.168）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaCompProcessor& processor;

    MantaPluginToolbar toolbar;
    juce::Label statusLabel;          ///< ツールバーの右（サイドチェインと遅れ）

    CompressorDisplay display;

    // ① Threshold / Ratio / Knee
    juce::Label compressionTitle;
    ValueEntrySlider thresholdSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider ratioSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider kneeSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label thresholdCaption, ratioCaption, kneeCaption;

    /** 仕様書2-2の**最重要メーター**の数値。①の下の空きへ大きく出す
        （メーターの細い棒だけでは、何dB削っているかが読めません）。 */
    juce::Label reductionValue, reductionCaption;

    // ③ Envelope
    juce::Label envelopeTitle;
    ValueEntrySlider attackSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider releaseSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label attackCaption, releaseCaption;
    juce::TextButton autoEnvelopeButton, adaptiveButton;

    // ④ Gain
    juce::Label gainTitle;
    ValueEntrySlider inputGainSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider makeupSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label inputGainCaption, makeupCaption;
    juce::TextButton autoGainButton, lookAheadButton, stereoLinkButton;

    // ⑤ Sidechain
    juce::Label sidechainTitle;
    ValueEntrySlider lowCutSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider highCutSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label lowCutCaption, highCutCaption;
    juce::TextButton filterButton, listenButton, swapButton;

    // ⑥ Global
    juce::Label globalTitle;
    ValueEntrySlider mixSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label mixCaption;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;

    /** 画面の大きさ。**固定です**（Phase 212）。

        伸縮をやめたので、覚えて戻す仕組み（`layoutReady`と
        `editorWidth`/`editorHeight`）も一緒に外してあります——
        大きさが1つしか無いのに保存すると、
        **寸法を変えたときに古い値のまま開く**という壊れ方をします。 */
    static constexpr int fixedWidth = 780;
    static constexpr int fixedHeight = 500;

    // 並びの寸法。**`paint()`と`resized()`の両方が見る**ので、数字を直に書かない（1.27）
    static constexpr int bottomSectionHeight = 110;
    static constexpr int compressionPanelWidth = 140;
    static constexpr int knobWidth = 62;

    /** つまみ1つぶんの高さ（見出し12＋つまみ＋数値欄15）。

        **縦積みにも下の帯にも、これを使うこと。** 帯ごとに「残り全部」を渡すと、
        高さが違うだけで丸の大きさが変わります。 */
    static constexpr int stackedKnobHeight = 82;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaCompEditor)
};
