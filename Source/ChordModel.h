#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <utility>
#include <cmath>   // 8.121：midiNoteNameの床割り（Phase 156）

//==============================================================================
// 仕様書5.11.1「コアデータモデル」。ChordCanvasの ChordTypes.h / Scale.h を
// 移植したもの（HANDOVER 8.5）。
//
// このファイルはデータと純粋な計算だけを持ち、UIにもオーディオにも依存しない。
// 依存しているのは juce_core とSTLだけなので、どのレイヤーからでもincludeできる。
//
//   ChordModel.h  … Chord・Scale（このファイル）
//   ChordEngine.h … スコアリングとボイシング（設計書1.2、5.11.2〜5.11.3）
//   ChordTrackModel.h … ChordRegion（ValueTreeに載るものはこちらへ置くこと）
//
// 移植元との違い：
//  - ChordCanvasの `ProjectSettings`（キーと編集カーソルの保持）は持ち込んでいない。
//    このプロジェクトではキーはコードトラックの属性（設計書1.3のChordTrack）で、
//    置き場所がValueTree側になるため。
//  - `GridConfig.h`（32小節・C1〜C7固定）も持ち込まない。タイムラインはズームも
//    スクロールもするので、固定値を前提にした計算を混ぜないこと（HANDOVER 8.5）。
//==============================================================================

//==============================================================================
/** 構成音が「何度」に当たるか。仕様書5.3.1の構成音カラーリングでも使う。 */
enum class ChordDegree
{
    Root,        // 1度
    Third,       // 3度
    Fourth,      // 4度（sus4）
    Fifth,       // 5度
    Sixth,       // 6度
    Seventh,     // 7度
    Ninth,       // 9度
    Eleventh,    // 11度
    Thirteenth   // 13度
};

//==============================================================================
/** 仕様書5.11.1の対応表にある全22種のコードタイプ。

    **並び順は保存データの互換性に直結する。** ValueTreeへは数値で書き出すため、
    途中に挿入すると既存プロジェクトのコードが別のコードとして読み戻される。
    追加するときは必ず `Sus2` の後ろ（`NumTypes` の直前）に足すこと。
*/
enum class ChordType
{
    Maj = 0,      // （無印）
    Aug,          // aug
    Sus4,         // sus4
    Min,          // m
    Dom7,         // 7
    Maj7,         // M7
    Min7,         // m7
    Min7Flat5,    // m7b5
    Six,          // 6
    Aug7,         // aug7
    Dom7Sus4,     // 7sus4
    Min6,         // m6
    MinMaj7,      // mM7
    Dim,          // dim
    AugMaj7,      // M7+5
    MinFlat5,     // mb5
    Dom7Flat5,    // 7b5
    Flat5,        // b5
    Add9,         // add9
    MinAdd9,      // madd9
    Dim7,         // dim7
    Sus2,         // sus2 ※新しいタイプは必ずここより後ろに追加する
    NumTypes
};

//==============================================================================
/** テンション（仕様書5.11.1の7種）。ビットマスクで複数同時に持てる。 */
namespace Tension
{
    constexpr int Nine         = 1 << 0;  // 9
    constexpr int FlatNine     = 1 << 1;  // b9
    constexpr int SharpNine    = 1 << 2;  // #9
    constexpr int Eleven       = 1 << 3;  // 11
    constexpr int SharpEleven  = 1 << 4;  // #11
    constexpr int Thirteen     = 1 << 5;  // 13
    constexpr int FlatThirteen = 1 << 6;  // b13
}

//==============================================================================
/** ピッチクラス（0=C, 1=C#, ... 11=B）の表示名。負の値や12以上でも折り返す。 */
inline juce::String pitchClassName (int pitchClass)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    return names[((pitchClass % 12) + 12) % 12];
}

