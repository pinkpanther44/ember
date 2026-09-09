#pragma once

#include "ChordModel.h"
#include <algorithm>
#include <array>
#include <vector>

//==============================================================================
// 設計書1.2のChordEngine。仕様書5.11.2「提案・スコアリング」と
// 5.11.3「ボイシングエンジン」を担当する。ChordCanvasの ChordSuggest.h /
// Voicing.h / ChordPadComponent::rebuildPads() / ChordInputPanel::writeChordNotes()
// を移植したもの（HANDOVER 8.5）。
//
// **ここにUIは無い。** ChordCanvasではパッドの矩形計算と候補の組み立てが
// 同じ関数（rebuildPads）に同居し、ノート生成もノブの値を直接読んでいたが、
// こちらでは「何を並べる・何を鳴らすか」＝このファイル、
// 「どう描く・どのノブから取るか」＝ChordPadPanel（未着手）に分けてある。
// 画面を出さずに値を確かめられるようにするため。
//
// 内訳：
//   ChordScoreWeights / connectionScore … 5.11.2 スコアリング
//   ChordGrid / buildChordGrid          … 5.11.2 11カテゴリ×12列の候補グリッド
//   namespace Voicing                   … 5.11.3 コード→MIDIノート番号
//   ChordPerformance / generateChordNotes … 5.11.3 発音パラメータ→ノートの並び
//==============================================================================

//==============================================================================
/** `connectionScore()` の加点値。

    初期値はChordCanvasのものをそのまま採用している（仕様書10.6）。
    使用感に応じて調整できるよう、定数を直接埋め込まずにこの構造体へ出してある。
*/
struct ChordScoreWeights
{
    float base            = 0.15f;   // 全候補に与える下駄
    float diatonic        = 0.30f;   // 構成音がすべてスケール内
    float rootInScale     = 0.10f;   // ルートだけスケール内（diatonicとは排他）
    float firstChordBonus = 0.25f;   // 進行の先頭（直前のコードが無い）

    /** ルートの動き（上行の半音数 0〜11）ごとの加点。

        5（完全4度上＝5度下降）が最も高い。仕様書5.11.2の
        「ルートモーション（4度上行を最重視）」がこの並び。
    */
    std::array<float, 12> rootMotion
    {
        0.08f,  //  0 同じルート（クオリティ変化）
        0.18f,  //  1 半音上行（クロマチックアプローチ）
        0.20f,  //  2 全音上行（順次進行）
        0.15f,  //  3 短3度上行
        0.05f,  //  4 長3度上行
        0.35f,  //  5 完全4度上＝5度下降（最強の進行）
        0.10f,  //  6 トライトーン（裏コード的な動き）
        0.15f,  //  7 完全5度上
        0.05f,  //  8 短6度上行
        0.15f,  //  9 長6度上行
        0.20f,  // 10 全音下行
        0.18f   // 11 半音下行
    };

    float dominantResolution = 0.15f;  // 直前がドミナント系で5度下降して解決した
    float commonTone         = 0.04f;  // 共通音1つあたり
    int   maxCommonTones     = 3;      // 共通音の加点はここで頭打ち
};

//==============================================================================
/** コードの構成音がすべてスケール内か。 */
inline bool isChordDiatonic (const Chord& chord, const Scale& scale)
{
    for (const auto& [pitchClass, degree] : chord.getTones())
        if (! scale.contains (pitchClass))
            return false;

    return true;
}

