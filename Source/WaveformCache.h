#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

//==============================================================================
/**
    波形描画用の共有キャッシュ（AudioThumbnailCache）とAudioFormatManagerをまとめたクラス。

    メンバーの宣言順が重要：AudioFormatManager → AudioThumbnailCache → 各AudioThumbnailの
    順で構築・破棄されないと、依存関係が壊れた状態でアクセスしてしまう
    （JUCE公式チュートリアル「Draw audio waveforms」で明記されている注意点）。
*/
class WaveformCache
{
public:
    WaveformCache();

    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    /** 指定したファイルの波形サムネイルを取得する（無ければ新規作成）。
        listenerToAddOnCreation を渡すと、新規作成時のみそのリスナーを登録する
        （波形データが後から届いたときに再描画をトリガーするため）。 */
    juce::AudioThumbnail& getThumbnail (const juce::String& filePath,
                                         juce::ChangeListener* listenerToAddOnCreation = nullptr);

private:
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 32 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformCache)
};