/** 8.121：MIDIノート番号を**音名＋オクターブ**で表す（"C4"、"F#3"）。Phase 156。

    **C4＝60の流儀**です（本人の指定）。C3＝60と呼ぶ流儀もあり、
    どちらも「間違い」ではありませんが、**混ぜると1オクターブずれます**。

    **音名を出すところは全部ここを通すこと。** 鍵盤の見出し（`getKeyboardLabel()`）、
    ノートの中の文字、ドラッグ中の表示が、別々に数えていると必ず食い違います（1.27）。
*/
inline juce::String midiNoteName (int midiNote)
{
    // **切り捨ての向きに注意。** `midiNote / 12`は負で0へ寄るので、
    // 負のノート番号ではオクターブが1つずれる（MIDIは0〜127だが、
    // 移調の途中で負を渡し得るので、床で割っておく）
    const int octave = (int) std::floor ((double) midiNote / 12.0) - 1;

    return pitchClassName (midiNote) + juce::String (octave);
}

/** コードタイプの表示用サフィックス（Majは無印なので空文字）。 */
inline juce::String chordTypeSuffix (ChordType type)
{
    switch (type)
    {
        case ChordType::Maj:        return "";
        case ChordType::Aug:        return "aug";
        case ChordType::Sus4:       return "sus4";
        case ChordType::Min:        return "m";
        case ChordType::Dom7:       return "7";
        case ChordType::Maj7:       return "M7";
        case ChordType::Min7:       return "m7";
        case ChordType::Min7Flat5:  return "m7b5";
        case ChordType::Six:        return "6";
        case ChordType::Aug7:       return "aug7";
        case ChordType::Dom7Sus4:   return "7sus4";
        case ChordType::Min6:       return "m6";
        case ChordType::MinMaj7:    return "mM7";
        case ChordType::Dim:        return "dim";
        case ChordType::AugMaj7:    return "M7+5";
        case ChordType::MinFlat5:   return "mb5";
        case ChordType::Dom7Flat5:  return "7b5";
        case ChordType::Flat5:      return "b5";
        case ChordType::Add9:       return "add9";
        case ChordType::MinAdd9:    return "madd9";
        case ChordType::Dim7:       return "dim7";
        case ChordType::Sus2:       return "sus2";
        default:                    return "";
    }
}

