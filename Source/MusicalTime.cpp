#include "MusicalTime.h"

#include "ProjectIds.h"

#include <vector>

namespace MusicalTime
{

namespace
{
    struct Entry
    {
        TempoMapSource* source = nullptr;
        juce::ValueTree root;
    };

    // **数は1つか2つです。** 配列を線形に見るのがいちばん速く、いちばん短い。
    // 増えたとしても、プロジェクトを何十個も同時に開くことはありません
    std::vector<Entry>& getEntries()
    {
        static std::vector<Entry> entries;
        return entries;
    }

    juce::SpinLock& getLock()
    {
        static juce::SpinLock lock;
        return lock;
    }

    /** 登録が無いときに返す表。**変化点なしの120BPM・4/4**なので、
        Phase 139までと同じ答えになります（`TempoMap.h`の説明）。 */
    const TempoMap& getDefaultMap()
    {
        static const TempoMap fallback;
        return fallback;
    }
}

void registerSource (TempoMapSource* source)
{
    if (source == nullptr)
        return;

    const juce::SpinLock::ScopedLockType lock (getLock());

    auto& entries = getEntries();

    // **二重登録ではなく、根の更新として扱う**（`setState()`が根ごと差し替えるため）
    for (auto& entry : entries)
    {
        if (entry.source == source)
        {
            entry.root = source->getTempoMapRoot();
            return;
        }
    }

    entries.push_back ({ source, source->getTempoMapRoot() });
}

void unregisterSource (TempoMapSource* source)
{
    if (source == nullptr)
        return;

    const juce::SpinLock::ScopedLockType lock (getLock());

    auto& entries = getEntries();

    for (int i = (int) entries.size(); --i >= 0;)
        if (entries[(size_t) i].source == source)
            entries.erase (entries.begin() + i);
}

const TempoMap& findMapFor (const juce::ValueTree& node)
{
    if (! node.isValid())
        return getDefaultMap();

    // **根まで遡って、持ち主を突き止める。** 親を4つほど辿るだけです
    // （`<NOTE>` → `<NOTES>` → `<TRACK>` → `<TRACKS>` → `<PROJECT>`）
    const auto root = node.getRoot();

    const juce::SpinLock::ScopedLockType lock (getLock());

    for (const auto& entry : getEntries())
        if (entry.source != nullptr && entry.root == root)
            return entry.source->getTempoMap();

    // どのプロジェクトにも属していないノード（クリップボードへ取った複製など）。
    // **失敗にしないこと**——1.32のとおり、切り離されたツリーは普通に出てきます
    return getDefaultMap();
}

double getBeatsFor (const juce::ValueTree& node, double timeSeconds)
{
    return findMapFor (node).getBeatAtTime (timeSeconds);
}

double getSecondsFor (const juce::ValueTree& node, double beatPosition)
{
    return findMapFor (node).getTimeForBeat (beatPosition);
}

TempoMap buildTempoMapFrom (const juce::ValueTree& projectState)
{
    TempoMap map;

    if (! projectState.isValid())
        return map;

    // 曲の頭の値（0拍目／0小節目）はプロジェクトのプロパティが持つ。
    // **拍子は文字列1つ**なので、分子と分母をここで割る
    const juce::String signature = projectState.getProperty (IDs::timeSignature, "4/4").toString();
    const int numerator = signature.upToFirstOccurrenceOf ("/", false, false).getIntValue();
    const int denominator = signature.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

    map.initialTempo = projectState.getProperty (IDs::tempo, 120.0);
    map.initialBeatsPerBar = numerator > 0 ? numerator : 4;
    map.initialDenominator = denominator > 0 ? denominator : 4;

    auto mapNode = projectState.getChildWithName (IDs::TEMPOMAP);

    // **古いプロジェクトにはこのノードがありません。** 無ければ変化点ゼロ＝
    // Phase 139までと完全に同じ答えになるので、読み替えは要らない（8.102）
    if (mapNode.isValid())
    {
        for (int i = 0; i < mapNode.getNumChildren(); ++i)
        {
            auto child = mapNode.getChild (i);

            if (child.hasType (IDs::TEMPOCHANGE))
            {
                TempoMap::TempoChange change;
                change.beatPosition = child.getProperty (IDs::tempoChangeBeat, 0.0);
                change.bpm = child.getProperty (IDs::tempo, 120.0);
                map.tempoChanges.push_back (change);
            }
            else if (child.hasType (IDs::TIMESIGCHANGE))
            {
                // **分母も表へ入れること**（Phase 145）。分子だけ入れていたので、
                // "6/8"の札が"6/4"と出て、**ドラッグで確定した瞬間に本当に書き換わって**いた
                const juce::String changeSignature = child.getProperty (IDs::timeSignature, "4/4").toString();
                const int changeNumerator = changeSignature.upToFirstOccurrenceOf ("/", false, false).getIntValue();
                const int changeDenominator = changeSignature.fromLastOccurrenceOf ("/", false, false).trim().getIntValue();

                TempoMap::MeterChange change;
                change.bar = (int) child.getProperty (IDs::timeSigChangeBar, 0);
                change.beatsPerBar = changeNumerator > 0 ? changeNumerator : 4;
                change.denominator = changeDenominator > 0 ? changeDenominator : 4;
                map.meterChanges.push_back (change);
            }
        }
    }

    // **並べ替えを忘れないこと。** TempoMapのwalkは昇順を前提にしています
    map.sortAndDeduplicate();

    return map;
}

} // namespace MusicalTime