/** 直前のコードからの「繋がりやすさ」を0.0〜1.0で返す（仕様書5.11.2）。

    音楽理論の定石を点数化しただけのヒューリスティックで、正解を出すものではない。
    パッドの色（4段階）を決めるのが用途なので、絶対値ではなく候補どうしの
    大小関係だけが意味を持つ。

    @param previous  直前のコード。進行の先頭ならnullptr
    @param candidate 候補のコード
    @param scale     現在のキー
    @param weights   加点値（既定はChordCanvas由来の初期値。仕様書10.6）
*/
inline float connectionScore (const Chord* previous,
                              const Chord& candidate,
                              const Scale& scale,
                              const ChordScoreWeights& weights = {})
{
    float score = weights.base;

    // ダイアトニックなコードは基本的に馴染みやすい
    if (isChordDiatonic (candidate, scale))
        score += weights.diatonic;
    else if (scale.contains (candidate.root))
        score += weights.rootInScale;   // ルートだけでもスケール内なら少し加点

    // 進行の先頭（直前のコードが無い）ならここまでで決める
    if (previous == nullptr)
        return juce::jlimit (0.0f, 1.0f, score + weights.firstChordBonus);

    // ルートの動き（上行の半音数で見る）
    const int motion = (((candidate.root - previous->root) % 12) + 12) % 12;
    score += weights.rootMotion[(size_t) motion];

    // 直前がドミナント系で、5度下降して解決する形なら追加ボーナス
    const bool previousIsDominant = (previous->type == ChordType::Dom7
                                  || previous->type == ChordType::Aug7
                                  || previous->type == ChordType::Dom7Sus4
                                  || previous->type == ChordType::Dom7Flat5);
    if (previousIsDominant && motion == 5)
        score += weights.dominantResolution;

    // 共通音が多いほど滑らかに繋がる
    int commonTones = 0;
    for (const auto& [pitchClassA, degreeA] : previous->getTones())
        for (const auto& [pitchClassB, degreeB] : candidate.getTones())
            if (pitchClassA == pitchClassB) { ++commonTones; break; }

    score += weights.commonTone * (float) juce::jmin (commonTones, weights.maxCommonTones);

    return juce::jlimit (0.0f, 1.0f, score);
}

//==============================================================================
/** コードパッドの2ページ（仕様書5.11.2）。 */
enum class ChordGridPage
{
    main,     // 定番の代理・経過コード集
    related   // 近親調（同主調・平行調のNM/HM/MM）
};

/** グリッドの1マス。理論的に候補が存在しない度数は `hasChord == false`（歯抜け）。 */
struct ChordGridCell
{
    bool  hasChord = false;
    Chord chord;
};

/** グリッドの1行（カテゴリ）。 */
struct ChordGridRow
{
    juce::String label;

    /** 列＝キーのルートからの半音オフセット（0〜11）。
        度数ではなく半音で持つので、スケール外のルート（経過dim等）も同じ表に置ける。 */
    std::array<ChordGridCell, 12> cells {};
};

/** 仕様書5.11.2の「11カテゴリ×12列」の候補グリッド。 */
struct ChordGrid
{
    std::vector<ChordGridRow> rows;
};