/** コードタイプごとの構成音（ルートからの半音数と、その度数）。

    返すのは静的なテーブルへの参照なので、呼ぶたびのコピーは発生しない。
    add9系の並びが昇順になっていないのは移植元のままで、ボイシング側（5.11.3）が
    並び順に依存しない作りになっているため。
*/
inline const std::vector<std::pair<int, ChordDegree>>& chordTypeIntervals (ChordType type)
{
    using D = ChordDegree;
    using V = std::vector<std::pair<int, D>>;

    static const V maj       { {0,D::Root}, {4,D::Third},  {7,D::Fifth} };
    static const V aug       { {0,D::Root}, {4,D::Third},  {8,D::Fifth} };
    static const V sus4      { {0,D::Root}, {5,D::Fourth}, {7,D::Fifth} };
    static const V min       { {0,D::Root}, {3,D::Third},  {7,D::Fifth} };
    static const V dom7      { {0,D::Root}, {4,D::Third},  {7,D::Fifth}, {10,D::Seventh} };
    static const V maj7      { {0,D::Root}, {4,D::Third},  {7,D::Fifth}, {11,D::Seventh} };
    static const V min7      { {0,D::Root}, {3,D::Third},  {7,D::Fifth}, {10,D::Seventh} };
    static const V min7b5    { {0,D::Root}, {3,D::Third},  {6,D::Fifth}, {10,D::Seventh} };
    static const V six       { {0,D::Root}, {4,D::Third},  {7,D::Fifth}, {9,D::Sixth} };
    static const V aug7      { {0,D::Root}, {4,D::Third},  {8,D::Fifth}, {10,D::Seventh} };
    static const V dom7sus4  { {0,D::Root}, {5,D::Fourth}, {7,D::Fifth}, {10,D::Seventh} };
    static const V min6      { {0,D::Root}, {3,D::Third},  {7,D::Fifth}, {9,D::Sixth} };
    static const V minMaj7   { {0,D::Root}, {3,D::Third},  {7,D::Fifth}, {11,D::Seventh} };
    static const V dim       { {0,D::Root}, {3,D::Third},  {6,D::Fifth} };
    static const V augMaj7   { {0,D::Root}, {4,D::Third},  {8,D::Fifth}, {11,D::Seventh} };
    static const V minb5     { {0,D::Root}, {3,D::Third},  {6,D::Fifth} };
    static const V dom7b5    { {0,D::Root}, {4,D::Third},  {6,D::Fifth}, {10,D::Seventh} };
    static const V b5        { {0,D::Root}, {4,D::Third},  {6,D::Fifth} };
    static const V add9      { {0,D::Root}, {4,D::Third},  {7,D::Fifth}, {2,D::Ninth} };
    static const V madd9     { {0,D::Root}, {3,D::Third},  {7,D::Fifth}, {2,D::Ninth} };
    static const V dim7      { {0,D::Root}, {3,D::Third},  {6,D::Fifth}, {9,D::Seventh} };
    static const V sus2      { {0,D::Root}, {2,D::Ninth},  {7,D::Fifth} };

    switch (type)
    {
        case ChordType::Maj:        return maj;
        case ChordType::Aug:        return aug;
        case ChordType::Sus4:       return sus4;
        case ChordType::Min:        return min;
        case ChordType::Dom7:       return dom7;
        case ChordType::Maj7:       return maj7;
        case ChordType::Min7:       return min7;
        case ChordType::Min7Flat5:  return min7b5;
        case ChordType::Six:        return six;
        case ChordType::Aug7:       return aug7;
        case ChordType::Dom7Sus4:   return dom7sus4;
        case ChordType::Min6:       return min6;
        case ChordType::MinMaj7:    return minMaj7;
        case ChordType::Dim:        return dim;
        case ChordType::AugMaj7:    return augMaj7;
        case ChordType::MinFlat5:   return minb5;
        case ChordType::Dom7Flat5:  return dom7b5;
        case ChordType::Flat5:      return b5;
        case ChordType::Add9:       return add9;
        case ChordType::MinAdd9:    return madd9;
        case ChordType::Dim7:       return dim7;
        case ChordType::Sus2:       return sus2;
        default:                    return maj;
    }
}

//==============================================================================
/** 設計書1.3のChord。ルート＋タイプ＋テンション＋ベース（分数コード）。

    ValueTreeのラッパー（3.2）ではなく素の値型にしてある。コードパッドは
    1操作ごとに11カテゴリ×12列ぶんの候補をスコアリングするので、
    その計算で毎回ValueTreeを触らせないため。保存するときだけ
    `ChordRegion`（設計書1.3。`ChordTrackModel.h`）がプロパティへ展開する。
*/
struct Chord
{
    int       root        = 0;               // 0=C ... 11=B
    ChordType type        = ChordType::Maj;
    int       tensionMask = 0;               // Tension::～ のビットの組み合わせ
    int       bass        = -1;              // -1 = 指定なし、0-11 = 分数コードのベース音

    /** 表示名（例："C#m7(9)/E"）。 */
    juce::String getName() const
    {
        juce::String name = pitchClassName (root) + chordTypeSuffix (type);

        juce::StringArray tensions;
        if (tensionMask & Tension::Nine)         tensions.add ("9");
        if (tensionMask & Tension::FlatNine)     tensions.add ("b9");
        if (tensionMask & Tension::SharpNine)    tensions.add ("#9");
        if (tensionMask & Tension::Eleven)       tensions.add ("11");
        if (tensionMask & Tension::SharpEleven)  tensions.add ("#11");
        if (tensionMask & Tension::Thirteen)     tensions.add ("13");
        if (tensionMask & Tension::FlatThirteen) tensions.add ("b13");

        if (! tensions.isEmpty())
            name += "(" + tensions.joinIntoString (",") + ")";

        if (bass >= 0 && bass != root)
            name += "/" + pitchClassName (bass);

        return name;
    }

