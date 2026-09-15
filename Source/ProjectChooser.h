#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ProjectTemplates.h"

//==============================================================================
/**
    8.151：**起動時のプロジェクト選択画面**（Phase 189／改善案⑰）。

    起動画面（`SplashWindow`）とメインウィンドウの間に出ます。

    ```
    起動画面 ──▶ プロジェクト選択画面 ──▶ プロジェクト
    ```

    ### 出ないことがある

    **2つの場合だけ、飛ばして本体へ行きます。**

    | | なぜ飛ばすか |
    |---|---|
    | `.ms1`を引数に起動した（エクスプローラでダブルクリック） | **開くものが決まっている** |
    | オートセーブが残っている（前回異常終了） | **先に「どれで始めるか」を訊いて、その後に「復元しますか」と訊くと、最初の答えを捨てることになる** |

    ### 閉じたら終了する

    ×を押したら**アプリごと終わります**。ここで空のプロジェクトを開くと、
    「選ばずに閉じた」が「選んだ」と同じ結果になり、閉じる意味がなくなります。
    （まだ何も作っていない時点なので、失うものはありません。）
*/
namespace ProjectChooser
{
    struct Result
    {
        enum class Type
        {
            fromTemplate,   // `entry`のテンプレートで始める
            openFile,       // `file`を開く
            quit            // 何も選ばずに閉じた
        };

        Type type = Type::quit;
        ProjectTemplates::Entry entry;
        juce::File file;
    };
}

//==============================================================================
class ProjectChooserComponent : public juce::Component
{
public:
    ProjectChooserComponent();
    ~ProjectChooserComponent() override;

    /** 選ばれたときに1度だけ呼ばれる。 */
    std::function<void (const ProjectChooser::Result&)> onChosen;

    /** 8.254：**選ばれたあと、閉じずに「読み込み中」へ切り替える**
        （Phase 262／本人の要望）。

        `detail`には開くものの名前を出します——**何を待っているのか**が分かるように。

        > **ここから先、選ぶための部品は全部隠します**（8.161：押しても何も
        > 起きないものを、押せそうに見せない）。 */
    void showLoading (const juce::String& message, const juce::String& detail);

    /** 段が変わったときに文字だけ差し替える（窓はそのまま）。 */
    void setLoadingMessage (const juce::String& message);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==========================================================================
    /** 8.254：くるくる（Phase 262）。

        ─────────────────────────────────────────────────────────────────
        **読み込みのあいだ、これは止まります**
        ─────────────────────────────────────────────────────────────────

        プロジェクトを開く処理（`MainComponent::loadProjectFile()`）は
        **メッセージスレッドを塞いだまま**走ります——中で市販プラグインを
        1つずつ作るので、13個のプロジェクトでは10秒近くかかることがあります。

        そのあいだタイマーは呼ばれないので、**絵は止まったまま**です。
        動くのは「切り替えた直後」と「段が変わったところ」だけ。

        > **それでも出す価値はあります。** 止まっていても**窓が在る**ことと、
        > **何をしているかが書いてある**ことが、いちばん効きます
        > （本人の報告：「落ちてしまったのかと思うこともある」）。
        >
        > **本当に回し続けるには**、プラグインの復元を後回しにして
        > 本体を先に見せる作りへ変えるしかありません。**今回はそこまでやりません。** */
    class BusySpinner : public juce::Component,
                         private juce::Timer
    {
    public:
        BusySpinner();

        void start();
        void stop();

        void paint (juce::Graphics& g) override;

    private:
        void timerCallback() override;

        float phase = 0.0f;
    };

    //==========================================================================
    /** 一覧の中身。**テンプレートと履歴で同じ描き方**にしてある——
        2行（見出しと補足）で、選ばれている行だけ帯が付く。 */
    class RowListModel : public juce::ListBoxModel
    {
    public:
        struct Row
        {
            juce::String title;
            juce::String subtitle;
        };

        std::vector<Row> rows;
        std::function<void (int)> onRowDoubleClicked;
        std::function<void()> onSelectionChanged;

        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height,
                                bool rowIsSelected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void selectedRowsChanged (int) override;
    };

    void refreshTemplates();
    void refreshRecent();
    void updateButtonStates();

    void startFromSelectedTemplate();
    void openSelectedRecent();
    void browseForProject();
    void deleteSelectedTemplate();

    /** 選ばれたことを1度だけ伝える。**2度呼ばない**——
        ダブルクリックとボタンで同じ行が2回通ることがある（1.5と同じ性質） */
    void report (const ProjectChooser::Result& result);

    juce::Label titleLabel, versionLabel;
    juce::Label templateCaption, recentCaption;

    // **モデルをListBoxより先に宣言すること。**
    //
    // `juce::ListBox`のコンストラクタは、渡されたモデルの中身
    // （`ListBoxModel::sharedState`という`shared_ptr`）を**その場で読みます**。
    // 後ろに置くと、**まだコンストラクタが走っていないモデル**を読むことになり、
    // 起動した瞬間にアクセス違反で落ちます（実際に落としました）。
    //
    // メンバは**書いた順に作られる**ので、順番が仕様です
    RowListModel templateModel, recentModel;

    juce::ListBox templateList { "templates", &templateModel };
    juce::ListBox recentList   { "recent", &recentModel };

    std::vector<ProjectTemplates::Entry> templateEntries;
    juce::Array<juce::File> recentFiles;

    juce::TextButton startFromTemplateButton, openRecentButton;
    juce::TextButton deleteTemplateButton, browseButton, quitButton;

    juce::Label emptyRecentLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    bool alreadyChosen = false;

    //==========================================================================
    // 8.254：読み込み中の表示（Phase 262）

    juce::Label loadingLabel, loadingDetail, loadingHint;
    BusySpinner spinner;

    /** **`resized()`が見ます。** 出す部品が入れ替わるので、置き方も分かれます。 */
    bool loading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectChooserComponent)
};

//==============================================================================
/** 選択画面を載せる窓。**呼ぶ側が持ちます**（閉じたら終了させたいので、
    自分で自分を消す`DialogWindow::launchAsync()`は使っていません）。 */
class ProjectChooserWindow : public juce::DocumentWindow
{
public:
    ProjectChooserWindow();

    /** 選ばれた／×で閉じられた。**どちらもここへ来ます**（×は`Type::quit`）。 */
    std::function<void (const ProjectChooser::Result&)> onChosen;

    /** 8.254：中身を「読み込み中」へ切り替える（Phase 262）。

        **窓は閉じません。** 閉じてから本体が出るまでのあいだ、
        画面に何も無い時間ができてしまうためです（本人の報告）。 */
    void showLoading (const juce::String& message, const juce::String& detail);
    void setLoadingMessage (const juce::String& message);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProjectChooserWindow)
};
