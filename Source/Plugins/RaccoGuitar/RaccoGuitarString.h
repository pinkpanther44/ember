#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

//==============================================================================
/**
    8.256：**弦1本の物理モデル**（Phase 264／Karplus-Strong 拡張版）。

    本人が別に作った `KSGuitar`（別リポジトリ）の `KSString` をそのまま移しました。
    **中身は変えていません**——向こうで音とチューニング精度を追い込んだものなので、
    移すときに手を入れると「どちらが正しいのか」が分からなくなります。

    ```
       励起 ─┬─▶[ ディレイライン ]─▶[ 分数遅延(1次オールパス) ]─┐
             │                                                  │
             └──────────[ ×decay ]◀─[ ループフィルタ(1次LPF) ]◀─┘
                                   │
                                   └─▶[ コムフィルタ ]─▶ 出力（ピックアップ位置）
    ```

    | 部品 | 何を決めるか |
    |---|---|
    | ディレイライン＋分数遅延 | **音程**（チューニング精度） |
    | ループフィルタ（`brightness`） | 倍音の減り方＝**音色の明るさ** |
    | `decay` | 減衰。**秒（T60）で指定**する（下） |
    | 出力のコムフィルタ | **ピックアップ位置**（ブリッジからのmm） |

    ### 減衰は「秒」で持つこと

    減衰係数はループ1周ごとに掛かるので、**係数を直に指定すると高い音ほど速く消えます**
    （E2とD6で1秒あたりのループ回数が約80倍違う）。周波数が決まるたびに

    ```
    decay = 0.001^(ループ遅延 / (T60 × サンプルレート))
    ```

    を逆算しており、**どの音程でも同じ秒数だけ鳴ります**。

    ### ループフィルタの位相遅延は都度計算する

    0.5サンプル固定と近似すると**高音ほどピッチが上ずります**（D6で+13.8cent）。
    `computeLoopFilterDelay()`で実際の位相遅延を引くと、全音域で0.5cent以内に入ります。

    ### インハーモニシティは入っていません

    1次オールパスチェーンによる分散を試したものの、**ギターの音域では効きませんでした**
    （基音〜低次倍音がサンプルレートに対して十分低域にあり、位相遅延がほぼ平坦）。
    係数を強めると高域で位相が巻き戻って逆方向に振れます。
    コードは残してありますが`dispersionAmount = 0`で**切ってあります**。
    やるならモード合成か、周波数依存の分数遅延で。
*/
class RaccoGuitarString
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        buffer.assign (bufferSize, 0.0f);
        pickupBuffer.assign (bufferSize, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        std::fill (pickupBuffer.begin(), pickupBuffer.end(), 0.0f);
        writePos = 0;
        apPrevIn = apPrevOut = 0.0f;
        lpState = 0.0f;

        for (auto& s : dispersionState)
            s = { 0.0f, 0.0f };
    }

    //==========================================================================
    /** スケール長（弦長）。mm単位。 */
    void setScaleLengthMm (float mm)
    {
        scaleLengthMm = std::clamp (mm, 300.0f, 1000.0f);
        updateDispersion();
    }

    float getScaleLengthMm() const { return scaleLengthMm; }

    //==========================================================================
    /** 周波数。ループ全体の遅延＝ディレイライン＋ループフィルタの位相遅延。 */
    void setFrequency (float freqHz)
    {
        frequency  = std::clamp (freqHz, 20.0f, (float) sampleRate * 0.45f);
        totalDelay = (float) sampleRate / frequency;

        updateDispersion();       // 音程が変わるとインハーモニシティも変わる

        // **ループフィルタの位相遅延を差し引くこと**（クラスの説明）。
        // 0.5サンプル固定にすると、高音ほどチューニングが狂う
        const float loopFilterDelay = computeLoopFilterDelay();
        float d = totalDelay - loopFilterDelay - dispersionDelay;

        intDelay = (int) std::floor (d);
        float frac = d - (float) intDelay;

        // オールパス補間の係数は frac が 0 に近いと不安定になる。
        // 整数部を1つ減らして frac を [0.1, 1.1) に収める
        if (frac < 0.1f)
        {
            --intDelay;
            frac += 1.0f;
        }
        intDelay = std::clamp (intDelay, 2, bufferSize - 2);

        apCoef = (1.0f - frac) / (1.0f + frac);

        updateLoopGain();
        updatePickupDelay();
    }

    void setBrightness (float b)
    {
        const float clamped = std::clamp (b, 0.05f, 0.999f);

        if (clamped != brightness)
        {
            brightness = clamped;

            // 位相遅延が変わるので、ディレイ長を計算し直す
            setFrequency (frequency);
        }
    }

    void setDecaySeconds (float seconds)
    {
        t60Seconds = std::clamp (seconds, 0.05f, 20.0f);
        updateLoopGain();
    }

    //==========================================================================
    /** ピックアップ位置（ブリッジからのmm）。

        実機のピックアップは**ブリッジから固定の距離**にあるので、フレットを押さえて
        実効弦長が短くなると「弦長に対する比率」が変わります。同じ音名でも
        **開放弦と押さえた音で音色が違う**、という挙動はここから出ます。 */
    void setPickupPositionMm (float mm)
    {
        pickupMm = std::clamp (mm, 5.0f, 400.0f);
        updatePickupDelay();
    }

    /** ピッキング位置（ブリッジからのmm）。 */
    void setPickPositionMm (float mm)
    {
        pickMm = std::clamp (mm, 5.0f, 400.0f);
    }

    /** いまの実効弦長（mm）。開放弦＝スケール長、1オクターブ上なら半分。 */
    float getEffectiveLengthMm() const
    {
        return scaleLengthMm * (openStringFreq / frequency);
    }

    /** 弦長に対するピッキング位置の比率（励起のコムフィルタ用）。 */
    float getPickPositionRatio() const
    {
        const float effLen = getEffectiveLengthMm();
        return std::clamp (pickMm / std::max (1.0f, effLen), 0.02f, 0.5f);
    }

    /** この弦の開放弦の周波数（実効弦長の基準）。 */
    void setOpenStringFrequency (float f) { openStringFreq = std::max (20.0f, f); }

    float getTotalDelay() const { return totalDelay; }

    //==========================================================================
    /** 1サンプル処理。**確保しないこと**（音のスレッドから毎サンプル呼ばれます）。 */
    float process (float excitation)
    {
        const float delayed = read (buffer, intDelay);

        // 分数遅延（1次オールパス補間）
        const float apOut = apCoef * (delayed - apPrevOut) + apPrevIn;
        apPrevIn  = delayed;
        apPrevOut = apOut;

        // インハーモニシティ（分散）。係数0のときは各段が単なる1サンプル遅延に
        // なってピッチがずれるので、**完全にバイパスすること**
        float dispersed = apOut;

        if (dispersionCoef != 0.0f)
        {
            for (int i = 0; i < numDispersionStages; ++i)
            {
                auto& s = dispersionState[(size_t) i];
                const float y = dispersionCoef * (dispersed - s.y) + s.x;
                s.x = dispersed;
                s.y = y;
                dispersed = y;
            }
        }

        // ループフィルタ：減衰付き1次ローパス
        lpState = brightness * dispersed + (1.0f - brightness) * lpState;
        const float loopOut = decay * lpState;

        write (buffer, excitation + loopOut);

        // ピックアップのコムフィルタ
        write (pickupBuffer, loopOut);
        const float out = loopOut - read (pickupBuffer, pickupDelaySamples);

        ++writePos;
        if (writePos >= bufferSize)
            writePos = 0;

        return out;
    }

