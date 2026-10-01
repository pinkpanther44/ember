#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

//==============================================================================
/**
    8.336：目標音高の頭脳（設計書（ライト版）6.2）。**ホストにも画面にも依存しません。**

    - `ScaleQuantizer`：最寄りのスケール音へ（境界に±0.15半音のヒステリシス）
    - `MidiNoteStack`：後着優先の単音（最大16音）
    - `ModeController`：モードごとに「何半音動かすか」を出す（Retune Speed の一次遅れ、無声の保持と戻り）
*/
namespace ShifterControl
{
    //==========================================================================
    /** スケール上の音か（`key`＝0..11、`scale`＝0 Chromatic／1 Major／2 Minor）。 */
    inline bool isInScale (int note, int key, int scale) noexcept
    {
        static constexpr bool major[12] { true, false, true, false, true, true, false, true, false, true, false, true };
        static constexpr bool minor[12] { true, false, true, true, false, true, false, true, true, false, true, false };

        if (scale == 0)
            return true;

        const int degree = ((note - key) % 12 + 12) % 12;
        return scale == 1 ? major[degree] : minor[degree];
    }

    /** 最寄りのスケール音（半音単位の整数）。等距離なら**下**（決定論のため）。 */
    inline int nearestInScale (float note, int key, int scale) noexcept
    {
        const int centre = (int) std::lround (note);
        int best = centre;
        float bestDistance = 1.0e9f;

        for (int offset = -6; offset <= 6; ++offset)
        {
            const int candidate = centre + offset;

            if (! isInScale (candidate, key, scale))
                continue;

            const float distance = std::abs (note - (float) candidate);

            if (distance < bestDistance - 1.0e-6f)
            {
                best = candidate;
                bestDistance = distance;
            }
        }

        return best;
    }

    //==========================================================================
    /** 設計書6.2：「n をスケール上の最寄り音 s へスナップ（境界に±0.15半音のヒステリシス）」。

        いま s にいるとき、**隣の音との境目を 0.15 半音越えるまで**動かない。
        境目の手前で揺れる声（ビブラートの端）が、音と音のあいだを行ったり来たりしないように。 */
    class ScaleQuantizer
    {
    public:
        static constexpr float hysteresis = 0.15f;

        void reset() noexcept { current = noNote; }

        int process (float note, int key, int scale) noexcept
        {
            const int nearest = nearestInScale (note, key, scale);

            if (current == noNote || ! isInScale (current, key, scale))
            {
                current = nearest;
                return current;
            }

            if (nearest != current)
            {
                // 隣り合うスケール音どうしなら、境目＋0.15 を越えたときだけ移る。離れた音へは即座に
                const int step = nearest > current ? 1 : -1;
                int neighbour = current + step;

                while (! isInScale (neighbour, key, scale))
                    neighbour += step;

                if (neighbour == nearest)
                {
                    const float boundary = 0.5f * (float) (current + nearest);
                    const bool crossed = step > 0 ? note > boundary + hysteresis : note < boundary - hysteresis;

                    if (crossed)
                        current = nearest;
                }
                else
                {
                    current = nearest;
                }
            }

            return current;
        }

        int getCurrent() const noexcept { return current; }

    private:
        static constexpr int noNote = -1000;
        int current = noNote;
    };

    //==========================================================================
    /** 設計書6.2：MIDIモード「押されているノートのうち最後のもの（後着優先、最大16音のスタック）」。 */
    class MidiNoteStack
    {
    public:
        void reset() noexcept { count = 0; lastReleased = -1; }

        void noteOn (int note) noexcept
        {
            noteOff (note);   // 同じ音が2つ積まれないように

            if (count == (int) notes.size())
            {
                // 満杯なら一番古いものを捨てる
                for (int i = 1; i < count; ++i)
                    notes[(size_t) i - 1] = notes[(size_t) i];

                --count;
            }

            notes[(size_t) count++] = note;
        }

        void noteOff (int note) noexcept
        {
            for (int i = 0; i < count; ++i)
            {
                if (notes[(size_t) i] == note)
                {
                    for (int j = i + 1; j < count; ++j)
                        notes[(size_t) j - 1] = notes[(size_t) j];

                    --count;

                    if (count == 0)
                        lastReleased = note;

                    return;
                }
            }
        }

        void allNotesOff() noexcept
        {
            if (count > 0)
                lastReleased = notes[(size_t) count - 1];

            count = 0;
        }

        /** 鳴らすべき音（無ければ -1）。`hold`なら最後に離した音を返し続ける。 */
        int current (bool hold) const noexcept
        {
            if (count > 0)
                return notes[(size_t) count - 1];

            return hold ? lastReleased : -1;
        }

