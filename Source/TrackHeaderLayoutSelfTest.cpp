#include "TrackHeaderLayoutSelfTest.h"

#include "ProjectModel.h"
#include "SelectionState.h"
#include "WaveformCache.h"
#include "TimelineComponent.h"
#include "ChannelStripComponent.h"
#include "MasterStripComponent.h"
#include "InspectorPanel.h"
#include "AudioEngine.h"
#include "AppColours.h"

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

#include <iostream>

namespace TrackHeaderLayoutSelfTest
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
                // **通ったものは黙っていません。** 何を見たのかが残らないと、
                // 「0問題」が「何も見ていない」と区別できません（8.294）
                say ("  ok    " + what);
            }
            else
            {
                ++problems;
                say ("  FAIL  " + what);
            }
        }

        juce::String describe (juce::Rectangle<int> r)
        {
            return "(" + juce::String (r.getX()) + "," + juce::String (r.getY())
                    + " " + juce::String (r.getWidth()) + "x" + juce::String (r.getHeight()) + ")";
        }

        //======================================================================
        /** 押せる場所は`expanded()`で少し広げてあるので、**そのぶんも含めて**見る。

            広げ方は`mouseDown()`のとおり：三角は3px、「i」と種類の絵は2px。
            ここを0にすると、**画面では重なっていないのに押すと隣が動く**という、
            いちばん見つけにくいずれを見逃します。 */
        struct Item
        {
            const char* name;
            juce::Rectangle<int> bounds;
            int grab;   // 当たり判定の広げしろ
        };

        std::vector<Item> collect (const TimelineComponent::HeaderRowLayout& layout)
        {
            std::vector<Item> items;

            const Item all[]
            {
                { "triangle",   layout.triangle,   3 },
                { "i",          layout.inspector,  2 },
                { "name",       layout.name,       0 },
                { "IN",         layout.monitor,    0 },
                { "type icon",  layout.typeIcon,   2 },
                { "arm",        layout.arm,        0 },
                { "automation", layout.automation, 0 },
                { "solo",       layout.solo,       0 },
                { "mute",       layout.mute,       0 },
                { "meter",      layout.meter,      0 },
            };

            for (const auto& item : all)
                if (! item.bounds.isEmpty())
                    items.push_back (item);

            return items;
        }
    }

    //==========================================================================
    bool runIfRequested (const juce::String& commandLine)
    {
        if (! commandLine.contains ("--header-selftest"))
            return false;

        problems = 0;

        say ("=== track header layout (8.295) ===");

        //----------------------------------------------------------------------
        // 種別を全部並べたプロジェクトを1つ作る。
        // **7つ全部**書くこと——足した種別をここへ書き忘れると、
        // 「絵が無いから何も出ない」行が検査されないまま残ります

        ProjectModel project;
        SelectionState selection;
        WaveformCache cache;

        project.createNewProject();

        struct Wanted { const char* name; TrackType type; };

        const Wanted wanted[]
        {
            { "Audio",   TrackType::Audio },
            { "MIDI",    TrackType::Midi },
            { "Send",    TrackType::Send },
            { "Folder",  TrackType::Folder },
            { "Chord",   TrackType::Chord },
            { "VCA",     TrackType::VCA },
            { "DrumOut", TrackType::DrumOut },
        };

        for (const auto& w : wanted)
            project.addTrack (juce::String (w.name) + " track with a fairly long name", w.type);

        // 8.146：MIDIも録音待機できる（Phase 184）。**ここではオーディオを待機させる**
        // ——INが出るのはオーディオだけなので（`drawTrackHeaderContents()`）
        for (int t = 0; t < project.getNumTracks(); ++t)
            if (project.getTrack (t).getType() == TrackType::Audio)
                project.getTrack (t).setArmed (true, nullptr);

        // 8.298：**パンを振っておく**（Phase 291）。
        //
        // 弧を付けたので、**真ん中のままでは弧が0の長さ**で写りません。
        // 左右どちらへも振って撮ります——**真ん中が0のつまみは12時から伸びる**ので、
        // 左へ振ったときと右へ振ったときで**反対向きに伸びること**まで見えます。
        {
            float pan = -0.7f;

            for (int t = 0; t < project.getNumTracks(); ++t)
            {
                project.getTrack (t).setPan (pan, nullptr);
                pan += 0.35f;

                if (pan > 1.0f)
                    pan = -0.7f;
            }
        }

        // 8.296・8.297：**インサートを2つ挿しておく**（Phase 289・290）。
        //
        // **空のラックでは絵が用を成しません**——インスペクタでは
        // 右の列がほとんど出ないので「2列に分かれている」ことが分からず、
        // Consoleでは**一括バイパス（B）が出ません**（インサートが1つも無いと隠れる）。
        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (track.getType() != TrackType::Midi)
                continue;

            for (const char* name : { "Manta EQ", "Manta Comp" })
            {
                juce::PluginDescription description;
                description.name = name;
                description.pluginFormatName = "Manta";
                description.fileOrIdentifier = name;

                track.addInsert (description, nullptr);
            }

            break;
        }

        TimelineComponent timeline (project, cache, selection);

        // **見える大きさにしておくこと。** 高さが0だと、行がどれも
        // 画面の外にあることになり、位置を聞いても意味のある値が返りません
        timeline.setSize (1400, 900);

        //----------------------------------------------------------------------
        // 幅と字下げを変えながら、全部の行を見る

        for (const int headerWidth : { 190, 230, 520 })
        {
            timeline.setTrackHeaderWidth (headerWidth);

            check (timeline.getTrackHeaderWidth() == headerWidth,
                    "header width " + juce::String (headerWidth) + " is accepted");

            say ("--- header width " + juce::String (headerWidth) + " ---");

            for (int t = 0; t < project.getNumTracks(); ++t)
            {
                auto track = project.getTrack (t);
                const auto label = juce::String (trackTypeToString (track.getType()))
                                    + " @" + juce::String (headerWidth);

                const auto layout = timeline.getHeaderRowLayout (t);
                const auto items = collect (layout);

                //--------------------------------------------------------------
                // 1. 重ならないこと（**当たり判定のぶんも含めて**）

                bool overlapping = false;

                for (size_t a = 0; a < items.size(); ++a)
                    for (size_t b = a + 1; b < items.size(); ++b)
                    {
                        const auto first = items[a].bounds.expanded (items[a].grab);
                        const auto second = items[b].bounds.expanded (items[b].grab);

                        if (first.intersects (second))
                        {
                            overlapping = true;

                            say ("        " + juce::String (items[a].name) + " " + describe (first)
                                   + " overlaps " + juce::String (items[b].name) + " " + describe (second));
                        }
                    }

                check (! overlapping, label + ": nothing overlaps anything else");

                //--------------------------------------------------------------
                // 2. ヘッダーの内側に収まること（**見切れない**）

                bool outside = false;

                for (const auto& item : items)
                    if (item.bounds.getX() < 0 || item.bounds.getRight() > headerWidth)
                    {
                        outside = true;
                        say ("        " + juce::String (item.name) + " " + describe (item.bounds)
                               + " sticks out of the header");
                    }

                check (! outside, label + ": everything fits inside the header");

                //--------------------------------------------------------------
                // 3. 名前の場所が残っていること。
                //
                // **40pxを下限にしてあります**——13pxのフォントで3文字ほど。
                // これを割ると、名前が「A…」のようになって見分けがつきません

                check (layout.name.getWidth() >= 40,
                        label + ": the name still has room (" + juce::String (layout.name.getWidth())
                          + "px)");

                //--------------------------------------------------------------
                // 4. 並び順（本人の指定）

                // 8.295：**コードトラックには「i」を出しません**（Phase 288／本人の判断）
                if (track.getType() == TrackType::Chord)
                    check (layout.inspector.isEmpty(),
                            label + ": a chord track has no i button");
                else
                    check (! layout.inspector.isEmpty(),
                            label + ": every other kind has an i button");

                // 8.295：2段目は**オートメーション → ミュート → ソロ**（本人の指定。
                // Consoleと同じ並び）。**隙間は2pxで揃える**（1かたまりに見せる）
                if (! layout.automation.isEmpty() && ! layout.mute.isEmpty())
                    check (layout.mute.getX() - layout.automation.getRight() == 2,
                            label + ": mute is tucked 2px after automation");

                if (! layout.mute.isEmpty() && ! layout.solo.isEmpty())
                    check (layout.solo.getX() - layout.mute.getRight() == 2,
                            label + ": solo is tucked 2px after mute (Console's order)");

                if (! layout.inspector.isEmpty() && ! layout.name.isEmpty())
                    check (layout.inspector.getRight() <= layout.name.getX(),
                            label + ": the i button is left of the name");

                if (! layout.name.isEmpty() && ! layout.typeIcon.isEmpty())
                    check (layout.name.getRight() <= layout.typeIcon.getX(),
                            label + ": the type icon is right of the name");

                if (! layout.monitor.isEmpty() && ! layout.typeIcon.isEmpty())
                    check (layout.monitor.getRight() <= layout.typeIcon.getX(),
                            label + ": IN sits just left of the type icon");

                // 8.295：**標準の高さの行では、「i」は三角の真上**（本人の指定）。
                // 低い行（コード・畳んだ行）では縦に重なるので、横に並べます
                if (! layout.triangle.isEmpty() && ! layout.inspector.isEmpty())
                {
                    const bool stacked = layout.inspector.getX() == layout.triangle.getX();

                    check (stacked ? layout.inspector.getBottom() <= layout.triangle.getY()
                                    : layout.triangle.getRight() <= layout.inspector.getX(),
                            label + (stacked ? ": the i button sits directly above the triangle"
                                             : ": the i button sits beside the triangle"));
                }

                //--------------------------------------------------------------
                // 5. 大きさ（本人の指定）。
                //
                // **「1.5倍以上」ではなく、いくつであるべきかを書きます**（8.294）。
                // 一度は●・IN・「i」も1.5倍にして、本人の指定で戻しました——
                // 「22px以上」で見ていると、**戻したことに気づけません**
                // （戻した値は下限を割るので落ちますが、「どれが幾つのはず」が残らない）。
                //
                // 1.5倍にするのは**オートメーション・ソロ・ミュートの3つだけ**です

                struct Expected { const char* name; juce::Rectangle<int> bounds; int w; int h; };

                for (const auto& item : { Expected { "arm",        layout.arm,        15, 15 },
                                           Expected { "automation", layout.automation, 23, 23 },
                                           Expected { "solo",       layout.solo,       26, 23 },
                                           Expected { "mute",       layout.mute,       26, 23 },
                                           Expected { "i",          layout.inspector,  15, 14 },
                                           Expected { "IN",         layout.monitor,    22, 14 },
                                           Expected { "type icon",  layout.typeIcon,   15, 15 } })
                {
                    if (item.bounds.isEmpty())
                        continue;

                    check (item.bounds.getWidth() == item.w && item.bounds.getHeight() == item.h,
                            label + ": " + item.name + " is " + juce::String (item.w) + "x"
                              + juce::String (item.h) + " " + describe (item.bounds));
                }
            }
        }

        //----------------------------------------------------------------------
        // 字下げ（フォルダの中）でも同じことが成り立つか。
        //
        // **いちばん狭い幅でやること**——字下げは右へ寄せるので、
        // 広い幅では余裕があって何も起きません

        say ("--- inside folders (header width 190) ---");

        timeline.setTrackHeaderWidth (190);

        auto folder = project.addTrack ("Outer folder", TrackType::Folder);
        auto inner = project.addTrack ("Inner folder", TrackType::Folder);
        auto deepest = project.addTrack ("Deep MIDI track", TrackType::Midi);

        project.moveTrackIntoFolder (inner, folder.getId());
        project.moveTrackIntoFolder (deepest, inner.getId());

        check (project.getTrackFolderDepth (deepest) == 2,
                "the deepest track really is two folders in");

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);
            const int depth = project.getTrackFolderDepth (track);

            if (depth == 0)
                continue;

            const auto label = juce::String (trackTypeToString (track.getType()))
                                + " at depth " + juce::String (depth);

            const auto layout = timeline.getHeaderRowLayout (t);
            const auto items = collect (layout);

            bool overlapping = false;

            for (size_t a = 0; a < items.size(); ++a)
                for (size_t b = a + 1; b < items.size(); ++b)
                    if (items[a].bounds.expanded (items[a].grab)
                            .intersects (items[b].bounds.expanded (items[b].grab)))
                    {
                        overlapping = true;
                        say ("        " + juce::String (items[a].name) + " overlaps "
                               + juce::String (items[b].name));
                    }

            check (! overlapping, label + ": nothing overlaps");
            check (layout.name.getWidth() >= 40,
                    label + ": the name still has room (" + juce::String (layout.name.getWidth())
                      + "px)");
        }

        //----------------------------------------------------------------------
        // 畳んだ行でも、出ているものが重ならないこと。
        //
        // **8.44で1度踏んだ形**です：畳んだ行は低く、下段の判定と重なり得ます

        say ("--- collapsed rows ---");

        folder.setCollapsed (true);

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            if (! track.isCollapsed())
                continue;

            const auto layout = timeline.getHeaderRowLayout (t);
            const auto items = collect (layout);

            bool overlapping = false;

            for (size_t a = 0; a < items.size(); ++a)
                for (size_t b = a + 1; b < items.size(); ++b)
                    if (items[a].bounds.expanded (items[a].grab)
                            .intersects (items[b].bounds.expanded (items[b].grab)))
                    {
                        overlapping = true;
                        say ("        " + juce::String (items[a].name) + " overlaps "
                               + juce::String (items[b].name));
                    }

            check (! overlapping, "a collapsed row has nothing overlapping");
        }

        //----------------------------------------------------------------------
        // 8.295：**絵も1枚落とします**（Phase 288）。
        //
        // 数で押さえられるのは「重なっていない・はみ出していない」までです。
        // **絵が何の絵か分かるか**、**1.5倍にして窮屈でないか**は、
        // 数えても出ません——`--icons`（8.266）が絵を落とすのと同じ考え方で、
        // **見て決めるものは見られるように**しておきます。
        //
        // 窓は出しません。`paintEntireComponent()`は画面に出ていない部品でも描けます

        {
            folder.setCollapsed (false);   // 畳んだままだと中身が写りません

            timeline.setTrackHeaderWidth (230);   // 既定の幅（`defaultTrackHeaderWidth`）

            // **ヘッダーだけ**を大きめに撮る。アレンジ側（クリップの置き場所）は
            // 今回の話に関係が無く、入れると絵が横に間延びします
            juce::Image shot (juce::Image::ARGB, timeline.getWidth(), timeline.getHeight(), true);

            {
                juce::Graphics g (shot);
                timeline.paintEntireComponent (g, true);
            }

            const auto headerStrip = shot.getClippedImage ({ 0, 0, 320, timeline.getHeight() });

            // 8.287：**置き場所は決めておくこと。** 作業フォルダへ落とすと、
            // どこから走らせたかで置き場所が変わり、**プロジェクトの中に
            // 混ざります**（0.6.0のときに`PluginPreview`で1度やっています）。
            // exeの隣（＝ビルドフォルダの中）なら、gitに入りません
            auto previewFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                            .getParentDirectory().getChildFile ("preview");

            previewFolder.createDirectory();

            auto file = previewFolder.getChildFile ("track_header.png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
            {
                png.writeImageToStream (headerStrip, *stream);
                say ("  (a picture of it: " + file.getFullPathName() + ")");
            }
            else
            {
                ++problems;
                say ("  FAIL  could not write " + file.getFullPathName());
            }
        }

        //----------------------------------------------------------------------
        // 8.301：**Consoleで選ぶと、アレンジ画面も同じトラックになる**
        // （Phase 294／本人の要望）。
        //
        // Consoleのストリップは`SelectionState`へ書くだけで、
        // アレンジ画面もインスペクタも**そこを見ています**。
        // ここで見るのは**その受け取り側**——
        // 「選択が変わったら、アレンジ画面の印も動くか」。

        say ("--- selection follows (8.301) ---");

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            auto track = project.getTrack (t);

            selection.selectTrack (track.getId());
            timeline.refreshSelectionFromState();

            check (timeline.getSelectedTrackIndex() == t,
                    juce::String (trackTypeToString (track.getType()))
                      + " (row " + juce::String (t) + ") becomes the selected row in the arrange view");
        }

        //----------------------------------------------------------------------
        // 8.295：**Consoleのストリップも1枚**（Phase 288／改善案2）。
        //
        // 本人の指定は「ボリュームメーター下、ボリューム量数値の右にアイコン」。
        // **場所が合っているかは、見ないと分かりません**——数えられるのは
        // 「重なっていない」までで、「思っていた場所か」は数から出ません。
        //
        // `AudioEngine`は**作るだけなら音のデバイスを開きません**
        // （`AudioEngine::AudioEngine()`は空で、`initialise()`が開きます）。

        {
            AudioEngine engineForLayout (project);

            juce::OwnedArray<ChannelStripComponent> strips;

            // **種類の違うものを並べること。** MIDI（押せる）・オーディオ・VCAで
            // 絵の出方が違うので、1本だけ撮っても比べられません
            for (int t = 0; t < project.getNumTracks(); ++t)
            {
                auto track = project.getTrack (t);
                const auto type = track.getType();

                // 8.295：**絵のある種類は全部**（Phase 288）。
                // コードトラックにはConsoleのストリップがありません
                if (type == TrackType::Chord)
                    continue;

                auto* strip = strips.add (new ChannelStripComponent (track, project, engineForLayout));
                strip->setSize (ChannelStripComponent::stripWidth, 420);
            }

            check (strips.size() >= 3, "there are strips of more than one kind to look at");

            //------------------------------------------------------------------
            // 8.298：**いちばん低い高さで、フェーダーが下限を割らないこと**
            // （Phase 291／本人の指定）。
            //
            // `minimumConsoleHeight`は足し算で出しています——**その足し算が
            // 実際の割り付けと合っているか**は、置いてみないと分かりません。
            // ここが破れると、**パネルの下限まで縮めたときにメーターが潰れます**。

            if (auto* strip = strips.getFirst())
            {
                strip->setSize (ChannelStripComponent::stripWidth,
                                 ChannelStripComponent::minimumConsoleHeight);

                check (strip->getFaderAreaHeight() >= ConsoleLayout::minimumFaderAreaHeight,
                        "at the minimum height the fader still gets "
                          + juce::String (ConsoleLayout::minimumFaderAreaHeight) + "px ("
                          + juce::String (strip->getFaderAreaHeight()) + "px)");

                // **1px低いと割れること**も見ます。割れないなら、
                // `minimumConsoleHeight`が**必要より大きい**ということです
                // ——下限は「ここまでは要る」であって、「余裕を見た値」ではありません
                strip->setSize (ChannelStripComponent::stripWidth,
                                 ChannelStripComponent::minimumConsoleHeight - 1);

                check (strip->getFaderAreaHeight() < ConsoleLayout::minimumFaderAreaHeight,
                        "...and one pixel lower it does not ("
                          + juce::String (strip->getFaderAreaHeight()) + "px)");

                strip->setSize (ChannelStripComponent::stripWidth, 420);   // 絵のために戻す
            }

            //------------------------------------------------------------------
            // 8.299：**フェーダーの高さが、どのストリップでも同じこと**
            // （Phase 292／本人の指定）。
            //
            // 本人の言葉：「マスターのフェーダーとメーター、VCAのフェーダーの
            // **最大縦幅サイズを他のトラックと揃えて、同じスピードで伸縮できるように**しよう」。
            //
            // **高さを2つで見ます**——1つだけだと「たまたま合っていた」を
            // 区別できません。**マスターとVCAだけ違う**という形は、
            // 1点では見つかりません。
            //
            // **高さは、いまの設定から決めます**（`AppSettings`は読むだけ。8.286）。
            const int faderWanted = ConsoleLayout::getFaderAreaHeight();
            const int tightHeight = ConsoleLayout::stripFixedHeight + faderWanted
                                      + ConsoleLayout::minimumRackAreaHeight;

            say ("  (the fader is set to " + juce::String (faderWanted) + "px, so the rack appears "
                   + "at " + juce::String (tightHeight) + "px)");

            //------------------------------------------------------------------
            // 8.302：**窓を伸ばしても、フェーダーは動かない**
            // （Phase 295／本人の指定）。
            //
            // 本人の言葉：「Consoleウィンドウの縦幅を伸縮した際は、
            // **フェーダーとメーターの縦幅は動かないように**」。
            //
            // **伸びたぶんはラックへ行きます**——ここでその両方を見ます。
            // 「動かない」だけを見ると、**どこにも行っていない**（＝どこかが欠けている）
            // のと区別できません。

            for (const int stripHeight : { tightHeight, tightHeight + 200, tightHeight + 500 })
            {
                check (ConsoleLayout::getFaderAreaHeightFor (stripHeight + 100)
                         == ConsoleLayout::getFaderAreaHeightFor (stripHeight),
                        "at " + juce::String (stripHeight) + "px, 100px more leaves the fader alone");

                check (ConsoleLayout::getRackAreaHeightFor (stripHeight + 100)
                         - ConsoleLayout::getRackAreaHeightFor (stripHeight) == 100,
                        "...and all 100px of it goes to the rack");
            }

            for (const int stripHeight : { tightHeight, tightHeight + 200 })
            {
                MasterStripComponent masterForCheck (project, engineForLayout);

                masterForCheck.setSize (MasterStripComponent::stripWidth, stripHeight);

                const int wanted = ConsoleLayout::getFaderAreaHeightFor (stripHeight);

                check (masterForCheck.getFaderAreaHeight() == wanted,
                        "master's fader is " + juce::String (wanted) + "px at "
                          + juce::String (stripHeight) + " ("
                          + juce::String (masterForCheck.getFaderAreaHeight()) + "px)");

                for (auto* strip : strips)
                {
                    strip->setSize (ChannelStripComponent::stripWidth, stripHeight);

                    check (strip->getFaderAreaHeight() == wanted,
                            juce::String (trackTypeToString (project.findTrackById (strip->getTrackId())
                                                                .getType()))
                              + "'s fader is " + juce::String (wanted) + "px at "
                              + juce::String (stripHeight) + " ("
                              + juce::String (strip->getFaderAreaHeight()) + "px)");
                }
            }

            // 8.283・8.295：**マスターも並べて撮ること。**
            //
            // dB表示の行を16から18へ上げたので（絵のぶん）、**マスターを16のまま
            // 残すと、隣り合ったメーターの行が2pxずれます**——数では出ませんが、
            // 並べて撮れば見えます。値は`ConsoleLayout::volumeReadoutRowHeight`1か所
            MasterStripComponent master (project, engineForLayout);

            // 8.298：**高さを2つ撮ります**（Phase 291）。
            //
            // 上が普段の高さ、下が**いちばん低い高さ**（本人の指定で下限になったところ）。
            // 下では**ラックが消えてフェーダーが伸びます**——
            // 「ラックが見えなくなるのは構わない」という本人の判断が、
            // **実際にどう見えるか**は並べないと分かりません
            // 8.302：**上の高さは、ラックが出るところから決めます**（Phase 295）。
            //
            // フェーダーの高さが先に決まるようになったので、**固定の420pxでは
            // ラックが1つも出ません**（240＋40が入らない）。
            // 絵の用は「普段の見え方」なので、**普段は出ているもの**が写る高さにします。
            const int tallHeight = ConsoleLayout::stripFixedHeight
                                     + ConsoleLayout::getFaderAreaHeight()
                                     + ConsoleLayout::defaultRackAreaHeight;

            const int shortHeight = ChannelStripComponent::minimumConsoleHeight;

            const int sheetWidth = ChannelStripComponent::stripWidth * strips.size()
                                    + MasterStripComponent::stripWidth;

            juce::Image sheet (juce::Image::ARGB, sheetWidth, tallHeight + 8 + shortHeight, true);

            {
                juce::Graphics g (sheet);

                g.setColour (AppColours::background);
                g.fillAll();

                int top = 0;

                for (const int stripHeight : { tallHeight, shortHeight })
                {
                    master.setSize (MasterStripComponent::stripWidth, stripHeight);

                    for (int i = 0; i < strips.size(); ++i)
                    {
                        strips[i]->setSize (ChannelStripComponent::stripWidth, stripHeight);

                        // 8.301：**1本は選んだ状態で撮ります**（Phase 294）。
                        // 選ばれているかどうかは**地の色でしか分からない**ので、
                        // 全部が同じ見た目の絵では確かめようがありません
                        strips[i]->setSelected (i == 1);

                        juce::Graphics::ScopedSaveState state (g);

                        g.setOrigin (ChannelStripComponent::stripWidth * i, top);
                        strips[i]->paintEntireComponent (g, true);
                    }

                    {
                        juce::Graphics::ScopedSaveState state (g);

                        g.setOrigin (ChannelStripComponent::stripWidth * strips.size(), top);
                        master.paintEntireComponent (g, true);
                    }

                    top += stripHeight + 8;
                }
            }

            auto previewFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                            .getParentDirectory().getChildFile ("preview");

            previewFolder.createDirectory();

            auto file = previewFolder.getChildFile ("console_strips.png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
            {
                png.writeImageToStream (sheet, *stream);
                say ("  (the console strips: " + file.getFullPathName() + ")");
            }
            else
            {
                ++problems;
                say ("  FAIL  could not write " + file.getFullPathName());
            }
        }

        //----------------------------------------------------------------------
        // 8.296：**インスペクタも1枚**（Phase 289／本人の指定の絵）。
        //
        // Consoleのストリップを中へ入れて2列にしたので、**何がどこに来るか**は
        // 見ないと分かりません（数で押さえられるのは重なりまで）。
        // **MIDIトラックを選んだところ**を撮ります——音源スロットが出るのはMIDIだけです。

        {
            AudioEngine engineForInspector (project);

            // **選ぶのは作るより先。**
            //
            // `SelectionState`の通知は`ChangeBroadcaster`——**非同期**です
            // （メッセージの行列に積まれる）。窓を出さない道具ではその行列を
            // 回していないので、**作ったあとに選んでも届きません**
            // （一度それで「何も選んでいません」の絵が撮れました）。
            // コンストラクタは`rebuildForSelection()`を直に呼ぶので、先に選んでおけば足ります
            for (int t = 0; t < project.getNumTracks(); ++t)
                if (project.getTrack (t).getType() == TrackType::Midi)
                {
                    selection.selectTrack (project.getTrack (t).getId());
                    break;
                }

            InspectorPanel inspector (project, selection, engineForInspector);

            inspector.setSize (InspectorPanel::fixedWidth, 900);

            check (inspector.getWidth() == InspectorPanel::fixedWidth,
                    "the inspector is the fixed width (" + juce::String (InspectorPanel::fixedWidth)
                      + "px)");

            juce::Image sheet (juce::Image::ARGB, inspector.getWidth(), inspector.getHeight(), true);

            {
                juce::Graphics g (sheet);
                inspector.paintEntireComponent (g, true);
            }

            auto previewFolder = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                                    .getParentDirectory().getChildFile ("preview");

            previewFolder.createDirectory();

            auto file = previewFolder.getChildFile ("inspector.png");

            file.deleteFile();

            juce::PNGImageFormat png;

            if (auto stream = file.createOutputStream())
            {
                png.writeImageToStream (sheet, *stream);
                say ("  (the inspector: " + file.getFullPathName() + ")");
            }
            else
            {
                ++problems;
                say ("  FAIL  could not write " + file.getFullPathName());
            }
        }

        //----------------------------------------------------------------------
        say ("--- " + juce::String (problems) + " problem(s) ---");

        // **`JUCEApplicationBase`のほうを使うこと**（`JUCEApplication`は`juce_gui_basics`）
        juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (problems == 0 ? 0 : 1);

        return true;
    }
}
