#pragma once

#include "MonotoneCubic.h"

#include <vector>

//==============================================================================
/**
    8.329：ビンの列を、**横1ピクセルに1点**の列へ直す（アナライザー設計書6.2の`CurveBuilder`）。

    x ピクセル目の周波数は f(x) = fMin·(fMax/fMin)^(x/(W−1))。
    各ピクセルが受け持つ幅 [f(x−0.5), f(x+0.5)] に入るビンの数で分けます。

    | ビンの数 | どうするか |
    |---|---|
    | 2個以上（主に高域） | 入っているビンの**パワー平均**をdBに |
    | 1個以下（主に低域） | 前後のビンのdBから、**log f の上の単調三次補間** |

    **どのピクセルがどちらで、どのビンを使うか**は、幅・周波数範囲・FFTサイズ・レートが
    変わったときだけ対応表として作り直します（`setAxis()`）。毎フレームは表をたどるだけ。

    W は**論理ピクセル**です（高DPIでも。点の間隔が十分に細かいため。設計書6.2）。
*/
class CurveBuilder
{
public:
    /** 対応表を作り直す。同じ値なら何もしない。 */
    void setAxis (double fMin, double fMax, int widthPx, double binHz, int numBins);

    /** ビンのdB列 → ピクセルのdB列（長さ`widthPx`）。 */
    void build (const std::vector<float>& binDb, std::vector<float>& outDbPerPx);

    int getWidth() const noexcept { return width; }
    double frequencyAtX (double x) const;
    double xForFrequency (double hz) const;

    /** その列がパワー平均か（自己検査が切り替わりの場所を探すため）。 */
    bool isAveragedColumn (int x) const { return columns[(size_t) x].averaged; }

private:
    struct Column
    {
        bool averaged = false;
        int lo = 0, hi = 0;          ///< 平均するビン [lo, hi]
        double logF = 0.0;           ///< 補間する位置（log2 f）
    };

    double minHz = 0.0, maxHz = 0.0, bin = 0.0;
    int width = 0, bins = 0;
    std::vector<Column> columns;

    int interpolationTop = 0;        ///< 補間に使うビンの上端（ここまでで単調三次を作る）
    std::vector<double> knotX, knotY;
    MonotoneCubic spline;
};
