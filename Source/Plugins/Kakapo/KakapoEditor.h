#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "KakapoProcessor.h"
#include "KakapoTheme.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <array>
#include <map>
#include <memory>

//==============================================================================
/**
    8.292：**画像の上に載るつまみ**（Phase 285）。色だけ`KakapoTheme`から引きます
    ——**画像は常に同じ明るさ**なので、ここだけテーマに従いません（8.256からの決まり）。
*/
class KakapoFieldLookAndFeel : public MantaKnobLookAndFeel
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                            float sliderPosProportional, float rotaryStartAngle,
                            float rotaryEndAngle, juce::Slider& slider) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;
};

//==============================================================================
/** 画像の上のつまみ1つ（見出し／つまみ／数値）。 */
class KakapoFieldKnob : public juce::Component
{
public:
    KakapoFieldKnob (const juce::String& name, juce::Colour arcColour);

    void resized() override;

    ValueEntrySlider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
    juce::Label label;

    static constexpr int labelHeight = 16;
    static constexpr int valueHeight = 18;
};

//==============================================================================
/**
    8.292：**12音の使用量**（Phase 285／仕様書5.2・7章）。

    ```
    PITCH CLASS
     ▇     ▇  ▇     ▇     ▇  ▇     ← 重み（長さも見た合計）
     C  C# D  D# E  F  F# G ...
    ```

    | 色 | 意味 |
    |---|---|
    | **バイオレット** | 優勢な候補のスケールに**入っている**音 |
    | 薄い墨 | **入っていない**音（＝点数を引いている音） |
    | **エメラルドグリーン** | 優勢な候補の**ルート** |

    **判定の中身がそのまま見えます。** 「なぜこのスケールなのか」は、
    候補の名前だけを出しても伝わりません——**引かれている音がどれか**が見えると、
    弾き方を変える手がかりになります。
*/
class KakapoPitchClassPanel : public juce::Component
{
public:
    void setState (const KakapoProcessor::AnalysisSnapshot& snapshot);

    void paint (juce::Graphics& g) override;

private:
    std::array<float, 12> histogram {};
    kakapo::ScaleCandidate candidate;
    bool hasEnoughNotes = false;
};

//==============================================================================
/**
    8.292：**スケールの候補2つ**（Phase 285／仕様書7章）。

    ```
    ┌──────────────────────────────┐
    │ MAJOR            FAVOURED    │
    │ C MAJOR              82%     │
    │ ████████████████▁▁▁▁▁▁       │
    ├──────────────────────────────┤
    │ MINOR                        │
    │ A MINOR              74%     │
    │ ██████████████▁▁▁▁▁▁▁▁       │
    └──────────────────────────────┘
    ```

    **優勢なほうに`FAVOURED`が付きます**（仕様書5.4のトーナルセンター。
    **候補そのものは変えていません**——強調だけです）。

    音が足りないときは`LISTENING...`とだけ出します（仕様書5.5）。
*/
class KakapoCandidatePanel : public juce::Component
{
public:
    void setState (const KakapoProcessor::AnalysisSnapshot& snapshot);

    void paint (juce::Graphics& g) override;

private:
    void drawCandidate (juce::Graphics& g, juce::Rectangle<int> area,
                         const kakapo::ScaleCandidate& scaleCandidate, bool favoured) const;

    kakapo::Result result;
    bool hasEnoughNotes = false;
};

//==============================================================================
/** 上の帯の小さな箱。**いちばん多く鳴っている音**を出します（トーナルセンター）。 */
class KakapoCentreBadge : public juce::Component
{
public:
    void setState (int pitchClass, bool majorFavoured, bool hasEnoughNotes);

    void paint (juce::Graphics& g) override;

private:
    int centre = -1;
    bool majorFavoured = true;
    bool enough = false;
};

//==============================================================================
/**
    鍵盤。**いま鳴っている音と、控えに残っている音を2段階で塗ります**（仕様書7章）。

    `MidiKeyboardComponent`は自分の`MidiKeyboardState`しか見ないので、
    **DAWのクリップから来た音は光りません**。ここは`AnalysisSnapshot`の
    2枚のマスクで塗るので、**どちらから来た音も同じように光ります**。
*/
class KakapoKeyboard : public juce::MidiKeyboardComponent
{
public:
    using juce::MidiKeyboardComponent::MidiKeyboardComponent;

    void setMasks (const std::array<bool, 128>& activeMask,
                    const std::array<bool, 128>& recentMask);

protected:
    void drawWhiteNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver,
                         juce::Colour lineColour, juce::Colour textColour) override;

    void drawBlackNote (int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                         bool isDown, bool isOver, juce::Colour noteFillColour) override;

private:
    juce::Colour overlayFor (int midiNoteNumber) const;

    std::array<bool, 128> active {};
    std::array<bool, 128> recent {};
};

