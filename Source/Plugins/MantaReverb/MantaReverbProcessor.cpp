#include "MantaReverbProcessor.h"

#include "MantaReverbEditor.h"
#include "../MantaPluginFormat.h"
#include "../../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace
{
    /** 識別子。`MantaPlugins::getEntries()`の表と**同じ文字列**であること。 */
    const char* const mantaReverbIdentifier = "manta:reverb";
}

namespace MantaReverbUiState
{
    // 8.249：Phase 259。**音には関係しません**（`MantaReverbProcessor.h`の頭）
    const juce::Identifier selectedEngine { "selectedEngine" };
}

//==============================================================================

MantaReverbProcessor::MantaReverbProcessor()
    : juce::AudioPluginInstance (BusesProperties()
                                   .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MantaReverb", MantaReverbParams::createParameterLayout())
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    parameters.mix = get (MantaReverbParams::mix);
    parameters.outputGain = get (MantaReverbParams::outputGain);
    parameters.routingMode = get (MantaReverbParams::routingMode);   // 8.249（Phase 259）

    // **文字列で引くのはここだけ**（毎ブロック引き直すと無視できない時間になります）
    for (int engine = 0; engine < MantaReverbParams::numEngines; ++engine)
    {
        auto& pointers = engineParameters[(size_t) engine];

        const auto id = [engine, &get] (const char* suffix)
        {
            return get (MantaReverbParams::engineParamId (engine, suffix));
        };

        pointers.predelay        = id (MantaReverbParams::predelay);
        pointers.decay           = id (MantaReverbParams::decay);
        pointers.size            = id (MantaReverbParams::size);
        pointers.diffusion       = id (MantaReverbParams::diffusion);
        pointers.highDampFreq    = id (MantaReverbParams::highDampFreq);
        pointers.highDampAmount  = id (MantaReverbParams::highDampAmount);
        pointers.lowDampFreq     = id (MantaReverbParams::lowDampFreq);
        pointers.lowDampAmount   = id (MantaReverbParams::lowDampAmount);
        pointers.earlyLevel      = id (MantaReverbParams::earlyLevel);
        pointers.width           = id (MantaReverbParams::width);
        pointers.algorithm       = id (MantaReverbParams::algorithm);   // 8.243（Phase 255）
        pointers.shape           = id (MantaReverbParams::shape);       // 8.246（Phase 256）
        pointers.spread          = id (MantaReverbParams::spread);
        pointers.twinTime        = id (MantaReverbParams::twinTime);       // 8.247（Phase 257）
        pointers.twinFeedback    = id (MantaReverbParams::twinFeedback);
        pointers.twinCross       = id (MantaReverbParams::twinCross);
        pointers.panMonoSum      = id (MantaReverbParams::panMonoSum);     // 8.248（Phase 258）
        pointers.panInvertRight  = id (MantaReverbParams::panInvertRight);
        pointers.panSwap         = id (MantaReverbParams::panSwap);
        pointers.engineLevel     = id (MantaReverbParams::engineLevel);    // 8.249（Phase 259）
        pointers.modulation      = id (MantaReverbParams::modulation);     // 8.250（Phase 260）
        pointers.saturation      = id (MantaReverbParams::saturation);     // 8.251
    }
}

MantaReverbProcessor::~MantaReverbProcessor() = default;

void MantaReverbProcessor::fillInPluginDescription (juce::PluginDescription& description) const
{
    // **組み直さないこと。** 表と食い違うと識別子が変わり、
    // 保存済みのプロジェクトがこのプラグインを見失います（1.27）
    MantaPlugins::findDescription (mantaReverbIdentifier, description);
}

const juce::String MantaReverbProcessor::getName() const
{
    return Branding::reverbPluginName;
}

//==============================================================================

void MantaReverbProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 9.5：**`prepareToPlay()`は何度でも来ます**（再生↔書き出しの行き来）。
    // そのたびに確保し直すこと——**書き出しはデバイスと違うレートで回ります**（8.153）
    for (auto& engine : engines)
        engine.prepare (sampleRate, samplesPerBlock);
}

void MantaReverbProcessor::releaseResources()
{
    for (auto& engine : engines)
        engine.reset();
}

bool MantaReverbProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // モノラルかステレオ、入口と出口が同じ形であること
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

MantaReverbEngine::Settings MantaReverbProcessor::buildEngineSettings (int engine) const
{
    const auto& pointers = engineParameters[(size_t) juce::jlimit (0, MantaReverbParams::numEngines - 1,
                                                                    engine)];

    MantaReverbEngine::Settings settings;

    // 8.243：**番号は必ず範囲へ丸めること**（Phase 255）。
    // オートメーションは0〜1で来るので、丸めないと表の外を指せます
    settings.kind = (MantaReverbAlgorithm::Kind)
                        juce::jlimit (0, MantaReverbAlgorithm::getKindCount() - 1,
                                       juce::roundToInt (pointers.algorithm->load()));

    settings.predelayMs = pointers.predelay->load();
    settings.decaySeconds = pointers.decay->load();

    // **倍率への変換は`sizeScaleFor()`ただ1つ**（1.27）——
    // 画面も同じ関数を使うので、描いている大きさと鳴っている大きさがずれません
    settings.sizeScale = MantaReverbParams::sizeScaleFor (pointers.size->load());

    settings.diffusion = pointers.diffusion->load();
    settings.highDampFrequency = pointers.highDampFreq->load();
    settings.highDampAmount = pointers.highDampAmount->load();
    settings.lowDampFrequency = pointers.lowDampFreq->load();
    settings.lowDampAmount = pointers.lowDampAmount->load();
    settings.earlyLevel = pointers.earlyLevel->load();
    settings.shape = pointers.shape->load();
    settings.spread = pointers.spread->load();
    settings.width = pointers.width->load();

    // 8.247：Phase 4a（Phase 257）。**msから秒へ直すのはここ1箇所**（1.27）
    settings.twin.timeSeconds = pointers.twinTime->load() / 1000.0;
    settings.twin.feedback = pointers.twinFeedback->load();
    settings.twin.cross = pointers.twinCross->load();

    // 8.248：Phase 4b（Phase 258）。**boolも`atomic<float>`で来ます**（0か1）
    settings.panoramaMonoSum = pointers.panMonoSum->load() > 0.5f;
    settings.panoramaInvertRight = pointers.panInvertRight->load() > 0.5f;
    settings.panoramaSwap = pointers.panSwap->load() > 0.5f;

    // 8.250〜8.251：Phase 6（Phase 260）
    settings.modulation = pointers.modulation->load();
    settings.saturation = pointers.saturation->load();

    return settings;
}

