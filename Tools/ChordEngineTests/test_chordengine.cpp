// ChordEngine.h（仕様書5.11.2）の値を手で確かめるための使い捨てテスト。
#include "ChordEngine.h"

#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;

static void checkFloat (const char* label, float got, float expected)
{
    const bool ok = std::fabs (got - expected) < 0.0005f;
    if (! ok) ++failures;
    std::printf ("%-34s got=%.4f expected=%.4f %s\n",
                 label, got, expected, ok ? "OK" : "*** NG ***");
}

static void checkInt (const char* label, int got, int expected)
{
    const bool ok = (got == expected);
    if (! ok) ++failures;
    std::printf ("%-34s got=%d expected=%d %s\n", label, got, expected, ok ? "OK" : "*** NG ***");
}

// 行を「列番号:コード名」で1行に潰す（歯抜けはそのまま抜ける）
static std::string rowToText (const ChordGridRow& row)
{
    std::string out;
    for (int col = 0; col < 12; ++col)
        if (row.cells[(size_t) col].hasChord)
        {
            if (! out.empty()) out += " ";
            out += std::to_string (col) + ":" + row.cells[(size_t) col].chord.getName().s;
        }
    return out;
}

static int cellCount (const ChordGridRow& row)
{
    int n = 0;
    for (const auto& c : row.cells)
        if (c.hasChord) ++n;
    return n;
}

static void checkRow (const ChordGrid& grid, int index, const char* label,
                      int expectedCells, const char* expectedText)
{
    if (index >= (int) grid.rows.size())
    {
        ++failures;
        std::printf ("*** NG 行 %d が無い\n", index);
        return;
    }

    const auto& row = grid.rows[(size_t) index];
    const std::string got = rowToText (row);
    const bool ok = (row.label.s == label)
                 && (cellCount (row) == expectedCells)
                 && (got == expectedText);
    if (! ok) ++failures;

    std::printf ("%-14s %2d個 %s\n", row.label.s.c_str(), cellCount (row), ok ? "OK" : "*** NG ***");
    std::printf ("    got      : %s\n", got.c_str());
    if (! ok)
        std::printf ("    expected : %s（%d個、ラベル\"%s\"）\n", expectedText, expectedCells, label);
}

