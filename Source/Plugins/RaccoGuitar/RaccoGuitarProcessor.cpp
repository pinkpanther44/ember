#include "RaccoGuitarProcessor.h"

#include "RaccoGuitarEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const raccoGuitarIdentifier = "manta:guitar";
}

const juce::Identifier& RaccoGuitarProcessor::articulationProperty()
{
    static const juce::Identifier id { "articulation" };

    return id;
}

//==============================================================================

RaccoGuitarProcessor::RaccoGuitarProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   // **入力は持ちません**（音源なので。9.5）
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RaccoGuitar", RaccoGuitarParams::createParameterLayout())
{
    brightnessParam = apvts.getRawParameterValue (RaccoGuitarParams::brightness);
    sustainParam    = apvts.getRawParameterValue (RaccoGuitarParams::sustain);
    pickPosParam    = apvts.getRawParameterValue (RaccoGuitarParams::pickPos);
    pickupSelParam  = apvts.getRawParameterValue (RaccoGuitarParams::pickupSel);
    hardnessParam   = apvts.getRawParameterValue (RaccoGuitarParams::hardness);
    attackParam     = apvts.getRawParameterValue (RaccoGuitarParams::attack);
    gainParam       = apvts.getRawParameterValue (RaccoGuitarParams::gain);

    synth.addSound (new RaccoGuitarSound());

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new RaccoGuitarVoice());
}

void RaccoGuitarProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (raccoGuitarIdentifier, description);
}

const juce::String RaccoGuitarProcessor::getName() const
{
    return Branding::guitarPluginName;
}

int RaccoGuitarProcessor::keySwitchNoteFor (GuitarArticulation articulation)
{
    switch (articulation)
    {
        case GuitarArticulation::brushing:      return ksBrushing;
        case GuitarArticulation::pinchHarmonic: return ksPinchHarm;
        case GuitarArticulation::normal:        return ksNormal;
        case GuitarArticulation::slide:         return ksSlide;
        case GuitarArticulation::palmMute:      return ksPalmMute;
        case GuitarArticulation::harmonic:      return ksHarmonic;
    }

    return ksNormal;
}

void RaccoGuitarProcessor::setArticulationFromUI (GuitarArticulation articulation)
{
    currentArticulation.store ((int) articulation);
}

//==============================================================================

void RaccoGuitarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない）
    const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    synth.setCurrentPlaybackSampleRate (rate);

    gainSmoothed.reset (rate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (gainParam->load());

    // **レガートの状態は持ち越さない。** 止めて動かし直したときに、
    // 前の「押さえている連なり」が残っていると最初の音がレガートになります
    legato.reset();
    sampleClock = 0;
    lastNoteOnClock = -1000000;

    filteredMidi.ensureSize (2048);
}

bool RaccoGuitarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    return mainOutput == juce::AudioChannelSet::stereo()
        || mainOutput == juce::AudioChannelSet::mono();
}

//==============================================================================

RaccoGuitarVoice* RaccoGuitarProcessor::findActiveVoice (int note)
{
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<RaccoGuitarVoice*> (synth.getVoice (i)))
            if (voice->isVoiceActive()
                && ! voice->isReleasing()
                && voice->getCurrentlyPlayingNote() == note)
                return voice;

    return nullptr;
}

float RaccoGuitarProcessor::glideTimeFor (int interval) const
{
    // スライド奏法だけ、音程差に応じて**時間をかけて**動く
    if ((GuitarArticulation) currentArticulation.load() == GuitarArticulation::slide)
        return juce::jmin (slideGlideMaxMs,
                            slideMsPerSemitone * (float) juce::jmax (1, interval));

    return hammerGlideMs;
}

