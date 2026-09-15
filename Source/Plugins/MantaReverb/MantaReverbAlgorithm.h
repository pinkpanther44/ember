#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

//==============================================================================
/**
    8.243：**アルゴリズムの表**（Phase 255／リバーブ仕様書4章・設計書3章）。

    ─────────────────────────────────────────────────────────────────────────
    ここが**ただ1つの判断の場所**
    ─────────────────────────────────────────────────────────────────────────

    「このアルゴリズムでこのつまみは効くか」「寸法はいくつか」を、
    **音の側と画面の側で別々に決めないこと**（1.27）。
    別々に書くと、**触れるのに効かないつまみ**や、その逆ができます
    （ディレイの`MantaDelayCharacter::getCapabilities()`と同じ形。8.208）。

    ─────────────────────────────────────────────────────────────────────────
    入っているもの（設計書8章の段階表）
    ─────────────────────────────────────────────────────────────────────────

    | | |
    |---|---|
    | Phase 1 | `Room` |
    | Phase 2 | `Plate`（Dattorro型）と`Hall` |
    | **Phase 3（いまここ）** | **`Ambience`。`Shape`と`Spread`** |
    | Phase 4 | `Twin Delays`、`Panorama` |
    | Phase 6 | `Random Hall` |

    **並びの末尾へ足すこと。** `AudioParameterChoice`は木へ**実値**（文字列ではなく
    番号）を書くので、途中へ挿すと**保存済みのプロジェクトが別のアルゴリズムで開きます**
    （8.218で書いた「選択肢を足すとき、保存とオートメーションは別物」の、
    さらに手前の話です）。

    ─────────────────────────────────────────────────────────────────────────
    `Room`と`Hall`は**同じFDN**、寸法だけが違う
    ─────────────────────────────────────────────────────────────────────────

    設計書4-2の決定：**Hallも4×4のアダマール行列による基本FDNのみ**
    （8〜16ラインへの拡張は見送り）。

    つまり2つの違いは**ライン長と初期反射のパターンだけ**です。
    形を2つ持つ理由がないので、`MantaReverbFdn`へ**表を渡す**形にしてあります。

    失われたもの：大規模FDNならではの超高密度な残響感。
    **「広さ」そのものは4本でも作れます**（設計書4-2）。

    `Plate`だけは形が違うので、別の箱です（`MantaReverbPlate`）。
*/
namespace MantaReverbAlgorithm
{
    /** **並びを変えないこと**（上の説明）。足すのは末尾。 */
    enum class Kind
    {
        room = 0,
        plate,
        hall,
        ambience,    // 8.245（Phase 256）
        twinDelays,  // 8.247（Phase 257）
        panorama,    // 8.248（Phase 258）
        randomHall   // 8.250（Phase 260）
    };

    inline int getKindCount() { return 7; }

    /** **`utf8()`へ通さないこと。** ホストのオートメーション一覧に出る文字列で、
        言語を切り替えるたびに変わると、書き出したオートメーションが読めなくなります。 */
    inline juce::StringArray getKindNames()
    {
        return { "Room", "Plate", "Hall", "Ambience", "Twin Delays", "Panorama", "Random Hall" };
    }

    /** 画面に出す一行の説明。**名前だけでは何が違うか分かりません**
        （ディレイの`MantaDelayRouting::getModeDescription()`と同じ理由。8.217）。

        **英語のままにしてあります**——ディレイのルーティングの説明と同じ扱いです
        （アルゴリズム名が英語なので、そこだけ日本語になると並びが崩れます）。 */
    inline const char* getDescription (Kind kind)
    {
        switch (kind)
        {
            case Kind::room:     return "Short decay, dense early reflections";
            case Kind::plate:    return "Dense and coloured, no room shape";
            case Kind::hall:     return "Long and wide, reflections far apart";
            case Kind::ambience: return "Air around the sound, barely a tail";
            case Kind::twinDelays: return "Two delays, one each side. Not a reverb";

            // 8.248：**`Mix`のことを書いておくこと**（Phase 258／8.225）。
            // 既定の25%のままだと、ステレオ加工が4分の1しか混ざりません——
            // **コードが正しくても「何も起きない」が起きます**
            case Kind::panorama: return "Stereo tool, no tail. Turn Mix up to 100%";

            // 8.250：**Modulation が0だとHallそのもの**（Phase 260）。
            // そう書いておかないと、選んだのに何も変わらないように見えます（8.225）
            case Kind::randomHall: return "Hall that drifts. Turn Modulation up";

            default:             return "";
        }
    }

