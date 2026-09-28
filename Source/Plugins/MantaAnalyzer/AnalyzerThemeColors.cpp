#include "AnalyzerThemeColors.h"

#include <cmath>

namespace
{
    float toLinear (float c)
    {
        return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
    }

    float toSrgb (float c)
    {
        c = juce::jlimit (0.0f, 1.0f, c);
        return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow (c, 1.0f / 2.4f) - 0.055f;
    }
}

// 変換式は Björn Ottosson による OKLab の定義（公開されている行列）
AnalyzerThemeColors::Oklch AnalyzerThemeColors::toOklch (juce::Colour colour)
{
    const float r = toLinear (colour.getFloatRed());
    const float g = toLinear (colour.getFloatGreen());
    const float b = toLinear (colour.getFloatBlue());

    const float l = std::cbrt (0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    const float m = std::cbrt (0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    const float s = std::cbrt (0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

    const float labL = 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s;
    const float labA = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
    const float labB = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;

    return { labL, std::sqrt (labA * labA + labB * labB), std::atan2 (labB, labA) };
}

juce::Colour AnalyzerThemeColors::fromOklch (Oklch v, float alpha)
{
    const float labA = v.c * std::cos (v.h);
    const float labB = v.c * std::sin (v.h);

    const float l_ = v.l + 0.3963377774f * labA + 0.2158037573f * labB;
    const float m_ = v.l - 0.1055613458f * labA - 0.0638541728f * labB;
    const float s_ = v.l - 0.0894841775f * labA - 1.2914855480f * labB;

    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;

    const float r =  4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    const float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    const float b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    return juce::Colour::fromFloatRGBA (toSrgb (r), toSrgb (g), toSrgb (b), alpha);
}

juce::Colour AnalyzerThemeColors::withLightness (juce::Colour colour, float lightness)
{
    auto v = toOklch (colour);
    v.l = juce::jlimit (0.0f, 1.0f, lightness);

    // **明るさの端では彩度を落とす**。OKLCHのまま明度だけ上げ下げすると、
    // sRGBに入らない色になり、切り詰めたときに色みが変わります
    const float edge = juce::jmin (v.l, 1.0f - v.l);
    v.c = juce::jmin (v.c, 0.4f * edge + 0.02f);

    return fromOklch (v, colour.getFloatAlpha());
}

AnalyzerThemeColors AnalyzerThemeColors::fromAccent (juce::Colour accent, bool darkBackground)
{
    // **不透明にしてから使う**。アクセントの不透明度をそのまま引き継ぐと、
    // 透明なアクセント（テーマを読む前の既定値など）で塗りが全部消えます
    accent = accent.withAlpha (1.0f);

    // 設計書6.5：**先にLを0.35〜0.85へ収める**
    const float L = juce::jlimit (0.35f, 0.85f, toOklch (accent).l);
    const auto base = withLightness (accent, L);

    AnalyzerThemeColors c;

    // 8.330：**地は本体のテーマに従う**（ライトは白系、ダークは黒系。本人の指定）
    if (darkBackground)
    {
        c.background = juce::Colour (0xff0d0e11);
        c.grid       = juce::Colours::white.withAlpha (0.07f);
        c.gridText   = juce::Colours::white.withAlpha (0.45f);
        c.text       = juce::Colour (0xffe6e6ea);
    }
    else
    {
        c.background = juce::Colour (0xfffafafc);
        c.grid       = juce::Colours::black.withAlpha (0.08f);
        c.gridText   = juce::Colours::black.withAlpha (0.50f);
        c.text       = juce::Colour (0xff1e1f24);
    }

    // 塗りの4段（下限より下 → レンジ下端 → レンジ上端 → 過多）。設計書6.5の式のままだと、
    // **アクセントが明るいと上の2段がくっつきます**（L=0.85 で 0.92 と 0.95）。
    // 設計書8.1の「どの段も0.08以上離れる」を守るため、**狭いところを押し広げ、
    // はみ出したら全体を下げます**（式どおりに離れていれば何も変わりません）
    float steps[4] = { L * 0.6f, L * 0.8f, juce::jmin (L * 1.2f, 0.92f), juce::jmin (L + 0.2f, 0.95f) };

    for (int i = 1; i < 4; ++i)
        steps[i] = juce::jmax (steps[i], steps[i - 1] + minimumStep + 0.01f);   // 0.01 は sRGB へ戻すときの目減りのぶん

    if (const float over = steps[3] - 0.97f; over > 0.0f)
        for (auto& s : steps)
            s -= over;

    c.belowLower  = withLightness (base, steps[0]);
    c.rangeBottom = withLightness (base, steps[1]);
    c.rangeTop    = withLightness (base, steps[2]);
    c.excessNear  = withLightness (base, 0.5f * (steps[2] + steps[3]));
    c.excessFar   = withLightness (base, steps[3]);
    c.rangeBand   = base;
    c.boundary    = withLightness (base, juce::jmin (L + 0.1f, 0.95f));
    c.peak        = c.text.withAlpha (0.7f);

    if (darkBackground)
    {
        // 暗い地：下が暗く、上へ行くほど明るい。線は塗りより明るく
        // 線は**少なくとも0.6**（黒に近いアクセントでも、暗い地から浮くように）
        c.curve      = withLightness (base, juce::jlimit (0.6f, 0.95f, L + 0.15f));
        c.fillBottom = c.belowLower;
        c.fillTop    = c.rangeTop;
    }
    else
    {
        // 白い地：**線をアクセントより暗く**（明るい線は白に沈む）。塗りは下が淡く、上がアクセント
        // 線は**多くても0.5**（白に近いアクセントでも、白い地から浮くように）
        c.curve      = withLightness (base, juce::jlimit (0.28f, 0.5f, L - 0.2f));
        c.fillBottom = withLightness (base, juce::jmin (L + 0.3f, 0.93f));
        c.fillTop    = base;
    }

    c.realtime    = c.curve.withAlpha (0.45f);

    return c;
}
