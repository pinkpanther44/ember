#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <memory>
#include <vector>

//==============================================================================
/**
    8.336：ピッチ検出（YIN。設計書（ライト版）6.1）。

    **ホストにも画面にも依存しません**（設計書1章「DSPの各部品はDAWなしで単体テストできる純粋なクラス」）。
    使うのはJUCEのFFTだけ。`--shifter-selftest`がここを直に試します。

    ### 窓と探索範囲は「時間」で決める（仕様書5章）

    | | 48 kHz | 定義 |
    |---|---|---|
    | 窓 W | 1536 | 32 ms |
    | 周期の下限 τmin | 48 | 1000 Hz |
    | 周期の上限 τmax | 600 | 80 Hz |
    | 1回に見る長さ | W＋τmax＝2136 | ここが`getSegmentLength()` |

    ### 差分関数はFFTの相関から（設計書6.1-2）

    d(τ) = Σ(x_j − x_{j+τ})² = e(0) + e(τ) − 2 r(τ)。r(τ) は窓（W）と区間（W＋τmax）の相互相関を
    FFT で、e(τ) は二乗の累積和で出します。直接計算（W×τmax＝約92万回の積和）は CPU の目標を超えるため。

    **FFT は Signalsmith Linear の実数FFT**（8.336：はじめJUCEのFFTで、検出だけで CPU 3.2% かかった。
    WindowsのJUCEは汎用の実装になるため。変換エンジンが同じライブラリを使うので、依存は増えません）。

    ### 後処理（設計書6.1-5）

    直近5回の**中央値**。±1オクターブの跳びは、**3回続くまで採らない**（オクターブ誤りの対策）。
*/
class PitchDetector
{
public:
    PitchDetector();
    ~PitchDetector();

    struct Result
    {
        bool voiced = false;
        float frequency = 0.0f;   ///< Hz（後処理済み。無声のときは直前の値）
        float rawFrequency = 0.0f; ///< Hz（この回の生の値。無声なら0）
        float aperiodicity = 1.0f; ///< CMNDFの谷の値（0＝完全な周期、1＝雑音）
        float rmsDb = -200.0f;
    };

    void prepare (double sampleRate);
    void reset();

    /** 1回に渡す長さ（W＋τmax）。 */
    int getSegmentLength() const noexcept { return windowLength + maxLag; }
    int getWindowLength() const noexcept { return windowLength; }

    /** `segment`は`getSegmentLength()`サンプル。**音のスレッドから呼んでよい**（確保しない）。 */
    Result analyse (const float* segment);

    /** 後処理（中央値・オクターブ）をかけない、1回ぶんの検出（試験用）。 */
    Result analyseRaw (const float* segment);

    static constexpr float threshold = 0.15f;       ///< 最初にこれを下回る谷を採る
    static constexpr float voicedLimit = 0.25f;     ///< 谷がこれ未満で有声
    static constexpr float silenceDb = -50.0f;      ///< RMS がこれ未満なら無声
    static constexpr double minFrequency = 80.0, maxFrequency = 1000.0;

private:
    double rate = 48000.0;
    int windowLength = 1536, minLag = 48, maxLag = 600;
    int fftSize = 4096;

    struct Fft;
    std::unique_ptr<Fft> fft;
    std::vector<float> prefixEnergy, difference;

    // 後処理
    std::array<float, 5> recent {};   ///< 直近の有声の音高（MIDIノート番号）
    int recentCount = 0, recentWrite = 0;
    float acceptedNote = -1.0f;
    int octaveJumpRun = 0;
    float lastFrequency = 0.0f;
};
