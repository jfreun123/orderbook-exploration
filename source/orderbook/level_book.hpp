#pragma once

// Market-by-price books: each side is only price -> total quantity at that
// price, with no individual orders, ids or queues. This is the book from
// David Gross's CppCon 2024 talk (AddOrder(Side, Price, Volume)), and the
// kind of book a market data feed publishes level by level. With no
// per-order work, finding the price level is nearly the whole cost of an
// update, so this is where std::map and a sorted vector differ most.
//
// update(side, price, delta) adds delta (which may be negative) to the
// quantity at price. A level is created when quantity first arrives and
// erased when it reaches zero; callers never take a level below zero.
// levels(side) returns every level on that side, best first.
//
// MapLevelBook keeps each side in a std::map with the best price at begin():
// bids sort descending, asks ascending.
//
// VectorLevelBook is Gross's "reverse vector": levels sorted worst to best,
// so the best price is at the back, where nearly all updates land and an
// insert or erase shifts only the few levels behind it. position() returns
// the level at the price, or where it would be inserted. Search::Linear
// scans for it from the back, which wins when the target is usually in the
// last few slots; Search::Binary is std::partition_point over the whole
// vector, for comparison.

#include "orderbook/types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace ob {

using LevelQty = std::pair<Price, Qty>;

class MapLevelBook {
public:
  void update(Side side, Price price, Qty delta) {
    if (side == Side::Buy) {
      apply(bids_, price, delta);
    } else {
      apply(asks_, price, delta);
    }
  }

  [[nodiscard]] std::optional<Price> best_bid() const { return best(bids_); }
  [[nodiscard]] std::optional<Price> best_ask() const { return best(asks_); }

  [[nodiscard]] std::vector<LevelQty> levels(Side side) const {
    return side == Side::Buy
               ? std::vector<LevelQty>(bids_.begin(), bids_.end())
               : std::vector<LevelQty>(asks_.begin(), asks_.end());
  }

private:
  template <class Levels>
  static void apply(Levels &levels, Price price, Qty delta) {
    auto [it, inserted] = levels.try_emplace(price, 0);
    it->second += delta;
    if (it->second == 0) {
      levels.erase(it);
    }
  }

  template <class Levels>
  static std::optional<Price> best(const Levels &levels) {
    return levels.empty() ? std::nullopt : std::optional(levels.begin()->first);
  }

  std::map<Price, Qty, std::greater<>> bids_;
  std::map<Price, Qty, std::less<>> asks_;
};

enum class Search : std::uint8_t { Linear, Binary };

template <Search kSearch> class VectorLevelBook {
public:
  void update(Side side, Price price, Qty delta) {
    Levels &levels = side == Side::Buy ? bids_ : asks_;
    auto it = position(levels, side, price);
    if (it != levels.end() && it->first == price) {
      it->second += delta;
      if (it->second == 0) {
        levels.erase(it);
      }
      return;
    }
    levels.insert(it, {price, delta});
  }

  [[nodiscard]] std::optional<Price> best_bid() const { return best(bids_); }
  [[nodiscard]] std::optional<Price> best_ask() const { return best(asks_); }

  [[nodiscard]] std::vector<LevelQty> levels(Side side) const {
    const Levels &levels = side == Side::Buy ? bids_ : asks_;
    return {levels.rbegin(), levels.rend()};
  }

private:
  using Levels = std::vector<LevelQty>;

  static Levels::iterator position(Levels &levels, Side side, Price price) {
    if constexpr (kSearch == Search::Linear) {
      auto it = levels.end();
      while (it != levels.begin() &&
             !better(side, price, std::prev(it)->first)) {
        --it;
      }
      return it;
    } else {
      return std::ranges::partition_point(levels, [&](const LevelQty &level) {
        return better(side, price, level.first);
      });
    }
  }

  static std::optional<Price> best(const Levels &levels) {
    return levels.empty() ? std::nullopt : std::optional(levels.back().first);
  }

  Levels bids_;
  Levels asks_;
};

} // namespace ob
