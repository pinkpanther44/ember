#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AppColours.h"
#include "Branding.h"

//==============================================================================
/**
    8.177：**7セグメントの数字表示**（Phase 218。Phase 219で数字のある場所へ広げた）。

    ```
     ──        a          消えている桁も薄く出す（本物のLEDと同じ）
    │  │     f   b
     ──          g
    │  │     e   c
     ──        d
    ```

    ### なぜフォントではなく図形なのか

    本人の判断で**ドットフォントは保留**です（多言語に対応しているものが少ない）。
    ただし**数字だけなら、フォントは要りません**——7本の棒で全部描けます。

    そして数字は、レトロに見せたい場所の**ほとんど**です
    （時間・BPM・拍子・つまみの数値）。文字のほうは今までどおりで構いません。

    ### 単位は文字のまま添える（Phase 219）

    `"0.0 dB"`のような表示は、**数字だけを棒で描き、単位は小さな文字で右に**置きます。
    実機のLEDメーターがそう見えるのと同じで、
    **単位まで棒にしようとすると、そこだけ読めなくなります**（`d`も`B`も7本では描けない）。

    ### 消えている棒も描く

    点いている棒だけを描くと、数字が変わるたびに**字の形そのものが動いて見えます**。
    薄く残しておくと「8」の枠が常にあるので、目が落ち着きます。

    ### Manta Studioでは使いません

    `Branding::retroUI`が`false`のときは`canDraw()`が常に`false`を返すので、
    使う側は**今までどおり文字で描きます**。
    切り替えの分岐をここ1箇所に閉じてあるので、使う側に`#if`は要りません。
*/
namespace SevenSegment
{
    /** 棒で描ける文字か。 */
    inline bool isDrawableCharacter (juce::juce_wchar character)
    {
        return juce::CharacterFunctions::isDigit (character)
                || character == ':' || character == '.' || character == '-'
                || character == '/' || character == ' ';
    }

    /** 棒で描く部分と、右に文字で添える部分に分ける。

        `"-7.9 dB"` → `{"-7.9", "dB"}`、`"120"` → `{"120", ""}`、
        `"Read"` → `{"", "Read"}`（数字が無いので全部文字）。 */
    struct Split
    {
        juce::String digits;
        juce::String suffix;
    };

    inline Split split (const juce::String& text)
    {
        int index = 0;
        bool sawDigit = false;

        for (auto character : text)
        {
            if (! isDrawableCharacter (character))
                break;

            sawDigit = sawDigit || juce::CharacterFunctions::isDigit (character);
            ++index;
        }

        if (! sawDigit)
            return { {}, text };

        return { text.substring (0, index).trim(), text.substring (index).trim() };
    }

    /** この文字列を棒で描くか。**Manta Studioでは常にfalse**です。 */
    inline bool canDraw (const juce::String& text)
    {
        if (! Branding::retroUI || text.isEmpty())
            return false;

        const auto parts = split (text);

        // 単位が長いものは、棒にすると数字が小さくなりすぎて逆に読めません
        return parts.digits.isNotEmpty() && parts.suffix.length() <= 4;
    }

    //==========================================================================
    namespace detail
    {
        /** 1桁の縦横比（高さ ÷ 幅）。**7セグは縦長**——正方形にすると数字に見えません。 */
        inline constexpr float digitAspect = 1.75f;

        /** 桁と桁のあいだ（1桁の幅に対する比）。 */
        inline constexpr float gapRatio = 0.22f;

        inline float widthRatioFor (juce::juce_wchar character)
        {
            // `:`と`.`は**細くする**——等幅にすると、区切りのほうに目が行きます
            return (character == ':' || character == '.') ? 0.34f : 1.0f;
        }

        inline void drawCharacter (juce::Graphics& g, juce::juce_wchar character,
                                    juce::Rectangle<float> box, juce::Colour colour)
        {
            const auto dim = colour.withMultipliedAlpha (0.13f);

            if (character == ' ')
                return;

            if (character == ':' || character == '.')
            {
                const float dot = box.getWidth() * 0.62f;

                g.setColour (colour);

                if (character == ':')
                {
                    g.fillEllipse (box.getCentreX() - dot * 0.5f, box.getY() + box.getHeight() * 0.30f - dot * 0.5f, dot, dot);
                    g.fillEllipse (box.getCentreX() - dot * 0.5f, box.getY() + box.getHeight() * 0.70f - dot * 0.5f, dot, dot);
                }
                else
                {
                    g.fillEllipse (box.getCentreX() - dot * 0.5f, box.getBottom() - dot, dot, dot);
                }

                return;
            }

            const float thickness = juce::jmax (1.2f, box.getHeight() * 0.13f);

            if (character == '/')
            {
                g.setColour (colour);
                g.drawLine (box.getX() + thickness, box.getBottom() - thickness,
                             box.getRight() - thickness, box.getY() + thickness, thickness);
                return;
            }

            // 棒の並びは a b c d e f g（上の図）。**この表だけが「どの数字がどの形か」**です
            static const bool patterns[11][7]
            {
                { true,  true,  true,  true,  true,  true,  false }, // 0
                { false, true,  true,  false, false, false, false }, // 1
                { true,  true,  false, true,  true,  false, true  }, // 2
                { true,  true,  true,  true,  false, false, true  }, // 3
                { false, true,  true,  false, false, true,  true  }, // 4
                { true,  false, true,  true,  false, true,  true  }, // 5
                { true,  false, true,  true,  true,  true,  true  }, // 6
                { true,  true,  true,  false, false, false, false }, // 7
                { true,  true,  true,  true,  true,  true,  true  }, // 8
                { true,  true,  true,  true,  false, true,  true  }, // 9
                { false, false, false, false, false, false, true  }, // -
            };

            const int index = character == '-' ? 10 : (int) (character - '0');

            if (! juce::isPositiveAndBelow (index, 11))
                return;

            const float inset = thickness * 0.5f;

            const float left = box.getX() + inset;
            const float right = box.getRight() - inset;
            const float top = box.getY() + inset;
            const float bottom = box.getBottom() - inset;
            const float middle = box.getCentreY();

            struct Bar { float x1, y1, x2, y2; };

            const Bar bars[7]
            {
                { left,  top,    right, top    },   // a 上
                { right, top,    right, middle },   // b 右上
                { right, middle, right, bottom },   // c 右下
                { left,  bottom, right, bottom },   // d 下
                { left,  middle, left,  bottom },   // e 左下
                { left,  top,    left,  middle },   // f 左上
                { left,  middle, right, middle },   // g 中
            };

            for (int i = 0; i < 7; ++i)
            {
                g.setColour (patterns[index][i] ? colour : dim);
                g.drawLine (bars[i].x1, bars[i].y1, bars[i].x2, bars[i].y2, thickness);
            }
        }
    }

