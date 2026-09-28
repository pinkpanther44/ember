#include "AnalyzerSettings.h"

#include <cmath>

const juce::Identifier AnalyzerSettings::treeType { "MantaAnalyzer" };

namespace
{
    // **キーの名前は保存の鍵**です。変えると、保存済みのプロジェクトの設定が初期値に戻ります
    const juce::Identifier kSchema          { "schema" };
    const juce::Identifier kCurve           { "curve" };
    const juce::Identifier kPeak            { "peak" };
    const juce::Identifier kRange           { "range" };
    const juce::Identifier kLowPhon         { "lowPhon" };
    const juce::Identifier kHighPhon        { "highPhon" };
    const juce::Identifier kAlign           { "align" };
    const juce::Identifier kCalDbfs         { "calibrationDbfs" };
    const juce::Identifier kCalSpl          { "calibrationSpl" };
    const juce::Identifier kManualOffset    { "manualOffset" };
    const juce::Identifier kRangeOpacity    { "rangeOpacity" };
    const juce::Identifier kHighlight       { "highlightDeviation" };
    const juce::Identifier kRealtime        { "realtime" };
    const juce::Identifier kResponse        { "response" };
    const juce::Identifier kPeakHold        { "peakHold" };
    const juce::Identifier kPeakHoldForever { "peakHoldInfinite" };
    const juce::Identifier kSmoothing       { "smoothing" };
    const juce::Identifier kSlope           { "slope" };
    const juce::Identifier kFftSize         { "fftSize" };
    const juce::Identifier kWindow          { "window" };
    const juce::Identifier kOverlap         { "overlap" };
    const juce::Identifier kChannel         { "channel" };
    const juce::Identifier kFreqMin         { "frequencyMin" };
    const juce::Identifier kFreqMax         { "frequencyMax" };
    const juce::Identifier kLevelTop        { "levelTop" };
    const juce::Identifier kLevelRange      { "levelRange" };
    const juce::Identifier kGrid            { "grid" };

    /** いちばん近い選択肢へ寄せる。 */
    template <typename T>
    T nearestChoice (const juce::Array<T>& choices, T value)
    {
        T best = choices.getFirst();

        for (auto c : choices)
            if (std::abs ((double) c - (double) value) < std::abs ((double) best - (double) value))
                best = c;

        return best;
    }
}

float AnalyzerSettings::releaseSeconds() const
{
    switch (response)
    {
        case Response::fast:      return 0.3f;
        case Response::medium:    return 1.0f;
        case Response::slow:      return 3.0f;
        case Response::integrate: return std::numeric_limits<float>::infinity();
    }

    return 1.0f;
}

const juce::Array<int>& AnalyzerSettings::fftSizeChoices()
{
    static const juce::Array<int> choices { 2048, 4096, 8192, 16384, 32768 };
    return choices;
}

const juce::Array<float>& AnalyzerSettings::smoothingChoices()
{
    static const juce::Array<float> choices { 0.0f, 1.0f / 24.0f, 1.0f / 12.0f, 1.0f / 6.0f, 1.0f / 3.0f, 1.0f };
    return choices;
}

const juce::Array<float>& AnalyzerSettings::levelRangeChoices()
{
    static const juce::Array<float> choices { 48.0f, 60.0f, 80.0f, 100.0f };
    return choices;
}

void AnalyzerSettings::constrain()
{
    lowPhon = juce::jlimit (20.0f, 87.0f, std::round (lowPhon));
    highPhon = juce::jlimit (lowPhon + 3.0f, 90.0f, std::round (highPhon));

    calibrationDbfs = juce::jlimit (-60.0f, 0.0f, calibrationDbfs);
    calibrationSpl = juce::jlimit (40.0f, 120.0f, calibrationSpl);
    manualOffsetDb = juce::jlimit (-12.0f, 12.0f, manualOffsetDb);
    rangeOpacity = juce::jlimit (0.0f, 0.6f, rangeOpacity);

    peakHoldSeconds = juce::jlimit (0.5f, 10.0f, peakHoldSeconds);
    smoothingOctaves = nearestChoice (smoothingChoices(), smoothingOctaves);
    slopeDbPerOctave = juce::jlimit (0.0f, 6.0f, std::round (slopeDbPerOctave * 2.0f) / 2.0f);

    fftSize = nearestChoice (fftSizeChoices(), fftSize);
    overlap = overlap < 0.625f ? 0.5f : 0.75f;

    frequencyMin = juce::jlimit (10.0f, 100.0f, frequencyMin);
    frequencyMax = juce::jlimit (5000.0f, 22000.0f, frequencyMax);
    levelTopDb = juce::jlimit (-24.0f, 12.0f, levelTopDb);
    levelRangeDb = nearestChoice (levelRangeChoices(), levelRangeDb);
}

