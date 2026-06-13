#include "orderbook/orderbook.hpp"
#include "orderbook/spsc_queue.hpp"
#include "orderbook/arena.hpp"
#include "orderbook/market_data.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace yob;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running " << #name << "... " << std::flush; \
    try { \
        test_##name(); \
        ++tests_passed; \
        std::cout << "PASS" << std::endl; \
    } catch (const std::exception& e) { \
        ++tests_failed; \
        std::cout << "FAIL: " << e.what() << std::endl; \
    } \
} while(0)

#define ASSERT_TRUE(cond) if (!(cond)) {     throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); }

#define ASSERT_EQ(a, b) if ((a) != (b)) {     throw std::runtime_error("Assertion failed: " #a " == " #b " at line " + std::to_string(__LINE__)); }

TEST(spsc_basic_push_pop) {
    SPSCQueue<int, 1024> q;
    ASSERT_TRUE(q.empty());

    ASSERT_TRUE(q.push(42));
    ASSERT_TRUE(!q.empty());

    int val;
    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 42);
    ASSERT_TRUE(q.empty());
}

TEST(spsc_capacity_boundary) {
    SPSCQueue<int, 4> q;

    ASSERT_TRUE(q.push(1));
    ASSERT_TRUE(q.push(2));
    ASSERT_TRUE(q.push(3));
    ASSERT_TRUE(!q.push(4));

    int val;
    ASSERT_TRUE(q.pop(val));
    ASSERT_EQ(val, 1);
    ASSERT_TRUE(q.push(4));
}

TEST(spsc_bulk_operations) {
    SPSCQueue<int, 1024> q;
    int items[10] = {0,1,2,3,4,5,6,7,8,9};

    size_t pushed = q.push_bulk<10>(items, 10);
    ASSERT_EQ(pushed, 10);

    int out;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(q.pop(out));
        ASSERT_EQ(out, i);
    }
}

TEST(pool_acquire_release) {
    struct TestObj {
        int x;
        explicit TestObj(int v) : x(v) {}
    };

    ObjectPool<TestObj, 100> pool;
    ASSERT_EQ(pool.available(), 100);

    auto* obj = pool.acquire(42);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->x, 42);
    ASSERT_EQ(pool.available(), 99);

    pool.release(obj);
    ASSERT_EQ(pool.available(), 100);
}

TEST(pool_exhaustion) {
    struct SimpleObj { int x; explicit SimpleObj(int v=0) : x(v) {} };
    ObjectPool<SimpleObj, 5> pool;

    SimpleObj* objs[5];
    for (int i = 0; i < 5; ++i) {
        objs[i] = pool.acquire(i);
        ASSERT_TRUE(objs[i] != nullptr);
    }

    ASSERT_TRUE(pool.acquire(99) == nullptr);

    pool.release(objs[2]);
    auto* obj = pool.acquire(99);
    ASSERT_TRUE(obj != nullptr);
    ASSERT_EQ(obj->x, 99);
}

TEST(ob_basic_add) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto added = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    ASSERT_TRUE(added);
    ASSERT_EQ(book.order_count(), 1);
    ASSERT_EQ(book.best_bid(), 10000);
    ASSERT_EQ(book.best_bid_qty(), 100);
    ASSERT_EQ(trades.size(), 0);
}

TEST(ob_bid_ask_setup) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    auto a2 = book.add_order(2, Side::Buy, 9995, 200, 0, trades);
    auto a3 = book.add_order(3, Side::Sell, 10005, 75, 0, trades);
    auto a4 = book.add_order(4, Side::Sell, 10010, 120, 0, trades);
    (void)a1; (void)a2; (void)a3; (void)a4;

    ASSERT_EQ(book.best_bid(), 10000);
    ASSERT_EQ(book.best_ask(), 10005);
    ASSERT_EQ(book.spread(), 5);
    ASSERT_EQ(book.mid_price(), 10002);
}

TEST(ob_match_buy_aggressive) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Sell, 10005, 75, 0, trades);
    auto a2 = book.add_order(2, Side::Sell, 10010, 120, 0, trades);
    (void)a1; (void)a2;

    trades.clear();
    auto added = book.add_order(3, Side::Buy, 10010, 200, 0, trades);
    (void)added;

    ASSERT_TRUE(trades.size() > 0);
    ASSERT_EQ(trades[0].price, 10005);
    ASSERT_EQ(trades[0].quantity, 75);
    ASSERT_EQ(trades[0].aggressor_side, Side::Buy);

    ASSERT_EQ(book.best_bid(), 10010);
}

TEST(ob_match_sell_aggressive) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    auto a2 = book.add_order(2, Side::Buy, 9995, 200, 0, trades);
    (void)a1; (void)a2;

    trades.clear();
    auto added = book.add_order(3, Side::Sell, 9995, 150, 0, trades);
    (void)added;

    ASSERT_TRUE(trades.size() > 0);
    ASSERT_EQ(trades[0].price, 10000);
    ASSERT_EQ(trades[0].quantity, 100);
    ASSERT_EQ(trades[0].aggressor_side, Side::Sell);
}

