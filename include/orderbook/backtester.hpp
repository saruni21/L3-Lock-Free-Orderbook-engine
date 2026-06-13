#pragma once
#include "orderbook/orderbook.hpp"
#include "orderbook/market_data.hpp"
#include <vector>
#include <string>
#include <fstream>

namespace yob {

struct BacktestConfig {
    double commission_per_share = 0.005;
    double market_impact_bps = 1.0;
    Price tick_size = 1;
    Quantity max_position = 10000;
    Quantity order_size = 100;
};

struct BacktestResult {
    double total_pnl = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double sharpe_ratio = 0.0;
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;
    double volatility = 0.0;
    size_t total_trades = 0;
    size_t winning_trades = 0;
    size_t losing_trades = 0;
    double win_rate = 0.0;
    double avg_trade_pnl = 0.0;
    double avg_win = 0.0;
    double avg_loss = 0.0;
    double profit_factor = 0.0;
    double avg_latency_ns = 0.0;
    double p50_latency_ns = 0.0;
    double p99_latency_ns = 0.0;
    double p999_latency_ns = 0.0;
    double max_latency_ns = 0.0;
    double orders_per_second = 0.0;
    double trades_per_second = 0.0;

    void print() const;
    void to_csv(const std::string& filepath) const;
};

struct Position {
    int64_t qty = 0;        // Positive = long, Negative = short
    Price avg_entry_price = 0;
    double realized_pnl = 0.0;

    void add_fill(Side side, Price price, Quantity qty, double commission) noexcept;
    [[nodiscard]] double unrealized_pnl(Price current_price) const noexcept;
    [[nodiscard]] double market_value(Price current_price) const noexcept;
};

class Backtester {
public:
    explicit Backtester(const BacktestConfig& cfg = BacktestConfig{});

    [[nodiscard]] BacktestResult run_ema_strategy(
        MarketDataSimulator& simulator,
        size_t num_messages,
        const EMASignalEngine::Config& signal_cfg = EMASignalEngine::Config{});

    [[nodiscard]] BacktestResult run_replay(
        const std::vector<MarketDataMessage>& messages,
        const EMASignalEngine::Config& signal_cfg = EMASignalEngine::Config{});

    [[nodiscard]] BacktestResult benchmark_latency(
        MarketDataSimulator& simulator,
        size_t num_messages,
        size_t warmup_messages = 10000);

private:
    BacktestConfig cfg_;

    void record_latency(Timestamp start_ns, Timestamp end_ns);
    void update_drawdown(double equity);

    std::vector<Timestamp> latencies_;
    std::vector<double> equity_curve_;
    double peak_equity_ = 0.0;
    double current_drawdown_ = 0.0;
};

} // namespace yob
