#include "LimiterCore.h"

#include "LimiterDsp.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// GainPath

void LimiterCore::GainPath::allocate (int cap)
{
    capacity = cap;

    // デックの輪は**2のべき乗**（位置の折り返しを割り算ではなくマスクで。1サンプルに何度も通るので）
    int dequeSize = 1;
    while (dequeSize < cap + 2)
        dequeSize <<= 1;

    dequeMask = dequeSize - 1;
    dequeValue.assign ((size_t) dequeSize, 1.0f);
    dequeIndex.assign ((size_t) dequeSize, 0);
    ring1.assign ((size_t) cap + 2, 1.0f);
    ring2.assign ((size_t) cap + 2, 1.0f);
    delay.assign ((size_t) cap + 2, 1.0f);
}

void LimiterCore::GainPath::reset (int windowLength, bool tri, int extraDelay)
{
    holdLength = std::max (1, windowLength);
    triangular = tri;

    if (tri)
    {
        length1 = (holdLength + 1) / 2;   // S = 2B − 1
        length2 = length1;
    }
    else
    {
        length1 = holdLength;
        length2 = 1;
    }

    std::fill (ring1.begin(), ring1.begin() + length1, 1.0f);
    std::fill (ring2.begin(), ring2.begin() + length2, 1.0f);
    sum1 = (double) length1;
    sum2 = (double) length2;
    pos1 = pos2 = 0;

    delayLength = std::max (0, extraDelay);
    std::fill (delay.begin(), delay.begin() + std::max (1, delayLength), 1.0f);
    delayPos = 0;

    dequeFirst = 0;
    dequeCount = 0;
    index = 0;
}

void LimiterCore::GainPath::copyFrom (const GainPath& other)
{
    // **中身を写すだけ**（確保し直さない。同じ大きさで`allocate()`してあるので）
    std::copy (other.dequeValue.begin(), other.dequeValue.end(), dequeValue.begin());
    std::copy (other.dequeIndex.begin(), other.dequeIndex.end(), dequeIndex.begin());
    std::copy (other.ring1.begin(), other.ring1.end(), ring1.begin());
    std::copy (other.ring2.begin(), other.ring2.end(), ring2.begin());
    std::copy (other.delay.begin(), other.delay.end(), delay.begin());

    dequeFirst = other.dequeFirst;
    dequeMask = other.dequeMask;
    dequeCount = other.dequeCount;
    index = other.index;
    holdLength = other.holdLength;
    length1 = other.length1;
    length2 = other.length2;
    pos1 = other.pos1;
    pos2 = other.pos2;
    sum1 = other.sum1;
    sum2 = other.sum2;
    triangular = other.triangular;
    delayLength = other.delayLength;
    delayPos = other.delayPos;
}

