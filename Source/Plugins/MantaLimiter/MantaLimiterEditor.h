#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaLimiterProcessor.h"
#include "../MantaKnobLookAndFeel.h"
#include "../MantaPluginToolbar.h"
#include "../MantaTheme.h"
#include "../../ValueEntrySlider.h"

#include <deque>

//==============================================================================
/**
    8.333：色のトークン（リミッター仕様書「カラートークン」・設計書「トークン生成の手順」）。

    **プラグイン側に色の固定値は持ちません**（警告色だけは意味を持つので例外）。
    メイン＝本体のパープル（`MantaTheme::accent()`）、副＝オレンジ（`MantaTheme::curve()`）。
    明度はOKLCHで動かします（`AnalyzerThemeColors`の変換を借りる。どの色相でも「+0.1」が同じ見た目の変化になる）。

    | トークン | 作り方 | 何に |
    |---|---|---|
    | main | メインそのもの | GR曲線、つまみの弧、選んでいるスタイル |
    | mainStrong | 明度 +0.10 | ホバー・ドラッグ中 |
    | mainWeak | 不透明度30% | GRの塗り |
    | sub | 副そのもの | ラウドネスのバー、目標ライン、オンのスイッチ |
    | subStrong | 明度 +0.10 | 目標との差が ±1 LU 以内 |
    | subWeak | 不透明度30% | ラウドネスの背景帯 |
    | waveIn／waveOut | 文字色 25%／55% | 入力・出力の波形 |
    | warn | 赤。メインか副の色相が赤±30°なら黄 | クリップ、天井超過 |

    メインと副の色相が30°未満なら、副の明度をメインから離れる向きに0.20ずらします。
    アクセントの上の文字は、白と黒のうちコントラスト比4.5:1以上のほう。
*/
struct LimiterTheme
{
    juce::Colour main, mainStrong, mainWeak, onMain;
    juce::Colour sub, subStrong, subWeak, onSub;
    juce::Colour waveIn, waveOut, warn;
    juce::Colour background, panel, text, textDim, grid;

    static LimiterTheme fromDaw();

    /** WCAGの相対輝度でのコントラスト比。 */
    static double contrastRatio (juce::Colour a, juce::Colour b);
};

//==============================================================================
/**
    8.333：スクロール表示（仕様書「入力波形・出力波形・GRの3層を右から左へ」。ライト版）。

    列（`LimiterMeters::Column`）を輪で持ち、新しい列を右端に足して全体を左へ送ります。
    **1列＝表示時間 × レート ÷ 幅（px）サンプル**の間引いたピーク（設計書`DisplayDecimator`）。

    **目盛りは dB**（上端 0 dB）。入力（waveIn）と出力（waveOut）は下から、GRは上から下向き（main の線と
    mainWeak の塗り）、天井は破線（sub）。**3つが同じ目盛り**なので、入力の高さ − GR ≒ 出力の高さ。
*/
class LimiterScrollDisplay : public juce::Component
{
public:
    void setTheme (const LimiterTheme& newTheme) { theme = newTheme; repaint(); }
    void setCeilingDb (float db) { ceilingDb = db; }

    /** 表示時間（秒）。5 か 10。 */
    void setSeconds (int seconds) { displaySeconds = seconds; }
    int getSeconds() const noexcept { return displaySeconds; }

    void addColumn (const LimiterMeters::Column& column);

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 目盛りの下端（dB）。上端は 0 dB。入力・出力・GRが同じ目盛り。 */
    static constexpr float rangeDb = 36.0f;

private:
    LimiterTheme theme;
    std::vector<LimiterMeters::Column> ring;
    int writePos = 0;
    float ceilingDb = -1.0f;
    int displaySeconds = 5;
};

//==============================================================================
/**
    8.333：メーター列（仕様書「メーター・ラウドネス仕様」）。

    入力ピーク（L／R、2秒保持）、GR（瞬時値＋区間最大）、出力ピークとTP（L／R）、
    ラウドネス（M・S のバー、I・LRA・PLR の数値）。**目標**は S／M のバーの上に破線で描き、
    Integrated との差を LU で出します（±1 LU以内は subStrong）。**TPの最大値とクリックはクリックでリセット**。
*/
class LimiterMeterView : public juce::Component
{
public:
    void setTheme (const LimiterTheme& newTheme) { theme = newTheme; repaint(); }
    void setFrame (const LimiterMeters::Frame& newFrame, float ceilingDb, float target);

    /** 数値の欄をクリックしたとき（計測のやり直し）。 */
    std::function<void()> onReset;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    /** 棒の目盛り（dB）。上端 0、下端 −48。ラウドネスは −48〜0 LUFS を同じ高さに。 */
    static constexpr float floorDb = -48.0f;

private:
    LimiterTheme theme;
    LimiterMeters::Frame frame;
    float ceiling = -1.0f, loudnessTarget = 0.0f;

