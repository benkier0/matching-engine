#pragma once
#include <cstdint>
#include <iostream>

enum class Side : uint8_t { BUY, SELL };

struct Order {
    uint64_t id;
    Side side;
    int64_t price; // cents
    int64_t quantity;
    uint64_t timestamp; // ns

    void print() const {
        std::cout << "Order{id=" << id
                  << ", side=" << (side == Side::BUY ? "BUY" : "SELL")
                  << ", price=" << price
                  << ", quantity=" << quantity
                  << ", ts=" << timestamp
                  << "}\n";
    }
};