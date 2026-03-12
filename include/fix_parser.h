#pragma once
#include "order.h"
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// Minimal FIX 4.2 parser. Handles 35=D (NewOrderSingle) and 35=F (OrderCancelRequest).
// Everything else returns nullopt. Missing required tags also return nullopt.
// Prices come in as decimal strings ("150.05") and are stored as fixed-point cents.

struct FIXNewOrder {
    std::string cl_ord_id;
    Side        side;
    OrderType   order_type;
    int64_t     price;    // fixed-point cents; 0 for MARKET
    int64_t     quantity;
};

struct FIXCancelRequest {
    std::string cl_ord_id;
    std::string orig_cl_ord_id;
    Side        side;
};

using FIXMessage = std::variant<FIXNewOrder, FIXCancelRequest>;

class FIXParser {
public:
    // Decode a raw SOH-delimited FIX message.
    static std::optional<FIXMessage> parse(std::string_view raw);

    // Encode a NewOrderSingle for testing/round-trip validation.
    static std::string build_new_order(const FIXNewOrder& msg);
};
