#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

//==============================================================================
/**
    9.5：**Manta Studio内部専用のプラグイン形式**（Phase 204）。

    ### なぜ`juce::AudioPluginFormat`なのか

    プラグインに関わる道は、**全部`AudioEngine::createPluginNode()`の1行**を
    通っています。

    ```cpp
    auto instance = pluginManager.getFormatManager()
                        .createPluginInstance (description, sampleRate, blockSize, errorMessage);
    ```

    `formatManager`へ自前のフォーマットを1つ登録するだけで、次が**何も書かずに**動きます
    （HANDOVER 9.5の表）。

    | | なぜ動くか |
    |---|---|
    | ブラウザに出る／ドラッグで挿せる | 一覧は`PluginManager::getKnownPlugins()`＝`PluginDescription`の配列 |
    | Console・インスペクタのスロットへ挿せる | 挿し口は`PluginDescription`しか見ていない |
    | プロジェクトに保存され、開き直すと戻る | `<PLUGIN>`に入るのは`PluginDescription`＋`getStateInformation()`（設計書3.8） |
    | GUIが開く／オン・オフの帯が付く | `PluginEditorContent`（8.130） |
    | オートメーションが効く | `"insert:<スロット>:<パラメータ番号>"`（仕様書5.6） |

    **`AudioProcessor`を直に`graph`へ足す道は作っていません。**
    そちらを作ると、保存・復元・一覧・オートメーションを全部もう一度書くことになります。

    ### 識別子は絶対に変えない

    `juce::PluginDescription::createIdentifierString()`が**保存の鍵**です
    （プロジェクトの`<PLUGIN>`に書かれ、開き直すときにこれで引き当てます）。
    JUCEの中身は次のとおりで、

    ```
    pluginFormatName + "-" + name + "-" + hex(fileOrIdentifier.hashCode()) + "-" + hex(uniqueId)
    ```

    **`name`まで混ざります**（HANDOVER 9.5には「名前は変えてよい」と書いてありますが、
    JUCE 9の実装では変わります。**表示名も据え置きにすること**）。

    - `pluginFormatName` … `"Manta"`
    - `name`             … `"Manta EQ"` のような**ASCII固定**。訳さないこと
    - `fileOrIdentifier` … `"manta:eq"`
    - `uniqueId`         … `Entry::uniqueId`（4文字の目印を数値にしたもの）

    中身が変わったぶんは`setStateInformation()`の側で吸収します
    （古い状態を読めるようにしておくこと）。

    ### 表示名を訳さない理由

    `name`は識別子に混ざるので、`utf8()`を通すと**言語を変えた瞬間に
    保存済みのプロジェクトがプラグインを見失います。**
    画面に出る日本語は**プラグインのGUIの中**だけにしてあります。

    ### 増やすとき

    `MantaPlugins::getEntries()`の表へ1行足すだけです。
    `PluginManager`もブラウザも、表を見ているので何も変えなくて済みます。
*/
namespace MantaPlugins
{
    /** `PluginDescription::pluginFormatName`に入る名前。**変えないこと**（識別子の一部）。 */
    juce::String getFormatName();

    /** 内蔵プラグイン1つぶんの登録内容。 */
    struct Entry
    {
        const char* identifier;    ///< `fileOrIdentifier`。**絶対に変えない**
        const char* name;          ///< 表示名。**ASCII固定**（識別子の一部なので訳さない）
        const char* descriptiveName;
        const char* category;      ///< `"Fx|EQ"`のようにVST3と同じ書き方（ブラウザの分類。8.68）
        const char* version;
        int uniqueId;              ///< **変えない**（識別子の一部）
        bool isInstrument;

        /** 実体を作る。**メッセージスレッドから呼ばれます。** */
        std::function<std::unique_ptr<juce::AudioPluginInstance>()> create;
    };

    /** 登録してある内蔵プラグインの表。**増やすときはここへ1行。** */
    const std::vector<Entry>& getEntries();

    /** 表をそのまま`PluginDescription`にしたもの（`PluginManager`が一覧へ足す）。 */
    juce::Array<juce::PluginDescription> getDescriptions();

    /** 識別子から`PluginDescription`を1つ引く。

        **`juce::AudioPluginInstance::fillInPluginDescription()`の中身はこれ1つ**に
        してあります。プラグイン側で自分の説明を組み直すと、表と食い違ったときに
        **識別子が変わって保存済みのプロジェクトが見失います**（1.27）。 */
    bool findDescription (const juce::String& identifier, juce::PluginDescription& result);

    /** この`PluginDescription`が内蔵プラグインか。

        **クラッシュ履歴とスキャンから外すのに使います**（HANDOVER 9.5）。
        外のプラグイン向けの仕掛けなので、自前のものへ当てると
        「落ちたので次から読み込みません」と**自分のバグを隠してしまいます。** */
    bool isMantaPlugin (const juce::PluginDescription& description);
}

//==============================================================================
/**
    内蔵プラグインを`juce::AudioPluginFormatManager`へ載せるためのフォーマット。

    **探しに行く先はありません**（ファイルではないので）。
    `canScanForPlugins()`が`false`、`searchPathsForPlugins()`が空を返すので、
    `juce::PluginDirectoryScanner`に渡しても何も起きません
    （`PluginManager`の側でも、念のため走査の対象から外してあります）。
*/
class MantaPluginFormat : public juce::AudioPluginFormat
{
public:
    MantaPluginFormat() = default;

    juce::String getName() const override;

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                               const juce::String& fileOrIdentifier) override;

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;

    /** **常に`false`。** 実体はアプリの中にあるので、探し直す意味がありません。 */
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }

    /** アプリに入っているかぎり必ずある（表に載っていれば`true`）。 */
    bool doesPluginStillExist (const juce::PluginDescription& description) override;

    /** **走査しない。** 一覧へは`PluginManager`が毎回足します（HANDOVER 9.5）。 */
    bool canScanForPlugins() const override { return false; }

    bool isTrivialToScan() const override { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }

    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }

    /** 作るのに時間はかからないので、同期で作って構わない。 */
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

protected:
    void createPluginInstance (const juce::PluginDescription& description,
                                double initialSampleRate, int initialBufferSize,
                                PluginCreationCallback callback) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MantaPluginFormat)
};
