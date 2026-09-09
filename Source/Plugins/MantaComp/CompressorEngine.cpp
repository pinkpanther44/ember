#include "CompressorEngine.h"

#include "MantaCompParameters.h"

#include <cmath>

namespace
{
    /** `ms`ミリ秒で1/eまで近づく係数。 */
    float timeConstant (float milliseconds, double sampleRate) noexcept
    {
        const double seconds = juce::jmax (0.00002, (double) milliseconds * 0.001);

        return (float) std::exp (-1.0 / (seconds * juce::jmax (1.0, sampleRate)));
    }

    constexpr float minimumLevel = 1.0e-9f;
}

//==============================================================================

float CompressorEngine::computeOutputDb (float inputDb, float thresholdDb,
                                          float ratio, float kneeDb) noexcept
{
    // 仕様書3-2の式そのまま。**ここを書き換えたらグラフも変わります**（同じ関数なので）
    const float over = inputDb - thresholdDb;
    const float safeRatio = juce::jmax (1.0f, ratio);

    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        // ニー区間：Threshold前後を2次曲線でつなぐ
        const float shifted = over + kneeDb * 0.5f;

        return inputDb + ((1.0f / safeRatio) - 1.0f) * shifted * shifted / (2.0f * kneeDb);
    }

    if (2.0f * over < -kneeDb)
        return inputDb;   // Threshold未満：無圧縮

    return thresholdDb + over / safeRatio;
}

//==============================================================================

void CompressorEngine::prepare (double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;

    lookAheadSamples = juce::jmax (1, juce::roundToInt (MantaCompParams::lookAheadMs * 0.001 * sampleRate));

    // **`prepare()`で確保する**（HANDOVER 9.5）。+1は書き位置と読み位置が
    // 重ならないようにするぶん
    for (auto& buffer : delayBuffer)
        buffer.assign ((size_t) (lookAheadSamples + 1), 0.0f);

    lastLowCutHz = -1.0f;
    lastHighCutHz = -1.0f;

    // 履歴も**ここで確保する**（HANDOVER 9.5）。
    // 1コマの長さはレートで決まるので、レートが変わったら取り直します
    history.assign ((size_t) historyCapacity, {});
    historyIntervalSamples = juce::jmax (1, juce::roundToInt (sampleRate / (double) historyRateHz));

    reset();
}

void CompressorEngine::readHistory (HistoryFrame* destination, int count) const noexcept
{
    if (destination == nullptr || count <= 0 || history.empty())
        return;

    const int limited = juce::jmin (count, historyCapacity);
    const int end = historyWritePosition.load (std::memory_order_acquire);

    int position = (end - limited + historyCapacity) % historyCapacity;

    for (int i = 0; i < limited; ++i)
    {
        destination[i] = history[(size_t) position];
        position = (position + 1) % historyCapacity;
    }
}

void CompressorEngine::reset()
{
    for (auto& buffer : delayBuffer)
        std::fill (buffer.begin(), buffer.end(), 0.0f);

    for (auto& channel : channels)
        channel.reset();

    delayWritePosition = 0;
    fastPeak = 0.0f;
    slowMeanSquare = 0.0f;
    averageReductionDb = 0.0f;

    std::fill (history.begin(), history.end(), HistoryFrame {});
    historyWritePosition.store (0, std::memory_order_release);
    historyCounter = 0;
    pendingInputPeak = 0.0f;
    pendingOutputPeak = 0.0f;
    pendingReductionDb = 0.0f;
}

void CompressorEngine::updateSidechainCoefficients (const Settings& settings)
{
    // **変わったときだけ作り直す。** 毎ブロック作り直すと、
    // つまみを触っていないときまでcos/sinを計算することになります
    if (settings.lowCutHz != lastLowCutHz)
    {
        lastLowCutHz = settings.lowCutHz;
        lowCutCoeffs = MantaBiquad::designHighPass (settings.lowCutHz, 0.707, sampleRate);
    }

    if (settings.highCutHz != lastHighCutHz)
    {
        lastHighCutHz = settings.highCutHz;
        highCutCoeffs = MantaBiquad::designLowPass (settings.highCutHz, 0.707, sampleRate);
    }
}

