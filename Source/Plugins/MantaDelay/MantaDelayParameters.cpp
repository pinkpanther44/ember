#include "MantaDelayParameters.h"
#include "MantaDelayCharacter.h"   // 8.208：キャラクターの名前（Phase 240）
#include "MantaDelayFilter.h"      // 8.210：フィルターの形の名前（Phase 242）
#include "MantaDelayLfo.h"         // 8.211：波形の名前（Phase 242）
#include "MantaDelayTaps.h"        // 8.214：タップの本数と目盛りの上限（Phase 243）
#include "MantaDelayRouting.h"     // 8.217：ルーティングの名前（Phase 244）

#include <cmath>

namespace MantaDelayParams
{
    namespace
    {
        /** 対数の目盛り（Manta EQ・Manta Compと同じ作り）。
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

        juce::String formatHz (float value, int) { return juce::String (value, 2) + " Hz"; }

        juce::String formatPercent (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        }

        /** ディレイタイム。**桁で小数点を変えます**——
            「1000.00 ms」は読みにくく、「3.00 ms」は3msと区別が要ります */
        juce::String formatMs (float value, int)
        {
            return juce::String (value, value < 10.0f ? 2 : (value < 100.0f ? 1 : 0)) + " ms";
        }

        //----------------------------------------------------------------------
        // 8.210：Phase 3で足したもの（Phase 242）

        /** フィルターの周波数。**`formatHz`とは別にします**——
            あちらはLFOの0.05〜20Hz用で小数2桁、こちらは20〜20000Hz用です
            （「2000.00 Hz」は読めません）。

            **出し方はManta EQに合わせます**（Phase 208／本人の要望）——
            小数点以下は出さず、kHz表記も使いません。`ValueEntrySlider`は
            数字を打ち込めるので、**打った値と出てくる値が食い違うと気持ち悪い**
            （"3 kHz"だと3150Hzも同じ表示になります）。 */
        juce::String formatFilterHz (float value, int)
        {
            return juce::String (juce::roundToInt (value)) + " Hz";
        }

        juce::String formatQ (float value, int) { return juce::String (value, 2); }

        //----------------------------------------------------------------------
        // 8.214：Phase 4で足したもの（Phase 243）

        /** タップのパン。**数字ではなく「L50 / C / R50」で見せます**——
            本体のパンと同じ考え方です（`ValueEntrySlider::DisplayUnit::panPercent`、
            Manta EQの`formatPan`）。**小数で出しても手では合わせられません。** */
        juce::String formatPan (float value, int)
        {
            const int percent = juce::roundToInt (std::abs (value) * 100.0f);

            if (percent == 0)
                return "C";

            return (value < 0.0f ? "L" : "R") + juce::String (percent);
        }
    }

    //==========================================================================

    juce::String engineParamId (int engine, const char* suffix)
    {
        // 8.217：**エンジンAは素通し**（Phase 244）。付け替えると
        // 保存済みのプロジェクトがディレイの設定を全部見失います（ヘッダの説明）
        return engine == 0 ? juce::String (suffix) : "b_" + juce::String (suffix);
    }

