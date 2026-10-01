#include "PitchEngine.h"

// Signalsmith Stretch（MIT。CMakeLists.txt の FetchContent で版を固定。8.336）
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <cmath>
#include <random>

namespace
{
    //==========================================================================
    /**
        8.336：`IPitchEngine`の Signalsmith Stretch 版（設計書（ライト版）6.3）。

        - 初期化：ブロック 50 ms・間隔 12.5 ms（`configure (ch, 0.05·fs, 0.0125·fs)`）
        - ピッチ：`setTransposeFactor (r)`
        - フォルマント：Link Off＝`setFormantFactor (2^(F/12), true)`（**ピッチを動かしても声の太さは保つ**）、
          Link On＝`compensatePitch = false`（**フォルマントがピッチに付いていく**＝早回しのような声）。
          ライブラリの中で確かめたこと：`compensatePitch`が真だと出力側の周波数へ写してから包絡を読み、
          偽だと入力の位置のまま読む（`updateFormants()`）
        - 基準 f0：`setFormantBase (f0 / fs)`（**単位は「サンプルレートに対する比」**。ライブラリの`freqToBand()`がそう読む）

        ### 決定論（仕様書5章）

        ライブラリの既定の作り方は`std::random_device`で種を取ります。**種を固定**して作ります
        （乱数を使うのは大きく引き伸ばすときだけで、ピッチだけなら使われませんが、念のため）。
        乱数器も処理系まかせの`default_random_engine`ではなく`std::mt19937`（8.335の教訓）。
    */
    class SignalsmithEngine final : public IPitchEngine
    {
    public:
        SignalsmithEngine() : stretch (1234567L) {}

        void prepare (double sampleRate, int numChannels, int) override
        {
            rate = sampleRate;
            channels = numChannels;
            stretch.configure (numChannels,
                               (int) std::lround (sampleRate * 0.05),
                               (int) std::lround (sampleRate * 0.0125));
            reset();
        }

        void reset() override
        {
            stretch.reset();
            stretch.setTransposeFactor (1.0f);
            stretch.setFormantFactor (1.0f, true);
            stretch.setFormantBase (0.0f);
        }

        int inputLatency() const override  { return stretch.inputLatency(); }
        int outputLatency() const override { return stretch.outputLatency(); }

        void setRatio (float ratio) override { stretch.setTransposeFactor (ratio); }

        void setFormant (float factor, bool followPitch) override
        {
            stretch.setFormantFactor (factor, ! followPitch);
        }

        void setFormantBase (float f0Hz) override
        {
            stretch.setFormantBase (f0Hz > 0.0f ? (float) (f0Hz / rate) : 0.0f);
        }

        void process (const float* const* in, float* const* out, int numSamples) override
        {
            stretch.process (in, numSamples, out, numSamples);
        }

    private:

        signalsmith::stretch::SignalsmithStretch<float, std::mt19937> stretch;
        double rate = 48000.0;
        int channels = 2;
    };
}

std::unique_ptr<IPitchEngine> createSignalsmithEngine()
{
    return std::make_unique<SignalsmithEngine>();
}
