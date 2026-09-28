#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
/**
    8.329：アクセントカラー1色から、塗りの段階を作る（アナライザー設計書6.5の`ThemeColors`）。

    **明るさだけを変える**ので、どの段階も同じ色みに見えます。明るさは**OKLCH**で計算します
    ——RGBやHSLで明るくすると、黄色と青で「同じだけ明るくした」の見え方がまるで違うため。

    アクセントの明度 L は先に 0.35〜0.85 へ収めます（黒や白に近いアクセントでも段階が見分けられるように）。

    | 用途 | 明度 | 不透明度 |
    |---|---|---|
    | 下限線より下の塗り | L × 0.6 | 100% |
    | レンジ内（下端 → 上端） | L × 0.8 → min(L × 1.2, 0.92) | 100% |
    | 過多の強調 | min(L + 0.2, 0.95) | 100% |
    | レンジ帯 | L | 設定（初期 25%） |
    | 境界線 | min(L + 0.1, 0.95) | 100% |
    | スペクトラム曲線 | min(L + 0.15, 0.95) | 100% |
    | ピーク曲線 | 文字色 | 70% |

    ### グラフの地は本体のテーマに従う（8.330／本人の指定）

    **ライトは白系、ダークは黒系。** Phase 319では仕様書3.3の「ほぼ黒」のまま、いつも暗くしていました。

    白い地では、アクセントより明るい線は沈んで見えません。ライトのときは
    **曲線をアクセントより暗く**し、塗りは**下が淡く、上へ行くほどアクセント**にしてあります
    （ダークはその逆で、下が暗く上が明るい）。レンジの段（上の表）は、どちらのテーマでも同じ式です。
*/
struct AnalyzerThemeColors
{
    juce::Colour background;
    juce::Colour grid;
    juce::Colour gridText;
    juce::Colour belowLower;
    juce::Colour rangeBottom;
    juce::Colour rangeTop;
    juce::Colour excessNear;       ///< 上限線のすぐ上
    juce::Colour excessFar;        ///< 上限線からいちばん離れたところ
    juce::Colour rangeBand;        ///< 不透明度は描くときに掛ける
    juce::Colour boundary;
    juce::Colour curve;
    juce::Colour peak;
    juce::Colour realtime;
    juce::Colour text;

    /** レンジを出さないときの塗り（下端 → 上端）。8.330 */
    juce::Colour fillBottom;
    juce::Colour fillTop;

    /** アクセントから全部を作る。`darkBackground`はグラフの地を暗くするか（本体のテーマ）。 */
    static AnalyzerThemeColors fromAccent (juce::Colour accent, bool darkBackground = true);

    /** 塗りの段どうしの、明度の最小の差（設計書8.1）。 */
    static constexpr float minimumStep = 0.08f;

    //--------------------------------------------------------------------------
    // OKLab／OKLCH（自己検査が明度を比べるので、外から呼べるようにしてあります）

    struct Oklch { float l, c, h; };

    static Oklch toOklch (juce::Colour colour);
    static juce::Colour fromOklch (Oklch value, float alpha = 1.0f);

    /** 明度だけを変えた色。 */
    static juce::Colour withLightness (juce::Colour colour, float lightness);
};