MantaReverbEngine::Settings MantaReverbProcessor::getDisplaySettings (int engine) const
{
    return buildEngineSettings (engine);
}

MantaReverbRouting::Mode MantaReverbProcessor::getRoutingMode() const
{
    // **番号は必ず範囲へ丸めること**（8.243と同じ。オートメーションは0〜1で来ます）
    return (MantaReverbRouting::Mode)
               juce::jlimit (0, MantaReverbRouting::getModeCount() - 1,
                              juce::roundToInt (parameters.routingMode->load()));
}

void MantaReverbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // グラフから渡るバッファには、使っていない出力に前のブロックが残っています
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // **設定はブロックの頭で1回**（サンプルごとに`atomic`を読みに行かない）
    for (int engine = 0; engine < MantaReverbParams::numEngines; ++engine)
        engines[(size_t) engine].setSettings (buildEngineSettings (engine));

    const float mix = juce::jlimit (0.0f, 1.0f, parameters.mix->load());
    const float outputGain = juce::Decibels::decibelsToGain (parameters.outputGain->load());

    // 8.249：ルーティング（Phase 259）
    const auto mode = getRoutingMode();
    const float sumGain = MantaReverbRouting::getSumGain (mode);

    const float levelA = juce::Decibels::decibelsToGain (
                             engineParameters[0].engineLevel->load());
    const float levelB = juce::Decibels::decibelsToGain (
                             engineParameters[1].engineLevel->load());

    auto* left = buffer.getWritePointer (0);
    auto* right = (numChannels > 1) ? buffer.getWritePointer (1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // **モノラルのときは同じ音を両方へ入れます。**
        // 片方を0にすると、FDNの入口のばらし方（`(L−R)`の行）が
        // 原音そのものになってしまい、狙った散らばりになりません
        const float dryLeft = left[sample];
        const float dryRight = (right != nullptr) ? right[sample] : dryLeft;

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        //----------------------------------------------------------------------
        // 8.249：**入口はモードごと**（Phase 259／`MantaReverbRouting`の表）

        float aLeft = 0.0f, aRight = 0.0f;
        float bLeft = 0.0f, bRight = 0.0f;

        switch (mode)
        {
            case MantaReverbRouting::Mode::single:
                engines[0].processSample (dryLeft, dryRight, aLeft, aRight);
                wetLeft = aLeft * levelA;
                wetRight = aRight * levelA;
                break;

            case MantaReverbRouting::Mode::cascade:
                // **AをBの入口へ。** Aのレベルは「Bへどれだけ送るか」になります
                // （ディレイのSeriesと同じ扱い。8.217）
                engines[0].processSample (dryLeft, dryRight, aLeft, aRight);
                engines[1].processSample (aLeft * levelA, aRight * levelA, bLeft, bRight);
                wetLeft = bLeft * levelB;
                wetRight = bRight * levelB;
                break;

            case MantaReverbRouting::Mode::monoSplit:
                // **左をA、右をB。** それぞれ1chぶんを両方の入口へ入れます——
                // 片側を0にすると、エンジンの中の`(L−R)`の行が原音そのものになります
                engines[0].processSample (dryLeft, dryLeft, aLeft, aRight);
                engines[1].processSample (dryRight, dryRight, bLeft, bRight);
                wetLeft = (aLeft * levelA + bLeft * levelB) * sumGain;
                wetRight = (aRight * levelA + bRight * levelB) * sumGain;
                break;

            case MantaReverbRouting::Mode::stereoSplit:
                engines[0].processSample (dryLeft, dryRight, aLeft, aRight);
                engines[1].processSample (dryLeft, dryRight, bLeft, bRight);
                wetLeft = (aLeft * levelA + bLeft * levelB) * sumGain;
                wetRight = (aRight * levelA + bRight * levelB) * sumGain;
                break;

            default:
                break;
        }

        // **混ぜるのは最後。** `Mix`は原音と響きの割合で、`Output`は全体の音量です
        left[sample] = (dryLeft * (1.0f - mix) + wetLeft * mix) * outputGain;

        if (right != nullptr)
            right[sample] = (dryRight * (1.0f - mix) + wetRight * mix) * outputGain;
    }

    // **`Single`ではBを回していません**（上のswitch）——鳴らないものにCPUを使わない

    // 3本目以降は触りません（`isBusesLayoutSupported()`でモノラルかステレオに限っています）
}

//==============================================================================

juce::AudioProcessorEditor* MantaReverbProcessor::createEditor()
{
    return new MantaReverbEditor (*this);
}

void MantaReverbProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MantaReverbProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::ValueTree MantaReverbProcessor::getUiState()
{
    // **毎回引き直すこと。** `replaceState()`を通ると、前に取った`ValueTree`は
    // 古い木を指したままになります
    return apvts.state.getOrCreateChildWithName ("UI", nullptr);
}
