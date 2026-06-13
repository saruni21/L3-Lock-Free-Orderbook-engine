#include "orderbook/market_data.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace yob {

// ============================================================================
// Market Data Simulator Implementation
// ============================================================================

MarketDataSimulator::MarketDataSimulator(const Config& cfg)
    : cfg_(cfg)
    , rng_(cfg.seed)
    , price_dist_(0.0, cfg.volatility)
    , arrival_dist_(cfg.arrival_rate)
    , size_dist_(1, cfg.max_order_size)
    , type_dist_(0.0, 1.0)
    , current_price_(cfg.start_price)
    , next_timestamp_(0)
{
}

MarketDataMessage MarketDataSimulator::next_message() {
    MarketDataMessage msg{};
    msg.timestamp_ns = next_timestamp_;
    next_timestamp_ += static_cast<Timestamp>(arrival_dist_(rng_) * 1e9 / cfg_.arrival_rate);

    msg.order_id = next_order_id_++;
    msg.side = random_side();

    // Price: random walk around current price
    double delta = price_dist_(rng_);
    current_price_ = static_cast<Price>(current_price_ + static_cast<Price>(delta));
    if (current_price_ < 1) current_price_ = 1;
    msg.price = current_price_;

    msg.quantity = size_dist_(rng_);
    msg.type = random_order_type();

    return msg;
}

void MarketDataSimulator::generate_batch(MarketDataMessage* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        out[i] = next_message();
    }
}

bool MarketDataSimulator::load_csv(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        MarketDataMessage msg{};
        std::string token;

        // timestamp,order_id,side,price,qty,type
        std::getline(ss, token, ','); msg.timestamp_ns = std::stoull(token);
        std::getline(ss, token, ','); msg.order_id = std::stoull(token);
        std::getline(ss, token, ','); msg.side = (token == "BUY" || token == "0") ? Side::Buy : Side::Sell;
        std::getline(ss, token, ','); msg.price = std::stoll(token);
        std::getline(ss, token, ','); msg.quantity = std::stoul(token);
        std::getline(ss, token, ','); 
        if (token == "LIMIT" || token == "0") msg.type = OrderType::Limit;
        else if (token == "MARKET" || token == "1") msg.type = OrderType::Market;
        else if (token == "CANCEL" || token == "2") msg.type = OrderType::Cancel;
        else msg.type = OrderType::Modify;

        replay_buffer_.push_back(msg);
    }

    return true;
}

MarketDataMessage MarketDataSimulator::next_replay_message() {
    if (replay_index_ < replay_buffer_.size()) {
        return replay_buffer_[replay_index_++];
    }
    return MarketDataMessage{};
}

OrderType MarketDataSimulator::random_order_type() noexcept {
    double r = type_dist_(rng_);
    if (r < cfg_.cancel_rate) return OrderType::Cancel;
    if (r < cfg_.cancel_rate + cfg_.modify_rate) return OrderType::Modify;
    return OrderType::Limit;
}

Side MarketDataSimulator::random_side() noexcept {
    return type_dist_(rng_) < 0.5 ? Side::Buy : Side::Sell;
}

// ============================================================================
// EMA Signal Engine Implementation
// ============================================================================

EMASignalEngine::EMASignalEngine(const Config& cfg)
    : cfg_(cfg)
    , fast_ema_(0)
    , slow_ema_(0)
    , last_signal_(Signal::Hold)
    , initialized_(false)
    , fast_alpha_(2.0 / (cfg.fast_period + 1.0))
    , slow_alpha_(2.0 / (cfg.slow_period + 1.0))
{
}

EMASignalEngine::Signal EMASignalEngine::on_price(Price mid_price) {
    if (!initialized_) {
        fast_ema_ = mid_price;
        slow_ema_ = mid_price;
        initialized_ = true;
        return Signal::Hold;
    }

    // Online EMA update: EMA_t = alpha * price + (1 - alpha) * EMA_{t-1}
    fast_ema_ = static_cast<Price>(fast_alpha_ * mid_price + (1.0 - fast_alpha_) * fast_ema_);
    slow_ema_ = static_cast<Price>(slow_alpha_ * mid_price + (1.0 - slow_alpha_) * slow_ema_);

    // Crossover detection with threshold
    Price diff = fast_ema_ - slow_ema_;
    Signal new_signal = Signal::Hold;

    if (diff > cfg_.threshold && last_signal_ != Signal::Buy) {
        new_signal = Signal::Buy;
    } else if (diff < -cfg_.threshold && last_signal_ != Signal::Sell) {
        new_signal = Signal::Sell;
    }

    if (new_signal != Signal::Hold) {
        last_signal_ = new_signal;
    }

    return new_signal;
}

void EMASignalEngine::reset() noexcept {
    fast_ema_ = 0;
    slow_ema_ = 0;
    last_signal_ = Signal::Hold;
    initialized_ = false;
}

// ============================================================================
// VWAP Signal Engine Implementation
// ============================================================================

void VWAPSignalEngine::on_trade(Price price, Quantity qty) noexcept {
    cumulative_pv_ += static_cast<__int128>(price) * qty;
    cumulative_qty_ += qty;
    total_qty_ += qty;
}

Price VWAPSignalEngine::vwap() const noexcept {
    if (cumulative_qty_ == 0) return 0;
    return static_cast<Price>(cumulative_pv_ / cumulative_qty_);
}

void VWAPSignalEngine::reset() noexcept {
    cumulative_pv_ = 0;
    cumulative_qty_ = 0;
    total_qty_ = 0;
}

} // namespace yob