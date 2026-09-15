#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Manta Reverb のパラメータ定義（リバーブ仕様書5章）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。
    **足すときは、いちばん末尾へ。**

    ### エンジンごとのIDは、いまから分けてあります

    ディレイでは Phase 5 になってから`b_`を足したので、
    **エンジン共通の`Mix`と`Output`がエンジンAの並びの真ん中に取り残されました**
    （8.217。後ろへ動かすと番号がずれるので、直せません）。

    ここでは**最初からその形にしてあります**：

    ```
        エンジンA：predelay   decay   size   …     （`engineParamId(0, …)`＝素通し）
        共通　　：mix   output
        エンジンB：b_predelay b_decay b_size …     （Phase 5で末尾へ足す）
    ```

    `engineParamId()`はもう居ますが、**エンジンはまだ1つです**——
    `numEngines`を2にするのはPhase 5で、それまでBのIDは作りません
    （使わないIDを保存ファイルへ入れない。9.5）。

    ### どこまで入っているか（設計書8章の段階表）

    | | 中身 |
    |---|---|
    | **Phase 1（いまここ）** | **Room 1系統。Predelay／Decay／Size／Diffusion／Damping／Early／Width／Mix／Output** |
    | Phase 2 | `algorithm`（Plate・Hall） |
    | Phase 3 | Ambience、Size/Shape/Spread |
    | Phase 4 | Twin Delays、Panorama |
    | Phase 5 | `routingMode`とエンジンB |
    | Phase 6 | Random Hall、Saturation |
*/
namespace MantaReverbParams
{
    //==========================================================================
    /** エンジンの数。8.249：**Phase 5で2になりました**（Phase 259）。 */
    inline constexpr int numEngines = 2;

    /** エンジンごとのID。**エンジン0は素通し**（上の説明）。 */
    juce::String engineParamId (int engine, const char* suffix);

    //==========================================================================
    // 5章：エンジンごとのもの

    /** 原音が鳴ってから響きが始まるまで（ms）。

        **短い部屋でも、少し空けると原音が前に出ます。**
        上限は`MantaReverbEngine::maxPredelayMs`と揃えること。 */
    inline constexpr const char* predelay = "predelay";

    /** RT60（秒）。60dB落ちるまでの時間。 */
    inline constexpr const char* decay = "decay";

    /** 部屋の大きさ（%）。**ディレイラインと初期反射の長さが両方変わります。** */
    inline constexpr const char* size = "size";

    /** 拡散の強さ（%）。0なら入口の拡散を通しません。 */
    inline constexpr const char* diffusion = "diffusion";

    inline constexpr const char* highDampFreq = "hfDampFreq";
    inline constexpr const char* highDampAmount = "hfDamp";
    inline constexpr const char* lowDampFreq = "lfDampFreq";
    inline constexpr const char* lowDampAmount = "lfDamp";

    /** 初期反射の量（%）。テールは常に1倍です。 */
    inline constexpr const char* earlyLevel = "early";

    /** 響きの広がり（%）。**100%が素通し**（0%ではありません）。 */
    inline constexpr const char* width = "width";

    //==========================================================================
    // エンジン共通

    inline constexpr const char* mix = "mix";
    inline constexpr const char* outputGain = "outputGain";

    //==========================================================================
    // 8.243：Phase 2（Phase 255）。**足すのは末尾**（上の注意書き）

    /** どのアルゴリズムか（`MantaReverbAlgorithm::Kind`）。

        **エンジンごとのものですが、並びは末尾です。**
        エンジンの塊の中へ入れると、Phase 1で保存したプロジェクトの
        `mix`と`outputGain`が**別のつまみになります**（9.5）。

        > つまり「意味の並び」と「番号の並び」は、もう揃っていません。
        > **揃えるために番号を動かさないこと**——ディレイが8.217で同じ形になっています。 */
    inline constexpr const char* algorithm = "algorithm";

    //==========================================================================
    // 8.246：Phase 3（Phase 256）。**足すのは末尾**

    /** 初期反射の山の位置（0＝頭が重い、1＝後ろが重い）。**0.5が「表のまま」**。 */
    inline constexpr const char* shape = "shape";

    /** 初期反射の散らばる幅（0.5で1.0倍）。**初期反射だけに効きます**——
        テールまで動かすのは`Size`のほうです。 */
    inline constexpr const char* spread = "spread";

    //==========================================================================
    // 8.247：Phase 4a（Phase 257／仕様書5章「Twin Delays：Time、Feedback、Cross-Feedback」）。
    //
    // **`Twin Delays`でしか効きません。** それでも並びは末尾です——
    // アルゴリズムごとにIDを分けると、**選び直すたびに別のつまみになる**ので、
    // オートメーションが追えません。

    inline constexpr const char* twinTime = "twinTime";
    inline constexpr const char* twinFeedback = "twinFeedback";
    inline constexpr const char* twinCross = "twinCross";

    //==========================================================================
    // 8.248：Phase 4b（Phase 258／仕様書5章
    // 「Panorama：Width、Phase Invert、Channel Swap、Mono Sum」）。
    //
    // **`Width`は足しません**——Phase 1からある共通のつまみがそのまま使えます。
    // 同じ意味のつまみを2つ持つと、**どちらが効いているのか**の話になります。

    inline constexpr const char* panMonoSum = "panMonoSum";
    inline constexpr const char* panInvertRight = "panInvert";
    inline constexpr const char* panSwap = "panSwap";

    //==========================================================================
    // 8.249：Phase 5（Phase 259／仕様書3-1・5章）

    /** そのエンジンの出口の音量（dB）。**仕様書5章の共通パラメータ`Level`**。

        Phase 1では作りませんでした——エンジンが1つしかなく、`Output`と
        区別が付かなかったためです。**2つになって初めて意味を持ちます**
        （`Dual`や`Split`でAとBの釣り合いを取る）。 */
    inline constexpr const char* engineLevel = "level";

    /** ルーティング（`MantaReverbRouting::Mode`）。**エンジン共通**。 */
    inline constexpr const char* routingMode = "routingMode";

    //==========================================================================
    // 8.250〜8.251：Phase 6（Phase 260）

    /** テールの揺れの深さ。**`Random Hall`だけで効きます**（0で揺らしません）。 */
    inline constexpr const char* modulation = "modulation";

    /** 歪みの掛かり具合。**0でOff**（仕様書のOn/Off＋Thresholdを1本にまとめたもの。
        理由は`MantaReverbSaturation`のヘッダ）。 */
    inline constexpr const char* saturation = "saturation";

    //==========================================================================
    // 範囲（**画面とDSPの両方が見ます**。1.27）

    inline constexpr double minPredelayMs = 0.0;
    inline constexpr double maxPredelayMs = 250.0;

    inline constexpr double minDecaySeconds = 0.2;
    inline constexpr double maxDecaySeconds = 12.0;

    /** `size`のつまみ（0〜1）を、長さの倍率へ移すところ。

        **画面もDSPもここを通すこと**（1.27）——別々に書くと、
        描いている部屋の大きさと鳴っている大きさがずれます。

        `0.5`で1.0倍。両端が`0.5倍`と`2.0倍`（`MantaReverbFdn::maxSizeScale`）です。
        **線形ではなく指数**：大きさは「倍で聞く」ものなので、
        線形だと小さい側が使いものになりません（ディレイのTimeと同じ話）。 */
    float sizeScaleFor (float normalised);

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
