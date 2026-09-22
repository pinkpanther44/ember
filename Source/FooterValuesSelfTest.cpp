#include "FooterValuesSelfTest.h"

#include "ProjectModel.h"
#include "TransportBarComponent.h"   // 8.307：小節カウンタの文字と、フッターの絵（Phase 300）

#include <juce_events/juce_events.h>

#include <iostream>

namespace FooterValuesSelfTest
{
    namespace
    {
        int problems = 0;

        void say (const juce::String& line)
        {
            std::cout << line << std::endl;
        }

        void check (bool condition, const juce::String& what)
        {
            if (condition)
            {
                say ("  ok    " + what);
            }
            else
            {
                ++problems;
                say ("  FAIL  " + what);
            }
        }

        /** 4/4・145BPMの曲に、4小節目から120BPM・Dメジャー、8小節目から3/4を置く。

            **画面で置いたときと同じ形**にしてあります（本人の報告の絵がこれ）。 */
        void buildProject (ProjectModel& project)
        {
            project.setTempo (145.0, nullptr);
            project.setTimeSignature ("4/4", nullptr);

            // コードトラックが無いとキーは置けません（`setKeyChange()`の決まり）
            project.addTrack ("Chords", TrackType::Chord);
            project.setProjectKey (Scale { 0, false }, nullptr);   // C major

            // 4/4なので、4小節目の頭は12拍目
            project.setTempoChange (12.0, 120.0, nullptr);
            project.setKeyChange (3, Scale { 2, false }, nullptr); // 0始まりなので4小節目
            project.setTimeSignatureChange (7, "3/4", nullptr);    // 8小節目
        }
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--footer-selftest"))
            return false;

        problems = 0;

        say ("footer values self-test (8.268)");

        ProjectModel project;
        buildProject (project);

        const double barOne = project.getBarStartTime (0);
        const double barThree = project.getBarStartTime (2);   // 変化点の手前
        const double barFour = project.getBarStartTime (3);    // テンポとキーが変わる
        const double barEight = project.getBarStartTime (7);   // 拍子が変わる

        //----------------------------------------------------------------------
        // ① 映す値

        {
            const auto atStart = project.getValuesInForceAt (barOne);

            check (juce::approximatelyEqual (atStart.tempo, 145.0), "bar 1 shows 145");
            check (atStart.timeSignature == "4/4", "bar 1 shows 4/4");
            check (atStart.key.root == 0 && ! atStart.key.minor, "bar 1 shows C major");
            check (atStart.tempoBar < 0 && atStart.keyBar < 0 && atStart.timeSignatureBar < 0,
                    "bar 1 says the values come from the start of the song");

            const auto beforeChange = project.getValuesInForceAt (barThree);
            check (juce::approximatelyEqual (beforeChange.tempo, 145.0),
                    "bar 3 still shows 145 (before the change)");

            const auto atChange = project.getValuesInForceAt (barFour);

            check (juce::approximatelyEqual (atChange.tempo, 120.0), "bar 4 shows 120");
            check (atChange.key.root == 2, "bar 4 shows D");
            check (atChange.tempoBar == 3, "bar 4 says the tempo comes from bar 4");
            check (atChange.keyBar == 3, "bar 4 says the key comes from bar 4");

            // **拍子はまだ変わっていない**（8小節目から）
            check (atChange.timeSignature == "4/4", "bar 4 still shows 4/4");
            check (atChange.timeSignatureBar < 0,
                    "bar 4 says the time signature still comes from the start");

            const auto atMeter = project.getValuesInForceAt (barEight);
            check (atMeter.timeSignature == "3/4", "bar 8 shows 3/4");
            check (atMeter.timeSignatureBar == 7, "bar 8 says it comes from bar 8");
        }

        //----------------------------------------------------------------------
        // ② **書き換える先**。ここが本番です

