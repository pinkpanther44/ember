#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
    仕様書4.8：スペクトラムアナライザー（設計書「AnalyzerEngine」）。

    ### 音を出すスレッドは、書き写すだけ

    `processBlock()`でやるのは**円環バッファへの書き写し**だけです。
    FFTは`EQSpectrum`が画面のスレッドで回します（30回／秒）。

    音のスレッドでFFTまで回すと、**表示を細かくするほど音が途切れやすくなります**
    ——見た目の設定が音の安定に効いてしまうのは、いちばん避けたい形です。

    ### なぜ`juce::AbstractFifo`ではないのか

    `AbstractFifo`は「**読み手が全部受け取る**」ための道具です。
    アナライザーが欲しいのは「**いちばん新しい4096サンプル**」だけで、
    画面が止まっているあいだの音は捨てて構いません。

    読み手が追いつかないと`AbstractFifo`は詰まり、
    **書き手（音のスレッド）が捨てる判断**を持つことになります。
    円環バッファに書きっぱなしにしておけば、その判断が要りません。

    書いている最中の場所を読むと1フレームだけ絵が乱れ得ますが、
    **音には一切影響しません**（読み手は絵しか作らない）。
*/
class EQAnalyserFifo
{
public:
    EQAnalyserFifo();

    /** 中身を消す。`prepareToPlay()`から呼ぶこと。 */
    void reset() noexcept;

    /** 音のスレッドから。**ステレオはモノへ混ぜて**書きます（表示は1本）。 */
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;

    /** いちばん新しい`numSamples`個を、古い順に`destination`へ写す。
        **画面のスレッドから呼ぶこと。** */
    void readLatest (float* destination, int numSamples) const noexcept;

    /** 円環バッファの長さ（2のべき乗）。 */
    static constexpr int bufferSize = 1 << 15;

private:
    std::vector<float> buffer;
    std::atomic<int> writePosition { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQAnalyserFifo)
};

//==============================================================================
/**
    円環バッファからFFTを取り、**対数目盛りの点列**にして返すところ。

    ### 目盛り

    20Hz〜20kHzを`numPoints`個の対数間隔に割ってあります。
    FFTのビンは**等間隔**なので、

    - 低いほう（1点にビンが1つも無い）… 隣のビンから補間する
    - 高いほう（1点にビンが何十個も入る）… **その中の最大**を取る

    最大を取るのは、平均にすると**細いピークが埋もれる**ためです
    （アナライザーは「どこが出ているか」を見る道具なので、尖りを残す）。

    ### 落ち方

    上がるときは即座、下がるときは`speed`に応じてゆっくり落とします。
    **上がりも遅くすると、短い音（ハイハット等）がまったく見えません。**
*/
class EQSpectrum
{
public:
    EQSpectrum();

    void prepare (double sampleRateToUse);

    /** 1フレームぶん取り込む。`frozen`のときは**取り込まず、前の絵を保ちます**
        （仕様書4.8のFreeze）。 */
    void update (const EQAnalyserFifo& fifo, float speed, bool frozen);

    /** 全部の点を無音（下限）へ戻す。 */
    void clear();

    static constexpr int numPoints = 256;

    /** `index`番目の点の周波数（Hz）。 */
    float getFrequency (int index) const noexcept { return frequencies[(size_t) index]; }

    /** `index`番目の点の大きさ（dBFS）。 */
    float getMagnitudeDb (int index) const noexcept { return magnitudesDb[(size_t) index]; }

    /** 下限（これ以下は「無音」として扱う）。 */
    static constexpr float floorDb = -110.0f;

private:
    static constexpr int fftOrder = 12;           // 4096点
    static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };

    std::vector<float> fftBuffer;    // 2 * fftSize（FFTが上書きするため）
    std::array<float, (size_t) numPoints> frequencies {};
    std::array<float, (size_t) numPoints> magnitudesDb {};

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQSpectrum)
};
