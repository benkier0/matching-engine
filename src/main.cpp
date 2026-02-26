#include "order_book.h"

int main() {
    OrderBook ob;
    ob.add_order({1, Side::BUY, 1000, 10, 1});
    ob.add_order({2, Side::BUY, 1010, 5, 2});
    ob.add_order({3, Side::SELL, 1020, 3, 3});
    ob.print_book();

    auto bid = ob.best_bid();
    auto ask = ob.best_ask();
    std::cout << "Best Bid: " << (bid ? std::to_string(*bid) : "none") << "\n";
    std::cout << "Best Ask: " << (ask ? std::to_string(*ask) : "none") << "\n";
}