float LimiterCore::GainPath::push (float r)
{
    //--------------------------------------------------------------------------
    // 区間最小値（単調デック。償却 O(1)）：新しい値より大きい候補を末尾から捨て、
    // 窓から外れた先頭を捨てれば、先頭が常に最小値
    while (dequeCount > 0)
    {
        const int back = (dequeFirst + dequeCount - 1) & dequeMask;

        if (dequeValue[(size_t) back] < r)
            break;

        --dequeCount;
    }

    {
        const int slot = (dequeFirst + dequeCount) & dequeMask;
        dequeValue[(size_t) slot] = r;
        dequeIndex[(size_t) slot] = index;
        ++dequeCount;
    }

    while (dequeIndex[(size_t) dequeFirst] <= index - holdLength)
    {
        dequeFirst = (dequeFirst + 1) & dequeMask;
        --dequeCount;
    }

    const float held = dequeValue[(size_t) dequeFirst];
    ++index;

    //--------------------------------------------------------------------------
    // 移動平均（累積和を double で。**1周するたびに数え直す**——足し引きの誤差を溜めないため。
    // 設計書は「24時間試験で確かめる」ですが、数え直せば溜まりようがありません）
    sum1 += (double) held - (double) ring1[(size_t) pos1];
    ring1[(size_t) pos1] = held;

    if (++pos1 >= length1)
    {
        pos1 = 0;
        sum1 = 0.0;
        for (int i = 0; i < length1; ++i)
            sum1 += (double) ring1[(size_t) i];
    }

    float average = (float) (sum1 / (double) length1);

    if (triangular)
    {
        sum2 += (double) average - (double) ring2[(size_t) pos2];
        ring2[(size_t) pos2] = average;

        if (++pos2 >= length2)
        {
            pos2 = 0;
            sum2 = 0.0;
            for (int i = 0; i < length2; ++i)
                sum2 += (double) ring2[(size_t) i];
        }

        average = (float) (sum2 / (double) length2);
    }

    average = std::min (1.0f, std::max (0.0f, average));

    //--------------------------------------------------------------------------
    // ゲイン経路の追加遅延 E（ランプの終点をピーク位置に合わせる）
    if (delayLength == 0)
        return average;

    const float out = delay[(size_t) delayPos];
    delay[(size_t) delayPos] = average;

    if (++delayPos >= delayLength)
        delayPos = 0;

    return out;
}

//==============================================================================

void LimiterCore::prepare (double coreSampleRate, int maxLookaheadSamples, int maxBlockSamples)
{
    sampleRate = coreSampleRate > 0.0 ? coreSampleRate : 48000.0;

    // 追加遅延 E ＝ N − (S − 1) ＋ 補正。補正は多くてもオーバーサンプリングの倍率ぶん＋α
    capacity = maxLookaheadSamples + 64;

    for (int c = 0; c < maxChannels; ++c)
    {
        active[(size_t) c].allocate (capacity);
        fading[(size_t) c].allocate (capacity);
        audioDelay[(size_t) c].assign ((size_t) capacity + truePeakDelay + 64, 0.0f);
        history[(size_t) c].assign ((size_t) capacity * 3 + 64, 1.0f);
        detectors[(size_t) c].prepare();
    }

    grTrace.assign ((size_t) std::max (1, maxBlockSamples), 0.0f);

    configure (std::max (1, std::min (maxLookaheadSamples, (int) std::lround (0.001 * sampleRate))), true, 0);
}

void LimiterCore::configure (int lookaheadSamples, bool truePeakDetection, int paddingSamples)
{
    lookahead = std::max (1, std::min (lookaheadSamples, capacity - 64));
    truePeak = truePeakDetection;
    tpDelay = truePeak ? truePeakDelay : 0;
    padding = std::max (0, paddingSamples);

    audioLength = getDelaySamples() + 1;
    historyLength = (int) history[0].size();

    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (audioDelay[(size_t) c].begin(), audioDelay[(size_t) c].end(), 0.0f);
        std::fill (history[(size_t) c].begin(), history[(size_t) c].end(), 1.0f);
        detectors[(size_t) c].reset();
        previousIntervalPeak[(size_t) c] = 0.0f;
        release[(size_t) c] = {};
    }

    audioPos = historyPos = 0;
    fadeSamplesLeft = 0;

    computeWindow (windowS, windowB);
    windowTriangular = dynamics.triangular;

    for (int c = 0; c < maxChannels; ++c)
        active[(size_t) c].reset (windowS, windowTriangular, lookahead - (windowS - 1) + padding);
}

void LimiterCore::computeWindow (int& S, int& B) const
{
    // 設計書：A = Lookahead × Attack × スタイル係数（1 ≤ A ≤ N+1）
    const int A = std::clamp ((int) std::lround ((double) lookahead * dynamics.attackRatio), 1, lookahead + 1);

    if (dynamics.triangular)
    {
        // 三角：長さ B の矩形を2段、S = 2B − 1。**S は N+1 を超えないこと**（E が負になる）
        B = std::clamp ((A + 1) / 2, 1, (lookahead + 2) / 2);
        S = 2 * B - 1;
    }
    else
    {
        S = A;
        B = A;
    }
}

