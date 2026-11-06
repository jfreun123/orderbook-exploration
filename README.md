# orderbook-exploration

Building a limit order book several ways, to learn what makes one fast. Built from [cpp-starter](https://github.com/jfreun123/cpp-starter), like [C1M](https://github.com/jfreun123/C1M).

## Goals

- [x] Build one limit order book with price-time priority: add, cancel, modify, and execution against the other side, with the matching rules written down and tested
- [ ] Build the same book several ways so the data structures can be compared like for like: `std::map` of price levels, a sorted vector, an array indexed by tick; orders at a level in a `std::list`, an intrusive list, a ring buffer
- [ ] Measure every version: nanoseconds per add, cancel and execution, memory per resting order, and how each changes with the number of levels and the number of orders at a level
- [ ] Replay a real feed through each book and check that they agree with each other and with the published book
- [ ] Reproduce the slow cases and diagnose them with standard tools (`perf`, cachegrind, a benchmark that isolates one operation): a level holding thousands of orders, a cancel of the oldest order, a sweep through many levels, a book that grows deep
- [ ] Be able to answer these from code and measurements in this repo:
  - [ ] What is in an order book, and what does price-time priority mean in practice?
  - [ ] What happens, step by step, when a market order arrives? A limit order that crosses? A cancel of an order that has partly filled?
  - [ ] Which operations does a matching engine spend its time on, and which data structure fits each one?
  - [ ] Why is `std::map` the obvious choice for price levels, and when is it the wrong one?
  - [ ] Design an order book for one instrument at a million messages a second: what is stored, where, and why

## The books

Every order-by-order book implements the same calls (`add_limit`, `add_market`, `cancel`, `modify`) under the same rules, written down in [docs/matching-rules.md](docs/matching-rules.md). Each header starts with a comment explaining its design.

| Book | Price levels | Orders at a level | Find an order by id |
|---|---|---|---|
| [`leetcode::Solution`](source/orderbook/leetcode_backlog.hpp) | `std::map` of price to total quantity | not kept: no time priority | not possible: no cancel |
| [`NaiveBook`](source/orderbook/naive_book.hpp) | none: one vector of orders per side, best at the back | (same vector) | linear scan |
| [`MapBook`](source/orderbook/map_book.hpp) | `std::map` | `std::list` | `std::unordered_map` to list iterator |
| [`VectorBook`](source/orderbook/vector_book.hpp) | sorted vector, best at the back, linear search from the back | intrusive list in a pool ([`OrderPool`](source/orderbook/order_pool.hpp)) | `std::unordered_map` to pool index |
| [`ArrayBook`](source/orderbook/array_book.hpp) | array indexed by tick | intrusive list in a pool | `std::unordered_map` to pool index |

`VectorBook` is the "reverse vector" from David Gross's CppCon 2024 talk [When Nanoseconds Matter: Ultrafast Trading Systems in C++](https://www.youtube.com/watch?v=sX2nF1fW7kI).

[`level_book.hpp`](source/orderbook/level_book.hpp) holds two **market-by-price** books (`MapLevelBook`, `VectorLevelBook`), the kind from that talk: price to total quantity, no individual orders. They exist for the comparison below.

## Running everything

Everything runs in the pinned toolchain image (`Dockerfile`) through `scripts/docker.sh`, so every number here comes from a command you can repeat. Measurements are pinned to one core.

```bash
# Build and test (Debug, clang-tidy, cppcheck)
scripts/docker.sh bash -c 'cmake --preset dev && cmake --build --preset dev && ctest --preset dev'

# AddressSanitizer
scripts/docker.sh bash -c 'cmake --preset asan && cmake --build --preset asan && ctest --preset asan'

# Optimized build for measurements
scripts/docker.sh bash -c 'cmake --preset bench && cmake --build --preset bench'

# Throughput: average ns per message, every book at three sizes -> results/throughput.txt
scripts/docker.sh --cpuset-cpus=2 -- ./build-bench/benchmark/benchmark

# Latency: every message timed on its own -> results/latency-*.{csv,txt,png}
scripts/docker.sh --cpuset-cpus=2 -- scripts/latency.sh

# The market-by-price comparison -> results/levels-*.{csv,txt,png}
scripts/docker.sh --cpuset-cpus=2 -- scripts/latency_levels.sh

# Replay: a JSON trace of every call and the book after it, for viz/index.html
scripts/docker.sh ./build/app/replay --book array --messages 600 > viz/trace.json
python3 -m http.server -d viz   # then open http://localhost:8000

# Formatting
scripts/docker.sh cmake --build build -t format-check
scripts/docker.sh cmake --build build -t format-fix
```

The message stream is synthetic ([`flow.hpp`](source/orderbook/flow.hpp)): seeded limit orders, market orders, cancels and modifies around a drifting mid price. It is the same on every machine and every standard library, so every book sees exactly the same messages. [`agreement_test.cpp`](test/source/agreement_test.cpp) feeds it to all four books and checks that they answer every message the same way.

## Results

From the files in `results/`, on a 13th Gen Intel i7-13700KF under WSL2, one core.

### Throughput

Average ns per message, book filled to `target_orders` first (`results/throughput.txt`):

| book | 1k orders | 10k orders | 100k orders |
|---|---|---|---|
| naive | 316 | 842 | 3,637 |
| map | 38 | 39 | 39 |
| vector | 38 | 38 | 38 |
| array | 29 | 30 | 29 |

### Latency per message

`benchmark/latency` times every message with `rdtsc`. Besides the book, two things change what it measures:

- `--scatter` fragments the heap before the book is built, the way a long-running process ends up. A fresh process hands out nodes one after the other, which flatters every node-based container.
- `--evict 4096` reads 4 MB of other memory between messages, outside the timed part, the way the rest of a trading system would. Without it the whole book stays in L1/L2 and the layout of its nodes barely matters.

Median / p90 per message, ns (`results/latency-*.txt`):

| book | hot, fresh heap | hot, scattered heap | out of cache, fresh heap | out of cache, scattered heap |
|---|---|---|---|---|
| map | 55 / 103 | 80 / 253 | 297 / 734 | 573 / 1,683 |
| vector | 50 / 100 | 76 / 254 | 267 / 716 | 391 / 1,414 |
| array | 42 / 77 | 55 / 216 | 264 / 719 | 365 / 1,352 |

`results/latency-map.png` is the chart from the talk reproduced: once the heap is scattered, the map's median moves from 55 to 80 ns and its tail stretches far past 200 ns. The vector and array books suffer too, because all three still find an order by id through a `std::unordered_map`: with the book out of cache they sit within a few percent of each other on a fresh heap. That per-order index is the next thing to make contiguous.

### std::map vs the reverse vector

Gross's book (`AddOrder(Side, Price, Volume)`) is market-by-price: one quantity per price, no orders. Finding the level is the whole update, so the level container decides the latency. [`level_flow.hpp`](source/orderbook/level_flow.hpp) feeds [`level_book.hpp`](source/orderbook/level_book.hpp) 1,000 levels a side with 90% of updates within 8 ticks of the top.

Median / p90 per update, ns (`results/levels-*.txt`):

| | hot, fresh heap | out of cache, fresh heap | out of cache, scattered heap |
|---|---|---|---|
| `std::map` | 51 / 76 | 232 / 467 | 294 / 1,391 |
| reverse vector, linear search | 26 / 49 | 79 / 207 | 79 / 212 |
| reverse vector, binary search | 36 / 48 | 93 / 167 | 94 / 174 |

- Hot in cache the vector already wins the median, 26 vs 51 ns, but the gap opens once the book is pushed out of cache: then every map node is a likely cache miss and a scattered heap makes it worse, while the vector's levels stay contiguous and do not care where the heap put them.
- Linear search from the back wins the median because most updates land in the last few slots, but its tail is long when an update lands deep in the book (p99 446 ns hot); binary search keeps the tail short (p99 185 ns).

`results/levels-cold.png` is the closest chart to the one in the talk.

## Project structure

```
orderbook-exploration/
├── source/orderbook/        # Header-only library: every book, the two flow generators
├── test/source/             # Google Test: LeetCode examples, matching rules, books agree
├── benchmark/source/        # benchmark (throughput), latency (per-message histogram)
├── app/source/replay.cpp    # Writes a JSON trace for the viewer
├── viz/index.html           # Plays a trace back as a price ladder, with a plain-English story
├── scripts/                 # docker.sh, latency.sh, latency_levels.sh, plot_latency.py
├── docs/matching-rules.md   # The rules every book follows
├── results/                 # Output of the runs quoted above
└── Dockerfile               # Pinned toolchain
```

## Prerequisites outside Docker

Ubuntu/Debian: `sudo apt-get install cmake clang clang-tidy cppcheck ninja-build python3-matplotlib`. The CMake presets (`dev`, `asan`, `tsan`, `bench`) work the same way natively.
