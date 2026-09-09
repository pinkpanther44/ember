// 本物のJUCEヘッダで ChordModel.h / ChordEngine.h がコンパイルできるかだけを見る。
//
// 他の test_*.cpp はスタブ（juce_core/juce_core.h）でコンパイルするので、
// 「値は合っているが、本物のJUCEでは通らない」状態を見逃してしまう。
// これはリンクせず `/c` で止めるので、JUCEをビルドする必要はない。
//
// **エンジンをどの.cppからもincludeしていない間は、これが唯一のコンパイル確認**
// になる（ヘッダオンリーなので、本体をビルドしても1行もコンパイルされない）。
#include "ChordModel.h"
#include "ChordEngine.h"

int compileCheck()
{
    const Scale key { 0, false };
    const ChordGrid grid = buildChordGrid (key, ChordGridPage::main, false);
    const Chord first { 0, ChordType::Maj7 };

    float total = 0.0f;
    int   count = 0;

    for (const auto& row : grid.rows)
        for (const auto& cell : row.cells)
            if (cell.hasChord)
            {
                total += connectionScore (&first, cell.chord, key);
                count += (int) Voicing::piano (cell.chord, 0, 0).size();
                count += (int) Voicing::guitarPitches (cell.chord, 0, true).size();
                count += Voicing::guitarShape (cell.chord, false).baseFret;

                ChordPerformance performance;
                performance.stroke = ChordStroke::alternate;
                count += (int) generateChordNotes (cell.chord, 0.0, 4.0, performance).size();
            }

    return (int) total + count + grid.rows.front().label.length();
}
