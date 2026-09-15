#include "JavaRhinoBassProcessor.h"

#include "JavaRhinoBassEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const javaRhinoBassIdentifier = "manta:bass";
}

//==============================================================================

JavaRhinoBassProcessor::JavaRhinoBassProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   // **入力は持ちません**（音源なので。9.5）
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "JavaRhinoBass", JavaRhinoBassParams::createParameterLayout())
{
    styleParamValue = apvts.getRawParameterValue (JavaRhinoBassParams::style);
    brightnessParam = apvts.getRawParameterValue (JavaRhinoBassParams::brightness);
    sustainParam    = apvts.getRawParameterValue (JavaRhinoBassParams::sustain);
    pluckPosParam   = apvts.getRawParameterValue (JavaRhinoBassParams::pluckPos);
    hardnessParam   = apvts.getRawParameterValue (JavaRhinoBassParams::hardness);
    attackParam     = apvts.getRawParameterValue (JavaRhinoBassParams::attack);
    clankParam      = apvts.getRawParameterValue (JavaRhinoBassParams::clank);
    blendParam      = apvts.getRawParameterValue (JavaRhinoBassParams::blend);
    toneParam       = apvts.getRawParameterValue (JavaRhinoBassParams::tone);
    gainParam       = apvts.getRawParameterValue (JavaRhinoBassParams::gain);
    legatoParam     = apvts.getRawParameterValue (JavaRhinoBassParams::legato);

    styleParam = dynamic_cast<juce::AudioParameterChoice*> (
                     apvts.getParameter (JavaRhinoBassParams::style));

    synth.addSound (new JavaRhinoBassSound());

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new JavaRhinoBassVoice());

    synth.setNoteStealingEnabled (true);
}

JavaRhinoBassProcessor::~JavaRhinoBassProcessor()
{
    // **待ってから壊すこと**（メッセージが飛んでいる途中で消えると、
    // 壊れたものを触りにいきます。1.5と同じ決まり）
    cancelPendingUpdate();
}

void JavaRhinoBassProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと**（1.27）
    MantaPlugins::findDescription (javaRhinoBassIdentifier, description);
}

const juce::String JavaRhinoBassProcessor::getName() const
{
    return Branding::bassPluginName;
}

//==============================================================================

void JavaRhinoBassProcessor::handleAsyncUpdate()
{
    // キースイッチが置いていった番号を、**メッセージスレッドから**パラメータへ書く
    const int choice = pendingStyle.exchange (-1);

    if (choice < 0 || styleParam == nullptr)
        return;

    styleParam->beginChangeGesture();
    *styleParam = choice;
    styleParam->endChangeGesture();
}

//==============================================================================

void JavaRhinoBassProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない）
    currentFs = sampleRate > 0.0 ? sampleRate : 44100.0;

    synth.setCurrentPlaybackSampleRate (currentFs);

    masterGain.reset (currentFs, 0.02);
    masterGain.setCurrentAndTargetValue (gainParam->load());

    toneZ[0] = toneZ[1] = 0.0f;

    // **レガートの状態は持ち越さない**（止めて動かし直したとき、
    // 前の「押さえている連なり」が残っていると最初の音がレガートになります）
    chain = {};
    sampleClock = 0;
    lastNoteOnClock = -1000000;

    filteredMidi.ensureSize (2048);
}

bool JavaRhinoBassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return mainOutput == juce::AudioChannelSet::stereo()
        || mainOutput == juce::AudioChannelSet::mono();
}

//==============================================================================

JavaRhinoBassVoice* JavaRhinoBassProcessor::findVoicePlaying (int note)
{
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<JavaRhinoBassVoice*> (synth.getVoice (i)))
            if (voice->isVoiceActive() && voice->getCurrentNote() == note)
                return voice;

    return nullptr;
}