juce::ValueTree AnalyzerSettings::toTree() const
{
    juce::ValueTree tree (treeType);

    tree.setProperty (kSchema, schemaVersion, nullptr);
    tree.setProperty (kCurve, curveVisible, nullptr);
    tree.setProperty (kPeak, peakVisible, nullptr);
    tree.setProperty (kRange, rangeVisible, nullptr);
    tree.setProperty (kLowPhon, lowPhon, nullptr);
    tree.setProperty (kHighPhon, highPhon, nullptr);
    tree.setProperty (kAlign, (int) align, nullptr);
    tree.setProperty (kCalDbfs, calibrationDbfs, nullptr);
    tree.setProperty (kCalSpl, calibrationSpl, nullptr);
    tree.setProperty (kManualOffset, manualOffsetDb, nullptr);
    tree.setProperty (kRangeOpacity, rangeOpacity, nullptr);
    tree.setProperty (kHighlight, highlightDeviation, nullptr);
    tree.setProperty (kRealtime, realtimeVisible, nullptr);
    tree.setProperty (kResponse, (int) response, nullptr);
    tree.setProperty (kPeakHold, peakHoldSeconds, nullptr);
    tree.setProperty (kPeakHoldForever, peakHoldInfinite, nullptr);
    tree.setProperty (kSmoothing, smoothingOctaves, nullptr);
    tree.setProperty (kSlope, slopeDbPerOctave, nullptr);
    tree.setProperty (kFftSize, fftSize, nullptr);
    tree.setProperty (kWindow, (int) window, nullptr);
    tree.setProperty (kOverlap, overlap, nullptr);
    tree.setProperty (kChannel, (int) channel, nullptr);
    tree.setProperty (kFreqMin, frequencyMin, nullptr);
    tree.setProperty (kFreqMax, frequencyMax, nullptr);
    tree.setProperty (kLevelTop, levelTopDb, nullptr);
    tree.setProperty (kLevelRange, levelRangeDb, nullptr);
    tree.setProperty (kGrid, gridVisible, nullptr);

    return tree;
}

AnalyzerSettings AnalyzerSettings::fromTree (const juce::ValueTree& tree)
{
    AnalyzerSettings s;   // **欠けたキーは、ここの初期値のまま**

    if (! tree.hasType (treeType))
        return s;

    auto readBool = [&tree] (const juce::Identifier& key, bool& value)
    {
        if (tree.hasProperty (key))
            value = (bool) tree.getProperty (key);
    };

    auto readFloat = [&tree] (const juce::Identifier& key, float& value)
    {
        if (tree.hasProperty (key))
            value = (float) (double) tree.getProperty (key);
    };

    auto readInt = [&tree] (const juce::Identifier& key, int& value)
    {
        if (tree.hasProperty (key))
            value = (int) tree.getProperty (key);
    };

    // 列挙は**知らない番号なら初期値のまま**（将来の版が足した選択肢を読んだとき）
    auto readEnum = [&tree] (const juce::Identifier& key, auto& value, int count)
    {
        if (! tree.hasProperty (key))
            return;

        const int n = (int) tree.getProperty (key);

        if (juce::isPositiveAndBelow (n, count))
            value = static_cast<std::remove_reference_t<decltype (value)>> (n);
    };

    readBool (kCurve, s.curveVisible);
    readBool (kPeak, s.peakVisible);
    readBool (kRange, s.rangeVisible);
    readFloat (kLowPhon, s.lowPhon);
    readFloat (kHighPhon, s.highPhon);
    readEnum (kAlign, s.align, 3);
    readFloat (kCalDbfs, s.calibrationDbfs);
    readFloat (kCalSpl, s.calibrationSpl);
    readFloat (kManualOffset, s.manualOffsetDb);
    readFloat (kRangeOpacity, s.rangeOpacity);
    readBool (kHighlight, s.highlightDeviation);
    readBool (kRealtime, s.realtimeVisible);
    readEnum (kResponse, s.response, 4);
    readFloat (kPeakHold, s.peakHoldSeconds);
    readBool (kPeakHoldForever, s.peakHoldInfinite);
    readFloat (kSmoothing, s.smoothingOctaves);
    readFloat (kSlope, s.slopeDbPerOctave);
    readInt (kFftSize, s.fftSize);
    readEnum (kWindow, s.window, 2);
    readFloat (kOverlap, s.overlap);
    readEnum (kChannel, s.channel, 5);
    readFloat (kFreqMin, s.frequencyMin);
    readFloat (kFreqMax, s.frequencyMax);
    readFloat (kLevelTop, s.levelTopDb);
    readFloat (kLevelRange, s.levelRangeDb);
    readBool (kGrid, s.gridVisible);

    s.constrain();
    return s;
}
