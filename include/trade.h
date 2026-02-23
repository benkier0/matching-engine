#pragma once
#include <cstdint>
#include <iostream>

struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    int64_t price;
    int64_t quantity;
    uint64_t timestamp;

    void print() const {
        std::cout << "Trade{buy_id=" << buy_order_id
                  << ", sell_id=" << sell_order_id
                  << ", price=" << price
                  << ", quantity=" << quantity
                  << ", ts=" << timestamp
                  << "}\n";
    }
};