#pragma once

// A seeded stream of level updates for the market-by-price books in
// level_book.hpp. fill() returns one add at every level of both sides, the
// book fully built. next() then returns updates that land within near_ticks
// of the top of the book near_pct of the time and anywhere in the book
// otherwise, the way a real feed's updates cluster at the top. Offset 0 is
// the tick next to the mid.
//
// The stream keeps its own copy of the quantity at every level, so a removal
// never takes more than is there, and erase_pct of removals take a level's
// whole quantity, erasing it. The mid is fixed, so the two sides never cross;
// levels still appear and disappear at the top as their quantity comes and
// goes.
//
// Random numbers come from rng() % bound, for the same reason as in
// flow.hpp.

#include "orderbook/types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace ob {

struct LevelUpdate {
  Side side;
  Price price;
  Qty delta;
};

struct LevelFlowConfig {
  std::uint64_t seed = 1;
  Price mid = 100'000;
  Price levels = 1'000;
  int near_pct = 90;
  Price near_ticks = 8;
  int erase_pct = 40;
  Qty max_qty = 100;
};

class LevelFlow {
public:
  explicit LevelFlow(const LevelFlowConfig &config)
      : config_(config), rng_(config.seed),
        bids_(static_cast<std::size_t>(config.levels)),
        asks_(static_cast<std::size_t>(config.levels)) {}

  std::vector<LevelUpdate> fill() {
    std::vector<LevelUpdate> out;
    for (Price offset = 0; offset < config_.levels; ++offset) {
      out.push_back(add(Side::Buy, offset));
      out.push_back(add(Side::Sell, offset));
    }
    return out;
  }

  LevelUpdate next() {
    const Side side = below(2) == 0 ? Side::Buy : Side::Sell;
    const bool near = static_cast<int>(below(100)) < config_.near_pct;
    const Price span =
        near ? std::min(config_.near_ticks, config_.levels) : config_.levels;
    const auto offset =
        static_cast<Price>(below(static_cast<std::uint64_t>(span)));
    const Qty have = qty_at(side, offset);
    if (have == 0 || below(2) == 0) {
      return add(side, offset);
    }
    Qty take = have;
    if (static_cast<int>(below(100)) >= config_.erase_pct) {
      take = std::min(
          have, 1 + static_cast<Qty>(below(static_cast<std::uint64_t>(have))));
    }
    qty_at(side, offset) -= take;
    return {.side = side, .price = price(side, offset), .delta = -take};
  }

  std::vector<LevelUpdate> take(std::size_t count) {
    std::vector<LevelUpdate> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(next());
    }
    return out;
  }

private:
  std::uint64_t below(std::uint64_t bound) { return rng_() % bound; }

  Qty &qty_at(Side side, Price offset) {
    auto &qty = side == Side::Buy ? bids_ : asks_;
    return qty[static_cast<std::size_t>(offset)];
  }

  [[nodiscard]] Price price(Side side, Price offset) const {
    return side == Side::Buy ? config_.mid - 1 - offset
                             : config_.mid + 1 + offset;
  }

  LevelUpdate add(Side side, Price offset) {
    const Qty qty = 1 + static_cast<Qty>(
                            below(static_cast<std::uint64_t>(config_.max_qty)));
    qty_at(side, offset) += qty;
    return {.side = side, .price = price(side, offset), .delta = qty};
  }

  LevelFlowConfig config_;
  std::mt19937_64 rng_;
  std::vector<Qty> bids_;
  std::vector<Qty> asks_;
};

} // namespace ob
