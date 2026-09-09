#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppColours.h"

//==============================================================================
/**
    8.132：**トランスポートのボタンを絵にする**（Phase 168／改善案リスト3の44。実験枠）。

    「再生」などのボタンを、文字ではなく記号で出す試みです。

    ### 戻し方

    `AppColours::useTransportIcons`を`false`にするだけです。
    **文字（"Play" / "Stop" / "Rec" / "|<"）はボタンに設定したまま残してある**ので、
    falseにすればPhase 167までの見た目に戻ります。

    ### なぜ画像ファイルではなく`Path`か

    形が単純（三角・四角・丸・棒）で、**コードで書けば大きさも色も自由**だからです。
    画像だと拡大で滲み、テーマごと・状態ごとに用意することになります。

    **本人が絵を用意する場合はSVGを1つずつ**で足ります（色違いは要りません）。
    `juce::Drawable::createFromSVG()`で読み、`replaceColour()`で塗り替えます——
    そのときは**単色のベタ塗り**で描いてもらうこと（塗り分けがあると、
    色ごとに差し替えを書くことになります）。
*/
namespace TransportIcons
{
    enum class Icon
    {
        none,
        play,       ///< 右向きの三角
        stop,       ///< 四角
        record,     ///< 丸
        goToStart   ///< 縦棒＋左向きの三角
    };

    /** `area`に収まる大きさで記号の形を作る。**中央に寄せる**。 */
    inline juce::Path makeIconPath (Icon icon, juce::Rectangle<float> area)
    {
        juce::Path path;

        // **正方形に収めてから中央へ置く。** ボタンの縦横比はまちまちなので、
        // area をそのまま使うと記号が潰れます
        const float size = juce::jmin (area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

        const float x = box.getX();
        const float y = box.getY();
        const float w = box.getWidth();
        const float h = box.getHeight();

        switch (icon)
        {
            case Icon::play:
                path.addTriangle (x, y, x, y + h, x + w * 0.92f, y + h * 0.5f);
                break;

            case Icon::stop:
                // **少し小さめに。** 四角は同じ大きさでも三角より重く見えます
                path.addRectangle (box.reduced (w * 0.08f));
                break;

            case Icon::record:
                path.addEllipse (box.reduced (w * 0.04f));
                break;

            case Icon::goToStart:
                // 縦棒（曲の頭）＋左向きの三角（戻る）
                path.addRectangle (x, y, w * 0.18f, h);
                path.addTriangle (x + w, y, x + w, y + h, x + w * 0.28f, y + h * 0.5f);
                break;

            case Icon::none:
            default:
                break;
        }

        return path;
    }

    //==========================================================================
    /** 記号を描くボタン。**中身は`juce::TextButton`のまま**です。

        文字も設定したまま残してあるので、`AppColours::useTransportIcons`を
        `false`にすれば、そのまま文字のボタンに戻ります。 */
    class IconButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;

        void setIcon (Icon newIcon)
        {
            if (icon == newIcon)
                return;

            icon = newIcon;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
        {
            if (! AppColours::useTransportIcons || icon == Icon::none)
            {
                juce::TextButton::paintButton (g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
                return;
            }

            // 地は今までどおりLookAndFeelに任せる（色の決め方を変えない）
            getLookAndFeel().drawButtonBackground (g, *this,
                                                    findColour (getToggleState() ? buttonOnColourId
                                                                                 : buttonColourId),
                                                    shouldDrawButtonAsHighlighted,
                                                    shouldDrawButtonAsDown);

            // **記号の色は文字の色と同じもの**を使う。
            // 呼び出し側は`textColourOffId`／`textColourOnId`で色を決めているので、
            // ここを別立てにすると、再生中だけ記号が沈む、といったことが起きます
            g.setColour (findColour (getToggleState() ? juce::TextButton::textColourOnId
                                                       : juce::TextButton::textColourOffId));

            g.fillPath (makeIconPath (icon, getLocalBounds().toFloat().reduced (iconInset)));
        }

    private:
        /** ボタンの枠と記号のあいだ。**多めに取ること**：記号は文字より重いので、
            詰めると押しにくそうに見えます。 */
        static constexpr float iconInset = 9.0f;

        Icon icon = Icon::none;
    };
}
