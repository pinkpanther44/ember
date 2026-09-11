#include "MantaPluginFormat.h"

#include "MantaEQ/MantaEQProcessor.h"
#include "MantaComp/MantaCompProcessor.h"
#include "MantaSynth/MantaSynthProcessor.h"
#include "MantaDelay/MantaDelayProcessor.h"   // 8.204：4つめ（Phase 238）
#include "../Branding.h"   // 8.175：ブランドごとの名前（Phase 216）

namespace MantaPlugins
{
    namespace
    {
        /** `pluginFormatName`。**識別子の一部なので、変えると保存済みのプロジェクトが
            プラグインを見失います**（`MantaPluginFormat.h`）。 */
        const char* const formatNameLiteral = Branding::pluginFormatName;

        /** 一覧に出すメーカー名。ブラウザはこれで束ねます（`BrowserPanel::getManufacturerLabel()`）。
            **識別子には入らない**ので、変えても保存済みのプロジェクトは壊れません。 */
        const char* const manufacturerLiteral = Branding::pluginManufacturer;
    }

    juce::String getFormatName()
    {
        return juce::String (formatNameLiteral);
    }

    const std::vector<Entry>& getEntries()
    {
        // **足すときはここへ1行。** `PluginManager`もブラウザも表を見ているので、
        // ほかに書き足す場所はありません（1.27）。
        //
        // `uniqueId`は4文字の目印を数値にしたもの（'M','E','Q','1'）。
        // **意味は無くて構いませんが、二度と変えないこと**（識別子の一部）。
        static const std::vector<Entry> entries
        {
            {
                "manta:eq",
                Branding::eqPluginName,
                "Parametric EQ",
                "Fx|EQ",
                "1.0.0",
                0x4d455131,   // 'MEQ1'
                false,
                [] () -> std::unique_ptr<juce::AudioPluginInstance>
                {
                    return std::make_unique<MantaEQProcessor>();
                }
            },
            {
                "manta:comp",
                Branding::compPluginName,
                "Compressor",
                "Fx|Dynamics",
                "1.0.0",
                0x4d434d31,   // 'MCM1'
                false,
                [] () -> std::unique_ptr<juce::AudioPluginInstance>
                {
                    return std::make_unique<MantaCompProcessor>();
                }
            },
            {
                "manta:delay",
                Branding::delayPluginName,
                "Delay",
                "Fx|Delay",
                "1.0.0",
                0x4d444c31,   // 'MDL1'
                false,
                [] () -> std::unique_ptr<juce::AudioPluginInstance>
                {
                    return std::make_unique<MantaDelayProcessor>();
                }
            },
            {
                "manta:synth",
                Branding::synthPluginName,
                "Virtual Analog Synth",
                // **音源はVST3と同じ書き方で"Instrument|"から始めること**。
                // ブラウザの分類はこの文字列を切って作っています（8.68）
                "Instrument|Synth",
                "1.0.0",
                0x4d535931,   // 'MSY1'
                true,         // **音源**（ここがtrueだと入力を持たない。下の`numInputChannels`）
                [] () -> std::unique_ptr<juce::AudioPluginInstance>
                {
                    return std::make_unique<MantaSynthProcessor>();
                }
            },
        };

        return entries;
    }

