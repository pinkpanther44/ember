#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

//==============================================================================
/**
    8.212：**ダッキング**（Phase 242／ディレイ仕様書5-4）。

    ─────────────────────────────────────────────────────────────────────────
    何をするもの
    ─────────────────────────────────────────────────────────────────────────

    仕様書5-4：**原音の入力に反応してウェット信号を抑制し、
    フレーズの隙間だけディレイが聴こえるようにする。**

    歌っているあいだはディレイが引っ込み、**切れたところで出てくる**。
    ディレイを深く掛けても言葉が埋もれないので、**掛けた量より効きます。**

    ─────────────────────────────────────────────────────────────────────────
    **ループの中には入れません**（仕様書の図とは違います）
    ─────────────────────────────────────────────────────────────────────────

    仕様書3-2の信号フローでは`… → Ducking → Feedback Gain`の順、
    つまり**フィードバックの中**に描かれています。ここでは**出口のウェットにだけ**掛けます。

    理由：ループの中で絞ると、**減衰の速さが入力の大きさで変わります。**
    大きく歌っているあいだは反復が短く、静かなところでは長く——
    「Feedbackをこれだけ回したから、これくらい残る」が**成り立たなくなります。**

    仕様書の狙い（**隙間だけ聴こえる**）は、出口で絞るだけで足ります。
    ディレイラインの中身は満額のまま残るので、**入力が切れた瞬間に出てきます**——
    ループの中で絞ると、絞っていたあいだのぶんは**消えてしまって戻ってきません。**

    > 一周の利得に手を出すものは`feedback`だけにしておくこと（8.209・8.210と同じ線）。

    ─────────────────────────────────────────────────────────────────────────
    何を見るか
    ─────────────────────────────────────────────────────────────────────────

    **原音（この瞬間の入力）**です。ディレイ音ではありません——
    ディレイ音を見ると自分で自分を絞ることになり、脈を打ちます。

    左右をまとめた1つの値で動かします（**片側だけ絞ると定位が動きます**）。
*/
class MantaDelayDucker
{
public:
    void prepare (double sampleRateToUse)
    {
        sampleRate = juce::jmax (8000.0, sampleRateToUse);
        reset();
    }

    void reset()
    {
        envelope = 0.0f;
        gain = 1.0f;
    }

    /** ブロックの頭で1回だけ（`std::exp`が入っています）。

        `amount`は0〜1、`attackMs`／`releaseMs`はミリ秒。 */
    void setSettings (float newAmount, float attackMs, float releaseMs)
    {
        amount = juce::jlimit (0.0f, 1.0f, newAmount);
        enabled = amount > 0.0001f;

        attackCoefficient  = makeCoefficient (attackMs);
        releaseCoefficient = makeCoefficient (releaseMs);
    }

    /** 原音1サンプル（左右をまとめたもの）を渡して、**ウェットに掛ける倍率**をもらう。

        **毎サンプル呼びます**——ブロックの頭で1回だけ出すと、
        Attackを1msにしても**ブロックの長さでしか動きません。** */
    float advance (float monoInput)
    {
        if (! enabled)
        {
            envelope = 0.0f;
            gain = 1.0f;
            return 1.0f;
        }

        const float target = std::abs (monoInput);

        // **上がるときと下がるときで速さが違う**（それがAttack／Releaseです）。
        // 上がるほうが速いのが普通ですが、逆にもできます——
        // 遅いAttackにすると、**頭だけ通って残りが絞られます**
        const float coefficient = (target > envelope) ? attackCoefficient : releaseCoefficient;

        envelope += (target - envelope) * coefficient;

        // **基準は-12dBFS**（0.25）。そこで`amount`いっぱいに絞れます。
        //
        // しきい値のつまみを出していません——ディレイのダッキングで
        // 触りたいのは「どれだけ引っ込むか」で、**何dBから効くか**ではない、
        // という判断です（仕様書5-4もAmount／Attack／Releaseの3つだけ）
        const float depth = juce::jmin (1.0f, envelope * 4.0f);

        gain = juce::jmax (0.0f, 1.0f - amount * depth);

        return gain;
    }

    /** 画面が出す「いまどれだけ絞っているか」（0＝絞っていない、1＝いっぱい）。

        **音のスレッドが書いた最後の値をそのまま読みます。** 1サンプルぶん
        古いことがありますが、30Hzで描く帯には関係ありません。 */
    float getReductionForDisplay() const { return 1.0f - gain; }

private:
    /** ミリ秒から一次の係数へ。**サンプルレートに依らないこと**（8.205）。 */
    float makeCoefficient (float milliseconds) const
    {
        const double seconds = (double) juce::jmax (0.1f, milliseconds) * 0.001;

        return (float) (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    double sampleRate = 44100.0;

    float amount = 0.0f;
    bool enabled = false;

    float attackCoefficient = 0.01f;
    float releaseCoefficient = 0.001f;

    float envelope = 0.0f;
    float gain = 1.0f;
};