    //==========================================================================
    /** そのアルゴリズムで効くつまみ。**画面のグレーアウトはここだけを見ます。**

        8.247：Phase 4aで増えました（Phase 257）。**1つのつまみに1つの旗**に
        してあります——「初期反射があるか」で`Shape`まで決めていると、
        `Twin Delays`のように**Spreadだけ意味を持つ**ものが表せません。 */
    struct Capabilities
    {
        /** `Decay`と`Size`。**空間の寸法**。 */
        bool decayAndSize = true;

        /** `Early`（初期反射の量）。**Plateは持ちません**——金属板に「壁の形」がないため。 */
        bool early = true;

        /** `Shape`（初期反射の山の位置）。 */
        bool shape = true;

        /** `Spread`。**`Twin Delays`では意味が変わりますが、効きます**
            （あちらでは「2本の間隔」。`MantaReverbTwinDelays`）。 */
        bool spread = true;

        /** 入口の拡散。 */
        bool diffusion = true;

        /** HF／LFのダンピング。 */
        bool damping = true;

        /** 8.247：`Time`・`Feedback`・`Cross`（Phase 257）。**`Twin Delays`だけ**。 */
        bool twinDelay = false;

        /** 8.248：`Mono Sum`・`Invert R`・`Swap L/R`（Phase 258）。**`Panorama`だけ**。 */
        bool panorama = false;

        /** 8.250：`Modulation`（Phase 260）。**`Random Hall`だけ**。

            HallとRandom Hallの違いは**これ1つ**なので、
            他のアルゴリズムでも効くようにすると、**Random Hallを選ぶ理由が無くなります**
            （設計書4-3：「簡易Hallをベースに1〜2本のラインを変調する」）。 */
        bool modulation = false;
    };

    inline Capabilities getCapabilities (Kind kind)
    {
        Capabilities capabilities;

        // 8.243：**Plateに初期反射はありません**（Phase 255）。
        //
        // Dattorro型は**入口の4段オールパスがその役**を兼ねています。
        // 別に初期反射を足すと、原典の響きではなくなります
        // （設計書4-1：**独自のチューニングや改良は行わない**）
        if (kind == Kind::plate)
        {
            capabilities.early = false;
            capabilities.shape = false;
            capabilities.spread = false;
        }

        // 8.247：**`Twin Delays`はリバーブではありません**（Phase 257／仕様書4章）。
        //
        // 部屋の寸法も初期反射もダンピングも無く、代わりに
        // `Time`・`Feedback`・`Cross`が効きます。
        // `Spread`だけは残します——**2本の間隔**という別の意味を持たせてあるので
        if (kind == Kind::twinDelays)
        {
            capabilities.decayAndSize = false;
            capabilities.early = false;
            capabilities.shape = false;
            capabilities.diffusion = false;
            capabilities.damping = false;
            capabilities.twinDelay = true;
        }

        // 8.248：**`Panorama`もリバーブではありません**（Phase 258／仕様書4章）。
        //
        // 効くのは`Predelay`と`Width`と3つのボタンだけ。
        // `Spread`まで落とすのは`Twin Delays`と違うところです——
        // あちらは「2本の間隔」という行き先がありましたが、こちらには何もありません
        if (kind == Kind::panorama)
        {
            capabilities.decayAndSize = false;
            capabilities.early = false;
            capabilities.shape = false;
            capabilities.spread = false;
            capabilities.diffusion = false;
            capabilities.damping = false;
            capabilities.panorama = true;
        }

        // 8.250：`Random Hall`（Phase 260）。**Hallに揺れが足せるだけ**の違いです
        if (kind == Kind::randomHall)
            capabilities.modulation = true;

        return capabilities;
    }

