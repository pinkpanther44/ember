#include "DragAndDropIds.h"

namespace DragAndDropIds
{
    // 接頭辞で種別を見分ける。プラグインの識別子やファイルパスに現れない形にしてある
    static const juce::String pluginPrefix { "pdaw-plugin:" };
    static const juce::String filePrefix   { "pdaw-file:" };

    juce::String makePluginDescription (const juce::PluginDescription& description)
    {
        return pluginPrefix + description.createIdentifierString();
    }

    bool isPluginDrag (const juce::var& description)
    {
        return description.toString().startsWith (pluginPrefix);
    }

    juce::String getPluginIdentifier (const juce::var& description)
    {
        if (! isPluginDrag (description))
            return {};

        return description.toString().fromFirstOccurrenceOf (pluginPrefix, false, false);
    }

    bool findPluginByIdentifier (const juce::Array<juce::PluginDescription>& knownPlugins,
                                  const juce::String& identifier,
                                  juce::PluginDescription& descriptionOut)
    {
        if (identifier.isEmpty())
            return false;

        for (const auto& candidate : knownPlugins)
        {
            if (candidate.createIdentifierString() == identifier)
            {
                descriptionOut = candidate;
                return true;
            }
        }

        return false;
    }

    juce::String makeFileDescription (const juce::File& file)
    {
        return filePrefix + file.getFullPathName();
    }

    bool isFileDrag (const juce::var& description)
    {
        return description.toString().startsWith (filePrefix);
    }

    juce::File getFile (const juce::var& description)
    {
        if (! isFileDrag (description))
            return {};

        const auto path = description.toString().fromFirstOccurrenceOf (filePrefix, false, false);

        return path.isNotEmpty() ? juce::File (path) : juce::File();
    }

    //==========================================================================
    // 8.66：ラックのスロット（Phase 104・105）

    // 区切りは`|`。トラックIDはUuidの文字列なので`|`は現れない。
    // **番号を後ろに置く**ことで、`fromLastOccurrenceOf`で確実に切り出せる
    static const juce::String insertPrefix { "pdaw-insert:" };
    static const juce::String sendPrefix   { "pdaw-send:" };

    // インサートとセンドで**同じ組み立て・切り出しを2つ書かない**（8.2）。
    // 違うのは接頭辞だけなので、そこだけを引数にしてある
    static juce::String makeSlotDescription (const juce::String& prefix,
                                              const juce::String& trackId, int slotIndex)
    {
        return prefix + trackId + "|" + juce::String (slotIndex);
    }

    static juce::String getSlotTrackId (const juce::String& prefix, const juce::var& description)
    {
        if (! description.toString().startsWith (prefix))
            return {};

        return description.toString().fromFirstOccurrenceOf (prefix, false, false)
                                     .upToLastOccurrenceOf ("|", false, false);
    }

    static int getSlotIndex (const juce::String& prefix, const juce::var& description)
    {
        if (! description.toString().startsWith (prefix))
            return -1;

        const auto tail = description.toString().fromLastOccurrenceOf ("|", false, false);

        // 空文字を`getIntValue()`に通すと0になってしまう。
        // 「1番目のスロット」と区別が付かないので、形が違うものは-1で弾く
        return (tail.isNotEmpty() && tail.containsOnly ("0123456789")) ? tail.getIntValue() : -1;
    }

    juce::String makeInsertDescription (const juce::String& trackId, int insertIndex)
    {
        return makeSlotDescription (insertPrefix, trackId, insertIndex);
    }

    bool isInsertDrag (const juce::var& description)
    {
        return description.toString().startsWith (insertPrefix);
    }

    juce::String getInsertSourceTrackId (const juce::var& description)
    {
        return getSlotTrackId (insertPrefix, description);
    }

    int getInsertIndex (const juce::var& description)
    {
        return getSlotIndex (insertPrefix, description);
    }

    juce::String makeSendDescription (const juce::String& trackId, int sendIndex)
    {
        return makeSlotDescription (sendPrefix, trackId, sendIndex);
    }

    bool isSendDrag (const juce::var& description)
    {
        return description.toString().startsWith (sendPrefix);
    }

    juce::String getSendSourceTrackId (const juce::var& description)
    {
        return getSlotTrackId (sendPrefix, description);
    }

    int getSendIndex (const juce::var& description)
    {
        return getSlotIndex (sendPrefix, description);
    }

    //==========================================================================
    // 8.67：Consoleのストリップの並べ替え（Phase 106）

    static const juce::String trackPrefix { "pdaw-track:" };

    juce::String makeTrackDescription (const juce::String& trackId)
    {
        return trackPrefix + trackId;
    }

    bool isTrackDrag (const juce::var& description)
    {
        return description.toString().startsWith (trackPrefix);
    }

    juce::String getTrackId (const juce::var& description)
    {
        if (! isTrackDrag (description))
            return {};

        return description.toString().fromFirstOccurrenceOf (trackPrefix, false, false);
    }
}
