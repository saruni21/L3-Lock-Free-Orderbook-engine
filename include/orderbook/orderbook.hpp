#pragma once
#include <cstdint>
#include "orderbook/types.hpp"
#include "orderbook/intrusive_tree.hpp"
#include "orderbook/arena.hpp"
#include "orderbook/spsc_queue.hpp"

#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <functional>
#include <algorithm>

namespace yob {

// Forward Declarations
struct Order;
struct PriceLevel;
class OrderBook;

// Order Structure
struct alignas(64) Order {
    Price price;
    Quantity quantity;
    Quantity initial_quantity;
    OrderId id;
    Side side;
    uint8_t padding1[3];
    Timestamp timestamp_ns;

    RBTreeNodeBase tree_node;

    Order* next_at_price = nullptr;
    Order* prev_at_price = nullptr;
    PriceLevel* price_level = nullptr;

    Order(OrderId oid, Side s, Price p, Quantity q, Timestamp ts) noexcept
        : price(p)
        , quantity(q)
        , initial_quantity(q)
        , id(oid)
        , side(s)
        , padding1{}
        , timestamp_ns(ts)
        , tree_node()
        , next_at_price(nullptr)
        , prev_at_price(nullptr)
        , price_level(nullptr)
    {}
};

static_assert(sizeof(Order) <= 128, "Order should fit in 2 cache lines");
static_assert(alignof(Order) == 64, "Order must be 64-byte aligned");

// Price Level Structure
struct alignas(64) PriceLevel {
    Price price;
    Quantity total_quantity;
    uint32_t order_count;
    Side side;
    uint8_t padding[3];

    RBTreeNodeBase tree_node;

    Order* head_order = nullptr;
    Order* tail_order = nullptr;

    explicit PriceLevel(Price p, Side s) noexcept
        : price(p)
        , total_quantity(0)
        , order_count(0)
        , side(s)
        , padding{}
        , tree_node()
        , head_order(nullptr)
        , tail_order(nullptr)
    {}

    void add_order(Order* order) noexcept {
        order->next_at_price = nullptr;
        order->prev_at_price = tail_order;
        order->price_level = this;

        if (tail_order != nullptr) {
            tail_order->next_at_price = order;
        } else {
            head_order = order;
        }
        tail_order = order;

        total_quantity += order->quantity;
        ++order_count;
    }

    void remove_order(Order* order) noexcept {
        if (order->prev_at_price != nullptr) {
            order->prev_at_price->next_at_price = order->next_at_price;
        } else {
            head_order = order->next_at_price;
        }

        if (order->next_at_price != nullptr) {
            order->next_at_price->prev_at_price = order->prev_at_price;
        } else {
            tail_order = order->prev_at_price;
        }

        total_quantity -= order->quantity;
        --order_count;

        order->next_at_price = nullptr;
        order->prev_at_price = nullptr;
        order->price_level = nullptr;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_order == nullptr;
    }
};

static_assert(sizeof(PriceLevel) <= 128, "PriceLevel should fit in 2 cache lines");

// Hook specializations
template<>
struct IntrusiveHook<Order> {
    static RBTreeNodeBase* to_node(Order* obj) noexcept { return &obj->tree_node; }
    static Order* from_node(RBTreeNodeBase* node) noexcept { 
        return reinterpret_cast<Order*>(
            reinterpret_cast<uint8_t*>(node) - offsetof(Order, tree_node)
        );
    }
    static const Order* from_node(const RBTreeNodeBase* node) noexcept {
        return reinterpret_cast<const Order*>(
            reinterpret_cast<const uint8_t*>(node) - offsetof(Order, tree_node)
        );
    }
};

template<>
struct IntrusiveHook<PriceLevel> {
    static RBTreeNodeBase* to_node(PriceLevel* obj) noexcept { return &obj->tree_node; }
    static PriceLevel* from_node(RBTreeNodeBase* node) noexcept {
        return reinterpret_cast<PriceLevel*>(
            reinterpret_cast<uint8_t*>(node) - offsetof(PriceLevel, tree_node)
        );
    }
    static const PriceLevel* from_node(const RBTreeNodeBase* node) noexcept {
        return reinterpret_cast<const PriceLevel*>(
            reinterpret_cast<const uint8_t*>(node) - offsetof(PriceLevel, tree_node)
        );
    }
};

// Key extractors
struct PriceLevelKey {
    using type = Price;
    Price operator()(const PriceLevel& pl) const noexcept { return pl.price; }
};

struct OrderKey {
    using type = OrderId;
    OrderId operator()(const Order& o) const noexcept { return o.id; }
};

// Flat Hash Map
template<typename K, typename V, size_t Capacity>
class FlatHashMap {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    struct Entry {
        K key;
        V value;
        bool occupied = false;
    };

public:
    static constexpr size_t MASK = Capacity - 1;

    FlatHashMap() {
        entries_.resize(Capacity);
    }

