// Times every message on its own and prints the latency distribution, in the
// style of the "OrderBook Latency Distribution" chart from David Gross's
// CppCon 2024 talk "When Nanoseconds Matter: Ultrafast Trading Systems in
// C++" (https://www.youtube.com/watch?v=sX2nF1fW7kI).
//
//   latency BOOK [--scatter] [--evict KB] [--seed N] [--messages N]
//                [--orders N]   for naive|map|vector|array
//                [--levels N]   for
//                map-levels|vector-levels|vector-levels-binary
//
// The order-by-order books get the order flow from flow.hpp, warmed up until
// about --orders are resting. The market-by-price books get level updates
// from level_flow.hpp on a book --levels deep per side, settled for one
// round of updates before timing starts. stdout is a histogram, one row per
// (operation, nanosecond) bucket, as book,variant,op,ns,count; anything
// slower than kMaxBucket lands in that last bucket. stderr is a summary of
// percentiles per operation.
//
// --scatter fragments the heap before the book is built: it allocates a few
// million small blocks and frees a random half of them, so the allocator
// hands the book's nodes out from addresses all over memory instead of one
// after the other. A long-running process ends up like this anyway; a fresh
// benchmark process does not, and flatters every node-based container. The
// other half of the blocks is deliberately never freed: it is what keeps the
// free space in pieces.
//
// --evict KB reads that much unrelated memory between messages, outside the
// timed region, one load per cache line, standing in for the rest of a
// trading system: by the next message, part of the book has been pushed out
// of cache. Without it the whole book stays in L1/L2 and the layout of its
// nodes barely matters.
//
// Each message is timed with the CPU's timestamp counter (rdtsc, fenced),
// converted to nanoseconds by timing the counter against steady_clock over
// a fixed stretch of wall time. The cost of the timing itself is printed as
// `timer floor` and is included in every number.

#include "orderbook/array_book.hpp"
#include "orderbook/flow.hpp"
#include "orderbook/level_book.hpp"
#include "orderbook/level_flow.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

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
#include <utility>
#include <vector>

#include <x86intrin.h>

namespace {

constexpr std::string_view kUsage =
    "usage: latency BOOK [--scatter] [--evict KB] [--seed N] [--messages N]\n"
    "               [--orders N]   for naive|map|vector|array\n"
    "               [--levels N]   for map-levels|vector-levels|"
    "vector-levels-binary\n";

constexpr std::array<std::string_view, 4> kOpNames{"add_limit", "add_market",
                                                   "cancel", "modify"};
constexpr std::size_t kMaxBucket = 2'000;
constexpr std::size_t kScatterBlocks = 4'000'000;

struct Options {
  std::string book;
  bool scatter = false;
  std::uint64_t seed = 1;
  std::size_t messages = 2'000'000;
  std::size_t evict_kb = 0;
  std::size_t orders = 10'000;
  ob::Price levels = 1'000;
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
    } else if (flag == "--messages") {
      options.messages = std::stoul(value);
    } else if (flag == "--evict") {
      options.evict_kb = std::stoul(value);
    } else if (flag == "--orders") {
      options.orders = std::stoul(value);
    } else if (flag == "--levels") {
      options.levels = std::stoll(value);
    } else {
      throw std::invalid_argument(std::format("unknown option {}", flag));
    }
  }
  return options;
}

std::uint64_t ticks() {
  _mm_lfence();
  const std::uint64_t now = __rdtsc();
  _mm_lfence();
  return now;
}

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

