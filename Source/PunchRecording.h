#pragma once

#include "ProjectModel.h"
#include "MidiRecording.h"

#include <set>

/**
    8.360：**パンチイン／アウト**——録れたものを範囲の中だけにする（Phase 349／本人の要望）。

    > 「録音のパンチイン／アウトって実装できるかな？」

    本人と決めたこと：

    | | |
    |---|---|
    | 範囲 | **ループとは別の専用の範囲**（ルーラーの赤い帯。`ProjectModel::getPunchStartTime()`など） |
    | 範囲の中の古いもの | **差し替える**（古いクリップは範囲の端で割って中を消す、古いノートは消す。Undo で戻る） |
    | 対象 | **オーディオと MIDI の両方** |

    ### 作り

    録音そのものは**今までどおり回し続ける**（カウントイン・ずれの補正・ファイルの書き出しは変えない）。
    止めたあとで、**録れた範囲とパンチ範囲の重なり**（`effectiveRange()`）だけを残す：

    - オーディオ：ファイルは削らず、**クリップの開始・長さ・オフセット**で範囲だけを見せる（あとで端を広げ直せる）
    - MIDI：範囲の中で**押し始めた**音だけを入れ、範囲の終わりで切る。CC も範囲の中だけ（`MidiRecording::applyTake()`）

    差し替えるのは**実際に録れた範囲の中だけ**（範囲の途中から録り始めたら、その手前の古い音は残す）。

    ここは画面にもエンジンにも触らない（モデルだけ）ので、窓を出さずに自己検査する（`--storage-selftest`）。
*/
namespace PunchRecording
{
    /** 録れた範囲（`recordStart`〜`recordEnd`）とパンチ範囲の重なり。重ならなければ空。 */
    inline juce::Range<double> effectiveRange (double recordStart, double recordEnd, double punchStart, double punchEnd)
    {
        const auto recorded = juce::Range<double> (juce::jmin (recordStart, recordEnd), juce::jmax (recordStart, recordEnd));
        const auto punch = juce::Range<double> (juce::jmin (punchStart, punchEnd), juce::jmax (punchStart, punchEnd));
        const auto overlap = recorded.getIntersectionWith (punch);
        return overlap.getLength() > 1.0e-9 ? overlap : juce::Range<double>();
    }

    /** トラックのクリップのうち、`range`にかかる部分を消す。**範囲の端で割り、中の切れ端を消す**。
        消した切れ端の数を返す。 */
    int clearAudioRange (Track track, juce::Range<double> range, juce::UndoManager* undoManager);

    /** 録れたファイルを、`range`の部分だけのクリップとして置く。
        `recordStart`はファイルの頭が当たる曲の時刻、`recordedLength`はファイルの長さ（秒）。 */
    AudioClip placeAudio (Track track, const juce::String& filePath, double recordStart, double recordedLength,
                          juce::Range<double> range, juce::UndoManager* undoManager);

    /** `range`の中で始まるノートを消し、手前から範囲へはみ出しているノートは範囲の頭で切る。
        消した数を返す（切ったものは数えない）。 */
    int clearNotesInRange (Track track, juce::Range<double> range, juce::UndoManager* undoManager);

    /** `range`の中の CC のうち、`controllers`に挙げた番号のものを消す（録り直した種類だけ差し替える）。 */
    int clearCCInRange (Track track, juce::Range<double> range, const std::set<int>& controllers,
                        juce::UndoManager* undoManager);

    //==========================================================================
    // 録音を止めたあとの手順（`ArrangeView`が呼ぶ）。**自己検査もこれを呼ぶ**ので、画面の手順と検査がずれない

    /** オーディオ：`keep`の古いクリップを消してから、録れたファイルを`keep`だけのクリップで置く。差し替えた切れ端の数を返す。 */
    int replaceAudio (Track track, const juce::String& filePath, double recordStart, double recordedLength,
                      juce::Range<double> keep, juce::UndoManager* undoManager);

    struct MidiResult
    {
        MidiRecording::Summary summary;
        int replacedNotes = 0;
    };

    /** MIDI：録り分の行き先のトラックで、`keep`の古いノートと、録り直した番号の CC を消してから、`keep`の中だけを入れる。 */
    MidiResult replaceMidi (ProjectModel& project, const RecordedMidiTake& take, double sampleRate, double stopSeconds,
                            juce::Range<double> keep, juce::UndoManager* undoManager);
}
