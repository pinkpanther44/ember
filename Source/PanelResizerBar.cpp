#include "PanelResizerBar.h"
#include "AppColours.h"

PanelResizerBar::PanelResizerBar (Edge edgeToUse)
    : edge (edgeToUse)
{
    setMouseCursor (isHorizontalBar() ? juce::MouseCursor::UpDownResizeCursor
                                       : juce::MouseCursor::LeftRightResizeCursor);
}

void PanelResizerBar::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::panel);

    // 掴めることが分かるよう、中央に短い線を引く
    const auto centre = getLocalBounds().getCentre().toFloat();
    const auto half = 14.0f;

    g.setColour (AppColours::border);

    if (isHorizontalBar())
        g.drawLine (centre.x - half, centre.y, centre.x + half, centre.y, 1.5f);
    else
        g.drawLine (centre.x, centre.y - half, centre.x, centre.y + half, 1.5f);
}

void PanelResizerBar::mouseDown (const juce::MouseEvent& e)
{
    // 親（＝メインウィンドウ）ではなくパネルの大きさが要る。ヘッダーの説明を参照。
    jassert (getCurrentSize != nullptr);

    dragStartSize = getCurrentSize != nullptr ? getCurrentSize() : 0;
    dragStartScreenPosition = e.getScreenPosition();
}

void PanelResizerBar::mouseDrag (const juce::MouseEvent& e)
{
    if (onSizeDragged == nullptr || dragStartSize <= 0)
        return;

    const auto delta = e.getScreenPosition() - dragStartScreenPosition;

    switch (edge)
    {
        // 右端の帯を右へ動かす（xが増える）とパネルは広がる
        case Edge::Right: onSizeDragged (dragStartSize + delta.x); break;

        // 左端の帯は逆。左へ動かす（xが減る）と広がる
        case Edge::Left:  onSizeDragged (dragStartSize - delta.x); break;

        // 上端の帯は、上へ動かす（yが減る）と高くなる
        case Edge::Top:   onSizeDragged (dragStartSize - delta.y); break;
    }
}
