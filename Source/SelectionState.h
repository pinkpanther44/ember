#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    設計書2.3.7「インスペクタパネル」のための、「いま何を選んでいるか」を持つクラス（Phase 17）。

    インスペクタは**選択対象に応じて表示が切り替わるコンテキストパネル**なので、
    「誰が選ばれているか」を一箇所で持ち、変化を購読できる必要がある。
    それまでは選択状態が`TimelineComponent`の中だけにあり、外から見えなかった。

    ProjectModelと違い、**これはプロジェクトに保存しない**。選択はセッション中の
    一時的な状態で、ファイルへ残す性質のものではないため。

    トラックの指定に番号ではなくIDを使うのは、このプロジェクト全体の方針に合わせたもの。
    トラックの並べ替えや増減があっても、選択が別のトラックへすり替わらない。

    変更の通知は`juce::ChangeBroadcaster`で行う（設計書1.2のObserverパターン）。
*/
class SelectionState : public juce::ChangeBroadcaster
{
public:
    enum class Type
    {
        None,
        Track,     // トラックそのもの（ヘッダーをクリック）
        AudioClip,
        MidiClip,
        /** 8.59：オートメーションの行（Phase 96）。**トラックと同じように選べる**。
            どのレーンかは`getAutomationTargetId()`で分かる。 */
        AutomationLane
    };

    SelectionState() = default;

    void selectNone();
    void selectTrack (const juce::String& trackId);

    /** クリップを選ぶ。clipIndexは「そのトラック内での、同じ種別のクリップの通し番号」
        （オーディオはgetClip、MIDIはgetMidiClipの引数と同じ数え方）。 */
    void selectClip (const juce::String& trackId, int clipIndex, bool isMidiClip);

    /** 8.59：オートメーションの行を選ぶ（Phase 96）。

        **trackIdが空文字ならマスター**のレーン。トラックと同じく**IDで持つ**ので、
        並べ替えや増減があっても別のレーンへすり替わらない（1.32）。 */
    void selectAutomationLane (const juce::String& trackId, const juce::String& targetId);

    Type getType() const                { return type; }
    juce::String getTrackId() const     { return trackId; }
    int getClipIndex() const            { return clipIndex; }

    /** 選んでいるオートメーションの対象（`AutomationTargets`の識別子）。
        `Type::AutomationLane`のときだけ意味を持つ。 */
    juce::String getAutomationTargetId() const { return automationTargetId; }

    bool isClipSelected() const         { return type == Type::AudioClip || type == Type::MidiClip; }
    bool isAutomationLaneSelected() const { return type == Type::AutomationLane; }

private:
    /** 中身が実際に変わったときだけ通知する（同じ選択を繰り返し設定しても鳴らさない）。 */
    void setSelection (Type newType, const juce::String& newTrackId, int newClipIndex,
                        const juce::String& newAutomationTargetId = {});

    Type type = Type::None;
    juce::String trackId;
    int clipIndex = -1;
    juce::String automationTargetId;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SelectionState)
};