    //==========================================================================
    /** FDN系（`Room`・`Hall`）の寸法。**`Plate`は使いません。**

        どの数字も**互いに素に近いこと**（倍数どうしにすると、
        山と谷が同じところで重なって金属的に響きます。8.220）。 */
    struct FdnTuning
    {
        /** ディレイライン4本の長さ（ミリ秒。`Size`が1.0のとき）。 */
        std::array<double, 4> lineMs;

        /** ライン内オールパスの長さ（ミリ秒）。**密度を上げるためのもの。** */
        std::array<double, 4> allPassMs;

        /** 入口の拡散2段の長さ（ミリ秒）。左。右は`inputDiffuserRatio`倍。 */
        std::array<double, 2> inputDiffuserMs;

        /** 8.245：テールをどれだけ出すか（Phase 256）。**初期反射との釣り合い**です。

            `Ambience`のためだけに足しました——**「空気感の付与が主目的」**（仕様書4章）
            なのに、テールが他と同じ大きさで出ていると、ただの狭い部屋になります。
            `Room`と`Hall`は**1.0のまま**（Phase 2と同じ音）。 */
        double lateLevel;
    };

    /** 入口の拡散の、**左に対する右の倍率**（8.223）。

        左右を同じにすると滲みが真ん中に固まります。
        **倍数にならない半端な数字**であること。 */
    inline constexpr double inputDiffuserRatio = 1.17;

    /** `Room`：短めの減衰、密な初期反射（仕様書4章）。 */
    inline constexpr FdnTuning roomTuning
    {
        { 23.9, 31.1, 41.3, 53.9 },
        {  5.1,  7.3,  9.7, 11.9 },
        {  4.7,  8.3 },
        1.0
    };

    /** `Hall`：広いホール。**ライン長が倍以上**で、1周が長いぶん密度が下がるので、
        ライン内オールパスも長くしてあります（**そこで詰め直す**）。 */
    inline constexpr FdnTuning hallTuning
    {
        { 53.1, 70.3, 89.9, 113.7 },
        { 11.3, 17.9, 23.1,  29.3 },
        {  9.7, 16.3 },
        1.0
    };

    /** 8.245：`Ambience`（Phase 256／仕様書4章「非常に短い残響。空気感の付与が主目的」）。

        **Roomの半分以下**のライン長です。1周が短いぶん**モードの間隔が広くなる**
        （＝色が付きやすい）ので、Roomのようには伸ばせません——
        `Decay`を上げていくと、部屋というより**箱の共鳴**に寄っていきます。
        **それがこのアルゴリズムの素性**で、隠すものではありません。

        テールを`0.55`まで下げてあるのは上の`lateLevel`の説明のとおりです。 */
    inline constexpr FdnTuning ambienceTuning
    {
        { 11.3, 15.7, 21.1, 26.9 },
        {  2.3,  3.7,  4.9,  6.1 },
        {  2.9,  5.3 },
        0.55
    };

    inline const FdnTuning& getFdnTuning (Kind kind)
    {
        switch (kind)
        {
            case Kind::hall:
            case Kind::randomHall:  // 8.250：**寸法はHallと同じ**（違うのは揺れだけ）
                return hallTuning;

            case Kind::ambience: return ambienceTuning;

            default:             return roomTuning;
        }
    }

