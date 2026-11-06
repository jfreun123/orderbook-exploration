// Times every message on its own and prints the latency distribution, in the
// style of the "OrderBook Latency Distribution" chart from David Gross's
// CppCon 2024 talk "When Nanoseconds Matter: Ultrafast Trading Systems in
// C++" (https://www.youtube.com/watch?v=sX2nF1fW7kI).
//
//   ./build-bench/benchmark/latency map               > map.csv
//   ./build-bench/benchmark/latency map --scatter     > map-scatter.csv
//
// stdout is a histogram, one row per (operation, nanosecond) bucket:
//   book,variant,op,ns,count
// stderr is a summary of percentiles per operation.
//
// --scatter fragments the heap before the book is built: it allocates a few
// million small blocks and frees a random half of them, so the allocator
// hands the book's nodes out from addresses all over memory instead of one
// after the other. A long-running process ends up like this anyway; a fresh
// benchmark process does not, and flatters every node-based container.
//
// Each message is timed with the CPU's timestamp counter (rdtsc, fenced),
// converted to nanoseconds by timing the counter against steady_clock. The
// cost of the timing itself is printed as `timer floor` and is included in
// every number.

// orderbook
#include "orderbook/array_book.hpp"
#include "orderbook/flow.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

// std
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// x86
#include <x86intrin.h>

