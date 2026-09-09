#pragma once

#include "ProjectIds.h"
#include "AutomationModel.h"   // 仕様書5.3.3：CCの繋ぎ方もAutomationCurveを使う（Phase 76）

#include <vector> // 仕様書5.3.2：チョークグループの適用（applyChokeGroups）

//==============================================================================
/**
    仕様書5.3.2・設計書1.3のDrumMapEntry。ドラムマップの1行ぶん。

    設計書1.3では`{midiNote, partName, muteGroup}`の3つだが、
    仕様書5.3.2が求める「行ごとのミュート」を入れるため`muted`を足してある。
*/
class DrumMapEntry
{
public:
    explicit DrumMapEntry (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    int getMidiNote() const;
    juce::String getPartName() const;

    /** 仕様書5.3.2：パート名を変える（Phase 68）。**空の名前は無視する。** */
    void setPartName (const juce::String& newName, juce::UndoManager* undoManager);

    /** 仕様書5.3.2：チョークグループ。0なら「グループなし」。

        同じ番号どうしでは、**後から鳴ったノートが前のノートを止める**
        （クローズドハイハットがオープンハイハットを切る、という挙動）。 */
    int getMuteGroup() const;
    void setMuteGroup (int newGroup, juce::UndoManager* undoManager);

    /** 仕様書5.3.2：行ごとのミュート。trueならこの音は鳴らないし書き出されもしない。 */
    bool isMuted() const;
    void setMuted (bool shouldBeMuted, juce::UndoManager* undoManager);
};

//==============================================================================
/**
    仕様書5.3.2・設計書1.3のDrumMap。MIDIノート番号とパート名の対応表。

    プロジェクト直下の`<DRUMMAPS>`に置き、トラックがIDで参照する。
    （検討事項）ユーザー定義マップの管理方法は仕様書10.7で継続検討。
    現状はGeneral MIDI準拠のマップを1つ自動生成し、行ごとのミュートと
    チョークグループだけを編集できる形にしている。
*/
class DrumMap
{
public:
    explicit DrumMap (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    juce::String getId() const;
    juce::String getName() const;

    int getNumEntries() const;
    DrumMapEntry getEntry (int index) const;

    /** MIDIノート番号で引く。無ければ`state.isValid()`がfalseのものを返す。 */
    DrumMapEntry findEntry (int midiNote) const;

    DrumMapEntry addEntry (int midiNote, const juce::String& partName, int muteGroup,
                           juce::UndoManager* undoManager);

    /** 仕様書5.3.2：その音がミュートされているか。マップに無い音はミュートしない。 */
    bool isNoteMuted (int midiNote) const;

    /** その音のチョークグループ（0＝なし）。 */
    int getMuteGroupForNote (int midiNote) const;
};

//==============================================================================
/**
    仕様書5.3.2：チョークグループの適用。

    同じグループ内で、**後から鳴ったノートが前のノートを止める**。
    `endTime`を書き換えるだけなので、呼び出し側の並び順は問わない。

    **再生（MidiPlayerProcessor）とMIDI書き出し（MidiFileExporter）の両方が使う。**
    片方だけに書くと「鳴っている内容と書き出した内容が違う」という
    気づきにくい食い違いになる（HANDOVER 1.14・8.2）。
*/
struct ChokeNote
{
    int muteGroup = 0;
    double startTime = 0.0;
    double endTime = 0.0;
};

void applyChokeGroups (std::vector<ChokeNote>& notes);

//==============================================================================
/**
    仕様書5.3.4・設計書1.3のGroovePoint。グリッド1マスぶんの「揺れ」を表す。

    `gridPosition`はパターンの中でのマス番号（0から始まる）。
    `timingOffsetMs`はそのマスのグリッド位置からのズレ、
    `velocityScale`はクリップの平均ベロシティに対する倍率。
*/
class GroovePoint
{
public:
    explicit GroovePoint (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    int getGridPosition() const;

    /** 8.135：**マス何個ぶんのズレか**（Phase 173／8.105の宿題2）。

        0.1なら「1マスの1割ぶん後ろ」。**テンポにもグリッドの細かさにも依らない**ので、
        どのテンポの曲へ当てても揺れ具合が変わりません。

        **古いプロジェクトも読めます**：この値が入っていなければ、
        `timingOffsetMs`と抽出時のテンポから換算して返します（`GrooveTemplate`側）。 */
    double getTimingOffsetGrids() const;

    /** 設計書1.3のミリ秒表記。**新しく書くのは`timingOffsetGrids`のほう**で、
        こちらは古いプロジェクトを読むためだけに残してあります。 */
    double getTimingOffsetMs() const;

    float getVelocityScale() const;
};

//==============================================================================
/**
    仕様書5.3.4・設計書1.3のGrooveTemplate。

    基準クリップから抜き出した「グリッドからのタイミング・ベロシティのズレ」を、
    パターン1周ぶん保持する。プロジェクト直下の`<GROOVETEMPLATES>`に置くので、
    **一度作れば他のクリップへ何度でも適用できる**（仕様書5.3.4の「保存・再利用」）。

    8.135：**ズレは「マス何個ぶん」で持ちます**（Phase 173／8.105の宿題2）。

    設計書1.3はミリ秒で持つとしていました。ミリ秒のまま別のテンポへ当てると
    揺れ具合が変わる（120BPMで抽出したハネを140BPMで使うと浅くなる）ので、
    Phase 172までは**抽出時のテンポを覚えておいて、グリッド間隔の比で伸縮**
    させていました。

    **マスの割合で持つと、その計算ごと要らなくなります。**
    「1マスの1割ぶん後ろ」は、どのテンポでも・どの細かさでも同じ意味だからです。
    曲の途中でテンポが変わっても、**そのマスのテンポで自動的に正しくなります**。

    `sourceTempo`は**古いプロジェクトを読むためだけ**に残してあります。
*/
class GrooveTemplate
{
public:
    explicit GrooveTemplate (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    juce::String getId() const;

    juce::String getName() const;
    void setName (const juce::String& newName, juce::UndoManager* undoManager);

    /** 1拍を何分割したグリッドで抽出したか（4なら16分音符相当）。 */
    int getGridDivision() const;

    /** パターン1周のマス数（1小節ぶんなら「拍数 × 分割数」）。 */
    int getPatternLength() const;

    /** 抽出したときのプロジェクトのテンポ（BPM）。

        8.135：**新しく作ったテンプレートでは使いません**（Phase 173／8.105の宿題2）。
        ズレを「マス何個ぶん」で持つようになったので、**当てる側はテンポを見ません**。
        古いプロジェクト（ミリ秒で保存されたもの）を読むときだけ要ります。 */
    double getSourceTempo() const;

    int getNumPoints() const;
    GroovePoint getPoint (int index) const;

    /** パターン内のマス番号で引く。無ければ`state.isValid()`がfalseのものを返す。 */
    GroovePoint findPoint (int gridPosition) const;

    /** 点を追加する（同じマスが既にあれば置き換える）。

        8.135：**ズレは「マス何個ぶん」で渡すこと**（Phase 173／8.105の宿題2）。
        互換のため`timingOffsetMs`も一緒に書きます——**古い版で開いても
        だいたい合った揺れになる**ようにするためです。 */
    GroovePoint addPoint (int gridPosition, double timingOffsetGrids, float velocityScale,
                          juce::UndoManager* undoManager);

    /** 8.135：この点のズレを「マス何個ぶん」で返す（Phase 173）。

        **古いプロジェクトの換算はここ1箇所**です。`timingOffsetGrids`が
        入っていなければ、`timingOffsetMs`と`sourceTempo`・`gridDivision`から
        計算して返します。呼び出し側は違いを知らずに済みます（1.27）。 */
    double getPointOffsetGrids (const GroovePoint& point) const;

    bool isEmpty() const { return getNumPoints() == 0; }
};

//==============================================================================
/**
    仕様書5.3.3：CCレーンで扱うコントローラーの種類。

    設計書1.3は`controllerNumber`について「0-127。ピッチベンド／アフタータッチは
    特殊値で表現」としている。ここではMIDIのCC番号（0〜127）をそのまま使い、
    **128以降を特殊値**として割り当てる。こうすると、レーンの識別子・保存形式・
    描画のいずれも「1つのint」で統一でき、種類ごとに分岐を増やさずに済む。

    値の範囲は種類によって違う（CCとアフタータッチは0〜127、ピッチベンドは0〜16383）ため、
    範囲を知りたいときは必ず`getMaxValue()`を通すこと。**127で決め打ちすると
    ピッチベンドだけ分解能が落ちる。**
*/
namespace MidiControllers
{
    constexpr int pitchBend       = 128; // ピッチベンド（0〜16383、中央8192）
    constexpr int channelPressure = 129; // チャンネルアフタータッチ（0〜127）

