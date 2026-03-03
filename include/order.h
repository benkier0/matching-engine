#pragma once
#include <cstdint>

enum class Side      : uint8_t { BUY, SELL };
enum class OrderType : uint8_t { LIMIT, MARKET };

struct Order {
    uint64_t  id;
    Side      side;
    OrderType type      = OrderType::LIMIT;
    int64_t   price;     // fixed-point cents; unused for MARKET orders
    int64_t   quantity;
    uint64_t  timestamp; // nanoseconds since epoch
};
