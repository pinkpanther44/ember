#pragma once

#include <juce_dsp/juce_dsp.h>

#include <cmath>

//==============================================================================
/**
    8.208：**キャラクター（音色モデル）**（Phase 240／ディレイ設計書2章・4章）。

    ─────────────────────────────────────────────────────────────────────────
    なぜStrategyにするか
    ─────────────────────────────────────────────────────────────────────────

    設計書2章：**`CharacterProcessor`をStrategyパターンで抽象化する設計自体は
    難易度★★☆☆☆程度だが、後々キャラクターを追加しやすくなるメリットが大きい。**

    Phase 1では`Digital Clean`しか無かったので**入れていませんでした**。
    Phase 2で3つ増えるいまが、作るのに自然なところです——
    5つめを足すときに作り直さずに済みます。

    ─────────────────────────────────────────────────────────────────────────
    ライト版であること（設計書4章）
    ─────────────────────────────────────────────────────────────────────────

    ★4以上だったものを★3以下へ圧縮した版です。**落としたものも書いてあります**——
    あとで「本格版」にするときに、何が足りないのかが分かるように。

    | | 入れたもの | 落としたもの |
    |---|---|---|
    | **Analog BBD** | ディレイタイムが長いほどカットオフが下がる可変ローパス＋固定ノイズフロア | コンパンダ挙動の精密再現、非線形歪み |
    | **Tape Echo** | Wow（低速サイン）＋Flutter（軽いランダム）＋`tanh`の飽和1段 | ヘッドバンプEQ、リピートごとの高域減衰、ヒスノイズ、ヒステリシス |
    | **Lo-Fi** | ビット深度とサンプルレートの削減＋帯域制限 | — |

    ─────────────────────────────────────────────────────────────────────────
    どこで呼ばれるか
    ─────────────────────────────────────────────────────────────────────────

    **フィードバックループの中**です（仕様書3-2の信号フロー）。

    ```
    出力 = pop()
    push (入力 + キャラクター(出力) * feedback)
    ```

    つまり**反復するたびに掛かります**——1回目より2回目が暗く、汚れていく。
    これがディレイの「経年」の正体です。**入口に1度だけ掛けると、
    何回反復しても同じ音**になり、まるで違う聞こえ方になります。

    ─────────────────────────────────────────────────────────────────────────
    音のスレッドから呼ばれます
    ─────────────────────────────────────────────────────────────────────────

    確保も、ロックも、`juce::String`も書かないこと（9.4）。
    **状態はチャンネルごとに持ちます**——1つを共有すると、左右が混ざります。
*/
namespace MantaDelayCharacter
{
    /** 音色モデルの種類。**並びを変えないこと**（保存されるのは番号です）。 */
    enum class Kind
    {
        digitalClean,   ///< 色付けなし（Phase 1から）
        analogBBD,      ///< バケツリレー素子風
        tapeEcho,       ///< テープループ風
        loFi,           ///< ビット／帯域を削る

        count
    };

    inline constexpr int getKindCount() { return (int) Kind::count; }

    /** 表示名。**ASCIIのみ**（つまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getKindNames()
    {
        return { "Digital Clean", "Analog BBD", "Tape Echo", "Lo-Fi" };
    }

    /** そのキャラクターで**効くつまみ**。画面のグレーアウトはこれを見ます。

        **1箇所で決めること**（1.27）。画面と音で別々に判断すると、
        **触れるのに効かないつまみ**や、その逆ができます。 */
    struct Capabilities
    {
        bool drive = false;    ///< 飽和・歪み
        bool tone = false;     ///< 高域の落とし方
        bool wow = false;      ///< 低速の揺れ
        bool flutter = false;  ///< 高速の揺れ
    };

    inline Capabilities getCapabilities (Kind kind)
    {
        switch (kind)
        {
            case Kind::digitalClean:
                return {};   // **何も効きません。** これが「色付けなし」の意味です

            case Kind::analogBBD:
                // 設計書4-1のライト版：可変ローパスと固定ノイズのみ。
                // **Wow／Flutterは入れていません**（コンパンダ挙動を落としたのと同じ理由で、
                // BBDの揺れは実機でもテープほど目立ちません）
                return { false, true, false, false };

            case Kind::tapeEcho:
                return { true, true, true, true };

            case Kind::loFi:
                return { true, true, false, false };

            case Kind::count:
            default:
                return {};
        }
    }

    //==========================================================================
    /** 画面と音の両方が読む設定。 */
    struct Settings
    {
        Kind kind = Kind::digitalClean;

