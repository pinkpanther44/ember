#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    8.288：**Orangutan Drums のパラメータ**（Phase 281）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    **足すときは、いちばん末尾へ。**

    ```
      0 〜 143   パッド16個 × 9つ（ENGINE TUNE DECAY TONE SNAP LEVEL PAN SEND OUT）
    144 〜 149   マスター（DRIVE GLUE REVERB SIZE DAMP VOLUME）
    ```

    **150個あります。** 内蔵プラグインではいちばん多く、`Manta Synthesizer`に
    次ぐ数です——パッドごとに9つ持つので、16倍に効きます。

    ### 8.289：**並びが変わりました**（Phase 282／本人の要望）

    | | Phase 281 | いま |
    |---|---|---|
    | パッド | 8つ×16＝128 | **9つ×16＝144**（`OUT`が戻りました） |
    | ベースゾーン | 3つ（134〜136） | **ありません**（本人の指定で廃止） |
    | 合計 | 137 | **150** |

    **番号で覚えているオートメーションは、ここで一度ずれます。**
    **公開前なので、いま揃えておきます**——出したあとでは動かせません。
    IDは変えていないので、**保存済みのプロジェクトのつまみの値は戻ります**
    （`apvts`はIDで引き当てます。番号で引くのはオートメーションだけ）。

    ### `MAGAZINE`から持って来なかったもの

    | | なぜ |
    |---|---|
    | シークエンサ（6つ） | **本人の指定**。本体のピアノロールとドラムエディタがあります |
    | ベースゾーン（3つ） | **本人の指定**（Phase 282で廃止）。`SUB 808`はパッドとしては残ります |

    **パラアウトは戻しました**（`out`）。本体が**ドラムアウトトラック**を持っていて、
    音源の1番以降のバスを別トラックで受けられます（8.144）——
    受け皿があるときだけバスが有効になるので、**普段は2ch出力のまま**です。

    ### パッドのIDは組み立てます

    `padId(0, "tune")`→`"p00_tune"`。**綴りはここだけ**です——
    工場キット（`OrangutanDrumsKits.h`）も画面も、この関数を通ります。

    > **`p00`から数えます**（画面の`PAD 01`は1から）。
    > ずれているように見えますが、**IDは変えられません**（保存の鍵）。
*/
namespace OrangutanDrumsParams
{
    inline constexpr int numPads = 16;

    /** パッド1つぶんのパラメータID。**綴りの出どころはここだけ。** */
    inline juce::String padId (int pad, const char* suffix)
    {
        return "p" + juce::String (pad).paddedLeft ('0', 2) + "_" + suffix;
    }

    // パッドのつまみ（`padId()`へ渡す接尾辞）
    inline constexpr const char* padEngine = "eng";
    inline constexpr const char* padTune   = "tune";
    inline constexpr const char* padDecay  = "dcy";
    inline constexpr const char* padTone   = "tone";
    inline constexpr const char* padSnap   = "snap";
    inline constexpr const char* padLevel  = "lvl";
    inline constexpr const char* padPan    = "pan";
    inline constexpr const char* padSend   = "send";

    /** 8.289：**このパッドの出口**（Phase 282／本人の要望で復活）。
        `MAIN`＝マスター段を通っていつもの出力へ／`DIRECT`＝そのパッド専用のバスへ。 */
    inline constexpr const char* padOut = "out";

    // マスター
    inline constexpr const char* drive  = "mDrive";
    inline constexpr const char* glue   = "mGlue";
    inline constexpr const char* reverb = "mRev";
    inline constexpr const char* size   = "mSize";
    inline constexpr const char* damp   = "mDamp";
    inline constexpr const char* volume = "mVol";

    /** エンジンの選択肢。**`engineName()`から作ります**（名前を2箇所に書かない。1.27）。 */
    juce::StringArray getEngineNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
