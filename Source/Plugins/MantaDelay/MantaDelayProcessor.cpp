#include "MantaDelayProcessor.h"

#include "MantaDelayEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaDelayIdentifier = "manta:delay";
}

//==============================================================================

MantaDelayProcessor::MantaDelayProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaDelay", MantaDelayParams::createParameterLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    parameters.timeMs       = get (MantaDelayParams::timeMs);
    parameters.sync         = get (MantaDelayParams::sync);
    parameters.syncDivision = get (MantaDelayParams::syncDivision);
    parameters.feedback     = get (MantaDelayParams::feedback);
    parameters.mix          = get (MantaDelayParams::mix);
    parameters.outputGain   = get (MantaDelayParams::outputGain);

    parameters.character    = get (MantaDelayParams::character);
    parameters.drive        = get (MantaDelayParams::drive);
    parameters.tone         = get (MantaDelayParams::tone);
    parameters.wowRate      = get (MantaDelayParams::wowRate);
    parameters.wowDepth     = get (MantaDelayParams::wowDepth);
    parameters.flutterRate  = get (MantaDelayParams::flutterRate);
    parameters.flutterDepth = get (MantaDelayParams::flutterDepth);

    // 8.210〜8.212：Phase 3（Phase 242）
    parameters.filterType = get (MantaDelayParams::filterType);
    parameters.filterFreq = get (MantaDelayParams::filterFreq);
    parameters.filterQ    = get (MantaDelayParams::filterQ);
    parameters.filterGain = get (MantaDelayParams::filterGain);
    parameters.filterPost = get (MantaDelayParams::filterPost);

    parameters.lfoShape = get (MantaDelayParams::lfoShape);
    parameters.lfoRate  = get (MantaDelayParams::lfoRate);
    parameters.lfoDepth = get (MantaDelayParams::lfoDepth);

    parameters.duckAmount  = get (MantaDelayParams::duckAmount);
    parameters.duckAttack  = get (MantaDelayParams::duckAttack);
    parameters.duckRelease = get (MantaDelayParams::duckRelease);
}

MantaDelayProcessor::~MantaDelayProcessor() = default;

void MantaDelayProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (mantaDelayIdentifier, description);
}

const juce::String MantaDelayProcessor::getName() const
{
    return Branding::delayPluginName;
}

//==============================================================================

void MantaDelayProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // **最大の長さで確保します。** つまみを回すたびに取り直すと、
    // 音のスレッドで確保が起きます（9.4）
    engine.prepare (sampleRate, samplesPerBlock,
                     juce::jmax (2, getTotalNumOutputChannels()),
                     MantaDelayParams::maxDelayMs / 1000.0);
}

void MantaDelayProcessor::releaseResources()
{
    engine.reset();
}

bool MantaDelayProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // モノラルかステレオ、入口と出口が同じ形であること
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

double MantaDelayProcessor::resolveDelaySeconds()
{
    const bool wantsSync = parameters.sync->load() > 0.5f;

    if (! wantsSync)
    {
        syncBpm.store (0.0);
        return juce::jlimit (MantaDelayParams::minDelayMs, MantaDelayParams::maxDelayMs,
                              (double) parameters.timeMs->load()) / 1000.0;
    }

    //--------------------------------------------------------------------------
    // 8.205：ホストからテンポをもらう（Phase 238）

    double bpm = 0.0;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    syncBpm.store (bpm);

    // **テンポが取れないときは、つまみの値で鳴らします。**
    // 黙って無音にしたり、勝手に120BPMを決め打ちしたりはしません——
    // 画面には`getSyncBpm() == 0`が伝わるので、そこで「来ていない」と出せます
    if (bpm <= 0.0)
        return juce::jlimit (MantaDelayParams::minDelayMs, MantaDelayParams::maxDelayMs,
                              (double) parameters.timeMs->load()) / 1000.0;

    const auto division = (MantaDelayParams::SyncDivision)
                             juce::jlimit (0, MantaDelayParams::getSyncDivisionCount() - 1,
                                            (int) parameters.syncDivision->load());

    // **換算はここ1つ**（`MantaDelayParams::getQuarterNotes()`）。画面も同じものを使います
    const double seconds = MantaDelayParams::getQuarterNotes (division) * 60.0 / bpm;

    return juce::jlimit (MantaDelayParams::minDelayMs / 1000.0,
                          MantaDelayParams::maxDelayMs / 1000.0, seconds);
}

void MantaDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // 使わない出力チャンネルは黙らせる（入口より出口が多い構成でのお約束）
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    MantaDelayEngine::Settings settings;

    settings.delaySeconds = resolveDelaySeconds();
    settings.feedback     = juce::jlimit (0.0f, MantaDelayParams::maxFeedback,
                                           parameters.feedback->load());
    settings.mix          = juce::jlimit (0.0f, 1.0f, parameters.mix->load());
    settings.outputGain   = juce::Decibels::decibelsToGain (parameters.outputGain->load());

    // 8.208：キャラクター（Phase 240）
    settings.character.kind = (MantaDelayCharacter::Kind)
                                 juce::jlimit (0, MantaDelayCharacter::getKindCount() - 1,
                                                (int) parameters.character->load());

    settings.character.drive        = parameters.drive->load();
    settings.character.tone         = parameters.tone->load();
    settings.character.wowRate      = parameters.wowRate->load();
    settings.character.wowDepth     = parameters.wowDepth->load();
    settings.character.flutterRate  = parameters.flutterRate->load();
    settings.character.flutterDepth = parameters.flutterDepth->load();

    // **BBDのカットオフはディレイタイムで決まります**（設計書4-1）
    settings.character.delaySeconds = settings.delaySeconds;

    //--------------------------------------------------------------------------
    // 8.210〜8.212：Phase 3（Phase 242）

    settings.filter.type = (MantaDelayFilter::Type)
                              juce::jlimit (0, MantaDelayFilter::getTypeCount() - 1,
                                             (int) parameters.filterType->load());

    settings.filter.frequencyHz = parameters.filterFreq->load();
    settings.filter.q           = parameters.filterQ->load();
    settings.filter.gainDb      = parameters.filterGain->load();
    settings.filter.post        = parameters.filterPost->load() > 0.5f;

    settings.lfoShape = (MantaDelayLfo::Shape)
                           juce::jlimit (0, MantaDelayLfo::getShapeCount() - 1,
                                          (int) parameters.lfoShape->load());

    settings.lfoRateHz = parameters.lfoRate->load();
    settings.lfoDepth  = parameters.lfoDepth->load();

    settings.duckAmount    = parameters.duckAmount->load();
    settings.duckAttackMs  = parameters.duckAttack->load();
    settings.duckReleaseMs = parameters.duckRelease->load();

    engine.setSettings (settings);
    engine.process (buffer);
}

//==============================================================================

juce::AudioProcessorEditor* MantaDelayProcessor::createEditor()
{
    return new MantaDelayEditor (*this);
}

void MantaDelayProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaDelayProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::ValueTree MantaDelayProcessor::getUiState()
{
    // **毎回引き直すこと。** `replaceState()`を通ると、前に取った`ValueTree`は
    // 古い木を指したままになります
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}
