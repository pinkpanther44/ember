#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

//==============================================================================
/**
    8.329：音のスレッドから画面のスレッドへ標本を渡す（アナライザー設計書4.1の`SampleFifo`）。

    **書き手1つ・読み手1つのロックフリーのリングバッファ**です。
    位置は`std::atomic<uint32_t>`で、acquire／releaseで受け渡します。

    ### Manta EQのアナライザーとの違い

    EQは「いちばん新しい4096個」だけを読むので、上書きし続ける円環で足りました（`EQAnalyserFifo`）。
    こちらは**時間平均と積算**をするので、**標本を欠かさず・順に**受け取る必要があります。
    上書きすると、同じ標本を2回数えたり、飛ばしたりします。

    ### 満杯のとき

    **書き手は待たずに、そのブロックを書かずに捨てて`overflow`を立てます**（設計書4.1）。
    読み手は次に`takeOverflow()`で気づいたら、**中に残っているものも捨てて**やり直します
    （途中が欠けた列を1本として解析しないため）。

    ### 確保は作るときだけ

    `push()`・`pop()`はメモリを確保しません（音のスレッドの決まり。仕様書4.4）。
*/
class AnalyzerFifo
{
public:
    /** 容量は2のべき乗へ切り上げます（位置の折り返しをマスクで済ませるため）。 */
    explicit AnalyzerFifo (int capacityFrames = 131072)
    {
        uint32_t capacity = 1;

        while (capacity < (uint32_t) capacityFrames)
            capacity <<= 1;

        mask = capacity - 1;
        left.assign (capacity, 0.0f);
        right.assign (capacity, 0.0f);
    }

    int getCapacity() const noexcept { return (int) (mask + 1); }

    /** **音のスレッドから。** `r`が`nullptr`なら`l`を両方へ（モノ入力。設計書4.1）。
        入りきらなければ、このブロックは書かずに`overflow`を立てます。 */
    void push (const float* l, const float* r, int n) noexcept
    {
        if (n <= 0)
            return;

        const uint32_t w = writePosition.load (std::memory_order_relaxed);
        const uint32_t readPos = readPosition.load (std::memory_order_acquire);
        const uint32_t used = w - readPos;

        if ((uint32_t) n > (mask + 1) - used)
        {
            overflowed.store (true, std::memory_order_release);
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            const uint32_t index = (w + (uint32_t) i) & mask;
            left[index] = l[i];
            right[index] = r != nullptr ? r[i] : l[i];
        }

        writePosition.store (w + (uint32_t) n, std::memory_order_release);
    }

    /** **画面のスレッドから。** 最大`maxN`個を取り出し、取り出した数を返す。 */
    int pop (float* l, float* r, int maxN) noexcept
    {
        const uint32_t readPos = readPosition.load (std::memory_order_relaxed);
        const uint32_t w = writePosition.load (std::memory_order_acquire);
        const int available = (int) (w - readPos);
        const int n = available < maxN ? available : maxN;

        for (int i = 0; i < n; ++i)
        {
            const uint32_t index = (readPos + (uint32_t) i) & mask;
            l[i] = left[index];
            r[i] = right[index];
        }

        readPosition.store (readPos + (uint32_t) n, std::memory_order_release);
        return n;
    }

    /** あふれていたら`true`を返して旗を下ろす（**画面のスレッドから**）。 */
    bool takeOverflow() noexcept
    {
        return overflowed.exchange (false, std::memory_order_acq_rel);
    }

    /** 残っているものを捨てる（**画面のスレッドから**。読み手が読んだことにするだけ）。 */
    void discardAll() noexcept
    {
        readPosition.store (writePosition.load (std::memory_order_acquire), std::memory_order_release);
    }

    int getNumReady() const noexcept
    {
        return (int) (writePosition.load (std::memory_order_acquire)
                        - readPosition.load (std::memory_order_acquire));
    }

private:
    std::vector<float> left, right;
    uint32_t mask = 0;

    std::atomic<uint32_t> writePosition { 0 };
    std::atomic<uint32_t> readPosition { 0 };
    std::atomic<bool> overflowed { false };
};