    /** 確保のための「どの表でもいちばん長い寸法」。

        **表を足したら、ここにも足すこと**（9.4「判断は1箇所」の裏返しで、
        **確保だけは全部を知っている必要があります**）。
        足し忘れると、そのアルゴリズムを選んだ瞬間に**寸法が頭打ち**になります
        （`setLength()`が確保の中へ丸めるので、落ちはしません——**黙って短くなります**）。 */
    inline FdnTuning getLongestFdnTuning()
    {
        FdnTuning longest = roomTuning;

        // **表を足したら、この並びへ1つ足すだけ**にしてあります
        for (const auto* tuning : { &roomTuning, &hallTuning, &ambienceTuning })
        {
            for (size_t i = 0; i < longest.lineMs.size(); ++i)
            {
                longest.lineMs[i] = juce::jmax (longest.lineMs[i], tuning->lineMs[i]);
                longest.allPassMs[i] = juce::jmax (longest.allPassMs[i], tuning->allPassMs[i]);
            }

            for (size_t i = 0; i < longest.inputDiffuserMs.size(); ++i)
                longest.inputDiffuserMs[i] = juce::jmax (longest.inputDiffuserMs[i],
                                                          tuning->inputDiffuserMs[i]);
        }

        return longest;
    }

    //==========================================================================
    /** 初期反射のタップ1つ。 */
    struct EarlyTap
    {
        double milliseconds;
        float gain;
    };

    static constexpr int numEarlyTaps = 8;

    /** 初期反射のパターン（左右1組）。

        **符号は交互にすること。** 同符号だと低い周波数でだけ全部が足し合わさって
        「ボン」と鳴ります。交互なら、足し合わせの山が周波数方向へばらけます。

        **左右で別の数字を持つこと**（8.223）。同じ音が同じ時刻に両耳へ届くと、
        頭の中で1点に聞こえます。 */
    struct EarlyPattern
    {
        std::array<EarlyTap, (size_t) numEarlyTaps> left;
        std::array<EarlyTap, (size_t) numEarlyTaps> right;
    };

    /** `Room`：**近い壁**。8msから始まり、70msで終わります。 */
    inline constexpr EarlyPattern roomEarly
    {
        { { {  8.3,  0.84f }, { 14.7, -0.72f }, { 21.1,  0.61f }, { 29.3, -0.52f },
            { 38.9,  0.44f }, { 48.1, -0.36f }, { 58.7,  0.29f }, { 71.3, -0.22f } } },
        { { { 10.9,  0.80f }, { 17.3, -0.69f }, { 24.7,  0.58f }, { 33.1, -0.49f },
            { 42.3,  0.41f }, { 52.9, -0.34f }, { 63.1,  0.27f }, { 76.1, -0.20f } } }
    };

    /** `Hall`：**遠い壁**。17msから始まり、143msまで伸びます。

        Roomの数字を一律に倍にはしていません——**倍にすると、同じ部屋の
        録音を遅くしただけ**に聞こえます。ホールは遠い反射ほど疎になるので、
        後ろのほうを広く取ってあります。 */
    inline constexpr EarlyPattern hallEarly
    {
        { { { 17.9,  0.72f }, { 29.3, -0.64f }, { 43.1,  0.55f }, { 59.7, -0.47f },
            { 79.3,  0.39f }, { 99.1, -0.31f }, { 121.7, 0.25f }, { 143.3, -0.19f } } },
        { { { 21.7,  0.69f }, { 34.1, -0.61f }, { 49.3,  0.52f }, { 67.9, -0.44f },
            { 87.1,  0.37f }, { 107.3, -0.29f }, { 129.1, 0.23f }, { 151.9, -0.18f } } }
    };

