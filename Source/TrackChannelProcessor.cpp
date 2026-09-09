#include "TrackChannelProcessor.h"

#include "ProjectModel.h" // AutomationTargets（正規化値→実値の変換）

TrackChannelProcessor::TrackChannelProcessor (Transport& transportToUse)
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (transportToUse)
{
}

TrackChannelProcessor::~TrackChannelProcessor() = default;

void TrackChannelProcessor::prepareToPlay (double, int)
{
    for (auto& level : levels)
        level.store (0.0f);
}

void TrackChannelProcessor::releaseResources()
{
    for (auto& level : levels)
        level.store (0.0f);
}

void TrackChannelProcessor::calculateGains (float volumeDb, float vcaOffsetDb, float pan,
                                             float& leftOut, float& rightOut)
{
    // 仕様書5.2.4：VCAのオフセットはdBで加算する（設計書1.3）。
    // **無音判定はオフセットを足す前に行う。** 足した後で判定すると、
    // フェーダーを一番下（-∞）まで下げたトラックがVCAで持ち上げられて鳴ってしまう。
    const float gain = volumeDb <= silenceThresholdDb
                          ? 0.0f
                          : juce::Decibels::decibelsToGain (volumeDb + vcaOffsetDb);

    // パンはイコールパワー（中央で-3dB）。左右に振ったときに音量が持ち上がって
    // 聞こえるのを避けるため、フェードと同じくsin/cosカーブを使う。
    const float limitedPan = juce::jlimit (-1.0f, 1.0f, pan);
    const float angle = (limitedPan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;

    leftOut = gain * std::cos (angle);
    rightOut = gain * std::sin (angle);
}

void TrackChannelProcessor::setMixSettings (float volumeDb, float pan, bool shouldBeAudible, float vcaOffsetDb)
{
    float left = 0.0f;
    float right = 0.0f;
    calculateGains (volumeDb, vcaOffsetDb, pan, left, right);

    gainLeft.store (left);
    gainRight.store (right);
    audible.store (shouldBeAudible);

    // オートメーションが無いパラメータの基準値として覚えておく
    currentVolumeDb.store (volumeDb);
    currentPan.store (pan);
    currentVcaOffsetDb.store (vcaOffsetDb);
}

void TrackChannelProcessor::setAutomation (std::vector<AutomationSample> volumePoints,
                                             std::vector<AutomationSample> panPoints)
{
    // 新しい配列は外で組み立て終わっているので、入れ替えの一瞬だけロックする
    const juce::SpinLock::ScopedLockType lock (automationLock);

    volumeAutomation = std::move (volumePoints);
    panAutomation = std::move (panPoints);
}

float TrackChannelProcessor::getAutomationValueAt (const std::vector<AutomationSample>& points,
                                                     juce::int64 positionSamples, float fallback)
{
    if (points.empty())
        return fallback;

    // 端の外側は、いちばん近い点の値のまま伸ばす（ProjectModel側の補間と同じ扱い）
    if (positionSamples <= points.front().positionSamples)
        return points.front().value;

    if (positionSamples >= points.back().positionSamples)
        return points.back().value;

    for (size_t i = 1; i < points.size(); ++i)
    {
        if (points[i].positionSamples < positionSamples)
            continue;

        const auto& previous = points[i - 1];
        const auto& next = points[i];
        const auto span = next.positionSamples - previous.positionSamples;

        if (span <= 0)
            return next.value;

        const float t = (float) (positionSamples - previous.positionSamples) / (float) span;

        // 区間の形は「手前の点」が持つ。モデル側（AutomationLane::getValueAt）と
        // 同じ関数を通すことで、画面に描いたカーブと実際の音が食い違わないようにする
        const float shaped = applyAutomationCurve ((AutomationCurve) previous.curve, t,
                                                    previous.curveAmount);

        return previous.value + (next.value - previous.value) * shaped;
    }

    return points.back().value;
}

float TrackChannelProcessor::getLevel (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, numMeterChannels))
        return 0.0f;

    return levels[channel].load();
}

void TrackChannelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    if (! audible.load())
    {
        buffer.clear();

        // ミュート中もメーターは減衰させる（振れたまま止まらないようにするため）
        for (auto& level : levels)
            level.store (level.load() * meterDecayPerBlock);

        return;
    }

    float startGains[numMeterChannels] = { gainLeft.load(), gainRight.load() };
    float endGains[numMeterChannels]   = { startGains[0], startGains[1] };

    // 仕様書5.6：再生中はオートメーションがフェーダーより優先される（Phase 19）。
    // ブロックの先頭と末尾で値を求め、その間はランプで繋ぐ。ブロック単位で
    // ゲインを切り替えると、動きの速いオートメーションでプチプチとノイズが出るため。
    if (transport.isPlaying() && ! automationBypassed.load())
    {
        const juce::SpinLock::ScopedTryLockType tryLock (automationLock);

        if (tryLock.isLocked() && ! (volumeAutomation.empty() && panAutomation.empty()))
        {
            const auto blockStart = transport.getPositionSamples();
            const auto blockEnd = blockStart + numSamples;

            const float volumeAtStart = getAutomationValueAt (volumeAutomation, blockStart, -1.0f);
            const float volumeAtEnd   = getAutomationValueAt (volumeAutomation, blockEnd, -1.0f);
            const float panAtStart    = getAutomationValueAt (panAutomation, blockStart, -1.0f);
            const float panAtEnd      = getAutomationValueAt (panAutomation, blockEnd, -1.0f);

            // オートメーションが無いパラメータ（fallbackの-1が返る）はフェーダーの値を使う
            const float faderVolumeDb = currentVolumeDb.load();
            const float faderPan = currentPan.load();

            // 仕様書5.2.4：VCAのオフセットは、フェーダー由来でもオートメーション由来でも
            // 同じように加算する（VCAは「今の音量に対する補正」であるため）
            const float vcaOffsetDb = currentVcaOffsetDb.load();

            auto volumeDbOf = [faderVolumeDb] (float normalised)
            {
                return normalised < 0.0f ? faderVolumeDb
                                          : AutomationTargets::toParameterValue (AutomationTargets::volume, normalised);
            };

            auto panOf = [faderPan] (float normalised)
            {
                return normalised < 0.0f ? faderPan
                                          : AutomationTargets::toParameterValue (AutomationTargets::pan, normalised);
            };

            calculateGains (volumeDbOf (volumeAtStart), vcaOffsetDb, panOf (panAtStart), startGains[0], startGains[1]);
            calculateGains (volumeDbOf (volumeAtEnd),   vcaOffsetDb, panOf (panAtEnd),   endGains[0],   endGains[1]);
        }
    }

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const int gainIndex = juce::jmin (ch, numMeterChannels - 1);
        buffer.applyGainRamp (ch, 0, numSamples, startGains[gainIndex], endGains[gainIndex]);
    }

    // メーターはフェーダー適用後に測る（＝そのトラックが実際に出している音量）
    const int numChannels = juce::jmin (buffer.getNumChannels(), numMeterChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, numSamples);
        const float decayed = levels[ch].load() * meterDecayPerBlock;
        levels[ch].store (juce::jmax (peak, decayed));
    }
}

const juce::String TrackChannelProcessor::getName() const   { return "Track Channel"; }
double TrackChannelProcessor::getTailLengthSeconds() const   { return 0.0; }
bool TrackChannelProcessor::acceptsMidi() const              { return false; }
bool TrackChannelProcessor::producesMidi() const             { return false; }

juce::AudioProcessorEditor* TrackChannelProcessor::createEditor() { return nullptr; }
bool TrackChannelProcessor::hasEditor() const                     { return false; }

int TrackChannelProcessor::getNumPrograms()                        { return 1; }
int TrackChannelProcessor::getCurrentProgram()                     { return 0; }
void TrackChannelProcessor::setCurrentProgram (int)                {}
const juce::String TrackChannelProcessor::getProgramName (int)     { return {}; }
void TrackChannelProcessor::changeProgramName (int, const juce::String&) {}

void TrackChannelProcessor::getStateInformation (juce::MemoryBlock&) {}
void TrackChannelProcessor::setStateInformation (const void*, int)   {}
