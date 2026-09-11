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
    parameters.routingMode   = get (MantaDelayParams::routingMode);
    parameters.crossFeedback = get (MantaDelayParams::crossFeedback);   // 8.218（Phase 245）

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

        // 8.219〜8.221：Phase 6（Phase 246）
        pointers.freeze    = id (MantaDelayParams::freeze);
        pointers.reverse   = id (MantaDelayParams::reverse);
        pointers.diffusion = id (MantaDelayParams::diffusion);
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
    inputCopy.setSize (numChannels, samplesPerBlock, false, true, true);

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

    // 8.219〜8.221：Phase 6（Phase 246）
    settings.freeze    = pointers.freeze->load() > 0.5f;
    settings.reverse   = pointers.reverse->load() > 0.5f;
    settings.diffusion = pointers.diffusion->load();

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

    if (numSamples <= 0 || numChannels <= 0
         || numSamples > (int) dryMono.size()
         || numSamples > inputCopy.getNumSamples())
        return;

    //--------------------------------------------------------------------------
    // 8.217〜8.218：ルーティング（Phase 244・245／仕様書3-1）

    for (int engine = 0; engine < MantaDelayParams::numEngines; ++engine)
        engines[(size_t) engine].setSettings (buildEngineSettings (engine));

    const auto mode = getRoutingMode();

    const float wetAmount = juce::jlimit (0.0f, 1.0f, parameters.mix->load());
    const float dryAmount = 1.0f - wetAmount;
    const float outputGain = juce::Decibels::decibelsToGain (parameters.outputGain->load());

    // 8.218：**戻りをどれだけ入れ替えるか**（Phase 245）。判断は`MantaDelayRouting`ただ1つ
    const float cross = MantaDelayRouting::getCrossAmount (mode, parameters.crossFeedback->load());
    const float keep = 1.0f - cross;

    const bool usesB = MantaDelayRouting::usesEngineB (mode)
                         && ! (mode == MantaDelayRouting::Mode::splitLR && numChannels < 2);

    //--------------------------------------------------------------------------
    // 入口を取っておく。**`buffer`は出口として書き換えます**

    const float monoScale = 1.0f / (float) numChannels;

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;

        for (int channel = 0; channel < numChannels; ++channel)
            sum += buffer.getReadPointer (channel)[i];

        dryMono[(size_t) i] = sum * monoScale;
    }

    // **`makeCopyOf()`は確保します**（9.4）。`prepareToPlay()`で取った場所へ写します
    for (int channel = 0; channel < juce::jmin (numChannels, inputCopy.getNumChannels()); ++channel)
        inputCopy.copyFrom (channel, 0, buffer, channel, 0, numSamples);

    //--------------------------------------------------------------------------
    // エンジンの出口のレベルと定位。**パンの式はタップと同じものを使います**（1.27）

    float gainA[2] = { 1.0f, 1.0f };
    float gainB[2] = { 1.0f, 1.0f };

    {
        const auto& a = engineParameters[0];
        const auto& b = engineParameters[1];

        // 8.218：**Ping-PongではPanが効きません**（左右を決めるのはモードそのもの）
        if (MantaDelayRouting::usesEnginePan (mode) && numChannels > 1)
        {
            MantaDelayTaps::getPanGains (a.pan->load(), gainA[0], gainA[1]);
            MantaDelayTaps::getPanGains (b.pan->load(), gainB[0], gainB[1]);
        }

        const float levelA = juce::jlimit (0.0f, 1.0f, a.level->load());
        const float levelB = juce::jlimit (0.0f, 1.0f, b.level->load());

        for (int side = 0; side < 2; ++side)
        {
            gainA[side] *= levelA;
            gainB[side] *= levelB;
        }
    }

    //--------------------------------------------------------------------------
    // 8.218：**1サンプルずつ、2つのエンジンを交互に回します**（Phase 245）。
    //
    // Phase 5aはブロックごとに`processWet()`を呼んでいましたが、
    // クロスフィードバックは**Aの戻りがBの線へ入る**ので間に合いません——
    // Aを1ブロック回し終えてからBを回すと、Bが受け取るのは**1ブロック遅れたA**です。
    //
    // **道は1本にしました。** Singleも同じ輪を通ります（`usesB`が`false`になるだけ）——
    // モードごとに別の道を作ると、**片方だけ直す日**が来ます（1.27）。

    if (engines[0].beginBlock (numChannels) <= 0)
        return;

    if (usesB && engines[1].beginBlock (numChannels) <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        engines[0].beginSample (dryMono[(size_t) i]);

        if (usesB)
            engines[1].beginSample (dryMono[(size_t) i]);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const int side = juce::jmin (channel, 1);

            const float input = inputCopy.getReadPointer (channel)[i];

            // **両方読んでから、両方書く。** 先にAへ書いてしまうと、
            // Bが読むのは**もうAの新しい音が入った線**になります
            float wetA = 0.0f, feedbackA = 0.0f;
            float wetB = 0.0f, feedbackB = 0.0f;

            engines[0].readChannel (channel, wetA, feedbackA);

            if (usesB)
                engines[1].readChannel (channel, wetB, feedbackB);

            //------------------------------------------------------------------
            // 8.218：**戻りを入れ替える**（Phase 245）。
            //
            // **混ぜること。足さないこと。** `自分 + cross × 相手`にすると、
            // 一周の利得が`feedback`を超えます——8.209（Driveの補正）と
            // 8.210（フィルターの山）でやったのと**まったく同じ間違い**です。
            //
            // 混ぜる形なら、行列`[[(1-x)a, xa], [xb, (1-x)b]]`の固有値は
            // 大きいほうでも`max(a, b)`を超えません（`x = 1`のときは`±√(ab)`）。
            // **つまり、どこまで回しても`feedback`の上限95%が効いています。**

            const float toA = usesB ? (feedbackA * keep + feedbackB * cross) : feedbackA;
            const float toB = usesB ? (feedbackB * keep + feedbackA * cross) : feedbackB;

            //------------------------------------------------------------------
            // 入口（モードごと）

            float inA = 0.0f;
            float inB = 0.0f;

            switch (mode)
            {
                case MantaDelayRouting::Mode::single:
                    inA = input;
                    break;

                case MantaDelayRouting::Mode::dual:
                    inA = input;
                    inB = input;
                    break;

                case MantaDelayRouting::Mode::series:
                    // **AをBの入口へ。** Aのレベルは「Bへどれだけ送るか」になります
                    inA = input;
                    inB = wetA * gainA[side];
                    break;

                case MantaDelayRouting::Mode::splitLR:
                    // **Aは左だけ、Bは右だけ**（モノラルのときは`usesB`が`false`なのでAだけ）
                    inA = (channel == 0 || ! usesB) ? input : 0.0f;
                    inB = (channel == 1) ? input : 0.0f;
                    break;

                case MantaDelayRouting::Mode::pingPong:
                    // 8.218：**入口はAだけ**（Phase 245）。Bは相手の戻りだけで鳴ります——
                    // Bにも入れると、1回目から左右そろって鳴って**跳ねません**。
                    //
                    // **左右はまとめて入れます**（`collapsesInputToMono()`）——
                    // 両エンジンをハードパンするので、ステレオのまま入れても
                    // 左右の情報は残らず、**1回目の反復が片側だけ欠けます**
                    inA = dryMono[(size_t) i];
                    break;

                case MantaDelayRouting::Mode::count:
                default:
                    break;
            }

            engines[0].writeChannel (channel, inA, toA);

            if (usesB)
                engines[1].writeChannel (channel, inB, toB);

            //------------------------------------------------------------------
            // 出口

            float wet = 0.0f;

            switch (mode)
            {
                case MantaDelayRouting::Mode::single:
                    wet = wetA * gainA[side];
                    break;

                case MantaDelayRouting::Mode::dual:
                    wet = wetA * gainA[side] + wetB * gainB[side];
                    break;

                case MantaDelayRouting::Mode::series:
                    // **出てくるのはBだけ**（Aの音はもうBの中を通っています）
                    wet = usesB ? wetB * gainB[side] : wetA * gainA[side];
                    break;

                case MantaDelayRouting::Mode::splitLR:
                case MantaDelayRouting::Mode::pingPong:
                    // **左はA、右はB**
                    if (! usesB)
                        wet = wetA * gainA[side];
                    else
                        wet = (channel == 0) ? wetA * gainA[0] : wetB * gainB[1];
                    break;

                case MantaDelayRouting::Mode::count:
                default:
                    break;
            }

            buffer.getWritePointer (channel)[i] = (input * dryAmount + wet * wetAmount) * outputGain;
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
