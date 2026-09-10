#include "PluginManager.h"
#include "AppSettings.h"   // 8.157：覚えておく場所（Phase 195）
#include "Plugins/MantaPluginFormat.h"   // 9.5：内蔵プラグイン（Phase 204）

namespace
{
    /** 8.174：**スキャナが数える名前**（＝VST3バンドルの側）を返す（Phase 215）。

        ```
        ...\Common Files\VST3\Foo.vst3\Contents\x86_64-win\Foo.vst3   ← 一覧に入る名前
        ...\Common Files\VST3\Foo.vst3                                ← スキャナが数える名前
        ```

        VST3の**バンドル**（中に`Contents\x86_64-win\`を持つフォルダ）では、
        この2つが食い違います。`recursiveFileSearch()`はバンドルを見つけた時点で
        中へ降りないので、数えるのは**外側**だけです。

        バンドルでない`.vst3`（ただのDLL）なら2つは同じなので、空を返します。 */
    juce::String getBundlePathFor (const juce::String& fileOrIdentifier)
    {
        if (! juce::File::isAbsolutePath (fileOrIdentifier))
            return {};

        for (auto file = juce::File (fileOrIdentifier).getParentDirectory();
             file.getParentDirectory() != file;
             file = file.getParentDirectory())
        {
            if (file.getFileExtension().equalsIgnoreCase (".vst3"))
                return file.getFullPathName();
        }

        return {};
    }
}

PluginManager::PluginManager()
{
    // Windows環境では実質VST3のみが登録される
    // （VST2は現在のJUCEには同梱されていないため対象外。仕様書9章の方針どおり）。
    //
    // 注意：AudioPluginFormatManager::addDefaultFormats()は現行のJUCEでは廃止されており、
    // 代わりに非メンバー関数juce::addDefaultFormatsToManager()を使う必要がある
    // （ARA対応にともなうheadlessモジュール分離の一環で変更された）。
    juce::addDefaultFormatsToManager (formatManager);

    // 9.5：**内蔵プラグインのフォーマットを1つ足す**（Phase 204）。
    //
    // `AudioEngine::createPluginNode()`は`formatManager`しか見ていないので、
    // ここへ載せるだけで、挿す・保存する・開き直す・オートメーションが
    // **全部そのまま動きます**（`Plugins/MantaPluginFormat.h`）。
    formatManager.addFormat (std::make_unique<MantaPluginFormat>());

    // 8.157：**前回までに分かっていることを持って始める**（Phase 195／E2）
    loadKnownPlugins();

    addBuiltInPlugins();
}

void PluginManager::addBuiltInPlugins()
{
    // 9.5：**毎回足す。** 探しに行く先が無いので、スキャンでは増えません。
    //
    // **覚えてある一覧に残っていたものは、先に全部払う。**
    // 表から消したもの・名前を変えたものが**古いXMLに残り続ける**ためです
    // （`saveKnownPlugins()`が書き出さなくなったのはPhase 205から。
    //   それ以前に書かれたファイルには入っています）。
    bool removedStaleEntries = false;

    for (const auto& description : knownPlugins.getTypes())
    {
        if (MantaPlugins::isMantaPlugin (description))
        {
            knownPlugins.removeType (description);
            removedStaleEntries = true;
        }
    }

    for (const auto& description : MantaPlugins::getDescriptions())
        knownPlugins.addType (description);

    // **古い書き込みが残っていたら、その場で消しておく。**
    // 放っておくと、内蔵プラグインを持たない版のexeで起動したときに
    // 「一覧には出るのに挿せない」が残り続けます（`saveKnownPlugins()`の説明）
    if (removedStaleEntries)
        saveKnownPlugins();
}

juce::File PluginManager::getPluginListFile()
{
    return AppSettings::getDataFolder().getChildFile ("plugin_list.xml");
}

juce::File PluginManager::getDeadMansPedalFile()
{
    return AppSettings::getDataFolder().getChildFile ("plugin_scan_in_progress.txt");
}

