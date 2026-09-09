#include "MusicalTimeBench.h"

#include "ProjectModel.h"
#include "Utf8.h"   // 日本語リテラルは必ずここを通すこと（`Utf8.h`の説明）

#include <cmath>

namespace MusicalTimeBench
{

namespace
{
    // 1トラックぶんのノート数。**1曲ぶんとしては多め**にしてあります——
    // 「重くなるならここ」を見るのが目的なので、軽い曲で測っても分かりません
    constexpr int numNotes = 20000;

    // 1回の計測でノートを何周するか。1周では時計の分解能に埋もれます
    constexpr int numPasses = 20;

    double toNanosecondsPerCall (juce::int64 ticks, juce::int64 numCalls)
    {
        const double seconds = (double) ticks / (double) juce::Time::getHighResolutionTicksPerSecond();

        return numCalls > 0 ? seconds * 1.0e9 / (double) numCalls : 0.0;
    }

    /** テンポの変化点を`count`個置く。**8小節ごとに1つ**、少しずつ速さを変えていく。

        **数を増やす向きにしか呼びません**（0 → 1 → 8 → …）ので、
        前の回に置いたものを消す必要はありません（同じ拍は上書きされます）。 */
    void setUpTempoChanges (ProjectModel& project, int count)
    {
        for (int i = 0; i < count; ++i)
            project.setTempoChange ((double) (i + 1) * 32.0, 100.0 + (double) (i % 60), nullptr);
    }

