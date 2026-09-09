#include "PianoRollTrackList.h"
#include "AppColours.h"
#include "Utf8.h"

PianoRollTrackList::PianoRollTrackList (ProjectModel& projectToUse)
    : project (projectToUse)
{
    updateSubscription();
    refresh();
}

PianoRollTrackList::~PianoRollTrackList()
{
    subscribedState.removeListener (this);
}

void PianoRollTrackList::updateSubscription()
{
    auto currentState = project.getState();

    if (subscribedState == currentState)
        return;

    subscribedState.removeListener (this);
    subscribedState = currentState;
    subscribedState.addListener (this);
}

void PianoRollTrackList::refresh()
{
    updateSubscription();

    visibleTrackIndices.clearQuick();

    // **MIDIトラックだけ**を並べる（ピアノロールで編集できるのはMIDIノート）
    for (int t = 0; t < project.getNumTracks(); ++t)
        if (project.getTrack (t).getType() == TrackType::Midi)
            visibleTrackIndices.add (t);

    repaint();
}

void PianoRollTrackList::setSelectedTrackId (const juce::String& trackId)
{
    if (selectedTrackId == trackId)
        return;

    selectedTrackId = trackId;
    repaint();
}

//==============================================================================
// モデルの変化に追従する（1.15：見張っていれば、誰が変えても同じように反映される）
//==============================================================================

void PianoRollTrackList::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    // 名前・色・ソロ・ミュート・透かしは一覧の見た目そのもの
    if (property == IDs::trackName || property == IDs::trackColor
         || property == IDs::solo || property == IDs::mute
         || property == IDs::trackWatermark)
        repaint();
}

void PianoRollTrackList::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (IDs::TRACK))
        refresh();
}

void PianoRollTrackList::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (IDs::TRACK))
        refresh();
}

void PianoRollTrackList::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (IDs::TRACKS))
        refresh();
}

//==============================================================================
// 配置
//==============================================================================

juce::Rectangle<int> PianoRollTrackList::getRowBounds (int visiblePosition) const
{
    return { 0, visiblePosition * rowHeight, getWidth(), rowHeight };
}

juce::Rectangle<int> PianoRollTrackList::getSoloButtonBounds (int visiblePosition) const
{
    return getChipBounds (visiblePosition, 2);
}

juce::Rectangle<int> PianoRollTrackList::getMuteButtonBounds (int visiblePosition) const
{
    return getChipBounds (visiblePosition, 1);
}

juce::Rectangle<int> PianoRollTrackList::getWatermarkButtonBounds (int visiblePosition) const
{
    return getChipBounds (visiblePosition, 0);
}

juce::Rectangle<int> PianoRollTrackList::getChipBounds (int visiblePosition, int indexFromRight) const
{
    auto row = getRowBounds (visiblePosition);

    // **右から数える。** 行の幅が変わっても、S・M・○の並びは崩れない
    return { row.getRight() - (buttonSize + 2) * (indexFromRight + 1),
             row.getY() + (rowHeight - buttonSize) / 2,
             buttonSize, buttonSize };
}

//==============================================================================
// 描画
//==============================================================================

