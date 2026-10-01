#include "ShifterEngine.h"

#include <algorithm>
#include <cmath>

namespace
{
    int nextPowerOfTwo (int n)
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    float noteFromFrequency (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

    /** 一次遅れの係数（時定数 `seconds`、1歩 `step` 秒）。 */
    float onePole (double seconds, double step)
    {
        return seconds <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-step / seconds));
    }

    float approach (float value, float target, float step)
    {
        return value < target ? std::min (target, value + step) : std::max (target, value - step);
    }
}

//==============================================================================
/** 書き手1つ・読み手1つのロックフリーの待ち行列（満杯なら新しいほうを捨てる。表示が欠けても音は止めない）。 */
struct ShifterEngine::Queue
{
    static constexpr uint32_t capacity = 256;

    bool push (const DisplayFrame& frame) noexcept
    {
        const uint32_t w = writePosition.load (std::memory_order_relaxed);

        if (w - readPosition.load (std::memory_order_acquire) >= capacity)
            return false;

        items[w % capacity] = frame;
        writePosition.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (DisplayFrame& frame) noexcept
    {
        const uint32_t r = readPosition.load (std::memory_order_relaxed);

        if (r == writePosition.load (std::memory_order_acquire))
            return false;

        frame = items[r % capacity];
        readPosition.store (r + 1, std::memory_order_release);
        return true;
    }

    std::array<DisplayFrame, capacity> items {};
    std::atomic<uint32_t> writePosition { 0 }, readPosition { 0 };
};

//==============================================================================
ShifterEngine::ShifterEngine() : display (std::make_unique<Queue>()) {}
ShifterEngine::~ShifterEngine() = default;

void ShifterEngine::prepare (double sampleRate, int, int numChannels)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    channels = juce::jlimit (1, 2, numChannels);

    // 変換エンジンは2つ（8.337）。**PSOLA は Signalsmith 版と同じ遅れを名乗る**——切り替えてもレイテンシーが変わらない
    engines[0] = createPitchEngine (PitchEngineType::spectral);
    engines[0]->prepare (rate, channels, subBlockSize);

    engineInputLatency = engines[0]->inputLatency();
    engineRoundTrip = engineInputLatency + engines[0]->outputLatency();

    engines[1] = createPitchEngine (PitchEngineType::psola);
    engines[1]->matchLatency (engineInputLatency, engines[0]->outputLatency());
    engines[1]->prepare (rate, channels, subBlockSize);
    jassert (engines[1]->inputLatency() == engineInputLatency
             && engines[1]->inputLatency() + engines[1]->outputLatency() == engineRoundTrip);
    latency = engineRoundTrip + subBlockSize;   // ＋FIFOの64（設計書5章）

    detector.prepare (rate);
    segment.assign ((size_t) detector.getSegmentLength(), 0.0f);

    const int detectSize = nextPowerOfTwo (engineInputLatency + detector.getSegmentLength() + 2 * subBlockSize);
    detectRing.assign ((size_t) detectSize, 0.0f);
    detectMask = detectSize - 1;

    const int drySize = nextPowerOfTwo (engineRoundTrip + 2 * subBlockSize);

    for (auto& ring : dryRing)
        ring.assign ((size_t) drySize, 0.0f);

    dryMask = drySize - 1;

    // 検出は約 5.3 ms ごと（48 kHz で 256 サンプル＝4 サブブロック。設計書6.1-6）
    detectHopSubBlocks = juce::jmax (1, juce::roundToInt (rate * (256.0 / 48000.0) / subBlockSize));

    // パラメータを遅らせる量（サブブロック単位）
    pitchDelaySubBlocks = juce::roundToInt ((double) engineInputLatency / subBlockSize);
    outputDelaySubBlocks = juce::roundToInt ((double) engineRoundTrip / subBlockSize);

    const int historySize = nextPowerOfTwo (juce::jmax (pitchDelaySubBlocks, outputDelaySubBlocks) + 2);
    history.assign ((size_t) historySize, Parameters {});
    historyMask = historySize - 1;

