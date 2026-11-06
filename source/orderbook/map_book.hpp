#pragma once

// The textbook book: a sorted map from price to the queue of orders resting
// at that price, plus a hash map from order id to where the order lives.
//
// Both maps keep the best price at begin(): bids sort descending, asks
// ascending. Each level is a std::list with the oldest order at the front,
// so it trades first, and the index holds list iterators, which std::list
// keeps valid until the order is erased.
//
// Compared to NaiveBook, finding a price level is O(log levels) instead of a
// scan of every order, and cancel goes straight to the order through the
// index. The cost is a heap allocation for every map node and list node, and
// a pointer chase for every step through either.
//
// modify keeps the order's place in the queue only when it shrinks at the
// same price. Any other change pulls the order and sends it again as a new
// arrival (docs/matching-rules.md).

#include "orderbook/types.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

class MapBook {
public:
  bool add_limit(OrderId id, Side side, Price price, Qty qty, Fills &fills) {
    if (qty <= 0 || index_.contains(id)) {
      return false;
    }
    qty = match(id, side, price, qty, fills);
    if (qty > 0) {
      rest(id, side, price, qty);
    }
    return true;
  }

  bool add_market(OrderId id, Side side, Qty qty, Fills &fills) {
    if (qty <= 0) {
      return false;
    }
    match(id, side, market_limit(side), qty, fills);
    return true;
  }

  bool cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) {
      return false;
    }
    const Level::iterator order = found->second;
    index_.erase(found);
    if (order->side == Side::Buy) {
      unlink(bids_, order);
    } else {
      unlink(asks_, order);
    }
    return true;
  }

  bool modify(OrderId id, Price price, Qty qty, Fills &fills) {
    auto found = index_.find(id);
    if (found == index_.end() || qty <= 0) {
      return false;
    }
    const Level::iterator order = found->second;
    if (price == order->price && qty <= order->qty) {
      order->qty = qty;
      return true;
    }
    const Side side = order->side;
    cancel(id);
    return add_limit(id, side, price, qty, fills);
  }

  [[nodiscard]] std::optional<Price> best_bid() const { return best(bids_); }
  [[nodiscard]] std::optional<Price> best_ask() const { return best(asks_); }

  [[nodiscard]] std::size_t order_count() const { return index_.size(); }

  [[nodiscard]] Snapshot snapshot(std::size_t depth) const {
    return {.bids = levels(bids_, depth), .asks = levels(asks_, depth)};
  }

private:
  struct Order {
    OrderId id;
    Side side;
    Price price;
    Qty qty;
  };

  using Level = std::list<Order>;
  using Bids = std::map<Price, Level, std::greater<>>;
  using Asks = std::map<Price, Level, std::less<>>;

  Qty match(OrderId id, Side side, Price limit, Qty qty, Fills &fills) {
    return side == Side::Buy ? match(id, side, limit, qty, asks_, fills)
                             : match(id, side, limit, qty, bids_, fills);
  }

  template <class Levels>
  Qty match(OrderId id, Side side, Price limit, Qty qty, Levels &levels,
            Fills &fills) {
    while (qty > 0 && !levels.empty() &&
           crosses(side, limit, levels.begin()->first)) {
      auto level = levels.begin();
      Order &maker = level->second.front();
      const Qty traded = std::min(qty, maker.qty);
      fills.push_back({.taker = id,
                       .maker = maker.id,
                       .price = maker.price,
                       .qty = traded});
      maker.qty -= traded;
      qty -= traded;
      if (maker.qty == 0) {
        index_.erase(maker.id);
        level->second.pop_front();
        if (level->second.empty()) {
          levels.erase(level);
        }
      }
    }
    return qty;
  }

  void rest(OrderId id, Side side, Price price, Qty qty) {
    Level &level = side == Side::Buy ? bids_[price] : asks_[price];
    level.push_back({.id = id, .side = side, .price = price, .qty = qty});
    index_.emplace(id, std::prev(level.end()));
  }

  template <class Levels>
  static void unlink(Levels &levels, Level::iterator order) {
    auto level = levels.find(order->price);
    level->second.erase(order);
    if (level->second.empty()) {
      levels.erase(level);
    }
  }

  template <class Levels>
  static std::optional<Price> best(const Levels &levels) {
    if (levels.empty()) {
      return std::nullopt;
    }
    return levels.begin()->first;
  }

  template <class Levels>
  static std::vector<LevelView> levels(const Levels &levels,
                                       std::size_t depth) {
    std::vector<LevelView> out;
    for (const auto &[price, orders] : levels) {
      if (out.size() == depth) {
        break;
      }
      LevelView &view =
          out.emplace_back(LevelView{.price = price, .orders = {}});
      for (const Order &order : orders) {
        view.orders.push_back({.id = order.id, .qty = order.qty});
      }
    }
    return out;
  }

  Bids bids_;
  Asks asks_;
  std::unordered_map<OrderId, Level::iterator> index_;
};

} // namespace ob