    [[nodiscard]] bool insert(K key, V value) noexcept {
        size_t idx = hash(key) & MASK;
        size_t probe = 0;

        while (probe < Capacity) {
            if (!entries_[idx].occupied) {
                entries_[idx].key = key;
                entries_[idx].value = value;
                entries_[idx].occupied = true;
                ++size_;
                return true;
            }
            if (entries_[idx].key == key) {
                entries_[idx].value = value;
                return true;
            }
            idx = (idx + 1) & MASK;
            ++probe;
        }
        return false;
    }

    [[nodiscard]] V* find(K key) noexcept {
        size_t idx = hash(key) & MASK;
        size_t probe = 0;

        while (probe < Capacity && entries_[idx].occupied) {
            if (entries_[idx].key == key) {
                return &entries_[idx].value;
            }
            idx = (idx + 1) & MASK;
            ++probe;
        }
        return nullptr;
    }

    bool remove(K key) noexcept {
        size_t idx = hash(key) & MASK;
        size_t probe = 0;

        while (probe < Capacity && entries_[idx].occupied) {
            if (entries_[idx].key == key) {
                entries_[idx].occupied = false;
                --size_;
                return true;
            }
            idx = (idx + 1) & MASK;
            ++probe;
        }
        return false;
    }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    void clear() noexcept {
        for (auto& e : entries_) e.occupied = false;
        size_ = 0;
    }

private:
    std::vector<Entry> entries_;
    size_t size_ = 0;

    static size_t hash(K key) noexcept {
        size_t h = 14695981039346656037ULL;
        for (size_t i = 0; i < sizeof(K); ++i) {
            h ^= (key >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
        return h;
    }
};

// Order Book
class alignas(128) OrderBook {
public:
    static constexpr size_t MAX_ORDERS = 1'000'000;
    static constexpr size_t MAX_PRICE_LEVELS = 100'000;
    static constexpr size_t ORDER_MAP_SIZE = 1'048'576;

    OrderBook() = default;
    ~OrderBook() = default;

    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    [[nodiscard]] bool add_order(OrderId id, Side side, Price price, 
                                  Quantity qty, Timestamp ts,
                                  std::vector<TradeReport>& trades) noexcept;

    [[nodiscard]] bool cancel_order(OrderId id) noexcept;

    [[nodiscard]] bool modify_order(OrderId id, Price new_price, 
                                     Quantity new_qty, Timestamp ts,
                                     std::vector<TradeReport>& trades) noexcept;

    [[nodiscard]] Price best_bid() const noexcept;
    [[nodiscard]] Price best_ask() const noexcept;
    [[nodiscard]] Quantity best_bid_qty() const noexcept;
    [[nodiscard]] Quantity best_ask_qty() const noexcept;
    [[nodiscard]] Price mid_price() const noexcept;
    [[nodiscard]] Price spread() const noexcept;

    void get_bids(size_t n, std::vector<PriceLevel>& out) const noexcept;
    void get_asks(size_t n, std::vector<PriceLevel>& out) const noexcept;

    [[nodiscard]] size_t order_count() const noexcept { return order_map_.size(); }
    [[nodiscard]] size_t bid_level_count() const noexcept { return bid_tree_.size(); }
    [[nodiscard]] size_t ask_level_count() const noexcept { return ask_tree_.size(); }

    void clear() noexcept;
    std::string dump() const;

private:
    struct BidCompare {
        bool operator()(Price a, Price b) const noexcept { return a > b; }
    };
    struct AskCompare {
        bool operator()(Price a, Price b) const noexcept { return a < b; }
    };

    IntrusiveRBTree<PriceLevel, PriceLevelKey, BidCompare> bid_tree_;
    IntrusiveRBTree<PriceLevel, PriceLevelKey, AskCompare> ask_tree_;

    FlatHashMap<OrderId, Order*, ORDER_MAP_SIZE> order_map_;

    ObjectPool<Order, MAX_ORDERS> order_pool_;
    ObjectPool<PriceLevel, MAX_PRICE_LEVELS> level_pool_;

    LinearArena arena_{64 * 1024 * 1024};

    [[nodiscard]] PriceLevel* find_or_create_level(Price price, Side side) noexcept;
    void remove_level(PriceLevel* level, Side side) noexcept;

    void match_order(Order* order, std::vector<TradeReport>& trades) noexcept;
    void match_buy_order(Order* order, std::vector<TradeReport>& trades) noexcept;
    void match_sell_order(Order* order, std::vector<TradeReport>& trades) noexcept;

    // Const-overloaded best level getters (standard C++ pattern)
    [[nodiscard]] PriceLevel* get_best_ask_level() noexcept;
    [[nodiscard]] const PriceLevel* get_best_ask_level() const noexcept;
    [[nodiscard]] PriceLevel* get_best_bid_level() noexcept;
    [[nodiscard]] const PriceLevel* get_best_bid_level() const noexcept;
};

} // namespace yob