    juce::String tapParamId (int engine, int tapIndex, const char* suffix)
    {
        // **nは1始まり**（画面の表示と揃える。Manta EQの`bandParamId()`と同じ）
        const auto name = "tap" + juce::String (tapIndex + 1) + "_" + suffix;

        return engine == 0 ? name : "b_" + name;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        const auto floatAttributes = [] (auto function)
        {
            return juce::AudioParameterFloatAttributes().withStringFromValueFunction (function);
        };

        // 8.214：**IDは`juce::String`で受けます**（Phase 243）。タップのIDは
        // `tapParamId()`が組み立てて返すので、`const char*`だと`toRawUTF8()`を
        // 通すことになり、**寿命の切れたものを渡しかねません**
        auto addFloat = [&layout, &floatAttributes] (const juce::String& id, const juce::String& name,
                                                      juce::NormalisableRange<float> range,
                                                      float defaultValue, auto function)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, floatAttributes (function)));
        };

        //----------------------------------------------------------------------
        // 8.217：**1エンジンぶんを1回だけ書いて、2回呼びます**（Phase 244）。
        //
        // 写しを2つ作ると、片方だけ範囲や既定値を直す日が来ます（1.27）。
        //
        // ### 並びについて
        //
        // **エンジンAのぶんは、1つも動かしていません。** `mix`と`outputGain`は
        // エンジン共通ですが、**Aの並びの真ん中（5・6番目）に居ます**——
        // 後ろへ動かすと番号がずれて、**保存済みのオートメーションが別のつまみに付きます**（9.5）。
        // なので`engine == 0`のときだけ、その場所で足します。
        //
        // 足したぶん（`engineLevel`／`enginePan`）は**Aの並びの末尾**、
        // `routingMode`はそのあと、Bのぶんは全部そのあとです。

        const auto addEngineParameters = [&] (int engine)
        {
            const auto id = [engine] (const char* suffix) { return engineParamId (engine, suffix); };

            // 画面には出ませんが、ホストのオートメーション一覧には出ます——
            // **AとBを見分けられること**
            const auto label = [engine] (const juce::String& text)
            {
                return engine == 0 ? text : "B " + text;
            };

            //------------------------------------------------------------------
            // 5-1：基本

            // **対数で配ります。** 20msと40msの差は聞いて分かりますが、
            // 2000msと2020msは同じに聞こえます——線形だと短い側が使いものになりません
            addFloat (id (timeMs), label ("Time"),
                       makeLogRange ((float) minDelayMs, (float) maxDelayMs), 375.0f, formatMs);

            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id (sync), 1 }, label ("Sync"), true));

            // **`AudioParameterChoice`にすること。** floatで番号を持つと、
            // オートメーションで中間の値が入ったときにどの音価か決まりません
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id (syncDivision), 1 }, label ("Division"),
                getSyncDivisionNames(), (int) SyncDivision::eighthDotted));

            addFloat (id (feedback), label ("Feedback"), { 0.0f, maxFeedback, 0.001f }, 0.35f, formatPercent);

            // **エンジン共通の2つ**（上の説明。Aの並びの中に居ます）
            if (engine == 0)
            {
                addFloat (mix, "Mix", { 0.0f, 1.0f, 0.001f }, 0.30f, formatPercent);
                addFloat (outputGain, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb);
            }

        //----------------------------------------------------------------------
            // 8.208：Phase 2（キャラクター。設計書4章のライト版）

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id (character), 1 }, label ("Character"),
                MantaDelayCharacter::getKindNames(), 0));   // 既定はDigital Clean

            // **DriveとToneは掛け持ちです。** Tapeでは飽和、Lo-Fiでは削り具合——
            // 「どのキャラクターでも、同じつまみが同じ役割の位置にある」ほうが覚えやすく、
            // 効かないキャラクターでは画面側がグレーアウトします
            // （効くかどうかの判断は`MantaDelayCharacter::getCapabilities()`ただ1つ。1.27）
            // 8.209：**既定は0%**（Phase 241/本人の指定）。
            // 色を付けるのは**頼まれてから**——キャラクターを選んだだけで
            // 音が歪むと、そのキャラクター本来の音が分かりません
            addFloat (id (drive), label ("Drive"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);
            addFloat (id (tone), label ("Tone"), { 0.0f, 1.0f, 0.001f }, 0.5f, formatPercent);

            addFloat (id (wowRate), label ("Wow Rate"), { 0.1f, 5.0f, 0.01f }, 0.7f, formatHz);
            addFloat (id (wowDepth), label ("Wow"), { 0.0f, 1.0f, 0.001f }, 0.25f, formatPercent);

            addFloat (id (flutterRate), label ("Flutter Rate"), { 3.0f, 20.0f, 0.1f }, 8.0f, formatHz);
            addFloat (id (flutterDepth), label ("Flutter"), { 0.0f, 1.0f, 0.001f }, 0.20f, formatPercent);

            //------------------------------------------------------------------
            // 8.210：Phase 3のフィルター（仕様書5-2）
            //
            // **既定は`Off`。** 8.209で決めたことをそのまま当てています——
            // 挿しただけで音が変わるべきではありません

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id (filterType), 1 }, label ("Filter"),
                MantaDelayFilter::getTypeNames(), 0));

            // **周波数は対数**（EQと同じ。200Hzと400Hzの差は聞いて分かりますが、
            // 10kHzと10.2kHzは同じに聞こえます）
            addFloat (id (filterFreq), label ("Freq"), makeLogRange (20.0f, 20000.0f), 2000.0f, formatFilterHz);

            // **Qの上は8まで。** これ以上は共振の山が高くなりすぎて、
            // 1倍に戻したときに**山以外がほとんど消えます**（`MantaDelayFilter`）
            addFloat (id (filterQ), label ("Q"), makeLogRange (0.2f, 8.0f), 0.707f, formatQ);

            // Bellのときだけ効きます（`MantaDelayFilter::usesGain()`）
            addFloat (id (filterGain), label ("Gain"), { -18.0f, 18.0f, 0.1f }, 0.0f, formatDb);

            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id (filterPost), 1 }, label ("Filter Post"), false));

            //------------------------------------------------------------------
            // 8.211：Phase 3のLFO（仕様書5-3）

            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id (lfoShape), 1 }, label ("LFO Shape"),
                MantaDelayLfo::getShapeNames(), 0));

            // **0.05Hzまで下げられます**（20秒で1周）。ゆっくり揺らすと
            // テープの伸び縮みのように聞こえます
            addFloat (id (lfoRate), label ("LFO Rate"), makeLogRange (0.05f, 20.0f), 0.5f, formatHz);

            // **既定は0%**（挿しただけで揺れない）
            addFloat (id (lfoDepth), label ("LFO"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);

            //------------------------------------------------------------------
            // 8.212：Phase 3のダッキング（仕様書5-4）

            // **既定は0%**（挿しただけで絞られない）
            addFloat (id (duckAmount), label ("Duck"), { 0.0f, 1.0f, 0.001f }, 0.0f, formatPercent);

            // **AttackとReleaseは対数**（1msと3msの差は効きますが、
            // 180msと200msの差はほとんど分かりません）
            addFloat (id (duckAttack), label ("Attack"), makeLogRange (0.5f, 200.0f), 8.0f, formatMs);
            addFloat (id (duckRelease), label ("Release"), makeLogRange (20.0f, 2000.0f), 250.0f, formatMs);

            //------------------------------------------------------------------
            // 8.214：Phase 4のマルチタップ（仕様書5-6）
            //
            // **既定は1本・step 1・Level 100%・真ん中**——つまりPhase 3と同じ鳴り方です
            // （`MantaDelayTaps`のパンが真ん中でちょうど1倍なのはこのため）。
            // **挿しただけでは何も変わらない**（8.209で決めた線）

            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { id (tapCount), 1 }, label ("Taps"), 1, MantaDelayTaps::maxTaps, 1));

            for (int tap = 0; tap < MantaDelayTaps::maxTaps; ++tap)
            {
                const auto number = juce::String (tap + 1);

                // **`step`は整数**。floatで持つと、オートメーションで中間の値が入ったときに
                // どの目盛りか決まりません（`syncDivision`を`Choice`にしたのと同じ理由。8.207）
                layout.add (std::make_unique<juce::AudioParameterInt> (
                    juce::ParameterID { tapParamId (engine, tap, tapStep), 1 },
                    label ("Tap " + number + " Step"), 1, MantaDelayTaps::maxStep, tap + 1));

                // **1本目だけ100%、2本目から0%。** 本数を増やした瞬間に
                // 知らないタップが鳴り出すより、**自分で上げたものだけが鳴る**ほうがよい
                addFloat (tapParamId (engine, tap, tapLevel), label ("Tap " + number + " Level"),
                           { 0.0f, 1.0f, 0.001f }, tap == 0 ? 1.0f : 0.0f, formatPercent);

                addFloat (tapParamId (engine, tap, tapPan), label ("Tap " + number + " Pan"),
                           { -1.0f, 1.0f, 0.001f }, 0.0f, formatPan);
            }

            //------------------------------------------------------------------
            // 8.217：Phase 5で足したぶん（Phase 244）。**エンジンの並びの末尾**
            //
            // **既定は100%・真ん中**（Singleでは何も変わりません）

            addFloat (id (engineLevel), label ("Level"), { 0.0f, 1.0f, 0.001f }, 1.0f, formatPercent);
            addFloat (id (enginePan), label ("Pan"), { -1.0f, 1.0f, 0.001f }, 0.0f, formatPan);
        };

        //----------------------------------------------------------------------
        // **Aが先、`routingMode`、そのあとB**（上の説明）。この順は変えないこと

        addEngineParameters (0);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { routingMode, 1 }, "Routing",
            MantaDelayRouting::getModeNames(), 0));   // 既定はSingle＝Phase 4までと同じ

        addEngineParameters (1);

        return layout;
    }
}