//==============================================================================
/** キーから候補グリッドを組み立てる（仕様書5.11.2）。

    @param scale      現在のキー
    @param page       Main（定番11行）か Related（近親調）か
    @param triadMode  trueなら各行を7th系ではなくトライアド系にする
*/
inline ChordGrid buildChordGrid (const Scale& scale,
                                 ChordGridPage page = ChordGridPage::main,
                                 bool triadMode = false)
{
    ChordGrid grid;

    const int         keyRoot = ((scale.root % 12) + 12) % 12;
    const ScaleFamily family  = scale.minor ? ScaleFamily::naturalMinor : ScaleFamily::major;
    const int*        steps   = scaleFamilySteps (family);

    // ピッチクラスを、キーのルートからの半音オフセット（＝列番号）に直す
    auto offsetOf = [keyRoot] (int pitchClass)
    {
        return (((pitchClass - keyRoot) % 12) + 12) % 12;
    };

    // 返す参照が有効なのは「次に newRow を呼ぶまで」。vectorが伸びると無効になるので、
    // 1行ぶんは必ずそのブロックの中で組み立て切ること。
    auto newRow = [&grid] (const char* label) -> ChordGridRow&
    {
        grid.rows.push_back ({});
        grid.rows.back().label = label;
        return grid.rows.back();
    };

    auto put = [] (ChordGridRow& row, int columnOffset, int chordRoot,
                   ChordType type, int tensionMask = 0)
    {
        auto& cell = row.cells[(size_t) (((columnOffset % 12) + 12) % 12)];
        cell.hasChord         = true;
        cell.chord.root       = ((chordRoot % 12) + 12) % 12;
        cell.chord.type       = type;
        cell.chord.tensionMask = tensionMask;
        cell.chord.bass       = -1;
    };

    // あるファミリーのダイアトニックコードを、そのルートの半音位置に並べる
    auto addFamilyRow = [&] (const char* label, int familyRoot, ScaleFamily fam)
    {
        auto&      row       = newRow (label);
        const int* famSteps  = scaleFamilySteps (fam);

        for (int degree = 0; degree < 7; ++degree)
        {
            const int pitchClass = (familyRoot + famSteps[degree]) % 12;
            put (row, offsetOf (pitchClass), pitchClass,
                 triadMode ? scaleFamilyTriad (fam, degree)
                           : scaleFamilySeventh (fam, degree));
        }
    };

    if (page == ChordGridPage::main)
    {
        // 1. ダイアトニック
        addFamilyRow ("Diatonic", keyRoot, family);

        // 2. 6thコード（メジャー系→6、マイナー系→m6。dimの度数は空になる）
        {
            auto& row = newRow ("6th");
            for (int degree = 0; degree < 7; ++degree)
            {
                const auto triad      = scaleFamilyTriad (family, degree);
                const int  pitchClass = (keyRoot + steps[degree]) % 12;

                if (triad == ChordType::Maj)      put (row, steps[degree], pitchClass, ChordType::Six);
                else if (triad == ChordType::Min) put (row, steps[degree], pitchClass, ChordType::Min6);
            }
        }

        // 3. セカンダリードミナント（ターゲット度数の列に置く。Iへのものは除く）
        {
            auto& row = newRow ("Sec.Dom");
            for (int degree = 1; degree < 7; ++degree)
            {
                const int target = (keyRoot + steps[degree]) % 12;
                put (row, steps[degree], (target + 7) % 12,
                     triadMode ? ChordType::Maj : ChordType::Dom7);
            }
        }

        // 4. オーギュメント化したセカンダリードミナント
        {
            auto& row = newRow ("Aug Sec.D");
            for (int degree = 1; degree < 7; ++degree)
            {
                const int target = (keyRoot + steps[degree]) % 12;
                put (row, steps[degree], (target + 7) % 12,
                     triadMode ? ChordType::Aug : ChordType::Aug7);
            }
        }

        // 5. パッシングディミニッシュ（スケール外のルート＝度数と度数の間の列）
        {
            auto& row = newRow ("Passing dim");
            for (int offset = 0; offset < 12; ++offset)
            {
                bool inScale = false;
                for (int degree = 0; degree < 7; ++degree)
                    if (steps[degree] == offset) { inScale = true; break; }

                if (! inScale)
                    put (row, offset, (keyRoot + offset) % 12,
                         triadMode ? ChordType::Dim : ChordType::Dim7);
            }
        }

        // 6. ディミニッシュ（12半音すべて。歯抜けなし）
        {
            auto& row = newRow ("diminish");
            for (int offset = 0; offset < 12; ++offset)
                put (row, offset, (keyRoot + offset) % 12,
                     triadMode ? ChordType::Dim : ChordType::Dim7);
        }

        // 7. セカンダリーII（II-VのIIm。ターゲット度数の列に置く）
        {
            auto& row = newRow ("II (II-V)");
            for (int degree = 1; degree < 7; ++degree)
            {
                const int target = (keyRoot + steps[degree]) % 12;
                put (row, steps[degree], (target + 2) % 12,
                     triadMode ? ChordType::Min : ChordType::Min7);
            }
        }

        // 8. セカンダリーIIのm7b5版（マイナーII-V用）
        {
            auto& row = newRow ("IIm7b5 (II-V)");
            for (int degree = 1; degree < 7; ++degree)
            {
                const int target = (keyRoot + steps[degree]) % 12;
                put (row, steps[degree], (target + 2) % 12,
                     triadMode ? ChordType::MinFlat5 : ChordType::Min7Flat5);
            }
        }

        // 9. 裏コード（セカンダリードミナントのトライトーン代理）
        {
            auto& row = newRow ("Sub Sec.D");
            for (int degree = 1; degree < 7; ++degree)
            {
                const int target = (keyRoot + steps[degree]) % 12;
                put (row, steps[degree], (target + 1) % 12,
                     triadMode ? ChordType::Maj : ChordType::Dom7);
            }
        }

        // 10. sus2（7th系が無いので常にトライアド）
        {
            auto& row = newRow ("sus2");
            for (int degree = 0; degree < 7; ++degree)
                put (row, steps[degree], (keyRoot + steps[degree]) % 12, ChordType::Sus2);
        }

        // 11. sus4
        {
            auto& row = newRow ("sus4");
            for (int degree = 0; degree < 7; ++degree)
                put (row, steps[degree], (keyRoot + steps[degree]) % 12,
                     triadMode ? ChordType::Sus4 : ChordType::Dom7Sus4);
        }

        // 12. add9（メジャー／マイナートライアドの度数にだけ。dimの度数は空）
        {
            auto& row = newRow ("add9");
            for (int degree = 0; degree < 7; ++degree)
            {
                const auto triad      = scaleFamilyTriad (family, degree);
                const int  pitchClass = (keyRoot + steps[degree]) % 12;

                if (triad == ChordType::Maj)      put (row, steps[degree], pitchClass, ChordType::Add9);
                else if (triad == ChordType::Min) put (row, steps[degree], pitchClass, ChordType::MinAdd9);
            }
        }
    }
    else // ChordGridPage::related（近親調）
    {
        if (! scale.minor)
        {
            addFamilyRow ("Diatonic", keyRoot, ScaleFamily::major);
            addFamilyRow ("Para NM",  keyRoot, ScaleFamily::naturalMinor);
            addFamilyRow ("Para HM",  keyRoot, ScaleFamily::harmonicMinor);
            addFamilyRow ("Para MM",  keyRoot, ScaleFamily::melodicMinor);

            const int relative = (keyRoot + 9) % 12;   // 平行短調
            addFamilyRow ("Rel NM",   relative, ScaleFamily::naturalMinor);
            addFamilyRow ("Rel HM",   relative, ScaleFamily::harmonicMinor);
            addFamilyRow ("Rel MM",   relative, ScaleFamily::melodicMinor);
        }
        else
        {
            addFamilyRow ("Diatonic", keyRoot, ScaleFamily::naturalMinor);
            addFamilyRow ("HM",       keyRoot, ScaleFamily::harmonicMinor);
            addFamilyRow ("MM",       keyRoot, ScaleFamily::melodicMinor);
            addFamilyRow ("Para Maj", keyRoot, ScaleFamily::major);

            const int relative = (keyRoot + 3) % 12;   // 平行長調
            addFamilyRow ("Rel Maj",  relative, ScaleFamily::major);
        }
    }

    return grid;
}