int main()
{
    const Scale cMajor { 0, false };
    const Scale aMinor { 9, true  };

    std::printf ("=== C major / Main ページ / 7th（列番号はキーのルートからの半音）===\n");
    const ChordGrid main = buildChordGrid (cMajor, ChordGridPage::main, false);
    checkInt ("Mainページの行数", (int) main.rows.size(), 12);

    checkRow (main, 0, "Diatonic", 7,
              "0:CM7 2:Dm7 4:Em7 5:FM7 7:G7 9:Am7 11:Bm7b5");
    checkRow (main, 1, "6th", 6,
              "0:C6 2:Dm6 4:Em6 5:F6 7:G6 9:Am6");
    checkRow (main, 2, "Sec.Dom", 6,
              "2:A7 4:B7 5:C7 7:D7 9:E7 11:F#7");
    checkRow (main, 3, "Aug Sec.D", 6,
              "2:Aaug7 4:Baug7 5:Caug7 7:Daug7 9:Eaug7 11:F#aug7");
    checkRow (main, 4, "Passing dim", 5,
              "1:C#dim7 3:D#dim7 6:F#dim7 8:G#dim7 10:A#dim7");
    checkRow (main, 5, "diminish", 12,
              "0:Cdim7 1:C#dim7 2:Ddim7 3:D#dim7 4:Edim7 5:Fdim7 6:F#dim7 "
              "7:Gdim7 8:G#dim7 9:Adim7 10:A#dim7 11:Bdim7");
    checkRow (main, 6, "II (II-V)", 6,
              "2:Em7 4:F#m7 5:Gm7 7:Am7 9:Bm7 11:C#m7");
    checkRow (main, 7, "IIm7b5 (II-V)", 6,
              "2:Em7b5 4:F#m7b5 5:Gm7b5 7:Am7b5 9:Bm7b5 11:C#m7b5");
    checkRow (main, 8, "Sub Sec.D", 6,
              "2:D#7 4:F7 5:F#7 7:G#7 9:A#7 11:C7");
    checkRow (main, 9, "sus2", 7,
              "0:Csus2 2:Dsus2 4:Esus2 5:Fsus2 7:Gsus2 9:Asus2 11:Bsus2");
    checkRow (main, 10, "sus4", 7,
              "0:C7sus4 2:D7sus4 4:E7sus4 5:F7sus4 7:G7sus4 9:A7sus4 11:B7sus4");
    checkRow (main, 11, "add9", 6,
              "0:Cadd9 2:Dmadd9 4:Emadd9 5:Fadd9 7:Gadd9 9:Amadd9");

    std::printf ("\n=== C major / Main / Triadモード（先頭2行だけ）===\n");
    const ChordGrid triad = buildChordGrid (cMajor, ChordGridPage::main, true);
    checkRow (triad, 0, "Diatonic", 7, "0:C 2:Dm 4:Em 5:F 7:G 9:Am 11:Bdim");
    checkRow (triad, 10, "sus4", 7, "0:Csus4 2:Dsus4 4:Esus4 5:Fsus4 7:Gsus4 9:Asus4 11:Bsus4");

    std::printf ("\n=== A minor / Main（先頭1行）===\n");
    const ChordGrid aMain = buildChordGrid (aMinor, ChordGridPage::main, false);
    checkRow (aMain, 0, "Diatonic", 7, "0:Am7 2:Bm7b5 3:CM7 5:Dm7 7:Em7 8:FM7 10:G7");

    std::printf ("\n=== Related ページ ===\n");
    const ChordGrid rel = buildChordGrid (cMajor, ChordGridPage::related, false);
    checkInt ("C major の Related 行数", (int) rel.rows.size(), 7);
    checkRow (rel, 4, "Rel NM", 7, "0:CM7 2:Dm7 4:Em7 5:FM7 7:G7 9:Am7 11:Bm7b5");

    const ChordGrid relMinor = buildChordGrid (aMinor, ChordGridPage::related, false);
    checkInt ("A minor の Related 行数", (int) relMinor.rows.size(), 5);
    checkRow (relMinor, 1, "HM", 7, "0:AmM7 2:Bm7b5 3:CM7+5 5:Dm7 7:E7 8:FM7 11:G#dim7");

    std::printf ("\n=== 繋がりやすさスコア ===\n");
    const Chord c    { 0, ChordType::Maj  };
    const Chord g7   { 7, ChordType::Dom7 };
    const Chord dm7  { 2, ChordType::Min7 };
    const Chord ebm7 { 3, ChordType::Min7 };

    // 先頭：base 0.15 + ダイアトニック 0.30 + 先頭ボーナス 0.25
    checkFloat ("先頭の C（Cメジャー）", connectionScore (nullptr, c, cMajor), 0.70f);

    // G7→C：0.15 + 0.30 + ルートモーション5度下降 0.35 + ドミナント解決 0.15 + 共通音1つ 0.04
    checkFloat ("G7 -> C", connectionScore (&g7, c, cMajor), 0.99f);

    // Dm7→G7：0.15 + 0.30 + 0.35 + 共通音2つ 0.08（Dm7はドミナントではない）
    checkFloat ("Dm7 -> G7", connectionScore (&dm7, g7, cMajor), 0.88f);

    // スケール外のコードは低い：0.15 + 0（非ダイアトニック・ルートも外）
    //  + ルートモーション(3-7=-4→8) 0.05 + 共通音（G7{7,11,2,5} vs Ebm7{3,6,10,1}）0
    checkFloat ("G7 -> Ebm7（スケール外）", connectionScore (&g7, ebm7, cMajor), 0.20f);

    // 重み付けを差し替えられること（仕様書10.6）
    ChordScoreWeights weights;
    weights.rootMotion[5] = 0.0f;
    checkFloat ("重みを変えた G7 -> C", connectionScore (&g7, c, cMajor, weights), 0.64f);

    std::printf ("\n=== ダイアトニック判定 ===\n");
    checkInt ("G7 は C major の中",   isChordDiatonic (g7, cMajor)   ? 1 : 0, 1);
    checkInt ("Ebm7 は C major の外", isChordDiatonic (ebm7, cMajor) ? 1 : 0, 0);

    std::printf ("\n失敗 %d 件\n", failures);
    return failures == 0 ? 0 : 1;
}
