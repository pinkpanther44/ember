// ChordEngine.h のボイシング（仕様書5.11.3）を手で確かめるための使い捨てテスト。
#include "ChordEngine.h"

#include <cstdio>
#include <cmath>
#include <string>

static int failures = 0;

static std::string listToText (const std::vector<int>& values)
{
    std::string out;
    for (int v : values)
    {
        if (! out.empty()) out += " ";
        out += std::to_string (v);
    }
    return out;
}

static void checkList (const char* label, const std::vector<int>& got, const char* expected)
{
    const std::string text = listToText (got);
    const bool ok = (text == expected);
    if (! ok) ++failures;
    std::printf ("%-30s [%-22s] expected=[%-22s] %s\n",
                 label, text.c_str(), expected, ok ? "OK" : "*** NG ***");
}

static void checkFrets (const char* label, const Voicing::GuitarShape& shape,
                        const char* expected, int expectedBaseFret)
{
    std::string text;
    for (int f : shape.frets)
    {
        if (! text.empty()) text += " ";
        text += (f < 0) ? std::string ("x") : std::to_string (f);
    }

    const bool ok = (text == expected) && (shape.baseFret == expectedBaseFret);
    if (! ok) ++failures;
    std::printf ("%-30s [%-18s] base=%d expected=[%-18s] base=%d %s\n",
                 label, text.c_str(), shape.baseFret, expected, expectedBaseFret,
                 ok ? "OK" : "*** NG ***");
}

static void checkInt (const char* label, int got, int expected)
{
    const bool ok = (got == expected);
    if (! ok) ++failures;
    std::printf ("%-30s got=%d expected=%d %s\n", label, got, expected, ok ? "OK" : "*** NG ***");
}

static void checkDouble (const char* label, double got, double expected)
{
    const bool ok = std::fabs (got - expected) < 1.0e-6;
    if (! ok) ++failures;
    std::printf ("%-30s got=%.4f expected=%.4f %s\n", label, got, expected, ok ? "OK" : "*** NG ***");
}