//==============================================================================
/** 仕様書5.11.3のボイシングエンジン。コードをMIDIノート番号の並びに変換する。

    ChordCanvasの `Voicing.h` の移植。移植元は音域の定数を `GridConfig.h`
    （32小節・C1〜C7固定）から取っていたが、こちらは持ち込まず、必要な2つだけを
    ここに置いてある（HANDOVER 8.5）。

    返るノート番号は必ず昇順・重複なし。
*/
namespace Voicing
{
    /** 生成するノートの音域。

        **`PianoRollComponent::lowestPitch` / `highestPitch` と同じ値にしてある。**
        ここを外れた音は捨てられるので、ピアノロールより広くすると
        「鳴っているのに画面に出ない・掴めないノート」ができてしまう（HANDOVER 1.9）。
        ピアノロール側の音域を変えるときは、ここも一緒に変えること。
        エンジンがUIヘッダをincludeしないよう（設計書1.1）、値は意図的に複製している。

        なお、オクターブを下げるとベース音がこの下限を割って落ちることがある。
        移植元と同じ挙動だが、UIを付けるとき（5.11.3のOctave）に見直す余地がある。
    */
    constexpr int lowestPitch  = 36;   // C2
    constexpr int highestPitch = 96;   // C7

    /** ギターのレギュラーチューニング（E2 A2 D3 G3 B3 E4）。低い弦から順。 */
    inline constexpr std::array<int, 6> openStrings { 40, 45, 50, 55, 59, 64 };

