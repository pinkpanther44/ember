#include "PitchEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
    constexpr double pi = 3.141592653589793;

    int nextPowerOfTwo (int n)
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    //==========================================================================
    /**
        8.337：`IPitchEngine`の TD-PSOLA 版（仕様書4章「採用」の方式。設計書（ライト版）7章の v2）。

        **自作です**（ライブラリを使わない）。ピッチ検出は本体（`ShifterEngine`の YIN）の結果を
        `setPitchInfo()`でもらい、ここでは**ピッチマーク・粒の切り出し・並べ直し**だけをします。

        ```
        入力 ─ ピッチマーク（周期ごと。低域を通した波形の山に寄せる） ─┐
                                                                     ├→ 粒（前後のマークまでの窓）を
        比率 r ─ 出力のマーク間隔 ＝ 解析のマーク間隔 ÷ r ──────────┘    出力のマークに重ねて足す
        ```

        ### 窓は左右で長さが違う（前のマーク〜次のマーク）

        左半分は「前のマークまで」、右半分は「次のマークまで」の raised cosine。**r＝1 なら隣の粒と足して
        ちょうど 1 になる**ので、ピッチを動かさないときは入力がそのまま出ます（Mix 50% で櫛形にならない）。

        ### 音程が「周期そのもの」で決まる

        出力のマーク間隔を 解析の間隔 ÷ r にするので、**出る周期は入力の周期のちょうど 1/r**。
        Signalsmith 版の弱点（純音がビンの格子に引かれて最大26セント）がありません。

        ### フォルマント

        粒そのものを伸び縮みさせる（倍率 k で読む位置を詰める）。Link Off は k＝Formant、
        Link On は k＝r×Formant（声の太さがピッチに付いていく）。**k は 0.5〜2 に制限**
        （窓の長さが遅れの範囲に収まるように。Signalsmith 版と同じ ±12 半音ぶん）。

        ### 無声のとき

        仕様書3章「無声音はシフトせずに通す」。5 ms 間隔・同じ長さの raised cosine で重ねるので、そのまま通ります。
        有声に戻ったら、**最初の粒を最初のマークに揃える**（r＝1 で入力と位相が揃うように）。

        ### 時刻

        粒は「いま − D」までに置き（D＝`inputLatency()`）、出すのは「いま − D − O」まで（O＝`outputLatency()`）。
        **O は粒の左半分の最長より長いこと**——そうでないと、あとから置く粒がもう出した音に重なる。
    */
    class PsolaEngine final : public IPitchEngine
    {
    public:
        void matchLatency (int inputSamples, int outputSamples) override
        {
            wantedInput = inputSamples;
            wantedOutput = outputSamples;
        }

        void prepare (double sampleRate, int numChannels, int) override
        {
            rate = sampleRate > 0.0 ? sampleRate : 48000.0;
            channels = std::max (1, std::min (2, numChannels));

            inLatency = wantedInput > 0 ? wantedInput : (int) std::lround (rate * 0.025);
            outLatency = wantedOutput > 0 ? wantedOutput : (int) std::lround (rate * 0.025);

            maxPeriod = std::ceil (rate / 80.0);   // 検出の下限と同じ
            unvoicedPeriod = std::max (16.0, std::round (rate * 0.005));
            maxBridge = 0.150 * rate;
            correlationScratch.assign ((size_t) (2.0 * maxPeriod + 16.0), 0.0);

            // 入力：D＋マークの先読み（1.25周期）＋粒の半分（1.5周期）＋ゆとり
            const int inSize = nextPowerOfTwo (inLatency + (int) (4.0 * maxPeriod) + 1024);
            inputMask = inSize - 1;

            for (auto& ring : input)
                ring.assign ((size_t) inSize, 0.0f);

            lowpassed.assign ((size_t) inSize, 0.0f);
            mono.assign ((size_t) inSize, 0.0f);

            // 出力：O＋粒の右半分の最長＋ゆとり
            const int outSize = nextPowerOfTwo (outLatency + (int) (8.0 * maxPeriod) + 1024);
            outputMask = outSize - 1;

            for (auto& ring : overlap)
                ring.assign ((size_t) outSize, 0.0f);

            // マークを寄せる山は**短い時間のエネルギー**（二乗を 2 kHz の一次の低域で）。声帯のパルスの位置で最大になる（8.337）
            lowpassCoefficient = (float) std::exp (-2.0 * pi * 2000.0 / rate);

            reset();
        }

        void reset() override
        {
            for (auto& ring : input)
                std::fill (ring.begin(), ring.end(), 0.0f);

            for (auto& ring : overlap)
                std::fill (ring.begin(), ring.end(), 0.0f);

            std::fill (lowpassed.begin(), lowpassed.end(), 0.0f);
            std::fill (mono.begin(), mono.end(), 0.0f);
            lowpassState = 0.0f;
            written = 0;
            emittedUpTo = 0;
            synthesis = -1.0;
            marksValid = false;
            voiced = false;
            period = 0.0;
            bridged = 0.0;
        }

        int inputLatency() const override  { return inLatency; }
        int outputLatency() const override { return outLatency; }

        void setRatio (float r) override { ratio = std::max (0.25, std::min (4.0, (double) r)); }

        void setFormant (float factor, bool followPitch) override
        {
            formant = factor;
            link = followPitch;
        }

        void setFormantBase (float) override {}

        void setPitchInfo (bool isVoiced, float f0Hz) override
        {
            voiced = isVoiced && f0Hz > 0.0f;
            period = voiced ? std::min (maxPeriod, rate / f0Hz) : 0.0;
        }

        void process (const float* const* in, float* const* out, int n) override
        {
            // 1. 入力を輪へ（マークを寄せるための低域も）
            for (int i = 0; i < n; ++i)
            {
                const int64_t t = written + i;
                float sum = 0.0f;

                for (int c = 0; c < channels; ++c)
                {
                    input[(size_t) c][(size_t) (t & inputMask)] = in[c][i];
                    sum += in[c][i];
                }

                sum /= (float) channels;
                mono[(size_t) (t & inputMask)] = sum;
                lowpassState = sum * sum + lowpassCoefficient * (lowpassState - sum * sum);
                lowpassed[(size_t) (t & inputMask)] = lowpassState;
            }

            written += n;

            // 2. 粒を「いま − D」まで置く
            const double horizon = (double) (written - inLatency);

            if (synthesis < 0.0)
                synthesis = std::max (0.0, horizon);

            while (synthesis <= horizon)
                placeGrain();

            // 3. 「いま − D − O」までを出す（そこより前には、もう粒が重ならない）
            for (int i = 0; i < n; ++i)
            {
                const int64_t t = written - n + i - inLatency - outLatency;

                for (int c = 0; c < channels; ++c)
                {
                    float value = 0.0f;

                    if (t >= 0)
                    {
                        auto& slot = overlap[(size_t) c][(size_t) (t & outputMask)];
                        value = slot;
                        slot = 0.0f;
                    }

                    out[c][i] = value;
                }
            }

            emittedUpTo = written - inLatency - outLatency;
        }

    private:
        /** `predicted`の前後 1/4 周期で、低域を通した波形がいちばん高いところ（使える入力の範囲で）。 */
        double refineMark (double predicted, double searchPeriod, double notBefore) const
        {
            const double reach = 0.25 * searchPeriod;
            const int64_t from = (int64_t) std::ceil (std::max (predicted - reach, notBefore));
            const int64_t to = std::min<int64_t> ((int64_t) std::floor (predicted + reach), written - 2);

            if (to < from)
                return std::min (predicted, (double) (written - 1));

            int64_t best = from;
            float bestValue = lowpassed[(size_t) (from & inputMask)];

            for (int64_t t = from + 1; t <= to; ++t)
            {
                const float v = lowpassed[(size_t) (t & inputMask)];

                if (v > bestValue)
                {
                    bestValue = v;
                    best = t;
                }
            }

            return (double) best;
        }

        /** 次のマーク：いまのマークのまわり1周期と、0.8〜1.2周期先のまわり1周期の**正規化相関がいちばん高い**ところ
            （放物線で小数まで）。8.337：山の高さで打つと、フォルマントで鳴る細かい山のどれを採るかが周期ごとに揺れて
            （マークのぶれ）粒どうしが打ち消し合い、+7 で倍音が 5〜16 dB 痩せた。**間隔を波形の似かたで決める**と揺れない。 */
        struct MarkSearch
        {
            double position;
            double score;   ///< 正規化相関（1＝同じ形）
        };

        /** 次のマーク：いまのマークのまわり1周期と、`lowFactor`〜`highFactor`周期先のまわり1周期の**正規化相関**が高いところ
            （放物線で小数まで）。8.337：山の高さで打つと、フォルマントで鳴る細かい山のどれを採るかが周期ごとに揺れて
            （マークのぶれ）粒どうしが打ち消し合い、+7 で倍音が 5〜16 dB 痩せた。**間隔を波形の似かたで決める**と揺れない。

            `preferEarliest`：いちばん高い山ではなく、**いちばん高い山の 85 % 以上ある最初の山**を採る（8.338。探す幅を
            2周期まで広げると、倍の周期＝1オクターブ下も同じくらい似るので、近いほうを採らないと音程が1オクターブ落ちる）。 */
        MarkSearch findNextMark (double from, double searchPeriod, double lowFactor, double highFactor, bool preferEarliest) const
        {
            const int64_t base = (int64_t) std::floor (from);
            const int half = std::max (4, (int) std::lround (0.5 * searchPeriod));
            const int lowLag = std::max (2, (int) std::floor (lowFactor * searchPeriod));
            int highLag = std::min ((int) std::ceil (highFactor * searchPeriod), (int) correlationScratch.size() - 2);

            // 使える入力の範囲まで（先読みが足りない低い声では、探す幅が狭くなる）
            highLag = (int) std::min<int64_t> (highLag, written - 2 - half - base);

            if (highLag <= lowLag)
                return { refineMark (from + searchPeriod, searchPeriod, from + 0.5 * searchPeriod), 0.0 };

            auto at = [this] (int64_t t) { return mono[(size_t) (t & inputMask)]; };

            double reference = 0.0;
            for (int j = -half; j <= half; ++j)
                reference += (double) at (base + j) * at (base + j);

            auto& c = correlationScratch;

            for (int lag = lowLag - 1; lag <= highLag + 1; ++lag)
            {
                double product = 0.0, energy = 0.0;

                for (int j = -half; j <= half; ++j)
                {
                    const double a = at (base + j), b = at (base + lag + j);
                    product += a * b;
                    energy += b * b;
                }

                c[(size_t) lag] = product / std::sqrt (reference * energy + 1.0e-30);
            }

            int bestLag = lowLag;

            for (int lag = lowLag + 1; lag <= highLag; ++lag)
                if (c[(size_t) lag] > c[(size_t) bestLag])
                    bestLag = lag;

            if (preferEarliest && c[(size_t) bestLag] > 0.0)
            {
                const double enough = 0.85 * c[(size_t) bestLag];

                for (int lag = lowLag; lag < bestLag; ++lag)
                {
                    if (c[(size_t) lag] >= enough && c[(size_t) lag] >= c[(size_t) lag - 1] && c[(size_t) lag] >= c[(size_t) lag + 1])
                    {
                        bestLag = lag;
                        break;
                    }
                }
            }

            double offset = 0.0;
            const double a = c[(size_t) bestLag - 1], b = c[(size_t) bestLag], d = c[(size_t) bestLag + 1];
            const double denominator = a - 2.0 * b + d;

            if (denominator < -1.0e-12)
                offset = std::max (-0.5, std::min (0.5, 0.5 * (a - d) / denominator));

            return { from + bestLag + offset, b };
        }

        double nextMark (double from, double searchPeriod) const
        {
            return findNextMark (from, searchPeriod, 0.8, 1.2, false).position;
        }

        float readInput (int channel, double position) const
        {
            const double floorPosition = std::floor (position);
            const int64_t i = (int64_t) floorPosition;
            const float fraction = (float) (position - floorPosition);
            const auto& ring = input[(size_t) channel];
            const float a = ring[(size_t) (i & inputMask)];
            const float b = ring[(size_t) ((i + 1) & inputMask)];
            return a + (b - a) * fraction;
        }

        /** 出力の`centre`に、入力の`source`を中心とする粒を足す（左右で長さの違う raised cosine）。 */
        void addGrain (double centre, double source, double leftHalf, double rightHalf, double scale, double gain)
        {
            // もう出した位置には足さない（有声の頭で粒を最初のマークへ引き戻すと、左端がそこへ届くことがある。
            // 足すと、輪の同じ場所をあとで読む**別の時刻**の音に混ざる）
            const int64_t first = std::max<int64_t> ((int64_t) std::ceil (centre - leftHalf), emittedUpTo);
            const int64_t last = (int64_t) std::floor (centre + rightHalf);

            for (int64_t j = first; j <= last; ++j)
            {
                const double d = (double) j - centre;
                const double half = d < 0.0 ? leftHalf : rightHalf;
                const double w = 0.5 * (1.0 + std::cos (pi * d / half));
                const double position = source + d * scale;
                const float g = (float) (gain * w);

                for (int c = 0; c < channels; ++c)
                    overlap[(size_t) c][(size_t) (j & outputMask)] += g * readInput (c, position);
            }
        }

        void placeGrain()
        {
            // 粒の左半分は O より短く（あとから置く粒が、もう出した音に重ならないように）
            const double halfLimit = (double) outLatency - 1.0;

            // 8.338：**検出が無声と言っても、波形がまだ周期的なら有声として続ける**（最長 150 ms）。
            // 本人の報告「唐突なピッチの変化がある場面では、エフェクトが一瞬掛からない」：音程が速く動く窓は
            // 前後の音が混ざって検出が無声と判定し、ここがそのまま通していた（50 ms で1オクターブのしゃくりで 60 ms）。
            // 直前の周期の 0.5〜2 倍の範囲に、いまの1周期とよく似た形（相関 0.6 以上）があるうちは追いかける
            bool useVoiced = voiced && period > 0.0;
            double P = period;
            bool bridging = false;

            if (useVoiced)
            {
                bridged = 0.0;
            }
            else if (marksValid && bridged < maxBridge)
            {
                const double lastSpacing = std::max (8.0, next - current);
                const auto found = findNextMark (current, lastSpacing, 0.5, 2.0, true);

                if (found.score >= 0.6)
                {
                    useVoiced = true;
                    bridging = true;
                    P = lastSpacing;
                }
            }

            auto advance = [this, bridging] (double from, double searchPeriod)
            {
                return bridging ? findNextMark (from, searchPeriod, 0.5, 2.0, true).position : nextMark (from, searchPeriod);
            };

            if (useVoiced)
            {
                if (! marksValid)
                {
                    // 有声に入った：最初の粒を最初のマークに揃える（r＝1 で入力と位相が揃う）
                    current = refineMark (synthesis, P, -1.0e18);
                    previous = current - P;
                    next = nextMark (current, P);
                    synthesis = current;
                    marksValid = true;
                }
                else
                {
                    // 出力のマークにいちばん近い解析のマークへ進める（r＞1 なら同じ粒を繰り返し、r＜1 なら飛ばす）
                    while (std::abs (next - synthesis) < std::abs (current - synthesis))
                    {
                        previous = current;
                        current = next;
                        next = advance (current, bridging ? std::max (8.0, current - previous) : P);
                    }
                }

                const double k = std::max (0.5, std::min (2.0, link ? ratio * formant : (double) formant));
                // 上げるときは粒を「出力の2周期」まで短くする（8.337）。入力の2周期のままだと、粒のスペクトルに入力の倍音の
                // 山谷が残り、出力の奇数倍音がその谷に落ちて1オクターブ上のように聞こえた（/a/ 220 Hz を +7）。r≦1 は変えない
                const double shorten = std::max (1.0, ratio);
                const double leftHalf = std::max (1.0, std::min (halfLimit, (current - previous) / k / shorten));
                const double rightHalf = std::max (1.0, std::min (halfLimit, (next - current) / k / shorten));
                const double spacing = std::max (1.0, (next - current) / ratio);

                // 重なりの量の平方根で割る（**エネルギーを保つ**。r＝1・k＝1 なら 1 ちょうどで、入力がそのまま出る）。
                // 平方根を取らずに振幅で割ると、粒が離れるとき（大きく下げる・フォルマントを上げる）にピークが4倍になった
                addGrain (synthesis, current, leftHalf, rightHalf, k, std::sqrt (spacing / (0.5 * (leftHalf + rightHalf))));
                synthesis += spacing;

                if (bridging)
                    bridged += spacing;
            }
            else
            {
                // 無声：そのまま通す（5 ms 間隔、同じ長さの raised cosine を重ねると 1 ちょうど）
                marksValid = false;
                bridged = 0.0;
                addGrain (synthesis, synthesis, unvoicedPeriod, unvoicedPeriod, 1.0, 1.0);
                synthesis += unvoicedPeriod;
            }
        }

        double rate = 48000.0;
        int channels = 2;
        int wantedInput = 0, wantedOutput = 0;
        int inLatency = 1200, outLatency = 1200;
        double maxPeriod = 600.0, unvoicedPeriod = 240.0;

        std::array<std::vector<float>, 2> input, overlap;
        std::vector<float> lowpassed, mono;   ///< マークの山を探す短時間エネルギー／周期の相関を取るモノラル
        int inputMask = 0, outputMask = 0;
        float lowpassCoefficient = 0.9f, lowpassState = 0.0f;
        int64_t written = 0;
        int64_t emittedUpTo = 0;   ///< ここより前は出し終えた（粒を足さない）

        double ratio = 1.0;
        float formant = 1.0f;
        bool link = false;
        bool voiced = false;
        double period = 0.0;

        double synthesis = -1.0;              ///< 次の粒を置く出力の位置（入力と同じ時間の物差し）
        double previous = 0.0, current = 0.0, next = 0.0;   ///< 解析のマーク
        bool marksValid = false;

        // 8.338：検出が無声と言っても周期が続いているあいだ、有声として追いかけた長さ（出力のサンプル）
        double bridged = 0.0, maxBridge = 7200.0;
        mutable std::vector<double> correlationScratch;   ///< マークを探す相関（`prepare()`で確保）
    };
}

std::unique_ptr<IPitchEngine> createPsolaEngine()
{
    return std::make_unique<PsolaEngine>();
}
