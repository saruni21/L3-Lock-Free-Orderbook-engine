#include "orderbook/orderbook.hpp"
#include "orderbook/market_data.hpp"
#include "orderbook/backtester.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>

using namespace yob;

void demo_order_book() {
    std::cout << "=== ORDER BOOK DEMO ===" << std::endl;

    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Buy, 10000, 100, rdtsc(), trades);
    auto a2 = book.add_order(2, Side::Buy, 9995, 200, rdtsc(), trades);
    auto a3 = book.add_order(3, Side::Buy, 9990, 150, rdtsc(), trades);
    auto a4 = book.add_order(4, Side::Sell, 10005, 75, rdtsc(), trades);
    auto a5 = book.add_order(5, Side::Sell, 10010, 120, rdtsc(), trades);
    auto a6 = book.add_order(6, Side::Sell, 10015, 90, rdtsc(), trades);
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;

    std::cout << book.dump();

    std::cout << "--- Matching aggressive buy order (10010 x 200) ---" << std::endl;
    trades.clear();
    auto added = book.add_order(7, Side::Buy, 10010, 200, rdtsc(), trades);
    (void)added;

    std::cout << "Trades executed: " << trades.size() << "" << std::endl;
    for (const auto& t : trades) {
        std::cout << "  " << t.quantity << " @ " << t.price 
                  << " (aggressor: " << (t.aggressor_side == Side::Buy ? "BUY" : "SELL") << ")" << std::endl;
    }

    std::cout << "After match:" << std::endl;
    std::cout << book.dump();

    std::cout << "--- Cancelling order #2 ---" << std::endl;
    auto cancelled = book.cancel_order(2);
    (void)cancelled;
    std::cout << book.dump();
}

