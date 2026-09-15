#pragma once

#include "../MantaTheme.h"
#include "../../AppColours.h"
#include "../../Branding.h"

//==============================================================================
/**
    8.257：**Java Rhino Bass の色**（Phase 265／本人の指定は「濃いめのグリーンと濃いめの青」）。

    `RaccoGuitarTheme`（8.256）と同じ作りです。

    | 使う場所 | 何を引くか |
    |---|---|
    | 画像の**外**（帯・チップ・鍵盤・出力） | `accent()` / `highlight()`（テーマに従う） |
    | 画像の**上**（つまみ・値・見出し） | `field*()`（**固定**） |

    ### 画像が濃いので、ギターとは明暗が逆

    `Racco Guitar`の絵は淡いので、**白を薄く重ねて**文字を濃くしました。
    こちらの絵は**中間より濃い緑**なので、同じことをすると文字が沈みます。

    - 白のベールは**厚め**（0.30）——絵の粒は残しつつ、地を1段明るく
    - つまみの地は**白に近い円**。濃い面の上では、これがいちばん強い手がかりになります
    - 弧は**指定どおりの濃い色**。白い円のすぐ外に置くので、濃い地の上でも縁で読めます

    地・枠・文字は`MantaTheme`のままです（8.204の決まり）。
*/
namespace JavaRhinoBassTheme
{
    /** 主＝**弦そのものを決めるところ**（Brightness・Sustain・Pluck Pos・Tone・Output）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::bassAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副＝**弾き方**（Hardness・Attack・Clank・奏法のチップ）。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::bassAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    //==========================================================================
    // 画像の上（**テーマに従わない**）

    inline juce::Colour fieldAccent()    { return juce::Colour (Branding::bassAccentPrimary (false)); }
    inline juce::Colour fieldHighlight() { return juce::Colour (Branding::bassAccentSecondary (false)); }

    /** 画像の上の文字。**黒ではなく濃い緑**——絵が緑なので、真っ黒だと浮きます。 */
    inline juce::Colour fieldInk() { return juce::Colour (0xff17302a); }

    inline juce::Colour fieldInkDim() { return juce::Colour (0x9917302a); }

    /** つまみの地。**濃い面の上ではこれがいちばん強い手がかり**なので、白に寄せます。 */
    inline juce::Colour fieldKnobBody() { return juce::Colour (0xfff2f6f4); }

    /** つまみの弧の地（どこまで回せるか）。 */
    inline juce::Colour fieldKnobTrack() { return juce::Colour (0x4417302a); }

    /** 絵に重ねる白。

        **0.30まで上げたら、ラメが灰色に沈みました**（絵の見どころが消えた）。
        0.18で、粒を残したまま文字が読める濃さになります。 */
    inline juce::Colour fieldVeil() { return juce::Colours::white.withAlpha (0.18f); }
}
