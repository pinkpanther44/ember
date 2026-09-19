#pragma once

#include "../MantaTheme.h"
#include "../../AppColours.h"
#include "../../Branding.h"

//==============================================================================
/**
    8.288：**Orangutan Drums の色**（Phase 281）。

    `RaccoGuitarTheme`（8.256）・`JavaRhinoBassTheme`（8.257）と同じ作りです。

    | 使う場所 | 何を引くか |
    |---|---|
    | 画像の**外**（帯・箱・鍵盤） | `accent()` / `highlight()`（テーマに従う） |
    | 画像の**上**（パッド・つまみ・値・見出し） | `field*()`（**固定**） |

    ### 色は**こちらで決めました**

    ギターの水色・ピンクも、ベースの濃い緑・濃い青も、本人の指定でした。
    **ここだけは指定がありません**（絵だけを頂いています）。合わせたのは絵です——

    | | |
    |---|---|
    | 絵 | **銅**（オレンジ〜赤茶） |
    | 主 | **濃いインディゴ**。銅の補色側なので、いちばん沈みません |
    | 副 | **濃いプラム**。主と混ざらず、絵の赤茶とも離れています |

    > **オレンジ系は選べません。** 絵がオレンジなので、弧も指針も消えます。
    > `Branding.h`のアクセント（Mantaのオレンジ／Emberのゴールド）を
    > そのまま使わなかったのは、この1点です。

    **気に入らなければ`Branding::drumsAccent*()`の2行だけ**で変わります。

    ### 文字は濃い一色・縁なし

    `Racco Guitar`と同じ扱いです（8.274で`Java Rhino Bass`もここへ揃えました）。
    **縁を描くのは濃い絵の上の手当て**で、この絵は明るく起こしてあるので要りません。

    絵そのものを明るくしてある（ガンマ0.72）ぶん、**白のベールは0.20**です
    ——ギター・ベースの0.22より薄いのは、ベールを厚くすると
    **銅が灰色に寄る**ためです（絵を起こすほうで明るさを稼いであります）。

    地・枠・文字は`MantaTheme`のままです（8.204の決まり）。
*/
namespace OrangutanDrumsTheme
{
    /** 主＝**パッドと、そのパッドの音を決めるところ**（TUNE・DECAY・TONE・SNAP）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::drumsAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副＝**出口まわり**（LEVEL・PAN・SEND、マスター、ベースゾーン）。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::drumsAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    //==========================================================================
    // 画像の上（**テーマに従わない**）

    inline juce::Colour fieldAccent()    { return juce::Colour (Branding::drumsAccentPrimary (false)); }
    inline juce::Colour fieldHighlight() { return juce::Colour (Branding::drumsAccentSecondary (false)); }

    /** 画像の上の**指針と文字**。**黒ではなく濃い焦げ茶**——絵が銅なので、
        真っ黒だと浮きます（`RaccoGuitarTheme::fieldInk()`が濃い藍なのと同じ理由）。 */
    inline juce::Colour fieldInk() { return juce::Colour (0xff3a2015); }

    inline juce::Colour fieldInkDim() { return juce::Colour (0x993a2015); }

    /** つまみとパッドの地。**塗ること**——透けると指針も文字も読めません。 */
    inline juce::Colour fieldKnobBody() { return juce::Colour (0xfff8f4f1); }

    /** つまみの弧の地（どこまで回せるか）。 */
    inline juce::Colour fieldKnobTrack() { return juce::Colour (0x443a2015); }

    /** 絵に重ねる白（上の説明）。 */
    inline juce::Colour fieldVeil() { return juce::Colours::white.withAlpha (0.20f); }
}
