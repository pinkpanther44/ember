#include "SelectionState.h"

void SelectionState::selectNone()
{
    setSelection (Type::None, {}, -1);
}

void SelectionState::selectTrack (const juce::String& newTrackId)
{
    setSelection (Type::Track, newTrackId, -1);
}

void SelectionState::selectClip (const juce::String& newTrackId, int newClipIndex, bool isMidiClip)
{
    setSelection (isMidiClip ? Type::MidiClip : Type::AudioClip, newTrackId, newClipIndex);
}

void SelectionState::selectAutomationLane (const juce::String& newTrackId, const juce::String& targetId)
{
    // 8.59：**対象の識別子まで見て通知を決める**（Phase 96）。
    // 同じトラックの別のレーンへ移ったときに、通知が飛ばないと
    // インスペクタが前のレーンを見せたままになる
    setSelection (Type::AutomationLane, newTrackId, -1, targetId);
}

void SelectionState::setSelection (Type newType, const juce::String& newTrackId, int newClipIndex,
                                    const juce::String& newAutomationTargetId)
{
    if (type == newType && trackId == newTrackId && clipIndex == newClipIndex
         && automationTargetId == newAutomationTargetId)
        return; // 変化していないなら通知しない（クリックのたびにUIを作り直さないため）

    type = newType;
    trackId = newTrackId;
    clipIndex = newClipIndex;
    automationTargetId = newAutomationTargetId;

    sendChangeMessage();
}