    /** 8.245：`Ambience`（Phase 256）。**1.7msから24msまで。**

        ここまで近いと、耳は個々の反射を聞き分けず**「音の周りの空気」**として聞きます。
        いちばん前のタップがほぼ原音に重なるので、**Earlyを上げると音が近づきます。** */
    inline constexpr EarlyPattern ambienceEarly
    {
        { { {  1.7,  0.88f }, {  3.9, -0.79f }, {  5.3,  0.71f }, {  7.9, -0.63f },
            { 11.3,  0.55f }, { 14.9, -0.47f }, { 19.3,  0.39f }, { 23.9, -0.31f } } },
        { { {  2.3,  0.85f }, {  4.7, -0.76f }, {  6.7,  0.68f }, {  9.1, -0.60f },
            { 12.7,  0.52f }, { 16.3, -0.44f }, { 21.1,  0.36f }, { 26.3, -0.29f } } }
    };

    inline const EarlyPattern& getEarlyPattern (Kind kind)
    {
        switch (kind)
        {
            case Kind::hall:
            case Kind::randomHall:  // 8.250：初期反射もHallと同じ
                return hallEarly;

            case Kind::ambience: return ambienceEarly;

            default:             return roomEarly;
        }
    }

    /** 確保のための「どのパターンでもいちばん後ろのタップ」（ミリ秒）。
        **パターンを足したら、この並びへ1つ足すこと**（`getLongestFdnTuning()`と同じ）。 */
    inline double getLongestEarlyMs()
    {
        double longest = 0.0;

        for (const auto* pattern : { &roomEarly, &hallEarly, &ambienceEarly })
            for (const auto& taps : { pattern->left, pattern->right })
                for (const auto& tap : taps)
                    longest = juce::jmax (longest, tap.milliseconds);

        return longest;
    }

    //==========================================================================
    /** 8.246：**Shape と Spread**（Phase 256／仕様書5章・設計書4-6）。

        ─────────────────────────────────────────────────────────────────
        ライト版であること（設計書4-6）
        ─────────────────────────────────────────────────────────────────

        設計書の決定：**立ち上がりのカーブを数種類の簡易パラメータに置き換える。**

        落としたもの：LX480の、連続的かつ精密な**「臨界時間窓」**のコントロール。
        **「立ち上がりの質感を変えられる」という体験自体**は残ります（設計書4-6）。

        設計書は「線形／指数などから**選べる**程度」と書いていますが、
        **つまみ1本の両端**にしました——他の操作子が全部つまみの中に
        コンボが2つ混ざると、そこだけ別の機械に見えます。
        **中身は設計書と同じ**（線形も指数も、この1本の上にあります）。

        ─────────────────────────────────────────────────────────────────
        何をするか
        ─────────────────────────────────────────────────────────────────

        | | 変えるもの | 効く先 |
        |---|---|---|
        | `Size` | 部屋の寸法 | **初期反射とテールの両方** |
        | `Spread` | 初期反射が**散らばる幅** | 初期反射だけ |
        | `Shape` | 初期反射の**山の位置** | 初期反射だけ |

        `Shape`が0なら**頭が重い**（近くの壁がはっきり返る）、
        1なら**後ろが重い**（遠くからゆっくり湧いてくる）。

        **どちらも0.5が「表のまま」**です——Phase 2までと同じ音になります
        （ディレイで引いた線の③：**既定値は前のPhaseと同じ音**）。 */
    inline constexpr double maxSpreadScale = 2.0;

    /** `spread`（0〜1）を、時間の倍率へ。**0.5で1.0倍。** */
    inline double spreadScaleFor (float spread)
    {
        return std::pow (maxSpreadScale, (double) (juce::jlimit (0.0f, 1.0f, spread) - 0.5f) * 2.0);
    }

    /** `shape`（0〜1）を、傾きへ。**0.5で0＝傾けない。**

        タップ`i`（0〜7）の重みは`exp(傾き × (i/7 − 0.5))`。
        端で`e^±1.25`＝**3.5倍の差**が付きます。 */
    inline float shapeSlopeFor (float shape)
    {
        return 2.5f * (juce::jlimit (0.0f, 1.0f, shape) - 0.5f) * 2.0f;
    }
}
