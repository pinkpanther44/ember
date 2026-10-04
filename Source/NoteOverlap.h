#pragma once

#include <algorithm>
#include <vector>

/**
    8.344：**同じ音程のノートの重なりを解く**（Phase 333／本人の報告「同じ音程を連続で打つと、たまに鳴らない」）。

    MIDI のノートオフは「その音程」を止めるだけで、どのノートオンのものかを区別できません。
    同じ音程の2つが重なったまま送ると、**前のノートのオフが、後から鳴らしたノートを途中で止めます**。

    - 前のノートの終わりを、**次のノートの頭で切る**
    - **頭が同じ2つは長いほうだけ残す**（オン2つ・オフ2つになって、片方のオフで両方止まるため）

    **再生（`MidiPlayerProcessor`）と MIDI ファイルの書き出し（`MidiFileExporter`）の両方がこれを通ります。**
    片方だけ変えると「鳴っている内容」と「書き出した内容」が食い違います（1.14。チョークと同じ決まり）。

    並びは「音程 → 頭」の順に並べ替わります。呼んだあとで一覧の位置（添え字）を使わないこと。
*/
template <typename Note, typename Time>
void trimSamePitchOverlaps (std::vector<Note>& notes, int Note::* pitch, Time Note::* start, Time Note::* end)
{
    std::sort (notes.begin(), notes.end(), [&] (const Note& a, const Note& b)
    {
        if (a.*pitch != b.*pitch) return a.*pitch < b.*pitch;
        if (a.*start != b.*start) return a.*start < b.*start;
        return a.*end > b.*end;   // 頭が同じなら長いほうを前に（残すほう）
    });

    std::vector<bool> drop (notes.size(), false);

    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (drop[i])
            continue;

        for (size_t j = i + 1; j < notes.size() && notes[j].*pitch == notes[i].*pitch; ++j)
        {
            if (drop[j])
                continue;

            if (! (notes[i].*start < notes[j].*start))
            {
                drop[j] = true;   // 頭が同じ短いほう
                continue;
            }

            notes[i].*end = std::min (notes[i].*end, notes[j].*start);
            break;                // 次に鳴るのは j。その先は j が見る
        }
    }

    size_t kept = 0;

    for (size_t i = 0; i < notes.size(); ++i)
        if (! drop[i])
            notes[kept++] = notes[i];

    notes.resize (kept);
}
