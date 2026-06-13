#include "orderbook/backtester.hpp"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace yob {

void Position::add_fill(Side side, Price price, Quantity qty, double commission) noexcept {
    if (qty == 0) return;

    double fill_value = static_cast<double>(price) * qty;
    double fill_commission = commission * qty;

    if (side == Side::Buy) {
        if (this->qty >= 0) {
            double total_value = static_cast<double>(avg_entry_price) * this->qty + fill_value;
            this->qty += qty;
            avg_entry_price = static_cast<Price>(total_value / this->qty);
        } else {
            if (qty >= static_cast<Quantity>(-this->qty)) {
                double close_pnl = (avg_entry_price - price) * (-this->qty) - fill_commission;
                realized_pnl += close_pnl;
                this->qty += static_cast<int64_t>(qty);
                if (this->qty > 0) {
                    avg_entry_price = price;
                }
            } else {
                double close_pnl = (avg_entry_price - price) * qty - fill_commission;
                realized_pnl += close_pnl;
                this->qty += static_cast<int64_t>(qty);
            }
        }
    } else {
        if (this->qty <= 0) {
            double total_value = static_cast<double>(avg_entry_price) * (-this->qty) + fill_value;
            this->qty -= static_cast<int64_t>(qty);
            avg_entry_price = static_cast<Price>(total_value / (-this->qty));
        } else {
            if (qty >= static_cast<Quantity>(this->qty)) {
                double close_pnl = (price - avg_entry_price) * this->qty - fill_commission;
                realized_pnl += close_pnl;
                this->qty -= static_cast<int64_t>(qty);
                if (this->qty < 0) {
                    avg_entry_price = price;
                }
            } else {
                double close_pnl = (price - avg_entry_price) * qty - fill_commission;
                realized_pnl += close_pnl;
                this->qty -= static_cast<int64_t>(qty);
            }
        }
    }
}

double Position::unrealized_pnl(Price current_price) const noexcept {
    if (qty == 0) return 0.0;
    if (qty > 0) {
        return static_cast<double>(current_price - avg_entry_price) * qty;
    } else {
        return static_cast<double>(avg_entry_price - current_price) * (-qty);
    }
}

double Position::market_value(Price current_price) const noexcept {
    return static_cast<double>(current_price) * qty;
}

void BacktestResult::print() const {
    std::cout << "========== BACKTEST RESULTS ==========" << std::endl;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Total P&L:          $" << std::setw(12) << total_pnl << "" << std::endl;
    std::cout << "Realized P&L:       $" << std::setw(12) << realized_pnl << "" << std::endl;
    std::cout << "Unrealized P&L:     $" << std::setw(12) << unrealized_pnl << "" << std::endl;
    std::cout << "Risk Metrics:" << std::endl;
    std::cout << "Sharpe Ratio:       " << std::setw(12) << sharpe_ratio << "" << std::endl;
    std::cout << "Max Drawdown:       $" << std::setw(12) << max_drawdown << "" << std::endl;
    std::cout << "Max Drawdown %:     " << std::setw(11) << max_drawdown_pct * 100 << "%" << std::endl;
    std::cout << "Volatility:         " << std::setw(12) << volatility << "" << std::endl;
    std::cout << "Trade Statistics:" << std::endl;
    std::cout << "Total Trades:       " << std::setw(12) << total_trades << "" << std::endl;
    std::cout << "Win Rate:           " << std::setw(11) << win_rate * 100 << "%" << std::endl;
    std::cout << "Avg Trade P&L:      $" << std::setw(12) << avg_trade_pnl << "" << std::endl;
    std::cout << "Avg Win:            $" << std::setw(12) << avg_win << "" << std::endl;
    std::cout << "Avg Loss:           $" << std::setw(12) << avg_loss << "" << std::endl;
    std::cout << "Profit Factor:      " << std::setw(12) << profit_factor << "" << std::endl;
    std::cout << "Latency (nanoseconds):" << std::endl;
    std::cout << "Average:            " << std::setw(12) << avg_latency_ns << "" << std::endl;
    std::cout << "P50:                " << std::setw(12) << p50_latency_ns << "" << std::endl;
    std::cout << "P99:                " << std::setw(12) << p99_latency_ns << "" << std::endl;
    std::cout << "P99.9:              " << std::setw(12) << p999_latency_ns << "" << std::endl;
    std::cout << "Max:                " << std::setw(12) << max_latency_ns << "" << std::endl;
    std::cout << "Throughput:" << std::endl;
    std::cout << "Orders/sec:         " << std::setw(12) << orders_per_second << "" << std::endl;
    std::cout << "Trades/sec:         " << std::setw(12) << trades_per_second << "" << std::endl;
    std::cout << "=======================================" << std::endl;
}