    /** 構成音の一覧（ピッチクラスと度数のペア）。テンションも含む。

        返るのはピッチクラス（0〜11）であって、MIDIノート番号ではない。
        実際に鳴らす高さを決めるのはボイシング側（5.11.3）の仕事。
    */
    std::vector<std::pair<int, ChordDegree>> getTones() const
    {
        std::vector<std::pair<int, ChordDegree>> out;

        for (const auto& [semitones, degree] : chordTypeIntervals (type))
            out.push_back ({ (root + semitones) % 12, degree });

        struct TensionEntry { int bit; int semitones; ChordDegree degree; };
        static const TensionEntry tensionTable[] =
        {
            { Tension::Nine,         2, ChordDegree::Ninth },
            { Tension::FlatNine,     1, ChordDegree::Ninth },
            { Tension::SharpNine,    3, ChordDegree::Ninth },
            { Tension::Eleven,       5, ChordDegree::Eleventh },
            { Tension::SharpEleven,  6, ChordDegree::Eleventh },
            { Tension::Thirteen,     9, ChordDegree::Thirteenth },
            { Tension::FlatThirteen, 8, ChordDegree::Thirteenth },
        };

        for (const auto& entry : tensionTable)
            if (tensionMask & entry.bit)
                out.push_back ({ (root + entry.semitones) % 12, entry.degree });

        return out;
    }

    bool operator== (const Chord& other) const
    {
        return root == other.root && type == other.type
            && tensionMask == other.tensionMask && bass == other.bass;
    }

    bool operator!= (const Chord& other) const { return ! (*this == other); }
};

//==============================================================================
/** 仕様書5.11.1のキー。ルート音＋メジャー／ナチュラルマイナー。 */
struct Scale
{
    int  root  = 0;       // 0=C ... 11=B
    bool minor = false;   // false=メジャー、true=マイナー（ナチュラル。ただしVはV7）

    /** 度数（0=I ... 6=VII）ごとのルート音（ピッチクラス）。 */
    int degreeRoot (int degree) const
    {
        static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int* steps = minor ? minorSteps : majorSteps;
        return (root + steps[((degree % 7) + 7) % 7]) % 12;
    }

    /** そのピッチクラスがスケール内の音か。 */
    bool contains (int pitchClass) const
    {
        pitchClass = ((pitchClass % 12) + 12) % 12;

        for (int i = 0; i < 7; ++i)
            if (degreeRoot (i) == pitchClass)
                return true;

        return false;
    }

    /** 度数ごとのダイアトニック7thコードのタイプ。 */
    ChordType diatonicSeventh (int degree) const
    {
        static const ChordType majorTypes[7] =
        {
            ChordType::Maj7, ChordType::Min7, ChordType::Min7,
            ChordType::Maj7, ChordType::Dom7, ChordType::Min7,
            ChordType::Min7Flat5
        };
        // マイナーはナチュラルマイナー基準。Vだけは実用性を優先してドミナント7th
        static const ChordType minorTypes[7] =
        {
            ChordType::Min7, ChordType::Min7Flat5, ChordType::Maj7,
            ChordType::Min7, ChordType::Dom7, ChordType::Maj7,
            ChordType::Dom7
        };
        return (minor ? minorTypes : majorTypes)[((degree % 7) + 7) % 7];
    }

    /** 度数ごとのダイアトニックトライアドのタイプ。 */
    ChordType diatonicTriad (int degree) const
    {
        static const ChordType majorTypes[7] =
        {
            ChordType::Maj, ChordType::Min, ChordType::Min,
            ChordType::Maj, ChordType::Maj, ChordType::Min,
            ChordType::Dim
        };
        static const ChordType minorTypes[7] =
        {
            ChordType::Min, ChordType::Dim, ChordType::Maj,
            ChordType::Min, ChordType::Maj, ChordType::Maj,
            ChordType::Maj
        };
        return (minor ? minorTypes : majorTypes)[((degree % 7) + 7) % 7];
    }

