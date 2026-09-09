#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "ProjectModel.h"

//==============================================================================
/**
    仕様書5.10「MIDIファイル書き出し」。

    プロジェクト内のMIDIクリップを、標準MIDIファイル（SMF形式1）として書き出す。
    MIDIトラック1本につき1トラックを作り、先頭にテンポトラックを置く。

    ノートの時刻は秒で保持している（ProjectModelのNote）ため、
    プロジェクトのテンポを使ってティックへ変換する。
*/
class MidiFileExporter
{
public:
    /** プロジェクトのMIDIノートをファイルへ書き出す。
        失敗した場合はエラーメッセージを返す（成功時は空文字）。 */
    static juce::String exportToFile (const ProjectModel& project, const juce::File& file);

private:
    // 4分音符あたりのティック数。細かすぎると数値が大きくなり、粗いと
    // クオンタイズしていないノートの位置がずれる。960は多くのDAWが使う値。
    static constexpr int ticksPerQuarterNote = 960;

    /** 8.134：BPMを、SMFのテンポメタイベントが要る「4分音符あたりのマイクロ秒」へ
        （Phase 171／8.105の宿題1）。**式を1箇所に置くため**の小物。 */
    static int microsecondsPerQuarterNote (double bpm)
    {
        return (int) (60.0 / juce::jmax (1.0, bpm) * 1000000.0);
    }
};
