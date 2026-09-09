#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    設計書3.1「プラグインフォーマットの抽象化」・3.2「プラグインスキャンのフロー」に
    対応するプラグイン管理クラス。

    サンドボックス（設計書3.4・3.5、仕様書5.8.1）はまだ**スキャンには**使っていません。
    落ちるプラグインがあると、スキャン中にアプリごと落ちる可能性が残っています
    （下の「落ちたものは次から飛ばす」で被害は1回で済むようにしてあります）。

    ### 8.157：スキャンが遅い／落ちる／毎回やり直しになる（Phase 195／8.1のE2）

    本人の報告は3つでした——**1分かかる**、**2〜3回に1回落ちる**、
    **毎回このために環境設定を開くのが面倒**。原因は3つとも別で、全部ここにありました。

    | 症状 | 原因 |
    |---|---|
    | 1分かかる | **毎回まっさらな一覧へ集めていた**ので、「既にあるものは開かない」が一度も効かない |
    | 2〜3回に1回落ちる | **`deadMansPedalFile`に空の`File`を渡していた**ので、落ちたプラグインを次回も開きに行く |
    | 毎回やり直し | **結果をどこにも保存していなかった** |

    #### 覚えておく場所

    `%APPDATA%\PersonalDAW\plugin_list.xml`（設定と同じ場所。8.17の表の対象外で、
    **移せない**もののひとつです——スキャン結果は環境に紐づく控えなので、
    プロジェクトの保存先と一緒に持ち歩くものではありません）。

    中身は`juce::KnownPluginList`のXMLで、**ブラックリストも一緒に入ります**
    （下の`deadMansPedal`が積んだもの）。

    #### 落ちたものは次から飛ばす

    `juce::PluginDirectoryScanner`は、**1つ開く前に「いまこれを開く」と
    ファイルへ書き**、無事に終わったら消します。落ちると書きっぱなしで残るので、
    次のスキャンの前に`applyBlacklistingsFromDeadMansPedal()`を通せば、
    **前回落ちたものがブラックリストへ入ります**。

    **つまり被害は1回だけです。** いまは毎回同じところで落ちていました。
*/
class PluginManager
{
public:
    PluginManager();

    /** 既定のVST3フォルダ（＋追加で指定したフォルダ）をスキャンし、
        見つかったプラグインの一覧を返す。 */
    juce::Array<juce::PluginDescription> scanForPlugins (const juce::FileSearchPath& extraFolders = {});

    /** 8.52：**別スレッドで走らせるためのスキャン**（Phase 91）。

        **`knownPlugins`には触りません**（自前の一覧へ集めて返すだけ）。
        画面のスレッドを止めずに走らせ、**結果の反映は`mergeScannedPlugins()`で
        メッセージスレッドから**行ってください。

        8.157：**`alreadyKnown`を渡すこと**（Phase 195）。集め先の一覧へ先に入れておくと、
        `scanNextFile()`の「既にあるものは開かない」が効きます。
        **ここを空で呼ぶと、毎回全部を開き直します**——それがPhase 194までの動きでした。

        **`alreadyKnown`はメッセージスレッドで取ること**（`getKnownPlugins()`）。
        この関数の中で`knownPlugins`を読むと、画面側の読み出しと衝突します。 */
    juce::Array<juce::PluginDescription> scanFoldersWithoutApplying (
        const juce::FileSearchPath& extraFolders,
        const juce::Array<juce::PluginDescription>& alreadyKnown) const;

    /** スキャン結果を一覧へ反映する。**メッセージスレッドから呼ぶこと。**

        8.157：**反映したらそのまま保存します**（Phase 195）。
        「保存し忘れた経路」を作らないため、書き出しはここ1箇所です（1.27）。 */
    void mergeScannedPlugins (const juce::Array<juce::PluginDescription>& found);

    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** スキャン済みのプラグイン一覧を返す（再スキャンはしない）。 */
    juce::Array<juce::PluginDescription> getKnownPlugins() const { return knownPlugins.getTypes(); }

    //==========================================================================
    // 8.157：覚えておく（Phase 195／8.1のE2）

    /** 覚えてある一覧を読み込む。**コンストラクタで1度呼ばれます。** */
    void loadKnownPlugins();

    /** いまの一覧を書き出す。**`mergeScannedPlugins()`が呼ぶので、普通は要りません。** */
    void saveKnownPlugins() const;

    /** 前回のスキャンで落ちたものをブラックリストへ入れる。

        **スキャンを始める前に呼ぶこと。** 返り値は、そのとき新しく弾いたものの名前
        （画面に「前回ここで落ちたので飛ばします」と出すため。無ければ空）。 */
    juce::StringArray blacklistWhatCrashedLastTime();

    /** ブラックリストに入っているもの（環境設定に出す）。 */
    juce::StringArray getBlacklistedPlugins() const { return knownPlugins.getBlacklistedFiles(); }

    /** ブラックリストを空にする（「もう一度試す」用）。 */
    void clearBlacklist();

    /** 一覧が空か（＝まだ一度もスキャンしていない）。起動時の自動スキャンの判断に使う。 */
    bool hasNoKnownPlugins() const { return knownPlugins.getNumTypes() == 0; }

    /** 覚えてある一覧のファイル。 */
    static juce::File getPluginListFile();

    /** 「いまこれを開いている」を書いておくファイル（`juce::PluginDirectoryScanner`が使う）。 */
    static juce::File getDeadMansPedalFile();

    /** 既定のVST3フォルダ（＋追加分）。**スキャンする場所の決め方はここ1箇所。** */
    juce::FileSearchPath buildSearchPath (const juce::FileSearchPath& extraFolders) const;

    /** 9.5：**Manta Studio内蔵のプラグインを一覧へ足す**（Phase 204）。

        探す先がファイルではないので、**スキャンでは増えません。**
        起動のたびに（`loadKnownPlugins()`のあとで）1度呼ばれます。
        表そのものは`Plugins/MantaPluginFormat.h`にあります。 */
    void addBuiltInPlugins();

private:
    juce::FileSearchPath getDefaultVST3SearchPath() const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginManager)
};