    /** レーンの見出しに出す名前（"CC11 Expression" など）。 */
    juce::String getDisplayName (int controllerNumber);

    /** その種類が取り得る最大値。描画・編集の正規化に使う。 */
    int getMaxValue (int controllerNumber);

    /** レーンを新しく作ったときの初期値（ピッチベンドだけ中央、他は0）。 */
    int getDefaultValue (int controllerNumber);

    /** よく使うものをレーン追加メニューに並べるための一覧。 */
    juce::Array<int> getCommonControllers();
}

//==============================================================================
/**
    仕様書5.3.3・設計書1.3のCCEventに対応する、ValueTreeの薄いラッパークラス。

    **時刻はノートと同じく「クリップの中身の先頭」からの相対値**（HANDOVER 1.14）。
    クリップの表示位置からではないので、タイムライン上の時刻を求めるときは
    必ず`MidiClip::contentTimeToTimeline()`を通すこと。
*/
class CCEvent
{
public:
    explicit CCEvent (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    /** CC番号（0〜127）、またはMidiControllersの特殊値。 */
    int getControllerNumber() const;

    int getValue() const;
    void setValue (int newValue, juce::UndoManager* undoManager);

    /** 仕様書5.3.3：この点から**次の点まで**の繋ぎ方（Phase 76）。

        **オートメーションと同じ`AutomationCurve`を使います**（8.36）。
        Phase 75まではCCだけが階段状で固定でしたが、
        「点や線の書き方をオートメーションと揃える」ために種別を持たせました。
        **保存されていないものは直線**（Phase 75以前のプロジェクト）。 */
    AutomationCurve getCurve() const;
    void setCurve (AutomationCurve newCurve, juce::UndoManager* undoManager);

    /** 仕様書5.3.3：この区間の**曲がり具合**（Phase 77／8.37）。
        **オートメーションの点と同じ扱い**（-1.0〜+1.0で、0が直線）。 */
    float getCurveAmount() const;
    void setCurveAmount (float newAmount, juce::UndoManager* undoManager);

    double getTime() const;
    void setTime (double newTimeSeconds, juce::UndoManager* undoManager);

    /** 8.138：**保存されているのはこちら**（Phase 176／8.105の宿題3）。
        `Note::getStartBeats()`と同じ扱いです（`MusicalTime.h`）。 */
    double getTimeBeats() const;
    void setTimeBeats (double newTimeBeats, juce::UndoManager* undoManager);
};

//==============================================================================
/** 設計書1.3のNoteに対応する、ValueTreeの薄いラッパークラス。

    8.138：**時刻は拍で保存します**（Phase 176／8.105の宿題3）。

    ```
    Phase 175まで： startTime（秒）が保存されていて、拍は換算だった
    Phase 176から： startBeats（拍）が保存されていて、**秒のほうが換算**
    ```

    **秒の見た目は変えていません。** `getStartTime()`は今までどおり曲の時刻を
    秒で返します——変わったのは「どこから来た値か」だけです。だから
    秒を読んでいる234箇所は1つも変わっていません（`MusicalTime.h`）。

    これで**テンポを変えると音も一緒に動きます**。「3小節目の2拍目にある」という
    事実そのものを持つようになったためです。
*/
class Note
{
public:
    explicit Note (juce::ValueTree treeToWrap);