double timer_floor_ns(double per_ns) {
  std::vector<std::uint64_t> samples(100'000);
  for (std::uint64_t &sample : samples) {
    const std::uint64_t start = ticks();
    sample = ticks() - start;
  }
  std::ranges::sort(samples);
  return static_cast<double>(samples[samples.size() / 2]) / per_ns;
}

void scatter_heap(std::uint64_t seed, std::size_t blocks) {
  std::mt19937_64 rng(seed);
  std::vector<void *> held(blocks);
  for (void *&block : held) {
    block = std::malloc(16 + (rng() % 112));
  }
  for (std::size_t i = held.size() - 1; i > 0; --i) {
    std::swap(held[i], held[rng() % (i + 1)]);
  }
  for (std::size_t i = 0; i < held.size() / 2; ++i) {
    std::free(held[i]);
  }
  held.resize(held.size() / 2);
  held.shrink_to_fit();
  static std::vector<void *> leaked;
  leaked = std::move(held);
}

class Evictor {
public:
  explicit Evictor(std::size_t kilobytes)
      : buffer_(kilobytes * 1'024 / sizeof(std::uint64_t), 1) {}

  void run() {
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < buffer_.size(); i += kLineWords) {
      sum += buffer_[i];
    }
    sink_ = sum;
  }

private:
  static constexpr std::size_t kLineWords = 64 / sizeof(std::uint64_t);
  std::vector<std::uint64_t> buffer_;
  volatile std::uint64_t sink_ = 0;
};

using Samples =
    std::vector<std::pair<std::string_view, std::vector<std::uint32_t>>>;

std::uint32_t to_ns(std::uint64_t ticks_elapsed, double per_ns) {
  return static_cast<std::uint32_t>(
      std::lround(static_cast<double>(ticks_elapsed) / per_ns));
}

std::uint32_t percentile(const std::vector<std::uint32_t> &sorted, double pct) {
  const auto rank = static_cast<std::size_t>(
      pct / 100.0 * static_cast<double>(sorted.size() - 1));
  return sorted[rank];
}

void report(const Options &options, std::string_view description,
            Samples samples) {
  const std::string_view variant = options.scatter ? "scattered" : "fresh";
  std::cout << "book,variant,op,ns,count\n";
  for (const auto &[name, nanos] : samples) {
    std::vector<std::uint64_t> histogram(kMaxBucket + 1);
    for (const std::uint32_t sample : nanos) {
      ++histogram[std::min<std::size_t>(sample, kMaxBucket)];
    }
    for (std::size_t bucket = 0; bucket < histogram.size(); ++bucket) {
      if (histogram[bucket] != 0) {
        std::cout << std::format("{},{},{},{},{}\n", options.book, variant,
                                 name, bucket, histogram[bucket]);
      }
    }
  }

  std::cerr << std::format("{} {} {}\n", options.book, variant, description);
  std::cerr << std::format("{:<11} {:>9} {:>6} {:>6} {:>6} {:>7} {:>8}\n", "op",
                           "count", "p50", "p90", "p99", "p99.9", "max");
  auto print = [](std::string_view name, std::vector<std::uint32_t> &sorted) {
    if (sorted.empty()) {
      return;
    }
    std::ranges::sort(sorted);
    std::cerr << std::format("{:<11} {:>9} {:>6} {:>6} {:>6} {:>7} {:>8}\n",
                             name, sorted.size(), percentile(sorted, 50),
                             percentile(sorted, 90), percentile(sorted, 99),
                             percentile(sorted, 99.9), sorted.back());
  };
  std::vector<std::uint32_t> all;
  for (auto &[name, nanos] : samples) {
    all.insert(all.end(), nanos.begin(), nanos.end());
    print(name, nanos);
  }
  if (samples.size() > 1) {
    print("all", all);
  }
}

template <class Book>
void measure(Book book, const Options &options, double per_ns) {
  ob::Flow flow{{.seed = options.seed, .target_orders = options.orders}};
  const std::vector<ob::Event> warmup = flow.take(options.orders * 4);
  const std::vector<ob::Event> measured = flow.take(options.messages);

  ob::Fills fills;
  fills.reserve(1'024);
  for (const ob::Event &event : warmup) {
    fills.clear();
    ob::apply(book, event, fills);
  }

  Evictor evictor{options.evict_kb};
  std::vector<std::uint64_t> elapsed(measured.size());
  for (std::size_t i = 0; i < measured.size(); ++i) {
    fills.clear();
    evictor.run();
    const std::uint64_t start = ticks();
    ob::apply(book, measured[i], fills);
    elapsed[i] = ticks() - start;
  }

  Samples samples;
  for (const std::string_view name : kOpNames) {
    samples.emplace_back(name, std::vector<std::uint32_t>{});
  }
  for (std::size_t i = 0; i < measured.size(); ++i) {
    samples.at(static_cast<std::size_t>(measured[i].type))
        .second.push_back(to_ns(elapsed[i], per_ns));
  }
  report(options,
         std::format("orders={} messages={} evict_kb={} resting_at_end={}",
                     options.orders, options.messages, options.evict_kb,
                     book.order_count()),
         std::move(samples));
}

template <class Book>
void measure_levels(Book book, const Options &options, double per_ns) {
  ob::LevelFlow flow{{.seed = options.seed, .levels = options.levels}};
  for (const auto &[side, price, delta] : flow.fill()) {
    book.update(side, price, delta);
  }
  for (const auto &[side, price, delta] : flow.take(options.messages)) {
    book.update(side, price, delta);
  }
  const std::vector<ob::LevelUpdate> measured = flow.take(options.messages);

  Evictor evictor{options.evict_kb};
  std::vector<std::uint64_t> elapsed(measured.size());
  for (std::size_t i = 0; i < measured.size(); ++i) {
    const auto &[side, price, delta] = measured[i];
    evictor.run();
    const std::uint64_t start = ticks();
    book.update(side, price, delta);
    elapsed[i] = ticks() - start;
  }

  Samples samples{{"update", {}}};
  for (const std::uint64_t sample : elapsed) {
    samples[0].second.push_back(to_ns(sample, per_ns));
  }
  const auto bids = book.levels(ob::Side::Buy).size();
  const auto asks = book.levels(ob::Side::Sell).size();
  report(options,
         std::format("levels={} updates={} evict_kb={} levels_at_end={}+{}",
                     options.levels, options.messages, options.evict_kb, bids,
                     asks),
         std::move(samples));
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options =
        parse(std::span(argv, static_cast<std::size_t>(argc)));
    const double per_ns = ticks_per_ns();
    std::cerr << std::format(
        "timer floor: {:.1f} ns (median of an empty measurement)\n",
        timer_floor_ns(per_ns));

    if (options.scatter) {
      scatter_heap(options.seed, kScatterBlocks);
    }
    if (options.book == "naive") {
      measure(ob::NaiveBook{}, options, per_ns);
    } else if (options.book == "map") {
      measure(ob::MapBook{}, options, per_ns);
    } else if (options.book == "vector") {
      measure(ob::VectorBook{}, options, per_ns);
    } else if (options.book == "array") {
      measure(ob::ArrayBook{ob::FlowConfig{}.max_price}, options, per_ns);
    } else if (options.book == "map-levels") {
      measure_levels(ob::MapLevelBook{}, options, per_ns);
    } else if (options.book == "vector-levels") {
      measure_levels(ob::VectorLevelBook<ob::Search::Linear>{}, options,
                     per_ns);
    } else if (options.book == "vector-levels-binary") {
      measure_levels(ob::VectorLevelBook<ob::Search::Binary>{}, options,
                     per_ns);
    } else {
      throw std::invalid_argument("unknown book " + options.book);
    }
  } catch (const std::exception &e) {
    std::cerr << "latency: " << e.what() << "\n" << kUsage;
    return 1;
  }
  return 0;
}
