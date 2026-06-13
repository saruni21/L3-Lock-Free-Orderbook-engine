#pragma once
#include "orderbook/types.hpp"
#include "orderbook/spsc_queue.hpp"
#include <random>
#include <thread>
#include <atomic>
#include <vector>
#include <cmath>

namespace yob {

class MarketDataSimulator {
public:
    struct Config {
        Price start_price;       // Starting price in ticks ($100.00)
        double volatility;        // Tick volatility per step
        double arrival_rate;      // Orders per second
        double cancel_rate;       // Fraction of events that are cancels
        double modify_rate;       // Fraction of events that are modifies
        Quantity max_order_size;
        uint64_t seed;
    };

    explicit MarketDataSimulator(const Config& cfg);

    [[nodiscard]] MarketDataMessage next_message();
    void generate_batch(MarketDataMessage* out, size_t count);

    [[nodiscard]] bool load_csv(const std::string& filepath);
    [[nodiscard]] bool has_replay_data() const noexcept { return !replay_buffer_.empty(); }
    [[nodiscard]] MarketDataMessage next_replay_message();

    [[nodiscard]] Price current_price() const noexcept { return current_price_; }

private:
    Config cfg_;
    std::mt19937_64 rng_;
    std::normal_distribution<double> price_dist_;
    std::exponential_distribution<double> arrival_dist_;
    std::uniform_int_distribution<Quantity> size_dist_;
    std::uniform_real_distribution<double> type_dist_;

    Price current_price_;
    OrderId next_order_id_ = 1;
    Timestamp next_timestamp_ = 0;

    std::vector<MarketDataMessage> replay_buffer_;
    size_t replay_index_ = 0;

    [[nodiscard]] OrderType random_order_type() noexcept;
    [[nodiscard]] Side random_side() noexcept;
};

class EMASignalEngine {
public:
    struct Config {
        size_t fast_period = 12;   // Fast EMA period (e.g., 12 ticks)
        size_t slow_period = 26;   // Slow EMA period (e.g., 26 ticks)
        double threshold = 0.0;     // Minimum spread to trigger signal (in ticks)
    };

    enum class Signal : int8_t {
        Hold = 0,
        Buy = 1,
        Sell = -1
    };

    explicit EMASignalEngine(const Config& cfg);

    [[nodiscard]] Signal on_price(Price mid_price);

    [[nodiscard]] Price fast_ema() const noexcept { return fast_ema_; }
    [[nodiscard]] Price slow_ema() const noexcept { return slow_ema_; }
    [[nodiscard]] Signal last_signal() const noexcept { return last_signal_; }

    void reset() noexcept;

private:
    Config cfg_;
    Price fast_ema_ = 0;
    Price slow_ema_ = 0;
    Signal last_signal_ = Signal::Hold;
    bool initialized_ = false;

    double fast_alpha_;
    double slow_alpha_;
};

class VWAPSignalEngine {
public:
    void on_trade(Price price, Quantity qty) noexcept;

    [[nodiscard]] Price vwap() const noexcept;
    [[nodiscard]] Quantity total_volume() const noexcept { return total_qty_; }

    void reset() noexcept;

private:
    __int128 cumulative_pv_ = 0;
    __int128 cumulative_qty_ = 0;
    Quantity total_qty_ = 0;
};

} // namespace yob