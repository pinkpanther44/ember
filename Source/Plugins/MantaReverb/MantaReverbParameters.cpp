#include "MantaReverbParameters.h"

#include "MantaReverbAlgorithm.h"
#include "MantaReverbFdn.h"
#include "MantaReverbRouting.h"
#include "MantaReverbTwinDelays.h"

#include <cmath>

namespace MantaReverbParams
{
    namespace
    {
        /** 対数の目盛り（Manta EQ・Comp・Delayと同じ作り）。
            端の値がぴったり出るように、skewではなく変換そのものを書いています。 */
        juce::NormalisableRange<float> makeLogRange (float minimum, float maximum)
        {
            const float logMin = std::log (minimum);
            const float logMax = std::log (maximum);

            return
            {
                minimum, maximum,
                [logMin, logMax] (float, float, float proportion)
                {
                    return std::exp (logMin + proportion * (logMax - logMin));
                },
                [logMin, logMax] (float, float, float value)
                {
                    return (std::log (juce::jmax (value, 1.0e-6f)) - logMin) / (logMax - logMin);
                },
                [] (float start, float end, float value) { return juce::jlimit (start, end, value); }
            };
        }

        juce::String formatDb (float value, int) { return juce::String (value, 1) + " dB"; }

        juce::String formatPercent (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        }

        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
        }

        /** 減衰時間。**桁で小数点を変えます**——
            「0.2 s」は読めますが、「10.00 s」は読みにくい。 */
        juce::String formatSeconds (float value, int)
        {
            return juce::String (value, value < 10.0f ? 2 : 1) + " s";
        }

        /** ダンピングの周波数。**Manta EQ・Delayに合わせて整数のHz**（8.210）——
            `ValueEntrySlider`は数字を打ち込めるので、
            打った値と出てくる値が食い違うと気持ち悪い（"6 kHz"だと6300Hzも同じ表示）。 */
        juce::String formatHz (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + " Hz";
        }

