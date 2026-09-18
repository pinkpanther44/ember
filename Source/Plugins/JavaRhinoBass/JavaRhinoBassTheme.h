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

    ### 8.274：**絵を淡いものに差し替えました**（Phase 273／本人の指定）

    Phase 272まではラメの粒がある**濃い緑**の絵で、その上に文字を載せるために
    **白＋暗い縁**という手を使っていました（8.259）。それでもまだ見づらい、というのが
    本人の言葉です。**絵のほうを淡いものへ替えて、手当てごと畳みました。**

    | | Phase 272まで | いま |
    |---|---|---|
    | 絵 | 濃い緑＋ラメ | **淡いパステル**（本人が用意） |
    | 文字 | 白＋暗い縁（2回描く） | **濃い一色・縁なし**（`Racco Guitar`と同じ） |
    | 白のベール | 0.18 | **0.22**（ギターと同じ） |

    **縁を描く手当ては、濃い絵のためのものでした。** 淡い絵の上では、
    濃い文字1色のほうが読めます——`Racco Guitar`がずっとそうしています（8.256）。
    **絵を替えたら、絵に合わせた手当ても畳むこと**（残すと、白い縁だけが浮きます）。

    つまみの地は**白に近い円**のまま。弧は**指定どおりの濃い色**なので、
    淡い地の上ではむしろ読みやすくなっています。

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

    /** 画像の上の**指針と文字**。**黒ではなく濃い緑**——絵に緑があるので、
        真っ黒だと浮きます（`RaccoGuitarTheme::fieldInk()`が濃い藍なのと同じ理由）。

        8.274：**見出しと数値もこれになりました**（Phase 273／本人の指定
        「文字色はRaccoと同じ黒色で縁は無し」）。`fieldText()`と`fieldTextEdge()`は
        **消しました**——絵が淡くなったので、白＋縁で浮かせる必要がありません。 */
    inline juce::Colour fieldInk() { return juce::Colour (0xff17302a); }

    inline juce::Colour fieldInkDim() { return juce::Colour (0x9917302a); }

    /** つまみの地。**濃い面の上ではこれがいちばん強い手がかり**なので、白に寄せます。 */
    inline juce::Colour fieldKnobBody() { return juce::Colour (0xfff2f6f4); }

    /** つまみの弧の地（どこまで回せるか）。 */
    inline juce::Colour fieldKnobTrack() { return juce::Colour (0x4417302a); }

    /** 絵に重ねる白。

        8.274：**0.18→0.22**（Phase 273）。`Racco Guitar`と同じ値です——
        淡い絵に濃い文字を載せる、という同じ形になったので、数字も揃えました。

        > Phase 272までは0.18でした。**0.30まで上げたら、ラメが灰色に沈んだ**ためです
        > （濃い絵の見どころが消えた）。**その制約はもうありません。** */
    inline juce::Colour fieldVeil() { return juce::Colours::white.withAlpha (0.22f); }
}
