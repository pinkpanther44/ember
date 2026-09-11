#include "MantaDelayProcessor.h"

#include "MantaDelayEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaDelayIdentifier = "manta:delay";
}

namespace MantaDelayUiState
{
    // 8.214：Phase 243。**音には関係しません**（`MantaDelayProcessor.h`の頭）
    const juce::Identifier selectedTap { "selectedTap" };

    // 8.217：Phase 244
    const juce::Identifier selectedEngine { "selectedEngine" };
}

//==============================================================================

MantaDelayProcessor::MantaDelayProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaDelay", MantaDelayParams::createParameterLayout())
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    // 8.217：**エンジン共通**（Phase 244）
    parameters.mix         = get (MantaDelayParams::mix);
    parameters.outputGain  = get (MantaDelayParams::outputGain);
    parameters.routingMode = get (MantaDelayParams::routingMode);

    // **文字列で引くのはここだけ**（毎ブロック引き直すと無視できない時間になります）
    for (int engine = 0; engine < MantaDelayParams::numEngines; ++engine)
    {
        auto& pointers = engineParameters[(size_t) engine];

        const auto id = [engine, &get] (const char* suffix)
        {
            return get (MantaDelayParams::engineParamId (engine, suffix));
        };

        pointers.timeMs       = id (MantaDelayParams::timeMs);
        pointers.sync         = id (MantaDelayParams::sync);
        pointers.syncDivision = id (MantaDelayParams::syncDivision);
        pointers.feedback     = id (MantaDelayParams::feedback);

        pointers.character    = id (MantaDelayParams::character);
        pointers.drive        = id (MantaDelayParams::drive);
        pointers.tone         = id (MantaDelayParams::tone);
        pointers.wowRate      = id (MantaDelayParams::wowRate);
        pointers.wowDepth     = id (MantaDelayParams::wowDepth);
        pointers.flutterRate  = id (MantaDelayParams::flutterRate);
        pointers.flutterDepth = id (MantaDelayParams::flutterDepth);

        // 8.210〜8.212：Phase 3（Phase 242）
        pointers.filterType = id (MantaDelayParams::filterType);
        pointers.filterFreq = id (MantaDelayParams::filterFreq);
        pointers.filterQ    = id (MantaDelayParams::filterQ);
        pointers.filterGain = id (MantaDelayParams::filterGain);
        pointers.filterPost = id (MantaDelayParams::filterPost);

        pointers.lfoShape = id (MantaDelayParams::lfoShape);
        pointers.lfoRate  = id (MantaDelayParams::lfoRate);
        pointers.lfoDepth = id (MantaDelayParams::lfoDepth);

        pointers.duckAmount  = id (MantaDelayParams::duckAmount);
        pointers.duckAttack  = id (MantaDelayParams::duckAttack);
        pointers.duckRelease = id (MantaDelayParams::duckRelease);

        // 8.214：Phase 4（Phase 243）
        pointers.tapCount = id (MantaDelayParams::tapCount);

        for (int tap = 0; tap < MantaDelayTaps::maxTaps; ++tap)
        {
            auto& tapPointers = pointers.taps[(size_t) tap];

            const auto tapId = [engine, tap, &get] (const char* suffix)
            {
                return get (MantaDelayParams::tapParamId (engine, tap, suffix));
            };

            tapPointers.step  = tapId (MantaDelayParams::tapStep);
            tapPointers.level = tapId (MantaDelayParams::tapLevel);
            tapPointers.pan   = tapId (MantaDelayParams::tapPan);
        }

        // 8.217：Phase 5（Phase 244）
        pointers.level = id (MantaDelayParams::engineLevel);
        pointers.pan   = id (MantaDelayParams::enginePan);
    }
}

//==============================================================================

MantaDelayTaps::Pattern MantaDelayProcessor::buildTapPattern (int engine) const
{
    const auto& pointers = engineParameters[(size_t) juce::jlimit (0, MantaDelayParams::numEngines - 1,
                                                                    engine)];

    MantaDelayTaps::Pattern pattern;

    pattern.count = juce::jlimit (1, MantaDelayTaps::maxTaps,
                                   juce::roundToInt (pointers.tapCount->load()));

    for (int tap = 0; tap < MantaDelayTaps::maxTaps; ++tap)
    {
        const auto& tapPointers = pointers.taps[(size_t) tap];
        auto& destination = pattern.taps[(size_t) tap];

        destination.step  = juce::jlimit (1, MantaDelayTaps::maxStep,
                                           juce::roundToInt (tapPointers.step->load()));
        destination.level = tapPointers.level->load();
        destination.pan   = tapPointers.pan->load();
    }

    return pattern;
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
    const int numChannels = juce::jmax (2, getTotalNumOutputChannels());

    // **最大の長さで確保します。** つまみを回すたびに取り直すと、
    // 音のスレッドで確保が起きます（9.4）
    //
    // 8.217：**Singleのときも両方用意します**（Phase 244）——
    // 途中でモードを変えたときに、**Bだけ用意されていない**状態を作らないため
    for (auto& engine : engines)
        engine.prepare (sampleRate, samplesPerBlock, numChannels,
                         MantaDelayParams::maxDelayMs / 1000.0);

    // 8.217：ルーティングで使う場所（Phase 244）。**ここで取ること**
    engineBufferA.setSize (numChannels, samplesPerBlock, false, true, true);
    engineBufferB.setSize (numChannels, samplesPerBlock, false, true, true);

    dryMono.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
}