void PluginManager::loadKnownPlugins()
{
    if (auto xml = juce::XmlDocument::parse (getPluginListFile()))
        knownPlugins.recreateFromXml (*xml);
}

void PluginManager::saveKnownPlugins() const
{
    auto xml = knownPlugins.createXml();

    if (xml == nullptr)
        return;

    // 9.5：**内蔵プラグインは書き出さないこと**（Phase 205／本人の報告）。
    //
    // Phase 204では「保存されたXMLに残っていても構わない」と考えていました
    // （読み込みのたびに払って足し直すので、中身はずれない）。**間違いでした。**
    //
    // このファイルは**exeと別に残ります**。内蔵プラグインを持たない版のexe
    // ——古いビルド、内蔵プラグインを外した版、別の環境へ持って行った控え——で
    // 起動すると、**一覧には出るのに挿すと「プラグインを読み込めませんでした」**に
    // なります。作れるフォーマットがそのexeに無いためです。
    //
    // **実際にそうなりました**（Phase 204のあと、Phase 203のRelease版を起動した）。
    // 一覧の元は起動のたびに`addBuiltInPlugins()`が入れるので、
    // **書き出す理由がそもそもありません。**
    for (int i = xml->getNumChildElements(); --i >= 0;)
    {
        auto* child = xml->getChildElement (i);

        if (child != nullptr && child->getStringAttribute ("format") == MantaPlugins::getFormatName())
            xml->removeChildElement (child, true);
    }

    xml->writeTo (getPluginListFile());
}

juce::StringArray PluginManager::blacklistWhatCrashedLastTime()
{
    const auto before = knownPlugins.getBlacklistedFiles();

    // **`deadMansPedal`に残っている＝そこで落ちた**（`PluginManager.h`）。
    // 中身を読んでブラックリストへ移すのはJUCEの仕事
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (knownPlugins,
                                                                        getDeadMansPedalFile());

    const auto after = knownPlugins.getBlacklistedFiles();

    juce::StringArray newlyBlacklisted;

    for (const auto& entry : after)
        if (! before.contains (entry))
            newlyBlacklisted.add (entry);

    // **弾いたことを覚えておく。** 保存しないと、次の起動でまた同じものを開きに行きます
    if (! newlyBlacklisted.isEmpty())
        saveKnownPlugins();

    // **読んだ記録は消す。** 残しておくと、そのプラグインを二度と開かないまま
    // 「落ちた記録」だけが積もり続けます。ここから先で本当に落ちたなら、
    // `PluginDirectoryScanner`がまた書き込みます
    getDeadMansPedalFile().deleteFile();

    return newlyBlacklisted;
}

void PluginManager::clearBlacklist()
{
    for (const auto& entry : knownPlugins.getBlacklistedFiles())
        knownPlugins.removeFromBlacklist (entry);

    saveKnownPlugins();
}

juce::FileSearchPath PluginManager::buildSearchPath (juce::AudioPluginFormat& format,
                                                     const juce::FileSearchPath& extraFolders) const
{
    // 8.189：**探す先はフォーマットごとに違います**（Phase 227／LV2対応）。
    //
    // Phase 226まで、この関数は**全フォーマットにVST3用のパスを渡していました**。
    // 1つしか無いあいだは正しかったのですが、LV2は置き場所が別です
    // （`~/.lv2`・`/usr/lib/lv2` など）。VST3のパスを渡しても**何も見つかりません**。
    //
    // **VST3以外はJUCEに訊きます。** `getDefaultLocationsToSearch()`は
    // LV2について、OSごとの標準の場所に加えて**環境変数`LV2_PATH`**まで見ます——
    // 自分で書くと、そこを取りこぼします。
    //
    // **VST3だけ自前のまま**にしてあるのは、8.178でLinuxの枝を足した経緯があり、
    // ここを入れ替えると**すでに動いている128個の走査の当たり方が変わる**ためです
    // （変える理由が無いところは変えない）。
    auto searchPath = format.getName().containsIgnoreCase ("VST3")
                        ? getDefaultVST3SearchPath()
                        : format.getDefaultLocationsToSearch();

    // 本人が足したフォルダは**どのフォーマットにも渡します**。
    // 「ここも見て」と言われた場所なので、形式で絞る理由がありません
    // （見当違いのフォルダなら、そのフォーマットが何も見つけないだけです）
    for (int i = 0; i < extraFolders.getNumPaths(); ++i)
        searchPath.add (extraFolders[i]);

    return searchPath;
}