        {
            // 変化点より手前で打つと、曲頭の値が変わる（Phase 269までと同じ）
            project.setTempoAtTime (barThree, 150.0, nullptr);

            check (juce::approximatelyEqual (project.getTempo(), 150.0),
                    "typing before the change edits the song's own tempo");
            check (juce::approximatelyEqual (project.getValuesInForceAt (barFour).tempo, 120.0),
                    "...and leaves the change at bar 4 alone");

            // 変化点より後ろで打つと、**その変化点**が変わる
            project.setTempoAtTime (barFour, 130.0, nullptr);

            check (juce::approximatelyEqual (project.getValuesInForceAt (barFour).tempo, 130.0),
                    "typing after the change edits the change");
            check (juce::approximatelyEqual (project.getTempo(), 150.0),
                    "...and leaves the song's own tempo alone");

            // **札が増えないこと。** カーソルの拍で置くと、同じ値の変化点が2つになります
            check (project.getTempoMap().tempoChanges.size() == 1,
                    "no second tempo marker appeared on the lane");
        }

        {
            project.setProjectKeyAtTime (barFour, Scale { 5, true }, nullptr);   // F minor

            const auto atChange = project.getValuesInForceAt (barFour);
            check (atChange.key.root == 5 && atChange.key.minor, "the key at bar 4 became F minor");
            check (project.getProjectKey().root == 0,
                    "...and the key at the start of the song is untouched");
            check (project.getKeyMap().changes.size() == 1,
                    "no second key marker appeared on the lane");
        }

        {
            project.setTimeSignatureAtTime (barEight, "6/8", nullptr);

            const auto atMeter = project.getValuesInForceAt (barEight);
            check (atMeter.timeSignature == "6/8", "the time signature at bar 8 became 6/8");
            check (project.getTimeSignature() == "4/4",
                    "...and the one at the start of the song is untouched");
            check (project.getTempoMap().meterChanges.size() == 1,
                    "no second time signature marker appeared on the lane");
        }

        //----------------------------------------------------------------------
        // 8.312：**ソロとミュートが両方押してあるとき**（Phase 305／本人の要望）。
        //
        // ここは「いま何が効いているか」を数える道具なので、
        // **どのトラックが鳴るか**も同じ場所で見ています
        // （画面を通さず`ProjectModel`に訊くだけ、という点も同じ）。

        say ("--- solo against mute");

        {
            ProjectModel song;
            song.createNewProject();

            auto a = song.addTrack ("A", TrackType::Midi);
            auto b = song.addTrack ("B", TrackType::Midi);

            check (song.isTrackAudible (a) && song.isTrackAudible (b),
                    "with nothing pressed, both are heard");

            // **ミュートだけなら、今までどおり黙る**
            a.setMuted (true, nullptr);
            check (! song.isTrackAudible (a), "muting a track silences it");
            check (song.isTrackAudible (b), "...and leaves the other one alone");

            //------------------------------------------------------------------
            // 8.313：**両方は点かない**（Phase 306／本人の指定）

            a.setSoloed (true, nullptr);

            check (a.isSoloed(), "pressing solo lights the solo button");
            check (! a.isMuted(), "...and takes the mute light off");
            check (song.isTrackAudible (a), "the track is heard");
            check (! song.isTrackAudible (b), "...and the other one is silenced by the solo");

            a.setMuted (true, nullptr);

            check (a.isMuted(), "pressing mute lights the mute button");
            check (! a.isSoloed(), "...and takes the solo light off");
            check (! song.isTrackAudible (a), "the track is silent again");
            check (song.isTrackAudible (b), "...and the other one is back, since no solo is left");

            // **外すほうでは消さない。** 消すと「ミュートを外したらソロも外れる」になります
            a.setSoloed (true, nullptr);
            a.setMuted (false, nullptr);

            check (a.isSoloed(), "taking the mute off does not take the solo off");

            //------------------------------------------------------------------
            // 8.312：**まとめて掛かったミュートとの関係**（Phase 305）。
            //
            // 同じ行のM・Sは両方点かなくなりましたが（8.313）、
            // **フォルダやVCAから掛かるミュート**は別の口から来るので、
            // 「その行のS」と同時に立ち得ます。**そのときはSが勝ちます。**

            auto folder = song.addTrack ("Group", TrackType::Folder);

            a.setParentFolderId (folder.getId(), nullptr);
            b.setParentFolderId (folder.getId(), nullptr);

            a.setSoloed (false, nullptr);
            a.setMuted (false, nullptr);
            b.setSoloed (false, nullptr);
            b.setMuted (false, nullptr);

            // フォルダごと黙らせてから、中の1本をソロにする
            folder.setMuted (true, nullptr);

            check (! song.isTrackAudible (a), "a muted folder silences what is inside it");

            a.setSoloed (true, nullptr);

            check (song.isTrackAudible (a),
                    "...but pressing S on a track inside it is heard anyway");
            check (! song.isTrackAudible (b), "...while the rest of the folder stays silent");

            // **逆は勝ちません**：フォルダのSは、中の1本の M を外しません
            folder.setMuted (false, nullptr);
            a.setSoloed (false, nullptr);
            a.setMuted (true, nullptr);
            folder.setSoloed (true, nullptr);

            check (! song.isTrackAudible (a),
                    "a folder's solo does not undo a track's own mute");
            check (song.isTrackAudible (b),
                    "...but the rest of the folder is heard");
        }