void benchmark_throughput() {
    std::cout << "=== THROUGHPUT BENCHMARK ===" << std::endl;

    constexpr size_t NUM_MESSAGES = 1'000'000;
    constexpr size_t WARMUP = 100'000;

    MarketDataSimulator::Config sim_cfg;
    sim_cfg.start_price = 10000;
    sim_cfg.volatility = 30.0;
    sim_cfg.arrival_rate = 50000.0;
    sim_cfg.cancel_rate = 0.25;
    sim_cfg.modify_rate = 0.05;
    sim_cfg.max_order_size = 500;

    MarketDataSimulator simulator(sim_cfg);

    OrderBook book;
    std::vector<TradeReport> trades;
    trades.reserve(1000);

    for (size_t i = 0; i < WARMUP; ++i) {
        auto msg = simulator.next_message();
        if (msg.type == OrderType::Limit) {
            auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity,
                           msg.timestamp_ns, trades);
            (void)added;
        } else if (msg.type == OrderType::Cancel) {
            auto cancelled = book.cancel_order(msg.order_id);
            (void)cancelled;
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    size_t total_trades = 0;

    for (size_t i = 0; i < NUM_MESSAGES; ++i) {
        auto msg = simulator.next_message();
        trades.clear();

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

        total_trades += trades.size();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    double elapsed_sec = elapsed_ns / 1e9;

    double msgs_per_sec = NUM_MESSAGES / elapsed_sec;
    double trades_per_sec = total_trades / elapsed_sec;
    double ns_per_msg = static_cast<double>(elapsed_ns) / NUM_MESSAGES;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Messages processed: " << NUM_MESSAGES << "" << std::endl;
    std::cout << "Trades generated:   " << total_trades << "" << std::endl;
    std::cout << "Elapsed time:       " << elapsed_sec * 1000 << " ms" << std::endl;
    std::cout << "Throughput:         " << msgs_per_sec / 1e6 << " M msgs/sec" << std::endl;
    std::cout << "Trades/sec:         " << trades_per_sec / 1e3 << " K trades/sec" << std::endl;
    std::cout << "Latency:            " << ns_per_msg << " ns/msg" << std::endl;
    std::cout << "Book state:         " << book.order_count() << " orders, "
              << book.bid_level_count() << " bid levels, "
              << book.ask_level_count() << " ask levels" << std::endl;
}

void run_backtest() {
    std::cout << "=== EMA CROSSOVER BACKTEST ===" << std::endl;

    MarketDataSimulator::Config sim_cfg;
    sim_cfg.start_price = 10000;
    sim_cfg.volatility = 40.0;
    sim_cfg.arrival_rate = 10000.0;
    sim_cfg.cancel_rate = 0.3;
    sim_cfg.modify_rate = 0.1;
    sim_cfg.max_order_size = 1000;
    sim_cfg.seed = 12345;

    MarketDataSimulator simulator(sim_cfg);

    BacktestConfig bt_cfg;
    bt_cfg.commission_per_share = 0.005;
    bt_cfg.market_impact_bps = 0.5;
    bt_cfg.order_size = 50;
    bt_cfg.max_position = 500;

    Backtester backtester(bt_cfg);

    EMASignalEngine::Config signal_cfg;
    signal_cfg.fast_period = 10;
    signal_cfg.slow_period = 30;
    signal_cfg.threshold = 2;

    auto result = backtester.run_ema_strategy(simulator, 500'000, signal_cfg);
    result.print();
}

void benchmark_latency_detailed() {
    std::cout << "=== LATENCY MICROBENCHMARK ===" << std::endl;

    MarketDataSimulator::Config sim_cfg;
    sim_cfg.start_price = 10000;
    sim_cfg.volatility = 25.0;
    sim_cfg.arrival_rate = 100000.0;
    sim_cfg.cancel_rate = 0.2;
    sim_cfg.modify_rate = 0.05;
    sim_cfg.max_order_size = 200;
    sim_cfg.seed = 99999;

    MarketDataSimulator simulator(sim_cfg);

    BacktestConfig bt_cfg;
    Backtester backtester(bt_cfg);

    auto result = backtester.benchmark_latency(simulator, 1'000'000, 50'000);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Latency Distribution (TSC cycles):" << std::endl;
    std::cout << "  Average: " << result.avg_latency_ns << "" << std::endl;
    std::cout << "  P50:     " << result.p50_latency_ns << "" << std::endl;
    std::cout << "  P99:     " << result.p99_latency_ns << "" << std::endl;
    std::cout << "  P99.9:   " << result.p999_latency_ns << "" << std::endl;
    std::cout << "  Max:     " << result.max_latency_ns << "" << std::endl;

    double tsc_freq_ghz = 3.0;
    std::cout << "Approximate nanoseconds (assuming " << tsc_freq_ghz << " GHz):" << std::endl;
    std::cout << "  Average: " << result.avg_latency_ns / tsc_freq_ghz << " ns" << std::endl;
    std::cout << "  P50:     " << result.p50_latency_ns / tsc_freq_ghz << " ns" << std::endl;
    std::cout << "  P99:     " << result.p99_latency_ns / tsc_freq_ghz << " ns" << std::endl;
    std::cout << "  P99.9:   " << result.p999_latency_ns / tsc_freq_ghz << " ns" << std::endl;
    std::cout << "  Max:     " << result.max_latency_ns / tsc_freq_ghz << " ns" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "Order Book Engine" << std::endl;
    std::cout << "=============================" << std::endl;
    std::cout << "C++20 | Lock-Free | Zero-Allocation | L3 Depth" << std::endl;

    if (argc > 1) {
        std::string mode(argv[1]);
        if (mode == "demo") {
            demo_order_book();
        } else if (mode == "bench") {
            benchmark_throughput();
        } else if (mode == "backtest") {
            run_backtest();
        } else if (mode == "latency") {
            benchmark_latency_detailed();
        } else {
            std::cout << "Usage: " << argv[0] << " [demo|bench|backtest|latency]";
            return 1;
        }
    } else {
        demo_order_book();
        benchmark_throughput();
        run_backtest();
        benchmark_latency_detailed();
    }

    return 0;
}