#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
