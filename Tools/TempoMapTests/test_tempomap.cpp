// Source/TempoMap.h の値を、アプリを起動せずに確かめる（Phase 140／HANDOVER 8.102）。
//
// **テンポと拍子の変化点は、画面に出るまで正しさを確かめられません。**
// しかも間違っていても「なんとなくずれている」としか見えないので、
// 数値で押さえておくところ（8.4の「耳で分かるもの・分からないもの」）。
//
// juce_core のスタブは Tools/ChordEngineTests のものを使い回します
// （TempoMap.h が juce::jmax しか使っていないため）。

#include "TempoMap.h"
#include "KeyMap.h"      // 仕様書5.11.1：キーの変化点（Phase 143）

#include <cstdio>
#include <cmath>

static int failures = 0;
static int checks = 0;

static void expectNear (const char* what, double actual, double expected, double tolerance = 1.0e-9)
{
    ++checks;

    if (std::fabs (actual - expected) <= tolerance)
        return;

    ++failures;
    std::printf ("  NG  %-46s actual=%.9f expected=%.9f\n", what, actual, expected);
}

static void expectInt (const char* what, int actual, int expected)
{
    ++checks;

    if (actual == expected)
        return;

    ++failures;
    std::printf ("  NG  %-46s actual=%d expected=%d\n", what, actual, expected);
}

static void section (const char* title)
{
    std::printf ("\n--- %s\n", title);
}