    /** 表示名（例："A Minor"）。 */
    juce::String getName() const
    {
        return pitchClassName (root) + juce::String (minor ? " Minor" : " Major");
    }

    bool operator== (const Scale& other) const
    {
        return root == other.root && minor == other.minor;
    }

    bool operator!= (const Scale& other) const { return ! (*this == other); }
};

//==============================================================================
/** スケールファミリー。仕様書5.11.2の「Related（近親調）」ページで使う。

    Scale が持つのはメジャー／ナチュラルマイナーの2つだけなので、
    ハーモニックマイナー・メロディックマイナーの度数表はこちらに置いてある。
*/
enum class ScaleFamily { major = 0, naturalMinor, harmonicMinor, melodicMinor };

/** ファミリーごとの、ルートからの半音数（7音）。 */
inline const int* scaleFamilySteps (ScaleFamily family)
{
    static const int majorSteps[7]         = { 0, 2, 4, 5, 7, 9, 11 };
    static const int naturalMinorSteps[7]  = { 0, 2, 3, 5, 7, 8, 10 };
    static const int harmonicMinorSteps[7] = { 0, 2, 3, 5, 7, 8, 11 };
    static const int melodicMinorSteps[7]  = { 0, 2, 3, 5, 7, 9, 11 };  // 上行形

    switch (family)
    {
        case ScaleFamily::naturalMinor:  return naturalMinorSteps;
        case ScaleFamily::harmonicMinor: return harmonicMinorSteps;
        case ScaleFamily::melodicMinor:  return melodicMinorSteps;
        default:                         return majorSteps;
    }
}

/** ファミリーと度数から、ダイアトニック7thコードのタイプ。 */
inline ChordType scaleFamilySeventh (ScaleFamily family, int degree)
{
    using CT = ChordType;
    static const CT M[7]  = { CT::Maj7,    CT::Min7,      CT::Min7,    CT::Maj7, CT::Dom7, CT::Min7,      CT::Min7Flat5 };
    static const CT NM[7] = { CT::Min7,    CT::Min7Flat5, CT::Maj7,    CT::Min7, CT::Min7, CT::Maj7,      CT::Dom7 };
    static const CT HM[7] = { CT::MinMaj7, CT::Min7Flat5, CT::AugMaj7, CT::Min7, CT::Dom7, CT::Maj7,      CT::Dim7 };
    static const CT MM[7] = { CT::MinMaj7, CT::Min7,      CT::AugMaj7, CT::Dom7, CT::Dom7, CT::Min7Flat5, CT::Min7Flat5 };

    degree = ((degree % 7) + 7) % 7;

    switch (family)
    {
        case ScaleFamily::naturalMinor:  return NM[degree];
        case ScaleFamily::harmonicMinor: return HM[degree];
        case ScaleFamily::melodicMinor:  return MM[degree];
        default:                         return M[degree];
    }
}

/** ファミリーと度数から、ダイアトニックトライアドのタイプ。 */
inline ChordType scaleFamilyTriad (ScaleFamily family, int degree)
{
    using CT = ChordType;
    static const CT M[7]  = { CT::Maj, CT::Min, CT::Min, CT::Maj, CT::Maj, CT::Min, CT::Dim };
    static const CT NM[7] = { CT::Min, CT::Dim, CT::Maj, CT::Min, CT::Min, CT::Maj, CT::Maj };
    static const CT HM[7] = { CT::Min, CT::Dim, CT::Aug, CT::Min, CT::Maj, CT::Maj, CT::Dim };
    static const CT MM[7] = { CT::Min, CT::Min, CT::Aug, CT::Maj, CT::Maj, CT::Dim, CT::Dim };

    degree = ((degree % 7) + 7) % 7;

    switch (family)
    {
        case ScaleFamily::naturalMinor:  return NM[degree];
        case ScaleFamily::harmonicMinor: return HM[degree];
        case ScaleFamily::melodicMinor:  return MM[degree];
        default:                         return M[degree];
    }
}