void CompressorEngine::updateAutoEnvelope (const Settings& settings,
                                            float& attackMsOut, float& releaseMsOut)
{
    attackMsOut = settings.attackMs;
    releaseMsOut = settings.releaseMs;

    // 仕様書3-3：**クレストファクター**（ピークとRMSの差）から決める。
    //
    // 尖った音（ドラム＝クレストが大きい）は**速く掴んでゆっくり放す**、
    // 詰まった音（ベース・ボーカル＝クレストが小さい）は
    // **ゆっくり掴んで速く放す**。単純ですが、当たっている場面のほうが多い決め方です
    if (settings.autoEnvelope)
    {
        const float rms = std::sqrt (juce::jmax (slowMeanSquare, minimumLevel));
        const float crestDb = juce::Decibels::gainToDecibels (juce::jmax (fastPeak, minimumLevel) / rms, 0.0f);
        const float amount = juce::jlimit (0.0f, 1.0f, (crestDb - 3.0f) / 15.0f);

        attackMsOut = juce::jmap (amount, 30.0f, 0.5f);
        releaseMsOut = juce::jmap (amount, 80.0f, 500.0f);
    }

    // 仕様書2-4：Adaptive。**掛かりっぱなしのときほどリリースを伸ばす**
    // ——短いリリースのまま強く掛け続けると、音量が上下に揺れます（ポンピング）
    if (settings.adaptiveRelease)
        releaseMsOut *= 1.0f + juce::jlimit (0.0f, 2.0f, averageReductionDb / 8.0f);

    publishedAttackMs.store (attackMsOut);
    publishedReleaseMs.store (releaseMsOut);
}

//==============================================================================

