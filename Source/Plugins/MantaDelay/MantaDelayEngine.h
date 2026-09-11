#pragma once

#include <juce_dsp/juce_dsp.h>

#include "MantaDelayCharacter.h"   // 8.208：音色モデル（Phase 240）
#include "MantaDelayDucker.h"      // 8.212：ダッキング（Phase 242）
#include "MantaDelayFilter.h"      // 8.210：ループ内フィルター（Phase 242）
#include "MantaDelayLfo.h"         // 8.211：LFO（Phase 242）

#include <array>
#include <atomic>

//==============================================================================
/**
    Manta Delay の音を作る部分（ディレイ設計書2章の`DelayEngine`）。

    ### いまの範囲（設計書8章）

    **Single Echo**です。マルチタップ・デュアルエンジン・リバースは入っていません。

    | Phase | 入っているもの |
    |---|---|
    | 1 | ディレイライン、テンポシンク、Feedback／Mix |
    | 2 | キャラクター（`MantaDelayCharacter`） |
    | **3（いまここ）** | ループ内フィルター、LFO、ダッキング |

    ### フィードバックループの順番

    仕様書3-2の信号フローに沿っています（**ダッキングだけ外しました**。理由は
    `MantaDelayDucker`の頭に書いてあります）。

    ```
        pop()
          ↓  ┌─ Pre（既定）
        フィルター
          ↓
        キャラクター（飽和・帯域・ビット・Wow/Flutter）
          ↓  └─ Post
        フィルター
          ↓
        × feedback → push()
    ```

    ### 補間はLagrange3rd

    設計書7章：**ディレイタイムを動かしている最中の劣化が少ない**ほうを選びます。
    Phase 1に変調はありませんでしたが、**つまみを回すこと自体が変調**です——
    Thiranは内部状態を持つので、回している最中に音が濁ります。
    （Phase 2でWow/Flutter、Phase 3でLFOが入り、**選んでおいて正解でした。**）

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

        // 8.208：Phase 2（キャラクター）
        MantaDelayCharacter::Settings character;

        //----------------------------------------------------------------------
        // 8.210〜8.212：Phase 3（Phase 242）

        MantaDelayFilter::Settings filter;

        MantaDelayLfo::Shape lfoShape = MantaDelayLfo::Shape::sine;
        float lfoRateHz = 0.5f;
        float lfoDepth  = 0.0f;

        float duckAmount    = 0.0f;
        float duckAttackMs  = 10.0f;
        float duckReleaseMs = 200.0f;
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

        // 8.208：**チャンネルごとに状態を持ちます**（Phase 240）。
        // 1つを共有すると、フィルターの内部状態が混ざって左右が潰れます
        for (auto& processor : characters)
            processor.prepare (sampleRate);

        // 8.210〜8.212：Phase 3（Phase 242）。**係数はチャンネルで共有、状態はチャンネルごと**
        for (auto& filter : feedbackFilters)
            filter.reset();

        filterDesign = {};

        lfo.prepare (sampleRate);
        ducker.prepare (sampleRate);

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

        for (auto& processor : characters)
            processor.reset();

        for (auto& filter : feedbackFilters)
            filter.reset();

        lfo.reset();
        ducker.reset();
    }

    void setSettings (const Settings& newSettings)
    {
        settings = newSettings;

        // **係数を出すのはここ（ブロックの頭）だけ。** 毎サンプル`std::exp()`を
        // 呼ぶと、それだけで無視できない時間になります
        for (auto& processor : characters)
            processor.setSettings (settings.character);

        // 8.210〜8.212：Phase 3（Phase 242）。**どれもブロックの頭で1回**
        filterDesign = MantaDelayFilter::design (settings.filter, sampleRate);

        lfo.setSettings (settings.lfoShape, settings.lfoRateHz, settings.lfoDepth);
        ducker.setSettings (settings.duckAmount, settings.duckAttackMs, settings.duckReleaseMs);
    }

    /** その場で処理する（in-place）。**音のスレッドから呼ばれます**——
        確保も、ロックも、`juce::String`もここには書かないこと（9.4）。 */
    void process (juce::AudioBuffer<float>& buffer)
    {
        // **`juce::dsp::DelayLine`は`getNumChannels()`を持っていません**（Phase 238で踏んだ）。
        // `prepare()`へ渡した数を自分で覚えておきます
        const int numChannels = juce::jmin (juce::jmin (buffer.getNumChannels(), preparedChannels),
                                             (int) characters.size());
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

        const bool filterActive = filterDesign.active;
        const bool filterPost = settings.filter.post;
        const float invChannels = 1.0f / (float) numChannels;

        for (int i = 0; i < numSamples; ++i)
        {
            // **毎サンプル寄せる。** ブロックの頭で1回だけ動かすと、
            // ブロックの境目が段差になって「プチッ」と鳴ります
            currentDelaySamples += (targetSamples - currentDelaySamples) * smoothingCoefficient;

            // 8.208：Wow/Flutterの揺れ（Phase 240）。
            // 8.211：LFOの揺れ（Phase 242）。**足す先は同じ**ですが、
            // Wow/Flutterはキャラクターのもの、LFOは効果として掛けるものです。
            //
            // **チャンネルごとに進めないこと**——左右で違う揺れになると定位が動きます。
            // ここで1回進めて、同じ値を両チャンネルへ使います
            const double modulated = currentDelaySamples
                                        + characters[0].advanceModulation()
                                        + lfo.advance();

            delayLine.setDelay ((float) juce::jlimit (1.0,
                                                      (double) delayLine.getMaximumDelayInSamples() - 2.0,
                                                      modulated));

            // 8.212：ダッキング（Phase 242）。**原音を見ます**——ディレイ音ではありません。
            //
            // **書き込む前に、全チャンネルぶん読んでおくこと。** 下の輪は
            // `data[i]`を上書きするので、そのあとで読むと**混ぜたあとの値**を見ます。
            // 左右をまとめた1つの値で動かします（片側だけ絞ると定位が動きます）
            float monoInput = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                monoInput += buffer.getReadPointer (channel)[i];

            const float duckGain = ducker.advance (monoInput * invChannels);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = buffer.getWritePointer (channel);

                const float input = data[i];

                // **読んでから書く**（上の説明）
                const float delayed = delayLine.popSample (channel);

                float wetSample = delayed;

                // 8.210：**フィルターもフィードバックの中**（Phase 242）。
                // Pre／Postはキャラクターとの前後です（仕様書5-2の`Position`）——
                // **Preは削ってから歪ませ、Postは歪ませてから削ります**
                if (filterActive && ! filterPost)
                    wetSample = feedbackFilters[(size_t) channel].process (wetSample, filterDesign.coeffs);

                // 8.208：**キャラクターはフィードバックの中**（Phase 240）。
                // 反復するたびに掛かるので、1回目より2回目が暗く、汚れていきます。
                // **入口に1度だけ掛けると、何回反復しても同じ音**になります
                wetSample = characters[(size_t) channel].processSample (wetSample);

                if (filterActive && filterPost)
                    wetSample = feedbackFilters[(size_t) channel].process (wetSample, filterDesign.coeffs);

                delayLine.pushSample (channel, input + wetSample * feedback);

                // 8.212：**ダッキングは出口のウェットにだけ。** 戻すほう（`pushSample`）には
                // 掛けません——掛けると減衰の速さが入力の大きさで変わります（`MantaDelayDucker`）
                data[i] = (input * dry + wetSample * wet * duckGain) * gain;
            }
        }

        // 表示用。**音のスレッドから書いて、画面が読むだけ**なので`atomic`
        displayDelaySeconds.store (currentDelaySamples / sampleRate);
        displayDuckReduction.store (ducker.getReductionForDisplay());
    }

    /** 画面がいま出すべきディレイタイム（寄せている最中の値）。 */
    double getDisplayDelaySeconds() const { return displayDelaySeconds.load(); }

    /** 8.212：いまダッキングがどれだけ絞っているか（0〜1。Phase 242）。

        **これが無いと、AttackとReleaseを回しても何が起きているか見えません。**
        絞られたぶんは「音が小さい」だけなので、耳だけでは掛かり具合が読めません。 */
    float getDisplayDuckReduction() const { return displayDuckReduction.load(); }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine;

    Settings settings;

    double sampleRate = 44100.0;
    double currentDelaySamples = -1.0;
    double smoothingCoefficient = 0.001;
    int preparedChannels = 0;

    /** 8.208：チャンネルごとの音色モデル（Phase 240）。

        **`std::array`で持ちます。** `prepare()`で数が決まり、
        それ以降は増減しません——音のスレッドで確保しないため（9.4）。
        ステレオまでなので2つで足ります（`isBusesLayoutSupported()`が
        モノラルかステレオしか通しません）。 */
    std::array<MantaDelayCharacter::Processor, 2> characters;

    /** 8.210：フィードバック内フィルター（Phase 242）。

        **係数は1つ、状態はチャンネルごと**（`MantaBiquad`の決まり）——
        同じ形を左右に掛けるのに、係数を2組作る理由はありません。
        一方、内部状態を共有すると**左右が混ざって定位が潰れます。** */
    MantaDelayFilter::Design filterDesign;
    std::array<MantaBiquad::Biquad, 2> feedbackFilters;

    /** 8.211：LFO（Phase 242）。**1つだけ**——左右で同じ揺れを使います。 */
    MantaDelayLfo::Oscillator lfo;

    /** 8.212：ダッキング（Phase 242）。**1つだけ**——左右をまとめて見ます。 */
    MantaDelayDucker ducker;

    std::atomic<double> displayDelaySeconds { 0.375 };
    std::atomic<float> displayDuckReduction { 0.0f };
};
