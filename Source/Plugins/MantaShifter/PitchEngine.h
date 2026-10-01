#pragma once

#include <memory>

//==============================================================================
/**
    8.336：ピッチ＋フォルマントの変換エンジン（設計書（ライト版）3章の`IPitchEngine`）。

    **エンジンは2つ**あります（8.337）：

    | | 中身 | 得意 | 苦手 |
    |---|---|---|---|
    | `spectral` | Signalsmith Stretch（位相ボコーダー。ライブラリ） | 和音まじり・息の多い声でも崩れにくい | 純音の音程がビンの格子に引かれる（最大26セント） |
    | `psola` | TD-PSOLA（自作。`PsolaEngine.cpp`） | 音程が周期そのもので決まる（純音も正確） | 検出を誤ると濁る・大きく下げるとざらつく |

    本体（`ShifterEngine`）はこの形しか知りません。

    ### 時刻の約束

    `setRatio()`などは「**いま渡した入力から`inputLatency()`だけ前の位置**」に効きます。
    本体は検出窓とMIDIをその位置に合わせてから渡します（設計書6.2「時刻合わせ」）。
    `setPitchInfo()`で渡す検出結果も、同じ位置のものです。

    ### レイテンシーは揃える

    **切り替えてもレイテンシーが変わらない**よう、PSOLA は Signalsmith 版と同じ遅れを名乗ります
    （`matchLatency()`。本体が`prepare()`の前に呼ぶ）。
*/
class IPitchEngine
{
public:
    virtual ~IPitchEngine() = default;

    /** `prepare()`の前に呼ぶ。この遅れに合わせられるエンジンだけが使う（PSOLA）。 */
    virtual void matchLatency (int /*inputLatency*/, int /*outputLatency*/) {}

    /** 非RT。全部をここで確保する。 */
    virtual void prepare (double sampleRate, int numChannels, int maxBlock) = 0;
    virtual void reset() = 0;

    virtual int inputLatency() const = 0;    ///< D
    virtual int outputLatency() const = 0;

    virtual void setRatio (float ratio) = 0;
    /** `followPitch`＝Link（フォルマントがピッチに付いていく）。 */
    virtual void setFormant (float factor, bool followPitch) = 0;
    /** 基準の f0（Hz）。0 以下ならエンジンに任せる。 */
    virtual void setFormantBase (float f0Hz) = 0;
    /** 検出の結果（有声か・f0）。**PSOLA はこれでピッチマークを打つ**。Signalsmith 版は使わない。 */
    virtual void setPitchInfo (bool /*voiced*/, float /*f0Hz*/) {}

    /** RT。入力と出力は同じ長さ。 */
    virtual void process (const float* const* in, float* const* out, int numSamples) = 0;
};

/** 保存される番号（**並べ替えないこと**）。 */
enum class PitchEngineType { spectral = 0, psola = 1 };

std::unique_ptr<IPitchEngine> createSignalsmithEngine();
std::unique_ptr<IPitchEngine> createPsolaEngine();

inline std::unique_ptr<IPitchEngine> createPitchEngine (PitchEngineType type = PitchEngineType::spectral)
{
    return type == PitchEngineType::psola ? createPsolaEngine() : createSignalsmithEngine();
}
