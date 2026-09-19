#pragma once

#include "../MantaTheme.h"
#include "../../AppColours.h"
#include "../../Branding.h"

//==============================================================================
/**
    8.292：**Kakapo の色**（Phase 285）。

    `RaccoGuitarTheme`（8.256）・`JavaRhinoBassTheme`（8.257）・
    `OrangutanDrumsTheme`（8.288）と同じ作りです。

    | 使う場所 | 何を引くか |
    |---|---|
    | 画像の**外**（帯・箱・鍵盤） | `accent()` / `highlight()`（テーマに従う） |
    | 画像の**上**（板・つまみ・値） | `field*()`（**固定**） |

    ### 色は**こちらで決めました**（本人の指定は「合いそうな色を」）

    絵は**苔の緑**です（カカポは緑のオウム）。緑の上で沈まない組み合わせを選びました。

    | | |
    |---|---|
    | 主 | **バイオレット**。黄緑の向かい側で、いちばん離れています |
    | 副 | **エメラルドグリーン**（8.293で琥珀から。本人の指定） |

    **黄緑は選べません**（絵に溶けます）。副のグリーンを青寄りのエメラルドにしてあるのは、
    そのためです——**この色を使うのは薄い板の上**（ルートの棒・FAVOURED）と、
    白い地のつまみの弧なので、絵と隣り合っても溶けません。

    **気に入らなければ`Branding::scaleAccent*()`の2行だけ**で変わります。

    ### 絵は暗いまま、**文字は板の上へ**

    この絵は平均の明るさが**79**しかありません（銅は119、ギター・ベースは187・191）。
    白のベールで明るくすると**苔の緑が灰色に寄る**ので、**絵は暗いまま**にして、
    読ませたいものは**薄い板の上**（`fieldPanel()`）へ載せてあります。

    > 8.290で見たとおり——**絵の上か、板の上か**で色を変えること。
    > `Orangutan Drums`は絵を起こして文字を直に載せましたが、
    > ここは絵の見どころ（苔の粒）を残すほうを採りました。
*/
namespace KakapoTheme
{
    /** 主＝**判定まわり**（候補の帯・トーナルセンター）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::scaleAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副＝**手で触るところ**（Hold・Volume・MIDI Thru）。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::scaleAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    //==========================================================================
    // 画像の上（**テーマに従わない**）

    inline juce::Colour fieldAccent()    { return juce::Colour (Branding::scaleAccentPrimary (false)); }
    inline juce::Colour fieldHighlight() { return juce::Colour (Branding::scaleAccentSecondary (false)); }

    /** 板の上の文字。**黒ではなく濃い墨緑**——絵が苔なので、真っ黒だと浮きます。 */
    inline juce::Colour fieldInk() { return juce::Colour (0xff1e2a10); }

    inline juce::Colour fieldInkDim() { return juce::Colour (0x991e2a10); }

    /** つまみの地。**塗ること**——透けると指針が読めません。 */
    inline juce::Colour fieldKnobBody() { return juce::Colour (0xfff4f7f0); }

    /** つまみの弧の地（どこまで回せるか）。 */
    inline juce::Colour fieldKnobTrack() { return juce::Colour (0x441e2a10); }

    /** **絵の上に置く板**（クラスの説明）。ここに文字を載せます。 */
    inline juce::Colour fieldPanel() { return juce::Colour (0xfff4f7f0).withAlpha (0.88f); }

    inline juce::Colour fieldPanelEdge() { return juce::Colour (0x551e2a10); }

    /** 絵に重ねる白。**薄めです**（絵を暗いまま見せるため）。 */
    inline juce::Colour fieldVeil() { return juce::Colours::white.withAlpha (0.12f); }
}
