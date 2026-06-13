#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>

namespace yob {

// ============================================================================
// Linear Arena Allocator
// ============================================================================
// Design: Bump-pointer allocator with monotonic growth
// Use case: Allocate all orders at startup, never free individual items
//           (orders are recycled via object pool, not freed to OS)
//
// Key properties:
//   - O(1) allocation (bump pointer)
//   - No fragmentation (linear layout)
//   - All memory released at once on arena destruction
//   - Cache-friendly: objects allocated contiguously
// ============================================================================

class LinearArena {
public:
    explicit LinearArena(size_t initial_capacity = 64 * 1024 * 1024) // 64MB default
        : capacity_(initial_capacity)
        , offset_(0) {
        memory_ = static_cast<uint8_t*>(
            ::operator new[](capacity_, static_cast<std::align_val_t>(alignof(std::max_align_t)))
        );
    }

    ~LinearArena() {
        ::operator delete[](memory_, static_cast<std::align_val_t>(alignof(std::max_align_t)));
    }

    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    LinearArena(LinearArena&&) = delete;
    LinearArena& operator=(LinearArena&&) = delete;

    [[nodiscard]] void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) {
        // Align offset
        const size_t aligned_offset = (offset_ + alignment - 1) & ~(alignment - 1);

        if (aligned_offset + size > capacity_) {
            throw std::bad_alloc();
        }

        void* ptr = memory_ + aligned_offset;
        offset_ = aligned_offset + size;
        return ptr;
    }

    template<typename T, typename... Args>
    [[nodiscard]] T* construct(Args&&... args) {
        static_assert(std::is_trivially_destructible<T>::value || 
                      std::is_nothrow_destructible<T>::value,
                      "Arena only supports objects with noexcept destructors");

        void* mem = allocate(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset() noexcept {
        offset_ = 0;
    }

    [[nodiscard]] size_t used() const noexcept { return offset_; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_t available() const noexcept { return capacity_ - offset_; }

private:
    uint8_t* memory_;
    size_t capacity_;
    size_t offset_;
};

// ============================================================================
// Object Pool (Free List)
// ============================================================================
// Design: Pre-allocate N objects, maintain free list for O(1) acquire/release
// Use case: Orders are created/cancelled frequently; pool eliminates malloc
//
// Key properties:
//   - O(1) acquire and release
//   - No heap pressure during trading hours
//   - Predictable memory footprint
//   - Cache-friendly if pool size matches working set
//
// CRITICAL FIX: Storage must be at least as large as FreeNode to prevent
// buffer overflow when we reinterpret_cast released objects as free list nodes.
// ============================================================================

template<typename T, size_t PoolSize>
class ObjectPool {
    static_assert(std::is_trivially_destructible<T>::value || 
                  std::is_nothrow_destructible<T>::value,
                  "Pool only supports objects with noexcept destructors");

    struct FreeNode {
        FreeNode* next;
    };

    // Storage must be large enough for both T and FreeNode, and aligned for both
    struct alignas(alignof(std::max_align_t)) Storage {
        uint8_t data[sizeof(T) > sizeof(FreeNode) ? sizeof(T) : sizeof(FreeNode)];
    };

public:
    ObjectPool() {
        // Pre-allocate storage
        storage_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            storage_.emplace_back();
        }

        // Initialize free list (LIFO for cache locality)
        free_list_ = nullptr;
        for (size_t i = PoolSize; i > 0; --i) {
            FreeNode* node = reinterpret_cast<FreeNode*>(&storage_[i - 1]);
            node->next = free_list_;
            free_list_ = node;
        }
        available_ = PoolSize;
    }

    ~ObjectPool() {
        // Note: does NOT call destructors on active objects
        // In production, track active objects and call dtors
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    template<typename... Args>
    [[nodiscard]] T* acquire(Args&&... args) {
        if (free_list_ == nullptr) {
            return nullptr; // Pool exhausted
        }

        FreeNode* node = free_list_;
        free_list_ = node->next;
        --available_;

        T* obj = reinterpret_cast<T*>(node);
        new (obj) T(std::forward<Args>(args)...);
        return obj;
    }

    void release(T* obj) noexcept {
        if (obj == nullptr) return;

        obj->~T();
        FreeNode* node = reinterpret_cast<FreeNode*>(obj);
        node->next = free_list_;
        free_list_ = node;
        ++available_;
    }

    [[nodiscard]] size_t available() const noexcept { return available_; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return PoolSize; }

private:
    std::vector<Storage> storage_;
    FreeNode* free_list_;
    size_t available_;
};

} // namespace yob