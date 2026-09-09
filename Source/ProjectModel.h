#pragma once

#include "Branding.h"        // 8.175：プロジェクトの拡張子（Phase 216）
#include "ProjectIds.h"
#include "AutomationModel.h"  // 仕様書5.6：オートメーション（AutomationLane等）
#include "MidiEditModel.h"    // 仕様書5.3.x：MIDIクリップ・CC・ドラムマップ・グルーヴ
#include "ChordModel.h"       // 仕様書5.11.1：コード・スケール（ChordCanvas移植）
#include "ChordTrackModel.h"  // 仕様書5.2.3：コードトラックのコード区間（ChordRegion）
#include "SnapGrid.h"         // 仕様書5.5・5.9：編集の刻み（スナップ）
#include "TempoMap.h"         // 仕様書5.1・5.9：テンポと拍子の変化点（Phase 140）
#include "MusicalTime.h"      // 8.137：ノードからテンポの表を引き当てる（Phase 175）
#include "KeyMap.h"           // 仕様書5.11.1：キーの変化点（Phase 143）
#include "WarpMap.h"          // 8.150：ソース↔クリップの対応表（Phase 188／8.48）

// PluginDescriptionは参照渡しでしか使わないため、前方宣言で足りる。
// データモデル層（設計書1.1）に、オーディオ処理モジュールへの依存を持ち込まないため。
namespace juce { class PluginDescription; }

//==============================================================================
// Phase 26でファイルを分割した。それ以前はこのヘッダ1つに
// 「ValueTreeの識別子・オートメーション・MIDI編集・トラック・プロジェクト」の
// すべてが入っており、1,200行を超えて目的の型を探すのが大変だった。
//
//   ProjectIds.h      … ValueTreeのノード種別・プロパティ名（設計書1.4）
//   AutomationModel.h … 仕様書5.6のオートメーション
//   MidiEditModel.h   … 仕様書5.3.xのMIDI編集（クリップ・CC・ドラム・グルーヴ）
//   ChordModel.h      … 仕様書5.11.1のコード・スケール（ChordCanvas移植）
//   ChordTrackModel.h … 仕様書5.2.3のコード区間（ChordRegion）
//   SnapGrid.h        … 仕様書5.5・5.9の編集の刻み（スナップ）
//   ProjectModel.h    … トラックとプロジェクト本体（このファイル）
//
// **このヘッダをincludeすれば全部入る**ので、既存の`#include "ProjectModel.h"`は
// そのままで動く。新しく書くコードでは、必要なものだけを直接includeしてもよい。
//==============================================================================

//==============================================================================
/** 仕様書5.2・設計書1.3で定義した6種のトラック種別。 */
enum class TrackType
{
    Audio,
    Midi,
    Send,
    Folder,
    Chord,
    VCA,

    /** 8.143：**マルチアウト音源の出力を受ける行**（Phase 181／改善案⑮）。

        ドラム音源はキック・スネア・ハットを**別々の出力バス**へ出せます。
        その1本を受け取って、**自分のフェーダー・インサート・センド・
        オートメーション**を持つのがこの種別です。

        **見た目と扱いはセンドトラックとほぼ同じ**です（クリップは置けない、
        音は外から入ってくる、行の地を塗って見分ける）。違うのは
        **音の入口が「他トラックからの送り」ではなく「音源の出力バス」**という点だけ。

        どこから受けるかは`drumOutSourceTrackId`と`drumOutSourceBus`が持ちます。 */
    DrumOut
};

juce::String trackTypeToString (TrackType type);
TrackType trackTypeFromString (const juce::String& text);

//==============================================================================
/**
    8.142：**トラックの時間の基準**（Phase 180／8.105の宿題3）。

    ### なぜ選べるようにするのか

    **ワープがないので、オーディオは伸び縮みできません**（8.48は見送り中）。
    だから「テンポに追従する」の意味が、MIDIとは違います。

    | 基準 | 開始位置 | 長さ | 向いているもの |
    |---|---|---|---|
    | `musical` | **拍**で持つ＝テンポに追従 | 秒（伸びない） | ループ素材、拍に貼り付いていてほしいもの |
    | `linear` | **秒**で持つ＝動かない | 秒 | 録音した演奏、効果音、SE |

    **オーディオの既定は`linear`です。** `musical`にすると、テンポを変えたときに
    **開始位置だけ動いて長さは変わらない**ので、隣のクリップと重なり得ます。
    それでも選べるようにしているのは、**どちらが正しいかは曲によって違う**からです
    （8.12の「1つしか無い前提は必ず崩れる」）。

    ### 効くのはオーディオクリップの開始位置だけです

    ノート・CC・コード区間・マーカー・オートメーション点・ループ範囲は
    **常に拍**です（8.138・8.139）。選ぶ余地があるのはオーディオだけなので、
    **メニューに出すのもオーディオトラックだけ**にしています。
*/
enum class TimeBase
{
    musical,
    linear
};

juce::String timeBaseToString (TimeBase base);
TimeBase timeBaseFromString (const juce::String& text);

/** 種別ごとの既定。**オーディオだけが`linear`**です。 */
TimeBase defaultTimeBaseFor (TrackType type);

/** 仕様書5.5：クリップゲインの範囲（Phase 80／8.40）。

    **画面のドラッグもメニューの入力もこの範囲に収める。** 上げすぎると割れるだけ、
    下げすぎても無音と区別がつかないので、両方を1箇所で決めておく。 */
constexpr float minClipGainDb = -24.0f;
constexpr float maxClipGainDb = 24.0f;

/** 仕様書5.5：オートフェードの長さ（秒。Phase 86／8.46）。
    **切り口の「プツッ」が消える程度**で、聞こえるフェードにはしない。 */
constexpr double autoFadeSeconds = 0.01;

//==============================================================================
/** 設計書1.3のAudioClipに対応する、ValueTreeの薄いラッパークラス。 */
class AudioClip
{
public:
    explicit AudioClip (juce::ValueTree treeToWrap);

    static AudioClip create (const juce::String& sourceFilePath, double startTimeSeconds,
                              double lengthSeconds, juce::UndoManager* undoManager, double offsetSeconds = 0.0);

    juce::ValueTree state;

    juce::String getId() const;
    juce::String getSourceFilePath() const;
    /** 曲の時刻（秒）。8.142：**トラックの基準で振り分けます**（Phase 180）——
        `musical`なら拍から換算し、`linear`なら秒をそのまま返します。 */
    double getStartTime() const;

    /** 8.142：**`musical`のトラックで保存されている位置**（Phase 180）。

        `linear`のトラックでは入っていません。**位置を決めるのは`setStartTime()`**で、
        こちらは並べ替えや比較のように「換算せずに済ませたい」ところで使います。 */
    double getStartBeats() const;
    void setStartBeats (double newStartBeats, juce::UndoManager* undoManager);

    /** 鳴っている長さ（秒）。**基準にかかわらず秒です**——オーディオは
        伸び縮みできないので、テンポを変えても長さは変わりません（8.142）。 */
    double getLength() const;
    double getOffset() const;
    double getFadeInSeconds() const;
    double getFadeOutSeconds() const;

    void setStartTime (double newStartTimeSeconds, juce::UndoManager* undoManager);
    void setLength (double newLengthSeconds, juce::UndoManager* undoManager);
    void setOffset (double newOffsetSeconds, juce::UndoManager* undoManager);
    void setFadeInSeconds (double newFadeInSeconds, juce::UndoManager* undoManager);
    void setFadeOutSeconds (double newFadeOutSeconds, juce::UndoManager* undoManager);

    /** 仕様書5.5：**クリップ単位のゲイン**（Phase 80／8.40）。単位はdBで、0が原音。

        **フェードとは別もの**です（フェードは端の増減、これはクリップ全体）。
        波形の見た目もこの値で伸び縮みするので、**見えている大きさと鳴る音量が一致**します。 */
    float getGainDb() const;
    void setGainDb (float newGainDb, juce::UndoManager* undoManager);

    /** そのまま掛けられる倍率（dBから直したもの）。**式を2箇所に書かないため**に用意している。 */
    float getGainLinear() const;

    /** 仕様書5.5：**逆再生**（Phase 86／8.46）。

        **ファイルには手を加えません。** 鳴らすときに後ろから読むだけなので、
        いつでも戻せます（元に戻すのに音質も落ちない）。 */
    bool isReversed() const;
    void setReversed (bool shouldBeReversed, juce::UndoManager* undoManager);

    /** 8.147：**トランスポーズ**（Phase 185／改善案㉞。仕様書5.5）。半音の数。

        **逆再生と同じで、ファイルには手を加えません。** 音程を動かしたものは
        `AudioTransform`がキャッシュへ作り、再生側がそちらを開きます。
        0へ戻せば素のファイルに戻るので、**何度上げ下げしても音質は落ちません**
        （そのつど元から作り直すため）。

        `setTranspose()`は`±AudioTransform::maxSemitones`へ収めます。 */
    int getTranspose() const;
    void setTranspose (int semitones, juce::UndoManager* undoManager);

    //==========================================================================
    // 8.149：**クリップ全体の伸縮**（Phase 187／8.48。仕様書5.5.1）

    /** タイムラインの秒 ÷ ソースの秒。1.0＝そのまま、1.5＝**遅く・長く**。

        **`getLength()`はタイムラインの長さのまま**です。伸縮しても
        「そのクリップが曲の何秒を占めるか」は`getLength()`が答えます。 */
    double getStretch() const;

    /** 伸縮の倍率を決める。**長さは変えません**——
        「同じ長さのまま中身を詰める／間延びさせる」ときの入口です。

        普通は`setLength()`と対で呼びます（`TimelineComponent::stretchClipToLength()`）。
        `AudioTransform`の範囲（0.25〜4.0）へ収めます。 */
    void setStretch (double ratio, juce::UndoManager* undoManager);

    /** このクリップが使っている**ソースの長さ**（秒）＝`getLength() / getStretch()`。

        **「ソースの何秒ぶんか」が要るところは、必ずここを通すこと。**
        伸縮していないうちは`getLength()`と同じ値なので、
        Phase 186までのコードは`getLength()`のままでも動いてしまいます
        ——**伸ばした瞬間にずれる**ので、置き換え漏れは目で探すしかありません（1.14）。 */
    double getSourceLength() const;

    /** 使っているソースの終わり（秒）＝`getOffset() + getSourceLength()`。 */
    double getSourceEnd() const;

    /** ソースの時刻 → タイムラインの時刻。

        8.150：**ワープマーカーがあれば折れ線を通ります**（Phase 188）。
        無ければ`getStretch()`倍のまっすぐな線で、Phase 187と同じです。 */
    double sourceTimeToTimeline (double sourceSeconds) const;

    /** タイムラインの時刻 → ソースの時刻（`sourceTimeToTimeline()`の逆）。
        **必ず対で使うこと**（8.101）。 */
    double timelineToSourceTime (double timelineSeconds) const;