    /** ノートを`numNotes`個持つMIDIトラックを1本作って返す。 */
    Track buildTrack (ProjectModel& project)
    {
        auto track = project.addTrack ("Bench", TrackType::Midi);

        // **0.25秒おきに1つ。** 均等に散らすのは、テンポの変化点の走査距離を
        // ノートごとに変えて、平均が「曲全体の平均」になるようにするため
        for (int i = 0; i < numNotes; ++i)
            track.addNote (60 + (i % 24), 100, (double) i * 0.25, 0.2, nullptr);

        return track;
    }
}

bool runIfRequested (const juce::String& commandLine)
{
    if (! commandLine.contains ("--measure-musical-time"))
        return false;

    const auto outputFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("MantaStudio-MusicalTimeBench.txt");

    juce::StringArray lines;

    lines.add ("Manta Studio - musical time bench (HANDOVER 8.137/8.138 - Phase 175/176)");
    lines.add ("output   : " + outputFile.getFullPathName());
   #if JUCE_DEBUG
    lines.add (utf8 ("built    : Debug  **この数値は比較に使えません**（8.1のE1）"));
   #else
    lines.add ("built    : Release");
   #endif
    lines.add ("notes    : " + juce::String (numNotes) + " x " + juce::String (numPasses) + " passes");

    ProjectModel project;
    auto track = buildTrack (project);

    // ------------------------------------------------------------------------
    // まず**引き当てが効いているか**を確かめる（8.137）。
    //
    // `MusicalTime::findMapFor()`がプロジェクトを見つけられないと、
    // **既定の120BPMで換算して、黙って違う答えを返します**。速さより先に、
    // ここが合っていることを見ておくこと——**静かに間違うのがいちばん厄介**です
    {
        project.setTempo (90.0, nullptr);
        project.setTempoChange (16.0, 150.0, nullptr);

        auto note = track.getNote (777);

        const double viaProject = project.getBeatPositionAt (note.getStartTime());
        const double viaNote = note.getStartBeats();
        const bool sameBeats = std::abs (viaProject - viaNote) < 1.0e-6;

        // 拍で置き直して、秒が戻ってくるか（`setStartBeats`と`getStartTime`が対か）
        const double originalSeconds = note.getStartTime();
        note.setStartBeats (viaNote, nullptr);
        const bool roundTrips = std::abs (note.getStartTime() - originalSeconds) < 1.0e-6;

        lines.add (juce::String ("check    : ")
                    + (sameBeats ? "beats OK" : "beats **NG**")
                    + (roundTrips ? "  roundtrip OK" : "  roundtrip **NG**")
                    + "  (tempo 90 + change@16)");
        lines.add ("");

        // 測るのは既定の状態からにするので、置いた変化点は外しておく
        project.removeTempoChange (16.0, nullptr);
        project.setTempo (120.0, nullptr);
    }

    lines.add (utf8 ("1回あたりのナノ秒（小さいほど速い）"));
    lines.add ("");
    lines.add (utf8 ("  変化点   getStartTime()   getStartBeats()    差"));
    lines.add ("  ------   --------------   ---------------    ------");

    // **最適化で消されないように、結果を足し込んで最後に使う**
    volatile double sink = 0.0;

    for (const int numTempoChanges : { 0, 1, 8, 64, 256 })
    {
        setUpTempoChanges (project, numTempoChanges);

        // 表を1回作らせておく（作り直しの時間を計測に混ぜない）
        sink += project.getTempoMap().getBeatAtTime (1.0);

        // 8.138：**Phase 176で上下が入れ替わりました**。いまは
        // `getStartTime()`がテンポの表を引く側で、`getStartBeats()`が素の読み出しです
        double secondsSideNs = 0.0;   // getStartTime()  ＝ 拍から換算する
        double beatsSideNs = 0.0;     // getStartBeats() ＝ プロパティを1つ読むだけ

        {
            const auto start = juce::Time::getHighResolutionTicks();
            double total = 0.0;

            for (int pass = 0; pass < numPasses; ++pass)
                for (int n = 0; n < track.getNumNotes(); ++n)
                    total += track.getNote (n).getStartTime();

            secondsSideNs = toNanosecondsPerCall (juce::Time::getHighResolutionTicks() - start,
                                                   (juce::int64) numNotes * numPasses);
            sink += total;
        }

        {
            const auto start = juce::Time::getHighResolutionTicks();
            double total = 0.0;

            for (int pass = 0; pass < numPasses; ++pass)
                for (int n = 0; n < track.getNumNotes(); ++n)
                    total += track.getNote (n).getStartBeats();

            beatsSideNs = toNanosecondsPerCall (juce::Time::getHighResolutionTicks() - start,
                                                 (juce::int64) numNotes * numPasses);
            sink += total;
        }

        const double ratio = secondsSideNs > 0.0 ? beatsSideNs / secondsSideNs : 0.0;

        lines.add ("  " + juce::String (numTempoChanges).paddedLeft (' ', 6)
                    + juce::String (secondsSideNs, 1).paddedLeft (' ', 17)
                    + juce::String (beatsSideNs, 1).paddedLeft (' ', 18)
                    + ("   x" + juce::String (ratio, 2)).paddedLeft (' ', 10));
    }

    lines.add ("");
    lines.add (utf8 ("読み方"));
    lines.add (utf8 ("  ・**Phase 176で上下が入れ替わりました**（8.138）。"));
    lines.add (utf8 ("    いま重いのは getStartTime()（拍から換算する側）です。"));
    lines.add (utf8 ("    Phase 175では getStartBeats() がその位置にいました——**数字は同じはず**です"));
    lines.add (utf8 ("  ・1回あたりの差が、そのまま「1ノートあたり増えた時間」です"));
    lines.add (utf8 ("    20,000個のノートを1フレームで引くなら、差50nsで +1.0ms／フレーム"));
    lines.add (utf8 ("    （60fpsの1フレームは16.6ms）"));
    lines.add (utf8 ("  ・変化点を増やして伸びるなら、`TempoMap`の走査が効いています"));
    lines.add (utf8 ("    （そのときは索引を足す。8.105のPhase 152）"));
    lines.add ("");
    lines.add ("sink = " + juce::String ((double) sink, 3)
                + utf8 ("  （最適化除けなので、値に意味はありません）"));

    // **UTF-8で書きます。** 日本語のリテラルは`utf8()`を通すこと——
    // 通さないと、書き出したファイルの中で二重に化けます（`Utf8.h`）
    outputFile.replaceWithText (lines.joinIntoString (juce::newLine));

    return true;
}

} // namespace MusicalTimeBench
