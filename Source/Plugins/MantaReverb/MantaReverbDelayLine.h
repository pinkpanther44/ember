#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <vector>

//==============================================================================
/**
    8.240：**整数長のディレイライン**（Phase 254／リバーブ設計書2章）。

    リバーブの中で使うディレイは、ディレイプラグインのものとは要るものが違います。

    | | ディレイ（`juce::dsp::DelayLine`） | ここ |
    |---|---|---|
    | 長さ | **実数**（つまみで連続的に動く。補間が要る） | **整数**（設計で決めた固定値） |
    | 本数 | 1〜2本 | **FDNで4本＋オールパス＋初期反射** |
    | 読む回数 | 1サンプルに複数回（マルチタップ） | 1本につき1回 |

    **補間を持ちません。** FDNのライン長は`Size`で変わりますが、
    変わるのは**ブロックの頭で作り直すとき**だけで、鳴っている最中には動きません。
    補間を入れると、要らない低域の落ち込みと計算が付いてきます。

    > **`Size`を回すと長さが変わります**（＝`resize()`が走ります）。
    > 中身は消さずに持ち越しますが、**長さが変わった瞬間に音は飛びます。**
    > リバーブの寸法を変えるとは本来そういうことで、隠すとかえって不自然です
    > （8.229のルーティング切替と同じ扱い。設計書4-5）。

    ### 確保は`prepare()`だけ

    9.4：**`processBlock()`では確保しない。** `prepare()`で
    **いちばん長い場合**を取っておき、`setLength()`は取った中で読む位置を変えるだけです。
*/
class MantaReverbDelayLine
{
public:
    /** `maxLengthSamples`ぶんを確保します。**これ以上は伸びません。** */
    void prepare (int maxLengthSamples)
    {
        capacity = juce::jmax (1, maxLengthSamples);
        buffer.assign ((size_t) capacity, 0.0f);
        length = capacity;
        writeIndex = 0;
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    /** いま使う長さ（サンプル）。**`prepare()`で取った範囲に収めます。** */
    void setLength (int newLength)
    {
        const int wanted = juce::jlimit (1, capacity, newLength);

        if (wanted == length)
            return;

        length = wanted;

        // **書く位置は中へ戻すこと。** 縮めたときに範囲の外を指したままだと、
        // 次の`push()`が確保の外を書きます
        if (writeIndex >= length)
            writeIndex = 0;
    }

    int getLength() const { return length; }

    /** いちばん古いものを返してから、新しいものを書く。**FDNのライン1本ぶん。** */
    float processSample (float input)
    {
        const float output = buffer[(size_t) writeIndex];

        buffer[(size_t) writeIndex] = input;

        if (++writeIndex >= length)
            writeIndex = 0;

        return output;
    }

    /** 書くだけ（初期反射のように、**1本を何度も読む**とき）。 */
    void push (float input)
    {
        buffer[(size_t) writeIndex] = input;

        if (++writeIndex >= length)
            writeIndex = 0;
    }

    /** `delaySamples`だけ前に書いたもの。**`push()`と組で使います。**

        `delaySamples`は1〜`getLength()`。外れた値は内側へ丸めます——
        **`Size`を縮めた直後**に、前の長さのままのタップが残ることがあるためです。 */
    float readAt (int delaySamples) const
    {
        const int back = juce::jlimit (1, length, delaySamples);

        int index = writeIndex - back;

        if (index < 0)
            index += length;

        return buffer[(size_t) index];
    }

    /** 8.250：**小数の位置で読む**（Phase 260／`Random Hall`の揺れ）。

        **読む位置が動くのはここだけ**——他のアルゴリズムは整数のまま読みます
        （ヘッダの「補間を持ちません」）。

        ─────────────────────────────────────────────────────────────────
        8.252：**線形ではなくラグランジュ3次**（測って入れ替えました）
        ─────────────────────────────────────────────────────────────────

        はじめは線形補間でした。**輪の中で何千回も通る**ので、
        1回ぶんではごくわずかな高域の丸まりが積み上がって、
        **`Modulation`を1%上げただけで3dB下がりました**（測定値）。

        しかも下がり方は深さに依りません——**小数部の平均が0.5**になるのは
        深さがいくつでも同じだからです。つまみの0と1%のあいだに段差ができる、
        いちばん質の悪い形でした。

        ラグランジュ3次は**状態を持たず**（同じ線を何度読んでも構わない）、
        振幅特性がずっと平らです。**ディレイプラグインが`Lagrange3rd`を
        選んでいるのと同じ理由**です（8.207）。

        > **`|H| ≤ 1`は保たれます。** ラグランジュの補間フィルターは
        > 真ん中の区間（ここでは`p0`と`p1`のあいだ）で最大平坦かつ`1`を超えません。
        > FDNの輪に入れても、**一周の利得は`g`のまま**です。 */
    float readAtFractional (double delaySamples) const
    {
        const double clamped = juce::jlimit (1.0, (double) length, delaySamples);

        const int whole = (int) clamped;
        const float f = (float) (clamped - whole);

        // `p0`と`p1`のあいだを読みます（`pBefore`と`pAfter`は形を決めるため）
        const float pBefore = readAt (juce::jmax (1, whole - 1));
        const float p0 = readAt (whole);
        const float p1 = readAt (juce::jmin (length, whole + 1));
        const float pAfter = readAt (juce::jmin (length, whole + 2));

        return pBefore * (-f * (f - 1.0f) * (f - 2.0f) / 6.0f)
             + p0      * ((f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f)
             + p1      * (-(f + 1.0f) * f * (f - 2.0f) / 2.0f)
             + pAfter  * ((f + 1.0f) * f * (f - 1.0f) / 6.0f);
    }

private:
    std::vector<float> buffer;
    int capacity = 1;
    int length = 1;
    int writeIndex = 0;
};