    //==========================================================================
    /** 棒で描く。**`canDraw()`が`true`のときだけ呼ぶこと。**

        `justification`は左寄せ・右寄せ・中央のどれか（縦は常に中央）。 */
    inline void draw (juce::Graphics& g, juce::Rectangle<float> area,
                       const juce::String& text, juce::Colour colour,
                       juce::Justification justification = juce::Justification::centred)
    {
        const auto parts = split (text);

        if (parts.digits.isEmpty())
            return;

        area = area.reduced (1.0f);

        // 単位の場所を先に取っておく。**残りで数字の大きさが決まります**
        float suffixWidth = 0.0f;
        const float suffixFontHeight = juce::jmin (11.0f, area.getHeight() * 0.5f);

        if (parts.suffix.isNotEmpty())
            suffixWidth = suffixFontHeight * 0.62f * (float) parts.suffix.length() + 3.0f;

        auto digitArea = area.withTrimmedRight (suffixWidth);

        float totalRatio = 0.0f;

        for (auto character : parts.digits)
            totalRatio += detail::widthRatioFor (character) + detail::gapRatio;

        if (totalRatio <= 0.0f || digitArea.getWidth() <= 0.0f)
            return;

        // **高さから決めて、収まらなければ幅で決め直す。** 逆にすると、
        // 桁数が増えたときに縦だけはみ出します
        float digitHeight = digitArea.getHeight();
        float unit = digitArea.getWidth() / totalRatio;

        if (unit * detail::digitAspect > digitHeight)
            unit = digitHeight / detail::digitAspect;
        else
            digitHeight = unit * detail::digitAspect;

        const float usedWidth = unit * totalRatio;

        float x = digitArea.getX();

        if (justification.testFlags (juce::Justification::horizontallyCentred))
            x = digitArea.getCentreX() - usedWidth * 0.5f;
        else if (justification.testFlags (juce::Justification::right))
            x = digitArea.getRight() - usedWidth;

        const float y = area.getCentreY() - digitHeight * 0.5f;

        for (auto character : parts.digits)
        {
            const float width = unit * detail::widthRatioFor (character);

            detail::drawCharacter (g, character, { x, y, width, digitHeight }, colour);

            x += width + unit * detail::gapRatio;
        }

        if (parts.suffix.isNotEmpty())
        {
            g.setColour (colour.withMultipliedAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (suffixFontHeight)));
            g.drawText (parts.suffix,
                         juce::Rectangle<float> (x + 2.0f, area.getY(), suffixWidth, area.getHeight()),
                         juce::Justification::centredLeft);
        }
    }
}

//==============================================================================
/** 数字だけを出す小さな表示（トランスポートの時間）。

    出せない文字が来たら**普通の文字として描きます**——
    カウントイン中の「カウント中」が化けないように。 */
class SegmentDisplay : public juce::Component
{
public:
    SegmentDisplay() { setInterceptsMouseClicks (false, false); }

    void setText (const juce::String& newText)
    {
        if (text == newText)
            return;

        text = newText;
        repaint();
    }

    void setTextColour (juce::Colour newColour)
    {
        if (colour == newColour)
            return;

        colour = newColour;
        repaint();
    }

    void setPlainFontHeight (float height) { plainFontHeight = height; }

    void paint (juce::Graphics& g) override
    {
        if (SevenSegment::canDraw (text))
        {
            SevenSegment::draw (g, getLocalBounds().toFloat(), text, colour);
            return;
        }

        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (plainFontHeight, juce::Font::bold)));
        g.drawText (text, getLocalBounds(), juce::Justification::centred);
    }

private:
    juce::String text;
    juce::Colour colour { AppColours::textPrimary };
    float plainFontHeight = 22.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentDisplay)
};

//==============================================================================
/**
    8.177：**押して書き換えられるラベルのまま、数字だけ棒で描く**（Phase 219）。

    BPMや拍子は`juce::Label`で、**クリックすると入力欄になります**。
    `SegmentDisplay`（ただのComponent）に置き換えると、その機能が消えます。

    そこで`paint()`だけ差し替えました。**編集中は基底に任せます**——
    打ち込んでいる最中の文字が棒になったら、何を書いているか分かりません。
*/
class SegmentLabel : public juce::Label
{
public:
    using juce::Label::Label;

    void paint (juce::Graphics& g) override
    {
        if (isBeingEdited() || ! SevenSegment::canDraw (getText()))
        {
            juce::Label::paint (g);
            return;
        }

        SevenSegment::draw (g, getLocalBounds().toFloat(), getText(),
                             findColour (juce::Label::textColourId),
                             getJustificationType());
    }
};