int main()
{
    const Chord c    { 0,  ChordType::Maj };
    const Chord cOnE { 0,  ChordType::Maj, 0, 4 };   // C/E
    const Chord f    { 5,  ChordType::Maj };
    const Chord am   { 9,  ChordType::Min };
    const Chord b    { 11, ChordType::Maj };
    const Chord g7   { 7,  ChordType::Dom7 };

    std::printf ("=== ピアノボイシング（36=C2, 60=C4）===\n");
    checkList ("C 基本形",       Voicing::piano (c, 0, 0),    "36 60 64 67");
    checkList ("C 第1転回",      Voicing::piano (c, 1, 0),    "36 64 67 72");
    checkList ("C 第2転回",      Voicing::piano (c, 2, 0),    "36 67 72 76");
    checkList ("C/E（分数）",    Voicing::piano (cOnE, 0, 0), "40 60 64 67");
    checkList ("G7 基本形",      Voicing::piano (g7, 0, 0),   "43 67 71 74 77");
    checkList ("C +1オクターブ", Voicing::piano (c, 0, 1),    "48 72 76 79");
    // 下限（C2=36）を割ったベース音は落ちる。移植元と同じ挙動
    checkList ("C -1オクターブ", Voicing::piano (c, 0, -1),   "48 52 55");

    std::printf ("\n=== ギターの押さえ方（低い弦から。x=ミュート）===\n");
    checkFrets ("C オープン",     Voicing::guitarShape (c,  false), "x 3 2 0 1 0", 0);
    checkFrets ("F バレー（E型）", Voicing::guitarShape (f,  true),  "1 3 3 2 1 1", 1);
    checkFrets ("Am バレー（E型）", Voicing::guitarShape (am, true),  "5 7 7 5 5 5", 5);
    checkFrets ("B バレー（A型）", Voicing::guitarShape (b,  true),  "x 2 4 4 4 2", 2);

    checkList ("C オープンの音",  Voicing::guitarPitches (c, 0, false), "48 52 55 60 64");
    checkList ("F バレーの音",    Voicing::guitarPitches (f, 0, true),  "41 48 53 57 60 65");

    std::printf ("\n=== ノート生成（仕様書5.11.3の発音パラメータ）===\n");
    {
        // ロングトーン：コード区間4拍、GT80%
        ChordPerformance performance;
        const auto notes = generateChordNotes (c, 0.0, 4.0, performance);

        checkInt    ("ロングトーンの音数", (int) notes.size(), 4);
        checkInt    ("最低音", notes.front().pitch, 36);
        checkDouble ("長さ = 4拍 * 0.8", notes.front().lengthBeats, 3.2);
        checkDouble ("開始 = 区間の頭",   notes.front().startBeats, 0.0);
    }
    {
        // 4分音符で4拍ぶん敷き詰める
        ChordPerformance performance;
        performance.hitLengthBeats = 1.0;
        const auto notes = generateChordNotes (c, 2.0, 4.0, performance);

        checkInt    ("4分×4回×4音", (int) notes.size(), 16);
        checkDouble ("1回目の開始", notes.front().startBeats, 2.0);
        checkDouble ("最後の開始",  notes.back().startBeats,  5.0);
        checkDouble ("1音の長さ",   notes.back().lengthBeats, 0.8);
    }
    {
        // ダウンストローク：低い音から0.05拍ずつずれる
        ChordPerformance performance;
        performance.stroke = ChordStroke::down;
        performance.strokeDeviationBeats = 0.05;
        const auto notes = generateChordNotes (c, 0.0, 4.0, performance);

        checkInt    ("ダウンの音数",   (int) notes.size(), 4);
        checkInt    ("1音目は最低音",  notes[0].pitch, 36);
        checkDouble ("2音目のずれ",    notes[1].startBeats, 0.05);
        checkDouble ("4音目のずれ",    notes[3].startBeats, 0.15);
        checkDouble ("4音目の長さ",    notes[3].lengthBeats, (4.0 - 0.15) * 0.8);
    }
    {
        // アップストローク：高い音から
        ChordPerformance performance;
        performance.stroke = ChordStroke::up;
        performance.strokeDeviationBeats = 0.05;
        const auto notes = generateChordNotes (c, 0.0, 4.0, performance);

        checkInt ("1音目は最高音", notes[0].pitch, 67);
        checkInt ("4音目は最低音", notes[3].pitch, 36);
    }
    {
        // 交互ストローク：2回目だけ逆向き
        ChordPerformance performance;
        performance.hitLengthBeats = 2.0;
        performance.stroke = ChordStroke::alternate;
        performance.strokeDeviationBeats = 0.05;
        const auto notes = generateChordNotes (c, 0.0, 4.0, performance);

        checkInt ("交互の音数",         (int) notes.size(), 8);
        checkInt ("1回目の1音目は低音", notes[0].pitch, 36);
        checkInt ("2回目の1音目は高音", notes[4].pitch, 67);
    }
    {
        // ずらし幅が区間より大きいと、はみ出す音は鳴らさない
        ChordPerformance performance;
        performance.stroke = ChordStroke::down;
        performance.strokeDeviationBeats = 0.5;
        const auto notes = generateChordNotes (c, 0.0, 1.0, performance);

        checkInt ("区間1拍・ずれ0.5拍", (int) notes.size(), 2);
    }
    {
        // ギターモード
        ChordPerformance performance;
        performance.guitar = true;
        const auto notes = generateChordNotes (c, 0.0, 4.0, performance);

        checkInt ("ギターの音数", (int) notes.size(), 5);
        checkInt ("最低音",       notes.front().pitch, 48);
    }
    {
        // 長さ0の区間はノートを作らない
        ChordPerformance performance;
        checkInt ("長さ0の区間", (int) generateChordNotes (c, 0.0, 0.0, performance).size(), 0);
    }

    std::printf ("\n失敗 %d 件\n", failures);
    return failures == 0 ? 0 : 1;
}
