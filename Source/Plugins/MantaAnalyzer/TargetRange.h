#pragma once

#include "AnalyzerSettings.h"
#include "MonotoneCubic.h"

#include <array>

//==============================================================================
/**
    8.329：等ラウドネス曲線（アナライザー設計書5.1の`Iso226`）。

    ### ⚠️ 係数は ISO 226:**2003** のもの（本人の判断）

    仕様書5.2は**2023年版**を指定していますが、2023年版の係数表は規格書（有料）にしか無く、
    確かな写しが手に入りませんでした。本人と相談して、**公開されている2003年版の係数で先に作る**
    ことにしています（Phase 319）。2003年版と2023年版の差は、多くの周波数で1 dB未満です
    （20 Hzの聴こえ始めが0.4 dB下がった、など）。

    **2023年版の表が手に入ったら、下の4つの表と`kConstant`を差し替えるだけ**で済むように、
    ここ1箇所にまとめてあります。有効範囲（`maxPhonAt()`）も版に合わせて見直すこと。

    係数は公開実装（dsprelated.com「Equal Loudness Curves (ISO226)」）から写し、
    手元の知識（ISO 226:2003の表）と突き合わせてあります。
*/
namespace Iso226
{
    constexpr int numBands = 29;

    /** 1/3オクターブの29点（20 Hz〜12.5 kHz）。 */
    extern const std::array<double, numBands> frequencies;

    /** 1 kHz の番号（`frequencies[17] == 1000`）。 */
    constexpr int index1k = 17;

    /** その周波数・ラウドネスレベル（phon）での音圧レベル（dB SPL）。仕様書5.2の式。 */
    double splAt (int band, double phon);

    /** 規格が有効とする上限（phon）。2003年版：4 kHzまでは90、5 kHz以上は80。 */
    double maxPhonAt (double hz);

    constexpr double minFrequency = 20.0;
    constexpr double maxFrequency = 12500.0;

    /** 係数の版（画面と文書に出す）。 */
    constexpr const char* edition = "ISO 226:2003";
}

//==============================================================================
/**
    8.329：ターゲットレンジ（設計書5章）。

    **曲線の形は設定を変えたときに1回だけ**求め、フレームごとに変わるのは
    **上下の位置（オフセット1つ）だけ**にしてあります（設計書5章冒頭）。

    表示する値は D(f) = 補間(f) ＋ S·log2(f/1000) ＋ offset（設計書5.2）。
    補間は log2 f の上の単調三次。20 Hz 未満と 12.5 kHz 超は**端の2点から直線外挿**。
*/
class TargetRange
{
public:
    TargetRange();

    /** phon・スロープ・合わせ方を受け取る（形の計算はphonが変わったときだけ）。 */
    void configure (const AnalyzerSettings& settings);

    /** 位置を進める。`referenceDb`は入力の基準帯域平均（時間平均したもの）、
        `latestReferenceDb`は同じものをいちばん新しいフレームで（**無音の判定はこちら**。8.329）。 */
    void update (float referenceDb, float latestReferenceDb, double dtSeconds);

    /** 次に有効な入力が来たら、ゆっくりではなく**すぐ**合わせる（画面を開いたとき）。 */
    void snapOnNextInput() noexcept { waitingForFirstInput = true; }

    float lowerDb (double hz) const;
    float upperDb (double hz) const;

    /** 表示するオフセット（dB）。 */
    float getOffsetDb() const noexcept { return offset; }

    /** 規格の定義域の外、または設定phonがその周波数の有効範囲の外（仕様書5.4）。 */
    bool outsideStandard (double hz) const;

    /** 1 kHz での下限と上限の中点（dB SPL）。 */
    double getMidpointAt1k() const noexcept { return midpoint1k; }

    /** 無音とみなす基準帯域平均（仕様書5.3）。 */
    static constexpr float silenceDb = -90.0f;

    /** 自動追従の時定数（秒）。 */
    static constexpr double followSeconds = 3.0;

private:
    float shapeDb (const MonotoneCubic& curve, const std::array<double, Iso226::numBands>& spl, double hz) const;
    float targetOffset (float referenceDb) const;

    AnalyzerSettings settings;
    float configuredLow = -1.0f, configuredHigh = -1.0f;

    std::array<double, Iso226::numBands> lowSpl {}, highSpl {};
    MonotoneCubic lowCurve, highCurve;
    double midpoint1k = 0.0;

    float offset = 0.0f;
    bool waitingForFirstInput = true;
};