void MantaDelayProcessor::releaseResources()
{
    for (auto& engine : engines)
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

double MantaDelayProcessor::resolveDelaySeconds (int engine)
{
    const auto& pointers = engineParameters[(size_t) engine];

    const bool wantsSync = pointers.sync->load() > 0.5f;

    const auto freeSeconds = [&pointers]
    {
        return juce::jlimit (MantaDelayParams::minDelayMs, MantaDelayParams::maxDelayMs,
                              (double) pointers.timeMs->load()) / 1000.0;
    };

    if (! wantsSync)
    {
        // 8.217：**テンポの表示を書くのはエンジンAだけ**（Phase 244）。
        // 両方が書くと、AがSyncでBがFreeのときに**0と実際の値が入れ替わり続けます**
        if (engine == 0)
            syncBpm.store (0.0);

        return freeSeconds();
    }

    //--------------------------------------------------------------------------
    // 8.205：ホストからテンポをもらう（Phase 238）

    double bpm = 0.0;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto hostBpm = position->getBpm())
                bpm = *hostBpm;

    if (engine == 0)
        syncBpm.store (bpm);

    // **テンポが取れないときは、つまみの値で鳴らします。**
    // 黙って無音にしたり、勝手に120BPMを決め打ちしたりはしません——
    // 画面には`getSyncBpm() == 0`が伝わるので、そこで「来ていない」と出せます
    if (bpm <= 0.0)
        return freeSeconds();

    const auto division = (MantaDelayParams::SyncDivision)
                             juce::jlimit (0, MantaDelayParams::getSyncDivisionCount() - 1,
                                            (int) pointers.syncDivision->load());

    // **換算はここ1つ**（`MantaDelayParams::getQuarterNotes()`）。画面も同じものを使います
    const double seconds = MantaDelayParams::getQuarterNotes (division) * 60.0 / bpm;

    return juce::jlimit (MantaDelayParams::minDelayMs / 1000.0,
                          MantaDelayParams::maxDelayMs / 1000.0, seconds);
}

//==============================================================================

MantaDelayEngine::Settings MantaDelayProcessor::buildEngineSettings (int engine)
{
    const auto& pointers = engineParameters[(size_t) engine];

    MantaDelayEngine::Settings settings;

    settings.delaySeconds = resolveDelaySeconds (engine);
    settings.feedback     = juce::jlimit (0.0f, MantaDelayParams::maxFeedback,
                                           pointers.feedback->load());

    // 8.208：キャラクター（Phase 240）
    settings.character.kind = (MantaDelayCharacter::Kind)
                                 juce::jlimit (0, MantaDelayCharacter::getKindCount() - 1,
                                                (int) pointers.character->load());

    settings.character.drive        = pointers.drive->load();
    settings.character.tone         = pointers.tone->load();
    settings.character.wowRate      = pointers.wowRate->load();
    settings.character.wowDepth     = pointers.wowDepth->load();
    settings.character.flutterRate  = pointers.flutterRate->load();
    settings.character.flutterDepth = pointers.flutterDepth->load();

    // **BBDのカットオフはディレイタイムで決まります**（設計書4-1）
    settings.character.delaySeconds = settings.delaySeconds;

    //--------------------------------------------------------------------------
    // 8.210〜8.212：Phase 3（Phase 242）

    settings.filter.type = (MantaDelayFilter::Type)
                              juce::jlimit (0, MantaDelayFilter::getTypeCount() - 1,
                                             (int) pointers.filterType->load());

    settings.filter.frequencyHz = pointers.filterFreq->load();
    settings.filter.q           = pointers.filterQ->load();
    settings.filter.gainDb      = pointers.filterGain->load();
    settings.filter.post        = pointers.filterPost->load() > 0.5f;

    settings.lfoShape = (MantaDelayLfo::Shape)
                           juce::jlimit (0, MantaDelayLfo::getShapeCount() - 1,
                                          (int) pointers.lfoShape->load());

    settings.lfoRateHz = pointers.lfoRate->load();
    settings.lfoDepth  = pointers.lfoDepth->load();

    settings.duckAmount    = pointers.duckAmount->load();
    settings.duckAttackMs  = pointers.duckAttack->load();
    settings.duckReleaseMs = pointers.duckRelease->load();

    // 8.214：Phase 4（Phase 243）。**画面と同じ`buildTapPattern()`**を通します（1.27）
    settings.taps = buildTapPattern (engine);

    return settings;
}

//==============================================================================

void MantaDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());

    // 使わない出力チャンネルは黙らせる（入口より出口が多い構成でのお約束）
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numSamples <= 0 || numChannels <= 0 || numSamples > (int) dryMono.size())
        return;

    //--------------------------------------------------------------------------
    // 8.217：Phase 5aのルーティング（Phase 244／仕様書3-1）

    for (int engine = 0; engine < MantaDelayParams::numEngines; ++engine)
        engines[(size_t) engine].setSettings (buildEngineSettings (engine));

    const auto mode = getRoutingMode();

    const float wetAmount = juce::jlimit (0.0f, 1.0f, parameters.mix->load());
    const float dryAmount = 1.0f - wetAmount;
    const float outputGain = juce::Decibels::decibelsToGain (parameters.outputGain->load());

    // **ダッキングが見る原音**（左右をまとめたもの）。**書き換える前に作ること**——
    // あとから作ると、混ぜたあとの音を見ることになります（8.212）
    {
        const float scale = 1.0f / (float) numChannels;

        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
                sum += buffer.getReadPointer (channel)[i];

            dryMono[(size_t) i] = sum * scale;
        }
    }

    // エンジンの出口のレベルと定位。**パンの式はタップと同じものを使います**（1.27）
    float gainA[2] = { 1.0f, 1.0f };
    float gainB[2] = { 1.0f, 1.0f };

    {
        const auto& a = engineParameters[0];
        const auto& b = engineParameters[1];

        MantaDelayTaps::getPanGains (a.pan->load(), gainA[0], gainA[1]);
        MantaDelayTaps::getPanGains (b.pan->load(), gainB[0], gainB[1]);

        const float levelA = juce::jlimit (0.0f, 1.0f, a.level->load());
        const float levelB = juce::jlimit (0.0f, 1.0f, b.level->load());

        for (int side = 0; side < 2; ++side)
        {
            // モノラルではパンの行き先がないので、Levelだけ（タップと同じ扱い）
            gainA[side] = (numChannels > 1) ? gainA[side] * levelA : levelA;
            gainB[side] = (numChannels > 1) ? gainB[side] * levelB : levelB;
        }
    }

    // **`makeCopyOf()`は確保します**（9.4）。`prepareToPlay()`で取った場所へ
    // `copyFrom()`で写します
    const auto fillFrom = [numChannels, numSamples] (juce::AudioBuffer<float>& destination,
                                                      const juce::AudioBuffer<float>& source)
    {
        const int count = juce::jmin (numSamples, destination.getNumSamples());

        for (int channel = 0; channel < juce::jmin (numChannels, destination.getNumChannels()); ++channel)
            destination.copyFrom (channel, 0, source, channel, 0, count);
    };

    // **Split L/Rはモノラルでは成り立ちません**（分ける先が無い）。
    // 黙ってAだけにします——**片方を無音にするより、鳴るほうがまし**
    const bool usesB = MantaDelayRouting::usesEngineB (mode)
                         && ! (mode == MantaDelayRouting::Mode::splitLR && numChannels < 2)
                         && numSamples <= engineBufferB.getNumSamples();

    fillFrom (engineBufferA, buffer);

    if (mode == MantaDelayRouting::Mode::series && usesB)
    {
        // **AをBの入口へ。** Aのレベルは「Bへどれだけ送るか」になります
        engines[0].processWet (engineBufferA, dryMono.data());

        for (int channel = 0; channel < numChannels; ++channel)
            engineBufferA.applyGain (channel, 0, numSamples, gainA[juce::jmin (channel, 1)]);

        fillFrom (engineBufferB, engineBufferA);

        // **原音はプラグインの入口のまま渡します**（`processWet()`の説明）
        engines[1].processWet (engineBufferB, dryMono.data());
    }
    else
    {
        engines[0].processWet (engineBufferA, dryMono.data());

        if (usesB)
        {
            fillFrom (engineBufferB, buffer);
            engines[1].processWet (engineBufferB, dryMono.data());
        }
    }

    //--------------------------------------------------------------------------
    // 出口で混ぜる

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        const auto* wetA = engineBufferA.getReadPointer (channel);
        const auto* wetB = usesB ? engineBufferB.getReadPointer (channel) : nullptr;

        const int side = juce::jmin (channel, 1);

        for (int i = 0; i < numSamples; ++i)
        {
            float wet = 0.0f;

            switch (mode)
            {
                case MantaDelayRouting::Mode::single:
                    wet = wetA[i] * gainA[side];
                    break;

                case MantaDelayRouting::Mode::dual:
                    wet = wetA[i] * gainA[side] + (wetB != nullptr ? wetB[i] * gainB[side] : 0.0f);
                    break;

                case MantaDelayRouting::Mode::series:
                    // **出てくるのはBだけ**（Aの音はもうBの中を通っています）
                    wet = (wetB != nullptr) ? wetB[i] * gainB[side] : wetA[i] * gainA[side];
                    break;

                case MantaDelayRouting::Mode::splitLR:
                    // **左はA、右はB**
                    if (wetB == nullptr)
                        wet = wetA[i] * gainA[side];
                    else
                        wet = (channel == 0) ? wetA[i] * gainA[0] : wetB[i] * gainB[1];
                    break;

                case MantaDelayRouting::Mode::count:
                default:
                    break;
            }

            data[i] = (data[i] * dryAmount + wet * wetAmount) * outputGain;
        }
    }
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