void CompressorEngine::process (juce::AudioBuffer<float>& buffer,
                                 const float* const* sidechain, int numSidechainChannels,
                                 const Settings& settings)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0 || delayBuffer[0].empty())
        return;

    updateSidechainCoefficients (settings);

    float attackMs = settings.attackMs;
    float releaseMs = settings.releaseMs;
    updateAutoEnvelope (settings, attackMs, releaseMs);

    const float attackCoefficient = timeConstant (attackMs, sampleRate);
    const float releaseCoefficient = timeConstant (releaseMs, sampleRate);

    // Auto/Adaptiveのための追従（速いピーク・遅いRMS・平均リダクション）
    const float peakAttack = timeConstant (1.0f, sampleRate);
    const float peakRelease = timeConstant (150.0f, sampleRate);
    const float meanSquareCoefficient = timeConstant (300.0f, sampleRate);
    const float averageCoefficient = timeConstant (400.0f, sampleRate);

    const float inputGain = juce::Decibels::decibelsToGain (settings.inputGainDb);
    const float makeupGain = juce::Decibels::decibelsToGain (settings.makeupDb);
    const float mix = juce::jlimit (0.0f, 1.0f, settings.mix);

    const bool useLookAhead = settings.lookAhead;
    const int delayLength = (int) delayBuffer[0].size();

    const bool hasSidechain = sidechain != nullptr && numSidechainChannels > 0;

    float* channelData[2] { buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr };

    float peakReduction = 0.0f;
    float inputPeak[2] { 0.0f, 0.0f };
    float outputPeak[2] { 0.0f, 0.0f };

    for (int n = 0; n < numSamples; ++n)
    {
        //----------------------------------------------------------------------
        // ① Input Gain（Dryにも掛かります——「コンプへ入る前の音量」なので）
        float input[2] { 0.0f, 0.0f };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            input[ch] = channelData[ch][n] * inputGain;
            inputPeak[ch] = juce::jmax (inputPeak[ch], std::abs (input[ch]));
        }

        if (numChannels == 1)
            input[1] = input[0];

        //----------------------------------------------------------------------
        // ② 検出用の信号を作る（外部サイドチェインがあればそちらを見る）
        float detect[2];

        for (int ch = 0; ch < 2; ++ch)
        {
            if (hasSidechain)
                detect[ch] = sidechain[juce::jmin (ch, numSidechainChannels - 1)][n];
            else
                detect[ch] = input[ch];
        }

        // 仕様書2-6：**検出だけに掛かるフィルタ**（出てくる音には掛かりません）
        if (settings.sidechainFilterOn)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                detect[ch] = channels[(size_t) ch].lowCut.process (detect[ch], lowCutCoeffs);
                detect[ch] = channels[(size_t) ch].highCut.process (detect[ch], highCutCoeffs);
            }
        }

        //----------------------------------------------------------------------
        // ③〜⑤ レベル → リダクション量 → Attack/Release追従
        //
        // Stereo LinkがONなら**左右の大きいほう**で1つの検出器を回します
        // （L/Rが同じだけ潰れる＝定位が動かない）
        const float linked = juce::jmax (std::abs (detect[0]), std::abs (detect[1]));

        // Auto/Adaptiveのための観測（リンクした値で見る）
        fastPeak = (linked > fastPeak ? peakAttack : peakRelease) * (fastPeak - linked) + linked;
        slowMeanSquare = meanSquareCoefficient * (slowMeanSquare - linked * linked) + linked * linked;

        float reduction[2] { 0.0f, 0.0f };

        for (int ch = 0; ch < 2; ++ch)
        {
            const float level = settings.stereoLink ? linked : std::abs (detect[ch]);
            const float levelDb = juce::Decibels::gainToDecibels (level + minimumLevel, -120.0f);

            const float targetDb = levelDb - computeOutputDb (levelDb, settings.thresholdDb,
                                                               settings.ratio, settings.kneeDb);

            auto& envelope = channels[(size_t) ch].envelopeDb;
            const float coefficient = targetDb > envelope ? attackCoefficient : releaseCoefficient;

            envelope = coefficient * (envelope - targetDb) + targetDb;
            reduction[ch] = juce::jmax (0.0f, envelope);

            // Stereo LinkがONなら、片方を計算して両方へ配る
            if (settings.stereoLink)
            {
                channels[1].envelopeDb = envelope;
                reduction[1] = reduction[0];
                break;
            }
        }

        const float loudestReduction = juce::jmax (reduction[0], reduction[1]);
        peakReduction = juce::jmax (peakReduction, loudestReduction);

        averageReductionDb = averageCoefficient * (averageReductionDb - loudestReduction) + loudestReduction;

        //----------------------------------------------------------------------
        // ⑥ Dryを先読みぶん遅らせる（**検出は遅らせていない信号を見ている**）
        float dry[2];

        const int readPosition = useLookAhead
                                   ? (delayWritePosition + delayLength - lookAheadSamples) % delayLength
                                   : delayWritePosition;

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& line = delayBuffer[(size_t) ch];

            dry[ch] = line[(size_t) readPosition];
            line[(size_t) delayWritePosition] = input[ch];
        }

        delayWritePosition = (delayWritePosition + 1) % delayLength;

        //----------------------------------------------------------------------
        // ⑦ リダクション＋Makeup（＋Auto Gain）
        //
        // 仕様書2-5：Auto Gainは**直近の平均リダクション**を足し戻します。
        // 「圧縮する前と後で、聴いた大きさをだいたい揃える」ためのものです
        const float autoGainDb = settings.autoGain ? averageReductionDb : 0.0f;
        const float outputGain = makeupGain * juce::Decibels::decibelsToGain (autoGainDb);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float value;

            if (settings.listen)
            {
                // 仕様書2-6：Listen。**検出に使っている音そのもの**を出します
                // （どの帯域で反応しているかを耳で確かめるため）
                value = detect[ch];
            }
            else
            {
                const float wet = dry[ch] * juce::Decibels::decibelsToGain (-reduction[ch]) * outputGain;

                // ⑧ Mix（パラレルコンプ）。**Dryは遅らせたほうを使う**ので、
                // Look AheadがONでも時間軸が揃います（仕様書3-6）
                value = dry[ch] * (1.0f - mix) + wet * mix;
            }

            channelData[ch][n] = value;
            outputPeak[ch] = juce::jmax (outputPeak[ch], std::abs (value));

            pendingOutputPeak = juce::jmax (pendingOutputPeak, std::abs (value));
        }

        //----------------------------------------------------------------------
        // ⑨ 履歴（画面の「時間の流れ」用。Phase 209）。
        // **その区間のいちばん大きかったところ**を1コマにします——
        // 平均で描くと、コンプが反応した理由（＝ピーク）が絵から消えます
        pendingInputPeak = juce::jmax (pendingInputPeak, std::abs (input[0]), std::abs (input[1]));
        pendingReductionDb = juce::jmax (pendingReductionDb, loudestReduction);

        if (++historyCounter >= historyIntervalSamples)
        {
            historyCounter = 0;

            const int position = historyWritePosition.load (std::memory_order_relaxed);

            history[(size_t) position] =
            {
                juce::Decibels::gainToDecibels (pendingInputPeak, -100.0f),
                juce::Decibels::gainToDecibels (pendingOutputPeak, -100.0f),
                pendingReductionDb
            };

            historyWritePosition.store ((position + 1) % historyCapacity, std::memory_order_release);

            pendingInputPeak = 0.0f;
            pendingOutputPeak = 0.0f;
            pendingReductionDb = 0.0f;
        }
    }

    //--------------------------------------------------------------------------
    latencySamples = useLookAhead ? lookAheadSamples : 0;

    publishedReductionDb.store (peakReduction);
    publishedAutoGainDb.store (settings.autoGain ? averageReductionDb : 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        const int source = juce::jmin (ch, numChannels - 1);

        inputLevelDb[(size_t) ch].store (juce::Decibels::gainToDecibels (inputPeak[source], -100.0f));
        outputLevelDb[(size_t) ch].store (juce::Decibels::gainToDecibels (outputPeak[source], -100.0f));
    }
}