void BacktestResult::to_csv(const std::string& filepath) const {
    std::ofstream file(filepath);
    file << "metric,value";
    file << "total_pnl," << total_pnl << "";
    file << "realized_pnl," << realized_pnl << "";
    file << "sharpe_ratio," << sharpe_ratio << "";
    file << "max_drawdown," << max_drawdown << "";
    file << "max_drawdown_pct," << max_drawdown_pct << "";
    file << "total_trades," << total_trades << "";
    file << "win_rate," << win_rate << "";
    file << "avg_latency_ns," << avg_latency_ns << "";
    file << "p99_latency_ns," << p99_latency_ns << "";
    file << "orders_per_second," << orders_per_second << "";
}

Backtester::Backtester(const BacktestConfig& cfg)
    : cfg_(cfg)
    , peak_equity_(0.0)
    , current_drawdown_(0.0)
{
    latencies_.reserve(1'000'000);
    equity_curve_.reserve(1'000'000);
}

BacktestResult Backtester::run_ema_strategy(
    MarketDataSimulator& simulator,
    size_t num_messages,
    const EMASignalEngine::Config& signal_cfg) {

    OrderBook book;
    EMASignalEngine signal_engine(signal_cfg);
    VWAPSignalEngine vwap_engine;
    Position position;

    BacktestResult result;
    std::vector<double> trade_pnls;
    trade_pnls.reserve(10000);

    Timestamp start_time = rdtsc();
    size_t trade_count = 0;

    for (size_t i = 0; i < num_messages; ++i) {
        auto msg = simulator.next_message();
        if (!msg.is_valid()) continue;

        Timestamp msg_start = rdtsc();
        std::vector<TradeReport> trades;

        switch (msg.type) {
            case OrderType::Limit: {
                auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity,
                               msg.timestamp_ns, trades);
                (void)added;
                break;
            }
            case OrderType::Cancel: {
                auto cancelled = book.cancel_order(msg.order_id);
                (void)cancelled;
                break;
            }
            case OrderType::Modify: {
                auto modified = book.modify_order(msg.order_id, msg.price, msg.quantity,
                                  msg.timestamp_ns, trades);
                (void)modified;
                break;
            }
            default:
                break;
        }

        for (const auto& trade : trades) {
            vwap_engine.on_trade(trade.price, trade.quantity);
            ++trade_count;
        }

        Price mid = book.mid_price();
        if (mid != INVALID_PRICE) {
            auto signal = signal_engine.on_price(mid);

            if (signal == EMASignalEngine::Signal::Buy &&
                position.qty < static_cast<int64_t>(cfg_.max_position)) {
                std::vector<TradeReport> exec_trades;
                auto added = book.add_order(1000000 + i, Side::Buy, book.best_ask(), cfg_.order_size,
                               msg.timestamp_ns, exec_trades);
                (void)added;
                for (const auto& t : exec_trades) {
                    double impact = static_cast<double>(t.price) * cfg_.market_impact_bps / 10000.0;
                    position.add_fill(Side::Buy, t.price, t.quantity,
                                      cfg_.commission_per_share + impact);
                }
            } else if (signal == EMASignalEngine::Signal::Sell &&
                       position.qty > -static_cast<int64_t>(cfg_.max_position)) {
                std::vector<TradeReport> exec_trades;
                auto added = book.add_order(1000000 + i, Side::Sell, book.best_bid(), cfg_.order_size,
                               msg.timestamp_ns, exec_trades);
                (void)added;
                for (const auto& t : exec_trades) {
                    double impact = static_cast<double>(t.price) * cfg_.market_impact_bps / 10000.0;
                    position.add_fill(Side::Sell, t.price, t.quantity,
                                      cfg_.commission_per_share + impact);
                }
            }
        }

        Timestamp msg_end = rdtsc();
        record_latency(msg_start, msg_end);

        Price current_mid = book.mid_price();
        if (current_mid != INVALID_PRICE) {
            double equity = position.realized_pnl + position.unrealized_pnl(current_mid);
            equity_curve_.push_back(equity);
            update_drawdown(equity);
        }
    }

    Timestamp end_time = rdtsc();

    result.total_trades = trade_count;
    result.realized_pnl = position.realized_pnl;
    Price final_mid = book.mid_price();
    result.unrealized_pnl = (final_mid != INVALID_PRICE) ?
                            position.unrealized_pnl(final_mid) : 0.0;
    result.total_pnl = result.realized_pnl + result.unrealized_pnl;
    result.max_drawdown = current_drawdown_;

    if (!latencies_.empty()) {
        std::sort(latencies_.begin(), latencies_.end());
        result.avg_latency_ns = static_cast<double>(
            std::accumulate(latencies_.begin(), latencies_.end(), 0ULL)) / latencies_.size();
        result.p50_latency_ns = static_cast<double>(latencies_[latencies_.size() * 50 / 100]);
        result.p99_latency_ns = static_cast<double>(latencies_[latencies_.size() * 99 / 100]);
        result.p999_latency_ns = static_cast<double>(latencies_[latencies_.size() * 999 / 1000]);
        result.max_latency_ns = static_cast<double>(latencies_.back());
    }

    double elapsed_sec = static_cast<double>(end_time - start_time) / 1e9;
    result.orders_per_second = static_cast<double>(num_messages) / elapsed_sec;
    result.trades_per_second = static_cast<double>(trade_count) / elapsed_sec;

    return result;
}

