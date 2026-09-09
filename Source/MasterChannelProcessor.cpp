#include "MasterChannelProcessor.h"
#include "ProjectModel.h" // AutomationTargets（正規化値→dB変換）

MasterChannelProcessor::MasterChannelProcessor (Transport& transportToUse)
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      transport (transportToUse)
{
}

MasterChannelProcessor::~MasterChannelProcessor() = default;

void MasterChannelProcessor::prepareToPlay (double sampleRate, int)
{
    transport.prepare (sampleRate);

    for (auto& level : outputLevels)
        level.store (0.0f);
}

void MasterChannelProcessor::releaseResources()
{
    for (auto& level : outputLevels)
        level.store (0.0f);
}

void MasterChannelProcessor::setVolumeDb (float newVolumeDb)
{
    gain.store (newVolumeDb <= silenceThresholdDb ? 0.0f : juce::Decibels::decibelsToGain (newVolumeDb));
    currentVolumeDb.store (newVolumeDb);
}

void MasterChannelProcessor::setVolumeAutomation (std::vector<TrackChannelProcessor::AutomationSample> points)
{
    const juce::SpinLock::ScopedLockType lock (automationLock);
    volumeAutomation = std::move (points);
}

float MasterChannelProcessor::getOutputLevel (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, numMeterChannels))
        return 0.0f;

    return outputLevels[channel].load();
}

void MasterChannelProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numSamples = buffer.getNumSamples();

    float startGain = gain.load();
    float endGain = startGain;

    // 仕様書5.6：マスター音量のオートメーション（Phase 20）。
    // トラックと同じく、ブロックの先頭と末尾の値をランプで繋いでノイズを避ける
    if (transport.isPlaying() && ! automationBypassed.load())
    {
        const juce::SpinLock::ScopedTryLockType tryLock (automationLock);

        if (tryLock.isLocked() && ! volumeAutomation.empty())
        {
            const auto blockStart = transport.getPositionSamples();
            const auto blockEnd = blockStart + numSamples;

            auto gainFor = [] (float normalised)
            {
                const float db = AutomationTargets::toParameterValue (AutomationTargets::volume, normalised);
                return db <= silenceThresholdDb ? 0.0f : juce::Decibels::decibelsToGain (db);
            };

            startGain = gainFor (TrackChannelProcessor::getAutomationValueAt (volumeAutomation, blockStart, 0.0f));
            endGain   = gainFor (TrackChannelProcessor::getAutomationValueAt (volumeAutomation, blockEnd, 0.0f));
        }
    }

    buffer.applyGainRamp (0, numSamples, startGain, endGain);

    // レベルは音量を適用した後に測る。フェーダーを下げたらメーターも下がる
    // （＝実際にスピーカーへ出て行く量が見える）ようにするため。
    const int numChannels = juce::jmin (buffer.getNumChannels(), numMeterChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, numSamples);
        const float decayed = outputLevels[ch].load() * meterDecayPerBlock;
        outputLevels[ch].store (juce::jmax (peak, decayed));
    }

    // 再生位置を1ブロックぶん進める（設計書1.5、Transport.h）。
    // マスターはグラフの最下流にあり、必ず全音源より後に処理されるため、
    // 「各音源が今ブロックの位置を読む → 最後にここで進める」という順序が保証される。
    transport.advance (numSamples);
}

const juce::String MasterChannelProcessor::getName() const   { return "Master"; }
double MasterChannelProcessor::getTailLengthSeconds() const   { return 0.0; }
bool MasterChannelProcessor::acceptsMidi() const              { return false; }
bool MasterChannelProcessor::producesMidi() const             { return false; }

juce::AudioProcessorEditor* MasterChannelProcessor::createEditor() { return nullptr; }
bool MasterChannelProcessor::hasEditor() const                      { return false; }

int MasterChannelProcessor::getNumPrograms()                         { return 1; }
int MasterChannelProcessor::getCurrentProgram()                      { return 0; }
void MasterChannelProcessor::setCurrentProgram (int)                 {}
const juce::String MasterChannelProcessor::getProgramName (int)      { return {}; }
void MasterChannelProcessor::changeProgramName (int, const juce::String&) {}

void MasterChannelProcessor::getStateInformation (juce::MemoryBlock&) {}
void MasterChannelProcessor::setStateInformation (const void*, int)   {}
