#pragma once

#include <array>
#include <cmath>

//==============================================================================
/**
    8.292：**Kakapo のスケール判定**（Phase 285／本人の仕様書5章）。

    12音の使用量（ヒストグラム）から、**メジャー1つ・マイナー1つ**を返します。
    `Racco Guitar`たちのDSPと同じで、**JUCEに依存させていません**——
    そのまま数値で測れます（`Tools/PluginPreview --scale`が仕様書18章の試験を回します）。

    ### 何を見て決めているか

    ```
    24候補 ＝ 12ルート × {メジャー, ナチュラルマイナー}

    inScale  … そのスケールに入っている音の重みの合計
    outScale … 入っていない音の重み（＝全部 - inScale）
    score    … inScale - outScale          （**外れた音は引く**）
    matchRatio … inScale / 全部            （0〜1。画面の帯はこれ）
    ```

    **引かないと、半音階を弾いたときに全部のスケールが満点になります**
    （どのスケールも7音は含むので）。引くことで「外れた音が少ない候補」が勝ちます。

    ### 並行調（CメジャーとAマイナー）

    **使う音が同じなので、点数では差が付きません。** 仕様書5.4のとおり、
    **いちばん多く鳴っている音**（トーナルセンター）で優劣だけ決めます。

    | | |
    |---|---|
    | いちばん多い音がメジャーのルート | **メジャー優勢** |
    | いちばん多い音がマイナーのルート | **マイナー優勢** |
    | どちらでもない | ルートの音の重みが大きいほう |

    **候補そのものは変えません**（表示の強調だけ）。仕様書の言うとおりです。

    ### 音が少ないとき

    **違うピッチクラスが3つ未満なら「情報不足」**とします（仕様書5.5）。
    2音では、どのスケールも同じくらい当てはまります——
    **当てはまりすぎるものは、何も言っていないのと同じ**です。
*/
namespace kakapo
{
    enum class ScaleType { major, minor };

    /** 全音階（メジャー）とナチュラルマイナーの音程。**仕様書5.3のまま**。 */
    inline constexpr int majorTemplate[7] { 0, 2, 4, 5, 7, 9, 11 };
    inline constexpr int minorTemplate[7] { 0, 2, 3, 5, 7, 8, 10 };

    inline bool isInScale (int offsetFromRoot, ScaleType type) noexcept
    {
        const int offset = ((offsetFromRoot % 12) + 12) % 12;

        for (const int step : (type == ScaleType::major ? majorTemplate : minorTemplate))
            if (step == offset)
                return true;

        return false;
    }

    /** 音名。**ASCII固定**（画面に出る用語は英語。9.7）。 */
    inline const char* pitchClassName (int pitchClass) noexcept
    {
        static const char* const names[12]
        {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        return names[((pitchClass % 12) + 12) % 12];
    }

    inline const char* scaleTypeName (ScaleType type) noexcept
    {
        return type == ScaleType::major ? "MAJOR" : "MINOR";
    }

    /** そのスケールの音程（7つ）。**構成音を並べるのは画面の仕事**なので、
        ここは数だけ返します（この見出しをJUCEに依存させないため）。 */
    inline const int* scaleSteps (ScaleType type) noexcept
    {
        return type == ScaleType::major ? majorTemplate : minorTemplate;
    }

    //==========================================================================
    struct ScaleCandidate
    {
        int root = 0;
        ScaleType type = ScaleType::major;
        float matchRatio = 0.0f;   ///< 0〜1（画面の帯）
        float score = 0.0f;        ///< 並べ替えに使った点数（外れた音を引いたもの）
    };

    struct Result
    {
        ScaleCandidate major;
        ScaleCandidate minor;

        /** いちばん多く鳴っている音（-1なら何も鳴っていない）。 */
        int tonalCentre = -1;

        /** **表示の強調だけ**に使う（上の説明）。 */
        bool majorFavoured = true;

        /** 判定できるだけの音があるか（違うピッチクラスが3つ以上）。 */
        bool hasEnoughNotes = false;
    };

    //==========================================================================
    /** ヒストグラム（長さ12の重み）から24候補を採点する。

        **同点のときは、トーナルセンターをルートに持つほうを採ります**
        ——そうしないと、同点の並びの中でいちばん番号の小さいルートが勝ち、
        **弾いている音と関係ない調が出ます**。 */
    inline Result evaluate (const std::array<float, 12>& histogram)
    {
        Result result;

        // **種類は先に入れておくこと。** 何も鳴っていないときは下で早く返るので、
        // ここを忘れると**「MAJOR」が2つ並びます**（画面にそう出ました）
        result.major.type = ScaleType::major;
        result.minor.type = ScaleType::minor;

        float total = 0.0f;
        int distinct = 0;
        int tonalCentre = -1;
        float strongest = 0.0f;

        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        {
            const float weight = histogram[(size_t) pitchClass];

            total += weight;

            if (weight > 0.0f)
                ++distinct;

            if (weight > strongest)
            {
                strongest = weight;
                tonalCentre = pitchClass;
            }
        }

        result.tonalCentre = tonalCentre;
        result.hasEnoughNotes = distinct >= 3 && total > 0.0f;

        if (total <= 0.0f)
            return result;

        auto best = [&histogram, total, tonalCentre] (ScaleType type)
        {
            ScaleCandidate winner;
            winner.type = type;

            bool first = true;

            for (int root = 0; root < 12; ++root)
            {
                float inScale = 0.0f;

                for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
                    if (isInScale (pitchClass - root, type))
                        inScale += histogram[(size_t) pitchClass];

                const float outOfScale = total - inScale;
                const float score = inScale - outOfScale;

                // **同点はトーナルセンターで割る**（クラスの説明）
                const bool better = first
                                     || score > winner.score + 1.0e-6f
                                     || (std::abs (score - winner.score) <= 1.0e-6f
                                          && root == tonalCentre);

                if (better)
                {
                    winner.root = root;
                    winner.score = score;
                    winner.matchRatio = inScale / total;
                    first = false;
                }
            }

            return winner;
        };

        result.major = best (ScaleType::major);
        result.minor = best (ScaleType::minor);

        //----------------------------------------------------------------------
        // 並行調の優劣（**表示の強調だけ**。仕様書5.4）
        if (tonalCentre == result.major.root)
            result.majorFavoured = true;
        else if (tonalCentre == result.minor.root)
            result.majorFavoured = false;
        else
            result.majorFavoured = histogram[(size_t) result.major.root]
                                    >= histogram[(size_t) result.minor.root];

        return result;
    }

    /** 2つの候補が並行調か（例：Cメジャー と Aマイナー）。

        **マイナーのルートは、メジャーのルートの3半音下**です。 */
    inline bool isRelativeKey (const ScaleCandidate& majorCandidate,
                                const ScaleCandidate& minorCandidate) noexcept
    {
        return ((majorCandidate.root + 9) % 12) == minorCandidate.root;
    }
}
