#pragma once
#include "order.h"
#include "trade.h"
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

// Price-time priority limit order book.
// Bids descending, asks ascending. add_order() returns generated trades.
// cancel_order() is O(1) via index_ + O(k) linear scan of the price-level deque.

class OrderBook {
public:
    // Add a limit or market order.  Returns any trades generated (may be empty).
    std::vector<Trade> add_order(Order order);

    // Cancel by order ID.  Returns true if the order was found and removed.
    bool cancel_order(uint64_t order_id);

    std::optional<int64_t> best_bid() const;
    std::optional<int64_t> best_ask() const;

    // Number of distinct price levels on each side.
    size_t bid_depth() const { return bids_.size(); }
    size_t ask_depth() const { return asks_.size(); }

    void print_book() const;

private:
    std::map<int64_t, std::deque<Order>, std::greater<int64_t>> bids_; // descending
    std::map<int64_t, std::deque<Order>>                        asks_; // ascending

    // Cancel index: order_id -> {side, resting price}
    std::unordered_map<uint64_t, std::pair<Side, int64_t>> index_;

    std::vector<Trade> match(Order& aggressor);
};