    // 入力ピークの保持（2秒）
    float heldInput[2] { -150.0f, -150.0f };
    double heldSince[2] { 0.0, 0.0 };

    juce::Rectangle<int> readoutArea;
};

//==============================================================================
/**
    8.333：Manta Limiter／Af Elephant Limiter の画面（リミッター仕様書「UI・カラー仕様」・設計書「UI・テーマ設計」）。

    ```
    ┌ Undo Redo │ A Copy │ Presets ─────────────────────────── 遅れ ┐ ← 共有ツールバー
    ├ スクロール表示（入力・出力・GR・天井）              │ メーター     ┤
    │                                                  │ In GR Out M S │
    │                                                  │ I LRA PLR TP  │
    ├ Style [Transparent][Punchy][Aggressive][Safe]  [Auto Release][True Peak]…[DC Filter] ┤
    ├ Gain Output Lookahead Attack Release Link │ Oversampling／Dither／Noise Shaping／目標 ┤
    └──────────────────────────────────────────────────────────────┘
    ```

    ### 大きさは1つ（固定）

    本人の指定（ほかの内蔵プラグインと同じ。9.5）。仕様書は「小・中・大の3段階」でしたが1つにしています。
    最初は中（960×540）に近い 960×560。**8.334で 800×560 に詰めました**（本人の要望：右に余りがあった）。
    選択欄をつまみのすぐ右へ寄せ、ボタンを文字に合わせた幅にし、メーターの列を 250 → 200 px に。

    描画は30 fps（設計書）。
*/
class MantaLimiterEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit MantaLimiterEditor (MantaLimiterProcessor& processorToUse);
    ~MantaLimiterEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 8.334：960 → 800（本人の要望。右の余りを詰めた）。
        幅の内訳：下の帯 = つまみ 6 × knobWidth ＋ 16 ＋ 選択欄 comboColumnWidth ＋ 左右の余白 36。 */
    static constexpr int fixedWidth = 800;
    static constexpr int fixedHeight = 560;

    static constexpr int knobWidth = 84;
    static constexpr int comboColumnWidth = 236;
    static constexpr int meterWidth = 200;

    /** 8.342：下の帯の高さ（地と中身で同じ矩形を使う）と、帯の内側の余白。
        中身の高さ 176 = ボタンの列 28 ＋ 8 ＋ 選択欄 5 行 × 28。 */
    static constexpr int bandHeight = 192;
    static constexpr int bandPaddingX = 10;
    static constexpr int bandPaddingY = 8;

    /** プレビューと自己検査のため。 */
    void tickForTesting() { timerCallback(); }
    LimiterScrollDisplay& getScrollDisplayForTesting() noexcept { return scroll; }
    juce::Rectangle<int> getBandAreaForTesting() const noexcept { return bandArea; }

private:
    void timerCallback() override;

    void setupKnob (ValueEntrySlider& slider, juce::Label& caption, const juce::String& text, const char* parameterId);
    void setupSwitch (juce::TextButton& button, const juce::String& text, const char* parameterId);
    void setupCombo (juce::ComboBox& box, juce::Label& caption, const juce::String& text,
                     const juce::StringArray& items, const char* parameterId);
    void applyTheme();
    void refreshControls();
    void refreshTargetBox();

    /** **いちばん最初に宣言すること**（つまみより後に壊れるように。8.168）。 */
    juce::SharedResourcePointer<MantaKnobLookAndFeel> knobLookAndFeel;

    MantaLimiterProcessor& processor;
    MantaPluginToolbar toolbar;
    juce::Label statusLabel;

    LimiterScrollDisplay scroll;
    LimiterMeterView meterView;
    juce::TextButton secondsButton, resetButton, autoResetButton;

    juce::Label styleTitle;
    juce::TextButton styleButtons[4];

    juce::TextButton autoReleaseButton, truePeakButton, unityButton, auditionButton, dcButton;

    ValueEntrySlider gainSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider outputSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider lookaheadSlider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider attackSlider    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider releaseSlider   { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    ValueEntrySlider linkSlider      { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label gainCaption, outputCaption, lookaheadCaption, attackCaption, releaseCaption, linkCaption;

    juce::ComboBox oversamplingBox, ditherBox, noiseShapingBox, targetBox;
    juce::Label oversamplingCaption, ditherCaption, noiseShapingCaption, targetCaption;
    juce::Slider customTargetSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    juce::OwnedArray<SliderAttachment> sliderAttachments;
    juce::OwnedArray<ButtonAttachment> buttonAttachments;
    juce::OwnedArray<ComboBoxAttachment> comboAttachments;

    LimiterTheme theme;
    juce::Colour lastMain, lastSub;
    AppColours::Theme lastDawTheme = AppColours::Theme::Dark;

    LimiterMeters::Frame latestFrame;

    juce::Rectangle<int> bandArea;   ///< 8.342：下の帯（`resized()`で決め、`paint()`も同じものを塗る）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaLimiterEditor)
};
