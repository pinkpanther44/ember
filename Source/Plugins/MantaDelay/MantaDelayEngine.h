#pragma once

#include <juce_dsp/juce_dsp.h>

#include "MantaDelayCharacter.h"   // 8.208：音色モデル（Phase 240）
#include "MantaDelayDucker.h"      // 8.212：ダッキング（Phase 242）
#include "MantaDelayFilter.h"      // 8.210：ループ内フィルター（Phase 242）
#include "MantaDelayLfo.h"         // 8.211：LFO（Phase 242）
#include "MantaDelayTaps.h"        // 8.214：マルチタップ（Phase 243）

#include <array>
#include <atomic>

//==============================================================================
/**
    Manta Delay の音を作る部分（ディレイ設計書2章の`DelayEngine`）。

    ### いまの範囲（設計書8章）

    **エンジンは1つ**です。デュアルエンジン・リバースは入っていません。

    | Phase | 入っているもの |
    |---|---|
    | 1 | ディレイライン、テンポシンク、Feedback／Mix |
    | 2 | キャラクター（`MantaDelayCharacter`） |
    | 3 | ループ内フィルター、LFO、ダッキング |
    | 4 | マルチタップ（`MantaDelayTaps`） |
    | **5（いまここ）** | **1サンプルずつ外から回せる口**（`beginSample()`ほか。8.218） |

    ### 8.214：色を付ける道が2本あります（Phase 243）

    **Phase 3までは1本でした。** マルチタップが入って分かれます——
    **鳴らすほう**（タップを足し合わせたもの）と**戻すほう**（パターンの終わり）は、
    もう同じ信号ではないからです（タップのLevelやPanは戻すほうに掛けません）。

    ```
                    ┌── タップを足す ──→ 色を付ける（鳴らす道）──→ 出口
        ディレイライン ┤
                    └── パターンの終わり → 色を付ける（戻す道）──→ × feedback → push()
    ```

    **タップ1本・step 1・Level 100%・真ん中**なら、2本の道は**同じ信号を同じ係数で**
    通るので、**Phase 3と同じ音**が出ます（Analog BBDのノイズだけは別々に湧きます）。

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

    ### 1サンプルのあいだに何回読んでもよい

    `juce::dsp::DelayLine`の`popSample (ch, delay, updateReadPointer)`は、
    **`updateReadPointer`を`false`にすれば読むだけ**です。
    タップの本数ぶん読んで、**最後の1回だけ`true`**にします。

    > **`Lagrange3rd`だからできます。** JUCEの補間のうち**`Thiran`は内部状態を持つ**ので、
    > 1サンプルに何度も呼ぶと状態が混ざります。`Lagrange3rd`は
    > バッファと位置だけから決まる純粋な計算です（Phase 238で選んだ理由がここでも効きました）。

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

        // 8.217：`mix`と`outputGain`はここから出ました（Phase 244）。
        // **エンジン共通**なので、混ぜるのも音量もプロセッサ側の仕事です

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

        // 8.214：Phase 4（Phase 243）
        MantaDelayTaps::Pattern taps;
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
        for (auto& processor : feedbackCharacters)
            processor.prepare (sampleRate);

        for (auto& processor : wetCharacters)   // 8.214：鳴らす道（Phase 243）
            processor.prepare (sampleRate);

        // 8.210〜8.212：Phase 3（Phase 242）。**係数はチャンネルで共有、状態はチャンネルごと**
        for (auto& filter : feedbackFilters)
            filter.reset();

        for (auto& filter : wetFilters)
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

        for (auto& processor : feedbackCharacters)
            processor.reset();

        for (auto& processor : wetCharacters)
            processor.reset();

        for (auto& filter : feedbackFilters)
            filter.reset();

        for (auto& filter : wetFilters)
            filter.reset();

        lfo.reset();
        ducker.reset();
    }

    void setSettings (const Settings& newSettings)
    {
        settings = newSettings;

        // **係数を出すのはここ（ブロックの頭）だけ。** 毎サンプル`std::exp()`を
        // 呼ぶと、それだけで無視できない時間になります
        for (auto& processor : feedbackCharacters)
            processor.setSettings (settings.character);

        for (auto& processor : wetCharacters)
            processor.setSettings (settings.character);

        // 8.210〜8.212：Phase 3（Phase 242）。**どれもブロックの頭で1回**
        filterDesign = MantaDelayFilter::design (settings.filter, sampleRate);

        lfo.setSettings (settings.lfoShape, settings.lfoRateHz, settings.lfoDepth);
        ducker.setSettings (settings.duckAmount, settings.duckAttackMs, settings.duckReleaseMs);

        //----------------------------------------------------------------------
        // 8.214：タップの係数もここで出します（Phase 243）。
        // **`getPanGains()`を毎サンプル呼ばないこと**——1ブロックに1回で足ります

        activeTaps = juce::jlimit (1, MantaDelayTaps::maxTaps, settings.taps.count);
        patternSteps = settings.taps.getPatternSteps();

        for (int tap = 0; tap < MantaDelayTaps::maxTaps; ++tap)
        {
            const auto& source = settings.taps.taps[(size_t) tap];

            tapSteps[(size_t) tap] = juce::jlimit (1, MantaDelayTaps::maxStep, source.step);

            const float level = juce::jlimit (0.0f, 1.0f, source.level);

            float left = 1.0f, right = 1.0f;
            MantaDelayTaps::getPanGains (source.pan, left, right);

            tapGains[(size_t) tap][0] = level * left;
            tapGains[(size_t) tap][1] = level * right;

            // **モノラルではパンを使いません**（行き先が無いので、Levelだけ）
            tapMonoGains[(size_t) tap] = level;
        }
    }

    /** 8.217：**ウェットだけを作ります**（Phase 244）。渡された`buffer`は
        入口の音で、**返るときには反復だけ**に置き換わっています。

        混ぜるのも出口の音量も、**プロセッサの仕事**です——
        エンジンごとに原音を混ぜてしまうと、Dualで**原音が2回足されます。**

        `dryMono`は**プラグインの入口の音**（左右をまとめたもの）。ダッキングが見ます——
        Seriesのとき、Bの入口はAの出口なので、**そこを見ると自分の反復で自分を絞ります。**

        **音のスレッドから呼ばれます**——確保も、ロックも、`juce::String`も
        ここには書かないこと（9.4）。 */
    void processWet (juce::AudioBuffer<float>& buffer, const float* dryMono)
    {
        const int numChannels = beginBlock (buffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            beginSample (dryMono != nullptr ? dryMono[i] : 0.0f);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* data = buffer.getWritePointer (channel);

                float wet = 0.0f, feedbackOut = 0.0f;

                readChannel (channel, wet, feedbackOut);

                // **1エンジンで完結するので、戻りは自分のもの**（交換しません）
                writeChannel (channel, data[i], feedbackOut);

                data[i] = wet;
            }
        }
    }

    //==========================================================================
    /**
        8.218：**1サンプルずつ外から回すための口**（Phase 245／仕様書3-1）。

        Ping-Pongとクロスフィードバックは、**Aの戻りがBの線へ入ります。**
        ブロックごとに`processWet()`を呼ぶ形では**間に合いません**——
        Aを1ブロック分回し終えてからBを回すと、Bが受け取るのは
        **1ブロック遅れたA**になり、ディレイタイムが勝手に伸びます。

        なので**プロセッサが1サンプルずつ回します**：

        ```
        for 各サンプル:
            A.beginSample();  B.beginSample();
            for 各チャンネル:
                A.readChannel (…);  B.readChannel (…);   ← 読むだけ（線は動かない）
                戻りを混ぜる
                A.writeChannel (…); B.writeChannel (…);  ← 書く
        ```

        **読むのが先、書くのが後**はPhase 1から変わりません（8.204）。
        2つのエンジンのあいだでも同じで、**両方読んでから両方書きます**——
        先にAへ書いてしまうと、Bが読むのは**もうAの新しい音が入った線**になります。
    */

    /** ブロックの頭で1回。**使えるチャンネル数**を返します（0なら回さないこと）。 */
    int beginBlock (int bufferChannels)
    {
        // **`juce::dsp::DelayLine`は`getNumChannels()`を持っていません**（Phase 238で踏んだ）。
        // `prepare()`へ渡した数を自分で覚えておきます
        blockChannels = juce::jmin (juce::jmin (bufferChannels, preparedChannels),
                                     (int) feedbackCharacters.size());

        if (blockChannels <= 0 || sampleRate <= 0.0)
            return 0;

        targetDelaySamples = juce::jlimit (1.0,
                                            (double) delayLine.getMaximumDelayInSamples() - 2.0,
                                            settings.delaySeconds * sampleRate);

        if (currentDelaySamples < 0.0)
            currentDelaySamples = targetDelaySamples;   // 最初の1回だけ、いきなり合わせる

        blockFeedback = juce::jlimit (0.0f, 0.99f, settings.feedback);
        blockFilterActive = filterDesign.active;
        blockFilterPost = settings.filter.post;

        return blockChannels;
    }

    /** 1サンプルにつき1回、**チャンネルより先に**。`dryMonoSample`はダッキングが見る原音。 */
    void beginSample (float dryMonoSample)
    {
        // **毎サンプル寄せる。** ブロックの頭で1回だけ動かすと、
        // ブロックの境目が段差になって「プチッ」と鳴ります
        currentDelaySamples += (targetDelaySamples - currentDelaySamples) * smoothingCoefficient;

        // 8.208：Wow/Flutterの揺れ（Phase 240）。
        // 8.211：LFOの揺れ（Phase 242）。**足す先は同じ**ですが、
        // Wow/Flutterはキャラクターのもの、LFOは効果として掛けるものです。
        //
        // **チャンネルごとに進めないこと**——左右で違う揺れになると定位が動きます。
        // ここで1回進めて、同じ値を両チャンネルへ使います。
        //
        // 8.214：**足す先はタップごとに増えました**（Phase 243）。
        // 進めるのは**やはりここで1回だけ**です
        const double modulationOffset = feedbackCharacters[0].advanceModulation()
                                          + lfo.advance();

        // 8.212：ダッキング（Phase 242）。**原音を見ます**——ディレイ音ではありません。
        //
        // 8.217：**その原音はプロセッサからもらいます**（Phase 244）。
        // Phase 4までは`buffer`から作っていましたが、Seriesではそこに
        // **Aの反復が入っている**ので、自分の出した音で自分を絞ることになります
        currentDuckGain = ducker.advance (dryMonoSample);

        // 8.214：**パターンの終わり**（Phase 243）。フィードバックはここから戻します
        currentPatternDelay = clampDelay ((double) patternSteps * currentDelaySamples + modulationOffset);

        for (int tap = 0; tap < activeTaps; ++tap)
        {
            // **揺れはパターン全体に同じだけ足します**（`step`倍しない）——
            // 倍にすると、後ろのタップほど大きく揺れて**パターンが伸び縮み**します
            currentTapDelays[(size_t) tap] = clampDelay ((double) tapSteps[(size_t) tap] * currentDelaySamples
                                                           + modulationOffset);
        }
    }

    /** チャンネル1つぶんを**読む**（線の読み出し位置はここで進みます）。

        `wetOut`は出口へ出す音、`feedbackOut`は**戻す道の音**です。
        交換するのは呼ぶ側の仕事——`feedbackOut`には`feedback`を掛けていません。 */
    void readChannel (int channel, float& wetOut, float& feedbackOut)
    {
        // 8.214：**タップを足し合わせる**（Phase 243）。
        // `updateReadPointer`を`false`にして読むだけなので、**何本でも読めます**
        float tapSum = 0.0f;

        for (int tap = 0; tap < activeTaps; ++tap)
        {
            const float tapGain = (blockChannels > 1) ? tapGains[(size_t) tap][(size_t) channel]
                                                       : tapMonoGains[(size_t) tap];

            if (tapGain == 0.0f)
                continue;   // 鳴らないタップは読まない（読んでも状態は動きません）

            tapSum += tapGain * delayLine.popSample (channel, (float) currentTapDelays[(size_t) tap], false);
        }

        // **読み出し位置を進めるのはここ1回だけ**（最後の1回を`true`にする）。
        // **読んでから書く**のはPhase 1から変わりません
        const float feedbackSource = delayLine.popSample (channel, (float) currentPatternDelay, true);

        {
            const bool filterActive = blockFilterActive;
            const bool filterPost = blockFilterPost;

            // 8.214：**色を付ける道は2本**（Phase 243。ヘッダの図）。
            //
            // 8.210：**フィルターもフィードバックの中**（Phase 242）。
            // Pre／Postはキャラクターとの前後です（仕様書5-2の`Position`）——
            // **Preは削ってから歪ませ、Postは歪ませてから削ります**
            //
            // 8.208：**キャラクターはフィードバックの中**（Phase 240）。
            // 反復するたびに掛かるので、1回目より2回目が暗く、汚れていきます。
            // **入口に1度だけ掛けると、何回反復しても同じ音**になります
            //
            // 8.212：**ダッキングは出口のウェットにだけ。** 戻すほうには掛けません——
            // 掛けると減衰の速さが入力の大きさで変わります（`MantaDelayDucker`）
            wetOut = colour (tapSum, wetFilters[(size_t) channel],
                              wetCharacters[(size_t) channel],
                              filterActive, filterPost) * currentDuckGain;

            feedbackOut = colour (feedbackSource, feedbackFilters[(size_t) channel],
                                   feedbackCharacters[(size_t) channel],
                                   filterActive, filterPost);
        }
    }

    /** チャンネル1つぶんを**書く**。

        `feedbackIn`は**交換を済ませた戻り**（`feedback`はここで掛けます）——
        Ping-Pongなら相手のもの、Dualなら自分と相手を混ぜたもの、Singleなら自分のもの。 */
    void writeChannel (int channel, float input, float feedbackIn)
    {
        delayLine.pushSample (channel, input + feedbackIn * blockFeedback);

        // 表示用。**音のスレッドから書いて、画面が読むだけ**なので`atomic`。
        // **チャンネルごとに書いても同じ値**なので、まとめずにここで済ませます
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
    /** 8.214：ディレイラインからはみ出さない位置へ収める（Phase 243）。

        **上も下も要ります。** 下は1サンプル（0だとディレイでなくなる）、
        上は`Lagrange3rd`が読む先（`delayInt + 3`）が入るぶんを残しておくこと。

        `step`を掛けると**簡単に上限を超えます**——Timeが4000msなら
        step 2で8000ms。そのときは**全部のタップが4000msへ寄ります**
        （目盛りが尽きた、ということ。黙って壊れるよりはよい）。 */
    double clampDelay (double samples) const
    {
        return juce::jlimit (1.0, (double) delayLine.getMaximumDelayInSamples() - 4.0, samples);
    }

    /** 8.214：フィルターとキャラクターを通す1本ぶん（Phase 243）。

        **鳴らす道と戻す道で同じ手順を使います**（1.27）——
        写しを作ると、片方だけPre／Postを直す日が来ます。 */
    float colour (float input, MantaBiquad::Biquad& filter,
                   MantaDelayCharacter::Processor& character,
                   bool filterActive, bool filterPost) const
    {
        float x = input;

        // **係数は1つを両方の道で使い回します**（`MantaBiquad`の決まり）。
        // 状態（`filter`）だけが道ごと・チャンネルごとです
        if (filterActive && ! filterPost)
            x = filter.process (x, filterDesign.coeffs);

        x = character.processSample (x);

        if (filterActive && filterPost)
            x = filter.process (x, filterDesign.coeffs);

        return x;
    }

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
    /** 8.214：**戻す道**（Phase 243）。Wow／Flutterを進めるのもこちらです。 */
    std::array<MantaDelayCharacter::Processor, 2> feedbackCharacters;

    /** 8.214：**鳴らす道**（Phase 243。ヘッダの図）。

        タップを足し合わせたものに掛かります。**戻す道とは別の状態が要ります**——
        入る信号が違うので（タップのLevelとPanは戻すほうに掛けません）、
        1つを共有すると**両方の信号が同じフィルターを通って混ざります。** */
    std::array<MantaDelayCharacter::Processor, 2> wetCharacters;

    /** 8.210：フィードバック内フィルター（Phase 242）。

        **係数は1つ、状態はチャンネルごと**（`MantaBiquad`の決まり）——
        同じ形を左右に掛けるのに、係数を2組作る理由はありません。
        一方、内部状態を共有すると**左右が混ざって定位が潰れます。**

        8.214：道が2本になったので、**状態は「道×チャンネル」で4つ**になりました
        （Phase 243）。係数はやはり1つです。 */
    MantaDelayFilter::Design filterDesign;
    std::array<MantaBiquad::Biquad, 2> feedbackFilters;
    std::array<MantaBiquad::Biquad, 2> wetFilters;

    /** 8.214：タップの係数（Phase 243）。**`setSettings()`で出して、毎サンプル読むだけ。** */
    int activeTaps = 1;
    int patternSteps = 1;
    std::array<int, (size_t) MantaDelayTaps::maxTaps> tapSteps { { 1 } };
    std::array<std::array<float, 2>, (size_t) MantaDelayTaps::maxTaps> tapGains {};
    std::array<float, (size_t) MantaDelayTaps::maxTaps> tapMonoGains {};

    /** 8.218：`beginBlock()`／`beginSample()`が置いて、`readChannel()`が読むもの
        （Phase 245）。**1サンプルずつ外から回せるようにするため**に持っています——
        以前はぜんぶ`processWet()`の中のローカル変数でした。 */
    int blockChannels = 0;
    double targetDelaySamples = 0.0;
    float blockFeedback = 0.0f;
    bool blockFilterActive = false;
    bool blockFilterPost = false;

    float currentDuckGain = 1.0f;
    double currentPatternDelay = 1.0;
    std::array<double, (size_t) MantaDelayTaps::maxTaps> currentTapDelays {};

    /** 8.211：LFO（Phase 242）。**1つだけ**——左右で同じ揺れを使います。 */
    MantaDelayLfo::Oscillator lfo;

    /** 8.212：ダッキング（Phase 242）。**1つだけ**——左右をまとめて見ます。 */
    MantaDelayDucker ducker;

    std::atomic<double> displayDelaySeconds { 0.375 };
    std::atomic<float> displayDuckReduction { 0.0f };
};
