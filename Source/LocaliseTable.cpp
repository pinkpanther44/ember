#include "Localise.h"

//==============================================================================
/*
    8.163：**訳の表**（Phase 201／本人の要望）。

    ### 見出しはソースに書いてある日本語そのもの

    `{ "保存", "Save", false }` の1つめは、**`utf8 ("保存")`に書いてあるのと
    1文字も違わない**必要があります。違うと引けず、日本語のまま出ます
    （画面は壊れませんが、訳されません）。

    **表は`Tools/ExtractStrings`が作ります**（`LocaliseTable.inc`）。
    手で足すのではなく、**ソースから抜き直して作り直すこと**——
    そうすれば「日本語を書き換えたのに見出しが古い」が起こりません。

    ### 3つめの値（`isHelp`）

    - `false`＝**ラベル**：ボタン名・メニュー・見出し・列名・Undoの名前
    - `true` ＝**説明**：ツールチップ・案内文・エラー文・状態表示

    「英語＋説明は日本語」を選ぶと、`true`のものだけ日本語のまま出ます。

    ### 並び

    **ソースのファイル名順**です。引くのはハッシュ表なので並びは速さに効きませんが、
    **どのファイルの文字列がどれだけあるか**が見えるほうが、
    足し忘れ・訳し漏れを目で探せます。
*/
namespace Localise
{
    namespace
    {
        const Entry table[] =
        {
            #include "LocaliseTable.inc"
        };
    }

    const Entry* getTable (int& numEntriesOut)
    {
        numEntriesOut = (int) (sizeof (table) / sizeof (table[0]));
        return table;
    }
}
