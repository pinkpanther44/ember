#pragma once

#include "../MantaTheme.h"
#include "../../AppColours.h"
#include "../../Branding.h"

//==============================================================================
/**
    8.256：**Racco Guitar の色**（Phase 264／本人の指定は「水色とピンク」）。

    ディレイ（`MantaDelayTheme`／8.204）・リバーブ（`MantaReverbTheme`／8.253）に
    続いて3つめの「専用アクセント」ですが、**製品で分けていないのはこれが初めて**です
    ——名前が両方とも`Racco Guitar`なので（`Branding::guitarPluginName`）。

    ### 画像の上だけ、テーマに従いません

    真ん中の大きな面は**明るい画像**です（`racco_guitar_field.jpg`）。
    ここに載るもの（つまみの弧・指針・文字）は、**ライト／ダークどちらでも同じ**
    明るい面の上に乗るので、**明るい側の値で固定**しています（`field*()`）。

    テーマで色を変えてしまうと、ダークのときに**明るい画像の上へ暗い地の
    つまみが乗って**、そこだけ別のアプリに見えます。

    | 使う場所 | 何を引くか |
    |---|---|
    | 画像の**外**（帯・チップ・鍵盤・出力） | `accent()` / `highlight()`（テーマに従う） |
    | 画像の**上**（つまみ・値・見出し） | `field*()`（**固定**） |

    地・枠・文字は`MantaTheme`のままです（8.204の決まり）。
*/
namespace RaccoGuitarTheme
{
    /** 主＝**弦そのものを決めるところ**（Brightness・Sustain・Pick Pos・Output）。 */
    inline juce::Colour accent()
    {
        return juce::Colour (Branding::guitarAccentPrimary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    /** 副＝**弾き方**（Hardness・Attack・奏法のチップ）。 */
    inline juce::Colour highlight()
    {
        return juce::Colour (Branding::guitarAccentSecondary (
                                 AppColours::getTheme() == AppColours::Theme::Dark));
    }

    //==========================================================================
    // 画像の上（**テーマに従わない**。上の説明）

    /** 画像の上の水色。 */
    inline juce::Colour fieldAccent()
    {
        return juce::Colour (Branding::guitarAccentPrimary (false));
    }

    /** 画像の上のピンク。 */
    inline juce::Colour fieldHighlight()
    {
        return juce::Colour (Branding::guitarAccentSecondary (false));
    }

    /** 画像の上の文字。**黒ではなく濃い藍**——画像が青緑寄りなので、
        真っ黒だと浮きます。 */
    inline juce::Colour fieldInk() { return juce::Colour (0xff1f3440); }

    /** 画像の上の小さい文字・目盛り。 */
    inline juce::Colour fieldInkDim() { return juce::Colour (0x991f3440); }

    /** つまみの地。画像を透かさず**塗ること**——透けると指針が読めません。 */
    inline juce::Colour fieldKnobBody() { return juce::Colour (0xfff4f7f8); }

    /** つまみの弧の地（どこまで回せるか）。 */
    inline juce::Colour fieldKnobTrack() { return juce::Colour (0x331f3440); }

    /** 画像に重ねる薄い白。**文字を読めるようにするため**（画像は場所で明るさが違う）。 */
    inline juce::Colour fieldVeil() { return juce::Colours::white.withAlpha (0.22f); }
}