        /** 部屋の大きさ。**つまみは0〜1ですが、出すのは倍率**——
            「50%」より「1.00×」のほうが、何に対しての大きさか読めます。 */
        juce::String formatSize (float value, int)
        {
            return juce::String (sizeScaleFor (value), 2) + " x";
        }
    }

    //==========================================================================

    juce::String engineParamId (int engine, const char* suffix)
    {
        // **エンジン0は素通し**（ヘッダの説明）。Phase 5でBが`b_`になります
        return engine == 0 ? juce::String (suffix) : "b_" + juce::String (suffix);
    }

    float sizeScaleFor (float normalised)
    {
        // `0 → 0.5倍`、`0.5 → 1.0倍`、`1 → 2.0倍`。
        // **`MantaReverbFdn::maxSizeScale`を超えないこと**——確保の長さがそれで決まっています
        const float minimum = (float) (1.0 / MantaReverbFdn::maxSizeScale);
        const float maximum = (float) MantaReverbFdn::maxSizeScale;

        return minimum * std::pow (maximum / minimum, juce::jlimit (0.0f, 1.0f, normalised));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto floatAttributes = [] (auto function)
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction (function);
        };

        // **IDは`juce::String`で受けます**（`engineParamId()`が組み立てて返すため。8.214）
        auto addFloat = [&layout, &floatAttributes] (const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue, auto function)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, floatAttributes (function)));
        };

        //----------------------------------------------------------------------
        // **1エンジンぶんを1回だけ書きます**（Phase 5で2回呼びます）。
        //
        // 写しを2つ作ると、片方だけ範囲や既定値を直す日が来ます（1.27）。

        const auto addEngineParameters = [&] (int engine)
        {
            const auto id = [engine] (const char* suffix) { return engineParamId (engine, suffix); };

            // 画面には出ませんが、ホストのオートメーション一覧には出ます——
            // **AとBを見分けられること**（Phase 5から要ります）
            const auto label = [engine] (const juce::String& text)
            {
                return engine == 0 ? text : "B " + text;
            };

            // **0msから。** プリディレイを入れるかどうかは曲によります——
            // 挿しただけで原音が遅れるべきではありません（8.209の②「0は何もしない」）
            addFloat (id (predelay), label ("Predelay"),
                       { (float) minPredelayMs, (float) maxPredelayMs, 0.1f }, 0.0f, formatMs);

            // **対数で配ります。** 0.5秒と1秒の差は聞いて分かりますが、
            // 11秒と11.5秒は同じに聞こえます
            addFloat (id (decay), label ("Decay"),
                       makeLogRange ((float) minDecaySeconds, (float) maxDecaySeconds), 1.2f, formatSeconds);

            // **つまみは0〜1、出すのは倍率**（`sizeScaleFor()`）。
            // 既定の0.5がちょうど1.00倍です
            addFloat (id (size), label ("Size"), { 0.0f, 1.0f, 0.001f }, 0.5f, formatSize);

            addFloat (id (diffusion), label ("Diffusion"), { 0.0f, 1.0f, 0.001f }, 0.70f, formatPercent);

            //------------------------------------------------------------------
            // ダンピング（仕様書5章）。**周波数は対数**（EQと同じ）

            addFloat (id (highDampFreq), label ("HF Freq"),
                       makeLogRange (500.0f, 20000.0f), 6000.0f, formatHz);
            addFloat (id (highDampAmount), label ("HF Damp"), { 0.0f, 1.0f, 0.001f }, 0.45f, formatPercent);

            addFloat (id (lowDampFreq), label ("LF Freq"),
                       makeLogRange (20.0f, 1000.0f), 180.0f, formatHz);
            addFloat (id (lowDampAmount), label ("LF Damp"), { 0.0f, 1.0f, 0.001f }, 0.20f, formatPercent);

            //------------------------------------------------------------------

            addFloat (id (earlyLevel), label ("Early"), { 0.0f, 1.0f, 0.001f }, 0.60f, formatPercent);

            // **100%が素通し**（0%ではありません）。0で真ん中、200%で広がります
            addFloat (id (width), label ("Width"), { 0.0f, 2.0f, 0.001f }, 1.0f, formatPercent);
        };

        addEngineParameters (0);

        //----------------------------------------------------------------------
        // エンジン共通。**エンジンの並びの外へ置くこと**（ヘッダの説明）

        // **既定は25%。** 挿しただけで原音が遠くなるべきではありません
        addFloat (mix, "Mix", { 0.0f, 1.0f, 0.001f }, 0.25f, formatPercent);
        addFloat (outputGain, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb);

        //----------------------------------------------------------------------
        // **ここから下が「後から足したもの」**（Phase 2以降）。
        //
        // 意味の並びではエンジンの一部ですが、**番号の並びは末尾**です。
        // エンジンの塊へ入れると、前のPhaseで保存したプロジェクトの
        // `mix`と`outputGain`が別のつまみになります（9.5）。
        //
        // **Phase 5ではここも2回呼びます**（1エンジンぶんを1回だけ書く形は同じ）

        const auto addAppendedEngineParameters = [&] (int engine)
        {
            const auto id = [engine] (const char* suffix) { return engineParamId (engine, suffix); };

            const auto label = [engine] (const juce::String& text)
            {
                return engine == 0 ? text : "B " + text;
            };

            // 8.243：Phase 2（Phase 255）。
            // **`AudioParameterChoice`にすること。** floatで番号を持つと、
            // オートメーションで中間の値が入ったときにどれか決まりません
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id (algorithm), 1 }, label ("Algorithm"),
                MantaReverbAlgorithm::getKindNames(),
                (int) MantaReverbAlgorithm::Kind::room));   // **既定はPhase 1と同じ音**

            // 8.246：Phase 3（Phase 256）。**どちらも0.5が「表のまま」**——
            // Phase 2までと同じ音になります（ディレイで引いた線の③）
            addFloat (id (shape), label ("Shape"), { 0.0f, 1.0f, 0.001f }, 0.5f, formatPercent);
            addFloat (id (spread), label ("Spread"), { 0.0f, 1.0f, 0.001f }, 0.5f, formatPercent);

            // 8.247：Phase 4a（Phase 257）。**`Twin Delays`でしか効きません**。
            //
            // **対数で配ります**——20msと40msの差は聞いて分かりますが、
            // 900msと920msは同じに聞こえます（ディレイのTimeと同じ話）
            addFloat (id (twinTime), label ("Delay Time"),
                       makeLogRange ((float) (MantaReverbTwinDelays::minTimeSeconds * 1000.0),
                                      (float) (MantaReverbTwinDelays::maxTimeSeconds * 1000.0)),
                       120.0f, formatMs);

            addFloat (id (twinFeedback), label ("Delay Feedback"),
                       { 0.0f, MantaReverbTwinDelays::maxFeedback, 0.001f }, 0.40f, formatPercent);

            // **既定は0**（8.209の②「0は何もしない」）——挿しただけで跳ねません
            addFloat (id (twinCross), label ("Cross"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);

            // 8.248：Phase 4b（Phase 258）。**どれも既定はオフ**——
            // アルゴリズムを選んだだけで左右が入れ替わったりしません
            const auto addBool = [&layout] (const juce::String& boolId, const juce::String& name)
            {
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    juce::ParameterID { boolId, 1 }, name, false));
            };

            addBool (id (panMonoSum), label ("Mono Sum"));
            addBool (id (panInvertRight), label ("Invert Right"));
            addBool (id (panSwap), label ("Swap L/R"));

            // 8.249：Phase 5（Phase 259）。**そのエンジンの出口**（仕様書5章の`Level`）。
            //
            // **エンジンAのぶんもここへ足しています。** `addEngineParameters()`の
            // 末尾へ入れると、そこから後ろの番号が全部ずれて、
            // **Phase 1〜4で保存したオートメーションが別のつまみに付きます**（9.5）
            addFloat (id (engineLevel), label ("Level"), { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb);

            // 8.250〜8.251：Phase 6（Phase 260）。**どちらも既定は0**
            // （＝前のPhaseと同じ音。ディレイで引いた線の②と③）
            addFloat (id (modulation), label ("Modulation"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);
            addFloat (id (saturation), label ("Saturation"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);
        };

        addAppendedEngineParameters (0);

        //----------------------------------------------------------------------
        // 8.249：Phase 5（Phase 259）。**エンジン共通**なので、Bの塊より前へ

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { routingMode, 1 }, "Routing",
            MantaReverbRouting::getModeNames(),
            (int) MantaReverbRouting::Mode::single));   // **既定はPhase 4までと同じ音**

        //----------------------------------------------------------------------
        // 8.249：**エンジンB**（Phase 259）。ここから後ろは全部新しいので、
        // **並びは1エンジンぶんを2回呼ぶだけ**で済みます（1.27）

        addEngineParameters (1);
        addAppendedEngineParameters (1);

        return layout;
    }
}