    /** `from` と同じかそれより上で、ピッチクラス `pitchClass` に一致する最小のノート番号。 */
    inline int nextPitchAtOrAbove (int from, int pitchClass)
    {
        return from + ((pitchClass - (from % 12) + 12) % 12);
    }

    /** コードのピッチクラス一覧（登場順・重複なし）。転回はこの並びを回して作る。 */
    inline std::vector<int> uniquePitchClasses (const Chord& chord)
    {
        std::vector<int> pitchClasses;

        for (const auto& [pitchClass, degree] : chord.getTones())
            if (std::find (pitchClasses.begin(), pitchClasses.end(), pitchClass) == pitchClasses.end())
                pitchClasses.push_back (pitchClass);

        return pitchClasses;
    }

    /** 音域外を捨てて、昇順・重複なしに整える。 */
    inline std::vector<int> finalize (std::vector<int> pitches)
    {
        std::vector<int> out;

        for (int pitch : pitches)
            if (pitch >= lowestPitch && pitch <= highestPitch)
                out.push_back (pitch);

        std::sort (out.begin(), out.end());
        out.erase (std::unique (out.begin(), out.end()), out.end());
        return out;
    }

    /** ピアノボイシング。C4付近に構成音を積み、ベース音をC2付近に別途置く。

        @param inversion     0=基本形、1=第1転回、2=第2転回、3=第3転回
        @param octaveOffset  全体のオクターブ移動（-2〜+2）
    */
    inline std::vector<int> piano (const Chord& chord, int inversion, int octaveOffset)
    {
        auto pitchClasses = uniquePitchClasses (chord);

        if (pitchClasses.empty())
            return {};

        // 転回：並びの先頭を後ろに回す
        const int numTones = (int) pitchClasses.size();
        inversion = ((inversion % numTones) + numTones) % numTones;
        std::rotate (pitchClasses.begin(), pitchClasses.begin() + inversion, pitchClasses.end());

        // C4（60）付近から上へ積む
        std::vector<int> pitches;
        int previous = nextPitchAtOrAbove (60, pitchClasses[0]);
        pitches.push_back (previous);

        for (size_t i = 1; i < pitchClasses.size(); ++i)
        {
            const int pitch = nextPitchAtOrAbove (previous + 1, pitchClasses[i]);
            pitches.push_back (pitch);
            previous = pitch;
        }

        // ベース音（分数コード指定があればその音）をC2（36）付近に
        const int bassPitchClass = (chord.bass >= 0) ? chord.bass : chord.root;
        pitches.insert (pitches.begin(), nextPitchAtOrAbove (36, bassPitchClass));

        for (auto& pitch : pitches)
            pitch += octaveOffset * 12;

        return finalize (std::move (pitches));
    }

    /** ギターのフレット配置。TAB譜表示と発音の両方に使う。

        `frets[弦]` は押さえるフレット（0=開放、-1=ミュート）。弦は低い順（E2→E4）。
    */
    struct GuitarShape
    {
        std::array<int, 6> frets { -1, -1, -1, -1, -1, -1 };
        int baseFret = 0;   // TAB譜のフレット窓の開始位置（ハイコード時は5など）
    };