//==============================================================================
int main()
{
    section ("変化点が無いとき（Phase 139までと同じ答えになること）");
    {
        TempoMap map;
        map.initialTempo = 120.0;      // 1拍 = 0.5秒
        map.initialBeatsPerBar = 4;    // 1小節 = 2.0秒

        expectNear ("拍 -> 秒 (8拍)",        map.getTimeForBeat (8.0), 4.0);
        expectNear ("秒 -> 拍 (4秒)",        map.getBeatAtTime (4.0), 8.0);
        expectNear ("小節2の頭は8拍目",      map.getBeatForBarStart (2), 8.0);
        expectInt  ("小節2の拍数",           map.getBeatsPerBarAtBar (2), 4);
        expectNear ("小節3の頭は6.0秒",      map.getTimeForBeat (map.getBeatForBarStart (3)), 6.0);

        const auto position = map.getBarPositionAtBeat (9.5);
        expectInt  ("9.5拍は2小節目",         position.bar, 2);
        expectNear ("小節の中では1.5拍",      position.beatsIntoBar, 1.5);
    }

    section ("テンポの変化点（拍で持つ）");
    {
        TempoMap map;
        map.initialTempo = 120.0;                 // 0〜8拍: 1拍 0.5秒
        map.initialBeatsPerBar = 4;
        map.tempoChanges.push_back ({ 8.0, 60.0 });  // 8拍目以降: 1拍 1.0秒
        map.sortAndDeduplicate();

        expectNear ("変化点の手前 (7拍)",     map.getTimeForBeat (7.0), 3.5);
        expectNear ("変化点ちょうど (8拍)",   map.getTimeForBeat (8.0), 4.0);
        expectNear ("変化点の先 (12拍)",      map.getTimeForBeat (12.0), 8.0);

        // **往復して戻ること。** ここが合わないと、寄せた位置がじわじわずれます（8.101）
        expectNear ("往復 12拍 -> 秒 -> 拍",  map.getBeatAtTime (map.getTimeForBeat (12.0)), 12.0);
        expectNear ("往復 3.25拍",            map.getBeatAtTime (map.getTimeForBeat (3.25)), 3.25);
        expectNear ("秒 -> 拍 (8.0秒)",       map.getBeatAtTime (8.0), 12.0);

        expectNear ("テンポ (7拍)",           map.getTempoAtBeat (7.0), 120.0);
        expectNear ("テンポ (8拍ちょうど)",   map.getTempoAtBeat (8.0), 60.0);

        // 3小節目（=8拍目）の頭で変わるので、そこから小節は4秒ぶんになる
        expectNear ("小節2の頭は4.0秒",       map.getTimeForBeat (map.getBeatForBarStart (2)), 4.0);
        expectNear ("小節3の頭は8.0秒",       map.getTimeForBeat (map.getBeatForBarStart (3)), 8.0);
    }

    section ("拍子の変化点（小節で持つ）");
    {
        TempoMap map;
        map.initialTempo = 120.0;                  // 1拍 = 0.5秒
        map.initialBeatsPerBar = 4;
        map.meterChanges.push_back ({ 2, 3 });     // 3小節目から3/4
        map.sortAndDeduplicate();

        expectNear ("小節2の頭は8拍目",        map.getBeatForBarStart (2), 8.0);
        expectNear ("小節3の頭は11拍目",       map.getBeatForBarStart (3), 11.0);
        expectNear ("小節4の頭は14拍目",       map.getBeatForBarStart (4), 14.0);

        expectInt  ("小節1の拍数",             map.getBeatsPerBarAtBar (1), 4);
        expectInt  ("小節2の拍数",             map.getBeatsPerBarAtBar (2), 3);

        expectNear ("小節3の頭は5.5秒",        map.getTimeForBeat (map.getBeatForBarStart (3)), 5.5);

        const auto position = map.getBarPositionAtBeat (12.0);
        expectInt  ("12拍は3小節目",            position.bar, 3);
        expectNear ("小節の中では1拍目",        position.beatsIntoBar, 1.0);

        // **小節の頭は必ず「小節の中で0拍目」に戻ること。**
        // ここがずれると、ルーラーの数字と小節線が食い違います
        for (int bar = 0; bar < 8; ++bar)
        {
            const auto atStart = map.getBarPositionAtBeat (map.getBeatForBarStart (bar));
            expectInt  ("小節の頭 -> 同じ小節番号", atStart.bar, bar);
            expectNear ("小節の頭 -> 0拍目",        atStart.beatsIntoBar, 0.0);
        }
    }

    section ("テンポと拍子を両方（互いに動かさないこと）");
    {
        TempoMap map;
        map.initialTempo = 120.0;
        map.initialBeatsPerBar = 4;
        map.meterChanges.push_back ({ 2, 3 });      // 3小節目から3/4
        map.tempoChanges.push_back ({ 11.0, 60.0 }); // 3小節目の頭（=11拍目）からテンポ半分
        map.sortAndDeduplicate();

        // 拍子は「小節 -> 拍」だけ、テンポは「拍 -> 秒」だけを決めるので、
        // **テンポを変えても、拍子の変わり目は11拍目のまま**
        expectNear ("小節3の頭は11拍目のまま",  map.getBeatForBarStart (3), 11.0);
        expectNear ("小節3の頭は5.5秒",         map.getTimeForBeat (11.0), 5.5);
        expectNear ("小節4の頭は8.5秒",         map.getTimeForBeat (map.getBeatForBarStart (4)), 8.5);

        expectNear ("往復 14拍",                map.getBeatAtTime (map.getTimeForBeat (14.0)), 14.0);
    }

    section ("並べ替えと重複の掃除");
    {
        TempoMap map;
        map.tempoChanges.push_back ({ 16.0, 90.0 });
        map.tempoChanges.push_back ({ 8.0, 60.0 });
        map.tempoChanges.push_back ({ 0.0, 200.0 });   // 0拍目は落とす（曲の頭の値の担当）
        map.meterChanges.push_back ({ 4, 5 });
        map.meterChanges.push_back ({ 0, 7 });         // 0小節目は落とす
        map.sortAndDeduplicate();

        expectInt  ("テンポの変化点は2つ",      (int) map.tempoChanges.size(), 2);
        expectNear ("1つ目は8拍",               map.tempoChanges[0].beatPosition, 8.0);
        expectNear ("2つ目は16拍",              map.tempoChanges[1].beatPosition, 16.0);
        expectInt  ("拍子の変化点は1つ",        (int) map.meterChanges.size(), 1);
        expectInt  ("その小節は4",              map.meterChanges[0].bar, 4);
    }

    section ("秒を経由した往復（レビュー#2の確認。Phase 145）");
    {
        // **小節の頭 -> 秒 -> 拍 -> 小節 が、同じ小節に戻ること。**
        // ここが戻らないと、置いた札が1小節手前に落ちます
        TempoMap map;
        map.initialTempo = 120.0;
        map.initialBeatsPerBar = 4;
        map.tempoChanges.push_back ({ 4.0, 90.0 });   // 4拍目から90BPM
        map.sortAndDeduplicate();

        for (int bar = 0; bar < 12; ++bar)
        {
            const double barStart = map.getTimeForBeat (map.getBeatForBarStart (bar));
            const auto back = map.getBarPositionAtBeat (map.getBeatAtTime (barStart));

            expectInt ("小節の頭 -> 秒 -> 小節", back.bar, bar);
        }
    }

    section ("キーの変化点（小節で持つ。Phase 143）");
    {
        KeyMap keys;
        keys.initialKey = Scale { 0, false };            // C major
        keys.changes.push_back ({ 8, Scale { 9, true } }); // 9小節目からAマイナー
        keys.changes.push_back ({ 4, Scale { 7, false } }); // 5小節目からGメジャー
        keys.changes.push_back ({ 0, Scale { 2, false } }); // 0小節目は落とす
        keys.sortAndDeduplicate();

        expectInt ("変化点は2つ",            (int) keys.changes.size(), 2);
        expectInt ("1つ目は4小節目",         keys.changes[0].bar, 4);
        expectInt ("2つ目は8小節目",         keys.changes[1].bar, 8);

        expectInt ("小節0はC",               keys.getKeyAtBar (0).root, 0);
        expectInt ("小節3はC（変わる手前）", keys.getKeyAtBar (3).root, 0);
        expectInt ("小節4はG（ちょうど）",   keys.getKeyAtBar (4).root, 7);
        expectInt ("小節7はG",               keys.getKeyAtBar (7).root, 7);
        expectInt ("小節8はA",               keys.getKeyAtBar (8).root, 9);
        expectInt ("小節8はマイナー",        keys.getKeyAtBar (8).minor ? 1 : 0, 1);
        expectInt ("小節99もA（最後まで）",  keys.getKeyAtBar (99).root, 9);

        // 変化点が無ければ、曲の頭のキーだけで決まる（Phase 142までと同じ）
        KeyMap plain;
        plain.initialKey = Scale { 5, true };
        expectInt ("変化点なしは頭のキー",   plain.getKeyAtBar (42).root, 5);
    }

    std::printf ("\n%d件中 %d件が失敗\n", checks, failures);

    return failures > 0 ? 1 : 0;
}
