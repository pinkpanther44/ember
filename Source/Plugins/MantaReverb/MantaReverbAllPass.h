#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <vector>

//==============================================================================
/**
    8.240：**Schroeder型オールパス1段**（Phase 254／リバーブ設計書2章）。

    ディレイの`MantaDelayDiffuser`と同じ式ですが、**1段ずつ持てる形**にしてあります
    ——リバーブでは入口の拡散（直列4段）と**FDNのライン内**（1本に1段）の
    両方で使うので、4段ひとまとめの箱では足りません。

    ```
        v = x + g·buf
        y = buf − g·v
        buf ← v
    ```

    ### なぜオールパスなのか

    **振幅は全周波数で1倍**です（だからオールパス）。変わるのは位相だけ。

    > 8.220で引いた線：**形で解けるなら、補正を書かない。**
    > FDNのフィードバックの中に置いても、**一周の利得は増えません。**
    > 密度を上げる方法は他にもありますが（ライン数を増やす、など）、
    > どれも減衰の計算をやり直すことになります。オールパスは**ただ置けます。**

    ### `g`の上限

    `0.75`を超えると、段の長さそのものが「カチッ」と聞こえてきます
    （8.223でディレイのディフューザーを詰めたときと同じ）。
*/
class MantaReverbAllPass
{
public:
    /** 係数の上限。**これ以上は段の長さが聞こえます**（上の説明）。 */
    static constexpr float maxCoefficient = 0.75f;

    void prepare (int maxLengthSamples)
    {
        capacity = juce::jmax (1, maxLengthSamples);
        buffer.assign ((size_t) capacity, 0.0f);
        length = capacity;
        index = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        index = 0;
    }

    void setLength (int newLength)
    {
        const int wanted = juce::jlimit (1, capacity, newLength);

        if (wanted == length)
            return;

        length = wanted;

        if (index >= length)
            index = 0;
    }

    int getLength() const { return length; }

    void setCoefficient (float newCoefficient)
    {
        coefficient = juce::jlimit (0.0f, maxCoefficient, newCoefficient);
    }

    float processSample (float input)
    {
        const float delayed = buffer[(size_t) index];
        const float v = input + coefficient * delayed;

        buffer[(size_t) index] = v;

        if (++index >= length)
            index = 0;

        return delayed - coefficient * v;
    }

    /** 8.244：**段の途中を読む**（Phase 255／Dattorro型の出口タップ）。

        返すのは中の節（`v`）です——原論文が拾っているのはそこ。
        **`processSample()`と組で使います**（読む位置は書く位置から数えます）。 */
    float readAt (int delaySamples) const
    {
        const int back = juce::jlimit (1, length, delaySamples);

        int position = index - back;

        if (position < 0)
            position += length;

        return buffer[(size_t) position];
    }

private:
    std::vector<float> buffer;
    int capacity = 1;
    int length = 1;
    int index = 0;
    float coefficient = 0.5f;
};
