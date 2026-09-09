#include "HitPointDetector.h"

// 8.180：既定パラメータ版（Phase 220）。中身は本体に丸投げするだけ
juce::Array<double> HitPointDetector::detect (const juce::File& file,
                                               juce::AudioFormatManager& formatManager)
{
    return detect (file, formatManager, Parameters{});
}

juce::Array<double> HitPointDetector::detect (const juce::File& file,
                                               juce::AudioFormatManager& formatManager,
                                               const Parameters& params)
{
    juce::Array<double> hitPoints;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return hitPoints;

    const double sampleRate = reader->sampleRate;
    const int frameSize = juce::jmax (1, (int) (params.frameSizeSeconds * sampleRate));
    const int numChannels = (int) juce::jmax ((juce::uint32) 1, reader->numChannels);
    const juce::int64 totalSamples = reader->lengthInSamples;

    juce::AudioBuffer<float> frameBuffer (numChannels, frameSize);

    // 8.47：**まず全フレームの音量を測る**（Phase 87）。
    //
    // Phase 85までは「RMSが0.10増えたら立ち上がり」という**固定のしきい値**でした。
    // 打楽器のように立ち上がりの鋭い音なら拾えますが、**録音レベルが低い素材や、
    // 音が滑らかに変わる素材では1つも見つかりません**（押しても何も起きないので、
    // ボタンが壊れているように見えます）。
    //
    // 素材のいちばん大きいところを基準にして、**そこからの相対でしきい値を決めます。**
    std::vector<float> frameRms;
    frameRms.reserve ((size_t) juce::jmax ((juce::int64) 1, totalSamples / frameSize));

    for (juce::int64 pos = 0; pos + frameSize <= totalSamples; pos += frameSize)
    {
        reader->read (&frameBuffer, 0, frameSize, pos, true, true);

        // 全チャンネルの平均RMSを、このフレームの音量とする
        float rmsSum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            rmsSum += frameBuffer.getRMSLevel (ch, 0, frameSize);

        frameRms.push_back (rmsSum / (float) numChannels);
    }

    if (frameRms.empty())
        return hitPoints;

    const float peakRms = *std::max_element (frameRms.begin(), frameRms.end());

    if (peakRms <= 0.0f)
        return hitPoints;   // 無音のファイル

    // **絶対値の下限も残す。** 相対だけにすると、ほぼ無音のファイルで
    // ノイズの揺らぎを立ち上がりとして拾ってしまう
    const float thresholdIncrease = juce::jmax (params.minimumThresholdIncrease,
                                                 peakRms * params.relativeThresholdIncrease);
    const float minimumRms = juce::jmax (params.minimumRms, peakRms * params.relativeMinimumRms);

    float previousRms = 0.0f;
    double lastHitTime = -params.debounceSeconds; // 先頭でも検出できるよう負の値で初期化

    for (size_t i = 0; i < frameRms.size(); ++i)
    {
        const float rms = frameRms[i];
        const double timeSeconds = (double) ((juce::int64) i * frameSize) / sampleRate;

        const bool loudEnough = rms >= minimumRms;
        const bool risingEdge = (rms - previousRms) >= thresholdIncrease;
        const bool pastDebounce = (timeSeconds - lastHitTime) >= params.debounceSeconds;

        if (loudEnough && risingEdge && pastDebounce)
        {
            hitPoints.add (timeSeconds);
            lastHitTime = timeSeconds;
        }

        previousRms = rms;
    }

    return hitPoints;
}
