#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaShifterProcessor.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <vector>

//==============================================================================
/**
    8.336：色（本人の指定「各DAWのメインカラー、副カラーをアクセントに」）。

    **プラグイン側に色の固定値は持ちません。** メイン＝本体のパープル（`MantaTheme::accent()`）、
    副＝オレンジ（`MantaTheme::curve()`）。リミッター（8.333）と同じ作りで、明度は OKLCH で動かします。

    | トークン | 何に |
    |---|---|
    | main | つまみの弧、選んでいるモード、**出ていく音高**の線 |
    | sub | オンのスイッチ、**検出した音高**の線、MIDIの音 |
    | mainWeak／subWeak | 線の下の薄い帯、スケール音の段 |
*/
struct ShifterTheme
{
    juce::Colour main, mainStrong, mainWeak, onMain;
    juce::Colour sub, subStrong, subWeak, onSub;
    juce::Colour background, panel, text, textDim, grid;

    static ShifterTheme fromDaw();
};

//==============================================================================
/**
    8.336：検出したピッチの表示（仕様書5章「UI：検出ピッチ表示」）。

    上に**いまの音名・セント・Hz**、下に**約5秒の軌跡**（検出＝副の色、出ていく音高＝メインの色）。
    縦は音高（半音）で、**いまの声の上下1オクターブ**。幹音に名前を書き、Quantize ではスケールの音の段を薄く塗ります。
*/
class ShifterPitchView : public juce::Component
{
public:
    void setTheme (const ShifterTheme& newTheme) { theme = newTheme; repaint(); }
    void setScale (int newMode, int newKey, int newScale);
    void addFrame (const ShifterEngine::DisplayFrame& frame);

    void paint (juce::Graphics& g) override;

    static constexpr int historyLength = 940;   ///< 約5秒（5.3 ms ごと）

    /** 8.343：MIDI モードのとき表示の下端に出す案内。 */
    static constexpr const char* midiSourceHint = "MIDI source: right-click this insert slot > MIDI Input";

private:
    ShifterTheme theme;
    std::vector<ShifterEngine::DisplayFrame> history = std::vector<ShifterEngine::DisplayFrame> ((size_t) historyLength);
    int writePos = 0, count = 0;
    ShifterEngine::DisplayFrame latest;
    int mode = 0, key = 0, scale = 0;

    /** 縦の中心（半音）。**表示は中心の上下1オクターブずつ**——広く取ると、ビブラートや補正の幅（数十セント）が
        数ピクセルになって2本の線が重なって見えた（8.336）。声が端の4半音に入ったら中心を寄せ直す。 */
    float centre = 60.0f;
    bool centred = false;

public:
    static constexpr float halfRange = 12.0f;
};

//==============================================================================
/**
    8.339：エンジンのトグルスイッチ（本人の指定：「1(PSOLA)⇔2(Spectral)のように」）。

    ```
    1 (●━━) 2        左＝1＝PSOLA、右＝2＝Spectral。クリックかスペースで切り替える（8.340：名前は出さずに数字だけ。本人の指定）
    ```

    `juce::Button`なので、フォーカス・キーボード・読み上げはほかのボタンと同じ。
    **トグルの状態が真＝Spectral**（右）。選んでいる側の文字はメインの色、もう片方は薄く。
*/
class ShifterEngineSwitch : public juce::Button
{
public:
    ShifterEngineSwitch() : juce::Button ("Engine") { setClickingTogglesState (true); }

    void setTheme (const ShifterTheme& newTheme) { theme = newTheme; repaint(); }

    /** `PitchEngineType`（0 Spectral／1 PSOLA）。 */
    int getEngineType() const { return getToggleState() ? 0 : 1; }
    void setEngineType (int type) { setToggleState (type == 0, juce::dontSendNotification); }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override;