    //==========================================================================
    // 8.150：**ワープマーカー**（Phase 188／8.48。仕様書5.5.1）
    //
    // **クリップの中で音を配り直します。** 使うソースの範囲（`getOffset()`〜
    // `getSourceEnd()`）も、曲の中での長さ（`getLength()`）も**変わりません**
    // ——変わるのは「どの音が、クリップの中のどこで鳴るか」だけです。
    //
    // だから`getSourceLength()`も`getSourceEnd()`もPhase 187のままで、
    // 再生側（`ClipPlayerProcessor`）も1行も変わりません。

    /** マーカー1つぶん。 */
    struct WarpMarker
    {
        /** ソースの中の時刻（秒）。 */
        double sourceSeconds = 0.0;

        /** クリップの頭から数えて、その音が鳴る時刻（秒）。 */
        double clipSeconds = 0.0;
    };

    /** 置いてあるマーカー（**ソースの時刻の昇順**）。 */
    std::vector<WarpMarker> getWarpMarkers() const;

    bool hasWarpMarkers() const;

    /** ソース↔クリップの対応表。**マーカーが無ければ`getStretch()`倍のまっすぐな線**。

        定義域は`getOffset()`〜`getSourceEnd()`（値域は0〜`getLength()`）で、
        **外はいちばん端の区間の速さでまっすぐ延長**されます（`WarpMap.h`）。 */
    WarpMap getWarpMap() const;

    /** マーカーを1つ置く。**同じ場所に既にあれば動かします。**

        置ける範囲は、**両隣のマーカーのあいだ**です（追い越すと時間が戻る）。
        入らなければ何もせずfalseを返します。 */
    bool addWarpMarker (double sourceSeconds, double clipSeconds, juce::UndoManager* undoManager);

    /** `sourceSeconds`のマーカーの**鳴る時刻だけ**を動かす（ソース側は動かしません）。

        **掴んだ音は掴んだ音のまま**で、それがいつ鳴るかを変える操作なので、
        ソース側を動かす入口は用意していません。 */
    bool moveWarpMarker (double sourceSeconds, double newClipSeconds, juce::UndoManager* undoManager);

    void removeWarpMarkerNear (double sourceSeconds, double toleranceSeconds,
                                juce::UndoManager* undoManager);

    /** マーカーを全部消す（**伸縮の倍率はそのまま**）。 */
    void clearWarpMarkers (juce::UndoManager* undoManager);

    /** 8.150：**ソースのファイル全体**の対応表（Phase 188）。

        `getWarpMap()`がクリップの中だけを見るのに対し、こちらは
        **ファイルの頭から**の対応です。作り直し（`AudioTransform`）と、
        その結果を読む`ClipPlayerProcessor`が使います。

        **クリップの外は`getStretch()`倍のまっすぐな線**なので、
        クリップの頭は必ず`getOffset() × getStretch()`に来ます
        ——だから再生側の読み出し位置の式はPhase 187から変わりません。 */
    WarpMap getRenderWarpMap() const;

    /** 仕様書5.5：**オートフェード**（Phase 86／8.46）。

        両端に短いフェードを付けて、切り口の「プツッ」を消す。
        **既にフェードが付いている端は触りません**（長さを指定して作ったものを潰さない）。 */
    void applyAutoFade (juce::UndoManager* undoManager);

    /** ヒットポイント（トランジェント位置）。単位は「ソースファイル先頭からの秒数」。
        設計書1.3のhitPointsに対応。ValueTreeにはカンマ区切りの文字列として格納する。 */
    juce::Array<double> getHitPoints() const;
    void setHitPoints (const juce::Array<double>& newHitPoints, juce::UndoManager* undoManager);
    void addHitPoint (double timeSeconds, juce::UndoManager* undoManager);
    void removeHitPointNear (double timeSeconds, double toleranceSeconds, juce::UndoManager* undoManager);
};

//==============================================================================
/**
    設計書1.3のPluginInstance・3.8「状態保存」に対応する、ValueTreeの薄いラッパークラス。

    プラグインの識別情報（`juce::PluginDescription`）を子ノードとして丸ごと保持し、
    内部状態（`getStateInformation()`の中身）はBase64文字列として持つ。

    設計書1.3ではPluginInstanceをTrack配下のINSERTSに置く想定だが、
    現在のオーディオエンジンは「音源1台」「テストトーン用インサート1台」しか扱えないため、
    暫定的にプロジェクト直下の`<PLUGINS>`へ役割（role）付きで置いている。
    トラックごとのインサート（仕様書5.7）を実装する際に、この配置を移すことになる。
*/
class PluginInstance
{
public:
    explicit PluginInstance (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    juce::String getRole() const;
    juce::String getDisplayName() const;

    /** 保存されている識別情報を復元する。読み取れなければfalseを返す。 */
    bool getDescription (juce::PluginDescription& descriptionOut) const;
    void setDescription (const juce::PluginDescription& description, juce::UndoManager* undoManager);

    /** プラグインの内部状態（設計書3.8。Base64で格納）。 */
    juce::MemoryBlock getPluginState() const;
    void setPluginState (const juce::MemoryBlock& data, juce::UndoManager* undoManager);

    //==========================================================================
    /** 仕様書5.7.2・設計書1.3：サイドチェイン入力として使うトラックのID。

        空文字ならサイドチェイン未使用。実際に配線されるかは、そのプラグインが
        第2入力バス（サイドチェインバス）を持っているかで決まる（設計書3.9）。
        持っていないプラグインにIDを設定しても、単に無視される。 */
    juce::String getSidechainSourceTrackId() const;
    void setSidechainSourceTrackId (const juce::String& trackId, juce::UndoManager* undoManager);

    //==========================================================================
    /** 8.63：**このインサートを通さない**（Phase 101／改善案㉘。仕様書5.7）。

        **外すのではなく、通さないだけ**です。設定も内部状態もそのまま残るので、
        **掛けた音と掛けない音を聴き比べて、すぐ戻せます**。

        音の側で見るのは`AudioEngine::rebuildTrackNodes()`——
        **配線からそのノードを外します**（プラグイン側のバイパス機能には頼りません。
        持っていないプラグインもあり、持っていても「通さない」の意味が実装ごとに違うため）。 */
    bool isBypassed() const;
    void setBypassed (bool shouldBypass, juce::UndoManager* undoManager);
};

//==============================================================================
/**
    仕様書5.2.2「センドトラック」に対応する、送り1本ぶんの設定。

    「どのセンドトラックへ、どれくらいの量を送るか」を持つ。
    プリ／ポストフェーダーの区別は送り先のセンドトラック側が持つ（設計書1.3）。
*/
class Send
{
public:
    explicit Send (juce::ValueTree treeToWrap);

    juce::ValueTree state;

    juce::String getTargetTrackId() const;
    float getLevelDb() const;
    void setLevelDb (float newLevelDb, juce::UndoManager* undoManager);
};


//==============================================================================
/**
    設計書1.3の Track（基底）に対応する、ValueTreeの薄いラッパークラス。

    Trackクラス自体はデータを持たず、渡されたValueTreeを介して
    プロパティを読み書きするだけの「ビュー」であることに注意してください。
    実体は常にProjectModel（またはそのTRACKSノード）が保持しています。
*/
class Track
{
public:
    explicit Track (juce::ValueTree treeToWrap);

    /** 新しいトラック用のValueTreeを作成して返します（まだ親には追加されていません）。 */
    static Track create (const juce::String& name, TrackType type, juce::UndoManager* undoManager);

    /** 8.61：**トラックカラーの見本**（Phase 99／改善案⑫。設計書2.4）。

        **ARGBのhex文字列で持ちます。** データ層は`juce_graphics`に依存していないので、
        `juce::Colour`はここでは使えません（設計書1.1の層分離。`Track::create()`と同じ理由）。
        色として使う側が`juce::Colour::fromString()`を通します。

        **見本を持つのはここ1箇所**にしてあります。インスペクタの色見本もここから引くので、
        「見本には無い色が既定で入っている」が起きません（1.27）。 */
    static juce::StringArray getColourPalette();

    /** 8.160：見本を並べるときの1行ぶんの数（Phase 198/本人の要望）。

        **見本は`8の倍数`で並んでいます**（彩度：高/これまでの8色/彩度：低の3段）。
        パレットを描く側は、ここで割って行を出します——**数を書き写さないこと**（8.2）。 */
    static int getColourPaletteColumns();

    /** 何本目のトラックか、から既定の色を決める（見本を順に回す）。

        8.61：**作るたびに色を変えます**（Phase 99）。全部が同じ灰色だと、
        クリップやヘッダーに色を反映しても**見分けが付かず、機能が無いのと同じ**でした。
        **既存のプロジェクトは変わりません**（保存済みのトラックは自分の色を持っている）。 */
    static juce::String getDefaultColourForIndex (int trackIndex);

    /** 8.60：**新しいトラックの既定の音量**（Phase 97／改善案㉖）。

        0dBで作ると、数本重ねただけでマスターが振り切れます。
        **既存のプロジェクトには効きません**（保存済みのトラックは自分の`volume`を持つ）。

        **フェーダーのダブルクリックは0dBのままです**（`setDoubleClickReturnValue`）。
        あちらは「素通し（ユニティ）へ戻す」という意味で、作るときの値とは別のものです。 */
    static constexpr float defaultTrackVolumeDb = -6.0f;

    juce::ValueTree state;

    juce::String getId() const;

    juce::String getName() const;
    void setName (const juce::String& newName, juce::UndoManager* undoManager);

    TrackType getType() const;

    /** 設計書2.4：トラックカラー。ARGBのhex文字列（`juce::Colour::toString()`の形式）で持つ。

        `juce::Colour`を使わないのは意図的：`juce_data_structures`は`juce_graphics`に
        依存していないため、データ層でColourを扱うとモジュール依存が増えてしまう
        （設計書1.1の層分離）。色として解釈するのはUI層の仕事。 */
    juce::String getColourString() const;
    void setColourString (const juce::String& argbHex, juce::UndoManager* undoManager);

    bool isMuted() const;
    void setMuted (bool shouldBeMuted, juce::UndoManager* undoManager);

    bool isSoloed() const;
    void setSoloed (bool shouldBeSoloed, juce::UndoManager* undoManager);

    /** 仕様書5.4：録音待機（レコードアーム）。録音した音声はこのトラックへ入る。 */
    bool isArmed() const;
    void setArmed (bool shouldBeArmed, juce::UndoManager* undoManager);

    //=========================================================================
    // 8.84：**MIDIトラックの入力設定**（Phase 124/改善案⑯。仕様書5.4）
    //
    // 鍵盤を弾いたときに、**どこから受けて、どのチャンネルで音源へ渡すか**。
    // MIDIトラック以外では意味を持ちません（インスペクタも出しません）。

    /** 受け取るMIDI入力デバイスの名前。空なら**すべてのデバイス**から受ける。 */
    juce::String getMidiInputDeviceName() const;
    void setMidiInputDeviceName (const juce::String& deviceName, juce::UndoManager* undoManager);

    /** 受け取るMIDIチャンネル（1〜16）。**0なら「すべて」**。 */
    int getMidiInputChannel() const;
    void setMidiInputChannel (int channel, juce::UndoManager* undoManager);

