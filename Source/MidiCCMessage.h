#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "ProjectModel.h" // MidiControllersの特殊値

//==============================================================================
/**
    仕様書5.3.3：CCイベント（`CCEvent`）から、実際に送るMIDIメッセージを作る。

    **なぜProjectModelに置かないか**：`juce::MidiMessage`は`juce_audio_basics`にあり、
    データモデル層（`juce_data_structures`）に持ち込むとモジュール依存が増える
    （設計書1.1の層分離。トラックカラーでColourを避けているのと同じ理由）。

    **なぜ共有するか**：この対応表が要る場所は「再生」（`MidiPlayerProcessor`）と
    「MIDI書き出し」（`MidiFileExporter`）の2つある。片方だけ直すと
    「鳴っている内容と書き出した内容が違う」という食い違いになり、
    しかも気づきにくい（HANDOVER 1.14・8.2で繰り返し出てくるパターン）。
*/
namespace MidiCCMessage
{
    /** コントローラー番号（MidiControllersの特殊値を含む）と値から、MIDIメッセージを作る。

        channelは1〜16。値の範囲はコントローラーの種類によって違うため、
        呼び出し側で`MidiControllers::getMaxValue()`の範囲へ収めておくこと。 */
    inline juce::MidiMessage create (int channel, int controllerNumber, int value)
    {
        if (controllerNumber == MidiControllers::pitchBend)
            return juce::MidiMessage::pitchWheel (channel, juce::jlimit (0, 16383, value));

        if (controllerNumber == MidiControllers::channelPressure)
            return juce::MidiMessage::channelPressureChange (channel, juce::jlimit (0, 127, value));

        return juce::MidiMessage::controllerEvent (channel,
                                                    juce::jlimit (0, 127, controllerNumber),
                                                    juce::jlimit (0, 127, value));
    }
}
