#include "order_book.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

using ns_t = uint64_t;

static ns_t now_ns() {
    return static_cast<ns_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

static void print_stats(const char* label, std::vector<ns_t>& lat) {
    if (lat.empty()) return;
    std::sort(lat.begin(), lat.end());

    size_t n     = lat.size();
    double mean  = std::accumulate(lat.begin(), lat.end(), 0.0) / n;
    double tput  = 1e9 / mean;

    auto pct = [&](double p) -> ns_t {
        size_t i = static_cast<size_t>(p / 100.0 * (n - 1));
        return lat[std::min(i, n - 1)];
    };

    std::cout << std::left  << std::setw(28) << label
              << std::right
              << std::setw(6) << static_cast<int>(tput / 1e6) << "M ops/s"
              << "   p50="   << std::setw(5) << pct(50)   << "ns"
              << "  p95="    << std::setw(5) << pct(95)   << "ns"
              << "  p99="    << std::setw(5) << pct(99)   << "ns"
              << "  p99.9="  << std::setw(6) << pct(99.9) << "ns"
              << "\n";
}

// non-crossing inserts: measures pure book maintenance cost (map insert,
// index update) without any matching work.
static void bench_insert(int n) {
    OrderBook ob;
    std::vector<ns_t> lat;
    lat.reserve(n);

    // Warm up L1/L2 caches with a pre-filled book.
    for (int i = 0; i < 1000; ++i)
        ob.add_order({(uint64_t)i, Side::BUY, OrderType::LIMIT,
                      50000 - i, 10, 0});

    for (int i = 0; i < n; ++i) {
        Order o{(uint64_t)(1000 + i), Side::BUY, OrderType::LIMIT,
                10000 + (i % 500), 10, 0};
        ns_t t0 = now_ns();
        ob.add_order(std::move(o));
        lat.push_back(now_ns() - t0);
    }

    print_stats("insert (no match)", lat);
}

// 1:1 matching: each pair of orders fully cancels.  Measures the hot path
// through the matching loop including index erasure and deque pop_front.
static void bench_match_1to1(int n) {
    std::vector<ns_t> lat;
    lat.reserve(n / 2);

    OrderBook ob;
    for (int i = 0; i < n; i += 2) {
        ob.add_order({(uint64_t)i,     Side::BUY,  OrderType::LIMIT, 10000, 1, 0});

        ns_t t0 = now_ns();
        ob.add_order({(uint64_t)(i+1), Side::SELL, OrderType::LIMIT, 10000, 1, 0});
        lat.push_back(now_ns() - t0);
    }

    print_stats("match (1:1 fill)", lat);
}

// multi-level sweep: each aggressive order crosses 5 price levels.
// exercises the inner matching loop and repeated map erasure.
static void bench_sweep(int n) {
    std::vector<ns_t> lat;
    lat.reserve(n);

    for (int round = 0; round < n; ++round) {
        OrderBook ob;
        uint64_t id = (uint64_t)round * 100;

        for (int lvl = 0; lvl < 5; ++lvl)
            ob.add_order({id + (uint64_t)lvl, Side::SELL, OrderType::LIMIT,
                          10000 + lvl * 10, 10, 0});

        ns_t t0 = now_ns();
        ob.add_order({id + 50, Side::BUY, OrderType::LIMIT, 10040, 50, 0});
        lat.push_back(now_ns() - t0);
    }

    print_stats("sweep (5-level)", lat);
}

// cancel: random-access erase from the index and price-level deque.
static void bench_cancel(int n) {
    OrderBook ob;
    std::vector<ns_t> lat;
    lat.reserve(n);

    for (int i = 0; i < n; ++i)
        ob.add_order({(uint64_t)i, Side::BUY, OrderType::LIMIT,
                      10000 + i, 10, 0}); // unique prices -> O(1) deque pop

    for (int i = 0; i < n; ++i) {
        ns_t t0 = now_ns();
        ob.cancel_order((uint64_t)i);
        lat.push_back(now_ns() - t0);
    }

    print_stats("cancel", lat);
}

int main() {
    constexpr int kWarmupRounds = 20'000;
    constexpr int kN            = 300'000;

    {
        OrderBook ob;
        for (int i = 0; i < kWarmupRounds; ++i)
            ob.add_order({(uint64_t)i, Side::BUY, OrderType::LIMIT, 10000, 1, 0});
    }

    std::cout << "matching-engine benchmark  (n=" << kN << " per case)\n"
              << std::string(70, '-') << "\n";

    bench_insert(kN);
    bench_match_1to1(kN);
    bench_sweep(kN / 10); // sweep is more expensive; fewer rounds
    bench_cancel(kN);

    std::cout << std::string(70, '-') << "\n"
              << "note: build with -DCMAKE_BUILD_TYPE=Release for accurate numbers\n";
}