    /** 8.138：**拍で作ります**（Phase 176）。

        秒で受け取らないのは、ここで作るツリーが**まだどこにも繋がっていない**からです。
        繋がっていないと`MusicalTime`がテンポの表へ辿り着けず、
        **既定の120BPMで換算してしまいます**。拍なら換算が要りません。

        秒から作りたいときは`Track::addNote()`を通すこと（あちらは
        **トラックのツリー**で換算するので、正しい表が引けます）。 */
    static Note create (int pitch, int velocity, double startBeats,
                        double lengthBeats, juce::UndoManager* undoManager);

    juce::ValueTree state;

    int getPitch() const;
    int getVelocity() const;

    /** 曲の時刻（秒）。**保存されている拍から、そのときのテンポで換算した値**です。 */
    double getStartTime() const;

    /** 鳴っている長さ（秒）。**「終わりの拍 - 始まりの拍」を秒へ直したもの**なので、
        途中でテンポが変わるところをまたぐノートでも正しくなります。 */
    double getLength() const;

    void setPitch (int newPitch, juce::UndoManager* undoManager);
    void setVelocity (int newVelocity, juce::UndoManager* undoManager);
    void setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager);
    void setLength (double newLengthSeconds, juce::UndoManager* undoManager);

    //==========================================================================
    // 8.138：**保存されているのはこちら**（Phase 176／8.105の宿題3）
    //
    // 上の秒の側と**同じものを、別の単位で見ているだけ**です。
    // 換算しているのは秒の側で、こちらはプロパティをそのまま読みます。
    //
    // **テンポの表は自分で引き当てます**（`MusicalTime::findMapFor()`）。
    // 引数で受け取る形にすると`getStartTime()`の見た目が変わり、
    // 秒を読んでいる234箇所すべてを直すことになるためです（`MusicalTime.h`）。