namespace {

std::uint64_t ticks() {
  _mm_lfence();
  const std::uint64_t now = __rdtsc();
  _mm_lfence();
  return now;
}

// Counter ticks per nanosecond, measured over a fixed stretch of wall time.
double ticks_per_ns() {
  using Clock = std::chrono::steady_clock;
  const auto wall_start = Clock::now();
  const std::uint64_t tick_start = ticks();
  while (Clock::now() - wall_start < std::chrono::milliseconds(200)) {
  }
  const std::uint64_t tick_end = ticks();
  const auto wall =
      std::chrono::duration<double, std::nano>(Clock::now() - wall_start);
  return static_cast<double>(tick_end - tick_start) / wall.count();
}

// Allocates `blocks` small blocks and frees a random half. The other half is
// deliberately never freed: it is what keeps the free space in pieces.
void scatter_heap(std::uint64_t seed, std::size_t blocks) {
  std::mt19937_64 rng(seed);
  std::vector<void *> held(blocks);
  for (void *&block : held) {
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    block = std::malloc(16 + (rng() % 112));
  }
  for (std::size_t i = held.size() - 1; i > 0; --i) {
    std::swap(held[i], held[rng() % (i + 1)]);
  }
  for (std::size_t i = 0; i < held.size() / 2; ++i) {
    std::free(held[i]); // NOLINT(cppcoreguidelines-no-malloc)
  }
  held.resize(held.size() / 2);
  held.shrink_to_fit();
  // Deliberately leaked; see above.
  static std::vector<void *> keep;
  keep = std::move(held);
}

struct Options {
  std::string book;
  bool scatter = false;
  std::uint64_t seed = 1;
  std::size_t orders = 10'000;
  std::size_t messages = 2'000'000;
};

Options parse(std::span<char *> args) {
  if (args.size() < 2) {
    throw std::invalid_argument("missing book");
  }
  Options options{.book = args[1]};
  for (std::size_t i = 2; i < args.size(); ++i) {
    const std::string_view flag = args[i];
    if (flag == "--scatter") {
      options.scatter = true;
      continue;
    }
    if (i + 1 >= args.size()) {
      throw std::invalid_argument(std::format("{} needs a value", flag));
    }
    const std::string value = args[++i];
    if (flag == "--seed") {
      options.seed = std::stoull(value);
    } else if (flag == "--orders") {
      options.orders = std::stoul(value);
    } else if (flag == "--messages") {
      options.messages = std::stoul(value);
    } else {
      throw std::invalid_argument(std::format("unknown option {}", flag));
    }
  }
  return options;
}

constexpr std::array<std::string_view, 4> kOpNames{"add_limit", "add_market",
                                                   "cancel", "modify"};
constexpr std::size_t kMaxBucket = 2'000; // ns; slower lands in this bucket

template <class Book>
void measure(Book book, const Options &options, const ob::FlowConfig &config,
             double per_ns) {
  ob::Flow flow{config};
  const std::vector<ob::Event> warmup = flow.take(options.orders * 4);
  const std::vector<ob::Event> measured = flow.take(options.messages);

  ob::Fills fills;
  fills.reserve(1'024);
  for (const ob::Event &event : warmup) {
    fills.clear();
    ob::apply(book, event, fills);
  }

  std::vector<std::uint64_t> elapsed(measured.size());
  for (std::size_t i = 0; i < measured.size(); ++i) {
    fills.clear();
    const std::uint64_t start = ticks();
    ob::apply(book, measured[i], fills);
    elapsed[i] = ticks() - start;
  }

  const std::string_view variant = options.scatter ? "scattered" : "fresh";
  std::array<std::vector<std::uint32_t>, kOpNames.size()> by_op;
  for (std::size_t i = 0; i < measured.size(); ++i) {
    const auto nanos = static_cast<std::uint32_t>(
        std::lround(static_cast<double>(elapsed[i]) / per_ns));
    by_op.at(static_cast<std::size_t>(measured[i].type)).push_back(nanos);
  }

  std::cout << "book,variant,op,ns,count\n";
  for (std::size_t op = 0; op < by_op.size(); ++op) {
    std::vector<std::uint64_t> histogram(kMaxBucket + 1);
    for (const std::uint32_t nanos : by_op.at(op)) {
      ++histogram[std::min<std::size_t>(nanos, kMaxBucket)];
    }
    for (std::size_t bucket = 0; bucket < histogram.size(); ++bucket) {
      if (histogram[bucket] != 0) {
        std::cout << std::format("{},{},{},{},{}\n", options.book, variant,
                                 kOpNames.at(op), bucket, histogram[bucket]);
      }
    }
  }

  std::cerr << std::format("{} {} orders={} messages={} resting_at_end={}\n",
                           options.book, variant, options.orders,
                           options.messages, book.order_count());
  std::cerr << std::format("{:<11} {:>9} {:>6} {:>6} {:>6} {:>7} {:>8}\n", "op",
                           "count", "p50", "p90", "p99", "p99.9", "max");
  auto percentile = [](const std::vector<std::uint32_t> &sorted, double pct) {
    const auto rank = static_cast<std::size_t>(
        pct / 100.0 * static_cast<double>(sorted.size() - 1));
    return sorted[rank];
  };
  std::vector<std::uint32_t> all;
  for (std::size_t op = 0; op <= by_op.size(); ++op) {
    std::vector<std::uint32_t> &sorted = op < by_op.size() ? by_op.at(op) : all;
    if (op < by_op.size()) {
      all.insert(all.end(), sorted.begin(), sorted.end());
    }
    if (sorted.empty()) {
      continue;
    }
    std::ranges::sort(sorted);
    std::cerr << std::format("{:<11} {:>9} {:>6} {:>6} {:>6} {:>7} {:>8}\n",
                             op < by_op.size() ? kOpNames.at(op) : "all",
                             sorted.size(), percentile(sorted, 50),
                             percentile(sorted, 90), percentile(sorted, 99),
                             percentile(sorted, 99.9), sorted.back());
  }
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options =
        parse(std::span(argv, static_cast<std::size_t>(argc)));
    const double per_ns = ticks_per_ns();

    std::vector<std::uint64_t> floor(100'000);
    for (std::uint64_t &sample : floor) {
      const std::uint64_t start = ticks();
      sample = ticks() - start;
    }
    std::ranges::sort(floor);
    std::cerr << std::format(
        "timer floor: {:.1f} ns (median of an empty measurement)\n",
        static_cast<double>(floor[floor.size() / 2]) / per_ns);

    if (options.scatter) {
      scatter_heap(options.seed, 4'000'000);
    }
    const ob::FlowConfig config{.seed = options.seed,
                                .target_orders = options.orders};
    if (options.book == "naive") {
      measure(ob::NaiveBook{}, options, config, per_ns);
    } else if (options.book == "map") {
      measure(ob::MapBook{}, options, config, per_ns);
    } else if (options.book == "vector") {
      measure(ob::VectorBook{}, options, config, per_ns);
    } else if (options.book == "array") {
      measure(ob::ArrayBook{config.max_price}, options, config, per_ns);
    } else {
      throw std::invalid_argument("unknown book " + options.book);
    }
  } catch (const std::exception &e) {
    std::cerr << "latency: " << e.what() << "\n"
              << "usage: latency naive|map|vector|array [--scatter] [--seed N] "
                 "[--orders N] [--messages N]\n";
    return 1;
  }
  return 0;
}
