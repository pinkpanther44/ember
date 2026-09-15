#pragma once

#include "../MantaTheme.h"
#include "../../Branding.h"
#include "../../AppColours.h"

//==============================================================================
/**
    8.253：**リバーブだけのアクセント2色**（Phase 261／本人の指定）。

    | | 主 | 副 |
    |---|---|---|
    | Manta Reverb | パープル | オレンジ（**アプリと同じ＝従来どおり**） |
    | Bf Owl Reverb | **パープル** | **ブルー** |

    ディレイ（`MantaDelayTheme`／8.204）に続いて2つめです。

    **主はどちらもパープル**で、副だけが違います——
    `Hawkbill Delay`が**ブルー＋イエロー**なので、並べたときに
    **主の色でどちらのプラグインか分かる**ようにしてあります。

    > **地・枠・文字は`MantaTheme`のまま。** そこまで変えると
    > 「同じアプリの中の別のアプリ」に見えます。**変えるのはアクセント2色だけ。**

    値は`Branding.h`にあります（**製品ごとの違いは全部あそこ1箇所**。8.175）。
*/
namespace MantaReverbTheme
{
    /** 主。**空間そのものを決めるところ**
        （Predelay・Decay・Size・Shape・Spread・Diffusion・Modulation）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::reverbAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副。**掛かり具合**（Early・Width・Damping・Level・Mix・Output・Saturation）。

        Manta EQ・Compで「オレンジ＝掛かっているぶん」にしてあるのと同じ役割です。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::reverbAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }
}
