#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "EQAnalyser.h"
#include "EQFilterDesign.h"
#include "MantaEQProcessor.h"

//==============================================================================
/**
    設計書5.1の`CurveDisplayComponent`：EQカーブとスペクトラムアナライザーの画面。

    ### ここが操作の中心

    仕様書4.1のとおり、**バンドはこのグラフの上で作って動かします。**

    | 操作 | 何が起きるか |
    |---|---|
    | 何も無いところをダブルクリック | その周波数にバンドを1つ作る |
    | つまみをドラッグ | Freq（横）とGain（縦） |
    | Shiftを押しながらドラッグ | **動きの大きいほうだけ**（横だけ／縦だけ） |
    | つまみの上でホイール | Q |
    | つまみをダブルクリック | そのバンドを削除する |
    | つまみを右クリック | 形状・スロープ・配置・数値入力・バイパス・削除（**つまみのすぐ横に出ます**） |
    | 地を右クリック | 縦軸の幅、鍵盤表示 |

    ### 選んでいるバンドには、つまみに小さなボタンが付く（Phase 206・207）

    ```
        S D ×         ← ソロ／ダイナミクス／削除
    L ─( 3 )          ← 旗（L/R/M/S）は、つまみに接して
    ```

    **Sがつまみの真上**、そこから右へD・×と並びます（Phase 207）。
    いちばんよく押すものを、つまみの中心の延長に置いてあります。

    **選んでいるあいだだけ出します。** 常に出すと、バンドが増えたときに
    グラフが小さなボタンで埋まります。

    旗はステレオ配置が`Stereo`以外のときだけ。
    **LとMidは左、RightとSideは右**——耳の位置と同じ並びにしてあります。

    ### 線と音は同じ係数から出す

    カーブは`EQFilterDesign::designBand()`＋`magnitudeAt()`で描いています——
    **音を出している`MantaEQProcessor`とまったく同じ関数**です（1.27）。

    さらに、係数のもとになる値は`MantaEQProcessor::getBandSettings()`から取ります。
    こちらは**ダイナミクスで動いているぶんも入って**いるので、
    ダイナミックEQのバンドは**いま実際に掛かっている形**で描かれます。

    ### 重さ

    `paint()`のたびに、**横2ピクセルおき**に全バンドの振幅を掛け合わせています
    （700px幅なら350点×バンド数）。1ピクセルおきにすると倍になり、
    **描き直しは毎秒30回**なので効いてきます。2px刻みでも線の滑らかさは変わりません。
*/
class EQCurveComponent : public juce::Component,
                          private juce::Timer
{
public:
    explicit EQCurveComponent (MantaEQProcessor& processorToUse);
    ~EQCurveComponent() override;

    void paint (juce::Graphics& g) override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void mouseMove (const juce::MouseEvent& event) override;
    void mouseExit (const juce::MouseEvent& event) override;
    void mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    /** 選ばれているバンドが変わったときに呼ばれる（下のつまみ列を切り替えるため）。 */
    std::function<void (int)> onBandSelected;

    /** バンドの有無・形状などが変わったときに呼ばれる（つまみの出し分けをやり直す）。 */
    std::function<void()> onBandsChanged;

    void setSelectedBand (int bandIndex);
    int getSelectedBand() const { return selectedBand; }

private:
    void timerCallback() override;

    //==========================================================================
    // 目盛りの変換。**縦軸の幅は`<UI>`のプロパティ**（右クリックで変えられる）

    juce::Rectangle<float> getGraphArea() const;
    float getCurveRangeDb() const;

    float frequencyToX (float frequencyHz) const;
    float xToFrequency (float x) const;
    float gainToY (float gainDb) const;
    float yToGain (float y) const;
    float analyserDbToY (float levelDb, float frequencyHz) const;

    //==========================================================================
    void drawGrid (juce::Graphics& g) const;
    void drawSpectrum (juce::Graphics& g, const EQSpectrum& spectrum,
                        juce::Colour colour, bool filled) const;
    void drawCurves (juce::Graphics& g);

    /** バンドの値と係数の写しを取り直す。**描く前と、マウスを受ける前に必要**
        （マウスの判定も、この写しの上で行うため）。 */
    void updateBandSnapshots();
    void drawHandles (juce::Graphics& g) const;
    void drawKeyboard (juce::Graphics& g) const;

    /** つまみの中心。カット系などGainを使わない形状は0dBの高さに置く。 */
    juce::Point<float> getHandlePosition (int bandIndex) const;

    /** その座標にあるバンド（無ければ-1）。 */
    int findBandAt (juce::Point<float> position) const;

    //==========================================================================
    /** 選んでいるバンドのつまみに付く小さなボタン（Phase 206）。 */
    enum class HandleButton
    {
        none = 0,
        solo,      ///< そのバンドの範囲だけ聴く
        dynamic,   ///< ダイナミックEQの入切
        remove     ///< 削除
    };

    juce::Point<float> getHandleButtonCentre (int bandIndex, HandleButton button) const;

    /** その座標にあるボタン（**選んでいるバンドのものだけ**）。 */
    HandleButton findHandleButtonAt (juce::Point<float> position) const;

    void applyHandleButton (int bandIndex, HandleButton button);

    /** ステレオ配置の旗（L/R/M/S）。`Stereo`のときは空。 */
    static juce::String getChannelFlagText (MantaEQParams::Channel channel);

    /** 旗を左に出すか（Left・Mid）。右はRight・Side。 */
    static bool isChannelFlagOnLeft (MantaEQParams::Channel channel);

    //==========================================================================
    void showBandMenu (int bandIndex);
    void showBackgroundMenu();


    void setParameter (int bandIndex, const char* suffix, float value);
    void beginGesture (int bandIndex, const char* suffix);
    void endGesture (int bandIndex, const char* suffix);
    juce::RangedAudioParameter* getParameter (int bandIndex, const char* suffix) const;

    //==========================================================================
    MantaEQProcessor& processor;

    EQSpectrum preSpectrum, postSpectrum;

    int selectedBand = -1;
    int hoveredBand = -1;

    /** ドラッグ中のバンド（-1なら掴んでいない）。 */
    int draggingBand = -1;
    bool draggingFrequencyOnly = false;
    bool draggingGainOnly = false;

    /** バンドごとの係数。**`paint()`の頭で作り直します**（音側とは別の持ち物）。 */
    EQFilterDesign::SectionList bandSections[MantaEQParams::numBands];
    MantaEQParams::BandSettings bandSettings[MantaEQParams::numBands];

    /** アナライザーへ渡してあるレート。**変わったら作り直す**
        （書き出しはデバイスと違うレートで走ります。8.153）。 */
    double lastSpectrumSampleRate = 0.0;

    static constexpr int keyboardHeight = 16;
    static constexpr int labelStripHeight = 14;

    /** つまみに付く小さなボタンの半径と間隔。 */
    static constexpr float handleButtonRadius = 6.5f;
    static constexpr float handleButtonSpacing = 15.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQCurveComponent)
};
