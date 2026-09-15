#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <utility>

//==============================================================================
/**
    8.248：**Panorama**（Phase 258／リバーブ仕様書4章・設計書4章）。

    ─────────────────────────────────────────────────────────────────────────
    EQプラグインのM/Sを、そのまま使います（設計書3章）
    ─────────────────────────────────────────────────────────────────────────

    設計書の決定：**EQプラグインのM/S処理を流用しそのまま採用**（★2）。

    `MantaEQProcessor`の仕様書4.16と**同じ取り方**です：

    ```
        mid  = (L + R) / 2
        side = (L − R) / 2
        L = mid + side × width
        R = mid − side × width
    ```

    **`width`が1のとき素通し**になる取り方であること——
    `(L+R)`と`(L−R)`のまま（÷2しない）にすると、真ん中で2倍になります。

    ─────────────────────────────────────────────────────────────────────────
    これはリバーブではありません（仕様書2-1）
    ─────────────────────────────────────────────────────────────────────────

    「ステレオ幅の拡張、モノサミング、位相反転、チャンネルスワップなどを行う
    **ステレオ加工ツール**」。響きは一切足しません。

    > **`Mix`は100%にして使うもの**です。25%のままだと、
    > 加工したものが原音に4分の1しか混ざらず、**ほとんど何も起きません。**
    > 画面の説明行にそう書いてあります（8.225）。

    ─────────────────────────────────────────────────────────────────────────
    順番
    ─────────────────────────────────────────────────────────────────────────

    ```
        入れ替え → 位相反転 → Mono Sum ／ Width
    ```

    **入れ替えを先にすること。** 後にすると「右を反転してから左右を入れ替えた」ことになり、
    ボタン2つの組み合わせが**押した順で変わって見えます。**

    ### `Mono Sum`と`Width`

    `Width`を0にするのと**同じこと**です（どちらもsideが消える）。
    ボタンがあるのは**1回押すだけで確かめられる**からで、
    入っているあいだは`Width`をグレーアウトします——
    **2つのものが同じ1つを取り合っている状態を、画面に出さないこと。**

    ### `Phase Invert`は**右だけ**

    両方返しても、耳には何も変わりません（差が同じなので）。
    **片側だけ返すのが「位相を反転する」の意味**で、
    左右の打ち消しを確かめるのに使います。
*/
class MantaReverbPanorama
{
public:
    struct Settings
    {
        float width = 1.0f;
        bool monoSum = false;
        bool invertRight = false;
        bool swapChannels = false;
    };

    void setSettings (const Settings& newSettings) { settings = newSettings; }

    /** 1サンプルぶん。**状態を持ちません**（遅れも溜めもしない）。 */
    void processSample (float inputLeft, float inputRight, float& outputLeft, float& outputRight) const
    {
        float left = inputLeft;
        float right = inputRight;

        // **入れ替えが先**（上の説明）
        if (settings.swapChannels)
            std::swap (left, right);

        if (settings.invertRight)
            right = -right;

        const float mid = 0.5f * (left + right);

        // **`Mono Sum`が入っているあいだは`Width`を見ません**（画面もグレーアウト）
        const float side = settings.monoSum ? 0.0f
                                             : 0.5f * (left - right) * settings.width;

        outputLeft = mid + side;
        outputRight = mid - side;
    }

private:
    Settings settings;
};
