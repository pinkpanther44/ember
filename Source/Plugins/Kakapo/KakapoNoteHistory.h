#pragma once

#include <array>
#include <cstdint>
#include <deque>

//==============================================================================
/**
    8.292：**Kakapo の音符の控え**（Phase 285／本人の仕様書5.1・設計書14章）。

    直近のノートを持ち、**12音の使用量（ヒストグラム）**を作ります。
    こちらもJUCEに依存させていません。

    ### 間引き方は2つ（本人の仕様）

    | | |
    |---|---|
    | `Mode::notes` | **直近N音**だけ残す |
    | `Mode::seconds` | **直近N秒**に鳴り始めた音だけ残す |

    どちらも`prune()`で切ります。**時間はサンプル数で数えます**
    ——秒で持つと、レートが変わったときに長さが変わります。

    ### 重みは「長さ」も見ます

    同じ音でも、**長く伸ばした音のほうが、その調らしさに効きます**。
    重みは`1 + 押されていた秒数`で、**押しっぱなしの音は上限で止めます**
    （伸ばしただけで調が決まってしまわないように。上限2秒ぶん＝重み3）。

    > **単純な出現回数でもよい**、と仕様書には書いてあります（5.2）。
    > 長さを見るほうを採ったのは、**16分の刻みと、伸ばした主音を
    > 同じ重みにしたくない**からです。

    ### 鳴っている音は消しません

    `prune()`は**まだ離していない音を残します**。押さえたままの和音が
    「古い」というだけで判定から外れると、**押さえているのに消える**という
    いちばん驚く形になります。
*/
namespace kakapo
{
    class NoteHistory
    {
    public:
        enum class Mode { notes, seconds };

        struct Entry
        {
            int midiNote = 0;
            int pitchClass = 0;
            std::int64_t onSample = 0;
            std::int64_t offSample = -1;   ///< **-1なら、まだ鳴っています**
        };

        void prepare (double newSampleRate)
        {
            sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
            clear();
        }

        void clear()
        {
            entries.clear();
            activeCount.fill (0);
            dirty = true;
        }

        void noteOn (int midiNote, std::int64_t sample)
        {
            if (midiNote < 0 || midiNote > 127)
                return;

            Entry entry;
            entry.midiNote = midiNote;
            entry.pitchClass = midiNote % 12;
            entry.onSample = sample;

            entries.push_back (entry);

            ++activeCount[(size_t) midiNote];
            dirty = true;
        }

        void noteOff (int midiNote, std::int64_t sample)
        {
            if (midiNote < 0 || midiNote > 127)
                return;

            if (activeCount[(size_t) midiNote] > 0)
                --activeCount[(size_t) midiNote];

            // **いちばん新しい「鳴りっぱなし」を閉じます**（同じ音を重ねて押したとき、
            // 古いほうから閉じると、後から押したぶんが永遠に残ります）
            for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
            {
                if (entry->midiNote == midiNote && entry->offSample < 0)
                {
                    entry->offSample = sample;
                    dirty = true;
                    return;
                }
            }
        }

        /** 古い音を捨てる。**鳴っている音は残します**（クラスの説明）。 */
        void prune (Mode mode, double holdLength, std::int64_t nowSample)
        {
            if (mode == Mode::notes)
            {
                const size_t wanted = (size_t) (holdLength < 1.0 ? 1.0 : holdLength);

                while (entries.size() > wanted)
                {
                    // **鳴っている音は捨てない。** いちばん古いものが鳴っていたら、
                    // そこで止めます（前へ進めても、その先はもっと新しいだけ）
                    if (entries.front().offSample < 0)
                        break;

                    entries.pop_front();
                    dirty = true;
                }
            }
            else
            {
                const auto window = (std::int64_t) (holdLength * sampleRate);

                while (! entries.empty())
                {
                    const auto& oldest = entries.front();

                    if (oldest.offSample < 0 || nowSample - oldest.onSample <= window)
                        break;

                    entries.pop_front();
                    dirty = true;
                }
            }
        }

        /** 12音の使用量。**変わっていなければ作り直しません**。 */
        const std::array<float, 12>& getHistogram (std::int64_t nowSample)
        {
            if (dirty || nowSample != lastHistogramSample)
                rebuildHistogram (nowSample);

            return histogram;
        }

        /** 画面の鍵盤用の2枚（**いま鳴っている音**と、**控えに残っている音**）。

            **1音ずつ訊かないこと。** 128回×控えの数だけ見比べることになります
            ——ここは音のスレッドから毎ブロック呼ばれます。 */
        void fillNoteMasks (std::array<bool, 128>& active, std::array<bool, 128>& recent) const
        {
            for (int note = 0; note < 128; ++note)
                active[(size_t) note] = activeCount[(size_t) note] > 0;

            recent.fill (false);

            for (const auto& entry : entries)
                recent[(size_t) entry.midiNote] = true;
        }

        /** いま鳴っている音（1つだけ訊きたいとき）。 */
        bool isNoteActive (int midiNote) const
        {
            return midiNote >= 0 && midiNote < 128 && activeCount[(size_t) midiNote] > 0;
        }

        int getNoteCount() const { return (int) entries.size(); }

    private:
        void rebuildHistogram (std::int64_t nowSample)
        {
            histogram.fill (0.0f);

            for (const auto& entry : entries)
            {
                const auto end = entry.offSample >= 0 ? entry.offSample : nowSample;
                const auto heldSamples = end > entry.onSample ? end - entry.onSample : 0;

                const float heldSeconds = (float) ((double) heldSamples / sampleRate);

                // **押しっぱなしで重みが伸び続けないように、上限で止めます**（クラスの説明）
                const float capped = heldSeconds > 2.0f ? 2.0f : heldSeconds;

                histogram[(size_t) entry.pitchClass] += 1.0f + capped;
            }

            dirty = false;
            lastHistogramSample = nowSample;
        }

        std::deque<Entry> entries;
        std::array<int, 128> activeCount {};
        std::array<float, 12> histogram {};

        double sampleRate = 48000.0;
        std::int64_t lastHistogramSample = -1;
        bool dirty = true;
    };
}