    juce::Array<juce::PluginDescription> getDescriptions()
    {
        juce::Array<juce::PluginDescription> descriptions;

        for (const auto& entry : getEntries())
        {
            juce::PluginDescription description;

            description.name             = entry.name;
            description.descriptiveName  = entry.descriptiveName;
            description.pluginFormatName = formatNameLiteral;
            description.category         = entry.category;
            description.manufacturerName = manufacturerLiteral;
            description.version          = entry.version;
            description.fileOrIdentifier = entry.identifier;
            description.uniqueId         = entry.uniqueId;
            description.deprecatedUid    = 0;
            description.isInstrument     = entry.isInstrument;

            // **ステレオ2in/2outで名乗ること。** グラフの数珠つなぎ
            // （`AudioEngine::connectTrackChain()`）は、2ch入出力でないものを
            // 経路から外します
            description.numInputChannels  = entry.isInstrument ? 0 : 2;
            description.numOutputChannels = 2;

            // 実体はexeの中なので、ファイルの更新日時という概念がありません。
            // **空のままにしておくこと**（`pluginNeedsRescanning()`は常にfalse）
            description.lastFileModTime = juce::Time();
            description.lastInfoUpdateTime = juce::Time::getCurrentTime();

            descriptions.add (description);
        }

        return descriptions;
    }

    bool findDescription (const juce::String& identifier, juce::PluginDescription& result)
    {
        for (const auto& description : getDescriptions())
        {
            if (description.fileOrIdentifier == identifier)
            {
                result = description;
                return true;
            }
        }

        return false;
    }

    bool isMantaPlugin (const juce::PluginDescription& description)
    {
        return description.pluginFormatName == formatNameLiteral;
    }
}

//==============================================================================

juce::String MantaPluginFormat::getName() const
{
    return MantaPlugins::getFormatName();
}

void MantaPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                                              const juce::String& fileOrIdentifier)
{
    juce::PluginDescription description;

    if (MantaPlugins::findDescription (fileOrIdentifier, description))
        results.add (new juce::PluginDescription (description));
}

bool MantaPluginFormat::fileMightContainThisPluginType (const juce::String& fileOrIdentifier)
{
    // `AudioPluginFormatManager::findFormatForDescription()`が、
    // **フォーマット名の一致に加えてこれも見ます。** 表に無い識別子を弾いておくと、
    // 消したプラグインが入ったままの古いプロジェクトを開いたときに、
    // 「見つかりません」で止まって**nullptrのスロットとして残ります**
    // （落ちるのではなく、番号がずれないまま欠番になる。`rebuildTrackNodes()`）
    juce::PluginDescription description;

    return MantaPlugins::findDescription (fileOrIdentifier, description);
}

juce::String MantaPluginFormat::getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier)
{
    juce::PluginDescription description;

    if (MantaPlugins::findDescription (fileOrIdentifier, description))
        return description.name;

    return fileOrIdentifier;
}

bool MantaPluginFormat::doesPluginStillExist (const juce::PluginDescription& description)
{
    juce::PluginDescription found;

    return MantaPlugins::findDescription (description.fileOrIdentifier, found);
}

void MantaPluginFormat::createPluginInstance (const juce::PluginDescription& description,
                                               double initialSampleRate, int initialBufferSize,
                                               PluginCreationCallback callback)
{
    for (const auto& entry : MantaPlugins::getEntries())
    {
        if (description.fileOrIdentifier != entry.identifier)
            continue;

        auto instance = entry.create();

        if (instance == nullptr)
            break;

        // **ここで`prepareToPlay()`まで済ませておくこと。**
        // 外のフォーマット（VST3等）は`createInstanceFromDescription()`の中で
        // 同じことをしており、`AudioEngine`はその前提で書かれています
        // （グラフへ入れるとき、もう一度`prepareToPlay()`が来ます）。
        //
        // 9.5：**渡された値を使うこと。** デバイスへ訊きに行くと、
        // 書き出し中（デバイスと違うレート。8.153）に食い違います
        const double rate  = initialSampleRate > 0.0 ? initialSampleRate : 44100.0;
        const int blockSize = initialBufferSize > 0 ? initialBufferSize : 512;

        instance->setPlayConfigDetails (description.numInputChannels,
                                         description.numOutputChannels,
                                         rate, blockSize);
        instance->prepareToPlay (rate, blockSize);

        callback (std::move (instance), {});
        return;
    }

    callback (nullptr, "Unknown Manta plugin: " + description.fileOrIdentifier);
}
