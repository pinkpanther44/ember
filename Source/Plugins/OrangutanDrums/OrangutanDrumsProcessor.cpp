#include "OrangutanDrumsProcessor.h"

#include "OrangutanDrumsEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const orangutanDrumsIdentifier = "manta:drums";
}

//==============================================================================

juce::AudioProcessor::BusesProperties OrangutanDrumsProcessor::makeBusesProperties()
{
    // 8.289：**メインのステレオに加えて、パッドごとのパラアウト16本**（Phase 282）。
    //
    // **既定では無効**にしておきます。本体は「ドラムアウトトラックがあるトラックだけ」
    // バスを有効にして音源を作り直すので（8.144）、**普段は2ch出力のまま**です。
    //
    // 一律で有効にしないのは8.111と同じ理由——バス構成を触るのは
    // プラグインによっては落ちるほど繊細で、**要るときだけ**にします。
    auto properties = BusesProperties()
                        // **入力は持ちません**（音源なので。9.5）
                        .withOutput ("Main", juce::AudioChannelSet::stereo(), true);

    for (int pad = 0; pad < numPads; ++pad)
        properties = properties.withOutput ("Pad " + juce::String (pad + 1).paddedLeft ('0', 2),
                                             juce::AudioChannelSet::stereo(), false);

    return properties;
}

OrangutanDrumsProcessor::OrangutanDrumsProcessor()
    : juce::AudioPluginInstance (makeBusesProperties()),
      apvts (*this, nullptr, "OrangutanDrums", OrangutanDrumsParams::createParameterLayout())
{
    cacheParameterPointers();

    for (auto& activity : padActivity)
        activity.store (0.0f);
}

void OrangutanDrumsProcessor::cacheParameterPointers()
{
    using namespace OrangutanDrumsParams;

    for (int pad = 0; pad < numPads; ++pad)
    {
        auto& pointers = padPointers[(size_t) pad];

        pointers.engine = apvts.getRawParameterValue (padId (pad, padEngine));
        pointers.tune   = apvts.getRawParameterValue (padId (pad, padTune));
        pointers.decay  = apvts.getRawParameterValue (padId (pad, padDecay));
        pointers.tone   = apvts.getRawParameterValue (padId (pad, padTone));
        pointers.snap   = apvts.getRawParameterValue (padId (pad, padSnap));
        pointers.level  = apvts.getRawParameterValue (padId (pad, padLevel));
        pointers.pan    = apvts.getRawParameterValue (padId (pad, padPan));
        pointers.send   = apvts.getRawParameterValue (padId (pad, padSend));
        pointers.out    = apvts.getRawParameterValue (padId (pad, padOut));
    }

    driveParam  = apvts.getRawParameterValue (drive);
    glueParam   = apvts.getRawParameterValue (glue);
    reverbParam = apvts.getRawParameterValue (reverb);
    sizeParam   = apvts.getRawParameterValue (size);
    dampParam   = apvts.getRawParameterValue (damp);
    volumeParam = apvts.getRawParameterValue (volume);
}

void OrangutanDrumsProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと**（1.27）
    MantaPlugins::findDescription (orangutanDrumsIdentifier, description);
}

const juce::String OrangutanDrumsProcessor::getName() const
{
    return Branding::drumsPluginName;
}

juce::ValueTree OrangutanDrumsProcessor::getUiState()
{
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}

//==============================================================================

void OrangutanDrumsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 9.5：**渡された値を使うこと**（デバイスへ訊きに行かない）
    currentFs = sampleRate > 0.0 ? sampleRate : 44100.0;

    // **パッドごとに違う種を渡します。** 同じ種だと、ノイズ系のエンジンを
    // 2つ並べたときに**同じ雑音が重なって**、位相のそろった1つの音に聞こえます
    for (int pad = 0; pad < numPads; ++pad)
        voices[(size_t) pad].prepare (currentFs, (juce::uint32) (0x9e3779b9u + 2654435761u * (juce::uint32) pad));

    masterFX.prepare (currentFs);

    masterGain.reset (currentFs, 0.02);
    masterGain.setCurrentAndTargetValue (volumeParam != nullptr ? volumeParam->load() : 0.8f);

    const int blockSize = juce::jmax (64, samplesPerBlock);

    scratch.setSize (4, blockSize, false, true, true);
    scratch.clear();

    monoRight.setSize (1, blockSize, false, true, true);
    monoRight.clear();

    for (auto& activity : padActivity)
        activity.store (0.0f);

    activeVoiceCount.store (0);

    // **画面へ知らせるため**に数えておきます（「いまは届いていません」の表示）
    int enabled = 0;

    for (int bus = 1; bus < getBusCount (false); ++bus)
        if (auto* output = getBus (false, bus))
            if (output->isEnabled())
                ++enabled;

    directOutsEnabled.store (enabled > 0);
}

