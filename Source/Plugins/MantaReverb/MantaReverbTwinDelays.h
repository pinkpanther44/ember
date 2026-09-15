#pragma once

#include "../MantaDelay/MantaDelayEngine.h"

#include <array>
#include <cmath>

//==============================================================================
/**
    8.247：**Twin Delays**（Phase 257／リバーブ仕様書4章・設計書4章）。

    ─────────────────────────────────────────────────────────────────────────
    ディレイプラグインの中身を、そのまま使います（設計書3章）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**ディレイプラグインのDelayEngineを流用しそのまま採用**（★3）。

    仕様書2-1の言うとおり、これは**リバーブではありません**——
    「リバーブというよりディレイ的な機能」。部屋の寸法も初期反射もありません。

    ```
        入力（左右をまとめて）
          │
          ├→ Engine A（Time）      ──→ 左
          │      ↕ Cross（戻りを入れ替える）
          └→ Engine B（Time×比率） ──→ 右
    ```

    ─────────────────────────────────────────────────────────────────────────
    `MantaDelayEngine`が**1サンプルずつ回せる形**になっているから、使えます
    ─────────────────────────────────────────────────────────────────────────

    ディレイのPhase 5b（8.218）で`beginSample()`／`readChannel()`／`writeChannel()`へ
    割ったのは、**クロスフィードバックのため**でした。
    そのときの形が、そのままここで要ります——
    **AとBが1本の輪でつながっている**ので、ブロックごとには回せません。

    > **借りるほうは何も足していません。** キャラクター・フィルター・LFO・
    > ダッキング・タップ・Freeze・Reverse・Diffuseは**全部既定のまま**（＝素通し）。
    > 使うのは`delaySeconds`と`feedback`だけです。

    ─────────────────────────────────────────────────────────────────────────
    入口は**まとめます**
    ─────────────────────────────────────────────────────────────────────────

    AとBを左右へ振り切るので、**ステレオのまま入れても左右の情報は残りません**
    （ディレイのPing-Pongと同じ話。8.218の`collapsesInputToMono()`）。
    左右そろえて入れたほうが、1回目の反復が片側だけ欠けることがありません。

    ─────────────────────────────────────────────────────────────────────────
    戻りは**足さずに混ぜる**
    ─────────────────────────────────────────────────────────────────────────

    8.218で引いた線をそのまま持ってきています：

    ```
        toA = fbA × (1 − cross) + fbB × cross
    ```

    `自分 + cross × 相手`にすると、`cross`を上げたぶん**一周の利得が増えます**。
    混ぜなら`max(a, b)`を超えないので、**どこまで回しても`Feedback`の上限が効きます。**

    ### `Cross`が効かない条件

    8.225でディレイに書いたのと同じです——**AとBが同じなら、入れ替えても何も起きません。**
    ここでは`Spread`が2本の時間を必ず違わせるので、**0にしない限りは効きます。**
*/
class MantaReverbTwinDelays
{
public:
    /** `Time`の上限（秒）。**確保の長さはこれで決まります。** */
    static constexpr double maxTimeSeconds = 1.0;
    static constexpr double minTimeSeconds = 0.001;

    /** `Feedback`の上限。ディレイと同じ（`MantaDelayParams::maxFeedback`と揃えること）。 */
    static constexpr float maxFeedback = 0.95f;

    struct Settings
    {
        double timeSeconds = 0.12;
        float feedback = 0.40f;

        /** 0＝そのまま、1＝戻りを完全に入れ替える（ピンポン）。 */
        float cross = 0.0f;

        /** 2本の間隔（0〜1）。`ratioFor()`を通してBの時間になります。 */
        float spread = 0.5f;
    };

    /** `Spread`（0〜1）を、**Bの時間のAに対する比率**へ。

        **単純な比にしないこと。** `1/2`や`2/3`だと、数回めの反復で
        AとBが同じ時刻に重なり、**2本あるのに1本に聞こえます**
        （8.220でディフューザーの段の長さに書いたのと同じ話）。

        0.5のとき`0.587`——3/5にも4/7にも当たりません。 */
    static double ratioFor (float spread)
    {
        return 0.83 * std::pow (0.5, (double) juce::jlimit (0.0f, 1.0f, spread));
    }

    void prepare (double sampleRateToUse, int maximumBlockSize)
    {
        // **借りる側も確保はいちばん長いときで**（9.4）。
        // `Time`を回すたびに取り直すと、音のスレッドで確保が起きます
        for (auto& engine : engines)
            engine.prepare (sampleRateToUse, maximumBlockSize, 1, maxTimeSeconds);

        reset();
    }

    void reset()
    {
        for (auto& engine : engines)
            engine.reset();
    }

    void setSettings (const Settings& newSettings)
    {
        settings = newSettings;

        cross = juce::jlimit (0.0f, 1.0f, settings.cross);

        const double time = juce::jlimit (minTimeSeconds, maxTimeSeconds, settings.timeSeconds);
        const double ratio = ratioFor (settings.spread);

        // **借りる側の設定は、この2つだけ触ります。** 残りは既定のまま＝素通し
        MantaDelayEngine::Settings engineSettings;
        engineSettings.feedback = juce::jlimit (0.0f, maxFeedback, settings.feedback);

        engineSettings.delaySeconds = time;
        engines[0].setSettings (engineSettings);

        engineSettings.delaySeconds = juce::jmax (minTimeSeconds, time * ratio);
        engines[1].setSettings (engineSettings);
    }

    /** ブロックの頭で1回。**`beginBlock()`を呼ばないと`readChannel()`が何も返しません。** */
    void beginBlock()
    {
        // **1チャンネルで回します**（入口はまとめる。上の説明）
        for (auto& engine : engines)
            activeChannels = engine.beginBlock (1);
    }

    /** 1サンプルぶん。**Aが左、Bが右**。 */
    void processSample (float inputLeft, float inputRight, float& outputLeft, float& outputRight)
    {
        if (activeChannels <= 0)
        {
            outputLeft = 0.0f;
            outputRight = 0.0f;
            return;
        }

        const float input = 0.5f * (inputLeft + inputRight);

        // ダッキングは使わないので、原音は渡しません（0でよい）
        engines[0].beginSample (0.0f);
        engines[1].beginSample (0.0f);

        // 8.218：**両方読んでから、両方書くこと。**
        // 先にAへ書くと、Bが読むのは「今のサンプルが入ったあとの線」になります
        float wetA = 0.0f, feedbackA = 0.0f;
        float wetB = 0.0f, feedbackB = 0.0f;

        engines[0].readChannel (0, wetA, feedbackA);
        engines[1].readChannel (0, wetB, feedbackB);

        const float keep = 1.0f - cross;

        engines[0].writeChannel (0, input, feedbackA * keep + feedbackB * cross);
        engines[1].writeChannel (0, input, feedbackB * keep + feedbackA * cross);

        outputLeft = wetA;
        outputRight = wetB;
    }

private:
    /** **2本とも`MantaDelayEngine`**（設計書3章の「流用しそのまま採用」）。 */
    std::array<MantaDelayEngine, 2> engines;

    Settings settings;
    float cross = 0.0f;
    int activeChannels = 0;
};
