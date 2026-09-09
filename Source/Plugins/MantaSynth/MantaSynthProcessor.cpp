#include "MantaSynthProcessor.h"

#include "MantaSynthEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaSynthIdentifier = "manta:synth";
}

//==============================================================================

MantaSynthProcessor::MantaSynthProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   // **入力は持ちません**（音源なので）。
                                   // `PluginDescription::numInputChannels`も0です
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaSynth", MantaSynthParams::createParameterLayout())
{
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new MantaSynthVoice());

    synth.addSound (new MantaSynthSound());
}

void MantaSynthProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (mantaSynthIdentifier, description);
}

const juce::String MantaSynthProcessor::getName() const
{
    return Branding::synthPluginName;
}

//==============================================================================

void MantaSynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない。8.153の書き出し）
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    synth.setCurrentPlaybackSampleRate (rate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<MantaSynthVoice*> (synth.getVoice (i)))
            voice->prepare (rate);

    // 20msで滑らかにする。**つまみを回したときの段差を消すため**
    outputGain.reset (rate, 0.02);

    fx1.prepare (rate);
    fx2.prepare (rate);
    eq.prepare (rate);
}

bool MantaSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return mainOutput == juce::AudioChannelSet::stereo()
        || mainOutput == juce::AudioChannelSet::mono();
}

//==============================================================================

void MantaSynthProcessor::updateVoiceSettings()
{
    MantaSynthSettings settings;

    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    settings.osc1Wave   = (int) get (MantaSynthParams::osc1Wave);
    settings.osc1Voices = (int) get (MantaSynthParams::osc1Voices);
    settings.osc1Detune = get (MantaSynthParams::osc1Detune);
    settings.osc1Level  = get (MantaSynthParams::osc1Level);
    settings.osc1Spread = get (MantaSynthParams::osc1Spread);

    settings.osc2Wave   = (int) get (MantaSynthParams::osc2Wave);
    settings.osc2Voices = (int) get (MantaSynthParams::osc2Voices);
    settings.osc2Detune = get (MantaSynthParams::osc2Detune);
    settings.osc2Level  = get (MantaSynthParams::osc2Level);
    settings.osc2Spread = get (MantaSynthParams::osc2Spread);
    settings.osc2Octave = (int) get (MantaSynthParams::osc2Octave);

    settings.subWave = (int) get (MantaSynthParams::subWave);

    // 選択肢は「-1 Oct」「-2 Oct」の2つ。0→-1、1→-2
    settings.subOctave = -1 - (int) get (MantaSynthParams::subOctave);
    settings.subLevel = get (MantaSynthParams::subLevel);

    settings.noiseType  = (int) get (MantaSynthParams::noiseType);
    settings.noiseLevel = get (MantaSynthParams::noiseLevel);

    settings.cutoff     = get (MantaSynthParams::cutoff);
    settings.resonance  = get (MantaSynthParams::resonance);
    settings.filterType = (int) get (MantaSynthParams::filterType);

    settings.ampA = get (MantaSynthParams::ampA);
    settings.ampD = get (MantaSynthParams::ampD);
    settings.ampS = get (MantaSynthParams::ampS);
    settings.ampR = get (MantaSynthParams::ampR);
    settings.modA = get (MantaSynthParams::modA);
    settings.modD = get (MantaSynthParams::modD);
    settings.modS = get (MantaSynthParams::modS);
    settings.modR = get (MantaSynthParams::modR);

    settings.lfoRate   = get (MantaSynthParams::lfoRate);
    settings.lfoDepth  = get (MantaSynthParams::lfoDepth);
    settings.lfoDest   = (int) get (MantaSynthParams::lfoDest);
    settings.lfo2Rate  = get (MantaSynthParams::lfo2Rate);
    settings.lfo2Depth = get (MantaSynthParams::lfo2Depth);
    settings.lfo2Dest  = (int) get (MantaSynthParams::lfo2Dest);
    settings.lfo2On    = get (MantaSynthParams::lfo2On) > 0.5f;

    settings.modEnvToCutoff = get (MantaSynthParams::modEnvCut);
    settings.driveAmt  = get (MantaSynthParams::drive);
    settings.glideTime = get (MantaSynthParams::glide);

    settings.osc1On   = get (MantaSynthParams::osc1On)   > 0.5f;
    settings.osc2On   = get (MantaSynthParams::osc2On)   > 0.5f;
    settings.subOn    = get (MantaSynthParams::subOn)    > 0.5f;
    settings.noiseOn  = get (MantaSynthParams::noiseOn)  > 0.5f;
    settings.filterOn = get (MantaSynthParams::filterOn) > 0.5f;
    settings.driveOn  = get (MantaSynthParams::driveOn)  > 0.5f;
    settings.lfoOn    = get (MantaSynthParams::lfoOn)    > 0.5f;
    settings.modEnvOn = get (MantaSynthParams::modEnvOn) > 0.5f;

    for (int slot = 0; slot < MantaSynthSettings::numSlots; ++slot)
    {
        settings.matrixSource[slot] =
            (int) apvts.getRawParameterValue (MantaSynthParams::matrixSource (slot))->load();
        settings.matrixDestination[slot] =
            (int) apvts.getRawParameterValue (MantaSynthParams::matrixDestination (slot))->load();
        settings.matrixAmount[slot] =
            apvts.getRawParameterValue (MantaSynthParams::matrixAmount (slot))->load();
    }

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<MantaSynthVoice*> (synth.getVoice (i)))
            voice->setSettings (settings);
}

void MantaSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // **音源なので、まず消すこと。** グラフから渡ってくるバッファには
    // 前のブロックの中身が残っています
    buffer.clear();

    updateVoiceSettings();

    // 画面の鍵盤で押したぶんを合流させる
    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);

    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    //--------------------------------------------------------------------------
    // マスター段：FX1 → FX2 → EQ → 出力ゲイン
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    const bool fx1Enabled = get (MantaSynthParams::fx1On) > 0.5f;
    const bool fx2Enabled = get (MantaSynthParams::fx2On) > 0.5f;

    fx1.setType (fx1Enabled ? (MantaSynthDSP::FXType) (int) get (MantaSynthParams::fx1Type)
                             : MantaSynthDSP::FXType::Off);
    fx1.setParams (get (MantaSynthParams::fx1Rate), get (MantaSynthParams::fx1Depth),
                    get (MantaSynthParams::fx1Mix), get (MantaSynthParams::fx1Fb));

    fx2.setType (fx2Enabled ? (MantaSynthDSP::FXType) (int) get (MantaSynthParams::fx2Type)
                             : MantaSynthDSP::FXType::Off);
    fx2.setParams (get (MantaSynthParams::fx2Rate), get (MantaSynthParams::fx2Depth),
                    get (MantaSynthParams::fx2Mix), get (MantaSynthParams::fx2Fb));

    // **切ってあるときは0dBを渡す**（係数を作り直さないと、切った瞬間に前の形が残ります）
    const bool eqEnabled = get (MantaSynthParams::eqOn) > 0.5f;

    eq.update (eqEnabled ? get (MantaSynthParams::eqLow) : 0.0f,
                eqEnabled ? get (MantaSynthParams::eqMid) : 0.0f,
                get (MantaSynthParams::eqMidF),
                eqEnabled ? get (MantaSynthParams::eqHigh) : 0.0f);

    outputGain.setTargetValue (get (MantaSynthParams::gain));

    const int numSamples = buffer.getNumSamples();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i];
        float r = right != nullptr ? right[i] : l;

        fx1.process (l, r);
        fx2.process (l, r);
        eq.process (l, r);

        const float g = outputGain.getNextValue();
        l *= g; r *= g;

        left[i] = l;
        if (right != nullptr) right[i] = r;

        // オシロスコープ用（**書きっぱなし**。読むのは画面のスレッド）
        const int position = scopeWritePos.load (std::memory_order_relaxed);
        scopeBuffer[position] = right != nullptr ? (l + r) * 0.5f : l;
        scopeWritePos.store ((position + 1) % scopeSize, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // メーター用のピーク。**上がるのは即座、落ちるのはゆっくり**
    // （一瞬のピークを見逃さないため。本体のメーターと同じ考え）
    for (int channel = 0; channel < 2; ++channel)
    {
        const int source = juce::jmin (channel, buffer.getNumChannels() - 1);
        const float peak = buffer.getMagnitude (source, 0, numSamples);
        const float current = outputLevel[channel].load();

        outputLevel[channel].store (peak > current ? peak : current * 0.82f + peak * 0.18f);
    }
}

//==============================================================================

juce::AudioProcessorEditor* MantaSynthProcessor::createEditor()
{
    return new MantaSynthEditor (*this);
}

void MantaSynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaSynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
