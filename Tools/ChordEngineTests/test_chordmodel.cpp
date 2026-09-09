// ChordModel.h（仕様書5.11.1）の値を手で確かめるための使い捨てテスト。
// HANDOVER 8.5「移植したらコード→構成音の配列を数個手で確かめてから次へ進むこと」に対応。
#include "ChordModel.h"

#include <cstdio>
#include <algorithm>
#include <string>

static int failures = 0;

static std::string tonesToText (const Chord& c)
{
    std::string out;
    for (const auto& [pc, deg] : c.getTones())
    {
        if (! out.empty()) out += " ";
        out += std::to_string (pc);
    }
    return out;
}

static void checkTones (const char* label, const Chord& c, const char* expected)
{
    const std::string got = tonesToText (c);
    const bool ok = (got == expected);
    if (! ok) ++failures;
    std::printf ("%-28s %-24s tones=[%-18s] expected=[%-18s] %s\n",
                 label, c.getName().toRawUTF8(), got.c_str(), expected,
                 ok ? "OK" : "*** NG ***");
}

static void checkName (const char* label, const std::string& got, const char* expected)
{
    const bool ok = (got == expected);
    if (! ok) ++failures;
    std::printf ("%-28s got=\"%s\" expected=\"%s\" %s\n",
                 label, got.c_str(), expected, ok ? "OK" : "*** NG ***");
}

static void checkInt (const char* label, int got, int expected)
{
    const bool ok = (got == expected);
    if (! ok) ++failures;
    std::printf ("%-28s got=%d expected=%d %s\n",
                 label, got, expected, ok ? "OK" : "*** NG ***");
}

int main()
{
    std::printf ("=== 構成音（ピッチクラス 0=C ... 11=B）===\n");

    checkTones ("C  (Maj)",     Chord { 0,  ChordType::Maj  },        "0 4 7");
    checkTones ("Am7",          Chord { 9,  ChordType::Min7 },        "9 0 4 7");
    checkTones ("G7",           Chord { 7,  ChordType::Dom7 },        "7 11 2 5");
    checkTones ("Bdim7",        Chord { 11, ChordType::Dim7 },        "11 2 5 8");
    checkTones ("Csus2",        Chord { 0,  ChordType::Sus2 },        "0 2 7");
    checkTones ("Cadd9",        Chord { 0,  ChordType::Add9 },        "0 4 7 2");
    checkTones ("FM7",          Chord { 5,  ChordType::Maj7 },        "5 9 0 4");
    checkTones ("Bm7b5",        Chord { 11, ChordType::Min7Flat5 },   "11 2 5 9");
    checkTones ("C#m7(9)/E",    Chord { 1,  ChordType::Min7, Tension::Nine, 4 }, "1 4 8 11 3");
    checkTones ("G7(b9,13)",    Chord { 7,  ChordType::Dom7, Tension::FlatNine | Tension::Thirteen, -1 },
                                                                      "7 11 2 5 8 4");

    std::printf ("\n=== 表示名 ===\n");
    checkName ("C#m7(9)/E",  Chord { 1, ChordType::Min7, Tension::Nine, 4 }.getName().s,  "C#m7(9)/E");
    checkName ("C（無印）",  Chord { 0, ChordType::Maj }.getName().s,                     "C");
    checkName ("ベース=ルート", Chord { 0, ChordType::Maj, 0, 0 }.getName().s,             "C");
    checkName ("G7sus4",     Chord { 7, ChordType::Dom7Sus4 }.getName().s,                "G7sus4");

    std::printf ("\n=== スケール ===\n");
    const Scale cMajor { 0, false };
    const Scale aMinor { 9, true  };

    checkName ("C major の名前", cMajor.getName().s, "C Major");
    checkName ("A minor の名前", aMinor.getName().s, "A Minor");

    std::printf ("C major degreeRoot : ");
    for (int i = 0; i < 7; ++i) std::printf ("%d ", cMajor.degreeRoot (i));
    std::printf ("(expected 0 2 4 5 7 9 11)\n");
    const int expectedCMajor[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        if (cMajor.degreeRoot (i) != expectedCMajor[i]) { ++failures; std::printf ("*** NG degree %d\n", i); }

    std::printf ("A minor degreeRoot : ");
    for (int i = 0; i < 7; ++i) std::printf ("%d ", aMinor.degreeRoot (i));
    std::printf ("(expected 9 11 0 2 4 5 7)\n");
    const int expectedAMinor[7] = { 9, 11, 0, 2, 4, 5, 7 };
    for (int i = 0; i < 7; ++i)
        if (aMinor.degreeRoot (i) != expectedAMinor[i]) { ++failures; std::printf ("*** NG degree %d\n", i); }

    // 負の度数・12超えのピッチクラスでも折り返すこと（移植時に添字を触ったので確認）
    checkInt ("degreeRoot(-1) = VII",  cMajor.degreeRoot (-1), 11);
    checkInt ("degreeRoot(7)  = I",    cMajor.degreeRoot (7),  0);
    checkInt ("pitchClassName(-1)=B",  std::string (pitchClassName (-1).toRawUTF8()) == "B" ? 1 : 0, 1);
    checkInt ("contains(C#) in Cmaj",  cMajor.contains (1) ? 1 : 0, 0);
    checkInt ("contains(B) in Cmaj",   cMajor.contains (11) ? 1 : 0, 1);
    checkInt ("contains(-1=B) in Cmaj", cMajor.contains (-1) ? 1 : 0, 1);
    checkInt ("contains(G#) in Amin",  aMinor.contains (8) ? 1 : 0, 0);

    std::printf ("\nC major のダイアトニック7th : ");
    for (int i = 0; i < 7; ++i)
    {
        Chord c { cMajor.degreeRoot (i), cMajor.diatonicSeventh (i) };
        std::printf ("%s ", c.getName().toRawUTF8());
    }
    std::printf ("\n  (expected CM7 Dm7 Em7 FM7 G7 Am7 Bm7b5)\n");

    std::printf ("A minor のダイアトニック7th  : ");
    for (int i = 0; i < 7; ++i)
    {
        Chord c { aMinor.degreeRoot (i), aMinor.diatonicSeventh (i) };
        std::printf ("%s ", c.getName().toRawUTF8());
    }
    std::printf ("\n  (expected Am7 Bm7b5 CM7 Dm7 E7 FM7 G7)\n");

    std::printf ("A harmonic minor の7th       : ");
    for (int i = 0; i < 7; ++i)
    {
        const int r = (9 + scaleFamilySteps (ScaleFamily::harmonicMinor)[i]) % 12;
        Chord c { r, scaleFamilySeventh (ScaleFamily::harmonicMinor, i) };
        std::printf ("%s ", c.getName().toRawUTF8());
    }
    std::printf ("\n  (expected AmM7 Bm7b5 CM7+5 Dm7 E7 FM7 G#dim7)\n");

    std::printf ("\n=== 22種すべてが構成音を返すか ===\n");
    for (int t = 0; t < (int) ChordType::NumTypes; ++t)
    {
        Chord c { 0, (ChordType) t };
        const auto tones = c.getTones();
        const bool ok = tones.size() >= 3;
        if (! ok) ++failures;
        std::printf ("%2d %-8s tones=%zu %s\n", t, chordTypeSuffix ((ChordType) t).toRawUTF8(),
                     tones.size(), ok ? "" : "*** NG ***");
    }

    std::printf ("\n失敗 %d 件\n", failures);
    return failures == 0 ? 0 : 1;
}