void LimiterCore::setDynamics (const Dynamics& d)
{
    dynamics = d;

    const double tau = std::max (0.001, (double) d.releaseMs / 1000.0);
    alphaManual = std::exp (-1.0 / (tau * sampleRate));
    alphaFast = std::exp (-1.0 / (tau * 0.25 * sampleRate));   // 速い系：Release × 0.25
    alphaSlow = std::exp (-1.0 / (tau * 4.0 * sampleRate));    // 遅い系：Release × 4

    int S = 1, B = 1;
    computeWindow (S, B);

    if (S != windowS || d.triangular != windowTriangular)
    {
        windowS = S;
        windowB = B;
        windowTriangular = d.triangular;
        rebuildActivePaths();
    }
}

void LimiterCore::rebuildActivePaths()
{
    const int extra = lookahead - (windowS - 1) + padding;

    // 流し直す長さ：ホールド S ＋ 平均 S ＋ 追加遅延 E ＋ 余裕
    const int replay = std::min (historyLength - 1, 2 * windowS + extra + 4);

    for (int c = 0; c < maxChannels; ++c)
    {
        fading[(size_t) c].copyFrom (active[(size_t) c]);
        active[(size_t) c].reset (windowS, windowTriangular, extra);

        const auto& h = history[(size_t) c];

        for (int i = replay; i > 0; --i)
            active[(size_t) c].push (h[(size_t) ((historyPos - i + historyLength) % historyLength)]);
    }

    fadeSamplesTotal = std::max (1, (int) std::lround (switchSeconds * sampleRate));
    fadeSamplesLeft = fadeSamplesTotal;
}

float LimiterCore::releaseStep (Release& state, float attackGain)
{
    // **リミットしていないあいだは計算しない**（いちばん多い場面。log も exp も要らない）
    if (attackGain >= 1.0f && state.db >= -1.0e-6 && state.fastDb >= -1.0e-6 && state.slowDb >= -1.0e-6)
    {
        state.db = state.fastDb = state.slowDb = 0.0;
        state.reductionSeconds = 0.0;
        return 1.0f;
    }

    // dB への変換は速い近似（`LimiterDsp::fastGainToDb`。誤差0.0001 dB未満）
    const double attackDb = attackGain >= 1.0f ? 0.0 : (double) LimiterDsp::fastGainToDb (std::max (attackGain, 1.0e-9f));

    // 下がる向きは即時、戻る向きだけ1次で追う（dBで）。**この形なら結果は attackDb 以下**
    auto follow = [attackDb] (double& s, double alpha)
    {
        if (attackDb < s)
            s = attackDb;
        else
            s += (1.0 - alpha) * (attackDb - s);
    };

    double db;

    if (! dynamics.autoRelease)
    {
        follow (state.db, alphaManual);
        db = state.db;
    }
    else
    {
        // 自動リリース：速い系と遅い系を並走させ、GRが長く続くほど遅い系へ（設計書）
        follow (state.fastDb, alphaFast);
        follow (state.slowDb, alphaSlow);

        if (attackDb <= -0.5)
            state.reductionSeconds += 1.0 / sampleRate;
        else
            state.reductionSeconds = 0.0;

        const double w = std::min (1.0, state.reductionSeconds / 0.5);
        state.db = (1.0 - w) * state.fastDb + w * state.slowDb;
        db = state.db;
    }

    // **近似で往復したぶんの誤差で、攻撃段より大きくならないように押さえる**
    // （ここが無オーバーシュートの最後の砦。近似は dB で0.0001未満ですが、1を超えて1サンプルでも
    //  攻撃段の値を上回れば、天井を超えます）
    const float gain = db >= 0.0 ? 1.0f : LimiterDsp::fastDbToGain ((float) db);
    return std::min (gain, attackGain);
}