BacktestResult Backtester::run_replay(
    const std::vector<MarketDataMessage>& messages,
    const EMASignalEngine::Config& signal_cfg) {

    OrderBook book;
    EMASignalEngine signal_engine(signal_cfg);
    BacktestResult result;

    for (const auto& msg : messages) {
        if (!msg.is_valid()) continue;

        std::vector<TradeReport> trades;
        switch (msg.type) {
            case OrderType::Limit: {
                auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity,
                               msg.timestamp_ns, trades);
                (void)added;
                break;
            }
            case OrderType::Cancel: {
                auto cancelled = book.cancel_order(msg.order_id);
                (void)cancelled;
                break;
            }
            case OrderType::Modify: {
                auto modified = book.modify_order(msg.order_id, msg.price, msg.quantity,
                                  msg.timestamp_ns, trades);
                (void)modified;
                break;
            }
            default:
                break;
        }

        Price mid = book.mid_price();
        if (mid != INVALID_PRICE) {
            (void)signal_engine.on_price(mid);
        }
    }

    return result;
}

BacktestResult Backtester::benchmark_latency(
    MarketDataSimulator& simulator,
    size_t num_messages,
    size_t warmup_messages) {

    OrderBook book;

    for (size_t i = 0; i < warmup_messages; ++i) {
        auto msg = simulator.next_message();
        std::vector<TradeReport> trades;
        if (msg.type == OrderType::Limit) {
            auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity,
                           msg.timestamp_ns, trades);
            (void)added;
        }
    }

    latencies_.clear();

    for (size_t i = 0; i < num_messages; ++i) {
        auto msg = simulator.next_message();
        std::vector<TradeReport> trades;

        Timestamp start = rdtsc();
        if (msg.type == OrderType::Limit) {
            auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity,
                           msg.timestamp_ns, trades);
            (void)added;
        } else if (msg.type == OrderType::Cancel) {
            auto cancelled = book.cancel_order(msg.order_id);
            (void)cancelled;
        }
        Timestamp end = rdtsc();

        latencies_.push_back(end - start);
    }

    BacktestResult result;
    if (!latencies_.empty()) {
        std::sort(latencies_.begin(), latencies_.end());
        result.avg_latency_ns = static_cast<double>(
            std::accumulate(latencies_.begin(), latencies_.end(), 0ULL)) / latencies_.size();
        result.p50_latency_ns = static_cast<double>(latencies_[latencies_.size() * 50 / 100]);
        result.p99_latency_ns = static_cast<double>(latencies_[latencies_.size() * 99 / 100]);
        result.p999_latency_ns = static_cast<double>(latencies_[latencies_.size() * 999 / 1000]);
        result.max_latency_ns = static_cast<double>(latencies_.back());
    }

    return result;
}

void Backtester::record_latency(Timestamp start_ns, Timestamp end_ns) {
    if (end_ns > start_ns) {
        latencies_.push_back(end_ns - start_ns);
    }
}

void Backtester::update_drawdown(double equity) {
    if (equity > peak_equity_) {
        peak_equity_ = equity;
    }
    double dd = peak_equity_ - equity;
    if (dd > current_drawdown_) {
        current_drawdown_ = dd;
    }
}

} // namespace yob
