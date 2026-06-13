#pragma once
#include <cstdint>
#include <string>
#include <chrono>
#include <limits>

namespace yob {

// ============================================================================
// Fundamental Types (cache-friendly, explicit sizes)
// ============================================================================

using Price = int64_t;      // Price in integer ticks (e.g., 100.50 -> 10050)
using Quantity = uint32_t;  // Order quantity
using OrderId = uint64_t;   // Unique order identifier
using Timestamp = uint64_t; // Nanoseconds since epoch (rdtsc or chrono)

// Price ticks: 1 tick = 0.01 for most equities
constexpr Price PRICE_TICK = 1;
constexpr Price INVALID_PRICE = std::numeric_limits<Price>::min();
constexpr OrderId INVALID_ORDER_ID = 0;

// ============================================================================
// Side Enum (1 byte, tightly packed)
// ============================================================================

enum class Side : uint8_t {
    Buy = 0,
    Sell = 1
};

inline constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// ============================================================================
// Order Type
// ============================================================================

enum class OrderType : uint8_t {
    Limit = 0,
    Market = 1,
    Cancel = 2,
    Modify = 3
};

// ============================================================================
// Market Data Message (SOA-friendly, 32 bytes, naturally aligned)
// ============================================================================

struct MarketDataMessage {
    Timestamp timestamp_ns;  // 8 bytes - arrival time
    OrderId order_id;        // 8 bytes
    Price price;             // 8 bytes
    Quantity quantity;       // 4 bytes
    Side side;               // 1 byte
    OrderType type;          // 1 byte
    uint16_t padding;        // 2 bytes - explicit padding

    // Total: 32 bytes

    bool is_valid() const noexcept {
        return order_id != INVALID_ORDER_ID && quantity > 0;
    }
};
static_assert(sizeof(MarketDataMessage) == 32, "MarketDataMessage must be 32 bytes");

// ============================================================================
// Trade Report
// ============================================================================

struct TradeReport {
    Timestamp timestamp_ns;
    Price price;
    Quantity quantity;
    Side aggressor_side;  // Which side initiated the trade
    uint8_t padding[7];
};
static_assert(sizeof(TradeReport) == 32, "TradeReport must be 32 bytes");

// ============================================================================
// High-resolution timing utilities
// ============================================================================

inline Timestamp rdtsc() noexcept {
#if defined(__x86_64__)
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#else
    // Fallback for non-x86
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
#endif
}

inline double tsc_to_ns(Timestamp tsc_delta, double tsc_freq_ghz) noexcept {
    return static_cast<double>(tsc_delta) / tsc_freq_ghz;
}

// TSC calibration (call once at startup)
inline double calibrate_tsc_freq_ghz() {
    auto start = std::chrono::steady_clock::now();
    Timestamp tsc_start = rdtsc();

    // Spin for ~100ms
    int dummy = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100)) {
        dummy++;
        (void)dummy; // Prevent unused warning
    }

    Timestamp tsc_end = rdtsc();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();

    return static_cast<double>(tsc_end - tsc_start) / static_cast<double>(elapsed);
}

} // namespace yob