bool OrangutanDrumsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainOutput != juce::AudioChannelSet::stereo()
         && mainOutput != juce::AudioChannelSet::mono())
        return false;

    // 8.289：**パラアウトは、無効かステレオのどちらか**（Phase 282）。
    //
    // **ここで受け入れた形しか来ません。** 本体は欲しい形を全部組んでから
    // `checkBusesLayoutSupported()`で訊きます（8.144）——ここが厳しすぎると
    // **断られて、1本も有効になりません**。逆に緩すぎると、
    // 受け取ったバッファのチャンネル数が思っているものと違います。
    for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
    {
        const auto& set = layouts.outputBuses.getReference (bus);

        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================

orangutan::VoiceParams OrangutanDrumsProcessor::readPadParams (int pad) const noexcept
{
    orangutan::VoiceParams params;

    if (pad < 0 || pad >= numPads)
        return params;

    const auto& pointers = padPointers[(size_t) pad];

    if (pointers.engine == nullptr)
        return params;

    params.engine = juce::jlimit (0, orangutan::ENG_COUNT - 1, (int) pointers.engine->load());
    params.tune   = pointers.tune->load();
    params.decay  = pointers.decay->load();
    params.tone   = pointers.tone->load();
    params.snap   = pointers.snap->load();
    params.level  = pointers.level->load();
    params.pan    = pointers.pan->load();
    params.send   = pointers.send->load();

    return params;
}

int OrangutanDrumsProcessor::getPadEngine (int pad) const noexcept
{
    if (pad < 0 || pad >= numPads)
        return 0;

    auto* pointer = padPointers[(size_t) pad].engine;

    return pointer != nullptr ? juce::jlimit (0, orangutan::ENG_COUNT - 1, (int) pointer->load()) : 0;
}

bool OrangutanDrumsProcessor::isPadDirectOut (int pad) const noexcept
{
    if (pad < 0 || pad >= numPads)
        return false;

    auto* pointer = padPointers[(size_t) pad].out;

    return pointer != nullptr && pointer->load() > 0.5f;
}

float OrangutanDrumsProcessor::getPadActivity (int pad) const noexcept
{
    return (pad >= 0 && pad < numPads) ? padActivity[(size_t) pad].load() : 0.0f;
}

//==============================================================================

void OrangutanDrumsProcessor::triggerPad (int pad, float velocity)
{
    if (pad < 0 || pad >= numPads)
        return;

    const auto params = readPadParams (pad);

    // チョークグループ（クローズハットがオープンハットを止める）。
    // **群はエンジンで決まります**——どのパッドに割り当てても効きます
    const int group = orangutan::engineChokeGroup (params.engine);

    if (group > 0)
    {
        for (int other = 0; other < numPads; ++other)
        {
            if (other == pad || ! voices[(size_t) other].isActive())
                continue;

            if (voices[(size_t) other].chokeGroup() == group)
                voices[(size_t) other].choke (4.0f);
        }
    }

    voices[(size_t) pad].trigger (params, velocity, -1.0f);

    padActivity[(size_t) pad].store (juce::jlimit (0.25f, 1.0f, velocity));
}

void OrangutanDrumsProcessor::triggerPadFromUI (int pad, float velocity)
{
    if (pad < 0 || pad >= numPads)
        return;

    // **鍵盤と同じ道**（クラスの説明）
    keyboardState.noteOn (1, padBaseNote + pad, juce::jlimit (0.05f, 1.0f, velocity));
    keyboardState.noteOff (1, padBaseNote + pad, 0.0f);
}

void OrangutanDrumsProcessor::handleMidiMessage (const juce::MidiMessage& message)
{
    // 8.289：**パッドの音域だけです**（Phase 282／ベースゾーンを廃止）
    if (message.isNoteOn() && isPadNote (message.getNoteNumber()))
        triggerPad (message.getNoteNumber() - padBaseNote, message.getFloatVelocity());
}

void OrangutanDrumsProcessor::renderSegment (int offset, int numSamples)
{
    if (numSamples <= 0)
        return;

    // **行き先はパッドごとに違います**（MAIN か、そのパッドのパラアウト）
    for (int pad = 0; pad < numPads; ++pad)
        voices[(size_t) pad].renderAdd (padDestinationL[pad] + offset, padDestinationR[pad] + offset,
                                         padSendL[pad] + offset, padSendR[pad] + offset, numSamples);
}

//==============================================================================

void OrangutanDrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    buffer.clear();

    auto mainBus = getBusBuffer (buffer, false, 0);
    const int mainChannels = mainBus.getNumChannels();

    if (numSamples <= 0 || mainChannels <= 0)
    {
        midiMessages.clear();
        return;
    }

    if (scratch.getNumSamples() < numSamples)
        scratch.setSize (4, numSamples, false, true, true);

    if (monoRight.getNumSamples() < numSamples)
        monoRight.setSize (1, numSamples, false, true, true);

    scratch.clear();
    monoRight.clear();

    // 画面のパッドから来たノートを合流させる
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    masterFX.setParams (driveParam->load(), glueParam->load(),
                         reverbParam->load(), sizeParam->load(), dampParam->load());

    masterGain.setTargetValue (volumeParam->load());

    //--------------------------------------------------------------------------
    // 行き先を決める。**ブロックの先頭で1回だけ**
    auto* sendL = scratch.getWritePointer (0);
    auto* sendR = scratch.getWritePointer (1);
    auto* dumpL = scratch.getWritePointer (2);   // DIRECTのパッドの送りを捨てる先
    auto* dumpR = scratch.getWritePointer (3);

    mainL = mainBus.getWritePointer (0);
    mainR = mainChannels > 1 ? mainBus.getWritePointer (1) : monoRight.getWritePointer (0);

    const int numBuses = getBusCount (false);

    for (int pad = 0; pad < numPads; ++pad)
    {
        float* destinationL = mainL;
        float* destinationR = mainR;
        bool direct = false;

        // **バスが有効なときだけパラアウトへ。** 無効ならMAINへ落とします
        // （受け皿を作る前に切り替えても音が消えないように。クラスの説明）
        if (isPadDirectOut (pad) && (pad + 1) < numBuses)
        {
            if (auto* bus = getBus (false, pad + 1))
            {
                if (bus->isEnabled())
                {
                    auto busBuffer = getBusBuffer (buffer, false, pad + 1);

                    if (busBuffer.getNumChannels() > 0)
                    {
                        destinationL = busBuffer.getWritePointer (0);
                        destinationR = busBuffer.getNumChannels() > 1
                                         ? busBuffer.getWritePointer (1)
                                         : busBuffer.getWritePointer (0);
                        direct = true;
                    }
                }
            }
        }

        padDestinationL[pad] = destinationL;
        padDestinationR[pad] = destinationR;

        // **DIRECTのパッドはリバーブへ送りません**（返りだけメインに乗ると辻褄が合いません）
        padSendL[pad] = direct ? dumpL : sendL;
        padSendR[pad] = direct ? dumpR : sendR;
    }

    //--------------------------------------------------------------------------
    // MIDIの位置でブロックを割って、サンプル精度で鳴らす
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int time = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (time > position)
        {
            renderSegment (position, time - position);
            position = time;
        }

        handleMidiMessage (metadata.getMessage());
    }

    if (position < numSamples)
        renderSegment (position, numSamples - position);

    //--------------------------------------------------------------------------
    // マスター段（DRIVE → GLUE → REVERB → リミッタ）と出力。**メインのバスだけ**
    for (int i = 0; i < numSamples; ++i)
    {
        float l = mainL[i];
        float r = mainR[i];

        masterFX.process (l, r, sendL[i], sendR[i]);

        const float gain = masterGain.getNextValue();

        mainL[i] = l * gain;
        mainR[i] = r * gain;
    }

    if (mainChannels == 1)
        for (int i = 0; i < numSamples; ++i)
            mainL[i] = 0.5f * (mainL[i] + mainR[i]);

    //--------------------------------------------------------------------------
    // パラアウトは**素のまま**出します（マスター段を通りません）。発散だけ止めます
    for (int bus = 1; bus < numBuses; ++bus)
    {
        if (auto* output = getBus (false, bus))
        {
            if (! output->isEnabled())
                continue;

            auto busBuffer = getBusBuffer (buffer, false, bus);

            for (int channel = 0; channel < busBuffer.getNumChannels(); ++channel)
            {
                auto* data = busBuffer.getWritePointer (channel);

                for (int i = 0; i < numSamples; ++i)
                    data[i] = juce::jlimit (-2.0f, 2.0f, orangutan::sanitise (data[i]));
            }
        }
    }

    //--------------------------------------------------------------------------
    // 表示のための数（音には関わりません）
    // **パラアウトが届く先があるか**（画面が「いまは届いていません」と出すのに使います）。
    // 本体はドラムアウトトラックがあるときだけバスを有効にします（8.144）
    bool anyBusEnabled = false;

    for (int bus = 1; bus < numBuses && ! anyBusEnabled; ++bus)
        if (auto* output = getBus (false, bus))
            anyBusEnabled = output->isEnabled();

    directOutsEnabled.store (anyBusEnabled);

    int active = 0;

    // **発光の減衰は、ブロックの長さで決めます。** ブロックごとに同じ率を掛けると、
    // バッファ長を変えたときに光り方が変わります（64サンプルと1024サンプルで別物に）
    const float decay = std::exp (-(float) numSamples / (float) (0.12 * currentFs));

    for (int pad = 0; pad < numPads; ++pad)
    {
        if (voices[(size_t) pad].isActive())
            ++active;

        const float value = padActivity[(size_t) pad].load();

        if (value > 0.001f)
            padActivity[(size_t) pad].store (value * decay);
        else if (value > 0.0f)
            padActivity[(size_t) pad].store (0.0f);
    }

    activeVoiceCount.store (active);

    midiMessages.clear();
}

//==============================================================================

std::optional<juce::String> OrangutanDrumsProcessor::getNameForMidiNoteNumber (int note,
                                                                                int midiChannel)
{
    juce::ignoreUnused (midiChannel);

    if (! isPadNote (note))
        return {};

    const int pad = note - padBaseNote;

    return juce::String (pad + 1).paddedLeft ('0', 2) + " "
             + orangutan::engineName (getPadEngine (pad));
}

//==============================================================================

juce::AudioProcessorEditor* OrangutanDrumsProcessor::createEditor()
{
    return new OrangutanDrumsEditor (*this);
}

void OrangutanDrumsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // **`<UI>`の子（選んでいるパッドと、開いている頁）も一緒に入ります**
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OrangutanDrumsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
