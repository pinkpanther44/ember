#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <vector>

class ProjectModel;

//==============================================================================
/**
    8.151：**プロジェクトテンプレート**（Phase 189／8.1のD8。仕様書10.3）。

    ### テンプレートとは、ただのプロジェクトファイル

    中身は`.ms1`そのままです。**専用の形式を作っていません**——
    形式が2つあると、保存の版が上がるたびに両方を変換することになり、
    片方を忘れます（8.138の変換は1本道であってほしい）。

    テンプレートから作ったプロジェクトは、**保存先だけ未定**になります。
    そのまま保存すると「名前を付けて保存」に回るので、
    **元のテンプレートを上書きすることがありません**（D8の「保存先だけ未定にする」）。

    ### 2種類ある

    | 種類 | どこから来るか | 消せるか |
    |---|---|---|
    | 組み込み | **その場で組み立てる**（ファイルは無い） | できない |
    | ユーザー | `StorageLocations::Kind::templates`のフォルダ | できる |

    組み込みをファイルとして配らないのは、**保存の版が上がったときに置いていかれるから**です。
    コードで組み立てていれば、`createNewProject()`が今の形を作ります（8.138）。

    ### 置き場所は自分で決めない

    フォルダは`StorageLocations`から引きます（8.17）。
    Phase 57で口だけ用意して、**ここが最初の利用者**です。
*/
namespace ProjectTemplates
{
    /** 組み込みテンプレート。**番号は保存していない**ので、並べ替えても壊れません。 */
    enum class BuiltIn
    {
        empty,          // トラック無し（「新規プロジェクト」と同じ）
        midi10,         // MIDIトラック10本
        midi10Audio10   // MIDIトラック10本＋オーディオトラック10本
    };

    struct Entry
    {
        juce::String name;
        juce::String description;

        /** ユーザーのテンプレートならファイル。組み込みでは空。 */
        juce::File file;

        /** `file`が空のときだけ意味を持つ。 */
        BuiltIn builtIn = BuiltIn::empty;

        bool isBuiltIn() const { return file == juce::File(); }
    };

    /** 置き場所（`StorageLocations::Kind::templates`）。 */
    juce::File getFolder();

    /** 一覧。**組み込みが先、ユーザーのものが名前順で後**に並びます。 */
    std::vector<Entry> getAll();

    /** テンプレートを`project`へ適用する。返り値が空なら成功、そうでなければエラー文。

        **呼んだ側は、この後に`AudioEngine::restorePluginsFromProject()`と
        画面の作り直しを必ず行うこと**（プロジェクトのルートが差し替わっています）。 */
    juce::String apply (ProjectModel& project, const Entry& entry);

    /** いまのプロジェクトをテンプレートとして保存する。返り値が空なら成功。

        **プラグインの内部状態は呼ぶ側で取り込んでおくこと**
        （`AudioEngine::capturePluginStatesIntoProject()`。設計書3.8）。 */
    juce::String saveCurrentAsTemplate (const ProjectModel& project, const juce::String& name);

    /** ユーザーのテンプレートを消す（組み込みには効きません）。 */
    bool remove (const Entry& entry);
}
