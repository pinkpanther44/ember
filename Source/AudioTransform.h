#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "WarpMap.h"   // 8.150：ソース↔鳴る時刻の対応表（Phase 188/8.48）

#include <functional>

//==============================================================================
/**
    8.147／8.149：**オーディオの音程と長さを作り直す**
    （Phase 185／改善案㉞、Phase 187／8.48。仕様書5.5・5.5.1）。

    ### 何をどうするか

    **ソースのファイル1本まるごとを作り直します。**
    出来たものはキャッシュへ置いておき、再生側はそれを開くだけです。

    | | 音程（`semitones`） | 伸縮（`stretch`） |
    |---|---|---|
    | 出来上がりの長さ | **変わらない** | **`× stretch`** |
    | 音程 | **`× 2^(n/12)`** | **変わらない** |
    | サンプルレート・チャンネル数 | 変わらない | 変わらない |

    **2つは独立して掛けられます**（+3半音で1.5倍に伸ばす、など）。

    ### なぜ「クリップの範囲だけ」ではないのか

    クリップは`offset`と`length`でソースの一部を指しています。
    範囲だけ作り直すと、**後でトリムし直すたびに作り直し**になります。

    まるごとなら、**ソースの位置`t`は出来上がりの`t × stretch`**という
    1つの掛け算で済み、再生側は`offset`にそれを掛けるだけです。

    ### なぜ「鳴らしながら」ではないのか

    `ClipPlayerProcessor`は**毎ブロック好きな位置から読むだけ**で、状態を持ちません
    （`setNextReadPosition()`→読む）。伸縮の道具は**状態を持つ**ので、
    リアルタイムにすると**シーク・ループの折り返し・道具自身のレイテンシ**の
    始末が全部増えます。**書き出して差し替えるなら、再生経路はほとんど変わりません。**

    ### 中身（自前の位相ボコーダー）

    `juce::dsp::FFT`で書いてあります。**外部ライブラリもライセンスの話もありません。**

    音程を`p`倍・長さを`S`倍にするには：

    1. **時間を`S × p`倍に伸ばす**（位相ボコーダー。音程はそのまま）
    2. **`p`倍の速さで読み直す**（長さが`S`倍に戻り、音程が`p`倍になる）

    音程だけなら`S = 1`（伸ばして同じだけ速く読む）、
    伸縮だけなら`p = 1`（伸ばして等速で読む＝読み直しが要らない）。

    伸ばすほうは**合成側のホップを`fftSize/4`に固定**して、
    分析側のホップを`Hs ÷ (S×p)`にしています。逆（分析を固定）にすると、
    **縮めるほど出力の重なりが減って**荒れます。

    **山の位相だけを進めて、裾は差を保たせます**（Laroche & Dolsonの位相ロック）。
    これをやらないと、**ビンごとに位相が食い違って打ち消し合い、音が小さくなります**
    ——実測で+12半音のとき-12dBでした（8.147）。

    > **得手不得手があります。** ±数半音・±数十%の持続音（ボーカル・パッド・ベース）なら
    > 実用になりますが、**打楽器と大きな移動は苦手**です（窓が42ms前後あるので、
    > 立ち上がりが前後20msほど滲みます）。
    > 変換は`process()`1箇所なので、**気に入らなければここだけ差し替えられます**
    > （RubberBand等）。
*/
namespace AudioTransform
{
    /** 半音の上限（上下とも）。**これ以上は音がもたない**ので、UIもここで止めます。 */
    inline constexpr int maxSemitones = 24;

    /** 8.149：伸縮の倍率の範囲（Phase 187／8.48）。**タイムラインの秒 ÷ ソースの秒**です。
        1より大きいと**遅く・長く**なります。外れるほど滲みが増えるので、ここで止めます。 */
    inline constexpr double minStretch = 0.25;
    inline constexpr double maxStretch = 4.0;

    /** 半音 → 周波数の倍率。**式を2箇所に書かないため**に置いています。 */
    double getPitchRatio (int semitones);

    /** 作り直しが要らない組み合わせか（＝素のファイルをそのまま鳴らせるか）。

        **判定はここ1箇所**（1.27）。「0半音・等倍」を各所で書くと、
        浮動小数の比べ方がずれます。 */
    bool isIdentity (int semitones, double stretch);

    /** 8.150：**折れている表は、全体が等倍でも作り直しが要ります**（Phase 188）。 */
    bool isIdentity (int semitones, const WarpMap& map);

    /** その組み合わせで出来上がるキャッシュのファイル。**在るかどうかは見ません。**

        名前には**ソースのパス・大きさ・更新時刻**を混ぜた値が入るので、
        ソースを差し替えると別のファイルになります（古いキャッシュを掴みません）。 */
    juce::File getRenderedFileFor (const juce::File& source, int semitones, double stretch);

    /** 8.150：折れ線ぶんも名前へ混ぜる（Phase 188）。
        **マーカーを1つ動かしたら別のファイル**になります。 */
    juce::File getRenderedFileFor (const juce::File& source, int semitones, const WarpMap& map);

    /** キャッシュの置き場所（`AppData/PersonalDAW/TransposeCache`）。

        **環境設定で変えられる場所には置きません**（`StorageLocations`の仲間にしない）。
        中身は**いつでも作り直せる**もので、プロジェクトの一部ではありません——
        消えても設定は残っているので、次に開いたときに作り直されます。 */
    juce::File getCacheFolder();

    /** 作る。**時間がかかるので、メッセージスレッドから呼ばないこと。**

        成功すれば空文字、失敗すればその理由を返します。
        既に在って中身が読めるなら、**何もせず空文字を返します**。

        `shouldAbort`がtrueを返したら、途中で止めて**作りかけを消します**
        （中途半端なファイルが残ると、次からそれを本物として掴みます）。
        **止めた回も空文字を返します**——呼ぶ側で`shouldAbort()`を訊き直すこと（8.148）。 */
    juce::String renderToFile (const juce::File& source, int semitones, double stretch,
                                std::function<bool()> shouldAbort = nullptr);

    /** 8.150：**表のとおりに**作る（Phase 188/8.48）。上のものは、
        点2つの表を渡すだけの薄い包みです。 */
    juce::String renderToFile (const juce::File& source, int semitones, const WarpMap& map,
                                std::function<bool()> shouldAbort = nullptr);

    /** キャッシュが大きくなりすぎたら、**古いものから消す**。

        呼ばないと際限なく増えます（`renderToFile()`の後で呼んでいます）。 */
    void pruneCache (juce::int64 maxTotalBytes = 2LL * 1024 * 1024 * 1024);

    /** その場で作り直す（ファイルを介さない）。**自己検査と、他から使う用。**

        8.149：**新しいバッファを返します**（Phase 187）。伸縮すると長さが変わるので、
        入れ物を書き換える形では収まりません。出来上がりの長さは
        `round(入力の長さ × stretch)`です。

        途中で止められたら**空のバッファ**を返します。 */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                       int semitones, double stretch,
                                       std::function<bool()> shouldAbort = nullptr);

    /** 8.150：**表のとおりに**作り直す（Phase 188/8.48）。

        `sampleRate`は、サンプル数と秒を読み替えるのに使います
        （表は秒で書かれているため）。出来上がりの長さは
        `表(入力の長さ) - 表(0)`です。 */
    juce::AudioBuffer<float> process (const juce::AudioBuffer<float>& input,
                                       int semitones, const WarpMap& map, double sampleRate,
                                       std::function<bool()> shouldAbort = nullptr);
}
