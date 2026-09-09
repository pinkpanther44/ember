#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../AppColours.h"

#include <cmath>

//==============================================================================
/**
    設計書5.2「配色」。**Manta Studio本体と同じ色**を使います。

    設計書には「配色は未定のプレースホルダー」と書いてありましたが、
    **本人の指定はパープルとオレンジ**（＝本体のアクセント色）です。
    そこで、値をここへ書き写すのではなく`AppColours`をそのまま引いています。

    | | 何に使うか |
    |---|---|
    | `AppColours::purple` | 選ばれているもの・アナライザーのPost |
    | `AppColours::orange` | いま掛かっているEQカーブ（いちばん見るもの） |
    | `AppColours::canvas` | グラフの地（本体のタイムラインと同じ） |

    ### テーマの切り替えに付いてくる

    値を持たずに毎回`AppColours`から引くので、**本体のライト／ダークが
    そのまま効きます**（1.34）。プラグイン側にだけ古い色が残る、が起こりません。

    ### バンドの色

    12本を**パープルからオレンジへ**回した色相で配ってあります
    （紫 → マゼンタ → 赤 → オレンジ）。本体のアクセント2色のあいだを通るので、
    **どのバンドの色もアプリの配色から外れません。**
*/
namespace MantaTheme
{
    inline juce::Colour graphBackground()   { return AppColours::canvas; }
    inline juce::Colour panelBackground()   { return AppColours::panel; }
    inline juce::Colour windowBackground()  { return AppColours::background; }
    inline juce::Colour border()            { return AppColours::border; }
    inline juce::Colour text()              { return AppColours::textPrimary; }
    inline juce::Colour textDim()           { return AppColours::textSecondary; }
    inline juce::Colour accent()            { return AppColours::purple; }
    inline juce::Colour curve()             { return AppColours::orange; }

    /** 目盛りの線。**地に薄く重ねる**（テーマの明暗どちらでも同じ濃さに見えるように）。 */
    inline juce::Colour grid()
    {
        return AppColours::getTheme() == AppColours::Theme::Dark
                 ? juce::Colours::white.withAlpha (0.08f)
                 : juce::Colours::black.withAlpha (0.08f);
    }

    /** 0dBの線と、1k・10kのような節目の線。 */
    inline juce::Colour gridStrong()
    {
        return AppColours::getTheme() == AppColours::Theme::Dark
                 ? juce::Colours::white.withAlpha (0.18f)
                 : juce::Colours::black.withAlpha (0.16f);
    }

    /** アナライザーのPre（EQを通す前）。**塗りつぶし**で出す。 */
    inline juce::Colour spectrumPre()
    {
        return AppColours::textSecondary.withAlpha (0.28f);
    }

    /** アナライザーのPost（EQを通した後）。**線**で出す。 */
    inline juce::Colour spectrumPost()
    {
        return AppColours::purple.withAlpha (0.55f);
    }

    /** バンドの色は、**ステレオ配置で決まります**（Phase 206／本人の要望）。

        | 配置 | 色 |
        |---|---|
        | Stereo / Left / Right | パープル |
        | Mid / Side | オレンジ |

        Phase 205まではバンド番号ごとの色（紫→橙のグラデーション）でしたが、
        **色に意味がありませんでした**——12本を見分けるだけなら丸の中の番号で足ります。

        **M/Sで処理しているバンドがひと目で分かる**ほうが、実際に効きます
        （M/Sのバンドは「なぜここだけ効きが違うのか」を見失いやすい）。

        `channelIndex`は`MantaEQParams::Channel`をintにしたもの。 */
    inline juce::Colour bandColour (int channelIndex)
    {
        // 3=Mid、4=Side（`MantaEQParams::Channel`の並びと対）
        return channelIndex >= 3 ? AppColours::orange : AppColours::purple;
    }

    /** つまみの丸の大きさ（グラフの上）。 */
    inline constexpr float bandHandleRadius = 7.0f;
}
