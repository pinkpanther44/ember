#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    8.260：**サンドボックスを、窓を出さずに確かめる**（Phase 268）。

    ```
    "Manta Studio.exe" --sandbox-selftest
    "Manta Studio.exe" --sandbox-selftest "C:\path\to\Some.vst3"
    ```

    引数を省くと**内蔵プラグイン**（`manta:eq`）で試します——
    どの環境にも必ずあるので、**外のプラグインが無くても確かめられます**
    （そのために`SandboxWorker`へ`MantaPluginFormat`を載せてあります）。

    ### 何を見るか

    | | なぜ |
    |---|---|
    | 子プロセスで読めるか | サンドボックスの入口 |
    | パラメータの数が同じか | **オートメーションは番号で覚えています**（仕様書5.6） |
    | 音が**ネイティブと一致**するか | 経路が正しいこと。1ブロック遅れも合わせて見ます |
    | レイテンシの申告 | プラグインの遅れ＋1ブロック（PDCが揃えるもの） |
    | 状態の往復 | 保存→復元でプラグインの設定が戻ること |
    | **子を殺しても親が生きているか** | サンドボックスの目的そのもの |

    **問題の数を終了コードで返します**（0なら合格）。
*/
namespace SandboxSelfTest
{
    /** コマンドラインに`--sandbox-selftest`があれば走らせて、trueを返す。 */
    bool runIfRequested (const juce::String& commandLine);

    /** 8.262：`--sandbox-editor-test`があれば走らせて、trueを返す（Phase 269）。

        ```
        "Manta Studio.exe" --sandbox-editor-test
        "Manta Studio.exe" --sandbox-editor-test "C:\path\to\Some.vst3"
        ```

        **こちらは窓を出します**——GUIの話なので、出さずには確かめられません。
        見るのは絵ではなく**繋がりの事実**です（別プロセスの窓が本当に
        こちらの窓の中にあるか、落ちた後も描き続けられるか）。
        絵は`sandbox-editor.png`に落ちます。 */
    bool runEditorTestIfRequested (const juce::String& commandLine);
}
