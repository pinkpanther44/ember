#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>

//==============================================================================
/**
    Manta Delay の音を作る部分（ディレイ設計書2章の`DelayEngine`）。

    ### Phase 1の範囲

    **Single Echo／Digital Cleanのみ**です（設計書8章）。
    キャラクター・フィルター・モジュレーション・マルチタップは入っていません。

    ### 補間はLagrange3rd

    設計書7章：**ディレイタイムを動かしている最中の劣化が少ない**ほうを選びます。
    Phase 1に変調はありませんが、**つまみを回すこと自体が変調**です——
    Thiranは内部状態を持つので、回している最中に音が濁ります。

    ### 時間の変え方は「テープ的」

    設計書5-1のTime Response Modeは**Phase 6の話**ですが、
    Phase 1でも**どちらかには必ずなります。** 何もしなければ
    「読み出し位置が飛ぶ＝ぶつ切れ」になるので、
    **目標値へ滑らかに寄せる**形（＝テープ的にピッチが動く）にしてあります。

    `juce::dsp::DelayLine`の`setDelay()`を毎サンプル少しずつ動かすだけです。
    **ブロックの頭で1回だけ動かすと、ブロックの境目で段差になります。**

    ### フィードバックは「書き戻す前」に読む

    ```
        出力 = ディレイライン.pop()
        ディレイライン.push (入力 + 出力 * feedback)
    ```

    **順番を逆にすると、1サンプルぶん短いディレイが挟まります**（無視できません）。
*/
class MantaDelayEngine
{
public:
    struct Settings
    {
        double delaySeconds = 0.375;
        float  feedback     = 0.35f;
        float  mix          = 0.30f;
        float  outputGain   = 1.0f;
    };

    void prepare (double sampleRateToUse, int maximumBlockSize, int numChannels,
                   double maximumDelaySeconds)
    {
        sampleRate = sampleRateToUse;

        const int maxSamples = juce::jmax (1, (int) std::ceil (maximumDelaySeconds * sampleRate) + 4);

        delayLine.setMaximumDelayInSamples (maxSamples);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maximumBlockSize);
        spec.numChannels = (juce::uint32) juce::jmax (1, numChannels);

        preparedChannels = (int) spec.numChannels;

        delayLine.prepare (spec);
        delayLine.reset();

        // **いまの目標値をそのまま現在値にする。** 0から寄せていくと、
        // 再生を始めた瞬間にディレイタイムが伸びていく音になります
        currentDelaySamples = -1.0;

        // 8.205：**寄せる速さはサンプルレートに依らないこと**（Phase 238）。
        // 「1秒で目標へ」を基準に係数を出します——固定値にすると、
        // 96kHzでは44.1kHzの半分の速さになります
        smoothingCoefficient = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));
    }

    void reset()
    {
        delayLine.reset();
        currentDelaySamples = -1.0;
    }

    void setSettings (const Settings& newSettings) { settings = newSettings; }

    /** その場で処理する（in-place）。**音のスレッドから呼ばれます**——
        確保も、ロックも、`juce::String`もここには書かないこと（9.4）。 */
    void process (juce::AudioBuffer<float>& buffer)
    {
        // **`juce::dsp::DelayLine`は`getNumChannels()`を持っていません**（Phase 238で踏んだ）。
        // `prepare()`へ渡した数を自分で覚えておきます
        const int numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
        const int numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0 || sampleRate <= 0.0)
            return;

        const double targetSamples = juce::jlimit (1.0,
                                                    (double) delayLine.getMaximumDelayInSamples() - 2.0,
                                                    settings.delaySeconds * sampleRate);

        if (currentDelaySamples < 0.0)
            currentDelaySamples = targetSamples;   // 最初の1回だけ、いきなり合わせる

        const float feedback = juce::jlimit (0.0f, 0.99f, settings.feedback);
        const float wet = juce::jlimit (0.0f, 1.0f, settings.mix);
        const float dry = 1.0f - wet;
        const float gain = settings.outputGain;

        for (int i = 0; i < numSamples; ++i)
        {
            // **毎サンプル寄せる。** ブロックの頭で1回だけ動かすと、
            // ブロックの境目が段差になって「プチッ」と鳴ります
            currentDelaySamples += (targetSamples - currentDelaySamples) * smoothingCoefficient;

            delayLine.setDelay ((float) currentDelaySamples);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = buffer.getWritePointer (channel);

                const float input = data[i];

                // **読んでから書く**（上の説明）
                const float delayed = delayLine.popSample (channel);

                delayLine.pushSample (channel, input + delayed * feedback);

                data[i] = (input * dry + delayed * wet) * gain;
            }
        }

        // 表示用。**音のスレッドから書いて、画面が読むだけ**なので`atomic`
        displayDelaySeconds.store (currentDelaySamples / sampleRate);
    }

    /** 画面がいま出すべきディレイタイム（寄せている最中の値）。 */
    double getDisplayDelaySeconds() const { return displayDelaySeconds.load(); }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;

    Settings settings;

    double sampleRate = 44100.0;
    double currentDelaySamples = -1.0;
    double smoothingCoefficient = 0.001;
    int preparedChannels = 0;

    std::atomic<double> displayDelaySeconds { 0.375 };
};
