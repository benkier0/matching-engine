#pragma once
#include <atomic>
#include <array>
#include <cstddef>

// Lock-free SPSC ring buffer. Capacity must be a power of two.
//
// write_ and read_ live on separate cache lines — without the padding both
// threads would hammer the same line on every push/pop (false sharing).
//
// Memory ordering is the minimal acquire/release pair from Vyukov's SPSC
// design: the producer does a relaxed load of its own index and an acquire
// load of the consumer's, then a release store after writing the slot.
// Consumer is symmetric. seq_cst is overkill here and adds fence instructions
// on ARM for no benefit.

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity >= 2,                       "Capacity must be >= 2");
    static_assert((Capacity & (Capacity - 1)) == 0,   "Capacity must be a power of two");

    static constexpr size_t kMask = Capacity - 1;

    // Pad each index to its own cache line to prevent false sharing.
    struct alignas(64) CacheLineIndex {
        std::atomic<size_t> value{0};
        char _pad[64 - sizeof(std::atomic<size_t>)];
    };

    CacheLineIndex        write_{};
    CacheLineIndex        read_{};
    std::array<T, Capacity> buffer_{};

public:
    SPSCQueue()  = default;
    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Called only from the producer thread.
    // Returns false if the queue is full; the item is not moved in that case.
    [[nodiscard]] bool try_push(const T& item) noexcept {
        const size_t w    = write_.value.load(std::memory_order_relaxed);
        const size_t next = (w + 1) & kMask;
        if (next == read_.value.load(std::memory_order_acquire))
            return false; // full
        buffer_[w] = item;
        write_.value.store(next, std::memory_order_release);
        return true;
    }

    // Called only from the consumer thread.
    // Returns false if the queue is empty.
    [[nodiscard]] bool try_pop(T& out) noexcept {
        const size_t r = read_.value.load(std::memory_order_relaxed);
        if (r == write_.value.load(std::memory_order_acquire))
            return false; // empty
        out = buffer_[r];
        read_.value.store((r + 1) & kMask, std::memory_order_release);
        return true;
    }

    // May be called from either thread; result is approximate if called
    // concurrently with pushes or pops.
    bool empty() const noexcept {
        return read_.value.load(std::memory_order_acquire) ==
               write_.value.load(std::memory_order_acquire);
    }

    constexpr size_t capacity() const noexcept { return Capacity; }
};