        //----------------------------------------------------------------------
        // 8.307：**小節・拍のカウンタ**（Phase 300／本人の指定）

        say ("--- the bar and beat counter");

        {
            // **0始まりで受けて、1始まりで返す**（`formatBarBeat()`の決まり）
            check (TransportBarComponent::formatBarBeat (0, 0) == "001.1",
                    "the start of the song is bar 1, beat 1 - not bar 0");

            check (TransportBarComponent::formatBarBeat (41, 1) == "042.2",
                    "bar 42 beat 2 reads as 042.2");

            // **幅が変わらないこと。** 桁が増えるたびに動くと、隣の秒表示まで動いて見えます
            check (TransportBarComponent::formatBarBeat (8, 0).length()
                     == TransportBarComponent::formatBarBeat (98, 0).length(),
                    "bar 9 and bar 99 take the same width");

            // **4桁はそのまま伸ばす**（切り落とすより、はみ出すほうがまし）
            check (TransportBarComponent::formatBarBeat (999, 0) == "1000.1",
                    "bar 1000 is shown in full, not cut down to three digits");

            // **モデルと繋いで確かめる。** 数字を組み立てる式だけ合っていても、
            // 渡す値を取り違えていれば画面は間違います（4/4なので9拍目＝3小節1拍目）
            ProjectModel song;
            song.setTempo (120.0, nullptr);          // 1拍 = 0.5秒
            song.setTimeSignature ("4/4", nullptr);

            const auto atBarThree = song.getBarBeatAt (song.getTimeForBeatPosition (8.0));

            check (TransportBarComponent::formatBarBeat (atBarThree.bar, atBarThree.beat) == "003.1",
                    "the ninth beat of a 4/4 song is the start of bar 3");

            const auto midBar = song.getBarBeatAt (song.getTimeForBeatPosition (9.5));

            check (TransportBarComponent::formatBarBeat (midBar.bar, midBar.beat) == "003.2",
                    "half way through the next beat still reads as beat 2");
        }

        //----------------------------------------------------------------------
        // 8.307：**フッターの絵**（Phase 300）。
        //
        // 余白と桁数は数で押さえられますが、**「2つ並べて読めるか」は数から出ません**
        // （秒と小節のどちらが主かは、大きさと色の差でしか伝わらない）。
        // `--header-selftest`が絵を落とすのと同じ考え方です（8.295）。

        say ("--- a picture of it");

        {
            TransportBarComponent bar;

            bar.setSize (1280, 96);
            bar.setPlayheadSeconds (59.6);
            bar.setBarBeat (41, 1);                       // 42小節2拍
            bar.setTempoAndTimeSignature (167.0, "4/4");
            bar.setProjectKey (4, false, true);           // Eメジャー

            juce::Image shot (juce::Image::ARGB, bar.getWidth(), bar.getHeight(), true);

            {
                juce::Graphics g (shot);
                bar.paintEntireComponent (g, true);
            }

            // 8.287：**置き場所はexeの隣**（作業フォルダへ落とすとgitに混ざる）
            auto previewFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                    .getParentDirectory().getChildFile ("preview");

            previewFolder.createDirectory();

            auto file = previewFolder.getChildFile ("footer.png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
            {
                png.writeImageToStream (shot, *stream);
                say ("  (a picture of it: " + file.getFullPathName() + ")");
            }
            else
            {
                ++problems;
                say ("  FAIL  could not write " + file.getFullPathName());
            }
        }

        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと。** `JUCEApplication`は`juce_gui_basics`で、
        // ここは画面を持たないので引き込みたくありません
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
