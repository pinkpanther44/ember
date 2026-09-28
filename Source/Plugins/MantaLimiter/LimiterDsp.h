#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

//==============================================================================
/**
    8.333：リミッターの周辺の部品（リミッター設計書「周辺DSP設計」）。

    **係数はすべて`prepare`のときに作り、`process`では掛け算と足し算だけ**です。
    ホストにも画面にも依存しません。
*/
namespace LimiterDsp
{
    constexpr double pi = 3.14159265358979323846;

    /** 8.333：**速い log2 と 2^x**（リミッター設計書：「log／expは多項式近似（誤差0.01 dB以内）」）。

        log2：仮数 m ∈ [1, 2) を y = (m−1)/(m+1) にして ln m = 2(y + y³/3 + y⁵/5 + y⁷/7)。誤差は約1e−6。
        2^x：整数部は指数の桁に、小数部は e^(f·ln2) のテイラー展開（8項）。相対誤差は約1e−6。
        **dBにして0.0001 dB未満**なので、設計書の0.01 dBに十分収まります。 */
    inline float fastLog2 (float x) noexcept
    {
        union { float f; uint32_t i; } v { x };
        const int exponent = (int) ((v.i >> 23) & 255u) - 127;
        v.i = (v.i & 0x007FFFFFu) | 0x3F800000u;   // 仮数だけ残して [1, 2)

        const float y = (v.f - 1.0f) / (v.f + 1.0f);
        const float y2 = y * y;
        const float ln = 2.0f * y * (1.0f + y2 * (1.0f / 3.0f + y2 * (0.2f + y2 * (1.0f / 7.0f))));

        return (float) exponent + ln * 1.4426950408889634f;
    }

    inline float fastExp2 (float x) noexcept
    {
        x = std::max (-126.0f, std::min (126.0f, x));
        const float whole = std::floor (x);
        const float f = (x - whole) * 0.6931471805599453f;

        const float p = 1.0f + f * (1.0f + f * (0.5f + f * (1.0f / 6.0f + f * (1.0f / 24.0f
                              + f * (1.0f / 120.0f + f * (1.0f / 720.0f + f * (1.0f / 5040.0f)))))));

        union { float f; uint32_t i; } v {};
        v.i = (uint32_t) ((int) whole + 127) << 23;
        return v.f * p;
    }

    /** 20·log10(x) ＝ 6.0206·log2(x)。 */
    inline float fastGainToDb (float x) noexcept { return 6.020599913279624f * fastLog2 (x); }
    inline float fastDbToGain (float db) noexcept { return fastExp2 (db * 0.16609640474436813f); }

    /** 0次の変形ベッセル関数（Kaiser窓のため）。 */
    inline double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;

