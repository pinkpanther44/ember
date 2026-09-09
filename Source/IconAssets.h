#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppColours.h"
#include "BinaryData.h"

//==============================================================================
/**
    8.133：**本人が用意した絵**（Phase 169／改善案リスト3の44）。

    `Resources/Icons/`のSVGをexeへ埋め込んであります（CMakeの`juce_add_binary_data`）。
    外のファイルを読む形にすると、実行ファイルだけ渡したときにアイコンが出ません。

    ### ファイル名はASCII

    `juce_add_binary_data`は**ファイル名からC++の識別子を作る**ので、
    日本語名だと壊れます。元の名前との対応：

    | 埋め込み名 | 元のファイル |
    |---|---|
    | `tool_select.svg` | 選択ツールボタン.svg |
    | `tool_pen.svg` | ペンツールボタン.svg |
    | `tool_cut.svg` | カットツールボタン.svg |
    | `tool_eraser.svg` | 消しゴムツールボタン.svg |
    | `transport_loop.svg` | ループボタン.svg |
    | `transport_metronome.svg` | メトロノームボタン.svg |
    | `browser_folder.svg` | Browser内フォルダアイコン.svg |
    | `browser_file.svg` | Browser内ファイルアイコン.svg |
    | `plugin_bypass.svg` | バイパスボタン.svg（Phase 210） |
    | `plugin_pin.svg` | 画面固定ボタン.svg（Phase 210） |

    ### 色は塗り替える

    **もらった絵は単色のベタ塗り**（ツール類は黒、ブラウザのフォルダは紫、
    ファイルはオレンジ）なので、`Drawable::replaceColour()`で**その1色を
    好きな色へ差し替えられます**。テーマの明暗も、押されているかどうかも、
    こちらで決められるので、**絵は1つで足ります**（色違いを用意してもらう必要が無い）。
*/
namespace IconAssets
{
    /** 埋め込んであるSVGを読む。名前は`BinaryData`の名前（"tool_select_svg"など）。 */
    inline std::unique_ptr<juce::Drawable> load (const char* binaryDataName)
    {
        int size = 0;

        if (const auto* data = BinaryData::getNamedResource (binaryDataName, size))
            return juce::Drawable::createFromImageData (data, (size_t) size);

        return {};
    }

    /** 単色のベタ塗りを`colour`へ塗り替えた複製を返す。

        **元の色を知らなくても済むように、絵に出てくる色を全部差し替えます。**
        もらった絵が単色である前提の作りで、**塗り分けのある絵では効きません**
        （そのときは`replaceColour()`を色ごとに呼ぶことになる）。 */
    inline std::unique_ptr<juce::Drawable> loadTinted (const char* binaryDataName, juce::Colour colour)
    {
        auto drawable = load (binaryDataName);

        if (drawable == nullptr)
            return {};

        // 本人からもらった絵に出てくる色（黒・紫・オレンジ）を、まとめて差し替える。
        // **含まれていない色を渡しても何も起きない**ので、並べておいて構わない
        for (const auto& original : { juce::Colour (0xff000000), juce::Colour (0xff8c52ff),
                                       juce::Colour (0xffff914d) })
            drawable->replaceColour (original, colour);

        return drawable;
    }

    //==========================================================================
    /** 絵を描くボタン。**中身は`juce::TextButton`のまま**です。

        文字も設定したまま残してあるので、`AppColours::useTransportIcons`を
        `false`にすれば、そのまま文字のボタンに戻ります（8.132と同じ形）。

        **色は文字の色から取ります**（`textColourOffId` / `textColourOnId`）。
        呼び出し側が既に決めている色をそのまま使うので、
        **押されているとき・選ばれているときの見え方が今までと変わりません**。 */
    class SvgButton : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;

        void setIconResource (const char* binaryDataName)
        {
            resourceName = binaryDataName;
            cachedColour = juce::Colour();   // 次の描画で作り直させる
            repaint();
        }

        /** 8.171：**枠と絵のあいだを狭める**（Phase 210）。

            既定（6px）はトランスポートのような**大きめのボタン**に合わせた値です。
            プラグインの帯のように**24pxしかないボタン**では、
            既定のままだと絵が10pxまで縮んで何の絵か分かりません。

            **触らなければ今までどおり**です。 */
        void setIconInset (float newInset)
        {
            iconInset = newInset;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override
        {
            if (! AppColours::useTransportIcons || resourceName == nullptr)
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

            const auto wanted = findColour (getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId);

            // **色が変わったときだけ作り直す。** SVGの読み直しは安くないので、
            // 毎フレーム作ると再生中に効いてきます
            if (drawable == nullptr || cachedColour != wanted)
            {
                drawable = loadTinted (resourceName, wanted);
                cachedColour = wanted;
            }

            if (drawable == nullptr)
                return;

            // **元の絵は正方形**（1500x1500）なので、正方形に収めてから中央へ置く。
            // ボタンの縦横比はまちまちなので、そのまま伸ばすと潰れます
            const auto area = getLocalBounds().toFloat().reduced (iconInset);
            const float size = juce::jmin (area.getWidth(), area.getHeight());

            drawable->drawWithin (g, juce::Rectangle<float> (size, size).withCentre (area.getCentre()),
                                   juce::RectanglePlacement::centred, 1.0f);
        }

    private:
        /** ボタンの枠と絵のあいだ。**多めに取ること**：絵は文字より重いので、
            詰めると押しにくそうに見えます。 */
        float iconInset = 6.0f;

        const char* resourceName = nullptr;
        std::unique_ptr<juce::Drawable> drawable;
        juce::Colour cachedColour;
    };
}