    // Drive：IIRのハーフバンドで2倍（設計書6.4。**遅れは0として扱う**＝位相だけずれる）
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) channels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) subBlockSize);

    dcCoefficient = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / rate);   // 10 Hz の DCブロッカー

    reset();
}

void ShifterEngine::reset()
{
    for (auto& e : engines)
        if (e != nullptr)
            e->reset();

    incomingEngine = -1;
    switchElapsed = 0;

    detector.reset();
    controller.reset();
    notes.reset();

    for (auto* block : { &inFifo, &outFifo, &wet, &driven })
        for (auto& channel : *block)
            channel.fill (0.0f);

    fifoPos = 0;
    totalIn = 0;
    written = 0;

    std::fill (detectRing.begin(), detectRing.end(), 0.0f);

    for (auto& ring : dryRing)
        std::fill (ring.begin(), ring.end(), 0.0f);

    detectCountdown = 0;
    lastDetection = {};
    smoothedF0 = 0.0f;
    pendingRead = pendingWrite = 0;
    historyWrite = 0;
    primed = false;

    currentShift = 0.0f;
    lastMode = lastKey = lastScale = -1;
    lastLink = false;
    transitionSubBlocks = 0;

    if (oversampler != nullptr)
        oversampler->reset();

    dcX.fill (0.0f);
    dcY.fill (0.0f);
    driveActive = false;
}

bool ShifterEngine::popDisplay (DisplayFrame& frame) noexcept
{
    return display->pop (frame);
}

//==============================================================================
void ShifterEngine::process (float* const* io, int numChannels, int numSamples, const Parameters& p,
                             const MidiEvent* events, int numEvents)
{
    if (engines[0] == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const int ioChannels = juce::jmin (2, numChannels);
    current = p;

    if (! primed)
    {
        // 最初のブロック：控えを今の値で埋め、平滑化もそこから始める（既定値から滑り出さない）
        std::fill (history.begin(), history.end(), p);
        pitchSmoothed = p.pitch;
        formantSmoothed = p.formant;
        mixSmoothed = p.mix * 0.01f;
        gainSmoothed = juce::Decibels::decibelsToGain (p.outputDb);
        driveSmoothed = p.drive;
        driveFade = p.driveOn ? 1.0f : 0.0f;
        bypassFade = p.bypass ? 1.0f : 0.0f;
        activeEngine = juce::jlimit (0, 1, p.engine);   // 最初のブロックは混ぜ替えずに、選ばれているほうで始める
        primed = true;
    }

    const int64_t blockStart = totalIn;
    int eventIndex = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        // MIDIは「届いた時刻」で待ち行列へ（効かせるのは D 後。`processSubBlock()`）
        while (eventIndex < numEvents && events[eventIndex].sampleOffset <= i)
        {
            const auto& e = events[eventIndex++];

            if (pendingWrite - pendingRead < (int) pending.size())
                pending[(size_t) (pendingWrite++ % (int) pending.size())] = { blockStart + i, e.status, e.data1, e.data2 };
        }

        for (int c = 0; c < channels; ++c)
            inFifo[(size_t) c][(size_t) fifoPos] = io[juce::jmin (c, ioChannels - 1)][i];

        for (int c = 0; c < ioChannels; ++c)
            io[c][i] = outFifo[(size_t) juce::jmin (c, channels - 1)][(size_t) fifoPos];

        if (++fifoPos == subBlockSize)
        {
            processSubBlock();
            fifoPos = 0;
        }
    }

    // 位置がブロックの外を指していたもの（あり得ないが、捨てずに今の時刻で）
    while (eventIndex < numEvents)
    {
        const auto& e = events[eventIndex++];

        if (pendingWrite - pendingRead < (int) pending.size())
            pending[(size_t) (pendingWrite++ % (int) pending.size())] = { blockStart + numSamples, e.status, e.data1, e.data2 };
    }

    totalIn += numSamples;
}

