#pragma once

#include <cmath>

//==============================================================================
/**
    8.292：**Kakapo の内蔵リード**（Phase 285／本人の仕様書6章・設計書15章）。

    **音色は固定**です。ユーザーが触れるのは音量だけ（仕様書6章の最後）。
    「外部音源へMIDIを回さなくても、弾きながらスケールを確かめられる」ための音なので、
    **音作りを増やすほど目的から遠ざかります。**

    ```
    PolyBLEPのこぎり波 ─▶ 1次ローパス（3.2kHz固定）─▶ ADSR ─▶ 出力
    ```

    ### のこぎり波は**帯域制限**してあります

    仕様書15章は「v1はナイーブな波形から。気になればPolyBLEPを将来検討（難易度+1）」。
    **先に入れました**——PolyBLEPは1つの関数で10行ほどで、
    **高い音でのジャリつきが消えます**。あとから足す理由がありません。

    ### モノフォニック（last-note priority）

    新しい音が来たら、**前の音の減衰を待たずに音程を切り替えて鳴らし直します**
    （仕様書15章のとおり）。離したときは、**その音がいま鳴っている音のときだけ**
    リリースへ入ります——和音を押さえて1本離したときに、切れないように。

    ### 切り替えのクリック

    音程はサンプル単位で切り替わりますが、**包絡は必ず0から立ち上がります**
    （アタック5ms）。音量の変化は`SmoothedValue`側（`KakapoProcessor`）で滑らかにします。
*/
namespace kakapo
{
    class LeadVoice
    {
    public:
        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

            // 1次ローパス（3.2kHz固定。**音色を丸めるだけ**）
            const double cutoff = 3200.0;
            filterCoefficient = (float) std::exp (-2.0 * 3.14159265358979 * cutoff / sampleRate);

            attackRate  = rateFor (0.005);   // 5ms
            decayRate   = rateFor (0.050);   // 50ms
            releaseRate = rateFor (0.080);   // 80ms

            reset();
        }

        void reset()
        {
            phase = 0.0;
            increment = 0.0;
            filterState = 0.0f;
            envelope = 0.0f;
            stage = Stage::idle;
            currentNote = -1;
        }

        bool isActive() const noexcept { return stage != Stage::idle; }

        int getCurrentNote() const noexcept { return currentNote; }

        void noteOn (int midiNote, float velocity)
        {
            currentNote = midiNote;
            velocityGain = 0.25f + 0.75f * (velocity < 0.0f ? 0.0f : (velocity > 1.0f ? 1.0f : velocity));

            const double hz = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
            increment = hz / sampleRate;

            // **位相は戻しません。** 戻すと、速く弾いたときに毎回同じ「プツ」が乗ります
            stage = Stage::attack;
        }

        void noteOff (int midiNote)
        {
            // **鳴っている音のときだけ**（クラスの説明）
            if (midiNote == currentNote && stage != Stage::idle)
                stage = Stage::release;
        }

        void allNotesOff()
        {
            if (stage != Stage::idle)
                stage = Stage::release;
        }

        /** 区間を書き足す（**足し算**。呼ぶ側がバッファを用意します）。 */
        void renderAdd (float* left, float* right, int numSamples)
        {
            if (stage == Stage::idle)
                return;

            for (int i = 0; i < numSamples; ++i)
            {
                const float sample = nextSample();

                left[i] += sample;

                if (right != nullptr)
                    right[i] += sample;

                if (stage == Stage::idle)
                    break;
            }
        }

    private:
        enum class Stage { idle, attack, decay, sustain, release };

        float rateFor (double seconds) const
        {
            return (float) (1.0 / (seconds * sampleRate));
        }

        /** PolyBLEP（段差の前後だけ、多項式で角を落とす）。 */
        float polyBlep (double t) const
        {
            const double dt = increment;

            if (t < dt)
            {
                const double x = t / dt;
                return (float) (x + x - x * x - 1.0);
            }

            if (t > 1.0 - dt)
            {
                const double x = (t - 1.0) / dt;
                return (float) (x * x + x + x + 1.0);
            }

            return 0.0f;
        }

        float nextSample()
        {
            // ---- 包絡 ----
            switch (stage)
            {
                case Stage::attack:
                    envelope += attackRate;

                    if (envelope >= 1.0f)
                    {
                        envelope = 1.0f;
                        stage = Stage::decay;
                    }

                    break;

                case Stage::decay:
                    envelope -= decayRate * (1.0f - sustainLevel);

                    if (envelope <= sustainLevel)
                    {
                        envelope = sustainLevel;
                        stage = Stage::sustain;
                    }

                    break;

                case Stage::release:
                    envelope -= releaseRate;

                    if (envelope <= 0.0f)
                    {
                        envelope = 0.0f;
                        stage = Stage::idle;
                        currentNote = -1;
                        return 0.0f;
                    }

                    break;

                case Stage::sustain:
                case Stage::idle:
                default:
                    break;
            }

            // ---- のこぎり波（帯域制限） ----
            phase += increment;

            if (phase >= 1.0)
                phase -= 1.0;

            float saw = (float) (2.0 * phase - 1.0) - polyBlep (phase);

            // ---- 1次ローパス ----
            filterState = saw * (1.0f - filterCoefficient) + filterState * filterCoefficient;

            return filterState * envelope * velocityGain * 0.6f;
        }

        double sampleRate = 48000.0;
        double phase = 0.0;
        double increment = 0.0;

        float filterCoefficient = 0.0f;
        float filterState = 0.0f;

        float envelope = 0.0f;
        float attackRate = 0.0f, decayRate = 0.0f, releaseRate = 0.0f;
        float sustainLevel = 0.8f;
        float velocityGain = 1.0f;

        Stage stage = Stage::idle;
        int currentNote = -1;
    };
}