    /** 音源へ送るときのMIDIチャンネル（1〜16）。**0なら「そのまま」**。

        マルチティンバー音源（1台で複数のパートを鳴らすもの）へ、
        トラックごとに違うチャンネルで送りたいときに使います。 */
    int getMidiOutputChannel() const;
    void setMidiOutputChannel (int channel, juce::UndoManager* undoManager);

    /** 音量はdB表記。0.0dBが基準。VCA連携時の加算方式は設計書1.3参照。 */
    float getVolumeDb() const;
    void setVolumeDb (float newVolumeDb, juce::UndoManager* undoManager);

    /** 定位。-1.0=左端、0.0=中央、+1.0=右端（仕様書5.7のチャンネルストリップ）。 */
    float getPan() const;
    void setPan (float newPan, juce::UndoManager* undoManager);

    /** オーディオクリップを追加する（Audioトラック向け。仕様書5.5、設計書1.3）。 */
    AudioClip addAudioClip (const juce::String& sourceFilePath, double startTimeSeconds,
                            double lengthSeconds, juce::UndoManager* undoManager, double offsetSeconds = 0.0);

    int getNumClips() const;
    AudioClip getClip (int index) const;
    void removeClip (const AudioClip& clip, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.7・設計書1.4：インサートスロット（トラックへ直列に挿すプラグイン）

    /** `<INSERTS>`ノードを返す。古いプロジェクトファイルには無いため、必要なら作る。 */
    juce::ValueTree getOrCreateInsertsNode (juce::UndoManager* undoManager);

    int getNumInserts() const;
    PluginInstance getInsert (int index) const;

    /** インサートを末尾へ追加する。 */
    PluginInstance addInsert (const juce::PluginDescription& description, juce::UndoManager* undoManager);

    void removeInsert (int index, juce::UndoManager* undoManager);

    /** 8.66：**インサートの並び順を変える**（Phase 104／改善案㉘㉙）。

        `toIndex`は移動後に置きたい位置（0が先頭）。**掛ける順番は音を決めます**
        （コンプ→EQとEQ→コンプは別の音）。

        **音の側は`AudioEngine::moveInsertInTrack()`が面倒を見ます。**
        ここを直接呼ぶとモデルだけが動き、グラフの並びは古いままになります
        （8.63のバイパスと同じ落とし穴）。 */
    void moveInsert (int fromIndex, int toIndex, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.3・設計書1.5：音源スロット（MIDI・インストゥルメントトラック用）
    //
    // インサート（エフェクト）とは別の専用スロットとして持つ。音源は信号経路の
    // **入口**（MIDIを受けて音を作る出発点）であり、途中に挿すインサートとは
    // 役割が違うため、`<INSERTS>`へ混ぜず`<INSTRUMENT>`という別ノードへ置いている。
    // 1トラックにつき1台（設計書1.5の「MidiPlayer →(MIDI)→ 音源」に対応）。

    bool hasInstrument() const;

    /** 割り当てられている音源。未設定なら`state.isValid()`がfalseになる。 */
    PluginInstance getInstrument() const;

    /** 音源を割り当てる（既にあれば差し替える）。 */
    PluginInstance setInstrument (const juce::PluginDescription& description, juce::UndoManager* undoManager);

    void removeInstrument (juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.6・設計書1.4：オートメーションレーン

    /** `<AUTOMATION>`ノードを返す。古いプロジェクトには無いため、必要なら作る。 */
    juce::ValueTree getOrCreateAutomationNode (juce::UndoManager* undoManager);

    int getNumAutomationLanes() const;
    AutomationLane getAutomationLane (int index) const;

    /** 対象パラメータのレーンを探す。無ければ`state.isValid()`がfalseのものを返す。 */
    AutomationLane findAutomationLane (const juce::String& targetId) const;

    /** 対象パラメータのレーンを返す（無ければ作る）。 */
    AutomationLane getOrCreateAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager);

    void removeAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager);

    /** 仕様書5.6：このトラックの書き込みモード。既定はRead。 */
    AutomationMode getAutomationMode() const;
    void setAutomationMode (AutomationMode newMode, juce::UndoManager* undoManager);

    /** 8.54：**その時刻で実際に効いている値**（Phase 93）。

        オートメーションはモデルの`volume`／`pan`を書き換えません
        （書き換えると、再生するたびにフェーダーの設定値が壊れる）。
        **画面が「いま鳴っている値」を知るにはここを通します。**

        点があって、なおかつ書き込みモードが`Read`のときだけレーンの値を返します。
        記録中（Touch/Latch/Write）は**フェーダーの値がそのまま音になる**ので
        （`TrackChannelProcessor::setAutomationBypassed`）、そちらを返します。

        **式を2箇所に書かないため**にモデル側へ置いてあります。Console・インスペクタが
        別々に判断すると、同じ再生位置で表示が食い違います。 */
    float getEffectiveVolumeDbAt (double timeSeconds) const;
    float getEffectivePanAt (double timeSeconds) const;

    //==========================================================================
    // 8.56：アレンジ画面に出すレーンの行（Phase 94／D3）
    //
    // **Phase 93までは「トラックごとに1つだけ」**（`automationView`）で、
    // そのトラック行に**重ねて**描いていた。専用の行を持つようになったので、
    // **1トラックに何本でも並べられる**（Volume と Pan を同時に見る、など）。
    //
    // 「どのレーンを開いているか」は画面の状態だが、モデルに置いている。
    // プロジェクトを開き直したときに開いていたレーンが復元されるほうが自然で、
    // UIに状態を持たせない原則（HANDOVER 3.1）とも一致するため。

    /** 行として出ているレーンの数。 */
    int getNumVisibleAutomationLanes() const;

    /** 上から`ordinal`番目の、行として出ているレーン。 */
    AutomationLane getVisibleAutomationLane (int ordinal) const;

    bool isAutomationLaneVisible (const juce::String& targetId) const;

    /** レーンの行を出す／隠す。**出すときは無ければ作る**（点が無くても行は出る）。 */
    void setAutomationLaneVisible (const juce::String& targetId, bool shouldBeVisible,
                                    juce::UndoManager* undoManager);

    /** 開いている行をすべて閉じる（ヘッダーの「A」メニューの「すべて隠す」）。 */
    void hideAllAutomationLanes (juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.2.2：センド（このトラックからセンドトラックへの送り）

    int getNumSends() const;
    Send getSend (int index) const;

    /** 送り先のセンドトラックを指定して、送りを1本追加する。
        既に同じ送り先があれば、それを返す（重複して作らない）。 */
    Send addSend (const juce::String& targetTrackId, juce::UndoManager* undoManager);

    void removeSend (int index, juce::UndoManager* undoManager);

    /** 8.66：**センドの並び順を変える**（Phase 105/改善案㉘㉙）。

        センドは並列なので、**順番は音を変えません**。それでも並べ替えられるのは、
        本数が増えたときに「よく触るものを上に」置けるようにするためです
        （インサートと同じ操作で済むほうが覚えることも減る）。

        インサートと違い、**Undoに積んで構いません**：センドの配線は
        `AudioEngine::rebuildSendConnections()`がモデルから作り直すので、
        モデルだけ巻き戻しても食い違いません（`Track::moveInsert`との違い）。 */
    void moveSend (int fromIndex, int toIndex, juce::UndoManager* undoManager);

    /** センドトラック自身の設定：プリフェーダーで受けるか（既定はポストフェーダー）。
        送り元ではなく送り先が持つ（設計書1.3のSendTrack.prePost）。 */
    bool isPreFader() const;
    void setPreFader (bool shouldBePreFader, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.2.4・設計書1.3：VCAトラック（このトラックがVCAのときだけ意味を持つ）
    //
    // リンク情報は**VCA側**が`linkedTrackIds`として持つ（設計書1.4のスキーマどおり）。
    // 1トラックが同時にリンクできるVCAは1つだけで、その保証は
    // `ProjectModel::assignTrackToVca()`（他のVCAから外してから付ける）が受け持つ。

    /** このVCAにリンクされているトラックのID一覧。 */
    juce::StringArray getLinkedTrackIds() const;
    void setLinkedTrackIds (const juce::StringArray& trackIds, juce::UndoManager* undoManager);

    bool isTrackLinkedToVca (const juce::String& trackId) const;

    /** リンクを1本足す／外す（既に無い／有る場合は何もしない）。 */
    void linkTrackToVca (const juce::String& trackId, juce::UndoManager* undoManager);
    void unlinkTrackFromVca (const juce::String& trackId, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.3.2：ドラムエディター（Phase 25）

    /** このトラックが使うドラムマップのID。空文字ならドラムマップ未使用。 */
    juce::String getDrumMapId() const;
    void setDrumMapId (const juce::String& mapId, juce::UndoManager* undoManager);

    /** 仕様書5.3.2：このトラックをドラムエディターで表示するか。

        表示モードをトラックに持たせているのは、**ドラムトラックは常にドラムエディターで
        編集したい**のが普通だから。クリップを切り替えるたびに選び直すのは煩わしい。 */
    bool isDrumEditorEnabled() const;
    void setDrumEditorEnabled (bool shouldBeEnabled, juce::UndoManager* undoManager);

    /** 8.1のD5：ピアノロールで、このトラックのノートを透かしとして出すか（Phase 74）。

        **トラックに持たせているのは、ドラムエディターの表示モードと同じ理由**：
        「このトラックは参照用に出しておきたい」は編集対象を変えても続く性質のもので、
        クリップを切り替えるたびに選び直すのは煩わしいためです。

        **Undoには積みません**（画面の見え方であって、曲の中身ではない。8.14の刻みと同じ扱い）。 */
    bool isWatermarkVisible() const;
    void setWatermarkVisible (bool shouldBeVisible);

    /** 8.44：**この行を畳んでいるか**（Phase 84／C12。仕様書5.2.3）。

        いまはコードトラックだけが使う。**画面の見え方なので、Undoの対象にしない**
        （`isWatermarkVisible()`と同じ扱い）。 */
    bool isCollapsed() const;
    void setCollapsed (bool shouldBeCollapsed);

    /** 8.62：**この行の高さ**（Phase 100）。ヘッダーの下端をドラッグして変える。

        **0なら「まだ決めていない」**で、種別ごとの既定を使う
        （決めるのは`TimelineComponent::getTrackAreaHeight()`1箇所。8.18）。

        **画面の見え方なので、Undoの対象にしません**（`isCollapsed()`と同じ扱い）。
        ただし**プロジェクトには保存します**：波形を大きく見たいトラックと
        場所を取りたくないトラックの区別は曲ごとの話で、開き直したときに残ってほしい。 */
    int getCustomRowHeight() const;
    void setCustomRowHeight (int newHeight);

    /** 8.50：**入っているフォルダのID**（Phase 89／D2）。空なら一番上の階層にいる。

        **子が親を指す形**にしてある（設計書1.3の`childTrackIds`から変更）。
        並べ替えや削除のたびに一覧を直す必要が無く、食い違いようがない。 */
    juce::String getParentFolderId() const;
    void setParentFolderId (const juce::String& folderId, juce::UndoManager* undoManager);

    //==========================================================================
    // 8.143：**パラアウトの受け皿**（Phase 181／改善案⑮）。
    // `TrackType::DrumOut`のときだけ意味を持つ

    /** 音を受け取る音源トラックのID。空なら、まだどこにも繋がっていない。 */
    juce::String getDrumOutSourceTrackId() const;

    /** 受け取る出力バスの番号。**1から**（0番は音源トラック自身が受ける）。 */
    int getDrumOutSourceBus() const;

    /** 受け取り先を決める。**`DrumOut`以外のトラックでは意味を持ちません。** */
    void setDrumOutSource (const juce::String& sourceTrackId, int busIndex,
                            juce::UndoManager* undoManager);

    //==========================================================================
    // 8.142：**このトラックの時間の基準**（Phase 180／8.105の宿題3）

    /** `musical`か`linear`か。**入っていなければ種別ごとの既定**（`TimeBase`の説明）。 */
    TimeBase getTimeBase() const;

    /** 基準を変える。**中のオーディオクリップの位置も、その場で持ち替えます。**

        持ち替えないと、`musical`にした瞬間に**拍が入っていないクリップ**が
        できてしまい、全部が曲の頭へ寄ります。**切り替えても音は動きません**
        ——いまの秒の位置を、そのまま拍へ（またはその逆へ）写すだけです。 */
    void setTimeBase (TimeBase newBase, juce::UndoManager* undoManager);

    // 8.94：**MIDIクリップの入口は消しました**（Phase 134）。
    //
    // Phase 131〜133は`getNumMidiClips()`が0を返すことで古い経路を素通りさせて
    // いましたが、**素通りは「効かないコードが残っている」だけ**です。
    // 消しておかないと、`addMidiClip()`をうっかり呼んで
    // **画面にも出ず音も鳴らない`<MIDICLIP>`**を作れてしまいます（8.91）。
    //
    // **`MidiClip`クラス自体は残しています**——古い`.ms1`を読むときの変換
    // （`migrateMidiClipsToTrackNotes()`）で必要だからです。
    // **新しく書き出されることはありません。**

    //==========================================================================
    // 8.91：MIDIはトラックが直接持つ（Phase 131／仕様書5.3）
    //
    // **クリップという入れ物をやめました。** `<TRACK><NOTES><NOTE>`と
    // `<TRACK><CCEVENTS><CC>`に、**曲の時刻そのもの**で載ります。
    //
    // ### なぜやめたか
    //
    // クリップは「中身の時刻」「オフセット（窓）」「開始位置」の3つを持ち、
    // **同じノートが3通りの数字で表される**入れ物でした（1.14）。
    // Phase 126〜130の不具合は全部ここから出ています——隠れたノートが見える、
    // 重なる、トリムが戻る、持ち主が勝手に変わる。
    // **時刻が1つしか無ければ、どれも起こりようがありません。**
    //
    // ### 画面に出る「クリップ」は計算で作る
    //
    // アレンジ画面の四角は**ノートの塊**（`getNoteBlocks()`）です。データではないので、
    // **重なりようがなく、伸縮という操作が存在せず、空白が埋まれば勝手に1つ**になります。
    //
    // ### 時刻はすべて「曲の時刻」
    //
    // 換算はもうありません。**`Note::getStartTime()`がそのまま曲の時刻**です。

    int getNumNotes() const;
    Note getNote (int index) const;

    /** ノートを1つ足す（`timelineStartSeconds`は**曲の時刻**）。

        8.138：中では拍へ直して`addNoteBeats()`を呼びます（Phase 176）。
        **秒を渡す側は何も変わりません。** */
    Note addNote (int pitch, int velocity, double timelineStartSeconds, double lengthSeconds,
                  juce::UndoManager* undoManager);

    /** 8.138：**拍で足す**（Phase 176／8.105の宿題3）。

        保存されているのが拍なので、**こちらが素の形**です。

        切り離されたツリー（クリップボードへ取った複製など）から戻すときは、
        **必ずこちらを使うこと**。あちらのノートは`getLength()`を訊かれても
        テンポの表へ辿り着けず、**既定の120BPMで答えます**（`MusicalTime.h`）。 */
    Note addNoteBeats (int pitch, int velocity, double startBeats, double lengthBeats,
                        juce::UndoManager* undoManager);

    void removeNote (const Note& note, juce::UndoManager* undoManager);

    /** 8.77：中のノートをまとめて上下させる（Phase 117から移設）。

        1つでもMIDIの範囲（0〜127）を外れるなら**何もせずfalseを返します**。
        外れるものだけ止めると、**和音の形が崩れます**。 */
    bool transposeNotes (int semitones, juce::UndoManager* undoManager);

    /** 仕様書5.3.4：このトラックへ最後に適用したグルーヴテンプレートのID。

        **記録するだけで、再生時に効くわけではありません**（グルーヴはノートの時刻と
        ベロシティを実際に書き換える1回きりの編集）。Phase 131でクリップから移設。
        識別子は`clipGrooveTemplateId`を使い回しています——**プロパティ名は
        ノードの種別と一致している必要がない**ので、対を増やさない（1.25）。 */
    juce::String getGrooveTemplateId() const;
    void setGrooveTemplateId (const juce::String& templateId, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.3.3：CC（Phase 131でクリップからトラックへ移した）
    //
    // **イベントは時刻の昇順**で並んでいることを前提にしている。
    // 追加は正しい位置へ挿し、移動後は`sortCCEvents()`で並べ直すこと。

    int getNumCCEvents() const;
    CCEvent getCCEvent (int index) const;

    int getNumCCEventsFor (int controllerNumber) const;
    CCEvent getCCEventFor (int controllerNumber, int index) const;

    /** 同じコントローラーの「次の点」（繋ぎ方を効かせるのに要る）。 */
    CCEvent findNextCCEvent (const CCEvent& event) const;

    /** **同じコントローラーの同じ時刻に既にあれば、値を上書きします**
        （同じ場所を何度も押したときに積み重ならないように）。 */
    CCEvent addCCEvent (int controllerNumber, int value, double timelineSeconds,
                        juce::UndoManager* undoManager);

    /** 8.138：**拍で足す**（Phase 176）。`addNoteBeats()`と同じ扱いです。 */
    CCEvent addCCEventBeats (int controllerNumber, int value, double timeBeats,
                              juce::UndoManager* undoManager);

    void removeCCEvent (const CCEvent& event, juce::UndoManager* undoManager);
    void removeAllCCEventsFor (int controllerNumber, juce::UndoManager* undoManager);
    void sortCCEvents (juce::UndoManager* undoManager);

    /** 指定時刻でのコントローラーの値。**補間せず、次の点まで保持**します。 */
    int getCCValueAt (int controllerNumber, double timelineSeconds) const;

    //==========================================================================
    // 8.91：アレンジ画面に出す「ノートの塊」（Phase 131）

    /** ノートの塊1つぶん。**データではなく、そのつど計算した見え方**です。 */
    struct NoteBlock
    {
        double startTime = 0.0;   // 最初のノートの開始（曲の時刻）
        double endTime = 0.0;     // 最後のノートの終わり
        int numNotes = 0;
    };

    /** ノートを時間順にまとめて、塊の一覧を返す。

        **`gapSeconds`以上の空きがあるところで切ります**（既定は呼び出し側が
        1小節ぶんを渡す）。空きより短い隙間は同じ塊に含めます——
        音符1つごとに切ると、まばらな旋律が細切れの四角だらけになるため。

        **端は丸めません**（ノートの実際の範囲）。「打ち込んだものがそのまま出る」
        という約束に忠実にするため。 */
    std::vector<NoteBlock> getNoteBlocks (double gapSeconds) const;

    //==========================================================================
    // 仕様書5.2.3・5.11：コードトラック（このトラックがChordのときだけ意味を持つ）

    /** このコードトラックのキー（仕様書5.11.1）。未設定ならCメジャー。

        **キーはコードトラックが持ちます**（設計書1.3のChordTrack.key）。
        仕様書5.2.3は「プロジェクト単位で保持」と書いていますが、同じ節が
        「1プロジェクトにつき基本1本」とも言っているので、実質は同じことです。
        プロジェクト全体のキーを知りたい側は`ProjectModel::getProjectKey()`を
        通してください（探し方を1箇所に集めるため）。 */
    Scale getChordKey() const;
    void setChordKey (const Scale& newKey, juce::UndoManager* undoManager);

    /** `<CHORDREGIONS>`ノードを返す。古いプロジェクトには無いため、必要なら作る。 */
    juce::ValueTree getOrCreateChordRegionsNode (juce::UndoManager* undoManager);

    /** コード区間を追加する。

        **開始時刻の順に並ぶ位置へ挿します。** 「直前のコード」を引く操作
        （スコアリングの基準・パッドの色）が並び順を前提にしているためで、
        追加のたびに並べ替えるより、入れる場所を選ぶほうが安全です。 */
    ChordRegion addChordRegion (const Chord& chord, double startTimeSeconds,
                                double lengthSeconds, juce::UndoManager* undoManager);

    /** 8.139：**拍で足す**（Phase 177）。`addNoteBeats()`と同じ扱いで、
        切り離されたツリーから戻すときはこちらを使うこと（`MusicalTime.h`）。 */
    ChordRegion addChordRegionBeats (const Chord& chord, double startBeats,
                                      double lengthBeats, juce::UndoManager* undoManager);

    int getNumChordRegions() const;

    /** 何番目かで引く（開始時刻順）。範囲外なら`state.isValid()`がfalseのものを返す。 */
    ChordRegion getChordRegion (int index) const;

    /** その時刻を含む区間。無ければ`state.isValid()`がfalseのものを返す。

        区間の終端は含みません（`start <= time < end`）。隣り合う区間の境目で
        両方が引っかかると、挿入位置とパッドの色が食い違うためです。 */
    ChordRegion findChordRegionAt (double timeSeconds) const;

    /** 8.128：**区間を隙間なく並べ直す**（Phase 164／改善案13）。

        コード区間を「旗」で表す（＝この旗から次の旗まで）ことにしたので、
        **長さは自分で決めるものではなく、次の旗までが自動的に長さ**になります。

        それぞれの区間の長さを「次の区間の始まりまで」に揃えます。
        **いちばん後ろの区間は触りません**——次が無いので、曲の終わりを決めるのは
        そちらの長さです。

        **区間を足す・動かす・消したあとは必ず呼ぶこと。** 呼び忘れると、
        旗と旗のあいだに「コードの無い隙間」が残ります
        （`findChordRegionAt()`は`[開始, 終了)`で探すため）。 */
    void normaliseChordRegions (juce::UndoManager* undoManager);


    /** その時刻より前にある、いちばん近い区間（スコアリングの「直前のコード」）。 */
    ChordRegion findChordRegionBefore (double timeSeconds) const;

    /** その範囲に重なる区間があるか。`ignoreRegionId`のものは数えない。

        **重なりの判定はここにしかありません。** 追加のときと移動・伸縮のときで
        別々に書くと必ずずれます（8.2「同じ判定を複数箇所に書くと、必ずずれる」）。
        重なると「その時刻のコード」が一意に決まらなくなり、直前のコードを引く処理も
        どちらを見ればよいか分からなくなります。 */
    bool hasOverlappingChordRegion (double startSeconds, double endSeconds,
                                    const juce::String& ignoreRegionId = {}) const;

    /** 区間の位置と長さを変える（移動・伸縮）。

        **開始時刻順の並びも保ちます。** `addChordRegion()`が順番を守って挿しているので、
        時刻だけ書き換えると並びが崩れ、「直前のコード」が別のコードになります。 */
    void setChordRegionTime (const ChordRegion& region, double newStartSeconds,
                             double newLengthSeconds, juce::UndoManager* undoManager);

    void removeChordRegion (const ChordRegion& region, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.5：クリップの分割・結合・複製（Phase 50）
    //
    // **オーディオとMIDIで共通の実装にしてある。** どちらも`<CLIPS>`の子で、
    // 開始位置・長さ・オフセットを同じプロパティ名で持っているため
    // （種別ごとに書くと、片方だけ直して食い違う典型になる。8.2）。
    // 引数がValueTreeなのはそのため。`AudioClip::state`／`MidiClip::state`を渡すこと。

    /** クリップを指定時刻（タイムライン上の秒）で2つに割る。

        **中身は動かさない。** 割れた2つは同じ中身を指したまま、見せる窓
        （オフセットと長さ）だけが変わる。MIDIのノートもオーディオの波形も、
        クリップの中の時刻は「中身の先頭」から数えているのでそのままで正しい（1.14）。

        時刻がクリップの内側でなければ何もせずfalseを返す。 */
    bool splitClipAt (const juce::ValueTree& clipState, double timeSeconds,
                      juce::UndoManager* undoManager);

    /** クリップを複製して、その直後（元の終わり）へ置く。

        中身ごと複製する（MIDIならノートとCCも一緒）。IDは振り直す。 */
    juce::ValueTree duplicateClip (const juce::ValueTree& clipState, juce::UndoManager* undoManager);

    /** クリップの複製を、指定した時刻へ置く（Phase 71／貼り付け用）。

        **IDは振り直す**（同じIDのクリップが2つあると、片方を消したつもりが
        両方に効く。1.32）。オーディオでもMIDIでも同じ扱いです。 */
    juce::ValueTree addClipCopy (const juce::ValueTree& clipState, double startTimeSeconds,
                                  juce::UndoManager* undoManager);

    /** 同じトラックの「次のクリップ」と結合する。

        **オーディオは、割ったものを戻す場合しか結合できません。**
        同じファイルの連続した部分（`次のオフセット == このオフセット + この長さ`）
        でなければ、音を書き出して1本にする処理が要ることになるためです。
        MIDIはノートを混ぜるだけなので、隣り合っていれば結合できます。

        できなかった理由は`reasonIfFailed`へ入ります（画面に出すため）。 */
    bool mergeClipWithNext (const juce::ValueTree& clipState, juce::UndoManager* undoManager,
                            juce::String& reasonIfFailed);

    //==========================================================================
    // 8.78：**クリップのグループ**（Phase 118／改善案㊱。仕様書5.5）
    //
    // ### 「結合」とは別ものです
    //
    // | | 何が起きるか |
    // |---|---|
    // | 結合（`mergeClipWithNext()`） | 隣り合う2つが**1つのクリップになる**（元に戻すには割る） |
    // | **グループ化**（ここ） | クリップは**そのまま**。**まとめて選ばれる**ようになるだけ |
    //
    // ### 実装は「まとめて選ばれる」だけ
    //
    // グループの1つを選ぶと、画面が**同じグループのクリップを全部選択に足します**
    // （`TimelineComponent`）。移動・削除・複製・カット／コピーは
    // **前から複数選択に効く**ので、そこへ乗るだけで全部まとめて動きます。
    //
    // **「グループだから一緒に動かす」という処理を操作ごとに書かないこと。**
    // 書くと、足すたびにグループ対応を忘れる箇所が出ます（8.2）。

    /** そのクリップのグループID。どのグループにも入っていなければ空文字。 */
    static juce::String getClipGroupId (const juce::ValueTree& clipState);

    static void setClipGroupId (const juce::ValueTree& clipState, const juce::String& groupId,
                                 juce::UndoManager* undoManager);

};

//==============================================================================
/**
    設計書1.3 / 1.7 の ProjectModel。

    ValueTreeをルートに持つ、プロジェクト全体の唯一の情報源（Single Source of Truth）。
    Undo/Redoはjuce::UndoManagerとValueTreeのトランザクション機構をそのまま利用するため、
    トラックの追加・削除・プロパティ変更をこのクラス経由で行う限り、
    独自のコマンドパターン実装は不要です（設計書1.7）。

    現時点ではAudioClip・MidiClip・PluginInstance等はまだ実装していません。
    まずTrackの出し入れが正しく動くことを確認するための土台です。
*/
class ProjectModel : public TempoMapSource,
                      private juce::ValueTree::Listener
{
public:
    ProjectModel();

    /** 8.137：`MusicalTime`の対応表から自分を外す（Phase 175）。
        **外し忘れると、壊れたポインタが表に残ります。** */
    ~ProjectModel() override;

    juce::UndoManager& getUndoManager() { return undoManager; }

    /** 「ユーザー操作1回ぶん」の区切りを作る。ここから次にbeginActionを呼ぶまでの変更が、
        Undoの1ステップにまとまる（`juce::UndoManager`は自動では区切らないため、
        これを呼ばないと起動以降の全変更が1つのステップになってしまう）。
        actionNameは「元に戻す: ○○」としてメニューに表示される。 */
    void beginAction (const juce::String& actionName) { undoManager.beginNewTransaction (actionName); }

    /** プロジェクトファイルの拡張子（設計書1.7）。**保存するときはこれ。**

        Phase 56で`.pdawproj`から変えました（8.1のB2）。中身の形は変わっていません。 */
    static juce::String getFileExtension() { return Branding::projectExtension; }

    /** Phase 56より前に保存したプロジェクトの拡張子。

        **開くときだけ受け付ける**（保存すると新しい拡張子になります）。
        中身は同じValueTreeなので、読み込みに特別な処理は要りません。
        古いファイルが手元から無くなったら、これと`getFileWildcard()`の
        セミコロン以降を消して構いません。 */
    static juce::String getLegacyFileExtension() { return ".pdawproj"; }

    /** ファイル選択ダイアログのフィルタ。**古い拡張子も出す**（開けなくならないように）。 */
    static juce::String getFileWildcard()  { return Branding::projectWildcard; }

    /** そのファイルがプロジェクトファイルか（新旧どちらの拡張子でもtrue）。

        **拡張子の判定を各所に書かないこと。** 新旧2つを見る必要があるので、
        書き写すと片方を忘れます（HANDOVER 8.2）。 */
    static bool isProjectFile (const juce::File& file)
    {
        return file.hasFileExtension (getFileExtension())
            || file.hasFileExtension (getLegacyFileExtension());
    }

    juce::String getName() const;
    void setName (const juce::String& newName, juce::UndoManager* undoManager);

    /** 仕様書5.1：新規プロジェクトを作る（現在の内容は破棄される）。 */
    void createNewProject();

    /** 8.138：**いま書き出す保存形式の版**（Phase 176／8.105の宿題3）。

        | 版 | 中身 |
        |---|---|
        | 1（プロパティが無い） | ノートとCCの時刻は**秒**（Phase 175まで） |
        | 2 | ノートとCCの時刻は**拍**（`startBeats`・`lengthBeats`・`timeBeats`） |
        | **3** | **コード区間・マーカー・オートメーション点・ループ範囲**も拍（8.139） |

        **読み込み時に一方通行で変換します**（`migrateTimesToBeats()`）。
        変換したときは、**1度だけ`.v1-backup`を残します**——
        保存のたびに作る`.bak`は毎回上書きされるので、2回保存すると古い形が消えるためです。 */
    static constexpr int currentFormatVersion = 3;

    //==========================================================================
    // 設計書3.8：プラグインの保存・復元。
    // トラックに属さないプラグインを、役割（role）で区別して1台ずつ保持する。
    //
    // `instrument`はPhase 13bまでの形式（音源1台を全MIDIトラックで共有）で使っていた役割で、
    // Phase 14以降は**古いプロジェクトの読み込み時だけ**参照する（loadFromFile内で
    // トラックの音源スロットへ移す。Track::setInstrument参照）。新しく書き込むことはない。
    static juce::String getInstrumentRole()   { return "instrument"; }
    static juce::String getMasterInsertRole() { return "masterInsert"; }

    /** 指定した役割のプラグイン情報を返す。無ければ無効なValueTreeを包んだものを返す
        （`state.isValid()`で判定できる）。 */
    PluginInstance getPluginForRole (const juce::String& role) const;

    /** 指定した役割のプラグインを登録する（既にあれば置き換える）。 */
    PluginInstance setPluginForRole (const juce::String& role, const juce::PluginDescription& description,
                                      juce::UndoManager* undoManager);

    void removePluginForRole (const juce::String& role, juce::UndoManager* undoManager);

    /** 現在編集中のファイル。未保存の新規プロジェクトでは存在しないFileを返す。 */
    juce::File getCurrentFile() const { return currentFile; }

    /** 前回の保存以降に変更が加えられたか（タイトルバーの「*」表示や終了確認に使う）。 */
    bool hasUnsavedChanges() const { return unsavedChanges; }

    /** 「未保存かどうか」が変化したときに呼ばれる（タイトル表示の更新用）。 */
    std::function<void()> onSavedStateChanged;

    /** ルートのValueTree。トラックの増減を購読したい層（AudioEngine等）が使う。
        **プロジェクトを読み込むとルート自体が差し替わる**ため、購読側は
        onStateReplacedで購読し直す必要がある。 */
    juce::ValueTree getState() const { return state; }

    /** 読み込み・新規作成でルートのValueTreeが差し替わった直後に呼ばれる。
        購読し直しの合図として使う。 */
    std::function<void()> onStateReplaced;

    /** 仕様書5.1：オートセーブから復元した直後の状態にする。
        「編集中のファイル」を元のプロジェクトファイル（無ければ空）に戻し、
        未保存扱いにする。復元した内容はまだ元ファイルへ書かれていないため。 */
    void markAsRecovered (const juce::File& originalFile);

    /** 仕様書5.7・設計書1.3のmasterBus：マスターチャンネルの音量（dB）。 */
    float getMasterVolumeDb() const;
    void setMasterVolumeDb (float newVolumeDb, juce::UndoManager* undoManager);

    /** `<MASTERBUS>`ノードを返す。古いプロジェクトファイルには無いため、必要なら作る。
        UI側がValueTree::Listenerとして購読するために公開している。 */
    juce::ValueTree getOrCreateMasterBusNode();

    /** 8.69：**マスターのインサートを、トラックと同じ道具で扱うための入口**
        （Phase 108／設計書1.4の`MASTERBUS/INSERTS`。8.1のD6）。

        `<MASTERBUS>`の中身は、インサートに関しては**トラックとまったく同じ形**です
        （`<INSERTS>`の下に`<PLUGININSTANCE>`が並ぶ）。そこで
        `Track`のインサート操作（`getNumInserts()`／`addInsert()`／`moveInsert()`…）を
        **そのまま使い回します**。同じ処理をマスター用にもう1組書くと、
        片方だけ直す形の食い違いが必ず出ます（8.2）。

        **インサート以外には使わないこと。** 返ってくる`Track`は
        `getId()`が空文字、`getType()`が既定値（Audio）になります。
        ——ただし`getId()`が空文字なのは**都合が悪いのではなく、そういう約束**です：
        `AudioEngine`はもともと**「trackIdが空文字ならマスター」**という決まりで
        動いているので（`beginAutomationTouch()`等）、このまま渡せば通ります。 */
    Track getMasterBusInsertHost();

    //==========================================================================
    // 仕様書5.6：マスターチャンネルのオートメーション（Phase 20）。
    // トラックと同じ仕組みだが、置き場所が`<MASTERBUS>`配下になる。

    AutomationLane findMasterAutomationLane (const juce::String& targetId) const;
    AutomationLane getOrCreateMasterAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager);

    AutomationMode getMasterAutomationMode() const;
    void setMasterAutomationMode (AutomationMode newMode, juce::UndoManager* undoManager);

    /** 8.54：マスターの、その時刻で実際に効いている音量（Phase 93）。
        `Track::getEffectiveVolumeDbAt()`と同じ考え方（判断も1箇所にまとめてある）。 */
    float getEffectiveMasterVolumeDbAt (double timeSeconds) const;

    /** 8.56：マスターのレーンの行（Phase 94／D3）。トラック側と同じ考え方。
        マスターはパンを持たないので、実質Volumeだけになる。 */
    int getNumVisibleMasterAutomationLanes() const;
    AutomationLane getVisibleMasterAutomationLane (int ordinal) const;
    bool isMasterAutomationLaneVisible (const juce::String& targetId) const;
    void setMasterAutomationLaneVisible (const juce::String& targetId, bool shouldBeVisible,
                                          juce::UndoManager* undoManager);
    void hideAllMasterAutomationLanes (juce::UndoManager* undoManager);

    /** 8.59：マスターのレーンを**中身ごと消す**（Phase 96）。
        `Track::removeAutomationLane()`と対（隠すのとは別物）。 */
    void removeMasterAutomationLane (const juce::String& targetId, juce::UndoManager* undoManager);

    /** 8.56：どこか1つでもレーンの行が開いているか（Phase 94）。
        マスター行を出すかどうかの判断に使う（開くためのボタンがそこにあるため）。 */
    bool isAnyAutomationLaneVisible() const;

    //==========================================================================
    // 仕様書5.9：ループ再生（Phase 48）
    //
    // **プロジェクトに保存する。** 「どこを繰り返し聴きながら作業しているか」は
    // 曲ごとの話で、開き直したときに残っていてほしいため
    // （メトロノームの入切がアプリ設定なのとは逆。6.3）。

    bool isLoopEnabled() const;
    void setLoopEnabled (bool shouldLoop, juce::UndoManager* undoManager);

    /** 曲の時刻（秒）。8.139：**保存されている拍から換算した値**です（Phase 177）。 */
    double getLoopStartTime() const;
    double getLoopEndTime() const;

    /** 8.139：**保存されているのはこちら**（Phase 177／8.105の宿題3）。
        テンポを変えても「2小節目から4小節目まで」のまま動きません。 */
    double getLoopStartBeats() const;
    double getLoopEndBeats() const;

    /** ループ範囲を決める。**開始と終了が逆でも入れ替えて受け取る**ので、
        ドラッグの向きを呼び出し側で気にしなくてよい。
        長さが0以下になる指定は無視する（折り返しようがないため）。 */
    void setLoopRange (double startSeconds, double endSeconds, juce::UndoManager* undoManager);

    /** 8.139：**拍で決める**（Phase 177）。向きは中で揃えます。 */
    void setLoopRangeBeats (double startBeats, double endBeats, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.9：マーカー（Phase 49）
    //
    // プロジェクト直下の`<MARKERS>`に置く。**時刻の順に並べて持つ**ので、
    // 「次のマーカーへ」「前のマーカーへ」は前から見ていくだけで済む
    // （コード区間と同じ考え方。8.5）。

    int getNumMarkers() const;

    /** 何番目かで引く（時刻順）。範囲外なら`state.isValid()`がfalseのものを返す。 */
    Marker getMarker (int index) const;

    /** マーカーを1つ足す。時刻の順を保った位置へ挿す。

        同じ位置に既にあれば**足さずにそれを返す**（連打で重ならないように）。 */
    Marker addMarker (double timeSeconds, const juce::String& name, juce::UndoManager* undoManager);

    /** 8.139：**拍で足す**（Phase 177）。`Track::addNoteBeats()`と同じ扱いで、
        切り離されたツリーから戻すときはこちらを使うこと（`MusicalTime.h`）。 */
    Marker addMarkerBeats (double timeBeats, const juce::String& name, juce::UndoManager* undoManager);

    void removeMarker (const Marker& marker, juce::UndoManager* undoManager);

    /** その時刻より後にある、いちばん近いマーカー。無ければ無効なものを返す。 */
    Marker findMarkerAfter (double timeSeconds) const;

    /** その時刻より前にある、いちばん近いマーカー。無ければ無効なものを返す。 */
    Marker findMarkerBefore (double timeSeconds) const;

    /** 時刻の順に並べ直す（マーカーを動かした後に呼ぶ）。 */
    void sortMarkers (juce::UndoManager* undoManager);

    /** 番号付きの既定名（"Marker 3" のような）。名前を付けずに足すときに使う。 */
    juce::String getNextMarkerName() const;

    /** プロジェクトのテンポ（BPM）。クオンタイズのグリッド計算等で使う。 */
    double getTempo() const;
    void setTempo (double newTempo, juce::UndoManager* undoManager);

    /** 拍子（設計書1.3のtimeSignature）。"4/4"のような文字列で保持している。 */
    juce::String getTimeSignature() const;

    /** 仕様書5.1：拍子を変更する（Phase 26）。

        "4/4"のような形式でない文字列は無視する（ルーラーの小節線や
        グルーヴのパターン長がこの値から計算されるため、壊れた値を入れさせない）。
        受け付けた場合はtrueを返す。 */
    bool setTimeSignature (const juce::String& newTimeSignature, juce::UndoManager* undoManagerToUse);

    /** 拍子として受け付ける形か（"3/4"のような、分子32以下・分母は2の累乗）。

        **判定はここ1箇所**（Phase 145）。フッター・レーンの両方が通します——
        片方だけ緩いと、**レーンからは入るのにフッターからは入らない値**ができます
        （8.12の「入口が2つあるものは、片方だけ直して食い違う」）。 */
    static bool isValidTimeSignature (const juce::String& text);

    /** 1小節あたりの拍数。拍子の分子にあたる（"4/4"なら4、"3/4"なら3）。
        ルーラーの小節線の計算に使う（仕様書5.9）。
        拍子が読み取れない場合は4を返す。 */
    int getBeatsPerBar() const;

    //==========================================================================
    // 8.98：**「時刻 → 小節・拍」の換算は、ここに集める**
    //       （Phase 138／Jの10番目「拍子とキーのレーン」の土台）
    //
    // Phase 137まで、小節の長さは各所で `60.0 / getTempo() * getBeatsPerBar()` と
    // **直に書かれていました**（10ファイル・49箇所）。テンポと拍子が曲に1つの
    // あいだはそれで合いますが、**途中で変わる形にした瞬間に全部が間違いになります。**
    //
    // **いまの中身は「曲に1つのテンポ・拍子を読むだけ」です**（Phase 138では
    // 動きが1つも変わらないのが正しい）。変化点を持たせるとき（8.98の3）に、
    // **差し替えるのはこの下の実装だけ**になります。
    //
    // **どれも時刻を引数に取るのが肝**です。「いつの拍か」を呼び出し側に
    // 書かせないでおけば、中身を変えたときに呼び出し側を直さずに済みます。
    // 逆に「引数の無い getBarSeconds()」を足すと、**それが後で全部直す場所に
    // なります**——足さないこと。

    /** 時刻を小節・拍に分けたもの。**どれも0始まり**（画面に出すときだけ+1する）。 */
    struct BarBeat
    {
        int bar = 0;                 // 何小節目か（0始まり）
        int beat = 0;                // その小節の中で何拍目か（0始まり）
        double beatFraction = 0.0;   // 拍の中のどこか（0.0以上1.0未満）
    };

    /** その時刻のテンポ（BPM）。 */
    double getTempoAt (double timeSeconds) const;

    /** その時刻の拍子（"4/4"のような文字列）。 */
    juce::String getTimeSignatureAt (double timeSeconds) const;

    /** その時刻の1小節あたりの拍数（拍子の分子）。 */
    int getBeatsPerBarAt (double timeSeconds) const;

    /** その時刻の1拍の長さ（秒）。**0にはなりません**（テンポを1未満へ落とさないため）。 */
    double getBeatSecondsAt (double timeSeconds) const;

    /** その時刻を含む小節の長さ（秒）。 */
    double getBarSecondsAt (double timeSeconds) const;

    /** 時刻 → 小節・拍。 */
    BarBeat getBarBeatAt (double timeSeconds) const;

    /** その時刻が何小節目か（0始まり）。`getBarBeatAt().bar`と同じ。 */
    int getBarIndexAt (double timeSeconds) const;

    /** 小節番号（0始まり）の頭の時刻。**ルーラーはこれで位置を出すこと**——
        `小節番号 × 小節の長さ` と書くと、小節ごとに長さが違う形にしたときに崩れます。 */
    double getBarStartTime (int barIndex) const;

    /** 小節の中の拍（どちらも0始まり）の頭の時刻。 */
    double getBeatStartTime (int barIndex, int beatIndex) const;

    /** アレンジ画面でノートを塊に分ける「空き」の長さ（8.91）。

        **1小節ぶん**です。アレンジ画面とピアノロールが**同じ値**を使わないと、
        同じ曲が2つの画面で違う塊に見えます（8.2）。**決めるのはここだけ。** */
    double getNoteBlockGapSeconds() const;

    //==========================================================================
    // 仕様書5.1・5.9：**テンポと拍子の変化点**（Phase 140／8.98の3）
    //
    // 8.100・8.101で作った換算関数の**中身がここへ差し替わりました**。
    // 呼び出し側は1行も変わっていません——それが先に土台を作った理由です。
    //
    // **`getTempo()`／`getTimeSignature()`は「曲の頭の値」として残ります。**
    // 変化点が1つも無ければ、答えはPhase 139までと完全に同じです
    // （＝**古いプロジェクトの読み替えは要りません**。`<TEMPOMAP>`が無いだけ）。
    //
    // **位置はmusicalな座標で持ちます**（テンポは拍、拍子は小節）。
    // 秒で持つと、手前のテンポを変えただけで拍子の変わり目が小節の途中へずれ、
    // **「小節の途中で拍子が変わる」という、あってはならない状態**が作れます
    // （8.91の「表現できない状態は、作らせない」）。詳しくは`TempoMap.h`。

    /** いまの表。**画面もオーディオもここを読みます**（ValueTreeを直に辿らないこと）。

        中身はValueTreeから作った写しで、変更があれば作り直されます。
        **オーディオスレッドへ渡すときは、この値をコピーして持たせること**（1.12）。

        8.137：`TempoMapSource`の実装でもあります（Phase 175）。
        **ノート側から名前で引き当てられるようにするため**です（`MusicalTime.h`）。 */
    const TempoMap& getTempoMap() const override;

    /** 8.137：この表が受け持つプロジェクトの根（Phase 175）。`MusicalTime`が使います。 */
    juce::ValueTree getTempoMapRoot() const override { return state; }

    /** テンポの変化点を置く／上書きする。位置は**拍**（曲の頭から数えた拍数）。

        同じ拍に既にあれば、その値を書き換えます（2つ置けません）。
        **0拍目は置けません**——そこは`setTempo()`（曲の頭の値）の担当です。 */
    void setTempoChange (double beatPosition, double bpm, juce::UndoManager* undoManagerToUse);

    /** テンポの変化点を消す。無ければ何もしません。 */
    void removeTempoChange (double beatPosition, juce::UndoManager* undoManagerToUse);

    /** 拍子の変化点を置く／上書きする。位置は**小節**（0始まり）。

        **小節にしか置けません**（上の説明）。`"3/4"`のような形でない文字列は
        受け付けず、falseを返します（`setTimeSignature()`と同じ判定）。
        **0小節目は置けません**——そこは`setTimeSignature()`の担当です。 */
    bool setTimeSignatureChange (int bar, const juce::String& newTimeSignature,
                                  juce::UndoManager* undoManagerToUse);

    /** 拍子の変化点を消す。無ければ何もしません。 */
    void removeTimeSignatureChange (int bar, juce::UndoManager* undoManagerToUse);

    //==========================================================================
    // 8.98：**拍の座標**（Phase 139）
    //
    // 「曲の頭から数えて何拍目か」を小数で表したものです。**寄せる・刻むは、
    // 秒ではなくこれで行います**——秒で刻んだ目盛りは、テンポが途中で変わった
    // 先で拍から外れるためです（`snapGridBeats()`の説明も参照）。
    //
    // **この2つは必ず対で使うこと。** 片方だけ通して、もう片方を掛け算で
    // 書くと、変化点をまたいだところで往復しなくなります。

    /** その時刻が曲の頭から何拍目か（小数）。 */
    double getBeatPositionAt (double timeSeconds) const;

    /** 拍の位置（小数）を時刻へ戻す。`getBeatPositionAt()`の逆。 */
    double getTimeForBeatPosition (double beatPosition) const;

    //==========================================================================
    // 仕様書5.5・5.9：編集の刻み（スナップ、Phase 54／8.1のB1）
    //
    // **時刻を決める操作は、すべてここを通すこと。** クリップ・ノート・
    // コード区間・ループ・マーカー・再生カーソルのどれも同じ関数を呼びます。
    // 各所で「拍に寄せる」「小節に寄せる」を直接書くと、設定を足しても
    // 効かない場所が残ります（それがPhase 53までの状態でした。8.14）。
    //
    // **プロジェクトに保存します**（ループ範囲と同じ扱い）。刻みは曲ごとの
    // 作り方に紐づくもので、開き直したときに戻っていてほしいため。

    SnapGrid getSnapGrid() const;

    /** 刻みを変える。**Undoには積みません**（編集内容ではなく、編集のしかたのため）。
        ただしプロジェクトファイルには入るので、未保存の変更としては数えます。 */
    void setSnapGrid (SnapGrid newGrid);

    /** その時刻での刻み1つぶんの長さ（秒）。**フリーなら0**。

        **「マス1つの大きさ」が欲しいときだけ使うこと**（ノートの既定の長さ、
        ループ範囲の最短）。**寄せるのに使ってはいけません**——寄せ先は
        `snapTime()`が拍の座標で決めます（8.98／Phase 139）。 */
    double getSnapSecondsAt (double atTime) const;

    /** いちばん近い目盛りへ寄せる。**動かす操作（ドラッグ）はこれ。**
        フリーのときは値をそのまま返すので、呼び出し側に分岐は要りません。
        0秒より手前へは行きません。 */
    double snapTime (double seconds) const;

    /** 手前の目盛りへ寄せる（切り捨て）。**作る操作はこれ。**
        クリックしたマスの頭から作りたいので、丸めると
        「マスの後ろ半分をクリックすると次のマスにできる」ことになります。 */
    double snapTimeDown (double seconds) const;

    // **`snapLength()`はPhase 139で消しました。** どこからも呼ばれていない上に、
    // 「長さを秒の目盛りで丸める」形はテンポが途中で変わると成り立ちません
    // （長さは**どこから測るか**で変わるため）。要るときは拍の座標で作り直すこと。

    /** トラックを足す。

        8.60：`afterTrackId`を渡すと、**そのトラックの真下**へ入る（Phase 97／改善案⑱）。
        空文字か、見つからないときは末尾。

        入れ先のフォルダも`afterTrackId`から決める：

        - **フォルダを指したら、そのフォルダの中の先頭**へ（「ここに足したい」の素直な解釈）
        - それ以外なら、**指したトラックと同じフォルダの中**へ（隣に並ぶ）

        **畳んでいるフォルダは開く。** 足したトラックが見えないと、
        押しても何も起きなかったように見えるため。

        コードトラックだけは例外で、**常に先頭**（仕様書5.2.3。`ensureChordTrackIsFirst()`）。 */
    Track addTrack (const juce::String& name, TrackType type, const juce::String& afterTrackId = {});

    /** トラックを削除する（Phase 33）。

        **他のトラックが持っている、このトラックへの参照も同時に外す。**
        外し忘れると、センドの送り先・VCAのリンク・サイドチェインのソースが
        「もう無いトラック」を指したまま残り、センドは「(不明な送り先)」になり、
        VCAのリンク本数には幽霊が数えられる（HANDOVER 1.18と同じ性質の話）。 */
    void removeTrack (const Track& track);

    /** トラックを複製する（Phase 70／8.29の表）。

        **IDは全部振り直す**（トラック・クリップ・コード区間）。
        同じIDのものが2つあると、片方を消したつもりが両方に効きます（1.32）。
        音源とインサートは`<INSTRUMENT>`/`<INSERTS>`ごとコピーされ、
        エンジンが作り直すときにモデルから読み直します（設計書3.8）。

        コードトラックは複製しません（1本だけの固定行。8.20）。
        できなかった場合は`state.isValid()`がfalseのTrackを返します。 */
    Track duplicateTrack (const Track& source);

    //==========================================================================
    // 仕様書5.2.3：コードトラックは一番上に固定（Phase 60／8.1の追加要望。8.20）
    //
    // アレンジ画面では**ルーラーの真下に固定表示**され、縦スクロールしても消えません。
    // 小節バーと同じ扱いにするための決まりで、モデル側では
    // 「先頭の子である」ことだけを守ります。

    /** 先頭がコードトラックか（＝固定行がある）。 */
    bool hasPinnedChordTrack() const;

    /** コードトラックが先頭でなければ先頭へ移す。

        **読み込み直後と、コードトラックを足した直後に呼ぶ。**
        Undoには積みません（並びを直したこと自体を戻せても意味がないため）。 */
    void ensureChordTrackIsFirst();

    /** トラックの並び順を入れ替える（Phase 33）。

        アレンジ画面もConsoleも**同じモデルの並び順**を見ているので、両方に効く。
        `toIndex`は移動後に置きたい位置（0が先頭）。

        **1回の呼び出しがUndoの1ステップになる。** ドラッグでの並べ替え（Phase 36）は
        ドロップ時に1回だけ呼ぶので、「1マスぶんのUndo」が積まれることはない。 */
    void moveTrack (int fromIndex, int toIndex);

    /** 8.51：**動かすのと入れ先（フォルダ）を1回の操作で**行う（Phase 90／D2）。

        アレンジ画面のヘッダーのドラッグはこちらを通る。
        **フォルダを動かすときは中身も連れていく**（置いていくと、
        入っているのに離れた場所にいる状態になる）。 */
    void moveTrackToSlot (const Track& track, int toIndex, const juce::String& newParentFolderId);

    /** そのフォルダの中にいるトラックのID（孫まで。**並び順のまま**）。 */
    juce::StringArray getFolderDescendantIds (const juce::String& folderId) const;

    //==========================================================================
    // 8.50：フォルダトラック（Phase 89／D2。仕様書5.2・設計書1.3）

    /** 何階層目にいるか（0＝一番上）。 */
    int getTrackFolderDepth (const Track& track) const;

    /** **畳んでいるフォルダの中にいるか**（孫まで効く）。
        画面はこれを見て「行の高さ0＝居ないのと同じ」にする（8.50）。 */
    bool isTrackHiddenByCollapsedFolder (const Track& track) const;

    /** そのフォルダへ入れてよいか。**自分自身と自分の子孫は行き先にできない**
        （輪になると、たどって戻ってこなくなる）。 */
    bool canMoveTrackIntoFolder (const Track& track, const juce::String& folderId) const;

    /** そのフォルダの中身の、いちばん下の行番号（フォルダ自身しか無ければフォルダの行）。 */
    int getLastRowOfFolder (const juce::String& folderId) const;

    /** フォルダへ入れる（`folderId`が空なら出す）。
        **入れるときは、そのフォルダの中身の最後尾へ並べ直す。** */
    void moveTrackIntoFolder (const Track& track, const juce::String& folderId);

    /** フォルダの入れ子の深さの上限。**たどる回数を区切るためのもの**で、
        ここまで深くできるという意味ではない。 */
    static constexpr int maxFolderDepth = 16;

    int getNumTracks() const;
    Track getTrack (int index) const;

    /** IDでトラックを引く。見つからない場合は`state.getParent().isValid()`がfalseのものを返す。 */
    Track findTrackById (const juce::String& trackId) const;

    //==========================================================================
    // 仕様書5.2.3・5.11：コードトラック

    /** 最初のコードトラック。1本も無ければ`state.isValid()`がfalseのものを返す。

        仕様書5.2.3は「1プロジェクトにつき基本1本」なので、**先頭の1本を
        プロジェクトのコードトラックとして扱います**。2本以上作れてしまうこと自体は
        止めていません（止めるとフォルダ内へ移す等の操作が窮屈になる）。 */
    Track findChordTrack() const;

    /** プロジェクトのキー（仕様書5.11.1）。コードトラックが無ければCメジャー。

        コードパッドもピアノロールの色分け（5.3.1）もここを通すこと。
        「どのトラックのキーか」を各所で探し始めると、必ずずれます。 */
    Scale getProjectKey() const;

    /** プロジェクトのキーを変える（Phase 63／8.1のC9）。

        **入口が2つあります**：コードパッドの上段と、トランスポートバーのBPMの左。
        どちらもここを通すので、片方で変えればもう片方の表示も揃います（1.27）。

        **コードトラックが無ければ何もせず false を返します。**
        キーの置き場所はコードトラックなので、ここで勝手に作ると
        「BPMの隣を触っただけでトラックが増える」ことになります。 */
    bool setProjectKey (const Scale& newKey, juce::UndoManager* undoManager);

    //==========================================================================
    // 仕様書5.11.1：**キーの変化点**（Phase 143／改善案㉔㉕。8.105）
    //
    // 8.102のテンポ・拍子と**同じ形**です：`getProjectKey()`は「曲の頭のキー」として
    // そのまま残し、変化点だけを`<KEYMAP>`に足します。
    // **変化点が1つも無ければ、答えはPhase 142までと完全に同じ**です
    // （＝古いプロジェクトの読み替えは要りません）。
    //
    // **位置は小節**です（転調は小節の頭で起こる。`KeyMap.h`）。

    /** いまのキーの表。**画面はここを読みます**（ValueTreeを直に辿らないこと）。 */
    const KeyMap& getKeyMap() const;

    /** その時刻で効いているキー。

        **キーを読む側は、原則こちらを使うこと。** `getProjectKey()`（引数なし）は
        「曲の頭のキー」なので、**フッターとコードパッドのKey欄のように
        「曲の設定」を出す場所だけ**が使います（8.106）。 */
    Scale getProjectKeyAt (double timeSeconds) const;

    /** キーの変化点を置く／上書きする。位置は**小節**（0始まり）。

        **0小節目は置けません**——そこは`setProjectKey()`（曲の頭のキー）の担当です。
        **コードトラックが無ければ何もせず false を返します**（`setProjectKey()`と同じ決まり。
        キーの置き場所はコードトラックなので、片方だけ緩くしない）。 */
    bool setKeyChange (int bar, const Scale& newKey, juce::UndoManager* undoManagerToUse);

    /** キーの変化点を消す。無ければ何もしません。 */
    void removeKeyChange (int bar, juce::UndoManager* undoManagerToUse);

    //==========================================================================
    // 仕様書5.2.4：VCAトラック（Phase 12d-2）

    /** 仕様書5.2.4：あるトラックに効いているVCAの影響を、まとめて1つにして返す。

        VCAの扱い（オフセットの求め方、ミュート／ソロの解釈）を**この1箇所に集約**している。
        同じ判定をエンジンとUIに別々に書くと必ずずれるため（HANDOVER 8.2）、
        ミキサー反映・ステム書き出し・ストリップ表示のいずれもここを通すこと。 */
    struct VcaInfluence
    {
        bool linked = false;   // どれかのVCAにリンクされているか
        float offsetDb = 0.0f; // 最終ゲインへ加算するdB（設計書1.3）
        bool muted = false;    // VCA側がミュート、またはVCAのフェーダーが下端
        bool soloed = false;   // VCA側がソロ
    };

    VcaInfluence getVcaInfluenceFor (const juce::String& trackId) const;

    /** 8.51：**入っているフォルダから受ける影響**（Phase 90／D2）。VCAと同じ形。

        親をたどって、**1つでもミュートされていれば`muted`**、
        **1つでもソロなら`soloed`**。孫まで効く。 */
    struct FolderInfluence
    {
        bool muted = false;
        bool soloed = false;
    };

    FolderInfluence getFolderInfluenceFor (const Track& track) const;

    /** そのトラックがリンクされているVCAトラック。無ければ無効なTrackを返す。 */
    Track findVcaForTrack (const juce::String& trackId) const;

    /** トラックをVCAへ割り当てる（vcaTrackIdが空文字なら解除）。

        1トラックにつきVCAは1つまでなので、他のVCAにリンクが残っていれば先に外す。
        Undoの区切りはこの中で作る。 */
    void assignTrackToVca (const juce::String& trackId, const juce::String& vcaTrackId);

    /** 仕様書5.2.4：VCAでソロにされているトラックが1本でもあるか（ソロ判定に使う）。 */
    bool isAnyVcaSoloed() const;

    //==========================================================================
    // 仕様書5.2.1：ミュートとソロ（Phase 53で判定をここへ集約）

    /** ソロが1つでも立っているか。**VCAのソロも数える**（VCA自身は音を通さないので、
        トラックを見て回るだけでは拾えない）。 */
    bool isAnySoloActive() const;

    /** そのトラックが「今、音として出る」か。

        **ミュート／ソロ／VCAの解釈をこの1箇所に集約している。**
        ミキサー（`AudioEngine::updateMixerSettings`）もMIDI書き出し
        （`MidiFileExporter`）もここを通すこと。同じ判定を書き写すと、
        「鳴っている内容」と「書き出した内容」が必ず食い違う（HANDOVER 8.2）。

        決まりごとは3つ：
        - **ミュートはソロより優先**（ソロ中のトラックをミュートしたら黙る）
        - **VCAのミュートは、リンク先のミュートと同じ扱い**（フェーダー下端も含む。1.20）
        - **センドトラックはソロの対象外**（ソロ中のトラックの残響が突然消えると
          不自然なため。一般的なDAWと同じ扱い）

        ソロが立っているかを何度も数え直さずに済むよう、`anySoloActive`を
        外から渡せるようにしてある（省略すると毎回数える）。 */
    bool isTrackAudible (const Track& track) const;
    bool isTrackAudible (const Track& track, bool anySoloActive) const;

    //==========================================================================
    // 仕様書5.3.4：グルーヴテンプレート（Phase 24）
    //
    // 置き場所はプロジェクト直下の`<GROOVETEMPLATES>`。クリップではなくプロジェクトに
    // 持たせるのは、**抽出したグルーヴを他のクリップへ使い回す**のが目的だから
    // （仕様書5.3.4「プロジェクト内で保存・再利用」）。
    //
    // （検討事項）テンプレートファイルとして外へ書き出すかは仕様書10.7で継続検討。
    // 現状はプロジェクト内保存のみ。

    //==========================================================================
    // 仕様書5.3.2：ドラムマップ（Phase 25）

    /** General MIDI準拠の既定マップを返す（無ければ作る）。

        仕様書10.7の「GM準拠のデフォルトマップ」に相当する。
        ユーザー定義マップの管理は10.7で継続検討のため、現状はこの1つだけ。 */
    DrumMap getOrCreateDefaultDrumMap();

    /** IDで引く。無ければ`state.isValid()`がfalseのものを返す。 */
    DrumMap findDrumMap (const juce::String& mapId) const;

    /** そのトラックに割り当てられているドラムマップ。
        未割り当てなら`state.isValid()`がfalseのものを返す
        （＝ドラムのミュート／チョークは効かず、従来どおりの挙動になる）。 */
    DrumMap getDrumMapForTrack (const Track& track) const;

    juce::ValueTree getOrCreateGrooveTemplatesNode();

    int getNumGrooveTemplates() const;
    GrooveTemplate getGrooveTemplate (int index) const;

    /** IDで引く。無ければ`state.isValid()`がfalseのものを返す。 */
    GrooveTemplate findGrooveTemplate (const juce::String& templateId) const;

    /** 空のテンプレートを作って登録する（点はGrooveQuantise側が詰める）。 */
    GrooveTemplate addGrooveTemplate (const juce::String& name, int gridDivision,
                                       int patternLength, double sourceTempo,
                                       juce::UndoManager* undoManager);

    void removeGrooveTemplate (int index, juce::UndoManager* undoManager);

    /** 設計書1.7：ValueTree::createXml()相当でXML保存する。
        成功すると、そのファイルが「現在編集中のファイル」になり、未保存フラグが下りる。
        既存ファイルを上書きする場合は、事前に `.bak` へ退避する（仕様書5.1）。 */
    bool saveToFile (const juce::File& file);

    /** 今の内容を指定ファイルへ書き出すだけ（「編集中のファイル」も未保存フラグも変えない）。
        仕様書5.1のオートセーブのように、裏で別ファイルへ書く用途に使う。 */
    bool writeCopyToFile (const juce::File& file) const;

    /** 設計書1.7：XMLを読み込んでValueTreeを復元する。 */
    bool loadFromFile (const juce::File& file);

    /** 8.151：**テンプレートから作った直後の状態にする**（Phase 189／8.1のD8）。

        - **編集中のファイルを外す**（そのまま保存すると「名前を付けて保存」へ回るので、
          テンプレート本体を上書きしません）
        - Undoの履歴を捨てる（テンプレートの組み立て手順を巻き戻せても意味が無い）
        - 「未保存の変更」を下ろす（何も触っていないのにタイトルへ「*」が付かないように）

        `createNewProject()`の後半と同じことをしています。
        **組み立ててから呼ぶこと**——`addTrack()`は変更として数えられます。 */
    void markAsNewFromTemplate();

private:
    // ValueTreeへの変更を購読して「未保存の変更あり」を検出する。
    // ValueTree::Listenerは子孫の変更も通知してくれるため、ルートを1つ購読するだけで
    // トラック・クリップ・ノートまで含めた全変更を拾える。
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markAsChanged(); markDerivedMapsDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { markAsChanged(); markDerivedMapsDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { markAsChanged(); markDerivedMapsDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override              { markAsChanged(); markDerivedMapsDirty(); }
    void valueTreeParentChanged (juce::ValueTree&) override                            { markAsChanged(); markDerivedMapsDirty(); }

    /** 8.102：テンポとキーの表を作り直す印。**変更の種類で選り分けていません**——
        「ここは関係ない」という判断を1つ間違えると、**古い表のまま画面が描かれます**。
        作り直しは変化点の数ぶんの足し算なので、毎回捨てても安いほうを取っています。 */
    void markDerivedMapsDirty() { tempoMapDirty = true; keyMapDirty = true; }

    void markAsChanged();
    void markAsSaved();

    /** ルートのValueTreeを差し替える（リスナーの付け替えも行う）。 */
    void setState (juce::ValueTree newState);

    /** `<PLUGINS>`ノードを返す。古いプロジェクトファイルには無いため、必要なら作る。 */
    juce::ValueTree getOrCreatePluginsNode();

    juce::ValueTree state;
    juce::ValueTree tracksNode;
    juce::UndoManager undoManager;

    juce::File currentFile;
    bool unsavedChanges = false;

    // 8.102：テンポと拍子の表（`TempoMap.h`）。**ValueTreeから作った写し**で、
    // 変更があれば次に訊かれたときに作り直す。`const`の関数から作り直すのでmutable
    mutable TempoMap tempoMap;
    mutable bool tempoMapDirty = true;

    // 8.106：キーの表（`KeyMap.h`）。テンポと同じ扱いだが、**別の表**にしてある——
    // `TempoMap`はオーディオスレッドが読むので、音に関係ないキーを混ぜない
    mutable KeyMap keyMap;
    mutable bool keyMapDirty = true;
};