void ShifterEngine::applyMidi (const uint8_t status, const uint8_t data1)
{
    const uint8_t type = status & 0xF0;

    if (type == 0x90 || type == 0x80)
    {
        // ベロシティ0のノートオンはノートオフ（呼び出し側で type を 0x80 にして渡す）
        if (type == 0x90)
            notes.noteOn (data1);
        else
            notes.noteOff (data1);
    }
    else if (type == 0xB0 && (data1 == 120 || data1 == 123))
    {
        notes.allNotesOff();
    }
}

void ShifterEngine::runDetection()
{
    // 窓の中心＝エンジンが効かせる位置（いま − D）。後ろ半分が先読み（設計書6.1-1）
    const int length = detector.getSegmentLength();
    const int64_t centre = written - engineInputLatency;
    const int64_t end = std::min<int64_t> (written, centre + length / 2);
    const int64_t start = end - length;

    if (start < 0)
    {
        lastDetection = {};
        return;
    }

    for (int k = 0; k < length; ++k)
        segment[(size_t) k] = detectRing[(size_t) ((start + k) & detectMask)];

    lastDetection = detector.analyse (segment.data());

    if (lastDetection.voiced && lastDetection.frequency > 0.0f)
    {
        const float a = onePole (0.030, detectHopSubBlocks * (double) subBlockSize / rate);
        smoothedF0 = smoothedF0 <= 0.0f ? lastDetection.frequency : smoothedF0 + (lastDetection.frequency - smoothedF0) * a;
    }

    // 画面へ渡すのは、この回の補正を計算したあと（`processSubBlock()`の7の後ろ）。
    // ここで渡すと1歩前の補正量で描かれ、出ていく音高の線が入力と同じだけ揺れて見えた（8.336）
    displayPending = true;
}

void ShifterEngine::pushDisplay()
{
    DisplayFrame frame;
    frame.voiced = lastDetection.voiced;
    frame.detectedHz = lastDetection.voiced ? lastDetection.frequency : 0.0f;
    frame.inputNote = frame.detectedHz > 0.0f ? noteFromFrequency (frame.detectedHz) : 0.0f;
    frame.shiftSemitones = currentShift;
    frame.outputNote = frame.inputNote + currentShift;
    frame.midiNote = notes.current (current.midiHold);
    display->push (frame);
    displayPending = false;
}