            if (term < 1.0e-12 * sum)
                break;
        }

        return sum;
    }

    inline double kaiser (int i, int length, double beta)
    {
        const double ratio = 2.0 * i / (double) (length - 1) - 1.0;
        return besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - ratio * ratio))) / besselI0 (beta);
    }

    inline double sinc (double x)
    {
        return std::abs (x) < 1.0e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
    }

    /** 8.333：**トゥルーピークの補間は8倍・各相16タップ**（検出と、出力のTP計で同じもの）。

        はじめは設計書どおり4倍・12タップでしたが、**4倍の点どうしの間の山を見落とします**——
        20 kHzの音を192 kHzの点で拾うと、点の間隔は位相で37.5°、山を最大0.47 dB低く読みます。
        自己検査（20 kHzで帯域制限した信号）で、天井を0.3 dB超えていました。8倍なら点の間隔は18.75°で、
        見落としは0.12 dB以下、長いフィルタで補間の誤差も小さくなります。 */
    constexpr int truePeakFactor = 8;
    constexpr int truePeakTapsPerPhase = 16;
    constexpr int truePeakDelay = truePeakTapsPerPhase / 2;   ///< 相0が元のサンプルそのもの＝遅れはちょうどこれ
    constexpr int truePeakHistory = 32;                        ///< 2のべき乗、タップ数より大きく

    /** `factor`倍の補間フィルタ（長さ factor × tapsPerPhase ＋ 1、中央は1）。
        **相0は元のサンプルそのもの**（sincの0点が標本点に乗る）なので、遅れは tapsPerPhase/2 サンプルちょうど。
        トゥルーピーク検出と、出力のTP計測が同じものを使います（設計書）。 */
    inline std::vector<float> designInterpolator (int factor, int tapsPerPhase, double beta)
    {
        const int length = factor * tapsPerPhase + 1;
        const int centre = length / 2;
        std::vector<float> h ((size_t) length);

        for (int i = 0; i < length; ++i)
            h[(size_t) i] = (float) (sinc ((double) (i - centre) / factor) * kaiser (i, length, beta));

        return h;
    }

    //==========================================================================
    /**
        2倍ハーフバンドFIR 1段（直線位相）。長さ L = 4K − 1、中央 c = 2K − 1。

        ハーフバンドは中央以外の「中央から偶数だけ離れた」係数が0なので、
        - 上げるとき：2つの出力のうち片方は**ただの遅延**、もう片方だけ 2K タップの積和
        - 下げるとき：中央の1タップと、2K タップの積和

        **1段の往復（上げて下げる）の遅れは、入力のレートで c サンプルちょうど**です。
    */
    class HalfbandStage
    {
    public:
        void design (int k, double beta)
        {
            K = k;
            const int length = 4 * K - 1;
            centre = 2 * K - 1;

            // 中央から奇数だけ離れた係数（2K個）。中央は 0.5 ちょうど（**片方の出力をただの遅延にするため**）
            taps.assign ((size_t) 2 * K, 0.0f);
            double sum = 0.0;

            for (int t = 0; t < 2 * K; ++t)
            {
                const int i = 2 * t;   // 偶数番目＝中央から奇数だけ離れた位置
                const double value = 0.5 * sinc ((double) (i - centre) / 2.0) * kaiser (i, length, beta);
                taps[(size_t) t] = (float) value;
                sum += value;
            }

            // **直流で1**になるよう、中央以外の合計を 0.5 に揃える
            for (auto& t : taps)
                t = (float) (t * 0.5 / sum);
        }

        /** 履歴は**同じ値を2か所に書く**輪（長さ2倍）。新しい順に連続で読めるので、
            タップごとに輪の位置を割り算で出さずに済みます（8.333：これが8倍の重さの大半でした）。 */
        void allocate (int channels)
        {
            upSize = 2 * K;
            downSize = 4 * K;
            upHistory.assign ((size_t) channels, std::vector<float> ((size_t) upSize * 2, 0.0f));
            downHistory.assign ((size_t) channels, std::vector<float> ((size_t) downSize * 2, 0.0f));
            upPos.assign ((size_t) channels, 0);
            downPos.assign ((size_t) channels, 0);
        }

        void reset()
        {
            for (auto& h : upHistory)   std::fill (h.begin(), h.end(), 0.0f);
            for (auto& h : downHistory) std::fill (h.begin(), h.end(), 0.0f);
            std::fill (upPos.begin(), upPos.end(), 0);
            std::fill (downPos.begin(), downPos.end(), 0);
        }

        /** 1サンプル入れて、2サンプル出す（時間の順）。 */
        void up (int channel, float x, float& first, float& second)
        {
            auto& h = upHistory[(size_t) channel];
            int& pos = upPos[(size_t) channel];

            pos = pos == 0 ? upSize - 1 : pos - 1;
            h[(size_t) pos] = x;
            h[(size_t) (pos + upSize)] = x;

            const float* w = h.data() + pos;   // w[t] = x[n − t]
            float y = 0.0f;

            for (int t = 0; t < 2 * K; ++t)
                y += taps[(size_t) t] * w[t];

            first = 2.0f * y;
            second = w[K - 1];   // 2 × 0.5 × x[n − (K−1)]
        }

        /** 2サンプル入れて、1サンプル出す。**1つ目（偶数）を入れたところで出す**（遅れを整数にするため）。 */
        float down (int channel, float first, float second)
        {
            auto& h = downHistory[(size_t) channel];
            int& pos = downPos[(size_t) channel];

            pos = pos == 0 ? downSize - 1 : pos - 1;
            h[(size_t) pos] = first;
            h[(size_t) (pos + downSize)] = first;

            // v[m] = w[2n − m]。偶数番目の係数は v[2t]、中央は v[c]
            const float* v = h.data() + pos;
            float z = 0.0f;

            for (int t = 0; t < 2 * K; ++t)
                z += taps[(size_t) t] * v[2 * t];

            z += 0.5f * v[centre];

            pos = pos == 0 ? downSize - 1 : pos - 1;
            h[(size_t) pos] = second;
            h[(size_t) (pos + downSize)] = second;

            return z;
        }

        int getCentre() const noexcept { return centre; }

    private:
        int K = 1, centre = 1, upSize = 2, downSize = 4;
        std::vector<float> taps;
        std::vector<std::vector<float>> upHistory, downHistory;
        std::vector<int> upPos, downPos;
    };

    //==========================================================================
    /**
        2倍・4倍・8倍（ハーフバンドを1〜3段。設計書）。**初段は急峻、後段は短く**。

        `getDelayInCoreSamples()`：上げて下げたときの遅れを、**いちばん高いレート**のサンプル数で。
        基準のレートでは端数になり得るので、端数はリミッターの遅延で埋めます（`LimiterEngine`）。
    */
    class Oversampler
    {
    public:
        static constexpr int maxStages = 3;

        void prepare (int channels, int maxBlock)
        {
            const int ks[maxStages] { 16, 8, 6 };   // 63 / 31 / 23 タップ

            for (int s = 0; s < maxStages; ++s)
            {
                stages[(size_t) s].design (ks[s], 8.0);
                stages[(size_t) s].allocate (channels);
            }

            for (auto& b : buffers)
                b.assign ((size_t) maxBlock * 8, 0.0f);

            numChannels = channels;
        }

        void setStages (int count)
        {
            numStages = std::clamp (count, 0, maxStages);

            for (auto& s : stages)
                s.reset();
        }

        int getFactor() const noexcept { return 1 << numStages; }

        int getDelayInCoreSamples() const noexcept
        {
            int delay = 0;
            for (int s = 0; s < numStages; ++s)
                delay += stages[(size_t) s].getCentre() * (getFactor() >> s);
            return delay;
        }

        /** 上げる。戻り値はチャンネルごとの作業場（長さ n × 倍率）。 */
        float* up (int channel, const float* input, int n)
        {
            auto& a = buffers[(size_t) channel * 2];
            auto& b = buffers[(size_t) channel * 2 + 1];

            std::copy (input, input + n, a.begin());
            int length = n;

            for (int s = 0; s < numStages; ++s)
            {
                for (int i = 0; i < length; ++i)
                    stages[(size_t) s].up (channel, a[(size_t) i], b[(size_t) (2 * i)], b[(size_t) (2 * i + 1)]);

                length *= 2;
                std::swap (a, b);
            }

            return a.data();
        }

        /** 下げる（`up()`が返した作業場を渡す）。 */
        void down (int channel, float* work, float* output, int n)
        {
            auto& spare = buffers[(size_t) channel * 2 + 1];
            float* source = work;
            float* target = spare.data();
            int length = n << numStages;

            for (int s = numStages - 1; s >= 0; --s)
            {
                for (int i = 0; i < length / 2; ++i)
                    target[i] = stages[(size_t) s].down (channel, source[2 * i], source[2 * i + 1]);

                length /= 2;
                std::swap (source, target);
            }

            std::copy (source, source + n, output);
        }

    private:
        std::array<HalfbandStage, maxStages> stages;
        std::array<std::vector<float>, 4> buffers;   // チャンネル2つ × 作業場2枚
        int numStages = 0, numChannels = 2;
    };

    //==========================================================================
    /** DCフィルタ（1次ハイパス、約5 Hz）：y[n] = x[n] − x[n−1] + R·y[n−1]、R = exp(−2π·5/fs)。 */
    struct DcFilter
    {
        void prepare (double sampleRate) { r = (float) std::exp (-2.0 * pi * 5.0 / sampleRate); reset(); }
        void reset() { x1 = y1 = 0.0f; }

        float process (float x)
        {
            const float y = x - x1 + r * y1;
            x1 = x;
            y1 = y;
            return y;
        }

        float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
    };

    //==========================================================================
    /** TPDFディザー（一様乱数2個の和、振幅 ±1 LSB）＋1次の誤差フィードバック（設計書）。 */
    struct Ditherer
    {
        void reset (uint32_t seed) { state = seed != 0 ? seed : 0x9e3779b9u; error = 0.0f; }

        float uniform()
        {
            // xorshift32
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) ((state >> 8) * (1.0 / 16777216.0));
        }

        float process (float x, float lsb, bool noiseShaping)
        {
            const float v = x - error;
            const float d = (uniform() + uniform() - 1.0f) * lsb;
            const float y = std::round ((v + d) / lsb) * lsb;
            error = noiseShaping ? (y - v) : 0.0f;
            return y;
        }

        uint32_t state = 0x9e3779b9u;
        float error = 0.0f;
    };

    //==========================================================================
    /**
        8.333：トゥルーピークの見張り（**検出と、出力のTP計の両方がこれ**。設計書「MeterEngineの出力TP計測も同じクラス」）。

        1サンプル入れるごとに「`truePeakDelay`サンプル前 i から i+1 までの区間」の最大（絶対値）を返します。

        ### 速くするために

        - 履歴は**同じ値を2か所に書く**輪（長さ2倍）。新しい順に**連続で読める**ので、積和がまとめて回ります
        - 係数は**タップごとに8相を並べた表**（タップ j・相 k ＝ 元の h[k + 8j]）。8相の足し算が独立になり、まとめて回ります

        ### 点のあいだの山：放物線で補う

        8倍の点どうしの間隔は、20 kHzの音（48 kHz）で位相18.75°。いちばん大きい点だけ見ると、山を
        最大0.12 dB低く読みます（自己検査で、天井を0.1 dBちょうど超えていました）。
        **いちばん大きい点とその両隣に放物線を当てて頂点を取る**と、正弦波で0.01 dB未満になります。
        区間の端（元のサンプルの位置）も補えるよう、**前の区間の最後の相を覚えて**おきます。
    */
    class TruePeakDetector
    {
    public:
        void prepare()
        {
            const auto h = designInterpolator (truePeakFactor, truePeakTapsPerPhase, 8.0);

            // **タップごとに8相を並べる**（[j][k]、相0は使わないので0）。内側のループが相をまたぐので、
            // 8つの足し算が互いに独立になり、まとめて計算できます
            for (int j = 0; j < truePeakTapsPerPhase; ++j)
                for (int k = 0; k < truePeakFactor; ++k)
                {
                    const int index = k + truePeakFactor * j;
                    coefficients[(size_t) (j * truePeakFactor + k)] = (k > 0 && index < (int) h.size()) ? h[(size_t) index] : 0.0f;
                }

            reset();
        }

        void reset()
        {
            history.fill (0.0f);
            pos = 0;
            lastPhase = 0.0f;
        }

        float push (float x) noexcept
        {
            pos = (pos - 1) & (truePeakHistory - 1);
            history[(size_t) pos] = x;
            history[(size_t) (pos + truePeakHistory)] = x;

            // w[j] = x[n − j]（新しい順、連続）
            const float* w = history.data() + pos;

            // z[0] = 前の区間の最後の相、z[1] = 区間の頭（元のサンプル）、z[2..8] = 相1〜7、z[9] = 区間の終わり
            float z[truePeakFactor + 2];
            z[0] = lastPhase;
            z[1] = w[truePeakDelay];
            z[truePeakFactor + 1] = w[truePeakDelay - 1];

            float acc[truePeakFactor] {};

            for (int j = 0; j < truePeakTapsPerPhase; ++j)
            {
                const float* c = coefficients.data() + j * truePeakFactor;
                const float sample = w[j];

                for (int k = 0; k < truePeakFactor; ++k)
                    acc[k] += c[k] * sample;
            }

            for (int k = 1; k < truePeakFactor; ++k)
                z[k + 1] = acc[k];

            lastPhase = z[truePeakFactor];

            // いちばん大きい点（区間の頭〜相7。区間の終わりは次の区間の頭として見る）
            int best = 1;
            for (int i = 2; i <= truePeakFactor + 1; ++i)
                if (std::abs (z[i]) > std::abs (z[best]))
                    best = i;

            float peak = std::abs (z[best]);

            // 放物線の頂点（両隣があり、山の形になっているときだけ）
            if (best >= 1 && best <= truePeakFactor)
            {
                const float a = z[best - 1], b = z[best], c = z[best + 1];
                const float curvature = a - 2.0f * b + c;

                // **本当に山の頂上のときだけ**（両隣が同じ符号で、どちらも小さい）。
                // 前の区間の最後の相のほうが大きいときなど、3点が山の形でないのに当てると、
                // 頂点が遠くへ飛びます（最初はこれで、メーターが +49 dB を読みました）
                const bool isTop = std::abs (a) <= std::abs (b) && std::abs (c) <= std::abs (b) && a * b >= 0.0f && c * b >= 0.0f;

                if (isTop && curvature * b < 0.0f)
                {
                    const float vertex = b - (c - a) * (c - a) / (8.0f * curvature);
                    peak = std::max (peak, std::abs (vertex));
                }
            }

            return peak;
        }

    private:
        std::array<float, truePeakFactor * truePeakTapsPerPhase> coefficients {};
        std::array<float, 2 * truePeakHistory> history {};
        int pos = 0;
        float lastPhase = 0.0f;
    };

    /** 出力のトゥルーピーク計（検出と同じもの）。 */
    using TruePeakMeter = TruePeakDetector;
}
