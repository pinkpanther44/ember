#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

//==============================================================================
/**
    8.219：**リバース**（Phase 246／ディレイ仕様書5-5）。

    ─────────────────────────────────────────────────────────────────────────
    どこに入れるか——**ディレイラインへ書く直前**
    ─────────────────────────────────────────────────────────────────────────

    ```
        入口 →[リバース]→ ディレイライン → タップ → 出口
                              ↑
                          戻り（× feedback）
    ```

    **戻りは通しません。** 通すと反復のたびに裏返って、
    **1回目は逆、2回目は順、3回目は逆**……と落ち着きません。

    入口で裏返しておくと、**「弾いたフレーズが逆さまになって返ってくる」**という、
    いちばん期待される形になります。キャラクターもフィルターもタップも、
    **裏返ったあとの音に対して今までどおり効きます**（何も足していません）。

    ─────────────────────────────────────────────────────────────────────────
    窓の長さは**ディレイタイムと同じ**
    ─────────────────────────────────────────────────────────────────────────

    仕様書5-5は「一定サイズのバッファ」としか書いていません。**つまみは足しませんでした。**

    ディレイタイムと同じにすると、**逆さまのフレーズが、反復と反復のあいだにちょうど収まります。**
    Syncを入れていればその音価ぶん——**拍に乗ります。**

    > つまみを足すと、**「Timeと合っていないと気持ち悪い」値を自分で探すこと**になります。
    > 要ると分かってから足せばよい（8.209で引いた線と同じ考え）。

    ─────────────────────────────────────────────────────────────────────────
    継ぎ目は**必ず鳴ります**——だからフェードを掛ける
    ─────────────────────────────────────────────────────────────────────────

    窓の終わりで読み出しが先頭へ飛ぶので、**そこで波形が途切れます。**
    何もしないと窓のたびに「プツッ」と鳴ります。

    **窓の両端を短くフェードします**（`fadeSeconds`）。
    逆さまの音は元々「立ち上がりが無く、終わりが急」なので、
    **フェードがあっても不自然になりません**——むしろそれらしく聞こえます。
*/
class MantaDelayReverse
{
public:
    /** 窓の上限（秒）。**ディレイタイムの上限と同じ**にしてあります。 */
    static constexpr double maxWindowSeconds = 4.0;

    /** 継ぎ目のフェード。**短すぎると鳴り、長すぎると頭が痩せます。** */
    static constexpr double fadeSeconds = 0.005;

    void prepare (double sampleRateToUse, int numChannels)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);

        const size_t size = (size_t) std::ceil (maxWindowSeconds * sampleRate) + 4;

        for (int channel = 0; channel < (int) buffers.size(); ++channel)
            buffers[(size_t) channel].assign (size, 0.0f);

        preparedChannels = juce::jlimit (1, (int) buffers.size(), numChannels);

        fadeSamples = juce::jmax (1, (int) (fadeSeconds * sampleRate));

        reset();
    }

    void reset()
    {
        for (auto& buffer : buffers)
            std::fill (buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        readOffset = 0;
    }

    /** ブロックの頭で1回。`windowSeconds`はディレイタイムそのもの。 */
    void setSettings (bool shouldBeActive, double windowSeconds)
    {
        active = shouldBeActive;

        const int wanted = juce::jlimit (fadeSamples * 3,
                                          (int) buffers[0].size() - 4,
                                          (int) std::round (windowSeconds * sampleRate));

        // **窓の長さを途中で変えないこと。** 読み書きの位置が窓を跨いでいる最中に
        // 変えると、**飛んだ先が前の窓の途中**になって鳴ります。
        // 次に頭へ戻るときまで待ちます
        pendingWindow = wanted;

        if (windowSamples <= 0)
            windowSamples = wanted;   // 最初の1回だけ、いきなり合わせる
    }

    bool isActive() const { return active; }

    /** 1サンプルぶん。**裏返した音を返します**（`active`が`false`なら素通し）。 */
    float processSample (int channel, float input)
    {
        auto& buffer = buffers[(size_t) juce::jlimit (0, preparedChannels - 1, channel)];

        // **止めていても書き続けます。** 書くのをやめると、
        // 入れた瞬間に**空っぽの窓**から読み出すことになって無音になります
        buffer[(size_t) writeIndex] = input;

        float output = input;

        if (active)
        {
            // 窓の中を**後ろから前へ**辿ります
            const int offset = juce::jlimit (0, windowSamples - 1, readOffset);

            int index = writeIndex - (windowSamples - offset);

            while (index < 0)
                index += (int) buffer.size();

            output = buffer[(size_t) (index % (int) buffer.size())] * getFade (offset);
        }

        // **位置を進めるのは、チャンネルをまたいで1回だけ**にしたいところですが、
        // ここはチャンネルごとに呼ばれます。呼ぶ側が最後のチャンネルで
        // `advance()`を呼ぶ形にしてあります（`MantaDelayEngine`）

        return output;
    }

    /** 1サンプル進める。**全チャンネルを処理したあとに1回だけ**呼ぶこと。 */
    void advance()
    {
        writeIndex = (writeIndex + 1) % (int) buffers[0].size();

        if (++readOffset >= windowSamples)
        {
            readOffset = 0;

            // **窓の長さを変えるのはここ**（頭へ戻るとき）。上の説明
            windowSamples = juce::jmax (1, pendingWindow);
        }
    }

private:
    /** 窓の両端のフェード（0〜1）。 */
    float getFade (int offset) const
    {
        if (windowSamples <= fadeSamples * 2)
            return 1.0f;

        if (offset < fadeSamples)
            return (float) offset / (float) fadeSamples;

        const int fromEnd = windowSamples - 1 - offset;

        if (fromEnd < fadeSamples)
            return (float) fromEnd / (float) fadeSamples;

        return 1.0f;
    }

    double sampleRate = 44100.0;
    int preparedChannels = 2;

    std::array<std::vector<float>, 2> buffers;

    bool active = false;

    int writeIndex = 0;
    int readOffset = 0;
    int windowSamples = 0;
    int pendingWindow = 0;
    int fadeSamples = 1;
};