juce::FileSearchPath PluginManager::getDefaultVST3SearchPath() const
{
    juce::FileSearchPath path;

   #if JUCE_WINDOWS
    path.add (juce::File ("C:\\Program Files\\Common Files\\VST3"));
    path.add (juce::File ("C:\\Program Files (x86)\\Common Files\\VST3"));
   #elif JUCE_MAC
    path.add (juce::File ("/Library/Audio/Plug-Ins/VST3"));
    path.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile ("Library/Audio/Plug-Ins/VST3"));
   #elif JUCE_LINUX
    // 8.178：**Linuxの標準の置き場所**（Phase 219／配布のため）。
    //
    // VST3の仕様が決めている順です。**個人のものを先に**——
    // 同じプラグインが両方にあるとき、自分で入れたほうが勝ちます。
    //
    // ここが空のままだと、Linux版は**一生プラグインを見つけません**
    // （スキャンは走るが、探す先が無い）。
    path.add (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile (".vst3"));
    path.add (juce::File ("/usr/local/lib/vst3"));
    path.add (juce::File ("/usr/lib/vst3"));
   #endif

    return path;
}

juce::Array<juce::PluginDescription> PluginManager::scanFoldersWithoutApplying (
    const juce::FileSearchPath& extraFolders,
    const juce::Array<juce::PluginDescription>& alreadyKnown) const
{
    // 8.189：**探す先はフォーマットごとに作ります**（Phase 227）。
    // ここで1本作って全部に配っていたのを、下のループの中へ移しました

    // 8.52：**自前の一覧へ集める**（Phase 91）。
    // `knownPlugins`を直接書き換えると、**別スレッドから走らせたときに
    // 画面側の読み出しと衝突**します（ブラウザパネルも同じ一覧を見ている）。
    juce::KnownPluginList scanned;

    // 8.157：**分かっているものを先に入れておく**（Phase 195／E2）。
    //
    // これが`scanNextFile()`の第1引数（`dontRescanIfAlreadyInList`）を効かせます。
    // **Phase 194まではここが空だったので、124個あるVST3を毎回全部開いていました**
    // ——それが「スキャンに1分」の正体です。
    //
    // 8.174：**それでも半分は毎回開き直していました**（Phase 215／本人の報告）。
    //
    // `isListingUpToDate()`は「**スキャナが数えた名前**でこの一覧を引けるか」を見ます。
    // ところがVST3の**バンドル**では、数える名前と一覧に入る名前が食い違います
    // （`getBundlePathFor()`の図）。引けないので**毎回「知らないもの」と判定**され、
    // 実測では129個中**31個**が起動のたびに開き直されていました
    // ——1コアを7秒、RAMは385MBまで膨らみ、UADx 9個とsforzandoのDLLが常駐したまま残る。
    //
    // **本物の説明は書き換えないこと。** `fileOrIdentifier`は識別子の一部で
    // （8.166）、直すと**保存済みのプロジェクトがそのプラグインを見失います**。
    // そこで、**バンドルの側を指すだけの目印**を別に足します。
    // 目印は返すときに外すので、`knownPlugins`には入りません。
    juce::StringArray markerIdentifiers;

    for (const auto& description : alreadyKnown)
    {
        scanned.addType (description);

        const auto bundle = getBundlePathFor (description.fileOrIdentifier);

        if (bundle.isEmpty() || bundle == description.fileOrIdentifier)
            continue;   // もともとバンドルの側で入っている（＝いままでも飛ばせていた）

        if (scanned.getTypeForFile (bundle) != nullptr)
            continue;   // 同じバンドルの2つ目。目印はもうある

        auto marker = description;

        marker.fileOrIdentifier = bundle;

        // **バンドルの更新日時を持たせること。** `pluginNeedsRescanning()`が
        // これと実物を見比べるので、**プラグインを入れ直したら開き直します**
        marker.lastFileModTime = juce::File (bundle).getLastModificationTime();

        if (scanned.addType (marker))
            markerIdentifiers.add (marker.createIdentifierString());
    }

    // **落ちた記録も持ち込む。** これが無いと、`scanNextFile()`が
    // ブラックリスト入りのものをもう一度開きに行きます
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal (scanned,
                                                                        getDeadMansPedalFile());

    const auto deadMansPedal = getDeadMansPedalFile();

    // `formatManager`は読むだけなので、別スレッドから触っても構わない
    for (auto* format : const_cast<juce::AudioPluginFormatManager&> (formatManager).getFormats())
    {
        // 9.5：**内蔵プラグインは走査しない**（Phase 204）。探す先がファイルではないので、
        // `PluginDirectoryScanner`に渡しても何も起きません。
        // **渡さないほうが意図がはっきりします**（`canScanForPlugins()`もfalse）
        if (format->getName() == MantaPlugins::getFormatName())
            continue;

        // 8.157：**`deadMansPedal`を渡すこと**（Phase 195／E2）。
        //
        // `PluginDirectoryScanner`は1つ開く前にここへ「いまこれを開く」と書き、
        // 無事に終わったら消します。**落ちると書きっぱなしで残る**ので、
        // 次のスキャンの前に読めば「前回落ちたもの」が分かります。
        //
        // **Phase 194までは空の`juce::File()`を渡していました**——
        // 書く先が無いので記録も残らず、**毎回同じところで落ちていました**
        // 8.189：**このフォーマットの探す先**（Phase 227）
        const auto searchPath = buildSearchPath (*format, extraFolders);

        juce::PluginDirectoryScanner scanner (scanned, *format, searchPath, true, deadMansPedal);

        juce::String nameOfPluginBeingScanned;

        while (scanner.scanNextFile (true, nameOfPluginBeingScanned))
        {
            // 1ファイルずつスキャン。**まだサンドボックス化していない**ので、
            // 不安定なプラグインがあるとここで落ちる可能性がある
            // （設計書3.2・5.8.1でサンドボックス実行に置き換える予定）。
            // ただし**落ちるのは1回だけ**です（上の`deadMansPedal`）
        }
    }

    // 8.174：**目印は外してから返すこと**（Phase 215）。
    // 混ざったまま`mergeScannedPlugins()`へ渡すと、`fileOrIdentifier`が違うぶん
    // 別物として足され、**ブラウザに同じプラグインが2つ並びます。**
    if (markerIdentifiers.isEmpty())
        return scanned.getTypes();

    juce::Array<juce::PluginDescription> results;

    for (const auto& description : scanned.getTypes())
        if (! markerIdentifiers.contains (description.createIdentifierString()))
            results.add (description);

    return results;
}

void PluginManager::mergeScannedPlugins (const juce::Array<juce::PluginDescription>& found)
{
    // **同じものは二重に入らない**（`addType()`が識別子で見分ける）
    for (const auto& description : found)
        knownPlugins.addType (description);

    // 8.157：**覚えておく**（Phase 195／E2）。書き出しはここ1箇所（1.27）
    saveKnownPlugins();
}

juce::Array<juce::PluginDescription> PluginManager::scanForPlugins (const juce::FileSearchPath& extraFolders)
{
    // 8.157：**別スレッド版と同じ道を通す**（Phase 195）。
    // Phase 194までは同じ走査を2つ書いていて、`deadMansPedal`の直しを
    // 片方だけ入れる、といった食い違いが起きる形でした（8.2）
    mergeScannedPlugins (scanFoldersWithoutApplying (extraFolders, getKnownPlugins()));

    return knownPlugins.getTypes();
}
