#include "WaveformCache.h"

WaveformCache::WaveformCache()
{
    formatManager.registerBasicFormats(); // WAV/AIFF等の基本フォーマットを登録
}

juce::AudioThumbnail& WaveformCache::getThumbnail (const juce::String& filePath,
                                                    juce::ChangeListener* listenerToAddOnCreation)
{
    auto it = thumbnails.find (filePath);
    if (it != thumbnails.end())
        return *it->second;

    auto thumb = std::make_unique<juce::AudioThumbnail> (512, formatManager, thumbnailCache);

    if (listenerToAddOnCreation != nullptr)
        thumb->addChangeListener (listenerToAddOnCreation);

    thumb->setSource (new juce::FileInputSource (juce::File (filePath)));

    auto& ref = *thumb;
    thumbnails[filePath] = std::move (thumb);
    return ref;
}
