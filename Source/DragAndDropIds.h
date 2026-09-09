#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    仕様書4.4・6章「ドラッグ&ドロップ中心の操作」で受け渡す情報の形式（Phase 21）。

    JUCEのドラッグ&ドロップは`juce::var`を1つ運ぶだけなので、
    「何をドラッグしているか」は文字列の約束事で表す。
    その約束を**送り手と受け手の両方から参照できる1箇所**に置いておかないと、
    片方だけ書式を変えたときに、ビルドは通るのに何も起きない不具合になる。

    プラグインは識別子（`PluginDescription::createIdentifierString()`）だけを運び、
    受け手側でスキャン済み一覧から引き直す。`PluginDescription`そのものを
    varへ詰めるより素直で、受け手が「まだスキャンされていない」状況にも気づける。
*/
namespace DragAndDropIds
{
    //==========================================================================
    // ブラウザパネルのプラグイン一覧から（仕様書4.4）

    juce::String makePluginDescription (const juce::PluginDescription& description);
    bool isPluginDrag (const juce::var& description);

    /** ドラッグ中のプラグインの識別子。プラグインのドラッグでなければ空文字。 */
    juce::String getPluginIdentifier (const juce::var& description);

    /** スキャン済み一覧から、識別子に一致するものを探す。
        見つからなければfalse（スキャンし直された後などに起こり得る）。 */
    bool findPluginByIdentifier (const juce::Array<juce::PluginDescription>& knownPlugins,
                                  const juce::String& identifier,
                                  juce::PluginDescription& descriptionOut);

    //==========================================================================
    // ブラウザパネルのファイルツリーから（仕様書4.4）

    juce::String makeFileDescription (const juce::File& file);
    bool isFileDrag (const juce::var& description);
    juce::File getFile (const juce::var& description);

    //==========================================================================
    // 8.66：ラックのインサート／センドを**箱として**ドラッグする
    //       （Phase 104・105／改善案㉘㉙＋D7）
    //
    // **並べ替えとコピーを1つの形式でまかなう。** どちらも運ぶ情報は同じ
    //   「どのトラックの、何番目のスロットか」
    // で、**落とした先が同じトラックなら並べ替え、違うトラックならコピー**という
    // 使い分けは受け手（`TrackRackComponent`）が決める。
    //
    // **番号で運ぶことに注意。** スロットそのものへの参照（ValueTree）を運ばないのは、
    // ドラッグ中に別の画面からスロットが消される可能性があるため。
    // 受け手は落とされた時点でもう一度番号を確かめてから動かす。

    juce::String makeInsertDescription (const juce::String& trackId, int insertIndex);
    bool isInsertDrag (const juce::var& description);

    /** ドラッグ元のトラックID。インサートのドラッグでなければ空文字。 */
    juce::String getInsertSourceTrackId (const juce::var& description);

    /** ドラッグ元のスロット番号。インサートのドラッグでなければ-1。 */
    int getInsertIndex (const juce::var& description);

    /** 8.66：センドも同じ形式で運ぶ（Phase 105）。

        **インサートとは別の接頭辞にしてあります。** 同じにすると、
        センドをインサートの位置へ落としたときに黙って挿さってしまいます
        （受け手はどちらのドラッグかを接頭辞だけで見分けている）。 */
    juce::String makeSendDescription (const juce::String& trackId, int sendIndex);
    bool isSendDrag (const juce::var& description);
    juce::String getSendSourceTrackId (const juce::var& description);
    int getSendIndex (const juce::var& description);

    //==========================================================================
    // 8.67：Consoleのストリップを**掴んで並べ替える**（Phase 106／改善案㉛）

    /** ドラッグしているトラックそのもの（並べ替え用）。

        **番号ではなくIDで運びます。** アレンジ画面の並べ替え（Phase 34・36）は
        画面の中だけで完結するので番号で足りていましたが、
        Consoleは**トラックの一部しか並べない**ので（コードトラックは出ない、
        VCAは出る）、番号だと画面ごとに意味が変わってしまいます。 */
    juce::String makeTrackDescription (const juce::String& trackId);
    bool isTrackDrag (const juce::var& description);
    juce::String getTrackId (const juce::var& description);
}