    static constexpr int trackWidth = 38, trackHeight = 18;

private:
    ShifterTheme theme;
};

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice の画面（仕様書5章「ノブ2つ＋モード選択＋検出ピッチ表示」）。

    ```
    ┌ Undo Redo │ A Copy │ ◀ Presets ▶ ──────────────────────────── Latency ┐
    │  ( Pitch )  [Link]  ( Formant )   │  C4  +12 ct  261.6 Hz           │
    │                                   │  ～～～ 軌跡（5秒） ～～～        │
    │ [Transpose][Quantize][Robot][MIDI]│                                  │
    ├ Key Scale Retune MIDI Hold │ Drive(On) Mix Output Bypass ────────────┤
    └─────────────────────────────────────────────────────────────────────┘
    ```

    **大きさは1つ（固定）**。9.5「内蔵プラグインは伸縮させない」。
*/
class MantaShifterEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit MantaShifterEditor (MantaShifterProcessor& processorToUse);
    ~MantaShifterEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static constexpr int fixedWidth = 780;
    static constexpr int fixedHeight = 470;

    /** 8.342：ツールバーの右に置く組（エンジン＝見出し＋スイッチ、レイテンシー）の幅。 */
    static constexpr int engineCaptionWidth = 46;
    static constexpr int engineSwitchWidth = 86;
    static constexpr int engineGroupWidth = engineCaptionWidth + engineSwitchWidth;
    static constexpr int statusWidth = 124;

    /** 8.342：下の帯の高さ（地と中身で同じ矩形を使う）と、帯の内側の余白。 */
    static constexpr int bandHeight = 120;
    static constexpr int bandPaddingX = 10;
    static constexpr int bandPaddingY = 8;

    /** プレビューと自己検査のため。 */
    void tickForTesting() { timerCallback(); }
    ShifterEngineSwitch& getEngineSwitchForTesting() noexcept { return engineSwitch; }
    ValueEntrySlider& getPitchSliderForTesting() noexcept { return pitchSlider; }
    ValueEntrySlider& getFormantSliderForTesting() noexcept { return formantSlider; }
    juce::TextButton& getLinkButtonForTesting() noexcept { return linkButton; }
    juce::Label& getEngineCaptionForTesting() noexcept { return engineCaption; }
    ShifterPitchView& getPitchViewForTesting() noexcept { return pitchView; }
    juce::Rectangle<int> getBandAreaForTesting() const noexcept { return bandArea; }

private:
    void timerCallback() override;

    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text, const char* parameterId);
    void setupSwitch (juce::TextButton& button, const juce::String& text, const char* parameterId);
    void setupCombo (juce::ComboBox& box, juce::Label& caption, const juce::String& text,
                     const juce::StringArray& items, const char* parameterId);
    void applyTheme();
    void refreshControls();

    /** **いちばん最初に宣言すること**（つまみより後に壊れるように。8.168）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaShifterProcessor& processor;
    MantaPluginToolbar toolbar;
    juce::Label statusLabel;

    ShifterPitchView pitchView;

    ValueEntrySlider pitchSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider formantSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider retuneSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider driveSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider mixSlider     { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outputSlider  { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label pitchCaption, formantCaption, retuneCaption, driveCaption, mixCaption, outputCaption;

    juce::TextButton modeButtons[4];
    ShifterEngineSwitch engineSwitch;   // 8.339：1 PSOLA ⇔ 2 Spectral（8.337 は押しボタン2つだった）
    juce::Label engineCaption;
    juce::TextButton linkButton, driveOnButton, bypassButton, midiHoldButton;

    // 8.341：Link＝つまみの連動。手で動かしているあいだだけ、もう片方を同じだけ動かす
    struct LinkState
    {
        double last = 0.0;        ///< 前の値（動いた量を出すため）
        bool userEditing = false; ///< `onDragStart`〜`onDragEnd`
    };

    LinkState pitchLink, formantLink;
    bool coupling = false;   ///< 連動で動かしている最中（もう片方から跳ね返らないように）

    juce::Rectangle<int> bandArea;   ///< 8.342：下の帯（`resized()`で決め、`paint()`も同じものを塗る）

    juce::ComboBox keyBox, scaleBox;
    juce::Label keyCaption, scaleCaption;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;
    juce::OwnedArray<ComboBoxAttachment> comboAttachments;

    ShifterTheme theme;
    juce::Colour lastMain, lastSub;
    AppColours::Theme lastDawTheme = AppColours::Theme::Dark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaShifterEditor)
};
