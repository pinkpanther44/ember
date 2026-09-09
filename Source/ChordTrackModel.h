#pragma once

#include "ProjectIds.h"
#include "ChordModel.h"

//==============================================================================
// 仕様書5.2.3のコードトラックと、設計書1.3の`ChordRegion`。
//
// **`ChordModel.h`とは分けてある。** あちらは juce_core とSTLだけに依存する
// 「純粋な計算」で、`Tools\ChordEngineTests`がJUCEを起動せずに値を確かめられるのは
// そのおかげ（HANDOVER 8.5）。ValueTreeを持ち込むとその性質が壊れるので、
// **ValueTreeに触るものはこちら側に置く**こと。
//
//   ChordModel.h      … Chord・Scale（juce_coreのみ。テストしやすい）
//   ChordEngine.h     … スコアリング・ボイシング（同上）
//   ChordTrackModel.h … ChordRegion（このファイル。ValueTreeに載る）
//==============================================================================

//==============================================================================
/** 仕様書5.9のマーカー（Phase 49）。タイムライン上の「目印」。

    `AudioClip`／`ChordRegion`と同じ、ValueTreeの薄いラッパー（HANDOVER 3.2）。

    8.139：**位置は拍で保存します**（Phase 177／8.105の宿題3）。
    `getTime()`は今までどおり秒を返しますが、**その値は換算で出てきます**
    （ノートと同じ形。8.138）。テンポを変えると、マーカーも一緒に動きます。

    **コードとは関係ないが、ここに置いている。** `ProjectModel.h`のumbrellaに
    含まれる小さな値型の置き場所が他に無く、`ProjectModel.h`本体は
    トラックとプロジェクトのためのファイルだから。置き場所を分けるなら、
    このファイルを`SmallModels.h`のような名前へ変えるのが素直。
*/
class Marker
{
public:
    explicit Marker (juce::ValueTree treeToWrap);

    /** 8.139：**拍で作ります**（Phase 177）。`Note::create()`と同じ理由で、
        出来たてのツリーは繋がっておらずテンポの表を引けないためです（8.138）。
        秒から作りたいときは`ProjectModel::addMarker()`を通すこと。 */
    static Marker create (double timeBeats, const juce::String& name,
                          juce::UndoManager* undoManager);

    juce::ValueTree state;

    juce::String getId() const;

    /** 曲の時刻（秒）。**保存されている拍から換算した値**です。 */
    double getTime() const;
    void setTime (double newTimeSeconds, juce::UndoManager* undoManager);

    /** 8.139：**保存されているのはこちら**（Phase 177）。 */
    double getTimeBeats() const;
    void setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager);

    juce::String getName() const;
    void setName (const juce::String& newName, juce::UndoManager* undoManager);
};

//==============================================================================
/** 設計書1.3のChordRegion。コードトラックに置く「コード区間」。

    `AudioClip`／`MidiClip`と同じ、ValueTreeの薄いラッパー（HANDOVER 3.2）。

    8.139：**位置と長さは拍で保存します**（Phase 177／8.105の宿題3）。
    `getStartTime()`は今までどおり秒を返しますが、**その値は換算で出てきます**
    （ノートと同じ形。8.138）。テンポを変えると、コード区間も一緒に動きます。

    **オーディオクリップだけが秒のまま**です（伸び縮みできないため。8.105のPhase 180）。
*/
class ChordRegion
{
public:
    explicit ChordRegion (juce::ValueTree treeToWrap);

    /** 新しい区間用のValueTreeを作って返す（まだ親には追加されていない）。

        8.139：**拍で作ります**（Phase 177）。`Note::create()`と同じ理由で、
        出来たてのツリーは繋がっておらずテンポの表を引けないためです（8.138）。
        秒から作りたいときは`Track::addChordRegion()`を通すこと。 */
    static ChordRegion create (const Chord& chord, double startBeats,
                               double lengthBeats, juce::UndoManager* undoManager);

    juce::ValueTree state;

    juce::String getId() const;

    /** 曲の時刻（秒）。8.139：**保存されている拍から換算した値**です（Phase 177）。 */
    double getStartTime() const;
    double getLength() const;
    double getEndTime() const { return getStartTime() + getLength(); }

    void setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager);
    void setLength (double newLengthSeconds, juce::UndoManager* undoManager);

    /** 8.139：**保存されているのはこちら**（Phase 177／8.105の宿題3）。
        `Note::getStartBeats()`と同じ扱いです（`MusicalTime.h`）。 */
    double getStartBeats() const;
    double getLengthBeats() const;

    void setStartBeats (double newStartBeats, juce::UndoManager* undoManager);
    void setLengthBeats (double newLengthBeats, juce::UndoManager* undoManager);

    /** 区間が持つコード。

        **保存されているコードタイプが範囲外なら`Maj`として読みます。**
        壊れたファイルや、将来タイプを増やしたあとのプロジェクトを古い版で開いた
        場合に、未定義の値でテーブルを引かないようにするためです（HANDOVER 1.17と
        同じ考え方で、読めなかったものは黙って飛ばさず、無難な値に倒す）。 */
    Chord getChord() const;
    void setChord (const Chord& newChord, juce::UndoManager* undoManager);
};