TEST(ob_cancel_order) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto added = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    (void)added;
    ASSERT_EQ(book.order_count(), 1);

    auto cancelled = book.cancel_order(1);
    ASSERT_TRUE(cancelled);
    ASSERT_EQ(book.order_count(), 0);
    ASSERT_EQ(book.best_bid(), INVALID_PRICE);
}

TEST(ob_multiple_orders_same_price) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    auto a2 = book.add_order(2, Side::Buy, 10000, 150, 0, trades);
    auto a3 = book.add_order(3, Side::Buy, 10000, 50, 0, trades);
    (void)a1; (void)a2; (void)a3;

    ASSERT_EQ(book.best_bid_qty(), 300);
    ASSERT_EQ(book.bid_level_count(), 1);
}

TEST(ob_fifo_priority) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Sell, 10000, 100, 0, trades);
    auto a2 = book.add_order(2, Side::Sell, 10000, 100, 1, trades);
    auto a3 = book.add_order(3, Side::Sell, 10000, 100, 2, trades);
    (void)a1; (void)a2; (void)a3;

    trades.clear();
    auto added = book.add_order(4, Side::Buy, 10000, 150, 3, trades);
    (void)added;

    ASSERT_EQ(trades.size(), 2);
    ASSERT_EQ(trades[0].quantity, 100);
    ASSERT_EQ(trades[1].quantity, 50);
}

TEST(ob_price_level_removal) {
    OrderBook book;
    std::vector<TradeReport> trades;

    auto a1 = book.add_order(1, Side::Buy, 10000, 100, 0, trades);
    auto a2 = book.add_order(2, Side::Buy, 9995, 200, 0, trades);
    (void)a1; (void)a2;

    auto cancelled = book.cancel_order(1);
    (void)cancelled;

    ASSERT_EQ(book.best_bid(), 9995);
    ASSERT_EQ(book.bid_level_count(), 1);
}

TEST(ema_basic) {
    EMASignalEngine::Config cfg;
    EMASignalEngine engine(cfg);

    // First price initializes EMAs
    auto sig = engine.on_price(10000);
    ASSERT_EQ(static_cast<int>(sig), 0);

    // Small price change - not enough to trigger crossover
    sig = engine.on_price(10001);
    ASSERT_EQ(static_cast<int>(sig), 0);

    // Rising prices should eventually trigger Buy
    for (int i = 0; i < 50; ++i) {
        sig = engine.on_price(10200 + i * 10);
    }

    // Fast EMA should be above slow EMA after sustained rise
    ASSERT_TRUE(engine.fast_ema() > engine.slow_ema());
    // And last signal should be Buy
    ASSERT_EQ(static_cast<int>(engine.last_signal()), 1);
}

TEST(ema_crossover) {
    EMASignalEngine::Config cfg;
    cfg.fast_period = 2;
    cfg.slow_period = 10;
    cfg.threshold = 0;

    EMASignalEngine engine(cfg);
    (void)engine.on_price(10000);

    (void)engine.on_price(9900);
    (void)engine.on_price(9800);        

    for (int i = 0; i < 20; ++i) {      
        (void)engine.on_price(10000 + i * 100);
    }

    ASSERT_EQ(static_cast<int>(engine.last_signal()), 1);
}

TEST(vwap_basic) {
    VWAPSignalEngine engine;

    engine.on_trade(10000, 100);
    ASSERT_EQ(engine.vwap(), 10000);

    engine.on_trade(10100, 100);
    ASSERT_EQ(engine.vwap(), 10050);

    engine.on_trade(9900, 200);
    ASSERT_EQ(engine.vwap(), 9975);
}

int main() {
    std::cout << "Order Book - Unit Tests" << std::endl;
    std::cout << "====================================" << std::endl;

    RUN_TEST(spsc_basic_push_pop);
    RUN_TEST(spsc_capacity_boundary);
    RUN_TEST(spsc_bulk_operations);
    RUN_TEST(pool_acquire_release);
    RUN_TEST(pool_exhaustion);
    RUN_TEST(ob_basic_add);
    RUN_TEST(ob_bid_ask_setup);
    RUN_TEST(ob_match_buy_aggressive);
    RUN_TEST(ob_match_sell_aggressive);
    RUN_TEST(ob_cancel_order);
    RUN_TEST(ob_multiple_orders_same_price);
    RUN_TEST(ob_fifo_priority);
    RUN_TEST(ob_price_level_removal);
    RUN_TEST(ema_basic);
    RUN_TEST(ema_crossover);
    RUN_TEST(vwap_basic);

    std::cout << "====================================" << std::endl;
    std::cout << "Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}