void LimiterCore::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::clamp (numChannels, 1, maxChannels);
    numSamples = std::min (numSamples, (int) grTrace.size());

    // 天井はリニアで（毎サンプルの log を避ける）。必要ゲインは C / |x| を直接
    const float ceiling = std::pow (10.0f, dynamics.ceilingDb / 20.0f);
    const float link = std::clamp (dynamics.link, 0.0f, 1.0f);
    const int audioDelayLength = getDelaySamples();
    const int audioCapacity = (int) audioDelay[0].size();

    std::array<float, maxChannels> own {}, required {};

    for (int n = 0; n < numSamples; ++n)
    {
        //----------------------------------------------------------------------
        // 検出 → 必要ゲイン（リニア）
        float minimum = 1.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            const float x = channels[c][n];
            float peak;

            if (truePeak)
            {
                // 区間 [i, i+1]（i = n − truePeakDelay）の最大（8倍補間＋放物線。`LimiterDsp::TruePeakDetector`）
                const float interval = detectors[(size_t) c].push (x);

                // **両側の区間の大きいほう**（ヘッダーの説明）
                peak = std::max (interval, previousIntervalPeak[(size_t) c]);
                previousIntervalPeak[(size_t) c] = interval;
            }
            else
            {
                peak = std::abs (x);
            }

            own[(size_t) c] = peak > ceiling ? ceiling / peak : 1.0f;
            minimum = std::min (minimum, own[(size_t) c]);
        }

        //----------------------------------------------------------------------
        // リンク：全チャンネルの最小値と自分の値を **dB で内分**（＝リニアでは幾何平均）。
        // 0％と100％は log が要らない。途中は速い近似で、**自分の必要ゲインより大きくしない**
        for (int c = 0; c < numChannels; ++c)
        {
            const float mine = own[(size_t) c];
            float r;

            if (link >= 1.0f || mine <= minimum)
                r = minimum;
            else if (link <= 0.0f)
                r = mine;
            else
                r = std::min (mine, LimiterDsp::fastExp2 (link * LimiterDsp::fastLog2 (minimum)
                                                          + (1.0f - link) * LimiterDsp::fastLog2 (mine)));

            required[(size_t) c] = r;
            history[(size_t) c][(size_t) historyPos] = required[(size_t) c];
        }

        if (++historyPos == historyLength)
            historyPos = 0;

        //----------------------------------------------------------------------
        // ゲイン経路 → リリース → 遅延した音声へ掛ける
        float worst = 1.0f;
        const float fadeT = fadeSamplesLeft > 0 ? 1.0f - (float) fadeSamplesLeft / (float) fadeSamplesTotal : 1.0f;

        for (int c = 0; c < numChannels; ++c)
        {
            float g = active[(size_t) c].push (required[(size_t) c]);

            if (fadeSamplesLeft > 0)
            {
                // **旧い経路と混ぜる**（どちらも天井を超えないので、混ぜても超えない）
                const float old = fading[(size_t) c].push (required[(size_t) c]);
                g = old + (g - old) * fadeT;
            }

            const float gain = releaseStep (release[(size_t) c], g);

            auto& line = audioDelay[(size_t) c];
            int readPos = audioPos - audioDelayLength;
            if (readPos < 0)
                readPos += audioCapacity;

            const float delayed = line[(size_t) readPos];
            line[(size_t) audioPos] = channels[c][n];

            channels[c][n] = dynamics.audition ? delayed * (1.0f - gain) : delayed * gain;
            worst = std::min (worst, gain);
        }

        if (++audioPos == audioCapacity)
            audioPos = 0;

        if (fadeSamplesLeft > 0)
            --fadeSamplesLeft;

        grTrace[(size_t) n] = worst;   // **リニアのまま**（dB へはエンジンが元のレートで1回だけ直す）
    }
}