private:
    //==========================================================================
    /** ループフィルタ（1次ローパス）の位相遅延を、基音の周波数について求める。

        ```
        H(z) = b / (1 - (1-b)z^-1)      b = brightness
        位相遅延 D(w) = -arg H(e^jw) / w
        ``` */
    float computeLoopFilterDelay() const
    {
        const float b = brightness;
        const float a = 1.0f - b;                       // フィードバック係数
        const float w = 2.0f * 3.14159265f * frequency / (float) sampleRate;

        // H(e^jw) = b / (1 - a·e^-jw)   分母 = (1 - a·cos w) + j(a·sin w)
        const float denRe = 1.0f - a * std::cos (w);
        const float denIm = a * std::sin (w);

        const float argH = -std::atan2 (denIm, denRe);   // arg H = -arg(分母)

        return (w > 1.0e-6f) ? (-argH / w) : 0.5f;
    }

    /** インハーモニシティ（分散）の更新。**いまは切ってあります**（クラスの説明）。 */
    void updateDispersion()
    {
        constexpr float refScaleMm = 648.0f;

        const float scaleRatio = refScaleMm / scaleLengthMm;
        const float freqRatio  = std::clamp (frequency / 220.0f, 0.5f, 2.0f);
        const float lengthNorm = std::clamp (totalDelay / 218.0f, 0.35f, 2.5f);

        const float strength = dispersionAmount * scaleRatio * scaleRatio
                                                * freqRatio * lengthNorm;

        dispersionCoef = std::clamp (-strength, -0.35f, 0.0f);

        if (dispersionCoef == 0.0f)
        {
            dispersionDelay = 0.0f;
        }
        else
        {
            const float perStage = (1.0f - dispersionCoef) / (1.0f + dispersionCoef);
            dispersionDelay = perStage * (float) numDispersionStages;
        }
    }

    void updateLoopGain()
    {
        const float exponent = totalDelay / (t60Seconds * (float) sampleRate);
        decay = std::pow (0.001f, exponent);
        decay = std::clamp (decay, 0.5f, 0.99999f);
    }

    /** mm指定の絶対位置を、いまの実効弦長に対する比率＝遅延サンプル数にする。 */
    void updatePickupDelay()
    {
        const float effLen = getEffectiveLengthMm();
        const float ratio  = std::clamp (pickupMm / std::max (1.0f, effLen), 0.02f, 0.5f);

        pickupDelaySamples = std::clamp ((int) (totalDelay * ratio), 1, bufferSize - 2);
    }

    float read (const std::vector<float>& buf, int delaySamples) const
    {
        int pos = writePos - delaySamples;

        while (pos < 0)
            pos += bufferSize;

        return buf[(size_t) pos];
    }

    void write (std::vector<float>& buf, float value)
    {
        buf[(size_t) writePos] = value;
    }

    static constexpr int bufferSize = 1 << 16;
    static constexpr int numDispersionStages = 12;

    /** インハーモニシティの強さ。**0＝無効**（クラスの説明の最後）。 */
    static constexpr float dispersionAmount = 0.0f;

    struct AllpassState { float x = 0.0f, y = 0.0f; };

    std::vector<float> buffer;
    std::vector<float> pickupBuffer;

    double sampleRate = 44100.0;
    int    writePos   = 0;

    float frequency  = 220.0f;
    float totalDelay = 100.0f;
    int   intDelay   = 100;
    float apCoef = 0.0f, apPrevIn = 0.0f, apPrevOut = 0.0f;

    float lpState    = 0.0f;
    float brightness = 0.5f;

    float t60Seconds = 3.0f;
    float decay      = 0.996f;

    // 弦の物理寸法
    float scaleLengthMm  = 648.0f;   // スケール長（25.5インチ相当）
    float openStringFreq = 82.41f;   // この弦の開放弦周波数
    float pickupMm       = 75.0f;    // ブリッジからのピックアップ距離
    float pickMm         = 110.0f;   // ブリッジからのピッキング距離

    // インハーモニシティ（分散）
    AllpassState dispersionState[numDispersionStages];
    float dispersionCoef  = 0.0f;
    float dispersionDelay = 0.0f;

    int pickupDelaySamples = 20;
};