//==============================================================================
/*  キースイッチ・音域制限・レガートの処理。

    **ここを通った後のMIDIだけが`Synthesiser`へ渡ります**。
    音域外のノートは捨て、レガートで繋ぐぶんはノートオンごと消して
    `legatoTo()`を直に呼びます（新しい声を立てないため）。
*/
void RaccoGuitarProcessor::filterMidi (juce::MidiBuffer& midiMessages)
{
    filteredMidi.clear();

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        const auto pos = metadata.samplePosition;
        const juce::int64 now = sampleClock + pos;

        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();

            //--- 音域外（キースイッチを含む） -------------------------------
            if (note < lowestNote || note > highestNote)
            {
                if (isKeySwitchNote (note))
                {
                    switch (note)
                    {
                        case ksBrushing:  currentArticulation.store ((int) GuitarArticulation::brushing);      break;
                        case ksPinchHarm: currentArticulation.store ((int) GuitarArticulation::pinchHarmonic); break;
                        case ksNormal:    currentArticulation.store ((int) GuitarArticulation::normal);        break;
                        case ksSlide:     currentArticulation.store ((int) GuitarArticulation::slide);         break;
                        case ksPalmMute:  currentArticulation.store ((int) GuitarArticulation::palmMute);      break;
                        case ksHarmonic:  currentArticulation.store ((int) GuitarArticulation::harmonic);      break;
                        default: break;
                    }
                }

                continue;   // **キースイッチは鳴らさない**
            }

            //--- レガートの判定 ----------------------------------------------
            const double guardSamples = legatoGuardMs * 0.001 * getSampleRate();

            if (legato.active && ! legato.held.empty())
            {
                const int  topNote  = legato.held.back();
                const int  interval = std::abs (note - topNote);

                // **和音と区別するための間**（同時に押した2音はレガートにしない）
                const bool inTime   = (double) (now - lastNoteOnClock) > guardSamples;

                if (interval >= 1 && interval <= maxLegatoInterval && inTime)
                {
                    if (auto* voice = findActiveVoice (legato.voiceNote))
                    {
                        voice->legatoTo (note, msg.getFloatVelocity(), glideTimeFor (interval));
                        legato.held.push_back (note);
                        lastNoteOnClock = now;
                        continue;
                    }
                }
            }

            //--- 普通のノートオン：新しい連なりを始める ----------------------
            if (legato.active && legato.held.size() > 1)
                filteredMidi.addEvent (
                    juce::MidiMessage::noteOff (msg.getChannel(), legato.voiceNote), pos);

            legato.active    = true;
            legato.voiceNote = note;
            legato.held      = { note };
            lastNoteOnClock  = now;

            filteredMidi.addEvent (msg, pos);
            continue;
        }

        if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();

            if (note < lowestNote || note > highestNote)
                continue;

            if (legato.active && legato.contains (note))
            {
                const bool wasTop = (note == legato.held.back());

                legato.held.erase (std::remove (legato.held.begin(), legato.held.end(), note),
                                    legato.held.end());

                if (legato.held.empty())
                {
                    filteredMidi.addEvent (
                        juce::MidiMessage::noteOff (msg.getChannel(), legato.voiceNote,
                                                     msg.getFloatVelocity()), pos);
                    legato.reset();
                }
                else if (wasTop)
                {
                    // プリング：押さえたままのキーの音程へ戻る
                    const int backTo   = legato.held.back();
                    const int interval = std::abs (note - backTo);

                    if (auto* voice = findActiveVoice (legato.voiceNote))
                        voice->legatoTo (backTo, 0.5f, glideTimeFor (interval));
                }

                continue;
            }

            filteredMidi.addEvent (msg, pos);
            continue;
        }

        filteredMidi.addEvent (msg, pos);
    }

    midiMessages.swapWith (filteredMidi);
}

//==============================================================================

void RaccoGuitarProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // **音源なので、まず消すこと**（グラフから渡るバッファには前のブロックが残っています）
    buffer.clear();

    // 画面の鍵盤で押したぶんを先に合流させる。**`filterMidi()`より前**
    // ——後にすると、鍵盤で押した音だけキースイッチも音域制限も効きません
    keyboardState.processNextMidiBuffer (midiMessages, 0, buffer.getNumSamples(), true);
    filterMidi (midiMessages);

    const float brightness = brightnessParam->load();
    const float sustainSec = sustainParam->load();
    const float pickPos    = pickPosParam->load();
    const float hardness   = hardnessParam->load();
    const float attack     = attackParam->load();

    const float pickupPos  = RaccoGuitarParams::pickupPosition ((int) pickupSelParam->load());
    const auto  art        = (GuitarArticulation) currentArticulation.load();

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<RaccoGuitarVoice*> (synth.getVoice (i)))
            voice->setParameters (brightness, sustainSec, pickPos, pickupPos,
                                   hardness, attack, art);

    synth.renderNextBlock (buffer, midiMessages, 0, buffer.getNumSamples());

    sampleClock += buffer.getNumSamples();

    //--------------------------------------------------------------------------
    // 出力ゲイン
    gainSmoothed.setTargetValue (gainParam->load());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gainSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    //--------------------------------------------------------------------------
    // 画面の`n VOICES`（**書きっぱなし**。読むのは画面のスレッド）
    int active = 0;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (synth.getVoice (i)->isVoiceActive())
            ++active;

    activeVoiceCount.store (active);
}

//==============================================================================

juce::AudioProcessorEditor* RaccoGuitarProcessor::createEditor()
{
    return new RaccoGuitarEditor (*this);
}

void RaccoGuitarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // 奏法はパラメータではないので、**保存する直前に写します**
    // （`RaccoGuitarParameters.h`の最後。書くのはここ＝メッセージスレッドだけ）
    apvts.state.setProperty (articulationProperty(), currentArticulation.load(), nullptr);

    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void RaccoGuitarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // 無ければノーマル（Phase 264より前に保存したものには入っていません）
    const int articulation = apvts.state.getProperty (articulationProperty(),
                                                       (int) GuitarArticulation::normal);

    currentArticulation.store (juce::jlimit (0, numGuitarArticulations - 1, articulation));
}
