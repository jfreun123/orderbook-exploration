// Replays a generated message stream through one book and writes a JSON
// trace of every step to stdout: the call made, what the book answered, the
// trades it produced, and the top of the book afterwards. viz/index.html
// plays it back.
//
//   replay [--book naive|map|vector|array] [--seed N] [--messages N]
//          [--orders N] [--spread TICKS] [--depth LEVELS] > viz/trace.json
//
// Every option has a default, in Options. Prices run from 0 to 200 around a
// mid of 100 so the ladder stays readable, --orders is the stream's
// target_orders, and --depth is how many price levels a side of each
// snapshot shows.

#include "orderbook/array_book.hpp"
#include "orderbook/flow.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage =
    "usage: replay [--book naive|map|vector|array] [--seed N] [--messages N]\n"
    "              [--orders N] [--spread TICKS] [--depth LEVELS]\n";

struct Options {
  std::string book = "array";
  std::uint64_t seed = 7;
  std::size_t messages = 400;
  std::size_t orders = 40;
  ob::Price spread = 8;
  std::size_t depth = 8;
};

Options parse(std::span<char *> args) {
  Options options;
  for (std::size_t i = 1; i < args.size(); i += 2) {
    const std::string_view flag = args[i];
    if (i + 1 >= args.size()) {
      throw std::invalid_argument(std::format("{} needs a value", flag));
    }
    const std::string value = args[i + 1];
    if (flag == "--book") {
      options.book = value;
    } else if (flag == "--seed") {
      options.seed = std::stoull(value);
    } else if (flag == "--messages") {
      options.messages = std::stoul(value);
    } else if (flag == "--orders") {
      options.orders = std::stoul(value);
    } else if (flag == "--spread") {
      options.spread = std::stoll(value);
    } else if (flag == "--depth") {
      options.depth = std::stoul(value);
    } else {
      throw std::invalid_argument(std::format("unknown option {}", flag));
    }
  }
  return options;
}

std::string_view side_name(ob::Side side) {
  return side == ob::Side::Buy ? "buy" : "sell";
}

std::string_view call_name(ob::EventType type) {
  switch (type) {
  case ob::EventType::Limit:
    return "add_limit";
  case ob::EventType::Market:
    return "add_market";
  case ob::EventType::Cancel:
    return "cancel";
  case ob::EventType::Modify:
    return "modify";
  }
  return "?";
}

std::string fills_json(const ob::Fills &fills) {
  std::string out = "[";
  for (std::size_t i = 0; i < fills.size(); ++i) {
    out += std::format("{}[{},{},{},{}]", i == 0 ? "" : ",", fills[i].taker,
                       fills[i].maker, fills[i].price, fills[i].qty);
  }
  return out + "]";
}

std::string levels_json(const std::vector<ob::LevelView> &levels) {
  std::string out = "[";
  for (std::size_t i = 0; i < levels.size(); ++i) {
    out += std::format(R"({}{{"price":{},"orders":[)", i == 0 ? "" : ",",
                       levels[i].price);
    for (std::size_t j = 0; j < levels[i].orders.size(); ++j) {
      out += std::format("{}[{},{}]", j == 0 ? "" : ",", levels[i].orders[j].id,
                         levels[i].orders[j].qty);
    }
    out += "]}";
  }
  return out + "]";
}

template <class Book>
void replay(Book book, const Options &options, const ob::FlowConfig &config) {
  ob::Flow flow{config};
  ob::Fills fills;
  std::cout << std::format(
      R"({{"book":"{}","seed":{},"orders":{},"spread":{},"depth":{},"steps":[)"
      "\n",
      options.book, options.seed, options.orders, options.spread,
      options.depth);
  for (std::size_t i = 0; i < options.messages; ++i) {
    const ob::Event event = flow.next();
    fills.clear();
    const bool accepted = ob::apply(book, event, fills);
    const ob::Snapshot snap = book.snapshot(options.depth);
    std::cout << std::format(
        R"({}{{"call":"{}","id":{},"side":"{}","price":{},"qty":{},"ok":{},)"
        R"("fills":{},"resting":{},"bids":{},"asks":{}}})"
        "\n",
        i == 0 ? "" : ",", call_name(event.type), event.id,
        side_name(event.side), event.price, event.qty, accepted,
        fills_json(fills), book.order_count(), levels_json(snap.bids),
        levels_json(snap.asks));
  }
  std::cout << "]}\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Options options =
        parse(std::span(argv, static_cast<std::size_t>(argc)));
    const ob::FlowConfig config{.seed = options.seed,
                                .start_mid = 100,
                                .max_price = 200,
                                .spread = options.spread,
                                .target_orders = options.orders};
    if (options.book == "naive") {
      replay(ob::NaiveBook{}, options, config);
    } else if (options.book == "map") {
      replay(ob::MapBook{}, options, config);
    } else if (options.book == "vector") {
      replay(ob::VectorBook{}, options, config);
    } else if (options.book == "array") {
      replay(ob::ArrayBook{config.max_price}, options, config);
    } else {
      throw std::invalid_argument("unknown book " + options.book);
    }
  } catch (const std::exception &e) {
    std::cerr << "replay: " << e.what() << "\n" << kUsage;
    return 1;
  }
  return 0;
}
