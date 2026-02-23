# Matching Engine

A high-performance C++ limit order book matching engine built for ultra-low latency order processing - the core infrastructure behind every financial exchange.

> **[X.X]M orders/sec** throughput · **[XXX]ns p99 latency** · price-time priority · FIX protocol · lock-free ingestion

---

## Performance

Benchmarked on Apple M-series / Intel i7 (fill in your hardware), single-threaded matching loop, pinned to core 0.

| Metric         | Result     |
|----------------|------------|
| Throughput     | [X.X]M ops/s |
| p50 latency    | [XXX]ns    |
| p95 latency    | [XXX]ns    |
| p99 latency    | [XXX]ns    |
| p99.9 latency  | [XXX]ns    |

Run the benchmark yourself:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bench/benchmark
```

---

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

---

## Features

- **Price-time priority matching** - orders matched at best available price; ties broken by arrival time
- **Limit and market orders** - limit orders rest in the book; market orders sweep available liquidity
- **Lock-free SPSC ring buffer** - single-producer single-consumer queue between ingestion and matching threads; zero mutex contention on the hot path
- **FIX protocol parser** - parses New Order Single (D) and Cancel Order (F) message types
- **Market replay mode** - ingests historical tick data (CSV) and simulates order execution against real market sequences
- **Latency harness** - measures p50/p95/p99/p99.9 using `std::chrono::steady_clock` with nanosecond resolution

---

## Design Decisions

### Integer prices, not floats
All prices are stored as 64-bit integers representing fixed-point values (e.g. price in cents). Floating-point arithmetic introduces representation errors that compound across millions of operations - a well-known source of bugs in financial systems. Every real exchange uses fixed-point arithmetic internally.

### Lock-free SPSC queue
The queue between the network/parser thread and the matching thread uses a single-producer single-consumer ring buffer backed by `std::atomic` with `memory_order_acquire/release` semantics. A mutex-based queue would force a kernel context switch on every order - costing ~1–10µs per operation. The lock-free alternative keeps everything in userspace and avoids false sharing by padding the head and tail pointers to separate cache lines.

### `std::map` for price levels
The order book uses `std::map<int64_t, std::deque<Order>>` for both the bid and ask sides. This gives O(log n) insertion and O(1) best-price access (via `begin()`/`rbegin()`). The tradeoff versus a flat array or custom structure is simplicity and correctness first - a natural next step would be replacing this with a more cache-friendly structure for the top-of-book levels.

### CPU affinity
The matching thread is pinned to a single physical core via `pthread_setaffinity_np`. This eliminates OS-level context switching and ensures the L1/L2 caches stay warm for the order book data. Without pinning, benchmark results are noisy and p99 numbers are significantly worse.

---

## Build

**Requirements:** C++20, CMake 3.20+

```bash
git clone https://github.com/benduncanson/matching-engine
cd matching-engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Run tests:**
```bash
./build/tests/order_book_test
```

**Run benchmark:**
```bash
./build/bench/benchmark
```

**Run market replay:**
```bash
./build/src/matching_engine --replay data/sample_ticks.csv
```

---

## Project Structure

```
matching-engine/
├── include/
│   ├── order.h           # Order and Fill structs
│   ├── order_book.h      # OrderBook class
│   ├── spsc_queue.h      # Lock-free ring buffer
│   └── fix_parser.h      # FIX protocol parser
├── src/
│   ├── order_book.cpp
│   ├── fix_parser.cpp
│   └── main.cpp
├── tests/
│   └── order_book_test.cpp
├── bench/
│   └── benchmark.cpp
├── data/
│   └── sample_ticks.csv  # Sample tick data for replay
└── CMakeLists.txt
```

---

## What I'd do next

- **Custom allocator** - replace `new`/`delete` on the order path with a pool allocator to eliminate heap allocation latency
- **Intrusive linked lists** - replace `std::deque` at each price level with an intrusive list to improve cache locality
- **Multi-symbol support** - route orders to per-symbol books via a symbol table; each book runs on its own thread
- **Full FIX session layer** - add logon/logout, heartbeat, and sequence number tracking for a complete FIX 4.2 session

---

## References

- [LOBSTER tick data](https://lobsterdata.com/) - source for sample market replay data
- [Fix Protocol spec](https://www.fixtrading.org/standards/) - FIX 4.2 message reference
- Corbet & Gregg, *Linux kernel lock-free data structures*
- Nasdaq TotalView-ITCH 5.0 specification