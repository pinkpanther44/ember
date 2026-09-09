#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

// juce::PluginDescriptionはコールバックの引数として使うため、宣言だけでは足りない。
// juce_gui_extraはjuce_audio_processorsを引き込まないので、ここで明示する。
#include <juce_audio_processors/juce_audio_processors.h>

#include "IconAssets.h"   // 8.133：フォルダ・ファイルの絵（Phase 169）
#include <algorithm>
#include <vector>

class AudioEngine;

//==============================================================================
/**
    設計書2.2「左：ブラウザパネル」・2.3.6・仕様書4.4に対応する、左サイドパネル（Phase 17）。

    タブで「プラグイン／ファイル」を切り替える。仕様書4.4はコードプリセットも挙げているが、
    コード進行支援（5.11）自体が未実装なので、そのタブはまだ出していない。

    - **プラグインタブ**：スキャン済みのVST3を一覧表示する。上部の検索ボックスで名前を絞り込み、
      種別（すべて／音源／エフェクト）とお気に入りでも絞れる（仕様書4.4のフィルタリング）。
      お気に入りはプロジェクトではなく**アプリ全体の設定**（AppSettings）に保存する。
      どのプロジェクトを開いても同じ顔ぶれでいてほしいため。
    - **ファイルタブ**：オーディオファイルをツリー表示する。

    仕様書4.4・6章の**ドラッグ&ドロップでの挿入に対応している**（Phase 21）。
    プラグインの行もファイルツリーもドラッグでき、受け口はタイムラインの
    トラックヘッダーとミキサーのチャンネルストリップ（DragAndDropIds.h参照）。
    ダブルクリックでの追加も、素早く済ませたいとき用に残してある。
*/
class BrowserPanel : public juce::Component,
                      private juce::ChangeListener,
                      private juce::FileBrowserListener
{
public:
    BrowserPanel (AudioEngine& audioEngineToUse);
    ~BrowserPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    /** プラグイン一覧を読み直す（Pluginsタブでスキャンした後に呼ぶ）。 */
    void refreshPluginList();

    /** プラグインがダブルクリックされたときに呼ばれる。
        音源かエフェクトかで挿入先が変わるため、判断は呼び出し元に任せる。 */
    std::function<void (const juce::PluginDescription&)> onPluginChosen;

    /** オーディオファイルがダブルクリックされたときに呼ばれる（タイムラインへの取り込み用）。 */
    std::function<void (const juce::File&)> onAudioFileChosen;

    /** パネルの標準幅。MainComponent側のレイアウト計算に使う。 */
    static constexpr int defaultWidth = 240;
    static constexpr int minimumWidth = 160;

private:
    enum class Tab { Plugins, Files };

    void setTab (Tab newTab);
    void updateTabButtons();

    /** 検索文字列とフィルタを適用して、表示するプラグインを絞り込む。 */
    void applyPluginFilter();

    /** 8.68：**カテゴリの一覧をスキャン結果から作り直す**（Phase 107／改善案⑧。仕様書4.4）。

        ### なぜ決め打ちの一覧にしないか

        カテゴリ（`juce::PluginDescription::category`）は**プラグインが自分で名乗る文字列**で、
        規格で決まった語彙がありません。こちらで「リバーブ／ディレイ／EQ…」と並べても、
        **入っているプラグインが1つも名乗っていない分類**が並ぶだけで、
        逆に名乗っている分類がどこにも出てこない、ということが起こります。

        **入っているものから作れば、必ず中身のある一覧になります。**

        ### `|`で切って並べる

        VST3は`"Fx|Reverb"`のように**縦棒で連ねた**書き方をします。
        そのまま1項目にすると`"Fx|Reverb"`と`"Fx|Delay"`が別物として並び、
        「Fxで絞る」ができません。切ってから集めています。 */
    void refreshCategoryFilter();

    /** 8.68：1つのプラグインが名乗っているカテゴリ（`|`で切ったもの）。
        何も名乗っていなければ空の配列を返す。 */
    static juce::StringArray getCategoryTokens (const juce::PluginDescription& description);

    bool isFavourite (const juce::PluginDescription& description) const;
    void toggleFavourite (const juce::PluginDescription& description);

    /** フォルダの走査が進んだときの通知（DirectoryContentsList）。 */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // ファイルツリーの操作を受け取る（juce::FileBrowserListener）。
    // 使うのはfileDoubleClicked()だけだが、純粋仮想なので全部実装する必要がある。
    /** 仕様書4.4：選択が変わるたびに、ドラッグで運ぶ情報を差し替える（Phase 21）。

        `FileTreeComponent`のドラッグ情報は「ツリー全体に1つ」しか持てないため、
        行ごとに違う値を渡すことができない。選択された瞬間に更新しておけば、
        ドラッグは必ず選択された行から始まるので実用上は同じことになる。 */
    void selectionChanged() override;
    void fileClicked (const juce::File& file, const juce::MouseEvent& e) override;
    void fileDoubleClicked (const juce::File& file) override;
    void browserRootChanged (const juce::File& newRoot) override;

    AudioEngine& audioEngine;

    Tab currentTab = Tab::Plugins;
    juce::TextButton pluginsTabButton { "Plugins" };
    juce::TextButton filesTabButton   { "Files" };

    //==========================================================================
    // プラグインタブ
    juce::TextEditor searchBox;

    /** 8.164：**種別はトグルボタン3つ**（Phase 202／本人の要望）。

        Phase 201まではコンボボックスでした。**いま何で絞っているか**を読むのに
        中身を開くまで分からない——というほどではありませんが、
        **3つしかないものを1行ずつ開いて選ぶ**のは押す回数が多い。
        **押した瞬間に切り替わり、選ばれているものが色で分かる**ほうが速いです。

        `setRadioGroupId()`で3つを1組にしてあります——
        **1つ選ぶと他は自動で外れ、選ばれているものを押しても外れません**
        （「どれも選んでいない」状態を作らせない）。 */
    juce::TextButton typeAllButton, typeInstrumentButton, typeEffectButton;

    /** 種別の絞り込み（1＝すべて／2＝音源／3＝エフェクト）。

        **番号はPhase 201までのコンボボックスのIDと同じ**にしてあります——
        `applyPluginFilter()`の分岐を書き換えずに済むので。 */
    int getTypeFilter() const;

    /** 8.68：カテゴリで絞る（Phase 107／改善案⑧）。中身は`refreshCategoryFilter()`が作る。 */
    juce::ComboBox categoryFilterBox;

    /** 8.164：**お気に入りだけ**（Phase 202／本人の報告）。

        Phase 201までは`juce::ToggleButton { "★ only" }`でした。
        **文字化けしていました**（画面には`â□□`と出ていた）——
        `★`はUTF-8で3バイトあり、`juce::String`の`const char*`版は
        7bit ASCIIとして読むためです（`Utf8.h`の説明そのままの穴）。
        **リテラルを直接コンストラクタへ渡していたのが原因**で、
        一覧の行の★（`fromUTF8`を通している）はずっと正しく出ていました。

        文字は**★だけ**にしました（本人の指定）。何のボタンかはツールチップで補います。 */
    juce::TextButton favouritesOnlyButton;
    juce::ListBox pluginListBox;
    juce::Label emptyLabel;

    juce::Array<juce::PluginDescription> allPlugins;      // スキャン結果そのもの
    juce::Array<juce::PluginDescription> filteredPlugins; // 表示中のもの（メーカー順に並べ替え済み）

    //==========================================================================
    // 8.162：**メーカーごとにまとめる**（Phase 200／本人の要望）。
    //
    // 本人の言葉は「ブラウザーのプラグインの並びを、プラグインメーカーごとに
    // まとめて分けてほしい」。
    //
    // **一覧に出る行と、プラグインの並びを分けます。**
    // 見出しの行はプラグインではないので、`filteredPlugins`の番号では表せません
    // （番号がずれると、押した行と挿さるプラグインが食い違う。1.32と同じ話）。

    /** 一覧の1行ぶん。**見出しか、プラグインか。** */
    struct PluginRow
    {
        /** 見出しの行ならメーカー名。プラグインの行なら空。 */
        juce::String manufacturer;

        /** プラグインの行なら`filteredPlugins`の番号。見出しなら-1。 */
        int pluginIndex = -1;

        /** 見出しの行だけ：そのメーカーが持っている数（畳んでいても出す）。 */
        int count = 0;

        bool isHeader() const { return pluginIndex < 0; }
    };

    std::vector<PluginRow> pluginRows;

    /** 8.163：**開いているメーカー**（Phase 201／本人の要望）。

        **「畳んでいるほう」ではなく「開いているほう」を持ちます。**
        本人の要望は「Browserは、デフォルトで畳まれている状態にしたい」で、
        畳んでいるほうを持つと**新しく出てきたメーカーが必ず開いた状態**になります
        （スキャンし直したとき・絞り込みを変えたときに、勝手に開くものが出る）。
        開いているほうを持てば、**名前が出てこないかぎり畳んだまま**です。

        **アプリ設定には保存しません**——閉じて開いたら畳まれている、が既定。 */
    juce::StringArray expandedManufacturers;

    /** そのメーカーの中身を出すか。

        **探しているときは畳みません。** 検索の文字が入っているのに見出しだけが
        並んだら、**探し物が見つからなかったのと見分けが付きません。** */
    bool isManufacturerExpanded (const juce::String& manufacturer) const;

    /** そのプラグインのメーカー名。名乗っていないときの置き場所も含めてここで決める。 */
    static juce::String getManufacturerLabel (const juce::PluginDescription& description);

    /** `filteredPlugins`から`pluginRows`を組み直す。**並べ替えもここ**。 */
    void rebuildPluginRows();

    /** その行のプラグイン（見出しの行・範囲外ならnullptr）。 */
    const juce::PluginDescription* getPluginForRow (int row) const;

    /** 8.68：`categoryFilterBox`に並べたカテゴリ（「すべて」は含まない）。
        選ばれた番号から名前を引き直すために持つ——**番号だけを覚えると、
        スキャンし直して並びが変わったときに別の分類で絞ってしまう**。 */
    juce::StringArray pluginCategories;

    //==========================================================================
    // ファイルタブ。DirectoryContentsListはバックグラウンドスレッドで走査するため、
    // 専用のTimeSliceThreadが要る（JUCEの標準構成）。
    juce::TimeSliceThread directoryScanThread { "Browser file scan" };
    juce::WildcardFileFilter audioFileFilter;
    std::unique_ptr<juce::DirectoryContentsList> directoryList;
    std::unique_ptr<juce::FileTreeComponent> fileTree;
    juce::TextButton chooseFolderButton { "Folder..." };

    //==========================================================================
    /** 8.133：**フォルダとファイルの絵**（Phase 169／改善案リスト3の44）。

        `juce::FileTreeComponent`は行の絵を`LookAndFeel`に描かせるので、
        被せないと差し替えられません。

        **色は塗り替えません。** もらった絵は既に色が付いていて
        （フォルダ＝紫、ファイル＝オレンジ）、**それが見分けの手がかり**だからです。
        ツール類（黒）だけを塗り替えているのと扱いが違います。

        **絵は1度だけ読んで持っておくこと。** 行ごと・描き直しごとに読むと、
        スクロールのたびにSVGを解析することになります。 */
    class FileRowLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        FileRowLookAndFeel()
            : folderIcon (IconAssets::load ("browser_folder_svg")),
              fileIcon   (IconAssets::load ("browser_file_svg"))
        {
            setColourScheme (AppColours::createColourScheme());   // 1.43・8.117
        }

        void drawFileBrowserRow (juce::Graphics& g, int width, int height,
                                  const juce::File&, const juce::String& filename,
                                  juce::Image* icon, const juce::String& fileSizeDescription,
                                  const juce::String& fileTimeDescription,
                                  bool isDirectory, bool isItemSelected, int itemIndex,
                                  juce::DirectoryContentsDisplayComponent& dcc) override;

    private:
        std::unique_ptr<juce::Drawable> folderIcon;
        std::unique_ptr<juce::Drawable> fileIcon;
    };

    FileRowLookAndFeel fileRowLookAndFeel;


    std::unique_ptr<juce::FileChooser> fileChooser;

    //==========================================================================
    class PluginListModel : public juce::ListBoxModel
    {
    public:
        explicit PluginListModel (BrowserPanel& ownerIn) : owner (ownerIn) {}

        int getNumRows() override { return (int) owner.pluginRows.size(); }
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override;

        /** 仕様書4.4：行をドラッグしたときに運ぶ情報（Phase 21）。
            ここが空のvarを返すと、ListBoxはドラッグを開始しない。 */
        juce::var getDragSourceDescription (const juce::SparseSet<int>& selectedRows) override;

    private:
        BrowserPanel& owner;
    };

    PluginListModel pluginListModel { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserPanel)
};
