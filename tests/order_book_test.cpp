#include "fix_parser.h"
#include "order_book.h"
#include "spsc_queue.h"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

// minimal test harness

struct TestRunner {
    int passed = 0, failed = 0;

    void run(const std::string& name, std::function<void()> fn) {
        try {
            fn();
            ++passed;
            std::cout << "  pass  " << name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "  FAIL  " << name << " — " << e.what() << "\n";
        }
    }

    int summary() const {
        std::cout << "\n" << passed << " passed";
        if (failed) std::cout << ", " << failed << " FAILED";
        std::cout << "\n";
        return failed > 0 ? 1 : 0;
    }
};

#define REQUIRE(expr)                                                          \
    do {                                                                       \
        if (!(expr))                                                           \
            throw std::runtime_error("assertion failed: " #expr               \
                                     " [" __FILE__ ":" + std::to_string(__LINE__) + "]"); \
    } while (0)


static Order limit_order(uint64_t id, Side side, int64_t price, int64_t qty) {
    return {id, side, OrderType::LIMIT, price, qty, 0};
}

static Order market_order(uint64_t id, Side side, int64_t qty) {
    return {id, side, OrderType::MARKET, 0, qty, 0};
}


static void test_empty_book() {
    OrderBook ob;
    REQUIRE(!ob.best_bid().has_value());
    REQUIRE(!ob.best_ask().has_value());
    REQUIRE(ob.bid_depth() == 0);
    REQUIRE(ob.ask_depth() == 0);
}

static void test_insert_no_cross() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY,  10000, 10));
    ob.add_order(limit_order(2, Side::SELL, 10100, 10));

    REQUIRE(*ob.best_bid() == 10000);
    REQUIRE(*ob.best_ask() == 10100);
    REQUIRE(ob.bid_depth() == 1);
    REQUIRE(ob.ask_depth() == 1);
}

static void test_no_trade_when_spread_open() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 10));
    auto trades = ob.add_order(limit_order(2, Side::SELL, 10100, 10));
    REQUIRE(trades.empty());
}

static void test_full_fill() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 10));
    auto trades = ob.add_order(limit_order(2, Side::SELL, 10000, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].quantity == 10);
    REQUIRE(trades[0].price == 10000);
    REQUIRE(trades[0].buy_order_id == 1);
    REQUIRE(trades[0].sell_order_id == 2);

    // Both sides should be empty
    REQUIRE(!ob.best_bid().has_value());
    REQUIRE(!ob.best_ask().has_value());
}

static void test_partial_fill_resting_side() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 10));
    auto trades = ob.add_order(limit_order(2, Side::SELL, 10000, 3));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].quantity == 3);

    // 7 shares remain on the bid
    REQUIRE(*ob.best_bid() == 10000);
    REQUIRE(!ob.best_ask().has_value());
}

static void test_partial_fill_aggressor() {
    // Aggressor is larger than the resting order
    OrderBook ob;
    ob.add_order(limit_order(1, Side::SELL, 10000, 3));
    auto trades = ob.add_order(limit_order(2, Side::BUY, 10000, 10));

    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].quantity == 3);

    // 7 shares of the aggressor rest on the ask
    REQUIRE(*ob.best_bid() == 10000);
    REQUIRE(!ob.best_ask().has_value());
}

static void test_price_priority() {
    // Two bids at different prices — sell should match the higher bid first.
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY,  9900, 10));
    ob.add_order(limit_order(2, Side::BUY, 10000, 10)); // better price

    auto trades = ob.add_order(limit_order(3, Side::SELL, 9900, 10));
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].buy_order_id == 2);    // order 2 matched (higher bid)
    REQUIRE(trades[0].price == 10000);        // filled at the passive price
}

static void test_time_priority() {
    // Two bids at the same price — first arrival should fill first.
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 5));
    ob.add_order(limit_order(2, Side::BUY, 10000, 5));

    auto trades = ob.add_order(limit_order(3, Side::SELL, 10000, 5));
    REQUIRE(trades.size() == 1);
    REQUIRE(trades[0].buy_order_id == 1); // order 1 was first
}

static void test_multi_level_sweep() {
    // Aggressive buy sweeps three ask levels.
    OrderBook ob;
    ob.add_order(limit_order(1, Side::SELL, 10010, 10));
    ob.add_order(limit_order(2, Side::SELL, 10020, 10));
    ob.add_order(limit_order(3, Side::SELL, 10030, 10));

    auto trades = ob.add_order(limit_order(4, Side::BUY, 10030, 25));
    REQUIRE(trades.size() == 3);
    REQUIRE(trades[0].price == 10010); // cheapest first
    REQUIRE(trades[1].price == 10020);
    REQUIRE(trades[2].price == 10030);
    REQUIRE(trades[2].quantity == 5);  // partial fill on last level

    // 5 remaining resting on the ask at 10030
    REQUIRE(*ob.best_ask() == 10030);
}

static void test_cancel_order() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 10));
    ob.add_order(limit_order(2, Side::BUY, 10000, 10));

    REQUIRE(ob.cancel_order(1));
    REQUIRE(!ob.cancel_order(1));   // already gone
    REQUIRE(!ob.cancel_order(99));  // never existed

    // Level should still exist for order 2
    REQUIRE(*ob.best_bid() == 10000);
    ob.cancel_order(2);
    REQUIRE(!ob.best_bid().has_value());
}

static void test_cancel_then_no_match() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::SELL, 10000, 10));
    ob.cancel_order(1);

    auto trades = ob.add_order(limit_order(2, Side::BUY, 10000, 10));
    REQUIRE(trades.empty());
    REQUIRE(!ob.best_ask().has_value());
}

