#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
/**
    設計書2.3.8「環境設定」のGeneralカテゴリ：**保存先フォルダの設定**（Phase 57／8.1のB3）。

    ### なぜ1箇所に集めたか

    Phase 56まで、保存先は**使う側がその場で組み立てていました**。
    `AutoSaveManager`は`%APPDATA%\PersonalDAW`を、`AudioEngine`は
    `ドキュメント\PersonalDAW Recordings`を、それぞれ自前で作っていて、
    **設定から引く口がありませんでした。**
    スナップ（8.14）と同じ形で、値と既定値をここへ集めています。

    ### 保存先を「移さない」ものがある

    次の3つは**この設定の対象外**で、`%APPDATA%\PersonalDAW`に固定です。

    | ファイル | なぜ動かせないか |
    |---|---|
    | `settings.xml`（`AppSettings`） | **この設定自体がそこに入っている。** 動かすと、次の起動で読む場所が分からない |
    | `crash_marker.txt`（`PluginCrashTracker`） | 「前回きちんと終了したか」の目印。ユーザーが触る場所に置くものではない |
    | プラグインのクラッシュ履歴 | 同上 |

    「バックアップ」が指すのは**オートセーブのファイルだけ**です（設計書5.1）。

    ### 存在しない場所を設定してしまったとき

    フォルダは、外付けドライブやネットワークドライブを指したまま
    次の起動で消えていることがあります。**そのときは黙って既定へ落とします**
    （`getFolder()`）。保存に失敗して初めて気づく、という形にしないためです。
*/
namespace StorageLocations
{
    /** 設定できる保存先。**追加するときは`getDefaultFolder()`と`getSettingsKey()`の
        両方を必ず埋めること**（片方だけだと、既定へ落ち続けるか、保存されないかになる）。 */
    enum class Kind
    {
        projects,    // プロジェクトファイル（開く／名前を付けて保存の初期フォルダ）
        backups,     // オートセーブ（設計書5.1のバックアップ）
        recordings,  // 録音した音声ファイル（仕様書5.4）
        templates    // プロジェクトテンプレート（仕様書10.3。**8.1のD8まで未使用**）
    };

    /** 設定していないときに使う場所。**フォルダは作りません**（読むだけの用途もあるため）。 */
    juce::File getDefaultFolder (Kind kind);

    /** いま設定されている場所。

        設定が無い場合と、**設定されたフォルダがもう存在しない場合**は既定を返します。 */
    juce::File getFolder (Kind kind);

    /** 保存先として使える状態にして返す（無ければ作る）。

        **作れなかった場合は既定を返します。** 書き込めない場所を設定したまま
        録音やオートセーブが黙って失敗する、という形にしないため。 */
    juce::File getFolderForWriting (Kind kind);

    /** 設定する。**存在しないフォルダを渡すと作ります。**
        `juce::File()`（空）を渡すと既定へ戻します。 */
    void setFolder (Kind kind, const juce::File& folder);

    /** いま既定のままか（環境設定で「既定に戻す」を出し分けるのに使う）。 */
    bool isUsingDefault (Kind kind);

    /** 環境設定に出す見出し。 */
    juce::String getDisplayName (Kind kind);

    /** 環境設定に出す補足（何がそこへ入るか）。 */
    juce::String getDescription (Kind kind);
}
