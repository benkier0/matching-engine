#include "order_book.h"
#include <iostream>

void OrderBook::add_order(const Order& order) {
    if (order.side == Side::BUY) {
        bids_[order.price].push_back(order);
    } else {
        asks_[order.price].push_back(order);
    }
}

bool OrderBook::cancel_order(uint64_t order_id) {
    return false;
}

std::optional<int64_t> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

void OrderBook::print_book() const {
    std::cout << "Bids:\n";
    for (const auto& [price, orders] : bids_) {
        std::cout << " " << price << ": " << orders.size() << " orders\n";
    }
    std::cout << "Asks:\n";
    for (const auto& [price, orders] : asks_) {
        std::cout << " " << price << ": " << orders.size() << " orders\n";
    }
}