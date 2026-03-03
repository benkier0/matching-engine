#include "order_book.h"
#include <algorithm>
#include <iostream>

std::vector<Trade> OrderBook::add_order(Order order) {
    // Market orders receive a sentinel price that sweeps the entire book.
    // INT64_MAX for a market buy means it will cross any ask price;
    // INT64_MIN for a market sell means it will cross any bid price.
    if (order.type == OrderType::MARKET) {
        order.price = (order.side == Side::BUY) ? INT64_MAX : INT64_MIN;
    }

    std::vector<Trade> trades = match(order);

    // Any unfilled quantity rests in the book (limit orders only).
    if (order.quantity > 0 && order.type == OrderType::LIMIT) {
        index_[order.id] = {order.side, order.price};
        if (order.side == Side::BUY)
            bids_[order.price].push_back(order);
        else
            asks_[order.price].push_back(order);
    }

    return trades;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = index_.find(order_id);
    if (it == index_.end()) return false;

    auto [side, price] = it->second;
    index_.erase(it);

    // Remove from whichever side the order lives on.
    auto remove_from = [&](auto& book) {
        auto level = book.find(price);
        if (level == book.end()) return;

        auto& queue = level->second;
        auto pos = std::find_if(queue.begin(), queue.end(),
            [order_id](const Order& o) { return o.id == order_id; });
        if (pos != queue.end()) queue.erase(pos);
        if (queue.empty()) book.erase(level);
    };

    if (side == Side::BUY) remove_from(bids_);
    else                   remove_from(asks_);

    return true;
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
    constexpr int kDepth = 5;

    // Print asks top-down (highest ask at the top)
    std::cout << "  price   qty\n";
    std::cout << "  ─────────────\n";
    int n = 0;
    for (auto it = asks_.rbegin(); it != asks_.rend() && n < kDepth; ++it, ++n) {
        int64_t total = 0;
        for (const auto& o : it->second) total += o.quantity;
        std::cout << "  " << it->first << "  " << total << "  (ask)\n";
    }
    std::cout << "  ─────────────\n";
    n = 0;
    for (const auto& [price, queue] : bids_) {
        if (n++ >= kDepth) break;
        int64_t total = 0;
        for (const auto& o : queue) total += o.quantity;
        std::cout << "  " << price << "  " << total << "  (bid)\n";
    }
    std::cout << "  ─────────────\n";
}

std::vector<Trade> OrderBook::match(Order& aggressor) {
    std::vector<Trade> trades;

    if (aggressor.side == Side::BUY) {
        // Walk up the ask ladder from the lowest-priced ask.
        while (!asks_.empty() && aggressor.quantity > 0) {
            auto level = asks_.begin();
            if (level->first > aggressor.price) break; // no more crossing prices

            auto& queue = level->second;
            while (!queue.empty() && aggressor.quantity > 0) {
                Order& passive = queue.front();
                int64_t qty   = std::min(aggressor.quantity, passive.quantity);

                trades.push_back({aggressor.id, passive.id,
                                  level->first, qty, aggressor.timestamp});
                aggressor.quantity -= qty;
                passive.quantity   -= qty;

                if (passive.quantity == 0) {
                    index_.erase(passive.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) asks_.erase(level);
        }
    } else {
        // Walk down the bid ladder from the highest-priced bid.
        while (!bids_.empty() && aggressor.quantity > 0) {
            auto level = bids_.begin();
            if (level->first < aggressor.price) break; // no more crossing prices

            auto& queue = level->second;
            while (!queue.empty() && aggressor.quantity > 0) {
                Order& passive = queue.front();
                int64_t qty   = std::min(aggressor.quantity, passive.quantity);

                trades.push_back({passive.id, aggressor.id,
                                  level->first, qty, aggressor.timestamp});
                aggressor.quantity -= qty;
                passive.quantity   -= qty;

                if (passive.quantity == 0) {
                    index_.erase(passive.id);
                    queue.pop_front();
                }
            }
            if (queue.empty()) bids_.erase(level);
        }
    }

    return trades;
}
