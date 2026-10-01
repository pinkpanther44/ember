#pragma once

#include <juce_dsp/juce_dsp.h>

#include "PitchDetector.h"
#include "PitchEngine.h"
#include "ShifterControl.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

//==============================================================================
/**
    8.336：Manta Shifter／Gibbon Voice の処理本体（設計書（ライト版）2章の信号フロー・5章・6章）。

    **ホストにも画面にも依存しません**（使うのはJUCEのFFTとオーバーサンプラーだけ）。
    `--shifter-selftest`がこれを直に回します。

    ```
    入力 ─┬─ 64サンプルのFIFO ─┬─ L+R → 検出用リング → YIN（5 msごと）─┐
          │                     │                                         ├→ ModeController → r
          │                     │   MIDI → 時刻を D 遅らせる ──────────────┘        │
          │                     └─ 変換エンジン（Signalsmith。r・フォルマント）─ Drive ─┐
          └─ ドライ遅延（エンジンの往復ぶん）──────────────────────────────── Mix ─ Output ─ Bypass → 出力
    ```

    ### 64サンプル単位（設計書5章「サブブロック処理」）

    DAWのブロック長に**かかわらず**、内部は64サンプルずつ。入出力のFIFOで揃えるので、
    **レイテンシーに64が足されます**。これでブロック長が 1 でも 4096 でも**出力がビットで一致**します
    （`--shifter-selftest`が確かめます）。

    ### 時刻合わせ（設計書6.2）

    エンジンは`setRatio()`を「最新の入力から D（`inputLatency()`）前の位置」に効かせます。そこで

    - **検出窓の中心**を「いま − D」に置く（窓の後ろ半分が**先読み**になる。モニタリングに使わない前提の利点）
    - **MIDI**は届いた時刻に D を足して待たせる
    - **パラメータ**も同じだけ遅らせる（オートメーションが音とずれないように）。Mix・Drive・Output・Bypass は
      出力の側なので、**エンジンの往復ぶん**遅らせる

    ### レイテンシーは1つに決まる

    `prepare()`で決まり、**処理中は変わりません**（設計書5章）。48 kHz で
    D 1200＋エンジンの出力側 1200＋64 ＝ 2464 サンプル（51.3 ms）。上限 100 ms に収まる。
*/
class ShifterEngine
{
public:
    struct Parameters
    {
        float pitch = 0.0f, formant = 0.0f;   ///< 半音
        bool link = false;
        int mode = 0, key = 0, scale = 0;
        float retuneMs = 20.0f;
        bool driveOn = false;
        float drive = 0.0f;                   ///< %
        float mix = 100.0f;                   ///< %
        float outputDb = 0.0f;
        bool bypass = false;
        bool midiHold = false;
        int engine = 0;   ///< 8.337：`PitchEngineType`（0 Spectral／1 PSOLA）
    };

    struct MidiEvent
    {
        int sampleOffset = 0;   ///< ブロック先頭からの位置
        uint8_t status = 0, data1 = 0, data2 = 0;
    };

    /** 画面へ（検出のたびに1つ。SPSCで渡す）。 */
    struct DisplayFrame
    {
        float detectedHz = 0.0f;
        bool voiced = false;
        float inputNote = 0.0f;    ///< 検出した音高（MIDIノート番号）
        float shiftSemitones = 0.0f;
        float outputNote = 0.0f;   ///< 出ていく音高（有声のとき）
        int midiNote = -1;
    };

    static constexpr int subBlockSize = 64;

    ShifterEngine();
    ~ShifterEngine();

    /** 非RT。**全部をここで確保します**（`process()`では確保しない。仕様書5章）。 */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    int getLatencySamples() const noexcept { return latency; }
    int getEngineInputLatency() const noexcept { return engineInputLatency; }
    double getSampleRate() const noexcept { return rate; }

    /** RT。`events`はこのブロックのMIDI（位置の順）。 */
    void process (float* const* channels, int numChannels, int numSamples, const Parameters& p,
                  const MidiEvent* events, int numEvents);

    bool popDisplay (DisplayFrame& frame) noexcept;

    /** 試験用：最後に検出した結果。 */
    const PitchDetector::Result& getLastDetection() const noexcept { return lastDetection; }
    float getCurrentShiftSemitones() const noexcept { return currentShift; }

private:
    void processSubBlock();
    void runDetection();
    void pushDisplay();
    void applyMidi (const uint8_t status, const uint8_t data1);

    double rate = 48000.0;
    int channels = 2;
    int latency = 0, engineInputLatency = 0, engineRoundTrip = 0;

    // 8.337：エンジンは2つ。切り替えは「新しいほうを初期化 → 往復の遅れぶん慣らす → 15 ms で混ぜ替え」
    std::array<std::unique_ptr<IPitchEngine>, 2> engines;
    int activeEngine = 0, incomingEngine = -1, switchElapsed = 0;
    std::array<std::array<float, subBlockSize>, 2> incomingWet {};

public:
    /** 試験用：いま音を出しているエンジン（切り替えの途中なら -1）。 */
    int getActiveEngineForTesting() const noexcept { return incomingEngine >= 0 ? -1 : activeEngine; }

private:
    PitchDetector detector;
    ShifterControl::ModeController controller;
    ShifterControl::MidiNoteStack notes;

    // 64サンプルのFIFO
    std::array<std::array<float, subBlockSize>, 2> inFifo {}, outFifo {}, wet {}, driven {};
    int fifoPos = 0;
    int64_t totalIn = 0;   ///< これまでに受け取ったサンプル数（MIDIの時刻の物差し）

    // ドライ遅延（エンジンの往復ぶん）と検出用のモノラル
    std::array<std::vector<float>, 2> dryRing;
    std::vector<float> detectRing, segment;
    int dryMask = 0, detectMask = 0;
    int64_t written = 0;   ///< リングへ書いたサンプル数

    // 検出
    int detectHopSubBlocks = 4, detectCountdown = 0;
    PitchDetector::Result lastDetection;
    float smoothedF0 = 0.0f;
    bool displayPending = false;

    // MIDIの待ち行列（時刻は totalIn の物差し）
    struct PendingMidi { int64_t time; uint8_t status, data1, data2; };
    std::array<PendingMidi, 1024> pending {};
    int pendingRead = 0, pendingWrite = 0;

    // パラメータの控え（サブブロックごと。遅らせて読む）
    std::vector<Parameters> history;
    int historyMask = 0, historyWrite = 0;
    int pitchDelaySubBlocks = 0, outputDelaySubBlocks = 0;
    Parameters current;
    bool primed = false;

    // 平滑化（サブブロック単位の一次遅れ・直線）
    float pitchSmoothed = 0.0f, formantSmoothed = 0.0f;
    float mixSmoothed = 1.0f, gainSmoothed = 1.0f, driveSmoothed = 0.0f;
    float driveFade = 0.0f, bypassFade = 0.0f;
    float currentShift = 0.0f;
    int lastMode = -1, lastKey = -1, lastScale = -1;
    bool lastLink = false;
    int transitionSubBlocks = 0;

    // Drive
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<float, 2> dcX {}, dcY {};
    float dcCoefficient = 0.9987f;
    bool driveActive = false;

    // 画面へ
    struct Queue;
    std::unique_ptr<Queue> display;
};
