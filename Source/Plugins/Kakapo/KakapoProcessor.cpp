#include "KakapoProcessor.h"

#include "KakapoEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const kakapoIdentifier = "manta:kakapo";
}

//==============================================================================

KakapoProcessor::KakapoProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   // **入力は持ちません**（音源なので。9.5）
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Kakapo", KakapoParams::createParameterLayout())
{
    holdModeParam   = apvts.getRawParameterValue (KakapoParams::holdMode);
    holdLengthParam = apvts.getRawParameterValue (KakapoParams::holdLength);
    volumeParam     = apvts.getRawParameterValue (KakapoParams::volume);
    midiThruParam   = apvts.getRawParameterValue (KakapoParams::midiThru);
}

void KakapoProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと**（1.27）
    MantaPlugins::findDescription (kakapoIdentifier, description);
}

const juce::String KakapoProcessor::getName() const
{
    return Branding::scalePluginName;
}

//==============================================================================

void KakapoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない）
    currentFs = sampleRate > 0.0 ? sampleRate : 44100.0;

    history.prepare (currentFs);
    voice.prepare (currentFs);

    outputGain.reset (currentFs, 0.02);
    outputGain.setCurrentAndTargetValue (volumeParam != nullptr ? volumeParam->load() : 0.7f);

    monoRight.setSize (1, juce::jmax (64, samplesPerBlock), false, true, true);
    monoRight.clear();

    passThrough.ensureSize (2048);

    // **持ち越さないこと**（止めて動かし直したとき、前の判定が残ります）
    sampleClock = 0;
    resetRequested.store (false);

    publishSnapshot();
}

bool KakapoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return mainOutput == juce::AudioChannelSet::stereo()
        || mainOutput == juce::AudioChannelSet::mono();
}

//==============================================================================

void KakapoProcessor::handleMidiMessage (const juce::MidiMessage& message, int samplePosition)
{
    const auto sample = sampleClock + samplePosition;

    if (message.isNoteOn())
    {
        history.noteOn (message.getNoteNumber(), sample);
        voice.noteOn (message.getNoteNumber(), message.getFloatVelocity());   // last-note priority
    }
    else if (message.isNoteOff())
    {
        history.noteOff (message.getNoteNumber(), sample);
        voice.noteOff (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        voice.allNotesOff();
    }
}

void KakapoProcessor::renderSegment (float* left, float* right, int offset, int numSamples)
{
    if (numSamples <= 0)
        return;

    voice.renderAdd (left + offset, right != nullptr ? right + offset : nullptr, numSamples);
}

//==============================================================================

void KakapoProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    buffer.clear();

    if (numSamples <= 0 || numChannels <= 0)
    {
        midiMessages.clear();
        return;
    }

    if (monoRight.getNumSamples() < numSamples)
        monoRight.setSize (1, numSamples, false, true, true);

    monoRight.clear();

    // **消すのはブロックの頭で**（クラスの説明）
    if (resetRequested.exchange (false))
    {
        history.clear();
        voice.allNotesOff();
    }

    // 画面の鍵盤から来たノートを合流させる
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    outputGain.setTargetValue (volumeParam->load());

    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : monoRight.getWritePointer (0);

    //--------------------------------------------------------------------------
    // MIDIの位置でブロックを割って、サンプル精度で鳴らす（設計書15章・17章）
    const bool thru = midiThruParam->load() > 0.5f;

    passThrough.clear();

    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int time = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (time > position)
        {
            renderSegment (left, right, position, time - position);
            position = time;
        }

        handleMidiMessage (metadata.getMessage(), time);

        if (thru)
            passThrough.addEvent (metadata.getMessage(), time);
    }

    if (position < numSamples)
        renderSegment (left, right, position, numSamples - position);

    //--------------------------------------------------------------------------
    // 出力
    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = outputGain.getNextValue();

        left[i] *= gain;

        if (numChannels > 1)
            right[i] *= gain;
    }

    if (numChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);

    for (int channel = 2; channel < numChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    //--------------------------------------------------------------------------
    // 判定（**古い音を落としてから**）
    sampleClock += numSamples;

    const auto mode = holdModeParam->load() > 0.5f ? kakapo::NoteHistory::Mode::seconds
                                                    : kakapo::NoteHistory::Mode::notes;

    history.prune (mode, (double) holdLengthParam->load(), sampleClock);

    publishSnapshot();

    //--------------------------------------------------------------------------
    // **入ってきた列は、必ず作り直すこと。** そのまま残すと、
    // ホストによっては同じ音符をもう一度受け取ります。
    // スルーが切ならここは空なので、**出口には何も出ません**
    midiMessages.swapWith (passThrough);
}

//==============================================================================

void KakapoProcessor::publishSnapshot()
{
    AnalysisSnapshot fresh;

    const auto& histogram = history.getHistogram (sampleClock);

    fresh.histogram = histogram;
    fresh.result = kakapo::evaluate (histogram);
    fresh.noteCount = history.getNoteCount();

    history.fillNoteMasks (fresh.active, fresh.recent);

    const juce::ScopedLock lock (snapshotLock);
    snapshot = fresh;
}

KakapoProcessor::AnalysisSnapshot KakapoProcessor::readSnapshot() const
{
    const juce::ScopedLock lock (snapshotLock);
    return snapshot;
}

//==============================================================================

juce::AudioProcessorEditor* KakapoProcessor::createEditor()
{
    return new KakapoEditor (*this);
}

void KakapoProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KakapoProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
