#include "fix_parser.h"
#include "order_book.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static uint64_t epoch_ns() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}

static void run_demo() {
    std::cout << "=== matching engine demo ===\n\n";

    OrderBook ob;
    uint64_t next_id = 1;

    auto add = [&](Side side, int64_t price_cents, int64_t qty) {
        Order o{next_id++, side, OrderType::LIMIT, price_cents, qty, epoch_ns()};
        auto trades = ob.add_order(o);
        for (const auto& t : trades) {
            std::cout << "  trade  buy=" << t.buy_order_id
                      << " sell=" << t.sell_order_id
                      << "  qty=" << t.quantity
                      << "  @" << std::fixed << std::setprecision(2)
                      << (t.price / 100.0) << "\n";
        }
    };

    // build a book with a spread
    std::cout << "placing resting orders...\n";
    add(Side::BUY,  14990, 100);
    add(Side::BUY,  14980, 200);
    add(Side::BUY,  14970, 150);
    add(Side::SELL, 15010, 100);
    add(Side::SELL, 15020, 200);
    add(Side::SELL, 15030, 150);

    std::cout << "\nbook state:\n";
    ob.print_book();

    // aggressive buy that crosses the spread and sweeps two levels
    std::cout << "\naggressive buy (qty=250 @$150.25):\n";
    add(Side::BUY, 15025, 250);

    std::cout << "\nbook after sweep:\n";
    ob.print_book();
}

// csv format: timestamp,side,price,quantity,order_id
static void run_replay(const char* path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "error: cannot open " << path << "\n";
        return;
    }

    OrderBook ob;
    std::string line;
    int total_orders = 0, total_trades = 0;

    std::getline(f, line);

    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> cols;
        while (std::getline(ss, tok, ',')) cols.push_back(tok);
        if (cols.size() < 5) continue;

        uint64_t ts  = std::stoull(cols[0]);
        Side side    = (cols[1] == "BUY") ? Side::BUY : Side::SELL;
        double price = std::stod(cols[2]);
        int64_t qty  = std::stoll(cols[3]);
        uint64_t id  = std::stoull(cols[4]);

        Order o{id, side, OrderType::LIMIT,
                static_cast<int64_t>(price * 100.0 + 0.5), qty, ts};
        auto trades = ob.add_order(o);
        total_orders++;
        total_trades += static_cast<int>(trades.size());
    }

    std::cout << "replay complete: " << total_orders << " orders, "
              << total_trades << " trades\n";
    std::cout << "final book:\n";
    ob.print_book();
}

int main(int argc, char* argv[]) {
    if (argc >= 3 && std::string(argv[1]) == "--replay") {
        run_replay(argv[2]);
    } else {
        run_demo();
    }
}
