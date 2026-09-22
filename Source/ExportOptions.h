#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Utf8.h"   // 8.307：一覧の日本語（Phase 300でここへ移りました）

#include <vector>

//==============================================================================
/**
    8.80：仕様書5.10「書き出し」の設定（Phase 120／8.1のD9・D10・D11）。

    ### なぜ1つにまとめたか

    D9（形式）・D10（ステムのモノ／ステレオ）・D11（ループ範囲だけ）は、
    どれも**同じ「書き出す前に決めること」**です。バラバラに作ると、
    設定を足すたびに書き出しの入口が増え、
    **「どこで何を決めるのか」が押してみないと分からなく**なります（8.1のJの7番目）。

    ミックスダウンもステムも**同じダイアログ**を通します。
    違うのは「ステムのときだけ出る項目」があることだけです。

    ### サンプルレートとMP3（Phase 191で足しました。8.153）

    Phase 190まで、この2つは出していませんでした。理由はどちらも片付いています。

    | | Phase 190までの理由 | いま |
    |---|---|---|
    | サンプルレート | クリップ再生がリサンプリングしていなかった | **Phase 190で直した**（8.152） |
    | MP3 | JUCEにエンコーダが無く、LAMEを抱える話になる | **Windowsが持っていた**（`Mp3Writer.h`） |
*/

/** 8.81：ステム書き出しの、トラック1本ぶんの設定（Phase 121/D10a）。

    **ダイアログへ渡す入力**（`trackId`・`name`・`isFolder`・`depth`）と、
    **返ってくる答え**（`include`・`mono`）を1つの型にまとめてあります。
    分けると、並び順を2箇所で合わせることになります（8.2）。 */
struct ExportStemTrack
{
    juce::String trackId;
    juce::String name;

    /** フォルダか（一覧で見分けが付くように印を出す）。 */
    bool isFolder = false;

    /** 何階層目にいるか（一覧の字下げに使う）。 */
    int depth = 0;

    /** 書き出すか。 */
    bool include = true;

    /** モノラルで書き出すか。 */
    bool mono = false;
};

struct ExportOptions
{
    /** 8.153：書き出す形式（Phase 191／8.1のD9b）。 */
    enum class Format { wav, mp3 };

    Format format = Format::wav;

    /** WAVのビット深度。16／24／32（32は浮動小数点）。**MP3では使いません。** */
    int bitsPerSample = 24;

    /** 8.153：MP3のビットレート（kbps）。**WAVでは使いません。** */
    int mp3BitrateKbps = 320;

    /** 8.153：書き出すサンプルレート（Phase 191／8.1のD9a）。

        **0なら「いまのオーディオデバイスのまま」**です。0を「48000」と書かないのは、
        デバイスの設定を変えたときに**書き出しだけ古いレートで固まる**のを避けるため——
        既定は「いま鳴っているのと同じ」であってほしい。

        **クリップの音程は変わりません**（Phase 190／8.152）。
        グラフごとこのレートで回すので、プラグインもこのレートで動きます。 */
    double sampleRate = 0.0;

    /** 拡張子（`.wav`／`.mp3`）。**判定を書き写さないこと**（8.2）。 */
    juce::String getFileExtension() const { return format == Format::mp3 ? ".mp3" : ".wav"; }

    /** ループ範囲だけを書き出すか（8.1のD11）。

        **ループが引かれていないときは選べません**（呼び出し側が項目を出しません）。
        ループのON/OFFとは無関係です——「再生時に繰り返すか」と
        「書き出す範囲」は別の話なので、**範囲さえ引いてあれば使えます**。 */
    bool loopRangeOnly = false;

    /** 8.81：ステム書き出しの、トラックごとの設定（Phase 121/D10a）。
        ミックスダウンでは使いません。 */
    std::vector<ExportStemTrack> stemTracks;

    /** そのトラックを書き出すか。**一覧に無ければ書き出します**——
        トラックが後から増えたときに、黙って落ちるより出るほうが安全です。 */
    bool shouldExportStem (const juce::String& trackId) const;

    /** そのトラックをモノラルで書き出すか。一覧に無ければステレオ。 */
    bool isStemMono (const juce::String& trackId) const;
};

