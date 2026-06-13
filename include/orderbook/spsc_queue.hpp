#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <cassert>

namespace yob {

// ============================================================================
// Lock-Free SPSC Ring Buffer
// ============================================================================
// Design: Classic Lamport-style SPSC queue with power-of-2 capacity
// Key invariants:
//   - Producer only writes to tail, consumer only reads from head
//   - Full condition: (tail + 1) % capacity == head
//   - Empty condition: tail == head
//   - Capacity must be power of 2 for bitmask indexing
//
// Cache line separation: head/tail on separate cache lines to prevent
// false sharing between producer and consumer threads.
//
// ABA Safety: Single producer + single consumer eliminates ABA by design
// (no concurrent pop/push on same end). For MPMC, would need tagged pointers.
// ============================================================================

template<typename T, size_t Capacity>
class alignas(128) SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, 
                  "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable<T>::value, 
                  "T must be trivially copyable for lock-free safety");

public:
    static constexpr size_t MASK = Capacity - 1;

    SPSCQueue() = default;
    ~SPSCQueue() = default;

    // Non-copyable, non-movable (pinned memory location matters for cache)
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // Producer API (called from single thread)
    [[nodiscard]] bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & MASK;

        // Check full (consumer's head might be lagging)
        if (next_tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (next_tail == head_cache_) {
                return false; // Queue full
            }
        }

        // Write item, then publish with release
        buffer_[current_tail & MASK] = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Consumer API (called from single thread)
    [[nodiscard]] bool pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check empty (producer's tail might be lagging)
        if (current_head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (current_head == tail_cache_) {
                return false; // Queue empty
            }
        }

        // Read item, then advance head with release
        item = buffer_[current_head & MASK];
        head_.store((current_head + 1) & MASK, std::memory_order_release);
        return true;
    }

    // Bulk operations for amortizing atomic overhead
    template<size_t BatchSize>
    [[nodiscard]] size_t push_bulk(const T* items, size_t count) noexcept {
        size_t pushed = 0;
        while (pushed < count && pushed < BatchSize) {
            if (!push(items[pushed])) break;
            ++pushed;
        }
        return pushed;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] size_t size_approx() const noexcept {
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        return (t - h) & MASK;
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept { return Capacity; }

private:
    // Producer cache line (64 bytes)
    alignas(64) std::atomic<size_t> tail_{0};
    size_t head_cache_ = 0;  // Cached copy of head (consumer's position)

    // Consumer cache line (64 bytes) - separate cache line prevents false sharing
    alignas(64) std::atomic<size_t> head_{0};
    size_t tail_cache_ = 0;  // Cached copy of tail (producer's position)

    // Data buffer (separate cache lines from control variables)
    alignas(64) T buffer_[Capacity];
};

// Verify cache line separation
static_assert(sizeof(SPSCQueue<int, 1024>) >= 192, 
              "SPSCQueue should span multiple cache lines for separation");

} // namespace yob