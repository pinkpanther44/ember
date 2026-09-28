#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    8.329：単調三次補間（Fritsch–Carlson。アナライザー設計書6.3）。

    **山の前後で、線が実際の値を超えてはみ出さない**補間です。普通の三次スプラインは
    尖った山の手前で持ち上がり、「そこにない音」を描いてしまいます。

    - 隣り合う点の傾き δ を求め、各点の接線 m を両側の δ の平均にする
    - δ の符号が変わる点（山・谷）と、δ が 0 の区間の両端は m = 0
    - α = m_k/δ_k、β = m_(k+1)/δ_k が α²+β² > 9 なら、両方を 3/√(α²+β²) 倍に縮める

    **スペクトラム曲線とターゲットレンジの29点で、同じものを使います**（1.27）。

    x は**単調増加**であること（アナライザーでは log 周波数）。範囲の外は端の値を返します
    （外挿が要る側は、呼ぶ側で決める。レンジは直線外挿。仕様書5.2）。
*/
class MonotoneCubic
{
public:
    MonotoneCubic() = default;

    MonotoneCubic (const double* xs, const double* ys, int n) { set (xs, ys, n); }

    void set (const double* xs, const double* ys, int n)
    {
        x.assign (xs, xs + n);
        y.assign (ys, ys + n);
        m.assign ((size_t) n, 0.0);

        if (n < 2)
            return;

        std::vector<double> delta ((size_t) n - 1);

        for (int k = 0; k < n - 1; ++k)
            delta[(size_t) k] = (y[(size_t) k + 1] - y[(size_t) k]) / (x[(size_t) k + 1] - x[(size_t) k]);

        m[0] = delta[0];
        m[(size_t) n - 1] = delta[(size_t) n - 2];

        for (int k = 1; k < n - 1; ++k)
        {
            const double a = delta[(size_t) k - 1];
            const double b = delta[(size_t) k];

            // 山・谷（符号が変わる）では平らにする
            m[(size_t) k] = (a * b <= 0.0) ? 0.0 : 0.5 * (a + b);
        }

        for (int k = 0; k < n - 1; ++k)
        {
            const double d = delta[(size_t) k];

            if (d == 0.0)
            {
                m[(size_t) k] = 0.0;
                m[(size_t) k + 1] = 0.0;
                continue;
            }

            const double alpha = m[(size_t) k] / d;
            const double beta = m[(size_t) k + 1] / d;
            const double s = alpha * alpha + beta * beta;

            if (s > 9.0)
            {
                const double tau = 3.0 / std::sqrt (s);
                m[(size_t) k] = tau * alpha * d;
                m[(size_t) k + 1] = tau * beta * d;
            }
        }
    }

    int size() const noexcept { return (int) x.size(); }

    double operator() (double at) const
    {
        const int n = (int) x.size();

        if (n == 0)
            return 0.0;

        if (n == 1 || at <= x.front())
            return y.front();

        if (at >= x.back())
            return y.back();

        const auto upper = std::upper_bound (x.begin(), x.end(), at);
        const int k = (int) (upper - x.begin()) - 1;

        return evaluate (k, at);
    }

    /** 区間 k（x[k]〜x[k+1]）の中の値。**区間が分かっているとき**の速い道。 */
    double evaluate (int k, double at) const
    {
        const double h = x[(size_t) k + 1] - x[(size_t) k];
        const double t = (at - x[(size_t) k]) / h;
        const double t2 = t * t;
        const double t3 = t2 * t;

        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;

        return h00 * y[(size_t) k] + h10 * h * m[(size_t) k]
             + h01 * y[(size_t) k + 1] + h11 * h * m[(size_t) k + 1];
    }

private:
    std::vector<double> x, y, m;
};