    /** 曲の頭から何拍目に鳴り始めるか（小数）。 */
    double getStartBeats() const;

    /** 何拍ぶん鳴るか。

        **位置ではなく長さ**なので、「終わりの拍 - 始まりの拍」で求めます。
        テンポが途中で変われば、**同じ秒数でも拍数は変わります**。 */
    double getLengthBeats() const;

    void setStartBeats (double newStartBeats, juce::UndoManager* undoManager);

    /** 長さを拍で決める。**始まりは動きません**（終わりだけが動きます）。 */
    void setLengthBeats (double newLengthBeats, juce::UndoManager* undoManager);
};

//==============================================================================
/** 設計書1.3のMidiClipに対応する、ValueTreeの薄いラッパークラス。 */
class MidiClip
{
public:
    explicit MidiClip (juce::ValueTree treeToWrap);

    static MidiClip create (double startTimeSeconds, double lengthSeconds, juce::UndoManager* undoManager);

    juce::ValueTree state;

    juce::String getId() const;
    double getStartTime() const;
    double getLength() const;

    /** 仕様書5.5：クリップの左端が「中身のどこから」始まるか（設計書1.3のClip.offset）。

        **ノートの時刻は、クリップの表示位置ではなく「中身の先頭」からの相対値**で持つ。
        オフセットはその中身のうち、どこからを窓として見せるかを表す。

            タイムライン上の発音時刻 = getStartTime() + (ノートの時刻 - getOffset())
            鳴る条件                 = getOffset() <= ノートの時刻 < getOffset() + getLength()

        こうしておくと、左端をトリムしても中のノートは動かない
        （オーディオクリップがソースファイル内の再生開始位置を持つのと同じ考え方）。
        Phase 22より前のクリップはオフセットを持たないが、既定値0で従来どおりに動く。 */
    double getOffset() const;
    void setOffset (double newOffsetSeconds, juce::UndoManager* undoManager);

    void setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager);
    void setLength (double newLengthSeconds, juce::UndoManager* undoManager);

    /** 「中身の先頭からの時刻」を「タイムライン上の時刻」へ直す。

        窓（オフセット〜オフセット+長さ）の外なら**falseを返す**。つまり
        「鳴るかどうか」の判定と座標変換をまとめて行う。

        **この判定を自分で書かないこと。** 同じ計算は再生・書き出し・描画のそれぞれに
        必要で、散らばると必ずずれる（HANDOVER 1.14・8.2）。ずれると
        「画面では切れているのに鳴る」「鳴っているのに書き出されない」という
        食い違いになり、原因が非常に追いにくい。 */
    bool contentTimeToTimeline (double contentTimeSeconds, double& timelineTimeOut) const;

    Note addNote (int pitch, int velocity, double startTimeSeconds,
                  double lengthSeconds, juce::UndoManager* undoManager);
    void removeNote (const Note& note, juce::UndoManager* undoManager);

    int getNumNotes() const;
    Note getNote (int index) const;

    /** 8.77：**中のノートをまとめて上下させる**（Phase 117／改善案㉝。仕様書5.3）。

        `semitones`は半音の数（正で上、負で下）。

        ### 端に当たったら、全部動かしません

        1つでもMIDIの範囲（0〜127）を外れるなら**何もせずfalseを返します**。
        外れるものだけ止めると、**和音の形が崩れます**——12半音上げて戻したときに
        元へ戻らない、という直しようのない壊れ方になります。

        「どこまで上げられるか」を呼び出し側で計算しないこと。
        同じ判定が散らばります（8.2）。 */
    bool transposeNotes (int semitones, juce::UndoManager* undoManager);