        float drive = 0.0f;        ///< 0〜1
        float tone = 0.5f;         ///< 0〜1（大きいほど明るい）
        float wowRate = 0.7f;      ///< Hz
        float wowDepth = 0.0f;     ///< 0〜1
        float flutterRate = 8.0f;  ///< Hz
        float flutterDepth = 0.0f; ///< 0〜1

        /** いまのディレイタイム（秒）。**BBDのカットオフがこれで決まります。** */
        double delaySeconds = 0.375;
    };

    //==========================================================================
    /**
        1チャンネルぶんの状態と処理。

        **仮想関数にしていません。** 種類は4つしかなく、毎サンプル呼ばれる
        ところで仮想呼び出しを挟むと、**分岐より高く付きます**。
        Strategyの狙い（増やしやすさ）は`Kind`と`getCapabilities()`が持っていて、
        **足すときに触るのはこのファイルだけ**です。
    */
    class Processor
    {
    public:
        void prepare (double sampleRateToUse)
        {
            sampleRate = juce::jmax (8000.0, sampleRateToUse);
            reset();
        }

        void reset()
        {
            lowpassState = 0.0f;
            highpassState = 0.0f;
            heldSample = 0.0f;
            holdCounter = 0.0;
            wowPhase = 0.0;
            flutterPhase = 0.0;
            flutterTarget = 0.0f;
            flutterCurrent = 0.0f;
        }

        void setSettings (const Settings& newSettings)
        {
            settings = newSettings;

            //------------------------------------------------------------------
            // 8.208：**係数はブロックの頭で1回だけ出します**（Phase 240）。
            // 毎サンプル`std::exp()`を呼ぶと、それだけで無視できない時間になります

            const auto capabilities = getCapabilities (settings.kind);

            // ---- ローパス（BBDとLo-Fiの「暗さ」）
            {
                double cutoff = sampleRate * 0.5;   // 素通し

                if (settings.kind == Kind::analogBBD)
                {
                    // 設計書4-1：**ディレイタイムが長いほどカットオフが下がる。**
                    //
                    // 実機のBBDは段数が決まっているので、長いディレイを作るには
                    // クロックを落とすしかなく、**そのぶん再生できる高域が下がります。**
                    // ここでは「50msで8kHz、そこから時間に反比例」で近似します
                    cutoff = 8000.0 * (0.05 / juce::jmax (0.001, settings.delaySeconds));
                    cutoff = juce::jlimit (700.0, 16000.0, cutoff);
                }
                else if (settings.kind == Kind::loFi)
                {
                    cutoff = 1200.0;   // 電話風の帯域制限（仕様書4章）
                }

                if (capabilities.tone)
                {
                    // Toneで上下に振れる（0.5で上の値どおり）
                    const double scale = std::pow (4.0, (double) settings.tone * 2.0 - 1.0);
                    cutoff = juce::jlimit (200.0, sampleRate * 0.45, cutoff * scale);
                }

                lowpassCoefficient = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                                * cutoff / sampleRate));
            }

