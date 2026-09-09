#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "AppColours.h"
#include "ProjectModel.h"   // 設計書2.4：見本は`Track::getColourPalette()`が持つ

//==============================================================================
/**
    設計書2.4：**トラックカラーの見本ボタン**（Phase 154で新設、Phase 161で共用へ）。

    `juce::TextButton`のままだと、選んでいる見本は`buttonOnColourId`＝パープルで
    **塗りつぶされ、見本の色そのものが見えなく**なります。
    「いまどの色か」を、その色で確かめられないのは本末転倒です（8.119）。

    **塗りは常に見本の色のまま、周りの枠だけを変えます。**
    枠の色は`textPrimary`——**見本には出てこない色**なので、
    どの色の上に乗っても枠として読めます（1.34：色は両テーマで確かめること）。

    > **`AppColours::purple`を枠に使わないこと。** 見本の2番目がほぼ同じ色で、
    > それを選んだときだけ枠が消えます。

    8.125：Phase 161で**インスペクタ専用から出しました**（改善案26）。
    トラックヘッダーの色帯からも同じパレットを出すので、
    見た目が2種類あると「別のもの」に見えます（1.27）。
*/
struct ColourSwatchButton : public juce::Button
{
    ColourSwatchButton() : juce::Button ({}) {}

    juce::Colour swatchColour;

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto area = getLocalBounds().toFloat();

        // **塗りはいつも見本の色。** 選んでいるかどうかで塗りを変えないこと——
        // 選んだ色が見えなくなるのが、そもそも直したかったこと
        g.setColour (swatchColour);
        g.fillRoundedRectangle (area.reduced (1.5f), 2.0f);

        // 選んでいるかどうかは**枠の色と太さ**で示す。
        // `textPrimary`は地の色との対比で決めてある色なので、**どの見本の上でも読める**
        const auto outlineColour = getToggleState()
                                      ? AppColours::textPrimary
                                      : (isMouseOver || isButtonDown ? AppColours::textSecondary
                                                                      : AppColours::border);

        g.setColour (outlineColour);
        g.drawRoundedRectangle (area.reduced (0.75f), 2.5f, getToggleState() ? 2.0f : 1.0f);
    }
};

//==============================================================================
/**
    8.125：**その場に出すカラーパレット**（Phase 161／改善案26）。

    トラックヘッダー左端の色帯を左クリックすると、`juce::CallOutBox`でこれが出ます。
    インスペクタまで行かずに色を変えられるようにするためのものです。

    **見本は`Track::getColourPalette()`から取ります**（1箇所。8.61）。
    ここで色を並べ直すと、インスペクタの並びと食い違います。
*/
class TrackColourPalette : public juce::Component
{
public:
    /** 選ばれたら呼ばれる。**閉じるのは呼び出し側の仕事**
        （`CallOutBox`は自分の親を知らないため）。 */
    std::function<void (juce::Colour)> onColourChosen;

    explicit TrackColourPalette (juce::Colour currentColour)
    {
        for (const auto& hex : Track::getColourPalette())
        {
            const auto colour = juce::Colour::fromString (hex);

            auto* swatch = swatches.add (new ColourSwatchButton());
            swatch->swatchColour = colour;

            // いま付いている色に枠を出す（どれが選ばれているか分かるように）
            swatch->setToggleState (colour == currentColour, juce::dontSendNotification);

            swatch->onClick = [this, colour]
            {
                if (onColourChosen != nullptr)
                    onColourChosen (colour);
            };

            addAndMakeVisible (swatch);
        }

        // 8.160：**段組みにします**（Phase 198/本人の要望）。
        //
        // Phase 197までは横一列でした（8色）。24色を一列に並べると
        // **画面からはみ出す**ので、`Track::getColourPaletteColumns()`で折り返します。
        // **数はモデル側が持っています**——ここで8と書くと、色を足したときにずれます（8.2）
        const int columns = juce::jmax (1, Track::getColourPaletteColumns());
        const int rows = (swatches.size() + columns - 1) / columns;

        setSize (swatchSize * columns + padding * 2,
                  swatchSize * rows + padding * 2);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (AppColours::panel);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (padding);
        const int columns = juce::jmax (1, Track::getColourPaletteColumns());

        for (int i = 0; i < swatches.size(); i += columns)
        {
            auto row = area.removeFromTop (swatchSize);

            for (int c = 0; c < columns && i + c < swatches.size(); ++c)
                swatches[i + c]->setBounds (row.removeFromLeft (swatchSize));
        }
    }

private:
    static constexpr int swatchSize = 22;
    static constexpr int padding = 6;

    juce::OwnedArray<ColourSwatchButton> swatches;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackColourPalette)
};
