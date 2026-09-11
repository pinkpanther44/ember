#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/**
    Manta Delay のパラメータ定義（ディレイ仕様書5章）。

    ### 並び順を変えないこと

    HANDOVER 9.5：**オートメーションはパラメータの番号で覚えています。**
    順番を変えると、保存済みのオートメーションが別のつまみに付きます。
    **足すときは、いちばん末尾へ。**

    ### いま入っているのはPhase 1だけです

    設計書8章の段階表に従っています。**Phase 1はSingle Echo／Digital Cleanのみ。**

    | | 中身 |
    |---|---|
    | Phase 1 | Time／Sync／Feedback／Mix／出力レベル |
    | **Phase 2（いまここ）** | キャラクター（BBD・Tape・Lo-Fi）、Drive／Tone／Wow／Flutter |
    | Phase 3 | フィードバック内フィルター、LFO、ダッキング |
    | Phase 4 | マルチタップ |
    | Phase 5 | デュアルエンジンとルーティング |
    | Phase 6 | リバース、ディフュージョン、UIの仕上げ |

    **先の段階のパラメータをいま作り置きしないこと。** 使われないIDが
    保存ファイルへ入り、あとで形が変わったときに**読めるのに意味が違う**状態になります。
*/
namespace MantaDelayParams
{
    //==========================================================================
    // 5-1：基本

    /** フリー（ms）のディレイタイム。`sync`がONのときは使いません。 */
    inline constexpr const char* timeMs = "timeMs";

    /** テンポシンクのON/OFF。 */
    inline constexpr const char* sync = "sync";

    /** シンクしているときの音価（下の`getSyncDivisionCount()`の並び）。 */
    inline constexpr const char* syncDivision = "syncDivision";

    inline constexpr const char* feedback = "feedback";
    inline constexpr const char* mix      = "mix";

    /** 出口のレベル（dB）。**Mixとは別に持ちます**——
        Mixは原音との混ぜ具合、こちらは全体の音量です。 */
    inline constexpr const char* outputGain = "outputGain";

    //==========================================================================
    // 8.208：Phase 2（キャラクター）。**足すのは末尾**（上の注意書き）

    inline constexpr const char* character = "character";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* tone = "tone";
    inline constexpr const char* wowRate = "wowRate";
    inline constexpr const char* wowDepth = "wowDepth";
    inline constexpr const char* flutterRate = "flutterRate";
    inline constexpr const char* flutterDepth = "flutterDepth";

    //==========================================================================
    /** 5-1：音価の一覧。**並びを変えないこと**（保存されるのは番号です）。

        付点（Dotted）と3連（Triplet）は仕様書5-1の指定どおり。
        **短いほうから長いほうへ**並べてあります——つまみを右へ回すと遅くなります。 */
    enum class SyncDivision
    {
        thirtySecond,       ///< 1/32
        sixteenthTriplet,   ///< 1/16T
        sixteenth,          ///< 1/16
        sixteenthDotted,    ///< 1/16.
        eighthTriplet,      ///< 1/8T
        eighth,             ///< 1/8
        eighthDotted,       ///< 1/8.
        quarterTriplet,     ///< 1/4T
        quarter,            ///< 1/4
        quarterDotted,      ///< 1/4.
        halfTriplet,        ///< 1/2T
        half,               ///< 1/2
        halfDotted,         ///< 1/2.
        whole,              ///< 1/1

        count
    };

    inline constexpr int getSyncDivisionCount() { return (int) SyncDivision::count; }

    /** 表示名。**ASCIIのみ**（プラグインのつまみの表示は訳しません。9.5）。 */
    inline juce::StringArray getSyncDivisionNames()
    {
        return { "1/32", "1/16T", "1/16", "1/16.",
                 "1/8T", "1/8",  "1/8.",
                 "1/4T", "1/4",  "1/4.",
                 "1/2T", "1/2",  "1/2.",
                 "1/1" };
    }

    /** その音価が**4分音符いくつぶん**か。

        **ここが換算の唯一の場所です**（1.27）。ディスプレイも音の側も、
        ディレイタイムはこの関数から出た値を使います——
        別々に数えると、**描いている線と鳴っている位置がずれます。**

        - 付点は1.5倍
        - 3連は2/3倍 */
    inline double getQuarterNotes (SyncDivision division)
    {
        switch (division)
        {
            case SyncDivision::thirtySecond:     return 0.125;
            case SyncDivision::sixteenthTriplet: return 0.25 * 2.0 / 3.0;
            case SyncDivision::sixteenth:        return 0.25;
            case SyncDivision::sixteenthDotted:  return 0.25 * 1.5;
            case SyncDivision::eighthTriplet:    return 0.5 * 2.0 / 3.0;
            case SyncDivision::eighth:           return 0.5;
            case SyncDivision::eighthDotted:     return 0.5 * 1.5;
            case SyncDivision::quarterTriplet:   return 1.0 * 2.0 / 3.0;
            case SyncDivision::quarter:          return 1.0;
            case SyncDivision::quarterDotted:    return 1.0 * 1.5;
            case SyncDivision::halfTriplet:      return 2.0 * 2.0 / 3.0;
            case SyncDivision::half:             return 2.0;
            case SyncDivision::halfDotted:       return 2.0 * 1.5;
            case SyncDivision::whole:            return 4.0;

            case SyncDivision::count:
            default:                             return 1.0;
        }
    }

    //==========================================================================
    /** 7章：**最大ディレイタイム。** 仕様書では「要確定（例：4000ms）」でしたが、
        **4000msに決めました**。

        1/1でも40BPMまでは届きます（4分音符4つ＝6秒…には足りませんが、
        そこまで遅い曲でディレイを1小節に合わせる場面は考えにくい）。
        **伸ばすとそのぶんメモリを持ちます**（44.1kHz・ステレオで約1.4MB／秒）。 */
    inline constexpr double maxDelayMs = 4000.0;

    /** つまみで直に触れる下限。**0msにはしません**——
        0だとディレイではなくなり、フィードバックが発振の形になります。 */
    inline constexpr double minDelayMs = 1.0;

    /** 仕様書5-1：フィードバックの上限。

        **100%は入れていません**（仕様書は「将来的に100%超も検討」）。
        1.0にすると減衰しないので、**止める手段が無いまま鳴り続けます。**
        Freezeは別の機能として持つべきもの（Phase 3以降）。 */
    inline constexpr float maxFeedback = 0.95f;

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
