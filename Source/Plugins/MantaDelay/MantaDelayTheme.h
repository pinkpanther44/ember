#pragma once

#include "../MantaTheme.h"
#include "../../Branding.h"
#include "../../AppColours.h"

//==============================================================================
/**
    8.204：**ディレイだけのアクセント2色**（Phase 238／本人の指定）。

    | | 主 | 副 |
    |---|---|---|
    | Manta Delay | パープル | オレンジ（**アプリと同じ＝従来どおり**） |
    | Hawkbill Delay | **ブルー** | **イエロー** |

    EQ・コンプ・シンセは`MantaTheme::accent()`／`curve()`（＝アプリのアクセント）を
    そのまま使っています。**ディレイはそこから外れる初めてのもの**です。

    > **地・枠・文字は`MantaTheme`のまま。** そこまで変えると
    > 「同じアプリの中の別のアプリ」に見えます。**変えるのはアクセント2色だけ。**

    値は`Branding.h`にあります（**製品ごとの違いは全部あそこ1箇所**。8.175）。
*/
namespace MantaDelayTheme
{
    /** 主。**反復そのもの**（ディスプレイの棒、Timeとフィードバックのつまみ）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::delayAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副。**いま効いているもの**（Syncが入っているとき、Mixのつまみ）。

        Manta EQ・Compで「オレンジ＝掛かっているぶん」にしてあるのと同じ役割です。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::delayAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }
}