void ShifterEngine::processSubBlock()
{
    const int n = subBlockSize;
    const double step = n / rate;

    // 1. リングへ（検出はL+Rの平均。仕様書5章「ステレオはL+Rで検出し、同じシフトを両chに」）
    for (int j = 0; j < n; ++j)
    {
        const float mono = channels > 1 ? 0.5f * (inFifo[0][(size_t) j] + inFifo[1][(size_t) j]) : inFifo[0][(size_t) j];
        detectRing[(size_t) ((written + j) & detectMask)] = mono;

        for (int c = 0; c < channels; ++c)
            dryRing[(size_t) c][(size_t) ((written + j) & dryMask)] = inFifo[(size_t) c][(size_t) j];
    }

    written += n;

    // 2. パラメータの控え。ピッチ側は D、出力側はエンジンの往復ぶん遅らせて読む
    history[(size_t) (historyWrite & historyMask)] = current;
    const auto& pp = history[(size_t) ((historyWrite - pitchDelaySubBlocks) & historyMask)];
    const auto& po = history[(size_t) ((historyWrite - outputDelaySubBlocks) & historyMask)];
    ++historyWrite;

    // 3. MIDI：エンジンの時刻（いま − D）までに来たもの
    const int64_t engineTime = written - engineInputLatency;

    while (pendingRead < pendingWrite)
    {
        const auto& e = pending[(size_t) (pendingRead % (int) pending.size())];

        if (e.time >= engineTime)
            break;

        const bool noteOffByVelocity = (e.status & 0xF0) == 0x90 && e.data2 == 0;
        applyMidi (noteOffByVelocity ? (uint8_t) (0x80 | (e.status & 0x0F)) : e.status, e.data1);
        ++pendingRead;
    }

    // 4. 検出（約5 msごと）
    if (--detectCountdown <= 0)
    {
        runDetection();
        detectCountdown = detectHopSubBlocks;
    }

    // 5. 切り替え（Mode・Key・Scale・Link）は 15 ms かけて移る（設計書4章）
    if (pp.mode != lastMode || pp.key != lastKey || pp.scale != lastScale || pp.link != lastLink)
    {
        if (lastMode >= 0)
            transitionSubBlocks = juce::jmax (1, juce::roundToInt (0.015 * rate / n));

        lastMode = pp.mode;
        lastKey = pp.key;
        lastScale = pp.scale;
        lastLink = pp.link;
    }

    // 6. 連続値は約 20 ms で追う（設計書4章）
    const float smooth = onePole (0.020, step);
    pitchSmoothed += (pp.pitch - pitchSmoothed) * smooth;
    formantSmoothed += (pp.formant - formantSmoothed) * smooth;

    // 7. 目標の比率
    ShifterControl::ModeController::Input in;
    in.mode = pp.mode;
    in.key = pp.key;
    in.scale = pp.scale;
    in.pitchSemitones = pitchSmoothed;
    in.retuneMs = pp.retuneMs;
    in.voiced = lastDetection.voiced;
    in.detectedNote = lastDetection.frequency > 0.0f ? noteFromFrequency (lastDetection.frequency) : 60.0f;
    in.midiNote = notes.current (pp.midiHold);

    const float target = controller.process (in, step);

    if (transitionSubBlocks > 0)
    {
        currentShift += (target - currentShift) * onePole (0.005, step);
        --transitionSubBlocks;
    }
    else
    {
        currentShift = target;
    }

    if (displayPending)
        pushDisplay();

    // 8. 変換。エンジンの切り替え（8.337）：新しいほうを初期化して、往復の遅れぶん慣らしてから 15 ms で混ぜ替える
    //    （初期化したエンジンの最初の往復ぶんは、切り替える前の入力を知らないので使えない）
    const int wantedEngine = juce::jlimit (0, 1, pp.engine);

    if (incomingEngine < 0 && wantedEngine != activeEngine)
    {
        incomingEngine = wantedEngine;
        engines[(size_t) incomingEngine]->reset();
        switchElapsed = 0;
    }
    else if (incomingEngine >= 0 && wantedEngine == activeEngine)
    {
        incomingEngine = -1;   // 混ぜ替えの前に戻された：今のまま
    }

    const float* ins[2] { inFifo[0].data(), inFifo[(size_t) (channels > 1 ? 1 : 0)].data() };

    auto run = [&] (IPitchEngine& e, float* const* outs)
    {
        e.setRatio (std::exp2 (currentShift / 12.0f));
        // 8.341：**Link は音には効かせない**（つまみの連動になった。画面が Formant も同じだけ動かすので、ここでも
        // 付いていかせると太さが二重にずれる）。エンジンの`followPitch`は使わずに残してある
        e.setFormant (std::exp2 (formantSmoothed / 12.0f), false);
        e.setFormantBase (lastDetection.voiced && smoothedF0 > 0.0f ? smoothedF0 : 200.0f);
        e.setPitchInfo (lastDetection.voiced, lastDetection.frequency);
        e.process (ins, outs, n);
    };

    float* outs[2] { wet[0].data(), wet[1].data() };
    run (*engines[(size_t) activeEngine], outs);

    if (incomingEngine >= 0)
    {
        float* incomingOuts[2] { incomingWet[0].data(), incomingWet[1].data() };
        run (*engines[(size_t) incomingEngine], incomingOuts);

        const int fadeLength = juce::jmax (1, juce::roundToInt (0.015 * rate));

        for (int j = 0; j < n; ++j)
        {
            const int sinceWarm = switchElapsed + j + 1 - engineRoundTrip;
            const float t = juce::jlimit (0.0f, 1.0f, (float) sinceWarm / (float) fadeLength);

            // **等パワー**で混ぜる（2つのエンジンは波の位相が揃わないので、直線で混ぜると真ん中で打ち消し合う）
            const float oldGain = std::cos (0.5f * juce::MathConstants<float>::pi * t);
            const float newGain = std::sin (0.5f * juce::MathConstants<float>::pi * t);

            for (int c = 0; c < channels; ++c)
                wet[(size_t) c][(size_t) j] = oldGain * wet[(size_t) c][(size_t) j] + newGain * incomingWet[(size_t) c][(size_t) j];
        }

        switchElapsed += n;

        if (switchElapsed >= engineRoundTrip + fadeLength)
        {
            activeEngine = incomingEngine;
            incomingEngine = -1;
        }
    }

    // 9. Drive（tanh。2倍オーバーサンプリング＋DCブロッカー。On/Off は 15 ms で混ぜ替える）
    const float fadeStep = (float) (n / (0.015 * rate));
    const float fadeStart = driveFade;
    driveFade = approach (driveFade, po.driveOn ? 1.0f : 0.0f, fadeStep);
    driveSmoothed += (po.drive - driveSmoothed) * smooth;

    if (fadeStart > 0.0f || driveFade > 0.0f)
    {
        if (! driveActive)
        {
            oversampler->reset();
            dcX.fill (0.0f);
            dcY.fill (0.0f);
            driveActive = true;
        }

        for (int c = 0; c < channels; ++c)
            std::copy (wet[(size_t) c].begin(), wet[(size_t) c].end(), driven[(size_t) c].begin());

        float* drivenPointers[2] { driven[0].data(), driven[1].data() };
        juce::dsp::AudioBlock<float> block (drivenPointers, (size_t) channels, (size_t) n);
        auto up = oversampler->processSamplesUp (block);

        const float g = 1.0f + 9.0f * juce::jlimit (0.0f, 100.0f, driveSmoothed) * 0.01f;   // 設計書：g = 1〜10
        const float normalise = 1.0f / std::tanh (g);

        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* data = up.getChannelPointer (c);

            for (size_t k = 0; k < up.getNumSamples(); ++k)
                data[k] = std::tanh (g * data[k]) * normalise;
        }

        oversampler->processSamplesDown (block);

        for (int c = 0; c < channels; ++c)
        {
            for (int j = 0; j < n; ++j)
            {
                // DCブロッカー（tanh は非対称な入力で直流を作る）
                const float x = driven[(size_t) c][(size_t) j];
                const float y = x - dcX[(size_t) c] + dcCoefficient * dcY[(size_t) c];
                dcX[(size_t) c] = x;
                dcY[(size_t) c] = y;

                const float t = fadeStart + (driveFade - fadeStart) * (float) (j + 1) / (float) n;
                wet[(size_t) c][(size_t) j] += (y - wet[(size_t) c][(size_t) j]) * t;
            }
        }
    }
    else
    {
        driveActive = false;
    }

    // 10. Mix（ドライはエンジンの往復ぶん遅らせて位相を揃える。線形で混ぜる）・Output・Bypass
    const float mixStart = mixSmoothed, gainStart = gainSmoothed, bypassStart = bypassFade;
    mixSmoothed += (po.mix * 0.01f - mixSmoothed) * smooth;
    gainSmoothed += (juce::Decibels::decibelsToGain (po.outputDb) - gainSmoothed) * smooth;
    bypassFade = approach (bypassFade, po.bypass ? 1.0f : 0.0f, fadeStep);

    const int64_t subStart = written - n;

    for (int j = 0; j < n; ++j)
    {
        const float t = (float) (j + 1) / (float) n;
        const float m = mixStart + (mixSmoothed - mixStart) * t;
        const float g = gainStart + (gainSmoothed - gainStart) * t;
        const float b = bypassStart + (bypassFade - bypassStart) * t;
        const int64_t dryIndex = subStart + j - engineRoundTrip;

        for (int c = 0; c < channels; ++c)
        {
            const float dry = dryIndex >= 0 ? dryRing[(size_t) c][(size_t) (dryIndex & dryMask)] : 0.0f;
            const float processed = g * ((1.0f - m) * dry + m * wet[(size_t) c][(size_t) j]);
            outFifo[(size_t) c][(size_t) j] = b * dry + (1.0f - b) * processed;
        }
    }
}