static void test_market_order_buy() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::SELL, 10100, 5));
    ob.add_order(limit_order(2, Side::SELL, 10200, 5));

    auto trades = ob.add_order(market_order(3, Side::BUY, 8));
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].price == 10100);  // cheapest ask first
    REQUIRE(trades[0].quantity == 5);
    REQUIRE(trades[1].price == 10200);
    REQUIRE(trades[1].quantity == 3);
}

static void test_market_order_sell() {
    OrderBook ob;
    ob.add_order(limit_order(1, Side::BUY, 10000, 5));
    ob.add_order(limit_order(2, Side::BUY,  9900, 5));

    auto trades = ob.add_order(market_order(3, Side::SELL, 7));
    REQUIRE(trades.size() == 2);
    REQUIRE(trades[0].price == 10000); // best bid first
    REQUIRE(trades[0].quantity == 5);
    REQUIRE(trades[1].price == 9900);
    REQUIRE(trades[1].quantity == 2);
}

// spsc queue tests

static void test_spsc_basic() {
    SPSCQueue<int, 8> q;
    REQUIRE(q.empty());

    REQUIRE(q.try_push(42));
    REQUIRE(!q.empty());

    int val = 0;
    REQUIRE(q.try_pop(val));
    REQUIRE(val == 42);
    REQUIRE(q.empty());
}

static void test_spsc_full() {
    SPSCQueue<int, 4> q; // capacity 4, usable slots = 3
    REQUIRE(q.try_push(1));
    REQUIRE(q.try_push(2));
    REQUIRE(q.try_push(3));
    REQUIRE(!q.try_push(4)); // full

    int v;
    REQUIRE(q.try_pop(v)); REQUIRE(v == 1);
    REQUIRE(q.try_push(4)); // now there's a slot
}

static void test_spsc_fifo_order() {
    SPSCQueue<int, 16> q;
    for (int i = 0; i < 10; ++i) { bool ok = q.try_push(i); REQUIRE(ok); }

    for (int i = 0; i < 10; ++i) {
        int v = -1;
        REQUIRE(q.try_pop(v));
        REQUIRE(v == i);
    }
    REQUIRE(q.empty());
}

// fix parser tests

static void test_fix_new_order_round_trip() {
    FIXNewOrder original{"ORD-001", Side::BUY, OrderType::LIMIT, 15005, 100};
    auto raw     = FIXParser::build_new_order(original);
    auto decoded = FIXParser::parse(raw);

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<FIXNewOrder>(*decoded));

    auto& msg = std::get<FIXNewOrder>(*decoded);
    REQUIRE(msg.cl_ord_id  == original.cl_ord_id);
    REQUIRE(msg.side       == original.side);
    REQUIRE(msg.order_type == original.order_type);
    REQUIRE(msg.quantity   == original.quantity);
    REQUIRE(msg.price      == original.price);
}

static void test_fix_market_order() {
    FIXNewOrder original{"ORD-002", Side::SELL, OrderType::MARKET, 0, 50};
    auto raw     = FIXParser::build_new_order(original);
    auto decoded = FIXParser::parse(raw);

    REQUIRE(decoded.has_value());
    auto& msg = std::get<FIXNewOrder>(*decoded);
    REQUIRE(msg.order_type == OrderType::MARKET);
    REQUIRE(msg.side == Side::SELL);
}

static void test_fix_cancel_request() {
    std::string raw = std::string("8=FIX.4.2\x01""35=F\x01""11=ORD-003\x01"
                                  "41=ORD-001\x01""54=2\x01");
    auto decoded = FIXParser::parse(raw);

    REQUIRE(decoded.has_value());
    REQUIRE(std::holds_alternative<FIXCancelRequest>(*decoded));

    auto& msg = std::get<FIXCancelRequest>(*decoded);
    REQUIRE(msg.cl_ord_id      == "ORD-003");
    REQUIRE(msg.orig_cl_ord_id == "ORD-001");
    REQUIRE(msg.side == Side::SELL);
}

static void test_fix_missing_tag_returns_nullopt() {
    // Missing qty tag (38)
    std::string raw = std::string("8=FIX.4.2\x01""35=D\x01""11=X\x01"
                                  "54=1\x01""40=2\x01""44=100.00\x01");
    REQUIRE(!FIXParser::parse(raw).has_value());
}


int main() {
    TestRunner t;

    std::cout << "order book\n";
    t.run("empty book",                test_empty_book);
    t.run("insert no cross",           test_insert_no_cross);
    t.run("no trade when spread open", test_no_trade_when_spread_open);
    t.run("full fill",                 test_full_fill);
    t.run("partial fill (resting)",    test_partial_fill_resting_side);
    t.run("partial fill (aggressor)",  test_partial_fill_aggressor);
    t.run("price priority",            test_price_priority);
    t.run("time priority",             test_time_priority);
    t.run("multi-level sweep",         test_multi_level_sweep);
    t.run("cancel order",              test_cancel_order);
    t.run("cancel then no match",      test_cancel_then_no_match);
    t.run("market order buy",          test_market_order_buy);
    t.run("market order sell",         test_market_order_sell);

    std::cout << "\nspsc queue\n";
    t.run("basic push/pop",            test_spsc_basic);
    t.run("full queue",                test_spsc_full);
    t.run("fifo order",                test_spsc_fifo_order);

    std::cout << "\nfix parser\n";
    t.run("new order round-trip",      test_fix_new_order_round_trip);
    t.run("market order",              test_fix_market_order);
    t.run("cancel request",            test_fix_cancel_request);
    t.run("missing tag -> nullopt",    test_fix_missing_tag_returns_nullopt);

    return t.summary();
}
