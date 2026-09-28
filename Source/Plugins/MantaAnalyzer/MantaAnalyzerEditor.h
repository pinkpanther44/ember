#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MantaAnalyzerProcessor.h"
#include "AnalyzerView.h"
#include "SpectrumAnalyzer.h"
#include "TargetRange.h"
#include "CurveBuilder.h"
#include "../../AppColours.h"

//==============================================================================
/**
    8.329：Manta Analyzer／Ember Analyzer の画面（アナライザー仕様書3章・設計書6章）。

    ### フレームごとの流れ（設計書6.1。**全部この窓のタイマーの中**）

    1. `SpectrumAnalyzer::process()` … FIFOを空になるまで読む
    2. `PeakHold::update()`
    3. `TargetRange::update()`
    4. `CurveBuilder::build()` … 平均・ピーク・（あれば）リアルタイムの3本
    5. `AnalyzerView`を描き直す

    **解析と描画が同じスレッド**なので、受け渡しに鍵は要りません（設計書2章）。
    描き直しに時間がかかりすぎたら（直近の平均が12 msを超えたら）30 Hzへ落とし、
    6 msを下回ったら60 Hzへ戻します。

    ### 窓の大きさは固定（8.330／本人の指定）

    Phase 319では仕様書3.1のとおり伸縮できるようにしていましたが、**ほかの内蔵プラグインと同じく
    固定**にしました（9.5）。900×500。大きさは保存しません。

    ### 製品名は出さない

    仕様書3.6（DAWの標準機能にするかもしれないため）。名前が出るのは窓のタイトルだけです。
*/
class MantaAnalyzerEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit MantaAnalyzerEditor (MantaAnalyzerProcessor& processorToUse);
    ~MantaAnalyzerEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 設定を変える（フッター・設定のポップオーバー・縦軸の操作が、みなここを通る。1.27）。 */
    void applySettings (const AnalyzerSettings& newSettings);

    const AnalyzerSettings& getSettings() const noexcept { return settings; }

    //--------------------------------------------------------------------------
    // 自己検査・プレビューのため

    /** タイマーを待たずに1回ぶん回す。 */
    void tickForTesting() { timerCallback(); }

    AnalyzerView& getViewForTesting() noexcept { return view; }

    /** 設定のポップオーバーの中身（絵にして並びを見るため）。 */
    static std::unique_ptr<juce::Component> createSettingsPanel (const AnalyzerSettings& initial, bool monoInput,
                                                                 std::function<void (const AnalyzerSettings&)> changed);
    const AnalyzerFrame& getFrameForTesting() const noexcept { return frame; }
    float getReferenceDbForTesting() const { return analyzer.getReferenceBandDb(); }

    static constexpr int footerHeight = 34;
    static constexpr int fixedWidth = 900;
    static constexpr int fixedHeight = 500;

    /** 8.330：**ラウドネス曲線の範囲（Range）を出すか**（Phase 320／本人の指定で試しに廃止）。

        本人の言葉は「イメージした曲線と異なるので、廃止にしてみてほしい」。
        **コードは残してあります**——`true`に戻せば、フッターのphon・Rangeボタン・
        設定の「Range position」など・グラフの帯と境界線・カーソルの「Upper / Lower」が全部戻ります。
        `TargetRange`と`Iso226`の自己検査もそのまま走っています（壊れていないことを保つため）。

        **戻す予定が無いと決まったら**、この旗と`TargetRange`まわりを消して構いません。 */
    static constexpr bool showsTargetRange = false;

private:
    void timerCallback() override;

    void rebuildPipelineIfNeeded();
    void refreshFooter();

    /** 描くときに使う設定（旗でレンジを消したもの）。 */
    AnalyzerSettings getDisplaySettings() const;
    void applyFooterColours();
    void showSettingsPanel();
    void resetPeaks();

    MantaAnalyzerProcessor& processor;
    AnalyzerSettings settings;

    SpectrumAnalyzer analyzer;
    PeakHold peaks;
    TargetRange range;
    CurveBuilder curves;
    AnalyzerFrame frame;

    AnalyzerView view;

    juce::TextButton freezeButton { "Freeze" };
    juce::TextButton resetPeakButton { "Reset Peak" };
    juce::Slider lowPhonSlider, highPhonSlider;
    juce::TextButton curveButton { "Curve" }, peakButton { "Peak" }, rangeButton { "Range" };
    juce::TextButton settingsButton;

    bool frozen = false;
    juce::Colour lastAccent;
    AppColours::Theme lastTheme = AppColours::Theme::Dark;

    double lastTickMs = 0.0;
    double paintAverageMs = 0.0;
    int currentRate = 60;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaAnalyzerEditor)
};