//==============================================================================
/**
    8.292：**Kakapo の画面**（Phase 285／本人の仕様書7章・設計書16章）。

    **並びは`Racco Guitar`と同じ**です（本人の指定）。1200×790。

    ```
    ┌────────────────────────────────────────────────────────────┐
    │ Undo Redo │ A Copy │ Presets                        Kakapo │
    ├────────────────────────────────────────────────────────────┤
    │ KAKAPO      [ 案内の帯 ]        [CENTRE] OUTPUT ──── 0.70  │
    ├────────────────────────────────────────────────────────────┤
    │ A SCALE SUGGESTER                            [MIDI THRU]   │
    │ ┌─ DETECTION ───────────┐ ┌─ STATUS ────────────────────┐  │
    │ │ 24 CANDIDATES / ...   │ │ C IS THE STRONGEST NOTE ... │  │
    │ └───────────────────────┘ └─────────────────────────────┘  │
    ├────────────────────────────────────────────────────────────┤
    │ ┌ PITCH CLASS ──┐  HOLD     ┌ SCALE ───────────────────┐   │
    │ │ ▇ ▇ ▇ ▇ ▇ ▇  │   ◯       │ C MAJOR    82%           │   │
    │ │ C C# D D# E F │ [N][S]    │ A MINOR    74%           │   │
    │ └───────────────┘ [RESET]   └──────────────────────────┘   │
    ├────────────────────────────────────────────────────────────┤
    │ 鍵盤（C2〜C6。鳴っている音と、控えに残っている音）          │
    └────────────────────────────────────────────────────────────┘
    ```

    ### なぜ`Racco Guitar`の形で足りたのか

    本人の指定は「`Racco Guitar`と同じ。**余白が多くなりそうなら`Orangutan Drums`の形**」。

    つまみは1つ（`HOLD`）しかありませんが、**絵の上に置くものが2枚の板**
    （12音の使用量と、候補2つ）あります。**1168×396は、ちょうど埋まりました。**

    > つまみの数で決めないこと。**何を見せるか**で決まります。

    ### 板の上に載せる理由

    絵が暗い（平均79）ので、**文字を直に載せると読めません**。
    `Orangutan Drums`は絵を起こしましたが、ここは**苔の粒を残したい**ので、
    薄い板（`KakapoTheme::fieldPanel()`）を敷いてその上に載せています（8.290の続き）。
*/
class KakapoEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit KakapoEditor (KakapoProcessor& processorToUse);
    ~KakapoEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    void registerHint (juce::Component& component, const juce::String& text);
    void showHint (const juce::String& text);

    void setHoldMode (int mode);
    void updateHoldSuffix();

    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawBox (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title) const;
    void drawField (juce::Graphics& g) const;
    void drawWordmark (juce::Graphics& g) const;

    static void drawTracked (juce::Graphics& g, const juce::String& text,
                              juce::Point<float> origin, const juce::Font& font, float tracking);

    /** 右の箱に出る1行（**いま何を根拠にしているか**）。 */
    juce::String statusText() const;

    //==========================================================================
    KakapoProcessor& processor;

    juce::SharedResourcePointer<KakapoFieldLookAndFeel> fieldLookAndFeel;

    MantaPluginToolbar toolbar;
    juce::Label titleLabel;

    juce::Image fieldImage;

    juce::Label hintLabel;
    std::map<juce::Component*, juce::String> hints;
    juce::String defaultHint;

    KakapoCentreBadge badge;
    KakapoPitchClassPanel pitchClassPanel;
    KakapoCandidatePanel candidatePanel;

    ValueEntrySlider outputSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label outputValue;

    std::unique_ptr<KakapoFieldKnob> holdKnob;

    std::array<juce::TextButton, 2> holdModeButtons;
    juce::TextButton resetButton, thruButton;

    KakapoKeyboard keyboard;

    //==========================================================================
    KakapoProcessor::AnalysisSnapshot lastSnapshot;
    int lastHoldMode = -1;
    int lastNoteCount = -1;

    //==========================================================================
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** **部品より後ろに置くこと**（1.5）。 */
    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;

    std::vector<juce::Slider*> styledSliders;
    std::vector<juce::Component*> styledComponents;

    //==========================================================================
    // 寸法。**`Racco Guitar`と同じ骨組み**（8.256）

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
    static constexpr int outputLabelX  = 840;
    static constexpr int outputSliderX = 898;
    static constexpr int outputSliderW = 130;
    static constexpr int outputValueX  = 1034;
    static constexpr int notesX = 1086;

    // 2つめの帯
    static constexpr int rowY = 144;
    static constexpr int rowH = 28;

    static constexpr int thruX = 1044;
    static constexpr int thruW = 124;

    static constexpr int boxY      = 180;
    static constexpr int boxH      = 62;
    static constexpr int leftBoxX  = 32;
    static constexpr int leftBoxW  = 540;
    // 8.293：**右端は左と同じだけ空けること**（Phase 286／本人の指摘）。
    // 帯の内側は左が16pxなのに、右は0でした——**箱もボタンも枠に張り付いて**見えます。
    // 右端を1168（＝1184-16）に揃えてあります
    static constexpr int rightBoxX = 588;
    static constexpr int rightBoxW = 580;

    // 絵の上の3つ（板・つまみの列・板）
    static constexpr int panelY = 302;
    static constexpr int panelH = 334;

    static constexpr int pitchPanelX = 40;
    static constexpr int pitchPanelW = 430;

    static constexpr int columnX = 492;
    static constexpr int columnW = 156;

    static constexpr int candidatePanelX = 672;
    static constexpr int candidatePanelW = 488;

    static constexpr int holdKnobW = 130;
    static constexpr int holdKnobH = 140;
    static constexpr int holdKnobY = 336;

    static constexpr int modeButtonY = 506;
    static constexpr int modeButtonH = 26;
    static constexpr int modeButtonW = 74;
    static constexpr int modeButtonGap = 6;

    static constexpr int resetButtonY = 548;
    static constexpr int resetButtonH = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KakapoEditor)
};
