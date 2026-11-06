# orderbook-exploration

Building a limit order book several ways, to learn what makes one fast. Built from [cpp-starter](https://github.com/jfreun123/cpp-starter), like [C1M](https://github.com/jfreun123/C1M).

## Goals

- [ ] Build one limit order book with price-time priority: add, cancel, modify, and execution against the other side, with the matching rules written down and tested
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

## Prerequisites

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

## Building and Testing

```bash
# Configure (Debug build with static analysis)
cmake --preset=dev

# Build
cmake --build --preset=dev

# Run tests
ctest --preset=dev
```

## Sanitizers

```bash
# AddressSanitizer
cmake --preset=asan && cmake --build --preset=asan && ctest --preset=asan

# ThreadSanitizer
cmake --preset=tsan && cmake --build --preset=tsan && ctest --preset=tsan
```

## Benchmark

```bash
cmake --preset=bench
cmake --build --preset=bench
./build-bench/benchmark/benchmark
```

## Code Formatting

```bash
# Check formatting
cmake --build build -t format-check

# Fix formatting
cmake --build build -t format-fix
```

## Project Structure

```
orderbook-exploration/
├── source/                  # Header-only library
│   ├── CMakeLists.txt
│   └── add.hpp              # Replace with your headers
├── test/                    # Unit tests (Google Test)
│   ├── CMakeLists.txt
│   └── source/
│       └── add_test.cpp
├── benchmark/               # Benchmarks
│   ├── CMakeLists.txt
│   └── source/
│       └── benchmark.cpp
├── cmake/                   # CMake modules
│   ├── dev-mode.cmake
│   ├── gtest.cmake
│   └── lint-targets.cmake
├── .clang-tidy
├── .clang-format
├── CMakeLists.txt
└── CMakePresets.json        # dev, asan, tsan, bench presets
```