    /** クリップの長さを、中のノートが全部収まる長さまで伸ばす（縮めることはしない）。

        ノートの位置は中身の先頭からの相対値なので、クリップの窓の外へノートを置くこと自体は
        データ上できてしまう。そのままにすると**アレンジ画面に描かれるクリップの長さと、
        実際に鳴る範囲が食い違う**（クリップは短いのに音は続く）。
        ノートを追加・移動・伸縮した後に呼んで、この食い違いを防ぐこと。

        **オフセット（＝左端のトリム）は動かさない。** 伸ばすのは右端だけなので、
        トリムして隠したノートが、後からノートを足したせいで復活することはない。

        長さが変わった場合にtrueを返す。 */
    bool growToFitNotes (juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.3.3：CCレーン（Phase 23）
    //
    // イベントは**時刻の昇順**で並んでいることを前提にしている（オートメーションレーンと
    // 同じ考え方）。順序が崩れると`getCCValueAt()`の解釈が意味を成さなくなるため、
    // 追加は正しい位置へ挿し、移動後は並べ替えること。

    int getNumCCEvents() const;
    CCEvent getCCEvent (int index) const;

    /** 指定コントローラーのイベント数（レーンの描画で使う）。 */
    int getNumCCEventsFor (int controllerNumber) const;

    /** 指定コントローラーのn番目のイベント（時刻順）。 */
    CCEvent getCCEventFor (int controllerNumber, int index) const;

    /** 仕様書5.3.3：同じコントローラーの「次の点」を返す（Phase 76）。

        **繋ぎ方（カーブ）を効かせるのに要ります。** 無ければ
        `state.isValid()`がfalseのものを返します。 */
    CCEvent findNextCCEvent (const CCEvent& event) const;

    /** イベントを1つ追加する（時刻順の正しい位置へ挿す）。

        **同じコントローラーの同じ時刻に既にイベントがあれば、値を上書きする。**
        こうしないと、同じ場所を何度もクリックしたときにイベントが積み重なり、
        どれが効いているのか分からなくなる。 */
    CCEvent addCCEvent (int controllerNumber, int value, double timeSeconds,
                        juce::UndoManager* undoManager);

    void removeCCEvent (const CCEvent& event, juce::UndoManager* undoManager);

    /** 指定コントローラーのイベントをすべて消す（レーンを削除するときに使う）。 */
    void removeAllCCEventsFor (int controllerNumber, juce::UndoManager* undoManager);

    /** イベントを時刻順に並べ替える（ドラッグで前後関係が入れ替わった後に呼ぶ）。 */
    void sortCCEvents (juce::UndoManager* undoManager);

    /** 指定時刻でのコントローラーの値。

        **補間はしない（ステップ状に保持する）。** MIDIのCCは「送られた値が次に
        送られるまで保たれる」ものなので、間を滑らかに繋ぐと実際の挙動と食い違う。
        最初のイベントより前は、そのコントローラーの既定値を返す。 */
    int getCCValueAt (int controllerNumber, double contentTimeSeconds) const;

    //==========================================================================
    // 仕様書5.3.3：表示するレーンの一覧（追加・並べ替え・表示/非表示）
    //
    // 「どのレーンを出すか」はクリップごとに持つ。CCイベント自体がクリップに
    // 属しているので、一緒に保存・復元されるほうが自然なため。

    juce::ValueTree getOrCreateCCLanesNode (juce::UndoManager* undoManager);

    int getNumCCLanes() const;

    /** n番目のレーンが担当するコントローラー番号。 */
    int getCCLaneController (int index) const;

    bool isCCLaneVisible (int index) const;
    void setCCLaneVisible (int index, bool shouldBeVisible, juce::UndoManager* undoManager);

    /** レーンを追加する（既にあれば何もしない）。追加後のレーン番号を返す。 */
    int addCCLane (int controllerNumber, juce::UndoManager* undoManager);

    void removeCCLane (int index, juce::UndoManager* undoManager);

    /** レーンの並びを入れ替える（仕様書5.3.3の「並べ替え」）。 */
    void moveCCLane (int fromIndex, int toIndex, juce::UndoManager* undoManager);

    /** 既にCCイベントを持っているのにレーンが1つも無い場合に、
        イベントの種類ぶんのレーンを作る。

        これが無いと、**データはあるのに画面に何も出ない**という状態になる
        （Phase 23より前に作られたクリップや、外部から入ったデータで起こり得る）。
        クリップを編集対象にしたときに呼ぶこと。 */
    void ensureCCLanesForExistingEvents (juce::UndoManager* undoManager);

    //==========================================================================
    /** 仕様書5.3.4・設計書1.3：このクリップへ最後に適用したグルーヴテンプレートのID。

        **記録するだけで、再生時に効くわけではない。** グルーヴはノートの時刻と
        ベロシティを実際に書き換える「1回きりの編集」で、オートメーションのように
        再生のたびに適用されるものではない。ここに残しているのは、
        インスペクタで「どのグルーヴを当てたか」を見せるため。 */
    juce::String getGrooveTemplateId() const;
    void setGrooveTemplateId (const juce::String& templateId, juce::UndoManager* undoManager);
};
