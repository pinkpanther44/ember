#pragma once

#include <juce_data_structures/juce_data_structures.h>

//==============================================================================
/**
    8.329：アナライザーの設定一式（アナライザー仕様書6.1／設計書3.1・7.2）。

    ### パラメータにしない

    **どれも音に影響しない**ので、ホストのオートメーション用パラメータにはしません
    （仕様書6章）。`AudioProcessorValueTreeState`も使わず、この構造体を
    **`ValueTree`へ書いて`getStateInformation()`で保存**します。

    設計書7.2は「キーと値の組（JSON）、先頭に`schema: 1`」と書いていますが、
    これはホストを仮定しない書き方でした。Manta Studioの内蔵プラグインは
    **みな`ValueTree`をXMLで保存**しているので、形はそちらに揃え、
    **`schema`属性・知らないキーは無視・欠けたキーは初期値**という中身だけ守っています。

    ### 範囲は読むときに寄せる

    `fromTree()`は**必ず範囲内へ寄せて**返します。手で書き換えたファイルや、
    将来の版が書いた値を読んでも、解析が壊れないように。
*/
struct AnalyzerSettings
{
    //--------------------------------------------------------------------------
    // 表示（フッター）

    bool curveVisible = true;       ///< スペクトラム曲線（仕様書3.6「表示切替」）
    bool peakVisible = true;        ///< ピーク曲線
    bool rangeVisible = true;       ///< ターゲットレンジ

    float lowPhon = 70.0f;          ///< 下限のラウドネスレベル（20〜90）
    float highPhon = 80.0f;         ///< 上限（下限＋3以上）

    //--------------------------------------------------------------------------
    // レンジの合わせ方（仕様書5.3）

    enum class Align { autoFollow = 0, calibrated = 1, manualOffset = 2 };

    Align align = Align::autoFollow;
    float calibrationDbfs = -20.0f; ///< 校正：この dBFS のピンクノイズが
    float calibrationSpl = 83.0f;   ///< この dB SPL
    float manualOffsetDb = 0.0f;    ///< 手動オフセット（±12）

    float rangeOpacity = 0.25f;     ///< レンジ帯の塗り（0〜0.6）
    bool highlightDeviation = true; ///< 逸脱の強調

    //--------------------------------------------------------------------------
    // 解析（仕様書4.1）

    bool realtimeVisible = false;   ///< リアルタイム曲線

    enum class Response { fast = 0, medium = 1, slow = 2, integrate = 3 };
    Response response = Response::medium;

    float peakHoldSeconds = 2.0f;   ///< 0.5〜10
    bool peakHoldInfinite = false;

    float smoothingOctaves = 1.0f / 3.0f;   ///< 0＝オフ、1/24〜1
    float slopeDbPerOctave = 4.5f;          ///< 0〜6、0.5刻み

    int fftSize = 8192;             ///< 2048〜32768（2のべき乗）

    enum class Window { hann = 0, blackmanHarris = 1 };
    Window window = Window::hann;

    float overlap = 0.75f;          ///< 0.5 か 0.75

    enum class Channel { sum = 0, left = 1, right = 2, mid = 3, side = 4 };
    Channel channel = Channel::sum;

    //--------------------------------------------------------------------------
    // 表示範囲

    float frequencyMin = 20.0f;     ///< 10〜100 Hz
    float frequencyMax = 20000.0f;  ///< 5k〜22k Hz
    float levelTopDb = 0.0f;        ///< 上端（+12〜−24）
    float levelRangeDb = 80.0f;     ///< 幅（48 / 60 / 80 / 100）
    bool gridVisible = true;

    //--------------------------------------------------------------------------

    /** 応答速度の減衰時定数（秒）。積算は無限大。 */
    float releaseSeconds() const;

    /** 立上りの時定数（仕様書4.2：50 ms）。 */
    static constexpr float attackSeconds = 0.05f;

    /** 選べる値（設定の画面と、読むときの寄せ先。**1箇所に置くこと**）。 */
    static const juce::Array<int>& fftSizeChoices();
    static const juce::Array<float>& smoothingChoices();   ///< オクターブ（0＝オフ）
    static const juce::Array<float>& levelRangeChoices();

    static constexpr int schemaVersion = 1;
    static const juce::Identifier treeType;

    juce::ValueTree toTree() const;

    /** 知らないキーは無視、欠けたキーは初期値、値は範囲へ寄せる。 */
    static AnalyzerSettings fromTree (const juce::ValueTree& tree);

    /** 範囲の外にある値を寄せ直す（`fromTree()`と、画面で変えたあと）。 */
    void constrain();

    bool operator== (const AnalyzerSettings&) const = default;
};