    /** コードの押さえ方を決める。

        @param highPosition  false=ローコード（開放弦を含むオープンコード）、
                             true=ハイコード（バレーコード。E型／A型を自動選択）
    */
    inline GuitarShape guitarShape (const Chord& chord, bool highPosition)
    {
        GuitarShape shape;

        const auto pitchClasses = uniquePitchClasses (chord);

        if (pitchClasses.empty())
            return shape;

        const int rootPitchClass = ((chord.root % 12) + 12) % 12;

        // コードの性質を判定（短3度を持ち長3度を持たなければマイナー系）
        auto has = [&pitchClasses, rootPitchClass] (int semitones)
        {
            const int target = (((rootPitchClass + semitones) % 12) + 12) % 12;
            return std::find (pitchClasses.begin(), pitchClasses.end(), target) != pitchClasses.end();
        };
        const bool isMinor = has (3) && ! has (4);

        if (highPosition)
        {
            // ---- バレーコード（セーハ）----
            // ルートからの相対フレット。E型は6弦ルート、A型は5弦ルート（6弦はミュート）。
            static const int eMajor[6] = { 0, 2, 2, 1, 0, 0 };
            static const int eMinor[6] = { 0, 2, 2, 0, 0, 0 };
            static const int aMajor[5] = { 0, 2, 2, 2, 0 };   // 5弦→1弦
            static const int aMinor[5] = { 0, 2, 2, 1, 0 };

            const int rootFret6 = (((rootPitchClass - (openStrings[0] % 12)) % 12) + 12) % 12;
            const int rootFret5 = (((rootPitchClass - (openStrings[1] % 12)) % 12) + 12) % 12;

            // 基本はE型。6弦ルートが7フレット以上に来るときはA型のほうが押さえやすい
            if (rootFret6 <= 6)
            {
                const int* relative = isMinor ? eMinor : eMajor;

                for (size_t string = 0; string < 6; ++string)
                    shape.frets[string] = rootFret6 + relative[string];

                shape.baseFret = rootFret6;
            }
            else
            {
                const int* relative = isMinor ? aMinor : aMajor;

                shape.frets[0] = -1;   // 6弦ミュート

                for (size_t string = 0; string < 5; ++string)
                    shape.frets[string + 1] = rootFret5 + relative[string];

                shape.baseFret = rootFret5;
            }

            return shape;
        }

        // ---- ローコード（オープンコード。0〜4フレット）----
        constexpr int lowestFret  = 0;
        constexpr int highestFret = 4;
        shape.baseFret = 0;

        const int bassPitchClass = (chord.bass >= 0) ? chord.bass : chord.root;

        // ベース音を押さえられる一番低い弦を探す。それより下の弦はベースより低い音に
        // なってしまうのでミュートしたまま（frets の初期値 -1）にする。
        int  startString   = 0;
        bool bassFound     = false;

        for (int string = 0; string < 6 && ! bassFound; ++string)
            for (int fret = lowestFret; fret <= highestFret; ++fret)
                if ((openStrings[(size_t) string] + fret) % 12 == bassPitchClass)
                {
                    startString = string;
                    bassFound   = true;
                    shape.frets[(size_t) string] = fret;
                    break;
                }

        for (int string = (bassFound ? startString + 1 : 0); string < 6; ++string)
            for (int fret = lowestFret; fret <= highestFret; ++fret)
            {
                const int pitch = openStrings[(size_t) string] + fret;

                if (std::find (pitchClasses.begin(), pitchClasses.end(), pitch % 12) != pitchClasses.end())
                {
                    shape.frets[(size_t) string] = fret;
                    break;
                }
            }

        return shape;
    }

    /** `guitarShape()` の押さえ方を、実際のMIDIノート番号に変換する。 */
    inline std::vector<int> guitarPitches (const Chord& chord, int octaveOffset, bool highPosition)
    {
        const auto shape = guitarShape (chord, highPosition);

        std::vector<int> pitches;

        for (size_t string = 0; string < 6; ++string)
            if (shape.frets[string] >= 0)
                pitches.push_back (openStrings[string] + shape.frets[string] + octaveOffset * 12);

        return finalize (std::move (pitches));
    }
}

//==============================================================================
/** ストロークの向き（仕様書5.11.3の Str）。 */
enum class ChordStroke
{
    none,       // 同時に鳴らす
    down,       // 低い弦から
    up,         // 高い弦から
    alternate   // 敷き詰めるとき1回ごとに交互
};

/** 仕様書5.11.3の発音パラメータ。 */
struct ChordPerformance
{
    bool guitar = false;        // false=ピアノボイシング、true=ギターボイシング

