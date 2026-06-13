#include "orderbook/orderbook.hpp"
#include "orderbook/market_data.hpp"
#include <iostream>
#include <chrono>

using namespace yob;

int main() {
    constexpr size_t N = 10'000'000;

    // Use regular aggregate initialization (not designated initializers)
    MarketDataSimulator::Config sim_cfg;
    sim_cfg.start_price = 10000;
    sim_cfg.volatility = 25.0;
    sim_cfg.arrival_rate = 100000.0;
    sim_cfg.cancel_rate = 0.2;
    sim_cfg.modify_rate = 0.05;
    sim_cfg.max_order_size = 200;
    sim_cfg.seed = 99999;

    MarketDataSimulator sim(sim_cfg);

    OrderBook book;
    std::vector<TradeReport> trades;
    trades.reserve(1000);

    // Warmup
    for (size_t i = 0; i < 100000; ++i) {
        auto msg = sim.next_message();
        if (msg.type == OrderType::Limit) {
            [[maybe_unused]] auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity, msg.timestamp_ns, trades);
        }
    }

    // Benchmark
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < N; ++i) {
        auto msg = sim.next_message();
        trades.clear();
        if (msg.type == OrderType::Limit) {
            [[maybe_unused]] auto added = book.add_order(msg.order_id, msg.side, msg.price, msg.quantity, msg.timestamp_ns, trades);
        } else if (msg.type == OrderType::Cancel) {
            [[maybe_unused]] auto cancelled = book.cancel_order(msg.order_id);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::cout << "Processed " << N << " messages in " << ns/1e6 << " ms" << std::endl;
    std::cout << "Throughput: " << (N * 1e9 / ns) / 1e6 << " M msgs/sec" << std::endl;
    std::cout << "Avg latency: " << static_cast<double>(ns) / N << " ns/msg" << std::endl;

    return 0;
}