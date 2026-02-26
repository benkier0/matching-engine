#pragma once
#include "order.h"
#include <map>
#include <deque>
#include <optional>

class OrderBook {
public:
    void add_order(const Order& order);

    bool cancel_order(uint64_t order_id);

    std::optional<int64_t> best_bid() const;
    std::optional<int64_t> best_ask() const;

    void print_book() const;

private:
    std::map<int64_t, std::deque<Order>, std::greater<>> bids_; // dsc
    std::map<int64_t, std::deque<Order>> asks_;                 // asc
};