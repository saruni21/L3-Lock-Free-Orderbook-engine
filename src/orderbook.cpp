#include "orderbook/orderbook.hpp"
#include <sstream>
#include <iomanip>

namespace yob {

bool OrderBook::add_order(OrderId id, Side side, Price price, 
                           Quantity qty, Timestamp ts,
                           std::vector<TradeReport>& trades) noexcept {
    if (order_map_.find(id) != nullptr) {
        return false;
    }

    Order* order = order_pool_.acquire(id, side, price, qty, ts);
    if (order == nullptr) {
        return false;
    }

    if (side == Side::Buy && price >= best_ask() && best_ask() != INVALID_PRICE) {
        match_buy_order(order, trades);
        if (order->quantity == 0) {
            order_pool_.release(order);
            return true;
        }
    } else if (side == Side::Sell && price <= best_bid() && best_bid() != INVALID_PRICE) {
        match_sell_order(order, trades);
        if (order->quantity == 0) {
            order_pool_.release(order);
            return true;
        }
    }

    PriceLevel* level = find_or_create_level(order->price, side);
    if (level == nullptr) {
        order_pool_.release(order);
        return false;
    }

    level->add_order(order);
    [[maybe_unused]] bool inserted = order_map_.insert(id, order);

    return true;
}

bool OrderBook::cancel_order(OrderId id) noexcept {
    Order** ptr = order_map_.find(id);
    if (ptr == nullptr) {
        return false;
    }

    Order* order = *ptr;
    PriceLevel* level = order->price_level;

    if (level != nullptr) {
        level->remove_order(order);

        if (level->empty()) {
            remove_level(level, order->side);
        }
    }

    order_map_.remove(id);
    order_pool_.release(order);

    return true;
}

bool OrderBook::modify_order(OrderId id, Price new_price, 
                              Quantity new_qty, Timestamp ts,
                              std::vector<TradeReport>& trades) noexcept {
    Order** ptr = order_map_.find(id);
    if (ptr == nullptr) {
        return false;
    }

    Order* old_order = *ptr;
    Side side = old_order->side;

    [[maybe_unused]] bool cancelled = cancel_order(id);

    return add_order(id, side, new_price, new_qty, ts, trades);
}

Price OrderBook::best_bid() const noexcept {
    const PriceLevel* level = get_best_bid_level();
    return level ? level->price : INVALID_PRICE;
}

Price OrderBook::best_ask() const noexcept {
    const PriceLevel* level = get_best_ask_level();
    return level ? level->price : INVALID_PRICE;
}

Quantity OrderBook::best_bid_qty() const noexcept {
    const PriceLevel* level = get_best_bid_level();
    return level ? level->total_quantity : 0;
}

Quantity OrderBook::best_ask_qty() const noexcept {
    const PriceLevel* level = get_best_ask_level();
    return level ? level->total_quantity : 0;
}

Price OrderBook::mid_price() const noexcept {
    Price bid = best_bid();
    Price ask = best_ask();
    if (bid == INVALID_PRICE || ask == INVALID_PRICE) {
        return INVALID_PRICE;
    }
    return (bid + ask) / 2;
}

Price OrderBook::spread() const noexcept {
    Price bid = best_bid();
    Price ask = best_ask();
    if (bid == INVALID_PRICE || ask == INVALID_PRICE) {
        return INVALID_PRICE;
    }
    return ask - bid;
}

void OrderBook::get_bids(size_t n, std::vector<PriceLevel>& out) const noexcept {
    out.clear();
    const PriceLevel* level = get_best_bid_level();
    while (level != nullptr && out.size() < n) {
        out.push_back(*level);
        level = bid_tree_.predecessor(level);
    }
}

void OrderBook::get_asks(size_t n, std::vector<PriceLevel>& out) const noexcept {
    out.clear();
    const PriceLevel* level = get_best_ask_level();
    while (level != nullptr && out.size() < n) {
        out.push_back(*level);
        level = ask_tree_.successor(level);
    }
}

void OrderBook::clear() noexcept {
    bid_tree_.clear();
    ask_tree_.clear();
    order_map_.clear();
    arena_.reset();
}

std::string OrderBook::dump() const {
    std::ostringstream oss;
oss << "=== ORDER BOOK ===" << std::endl;
    oss << "Bids (" << bid_tree_.size() << " levels, " 
        << order_map_.size() << " orders):" << std::endl;

    const PriceLevel* bid = get_best_bid_level();
    size_t count = 0;
    while (bid != nullptr && count < 10) {
        oss << "" << std::setw(8) << bid->price 
            << " x " << std::setw(4) << bid->total_quantity
            << " (" << bid->order_count << " orders)" << std::endl;
        bid = bid_tree_.predecessor(bid);
        ++count;
    }

    oss << "Asks (" << ask_tree_.size() << " levels):" << std::endl;
    const PriceLevel* ask = get_best_ask_level();
    count = 0;
    while (ask != nullptr && count < 10) {
        oss << "" << std::setw(8) << ask->price 
            << " x " << std::setw(4) << ask->total_quantity
            << " (" << ask->order_count << " orders)" << std::endl;
        ask = ask_tree_.successor(ask);
        ++count;
    }

    oss << "Spread: " << spread() << "" << std::endl;
    oss << "==================" << std::endl;
    return oss.str();
}

PriceLevel* OrderBook::find_or_create_level(Price price, Side side) noexcept {
    PriceLevel* existing = nullptr;
    if (side == Side::Buy) {
        existing = bid_tree_.find(price);
    } else {
        existing = ask_tree_.find(price);
    }

    if (existing != nullptr) {
        return existing;
    }

    PriceLevel* level = level_pool_.acquire(price, side);
    if (level == nullptr) {
        return nullptr;
    }

    if (side == Side::Buy) {
        [[maybe_unused]] bool inserted = bid_tree_.insert(level);
    } else {
        [[maybe_unused]] bool inserted = ask_tree_.insert(level);
    }

    return level;
}

void OrderBook::remove_level(PriceLevel* level, Side side) noexcept {
    if (side == Side::Buy) {
        bid_tree_.remove(level);
    } else {
        ask_tree_.remove(level);
    }
    level_pool_.release(level);
}

void OrderBook::match_order(Order* order, std::vector<TradeReport>& trades) noexcept {
    if (order->side == Side::Buy) {
        match_buy_order(order, trades);
    } else {
        match_sell_order(order, trades);
    }
}

void OrderBook::match_buy_order(Order* order, std::vector<TradeReport>& trades) noexcept {
    while (order->quantity > 0) {
        PriceLevel* best_ask_level = get_best_ask_level();
        if (best_ask_level == nullptr || best_ask_level->price > order->price) {
            break;
        }

        Order* resting = best_ask_level->head_order;
        while (resting != nullptr && order->quantity > 0) {
            Quantity match_qty = std::min(order->quantity, resting->quantity);

            TradeReport trade;
            trade.timestamp_ns = rdtsc();
            trade.price = resting->price;
            trade.quantity = match_qty;
            trade.aggressor_side = Side::Buy;
            trades.push_back(trade);

            order->quantity -= match_qty;
            resting->quantity -= match_qty;
            best_ask_level->total_quantity -= match_qty;

            if (resting->quantity == 0) {
                Order* next = resting->next_at_price;
                best_ask_level->remove_order(resting);
                order_map_.remove(resting->id);
                order_pool_.release(resting);
                resting = next;
            } else {
                resting = resting->next_at_price;
            }
        }

        if (best_ask_level->empty()) {
            remove_level(best_ask_level, Side::Sell);
        }
    }
}

void OrderBook::match_sell_order(Order* order, std::vector<TradeReport>& trades) noexcept {
    while (order->quantity > 0) {
        PriceLevel* best_bid_level = get_best_bid_level();
        if (best_bid_level == nullptr || best_bid_level->price < order->price) {
            break;
        }

        Order* resting = best_bid_level->head_order;
        while (resting != nullptr && order->quantity > 0) {
            Quantity match_qty = std::min(order->quantity, resting->quantity);

            TradeReport trade;
            trade.timestamp_ns = rdtsc();
            trade.price = resting->price;
            trade.quantity = match_qty;
            trade.aggressor_side = Side::Sell;
            trades.push_back(trade);

            order->quantity -= match_qty;
            resting->quantity -= match_qty;
            best_bid_level->total_quantity -= match_qty;

            if (resting->quantity == 0) {
                Order* next = resting->next_at_price;
                best_bid_level->remove_order(resting);
                order_map_.remove(resting->id);
                order_pool_.release(resting);
                resting = next;
            } else {
                resting = resting->next_at_price;
            }
        }

        if (best_bid_level->empty()) {
            remove_level(best_bid_level, Side::Buy);
        }
    }
}

// Non-const getters (for internal mutation)
PriceLevel* OrderBook::get_best_ask_level() noexcept {
    return const_cast<PriceLevel*>(
        static_cast<const OrderBook*>(this)->get_best_ask_level()
    );
}

PriceLevel* OrderBook::get_best_bid_level() noexcept {
    return const_cast<PriceLevel*>(
        static_cast<const OrderBook*>(this)->get_best_bid_level()
    );
}

// Const getters (for read-only access)
const PriceLevel* OrderBook::get_best_ask_level() const noexcept {
    return ask_tree_.minimum();
}

const PriceLevel* OrderBook::get_best_bid_level() const noexcept {
    return bid_tree_.minimum();
}

} // namespace yob