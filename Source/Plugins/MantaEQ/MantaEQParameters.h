#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Manta EQ のパラメータ定義（設計書3章「パラメータ設計」）。

    ### バンド数は12で固定

    仕様書5章の「固定 vs 可変24バンド」は、**固定**にしました。
    ホストオートメーションは`"insert:<スロット>:<パラメータ番号>"`という
    **番号**で覚えているので（仕様書5.6）、本数が動くとオートメーションの
    行き先が変わります。**使うぶんだけオンにする**形にしてあり、
    切ってあるバンドは係数計算も走りません。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。

    - バンド1の12個 → バンド2の12個 → … → バンド12の12個 → 出力セクション
    - **足すときは、いちばん末尾へ**（出力セクションのさらに後ろ）

    ### 単位と範囲

    | | 範囲 | 目盛り |
    |---|---|---|
    | Freq | 20Hz〜20kHz | 対数（1kHzが真ん中） |
    | Gain | -30〜+30 dB | 等間隔 |
    | Q | 0.025〜40 | 対数（0.707が真ん中付近） |
    | Dynamic Threshold | -60〜0 dB | 等間隔 |
    | Dynamic Range | -30〜+30 dB | 等間隔（正で「上げる」、負で「下げる」） |
    | Attack | 0.1〜200 ms | 対数 |
    | Release | 5〜2000 ms | 対数 |
*/
namespace MantaEQParams
{
    //==========================================================================
    /** バンド数。**変えないこと**（パラメータの本数が変わる＝オートメーションがずれる）。 */
    inline constexpr int numBands = 12;

    /** カット系フィルタの縦続段数の上限（96 dB/oct ＝ 16次 ＝ 8段）。 */
    inline constexpr int maxSectionsPerBand = 8;

    //==========================================================================
    /** 仕様書4.2：フィルター形状。

        **並びを変えないこと**（`juce::AudioParameterChoice`は番号で保存します）。
        足すときは末尾へ。 */
    enum class Shape
    {
        bell = 0,
        lowShelf,
        highShelf,
        lowCut,
        highCut,
        notch,
        bandPass,
        tiltShelf,
        allPass,

        numShapes
    };

    /** 仕様書4.4：ステレオ配置。**並びを変えないこと。** */
    enum class Channel
    {
        stereo = 0,
        left,
        right,
        mid,
        side,

        numChannels
    };

    /** 仕様書4.7：処理モード。**並びを変えないこと。**

        Natural Phaseは仕様書8章で見送りが決まっています
        （アルゴリズムが非公開で、設計の当てが付かない）。 */
    enum class ProcessingMode
    {
        zeroLatency = 0,
        linearPhase,

        numModes
    };

    /** 仕様書4.3：スロープ（Pro-Q3方式の段階選択）。

        Pro-Q4の「1 dB/oct単位の連続可変」は見送りました——
        段数を可変にする設計が要り（仕様書4.3の🔴）、
        **段階式でも実用上の不足がありません。** */
    inline constexpr int slopeChoicesDbPerOctave[] { 6, 12, 18, 24, 36, 48, 72, 96 };
    inline constexpr int numSlopeChoices = (int) (sizeof (slopeChoicesDbPerOctave) / sizeof (int));

    juce::StringArray getShapeNames();
    juce::StringArray getChannelNames();
    juce::StringArray getSlopeNames();

    /** その形状がGainを使うか（使わないものはGUIでつまみを伏せる）。 */
    bool shapeUsesGain (Shape shape);

    /** その形状がSlopeを使うか（LowCut／HighCutだけ）。 */
    bool shapeUsesSlope (Shape shape);

    //==========================================================================
    // パラメータID。設計書3章の命名規則（`band{n}_xxx` / `output_xxx`）に合わせてある。
    // **nは1始まり**（画面の表示と揃える）。

    juce::String bandParamId (int bandIndex, const char* suffix);

