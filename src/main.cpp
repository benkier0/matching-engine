#include <trade.h>
#include <order.h>

int main() {
    Order o{1, Side::BUY, 1000, 50, 123456789};
    o.print();

    Trade t{1, 2, 1000, 50, 123456790};
    t.print();

    return 0;
}