    private:
        std::array<int, 16> notes {};
        int count = 0;
        int lastReleased = -1;
    };

    //==========================================================================
    /** モードごとに「何半音動かすか」を出す（設計書6.2の表）。

        ```
        n = 69 + 12 log2(f0/440)（検出した音高）
        c = 目標 − n（補正量）  → Retune Speed の一次遅れで cs
        出力 = cs + Pitch（半音）、比率 r = 2^(出力/12)
        ```

        | モード | 目標 | 無声のあいだ |
        |---|---|---|
        | Transpose | 使わない（出力＝Pitch） | 同じ |
        | Quantize | 最寄りのスケール音 | 直前の出力を100 ms保ち、50 msで Pitch へ戻す |
        | Robot | C4（60） | 同じ |
        | MIDI | 押している音（無ければ r＝1） | 同じ |

        **Retune Speed は Quantize・Robot・MIDI の共通**（仕様書4章「ピッチ補正ロジック：Quantize/Robot/MIDIで共通化」）。
        比率は 0.25〜4（±24半音）に制限します（設計書の Robot の制限を全モードに）。 */
    class ModeController
    {
    public:
        struct Input
        {
            int mode = 0;               ///< ShifterParams::Mode
            int key = 0, scale = 0;
            float pitchSemitones = 0.0f;
            float retuneMs = 20.0f;
            bool voiced = false;
            float detectedNote = 60.0f; ///< MIDIノート番号（小数）
            int midiNote = -1;          ///< -1＝押していない（保持込み）
        };

        static constexpr float maxSemitones = 24.0f;
        static constexpr double holdSeconds = 0.100, returnSeconds = 0.050;

        void reset() noexcept
        {
            correction = 0.0f;
            output = 0.0f;
            unvoicedSeconds = 1.0e9;
            returnFrom = 0.0f;
            quantizer.reset();
            wasMode = -1;
        }

        /** `dt`＝前回からの秒数（サブブロック1つぶん）。戻り値は出力の半音。 */
        float process (const Input& in, double dt) noexcept
        {
            if (in.mode != wasMode)
            {
                quantizer.reset();
                wasMode = in.mode;
            }

            if (in.mode == 0)
            {
                // Transpose：検出を使わない
                correction = 0.0f;
                unvoicedSeconds = 1.0e9;
                return output = clampSemitones (in.pitchSemitones);
            }

            if (in.mode == 3 && in.midiNote < 0)
            {
                // MIDI で押していない（保持もしない）＝原音（r＝1。Pitch も掛けない）。
                // 急に戻すとクリックになるので、Retune Speed で戻す
                correction += (0.0f - correction) * coefficient (in.retuneMs, dt);
                unvoicedSeconds = 1.0e9;
                return output = clampSemitones (correction);
            }

            if (! in.voiced)
            {
                if (unvoicedSeconds > 1.0e8)
                    unvoicedSeconds = 0.0;   // 無声に入った

                unvoicedSeconds += dt;

                if (unvoicedSeconds <= holdSeconds)
                {
                    returnFrom = output;      // 直前の出力を保つ
                    return output;
                }

                const float t = (float) std::min (1.0, (unvoicedSeconds - holdSeconds) / returnSeconds);
                output = returnFrom + (in.pitchSemitones - returnFrom) * t;
                correction = output - in.pitchSemitones;   // 有声に戻ったとき、ここから続ける
                return output = clampSemitones (output);
            }

            unvoicedSeconds = 1.0e9;

            float target = in.detectedNote;

            if (in.mode == 1)
                target = (float) quantizer.process (in.detectedNote, in.key, in.scale);
            else if (in.mode == 2)
                target = 60.0f;
            else if (in.mode == 3)
                target = (float) in.midiNote;

            const float wanted = target - in.detectedNote;
            correction += (wanted - correction) * coefficient (in.retuneMs, dt);

            return output = clampSemitones (correction + in.pitchSemitones);
        }

        float getOutputSemitones() const noexcept { return output; }

    private:

        static float clampSemitones (float s) noexcept
        {
            return s < -maxSemitones ? -maxSemitones : (s > maxSemitones ? maxSemitones : s);
        }

        /** 一次遅れの係数（時定数 τ＝Retune Speed。0 なら即座）。 */
        static float coefficient (float retuneMs, double dt) noexcept
        {
            if (retuneMs <= 0.0f)
                return 1.0f;

            return (float) (1.0 - std::exp (-dt / (retuneMs * 0.001)));
        }

        ScaleQuantizer quantizer;
        float correction = 0.0f, output = 0.0f, returnFrom = 0.0f;
        double unvoicedSeconds = 1.0e9;
        int wasMode = -1;
    };
}
