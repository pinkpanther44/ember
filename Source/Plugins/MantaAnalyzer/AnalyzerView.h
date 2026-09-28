#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AnalyzerSettings.h"
#include "AnalyzerThemeColors.h"

#include <functional>
#include <vector>

//==============================================================================
/**
    8.329：描くもの一式（1ピクセル1点。`MantaAnalyzerEditor`が毎フレーム作る）。
*/
struct AnalyzerFrame
{
    std::vector<float> average;     ///< スペクトラム曲線（dB）
    std::vector<float> peak;        ///< ピーク曲線
    std::vector<float> realtime;    ///< リアルタイム曲線
    std::vector<float> lower;       ///< レンジの下限（dB。スロープ・位置込み）
    std::vector<float> upper;       ///< 上限
    std::vector<char> outside;      ///< 規格の範囲の外（仕様書5.4）

    double frequencyMin = 20.0;
    double frequencyMax = 20000.0;
    bool hasData = false;
};

//==============================================================================
/**
    8.329：グラフの部分（アナライザー設計書6.4・6.6の`AnalyzerView`）。

    ### 描く順（仕様書3.3。奥 → 手前）

    1. 背景（ほぼ黒）　2. グリッド　3. レンジ帯（半透明）　4. スペクトラムの塗り
    5. 逸脱の強調　6. レンジの境界線　7. スペクトラム曲線　8. ピーク曲線　9. カーソル読み取り

    ### 塗りは画像へ、線はパスで

    塗りの色は**列ごと・高さごと**に変わります（下限より下は暗く、レンジ内は下から上へ明るく、
    上限を超えたぶんはさらに明るく）。これを縦のグラデーションで列ごとに描くと、
    1フレームで**数千回**の塗りになります。そこで塗り（4・5）は**画面と同じ大きさの画像へ
    1画素ずつ書き**、線（6〜8）は**アンチエイリアス付きのパス**で上から描きます。
    塗りの上端は曲線と同じ位置なので、画像の縁は線の下に隠れます。

    ### 不足は「穴」

    曲線が下限より下にあるとき、曲線から下限線までは**何も塗りません**（仕様書3.4）。
    背景がそのまま見えて、「足りない量」が穴として分かります。
*/
class AnalyzerView : public juce::Component
{
public:
    AnalyzerView();

    void setSettings (const AnalyzerSettings& newSettings);
    void setColours (const AnalyzerThemeColors& newColours);

    /** 描く中身を受け取る（**呼んだあとで`repaint()`**）。 */
    void setFrame (const AnalyzerFrame& newFrame) { frame = &newFrame; }

    /** 曲線を置く四角（右の目盛りと下の周波数の帯を除いたところ）。幅が表示点の数になります。 */
    juce::Rectangle<int> getPlotBounds() const;

    /** 最後の`paint()`にかかった時間（ミリ秒）。フレームレートを落とすかの判断に使います（設計書6.1）。 */
    double getLastPaintMilliseconds() const noexcept { return lastPaintMs; }

    std::function<void()> onResetPeaks;

    /** 縦軸でドラッグ・ホイール・ダブルクリックしたとき（上端 dB, 幅 dB）。 */
    std::function<void (float top, float range)> onLevelViewChanged;

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    static constexpr int axisWidth = 44;
    static constexpr int frequencyStripHeight = 18;

    //--------------------------------------------------------------------------
    // 自己検査・プレビューのため

    /** その x（プロット内）でのカーソル読み取りの文字（仕様書3.5）。 */
    juce::String getReadoutText (int plotX) const;

    /** カーソルを置いたことにする（プレビューの絵のため。ウィンドウ座標のx、-1で消す）。 */
    void setCursorForTesting (int x) { cursorX = x; repaint(); }

private:
    float dbToY (float db) const;
    float yToDb (float y) const;
    double xToFrequency (float plotX) const;
    float frequencyToX (double hz) const;

    bool isOnAxis (juce::Point<int> p) const;

    void paintGrid (juce::Graphics& g, juce::Rectangle<int> plot);
    void paintFills (juce::Graphics& g, juce::Rectangle<int> plot);
    void paintRangeBand (juce::Graphics& g, juce::Rectangle<int> plot);
    void paintLines (juce::Graphics& g, juce::Rectangle<int> plot);
    void paintReadout (juce::Graphics& g, juce::Rectangle<int> plot);

    juce::Path makeLine (const std::vector<float>& values, juce::Rectangle<int> plot, int from, int to) const;

    AnalyzerSettings settings;
    AnalyzerThemeColors colours;
    const AnalyzerFrame* frame = nullptr;

    juce::Image fillImage;
    int cursorX = -1;

    float dragStartTop = 0.0f;
    bool draggingAxis = false;

    double lastPaintMs = 0.0;
};
