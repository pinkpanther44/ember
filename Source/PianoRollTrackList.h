#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "ProjectModel.h"

//==============================================================================
/**
    ピアノロールの左に置くMIDIトラックの一覧（設計書2.3.3／8.1のG4・D5、Phase 73）。

    **ツールバーのトラック選択コンボの置き換えです。** コンボは
    「いま何を編集しているか」しか出せず、他のトラックの様子（ソロ／ミュート）を
    見るにはアレンジ画面へ戻る必要がありました。

    出すのは**MIDIトラックだけ**：ピアノロールで編集できるのはMIDIノートなので、
    オーディオやセンドを並べても選べません。

    **描くのは自前**（`juce::ListBox`を使っていない）：行の中身が
    「色帯＋名前＋S／M」と決まっていて、アレンジ画面のトラックヘッダーと
    同じ形にしたかったためです（8.18）。行数は多くありません。
*/
class PianoRollTrackList : public juce::Component,
                            private juce::ValueTree::Listener
{
public:
    explicit PianoRollTrackList (ProjectModel& projectToUse);
    ~PianoRollTrackList() override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;

    /** 編集中のトラック（trackId）を教える。一覧の選択表示に使う。 */
    void setSelectedTrackId (const juce::String& trackId);

    /** 一覧を作り直す（トラックが増減した／プロジェクトが変わった）。 */
    void refresh();

    /** 行が選ばれたときに呼ばれる。**自分では編集対象を変えない**
        （どのクリップを開くかを決めるのはビューの仕事）。 */
    std::function<void (const juce::String& trackId)> onTrackSelected;

    /** ソロ／ミュートを変えたときに呼ばれる（音へ反映するのは呼び出し側）。 */
    std::function<void()> onMixerValueChanged;

    /** 一覧の幅。**ヘッダーの角（鍵盤の幅）とは別物**なので、揃える必要はない。 */
    static constexpr int preferredWidth = 168;

private:
    static constexpr int rowHeight = 22;
    static constexpr int colourBandWidth = 4;
    static constexpr int buttonSize = 15;

    /** 一覧に出すトラックの番号（MIDIトラックだけ）。
        **画面上の並び順とプロジェクトの番号は一致しません。** */
    juce::Array<int> visibleTrackIndices;

    juce::Rectangle<int> getRowBounds (int visiblePosition) const;
    juce::Rectangle<int> getSoloButtonBounds (int visiblePosition) const;
    juce::Rectangle<int> getMuteButtonBounds (int visiblePosition) const;

    /** 8.1のD5：透かしの入切（Phase 74）。**S／Mの右隣**に置く。 */
    juce::Rectangle<int> getWatermarkButtonBounds (int visiblePosition) const;

    /** 行の右端から`indexFromRight`番目の丸／四角の位置。
        **右から数える**ので、行の幅が変わっても並びが崩れない。 */
    juce::Rectangle<int> getChipBounds (int visiblePosition, int indexFromRight) const;

    /** プロジェクトのルートへの購読を張り直す（読み込みでルートが差し替わるため。3.1）。 */
    void updateSubscription();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;

    ProjectModel& project;
    juce::ValueTree subscribedState;
    juce::String selectedTrackId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollTrackList)
};