    /** **バンドがあるかどうか**（削除で消える／カーブのダブルクリックで増える）。

        `bandActive`（＝通すかどうか）と対です。**2つある理由**はPhase 206：
        「オフにする」だけだと、**バイパスして聴き比べる**ことができませんでした
        （消してしまうと、周波数もQも戻せない）。

        | | 消える？ | 音を通す？ | カーブ |
        |---|---|---|---|
        | `enabled = false` | **消える**（スロットが空く） | — | 出ない |
        | `active = false` | 残る | **通さない** | 点線で残る |*/
    inline constexpr const char* bandEnabled   = "enabled";
    inline constexpr const char* bandFreq      = "freq";
    inline constexpr const char* bandGain      = "gain";
    inline constexpr const char* bandQ         = "q";
    inline constexpr const char* bandShape     = "shape";
    inline constexpr const char* bandSlope     = "slope";
    inline constexpr const char* bandChannel   = "stereoPlacement";
    inline constexpr const char* bandDynOn     = "dynEnabled";
    inline constexpr const char* bandDynThresh = "dynThreshold";
    inline constexpr const char* bandDynRange  = "dynRange";
    inline constexpr const char* bandDynAttack = "dynAttack";
    inline constexpr const char* bandDynRelease = "dynRelease";

    inline constexpr const char* outputGain    = "output_gain";
    inline constexpr const char* outputPan     = "output_pan";
    inline constexpr const char* outputMsBalance = "output_msBalance";
    inline constexpr const char* outputPhaseInvert = "output_phaseInvert";
    inline constexpr const char* outputAutoGain = "output_autoGain";

    // 仕様書4.7：処理モード（Phase 205で末尾へ追加。**足すときは末尾へ**）
    inline constexpr const char* processingMode = "processing_mode";
    inline constexpr const char* processingResolution = "processing_resolution";

    juce::StringArray getProcessingModeNames();
    juce::StringArray getResolutionNames();

    /** バイパスしていないか（Phase 206で追加。**末尾に足してあります**）。
        `bandEnabled`の説明の表を読むこと。 */
    inline constexpr const char* bandActive = "active";

    //==========================================================================
    /** 1バンドぶんの、いまの値をまとめたもの。

        **音を出す側と、カーブを描く側の両方がこれを使います**（1.27）。
        別々に読むと、**聴こえている音と描いてある線がずれます**——
        しかも「ずれている」と気づけない種類のずれ方をします。 */
    struct BandSettings
    {
        bool enabled = false;
        bool active = true;      ///< バイパスしていないか（`bandEnabled`の表）
        float frequency = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.707f;
        Shape shape = Shape::bell;
        int slopeDbPerOctave = 24;
        Channel channel = Channel::stereo;

        bool dynamicEnabled = false;
        float dynamicThresholdDb = -20.0f;
        float dynamicRangeDb = 0.0f;
        float dynamicAttackMs = 20.0f;
        float dynamicReleaseMs = 200.0f;

        /** ダイナミクスで足されるゲイン（dB）。**保存される値ではありません**
            ——音を出す側が毎ブロック入れ、カーブを描く側はそれを読んで
            「いま実際に掛かっている形」を描きます。 */
        float dynamicOffsetDb = 0.0f;

        /** 係数計算に実際に使うゲイン。 */
        float effectiveGainDb() const { return gainDb + dynamicOffsetDb; }
    };

    /** 出力セクション（仕様書4.16）。 */
    struct OutputSettings
    {
        float gainDb = 0.0f;
        float pan = 0.0f;          ///< -1（左）〜+1（右）
        float msBalance = 0.0f;    ///< -1（Midのみ）〜+1（Sideのみ）
        bool phaseInvert = false;
        bool autoGain = false;

        ProcessingMode mode = ProcessingMode::zeroLatency;
        int linearPhaseIrLength = 4096;   ///< Linear Phaseのときの処理解像度
    };

    //==========================================================================
    /** APVTSのレイアウトを作る。**並び順がオートメーションの番号**です。 */
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** バンドの既定周波数（対数で散らしてある）。

        **バンドを1つずつオンにしていったときに、重ならない場所へ出る**ように
        してあります。全部1kHzだと、2つ目を出した瞬間に1つ目と重なって
        「増えていない」ように見えます。 */
    float getDefaultFrequency (int bandIndex);

    //==========================================================================
    // 目盛りの変換（GUIと係数計算の両方で使う。1.27）

    /** Hz → 0〜1（対数）。 */
    float frequencyToProportion (float hz);

    /** 0〜1 → Hz（対数）。 */
    float proportionToFrequency (float proportion);

    inline constexpr float minFrequency = 20.0f;
    inline constexpr float maxFrequency = 20000.0f;
    inline constexpr float maxGainDb = 30.0f;
    inline constexpr float minQ = 0.025f;
    inline constexpr float maxQ = 40.0f;
}