//==============================================================================
/**
    8.307：**ヘッダーへ出しました**（Phase 300／本人の報告）。

    もとは`ExportOptions.cpp`の無名namespaceの中にありました。
    出したのは**窓を出さずに並びを数えるため**です——
    本人の報告（「トラック数が多くなるとモノラルが見切れる」）は
    **中身の高さが窓を超えたときにだけ**出るので、
    **行をたくさん作って測らないと確かめられません**（8.295と同じ話）。

    使うのは`ExportOptionsDialog::show()`と、`--header-selftest`だけです。
*/
/**
    8.81：ステムのトラック一覧（Phase 121／8.1のD10a）。

    1行につき **［書き出す］トラック名［モノ］** の3つ。
    フォルダの中のトラックは**字下げ**して、どこに入っているかが分かるようにします。

    **`juce::AlertWindow::addCustomComponent()`はコンポーネントを所有しません。**
    ダイアログが消えるまで生かしておくのは呼び出し側の仕事です（下の`DialogHolder`）。
*/
class StemTrackList : public juce::Component
{
public:
    explicit StemTrackList (const std::vector<ExportStemTrack>& tracksToShow)
        : tracks (tracksToShow)
    {
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            auto* row = rows.add (new Row());

            row->include.setToggleState (tracks[i].include, juce::dontSendNotification);
            row->mono.setToggleState (tracks[i].mono, juce::dontSendNotification);

            row->name.setText (juce::String::repeatedString ("    ", tracks[i].depth)
                                 + (tracks[i].isFolder ? utf8 ("［フォルダ］") : juce::String())
                                 + tracks[i].name,
                                juce::dontSendNotification);
            row->name.setFont (juce::FontOptions (12.0f));

            // **ラベルにマウスを食べさせない。** 名前を押しても何も起きないのは
            // 構いませんが、押した感じだけ出るのは紛らわしい（8.74と同じ話）
            row->name.setInterceptsMouseClicks (false, false);

            content.addAndMakeVisible (row->include);
            content.addAndMakeVisible (row->name);
            content.addAndMakeVisible (row->mono);
        }

        selectAllButton.onClick = [this] { setAllIncluded (true); };
        noneButton.onClick      = [this] { setAllIncluded (false); };

        addAndMakeVisible (selectAllButton);
        addAndMakeVisible (noneButton);

        // 8.307：**列の真上に置く**（Phase 300／本人の指定）。
        //
        // Phase 299までは「左＝書き出す　／　右＝モノラル」と**文で**書いてありました。
        // 文だと**目で列まで辿り直す**ことになります——見出しは、
        // それが指しているものの真上にあるときだけ見出しとして働きます。
        //
        // 左の列に見出しが要らないのは、**そちらが既定の操作**だからです
        // （「全部」「全部外す」が同じ列に効くので、何の列かはそこで分かります）
        headerLabel.setText (utf8 ("モノラル"), juce::dontSendNotification);
        headerLabel.setFont (juce::FontOptions (11.0f));
        headerLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (headerLabel);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        setSize (420, 220);
    }

    //==========================================================================
    // 8.307：**測るための入口**（Phase 300）。
    //
    // `--header-selftest`から、行の矩形と「見えている幅」を見ます。
    // **窓に出していない状態でも答えられること**——道具は窓を作りません。

    struct Row
    {
        juce::ToggleButton include;
        juce::Label name;
        juce::ToggleButton mono;
    };

    /** 行を1つ覗く。範囲の外なら`nullptr`。 */
    const Row* getRowForTesting (int index) const
    {
        return juce::isPositiveAndBelow (index, rows.size()) ? rows[index] : nullptr;
    }

    /** スクロールバーを除いた、中身の見えている幅。 */
    int getVisibleContentWidthForTesting() const { return content.getWidth(); }

    /** 中身が置かれている場所（この部品の中でのX）。 */
    int getViewportXForTesting() const { return viewport.getX(); }

    /** 「モノラル」の見出しの矩形。 */
    juce::Rectangle<int> getMonoCaptionBoundsForTesting() const { return headerLabel.getBounds(); }

    /** 画面の状態を`ExportStemTrack`へ書き戻す。 */
    std::vector<ExportStemTrack> getResult() const
    {
        auto result = tracks;

        for (size_t i = 0; i < result.size() && i < (size_t) rows.size(); ++i)
        {
            result[i].include = rows[(int) i]->include.getToggleState();
            result[i].mono    = rows[(int) i]->mono.getToggleState();
        }

        return result;
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto buttonRow = area.removeFromTop (22);

        area.removeFromTop (4);
        viewport.setBounds (area);

        // 8.307：**スクロールバーが出るぶんを、先に引く**（Phase 300／本人の報告）。
        //
        // > 「トラック数が多くなると、モノラルチェックボックスが
        // > スクロールバー等により見切れてしまう」
        //
        // Phase 299までは`viewport.getMaximumVisibleWidth()`を使っていました。
        // あれが答えるのは「**いま出ているスクロールバー**を除いた幅」で、
        // 中身の高さを決めるのは**その次の行**です——つまり**1回目は必ず
        // 「バーは無い」と答えます**。中身が広いまま作られ、あとから出てきた
        // バーが右端に重なるので、**いちばん右のモノラルだけが隠れます**。
        //
        // **出るかどうかは、高さから自分で決めること**（8.295でラックの
        // バイパスボタンが隠れたのと、同じ形・同じ直し方）。
        const int wantedHeight = juce::jmax (1, rows.size() * rowHeight);
        const bool willScroll = wantedHeight > viewport.getHeight();

        const int contentWidth = juce::jmax (200, viewport.getWidth()
                                                    - (willScroll ? viewport.getScrollBarThickness() : 0));

        content.setSize (contentWidth, wantedHeight);

        for (int i = 0; i < rows.size(); ++i)
        {
            auto row = juce::Rectangle<int> (0, i * rowHeight, content.getWidth(), rowHeight);

            rows[i]->include.setBounds (row.removeFromLeft (columnWidth));
            rows[i]->mono.setBounds (row.removeFromRight (columnWidth));
            rows[i]->name.setBounds (row);
        }

        //------------------------------------------------------------------
        // 8.307：見出しは**モノラルの列の真上**（Phase 300／本人の指定）。
        //
        // **列の右端に合わせて置くこと。** 「四角の中心に合わせて中央寄せ」だと、
        // 列が部品の右端にあるぶん**はみ出して、押し戻されます**
        // （`constrainedWithin`が左へずらすので、**11pxずれて**いました。
        // `--header-selftest`が**4トラックのときだけ**見つけました——
        // スクロールバーが出ているほうだけ見ていたら、通り抜けています）。
        //
        // 幅を列より少し広く取り、**右端を揃える**と、中身の中心が
        // 四角の中心とほぼ重なります（差は3px）。
        //
        // **スクロールバーのぶんも入っている**（`contentWidth`から出しているため）
        const int monoColumnRight = viewport.getX() + contentWidth;

        selectAllButton.setBounds (buttonRow.removeFromLeft (72));
        buttonRow.removeFromLeft (4);
        noneButton.setBounds (buttonRow.removeFromLeft (84));

        headerLabel.setBounds (monoColumnRight - captionWidth, buttonRow.getY(),
                                captionWidth, buttonRow.getHeight());
    }

