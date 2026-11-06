// Replays one generated message stream through each order-by-order book and
// reports the average time per message once the book has filled up.
//
//   benchmark                       every book at 1k, 10k and 100k orders
//   benchmark map 100000 1000000    one book, one size, one message count
//
// The stream is generated before the clock starts, and the book is filled to
// its working size before the clock starts (four times target_orders
// messages, long enough to reach the target and settle), so the number is
// the cost of the book alone at that size. The naive book is O(orders) per
// message, so the all-books run gives it fewer messages to keep its biggest
// run to seconds.

#include "orderbook/array_book.hpp"
#include "orderbook/flow.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Result {
  double ns_per_message;
  std::size_t resting;
  std::size_t fills;
};

template <class Book>
Result run(Book book, std::span<const ob::Event> warmup,
           std::span<const ob::Event> measured) {
  ob::Fills fills;
  for (const ob::Event &event : warmup) {
    fills.clear();
    ob::apply(book, event, fills);
  }
  std::size_t fill_count = 0;
  const auto start = std::chrono::steady_clock::now();
  for (const ob::Event &event : measured) {
    fills.clear();
    ob::apply(book, event, fills);
    fill_count += fills.size();
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double nanos =
      std::chrono::duration<double, std::nano>(elapsed).count() /
      static_cast<double>(measured.size());
  return {.ns_per_message = nanos,
          .resting = book.order_count(),
          .fills = fill_count};
}

void bench(std::string_view name, std::size_t target_orders,
           std::size_t messages) {
  const ob::FlowConfig config{.seed = 1, .target_orders = target_orders};
  ob::Flow flow{config};
  const std::vector<ob::Event> warmup = flow.take(target_orders * 4);
  const std::vector<ob::Event> measured = flow.take(messages);

  Result result{};
  if (name == "naive") {
    result = run(ob::NaiveBook{}, warmup, measured);
  } else if (name == "map") {
    result = run(ob::MapBook{}, warmup, measured);
  } else if (name == "vector") {
    result = run(ob::VectorBook{}, warmup, measured);
  } else if (name == "array") {
    result = run(ob::ArrayBook{config.max_price}, warmup, measured);
  } else {
    throw std::invalid_argument("unknown book: " + std::string(name));
  }
  std::cout << std::format("{:<6} {:>14} {:>10} {:>12} {:>10} {:>12.1f}\n",
                           name, target_orders, result.resting, messages,
                           result.fills, result.ns_per_message);
}

void header() {
  std::cout << std::format("{:<6} {:>14} {:>10} {:>12} {:>10} {:>12}\n", "book",
                           "target_orders", "resting", "messages", "fills",
                           "ns/message");
}

} // namespace

int main(int argc, char **argv) {
  try {
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() != 1 && args.size() != 4) {
      std::cerr << std::format("usage: {} [book target_orders messages]\n",
                               args[0]);
      return 2;
    }
    header();
    if (args.size() == 4) {
      bench(args[1], std::stoul(args[2]), std::stoul(args[3]));
      return 0;
    }
    for (const std::size_t orders : {1'000UL, 10'000UL, 100'000UL}) {
      for (const std::string_view book : {"naive", "map", "vector", "array"}) {
        const std::size_t messages = book == "naive" ? 100'000 : 1'000'000;
        bench(book, orders, messages);
      }
    }
  } catch (const std::exception &e) {
    std::cerr << std::format("fatal: {}\n", e.what());
    return 1;
  }
  return 0;
}