    /** ピアノなら転回形（0〜3）、ギターなら 0=ローコード／1=ハイコード。 */
    int inversion = 0;

    int octaveOffset = 0;       // -2〜+2

    /** 1音ぶんの音価（拍）。0以下なら「コード区間ぶんのロングトーン」。
        全音符=4.0 / 2分=2.0 / 4分=1.0 / 8分=0.5 / 16分=0.25 / 32分=0.125 */
    double hitLengthBeats = 0.0;

    double gate     = 0.8;      // GT%。音価に対する実発音長の割合（0.0〜1.0）
    int    velocity = 100;      // Vel

    ChordStroke stroke = ChordStroke::none;
    double strokeDeviationBeats = 0.0;   // Dev。ストローク時の音同士のずれ幅（拍）
};

/** `generateChordNotes()` が返す1ノート。まだMIDIクリップには載っていない。

    時刻はコード区間と同じ「拍」で、クリップ先頭からの相対ではない。
    `MidiClip::addNote()` へ渡すときに、クリップの座標へ直すのは呼び出し側の仕事
    （クリップ内の時刻は中身の先頭から数える。HANDOVER 1.14）。
*/
struct GeneratedChordNote
{
    int    pitch       = 60;
    double startBeats  = 0.0;
    double lengthBeats = 1.0;
    int    velocity    = 100;
};

/** コード1つと発音パラメータから、書き込むノートの並びを作る（仕様書5.11.3）。

    ChordCanvasの `ChordInputPanel::writeChordNotes()` から、ノブの読み取りと
    NoteSequenceへの書き込みを外して、計算だけを取り出したもの。

    @param chord        鳴らすコード
    @param startBeats   コード区間の開始（拍）
    @param lengthBeats  コード区間の長さ（拍）
    @param performance  発音パラメータ
*/
inline std::vector<GeneratedChordNote> generateChordNotes (const Chord& chord,
                                                           double startBeats,
                                                           double lengthBeats,
                                                           const ChordPerformance& performance)
{
    std::vector<GeneratedChordNote> notes;

    if (lengthBeats <= 0.0)
        return notes;

    const auto pitches = performance.guitar
                            ? Voicing::guitarPitches (chord, performance.octaveOffset,
                                                      performance.inversion == 1)
                            : Voicing::piano (chord, performance.inversion,
                                              performance.octaveOffset);

    if (pitches.empty())
        return notes;

    const double endBeats = startBeats + lengthBeats;

    // 和音1回ぶんを position に置く
    auto placeHit = [&] (double position, double span, int hitIndex)
    {
        bool downStroke = true;

        if (performance.stroke == ChordStroke::up)
            downStroke = false;
        else if (performance.stroke == ChordStroke::alternate && (hitIndex % 2) == 1)
            downStroke = false;

        std::vector<int> ordered = pitches;   // 昇順で来る

        if (! downStroke)
            std::reverse (ordered.begin(), ordered.end());

        for (size_t i = 0; i < ordered.size(); ++i)
        {
            const double offset = (performance.stroke == ChordStroke::none)
                                    ? 0.0
                                    : (double) i * performance.strokeDeviationBeats;

            // ずらしすぎて区間からはみ出す音は鳴らさない
            if (offset >= span)
                break;

            GeneratedChordNote note;
            note.pitch       = ordered[i];
            note.startBeats  = position + offset;
            note.lengthBeats = juce::jmax (0.1, (span - offset) * performance.gate);
            note.velocity    = performance.velocity;
            notes.push_back (note);
        }
    };

    // 「コード区間ぶんのロングトーン」なら1回だけ
    if (performance.hitLengthBeats <= 0.0)
    {
        placeHit (startBeats, lengthBeats, 0);
        return notes;
    }

    // 音価が指定されていれば、コード区間を敷き詰める
    double position = startBeats;
    int    hitIndex = 0;

    while (position < endBeats - 1.0e-6)
    {
        placeHit (position, juce::jmin (performance.hitLengthBeats, endBeats - position), hitIndex);
        position += performance.hitLengthBeats;
        ++hitIndex;
    }

    return notes;
}