private:
    // `Row`はpublicにあります（8.307：測るための入口）

    void setAllIncluded (bool shouldInclude)
    {
        for (auto* row : rows)
            row->include.setToggleState (shouldInclude, juce::dontSendNotification);
    }

    static constexpr int rowHeight = 22;

    /** 8.307：チェックの列の幅と、その中で四角が描かれる中心（Phase 300）。

        **見出しを合わせるのに要ります**（`resized()`）。
        `juce::ToggleButton`の四角は左端から描かれ、高さの8割ぶんの正方形です
        ——22pxの行なら約18px、その中心はおよそ9pxの位置。 */
    static constexpr int columnWidth = 28;
    static constexpr int tickBoxCentre = 9;

    /** 見出し「モノラル」の幅。**列より広く取って、右端を揃えます**（`resized()`）。 */
    static constexpr int captionWidth = 44;

    std::vector<ExportStemTrack> tracks;
    juce::OwnedArray<Row> rows;

    juce::Component content;
    juce::Viewport viewport;
    juce::Label headerLabel;
    juce::TextButton selectAllButton { utf8 ("全部") };
    juce::TextButton noneButton { utf8 ("全部外す") };
};

//==============================================================================
/**
    8.80：書き出しの設定を聞くダイアログ（Phase 120）。

    **`juce::AlertWindow`で組んでいます。** 項目が数個で、押したら消える一度きりの
    やり取りなので、専用のコンポーネントを作るほどではありません
    （`PreferencesDialog`のような「開いたまま触るもの」とは性質が違います）。

    **非同期です。** JUCEのモーダルループを回す形（`runModalLoop()`）は
    メッセージスレッドを止めるので、この作りでは使いません（1.5と同じ理由）。
*/
namespace ExportOptionsDialog
{
    /** 設定を聞いて、OKなら`onAccepted`を呼ぶ（キャンセルなら呼ばない）。

        @param isStems             ステム書き出しか（ステム専用の項目を出すかの判断）
        @param loopRangeSeconds    引かれているループの長さ（秒）。0以下なら
                                   「ループ範囲だけ」の項目を出さない
        @param previousOptions     前回の設定。**`stemTracks`にいまのトラック一覧を
                                   入れて渡すこと**（名前・階層・前回の選択をそこから読む）
        @param onAccepted          決まった設定を受け取る
    */
    void show (bool isStems, double loopRangeSeconds, const ExportOptions& previousOptions,
                std::function<void (const ExportOptions&)> onAccepted);
}
