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

All four implement the same calls (`add_limit`, `add_market`, `cancel`,
`modify`) under the same rules, written down in
[docs/matching-rules.md](docs/matching-rules.md).

| Book | Price levels | Orders at a level | Find an order by id |
|---|---|---|---|
| [`leetcode::Solution`](source/orderbook/leetcode_backlog.hpp) | `std::map` of price to total quantity | not kept: no time priority | not possible: no cancel |
| [`NaiveBook`](source/orderbook/naive_book.hpp) | none: one vector of orders per side, best at the back | (same vector) | linear scan |
| [`MapBook`](source/orderbook/map_book.hpp) | `std::map` | `std::list` | `std::unordered_map` to list iterator |
| [`VectorBook`](source/orderbook/vector_book.hpp) | sorted vector, best at the back, linear search from the back | intrusive list in a pool ([`OrderPool`](source/orderbook/order_pool.hpp)) | `std::unordered_map` to pool index |
| [`ArrayBook`](source/orderbook/array_book.hpp) | array indexed by tick | intrusive list in a pool | `std::unordered_map` to pool index |

`VectorBook` is the "reverse vector" from David Gross's CppCon 2024 talk
[When Nanoseconds Matter: Ultrafast Trading Systems in C++](https://www.youtube.com/watch?v=sX2nF1fW7kI).

## Running everything

Everything runs in the pinned toolchain image (`Dockerfile`), through
`scripts/docker.sh`, so every number here comes from a command you can repeat:

```bash
# Build and test (Debug, clang-tidy, cppcheck)
scripts/docker.sh bash -c 'cmake --preset dev && cmake --build --preset dev && ctest --preset dev'

# Optimized build for measurements
scripts/docker.sh bash -c 'cmake --preset bench && cmake --build --preset bench'

# Throughput: average ns per message, every book at three sizes
scripts/docker.sh --cpuset-cpus=2 -- ./build-bench/benchmark/benchmark

# Latency: every message timed on its own, on a fresh heap and a scattered
# one, plotted into results/latency-*.png
scripts/docker.sh --cpuset-cpus=2 -- scripts/latency.sh

# Replay: a JSON trace of every call and the book after it, for viz/index.html
scripts/docker.sh ./build/app/replay --book array --messages 600 > viz/trace.json
python3 -m http.server -d viz   # then open http://localhost:8000
```

The message stream is synthetic ([`flow.hpp`](source/orderbook/flow.hpp)):
seeded adds, cancels, modifies and market orders around a drifting mid
price. It is the same on every machine and every standard library, so every
book sees exactly the same messages.

## Prerequisites (outside Docker)

Install on macOS (Homebrew):
```bash
brew install cmake llvm cppcheck ninja
# llvm is keg-only; symlink clang-tidy into PATH:
ln -sf /usr/local/opt/llvm/bin/clang-tidy /usr/local/bin/clang-tidy
```

Install on Ubuntu/Debian:
```bash
sudo apt-get install cmake clang clang-tidy cppcheck ninja-build
```

## Sanitizers

```bash
scripts/docker.sh bash -c 'cmake --preset asan && cmake --build --preset asan && ctest --preset asan'
```

## Code Formatting

```bash
cmake --build build -t format-check
cmake --build build -t format-fix
```

## Project Structure

```
orderbook-exploration/
├── source/orderbook/        # Header-only library: every book, the flow generator
├── test/source/             # Google Test: LeetCode examples, matching rules, books agree
├── benchmark/source/        # benchmark (throughput), latency (per-message histogram)
├── app/source/replay.cpp    # Writes a JSON trace for the viewer
├── viz/index.html           # Plays a trace back as a price ladder
├── scripts/                 # docker.sh, latency.sh, plot_latency.py
├── docs/matching-rules.md   # The rules every book follows
├── results/                 # Output of the runs quoted in the blog
└── Dockerfile               # Pinned toolchain
```