void PianoRollTrackList::paint (juce::Graphics& g)
{
    g.fillAll (AppColours::panel);

    if (visibleTrackIndices.isEmpty())
    {
        g.setColour (AppColours::textSecondary);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (utf8 ("MIDIトラックがありません"), getLocalBounds().reduced (6, 8),
                           juce::Justification::centredTop, 3);
        return;
    }

    g.setFont (juce::FontOptions (11.0f));

    for (int i = 0; i < visibleTrackIndices.size(); ++i)
    {
        auto track = project.getTrack (visibleTrackIndices[i]);
        auto row = getRowBounds (i);

        const bool isSelected = (track.getId() == selectedTrackId);

        // **選択中の行はアレンジ画面のヘッダーと同じ見せ方**（8.18）にしてある。
        // 画面をまたいで「いま選んでいるもの」の見え方が違うと読み替えが要る
        g.setColour (isSelected ? AppColours::purple.withAlpha (0.25f) : AppColours::panel);
        g.fillRect (row);

        // 色帯（トラックの色。アレンジ画面のヘッダーと同じ位置）
        g.setColour (juce::Colour::fromString (track.getColourString()));
        g.fillRect (row.getX(), row.getY() + 1, colourBandWidth, row.getHeight() - 2);

        // 名前。**S／M／○のぶんは空けておく**（重なると名前が読めない）
        g.setColour (isSelected ? AppColours::textPrimary : AppColours::textSecondary);
        g.drawText (track.getName(),
                     row.getX() + colourBandWidth + 4, row.getY(),
                     row.getWidth() - colourBandWidth - (buttonSize + 2) * 3 - 8, row.getHeight(),
                     juce::Justification::centredLeft, true);

        // ソロ／ミュート。**アレンジ画面と同じ色分け**（S＝purple、M＝orange。8.18）
        auto drawChip = [&g] (juce::Rectangle<int> bounds, const juce::String& text,
                               bool isOn, juce::Colour onColour)
        {
            g.setColour (isOn ? onColour : AppColours::background);
            g.fillRect (bounds);
            g.setColour (AppColours::border);
            g.drawRect (bounds);

            g.setColour (isOn ? juce::Colours::white : AppColours::textSecondary);
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.drawText (text, bounds, juce::Justification::centred);
        };

        drawChip (getSoloButtonBounds (i), "S", track.isSoloed(), AppColours::purple);
        drawChip (getMuteButtonBounds (i), "M", track.isMuted(), AppColours::orange);

        // 8.1のD5：透かしの入切（Phase 74）。
        //
        // **文字ではなく丸そのものを描く。** 「○」のような記号はフォントに
        // 無いことがあり、四角や「…」に化けます（1.30）。
        // **色はそのトラックの色**：グリッドに出る透かしと同じ色なので、
        // どの帯がどの行のものか一目で分かる
        {
            auto bounds = getWatermarkButtonBounds (i);
            const bool isOn = track.isWatermarkVisible();
            const auto trackColour = juce::Colour::fromString (track.getColourString());

            g.setColour (AppColours::background);
            g.fillRect (bounds);
            g.setColour (AppColours::border);
            g.drawRect (bounds);

            auto circle = bounds.reduced (4).toFloat();

            if (isOn)
            {
                g.setColour (trackColour);
                g.fillEllipse (circle);
            }

            g.setColour (isOn ? trackColour.darker (0.4f) : AppColours::textSecondary);
            g.drawEllipse (circle, 1.0f);
        }

        g.setColour (AppColours::border.withAlpha (0.5f));
        g.drawHorizontalLine (row.getBottom() - 1, (float) row.getX(), (float) row.getRight());

        g.setFont (juce::FontOptions (11.0f));
    }
}

//==============================================================================
// 操作
//==============================================================================

void PianoRollTrackList::mouseDown (const juce::MouseEvent& e)
{
    const int visiblePosition = e.y / juce::jmax (1, rowHeight);

    if (! juce::isPositiveAndBelow (visiblePosition, visibleTrackIndices.size()))
        return;

    auto track = project.getTrack (visibleTrackIndices[visiblePosition]);

    // **ソロ／ミュートを先に見ること**（行の選択より先。アレンジ画面と同じ順番。8.18）
    if (getSoloButtonBounds (visiblePosition).contains (e.getPosition()))
    {
        project.beginAction (utf8 ("ソロの切り替え"));
        track.setSoloed (! track.isSoloed(), &project.getUndoManager());

        if (onMixerValueChanged != nullptr)
            onMixerValueChanged();

        repaint();
        return;
    }

    if (getMuteButtonBounds (visiblePosition).contains (e.getPosition()))
    {
        project.beginAction (utf8 ("ミュートの切り替え"));
        track.setMuted (! track.isMuted(), &project.getUndoManager());

        if (onMixerValueChanged != nullptr)
            onMixerValueChanged();

        repaint();
        return;
    }

    // 8.1のD5：透かしの入切（Phase 74）。**Undoには積みません**
    // （画面の見え方であって、曲の中身ではない。8.14の刻みと同じ扱い）
    if (getWatermarkButtonBounds (visiblePosition).contains (e.getPosition()))
    {
        track.setWatermarkVisible (! track.isWatermarkVisible());
        repaint();
        return;
    }

    if (onTrackSelected != nullptr)
        onTrackSelected (track.getId());
}
