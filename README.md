# Matching Engine

A C++ limit order book with price-time priority, FIX 4.2 parsing, and a lock-free ingestion queue.

**13M inserts/sec · 6M matches/sec · 42ns p50 match latency** — Apple M1 Pro, Release build, single-threaded

## Performance

n=300k iterations per case, `-O3 -march=native`

| Case | Throughput | p50 | p95 | p99 | p99.9 |
|---|---|---|---|---|---|
| insert (no match) | 13M ops/s | 42 ns | 125 ns | 167 ns | 1292 ns |
| match (1:1 fill)  |  6M ops/s | 125 ns | 167 ns | 208 ns |  291 ns |
| sweep (5-level)   |  1M ops/s | 667 ns | 709 ns | 792 ns | 5000 ns |
| cancel            |  1M ops/s | 250 ns | 2000 ns | 4542 ns | 77000 ns |

The cancel tail is from scanning the price-level deque when orders share a price. An intrusive doubly-linked list would make cancel O(1) and kill the tail.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/bench/benchmark
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Matching Engine                   │
│                                                     │
│  ┌─────────────┐    ┌──────────────────────────┐    │
│  │ FIX Parser  │───▶│    Lock-free SPSC Queue  │    │
│  └─────────────┘    └────────────┬─────────────┘    │
│  ┌─────────────┐                 │                  │
│  │Market Replay│─────────────────┤                  │
│  └─────────────┘                 ▼                  │
│                       ┌──────────────────┐          │
│                       │    Order Book    │          │
│                       │  ┌────┐  ┌────┐  │          │
│                       │  │Bid │  │Ask │  │          │
│                       │  └────┘  └────┘  │          │
│                       └────────┬─────────┘          │
│                                ▼                    │
│                    ┌───────────────────┐            │
│                    │  Matching Core    │            │
│                    └─────┬───────┬─────┘            │
│              ┌───────────┘       └──────────┐       │
│              ▼                              ▼       │
│   ┌──────────────────┐         ┌──────────────────┐ │
│   │  Trade Reporter  │         │    Benchmark     │ │
│   └──────────────────┘         │     Harness      │ │
│                                └──────────────────┘ │
└─────────────────────────────────────────────────────┘
```

## Features

- Price-time priority — best price wins, ties broken by arrival order (FIFO per level)
- Limit and market orders — market orders use a sentinel price that sweeps all available liquidity
- Lock-free SPSC queue — ring buffer between parser/network thread and matching thread, no mutexes on the hot path
- FIX 4.2 parser — decodes `NewOrderSingle` (35=D) and `OrderCancelRequest` (35=F), returns `std::variant<FIXNewOrder, FIXCancelRequest>`
- Cancel index — `unordered_map<order_id, {side, price}>` for O(1) lookup before the deque scan
- Market replay — reads historical tick CSV and replays against a live book
- 20 unit tests — price priority, time priority, partial fills, multi-level sweeps, market orders, cancel, SPSC queue, FIX round-trips

## Design decisions

**Integer prices, not floats**

Prices are `int64_t` in fixed-point cents ($150.05 → 15005). Floating-point errors are cumulative across millions of operations and have caused real production incidents. IEEE 754 doubles have 53 bits of mantissa — at typical equity prices that means sub-cent errors that compound. Every exchange I'm aware of uses integer fixed-point internally.

**Lock-free SPSC queue**

A mutex-based queue incurs a kernel context switch on every push — roughly 1–10µs. The SPSC lock-free version stays entirely in userspace. Head and tail are on separate 64-byte cache lines to prevent false sharing: without the padding, every push invalidates the consumer's cache line and vice versa, effectively serialising the threads.

This follows Dmitry Vyukov's SPSC queue design. With a single producer and single consumer you never need more than acquire/release — seq_cst adds unnecessary fence instructions on ARM.

**`std::map` for price levels**

`std::map<int64_t, std::deque<Order>>` gives O(log n) insertion and O(1) best-price access via `begin()`. The tradeoff is pointer chasing through the red-black tree on every level traversal. A flat sorted array would be more cache-friendly since the number of distinct prices is usually small. I started with `std::map` to get the matching logic right before optimising.

**Cancel index**

Most matching engine writeups skip the cancel path, but in practice cancel rates can easily exceed new order rates (20:1 cancel-to-trade ratios are common in equities). The hash map gives O(1) lookup to find the right price level, then O(k) to scan the deque. In the benchmark the p99 tail on cancel comes from that scan when many orders share a price.

## Build

Requirements: C++20, CMake 3.20+

```bash
git clone https://github.com/benduncanson/matching-engine
cd matching-engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run tests:
```bash
./build/tests/order_book_test
```

Run benchmark:
```bash
./build/bench/benchmark
```

Run market replay:
```bash
./build/src/matching_engine --replay data/sample_ticks.csv
```

Debug build (AddressSanitizer + UBSan):
```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
./build-debug/tests/order_book_test
```

## Project structure

```
matching-engine/
├── include/
│   ├── order.h           # Order struct + Side/OrderType enums
│   ├── trade.h           # Trade (fill) struct
│   ├── order_book.h      # OrderBook class
│   ├── spsc_queue.h      # Lock-free ring buffer
│   └── fix_parser.h      # FIX 4.2 message types + parser
├── src/
│   ├── order_book.cpp    # Matching core + cancel
│   ├── fix_parser.cpp    # Tag extraction + message decode
│   └── main.cpp          # Demo mode + --replay mode
├── tests/
│   └── order_book_test.cpp   # 20 unit tests (no external framework)
├── bench/
│   └── benchmark.cpp     # p50/p95/p99/p99.9 latency harness
├── data/
│   └── sample_ticks.csv  # Sample tick data for replay
└── CMakeLists.txt
```

## What I'd do next

- Intrusive linked list at each price level — eliminates the O(k) cancel scan
- Pool allocator — `std::deque` and `std::map` nodes hit the general allocator on every order; a slab allocator would shave 30–50ns off the insert path
- Multi-symbol routing — `robin_hood::unordered_map<Symbol, OrderBook>` dispatching to per-symbol books on their own threads
- Top-of-book cache — cache `best_bid`/`best_ask` as atomics so market data subscribers don't contend with the matching thread
- Full FIX session layer — logon/logout, heartbeat (35=0), sequence numbers and gap-fill

## References

- Nasdaq TotalView-ITCH 5.0 spec — canonical description of how a real exchange encodes its order book feed
- [FIX Protocol specification](https://www.fixtrading.org/standards/) — FIX 4.2 message reference
- [LOBSTER market data](https://lobsterdata.com/) — reconstructed limit order book data for realistic replay testing
- Dmitry Vyukov, *Single-Producer Single-Consumer Queue* (2010)