/*  キースイッチ・音域制限・レガートの状態機械。 */
void JavaRhinoBassProcessor::filterMidi (juce::MidiBuffer& midiMessages)
{
    filteredMidi.clear();

    const bool legatoEnabled = legatoParam->load() > 0.5f;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const int pos = metadata.samplePosition;

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();

            //--- キースイッチ -----------------------------------------------
            if (isKeySwitchNote (note))
            {
                // **ここでは置くだけ**（書くのはメッセージスレッド。クラスの説明）
                pendingStyle.store (note - ksFinger);
                triggerAsyncUpdate();
                continue;   // **鳴らさない**
            }

            //--- 演奏音域外 --------------------------------------------------
            if (note < lowestNote || note > highestNote)
                continue;

            //--- レガート判定 ------------------------------------------------
            const juce::int64 clock = sampleClock + pos;
            const bool timeOk = (clock - lastNoteOnClock) > (juce::int64) (currentFs * 0.060);

            if (legatoEnabled && chain.voiceNote >= 0 && timeOk)
            {
                const int diff = std::abs (note - chain.voiceNote);

                if (diff >= 1 && diff <= 7)
                {
                    if (auto* voice = findVoicePlaying (chain.voiceNote))
                    {
                        // 音程差が小さければハンマリング、大きければスライド寄りに
                        const double glide = (diff <= 2) ? 6.0
                                                          : juce::jmin (280.0, 25.0 * diff);

                        voice->legatoTo (note, glide, msg.getFloatVelocity());

                        chain.held.addIfNotAlreadyThere (note);
                        chain.voiceNote = note;
                        lastNoteOnClock = clock;
                        continue;   // **ノートオンを握りつぶす**
                    }
                }
            }

            chain.voiceNote = note;
            chain.held.clearQuick();
            chain.held.add (note);
            lastNoteOnClock = clock;

            filteredMidi.addEvent (msg, pos);
            continue;
        }

        if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();

            if (isKeySwitchNote (note))
                continue;

            if (note < lowestNote || note > highestNote)
                continue;

            chain.held.removeAllInstancesOf (note);

            if (note == chain.voiceNote)
            {
                if (! chain.held.isEmpty())
                {
                    // プリング：下の保持音へ戻す
                    const int back = chain.held.getLast();

                    if (auto* voice = findVoicePlaying (chain.voiceNote))
                    {
                        voice->legatoTo (back, 8.0, 0.45f);
                        chain.voiceNote = back;
                        continue;
                    }
                }
                else
                {
                    // 全部離した：**実際に鳴っているノート番号で**ノートオフを出す
                    filteredMidi.addEvent (
                        juce::MidiMessage::noteOff (msg.getChannel(), chain.voiceNote,
                                                     msg.getFloatVelocity()), pos);
                    chain.voiceNote = -1;
                    continue;
                }
            }

            filteredMidi.addEvent (msg, pos);
            continue;
        }

        filteredMidi.addEvent (msg, pos);
    }

    midiMessages.swapWith (filteredMidi);
}

//==============================================================================

void JavaRhinoBassProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // **音源なので、まず消すこと**
    buffer.clear();

    // 画面の鍵盤を先に合流させる（**`filterMidi()`より前**）
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);
    filterMidi (midiMessages);

    //--------------------------------------------------------------------------
    // 全ボイスへつまみを配る
    BassVoiceParams vp;
    vp.brightness = brightnessParam->load();
    vp.sustain    = sustainParam->load();
    vp.pluckPos   = pluckPosParam->load();
    vp.hardness   = hardnessParam->load();
    vp.attack     = attackParam->load();
    vp.clank      = clankParam->load();
    vp.blend      = blendParam->load();

    // **番号をそのまま`BassStyle`にしないこと**（`JavaRhinoBassParameters.h`）
    vp.style = (int) JavaRhinoBassParams::styleFromChoice ((int) styleParamValue->load());

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<JavaRhinoBassVoice*> (synth.getVoice (i)))
            voice->setParams (vp);

    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    //--------------------------------------------------------------------------
    // パッシブトーン（出口の1次LPF：800Hz〜12kHz）
    {
        const float t = toneParam->load() / 10.0f;
        const double fc = 800.0 * std::pow (15.0, (double) t);
        const float coef = (float) (1.0 - std::exp (-2.0 * bassdsp::kPi
                                                      * juce::jmin (fc, currentFs * 0.45) / currentFs));

        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        {
            auto* p = buffer.getWritePointer (ch);
            float z = toneZ[ch];

            for (int i = 0; i < numSamples; ++i)
            {
                z += coef * (p[i] - z);
                p[i] = z;
            }

            toneZ[ch] = z;
        }
    }

    //--------------------------------------------------------------------------
    // 出力ゲイン（ジッパーノイズ防止）
    masterGain.setTargetValue (gainParam->load());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* p = buffer.getWritePointer (ch);
        auto g = masterGain;

        for (int i = 0; i < numSamples; ++i)
            p[i] *= g.getNextValue();
    }

    masterGain.skip (numSamples);

    sampleClock += numSamples;

    //--------------------------------------------------------------------------
    // 画面の`n VOICES`（**書きっぱなし**。読むのは画面のスレッド）
    int active = 0;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (synth.getVoice (i)->isVoiceActive())
            ++active;

    activeVoiceCount.store (active);
}

//==============================================================================

juce::AudioProcessorEditor* JavaRhinoBassProcessor::createEditor()
{
    return new JavaRhinoBassEditor (*this);
}

void JavaRhinoBassProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void JavaRhinoBassProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