            // ---- ハイパス（Lo-Fiの「細さ」）。**BBDには掛けません**
            {
                const double cutoff = (settings.kind == Kind::loFi) ? 300.0 : 5.0;

                highpassCoefficient = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                                 * cutoff / sampleRate));
            }

            // ---- ビット深度とサンプルレートの削減（Lo-Fi）
            if (settings.kind == Kind::loFi)
            {
                // Driveで削り具合が変わる（0で12bit・素のレート、1で6bit・1/8）
                const float bits = juce::jmap (settings.drive, 12.0f, 6.0f);

                quantiseStep = 2.0f / std::pow (2.0f, bits);
                holdIncrement = juce::jmap (settings.drive, 1.0f, 0.125f);
            }
            else
            {
                quantiseStep = 0.0f;
                holdIncrement = 1.0f;
            }

            // ---- 飽和
            //
            // 8.209：**Driveが0なら、飽和そのものを通しません**（Phase 241）。
            // `g=1`でも`tanh(x)`は波形を丸めます。**0は「何もしない」であるべき**です
            driveGain = (capabilities.drive && settings.drive > 0.0001f)
                          ? juce::jmap (settings.drive, 1.0f, 12.0f)
                          : 0.0f;   // 0＝素通し（下の`processSample()`が見ています）

            // 8.209：**戻す量は`1/g`。`1/tanh(g)`ではありません**（Phase 241/本人の報告）。
            //
            // `tanh(g·x)`の**小さい信号に対する増幅は`g`**です
            // （`tanh(y) ≈ y - y³/3`なので、原点の傾きが`g`）。
            // `tanh`が頭を押さえるのは**大きくなってから**で、静かなところは素通しで`g`倍。
            //
            // `1/tanh(g)`で戻すと、`g=4.85`のとき`tanh(4.85)≈0.9999`なので
            // **ほとんど戻していないことになり、フィードバックの一周が4.85倍**になります。
            // 静かな反復がひと回りごとに膨らみ、`tanh`の頭打ちに達したところで
            // **飽和したまま鳴り続けます**——本人の報告「Driveを上げると綺麗に聞こえない」の正体です。
            //
            // **一周の利得は`feedback`を超えてはいけません。**
            driveCompensation = (driveGain > 0.0f) ? 1.0f / driveGain : 1.0f;

            // ---- 揺れ
            wowIncrement = (double) settings.wowRate / sampleRate;
            flutterIncrement = (double) settings.flutterRate / sampleRate;

            wowEnabled = capabilities.wow && settings.wowDepth > 0.0f;
            flutterEnabled = capabilities.flutter && settings.flutterDepth > 0.0f;
        }

        /** ディレイタイムに足す揺れ（サンプル数）。**毎サンプル進めます。**

            **チャンネルごとに呼ばないこと**——左右で違う揺れになると、
            定位が動いて気持ち悪くなります。ブロックの中で1回だけ進めて、
            **同じ値を両チャンネルへ**使います。 */
        double advanceModulation()
        {
            if (! wowEnabled && ! flutterEnabled)
                return 0.0;

            double offset = 0.0;

            if (wowEnabled)
            {
                wowPhase += wowIncrement;

                if (wowPhase >= 1.0)
                    wowPhase -= 1.0;

                // **深さはミリ秒で決めます。** 割合にすると、
                // 長いディレイほど揺れが大きくなって使いものになりません
                const double depthSamples = settings.wowDepth * 0.004 * sampleRate;

                offset += std::sin (wowPhase * juce::MathConstants<double>::twoPi) * depthSamples;
            }

            if (flutterEnabled)
            {
                flutterPhase += flutterIncrement;

                if (flutterPhase >= 1.0)
                {
                    flutterPhase -= 1.0;

                    // **ランダムは折り返しのときだけ引く。** 毎サンプル引くと
                    // 揺れではなく雑音になります
                    flutterTarget = random.nextFloat() * 2.0f - 1.0f;
                }

                // 目標へ寄せる（角の取れた揺れになる）
                flutterCurrent += (flutterTarget - flutterCurrent) * 0.02f;

                const double depthSamples = settings.flutterDepth * 0.0006 * sampleRate;

                offset += flutterCurrent * depthSamples;
            }

            return offset;
        }

        /** フィードバックループの中で、反復1回ぶんに掛かる処理。 */
        float processSample (float input)
        {
            if (settings.kind == Kind::digitalClean)
                return input;

            float x = input;

            // ---- 飽和（Tape・Lo-Fi）
            if (driveGain > 0.0f)
                x = std::tanh (x * driveGain) * driveCompensation;

            // ---- ビット深度とサンプルレートの削減（Lo-Fi）
            if (quantiseStep > 0.0f)
            {
                holdCounter += holdIncrement;

                if (holdCounter >= 1.0)
                {
                    holdCounter -= 1.0;
                    heldSample = std::round (x / quantiseStep) * quantiseStep;
                }

                x = heldSample;
            }

            // ---- ローパス（BBDの暗さ、Lo-Fiの帯域制限）
            lowpassState += (x - lowpassState) * lowpassCoefficient;
            x = lowpassState;

            // ---- ハイパス（Lo-Fiの細さ）。**直流も落とします**
            highpassState += (x - highpassState) * highpassCoefficient;
            x -= highpassState;

            // ---- ノイズフロア（BBD。設計書4-1の「固定レベル」）
            if (settings.kind == Kind::analogBBD)
                x += (random.nextFloat() * 2.0f - 1.0f) * 0.00035f;

            return x;
        }

    private:
        Settings settings;

        double sampleRate = 44100.0;

        float lowpassCoefficient = 1.0f;
        float highpassCoefficient = 0.001f;
        float lowpassState = 0.0f;
        float highpassState = 0.0f;

        float quantiseStep = 0.0f;
        float holdIncrement = 1.0f;
        double holdCounter = 0.0;
        float heldSample = 0.0f;

        float driveGain = 0.0f;   // 0＝飽和を通さない（8.209）
        float driveCompensation = 1.0f;

        bool wowEnabled = false;
        bool flutterEnabled = false;
        double wowIncrement = 0.0;
        double flutterIncrement = 0.0;
        double wowPhase = 0.0;
        double flutterPhase = 0.0;
        float flutterTarget = 0.0f;
        float flutterCurrent = 0.0f;

        juce::Random random;
    };
}
