#pragma once

// The "reverse vector" book from David Gross's CppCon 2024 talk "When
// Nanoseconds Matter: Ultrafast Trading Systems in C++": each side's price
// levels sit in one sorted vector, worst price first and best price at the
// back.
//
// Nearly all activity happens near the best price. With the best price at
// the back, adding or removing a level there shifts only the few levels
// behind it, and finding a level is a short scan from the back, which beats
// a binary search when the target is usually in the last few slots. The
// levels are contiguous, so that scan walks memory in order instead of
// chasing tree pointers like std::map.
//
// find() scans from the back for an existing level and gives up once it has
// passed the place the price would be. find_or_insert() does the same scan
// and inserts a new level at that place.
//
// Each level's orders live in an OrderPool queue, the same storage ArrayBook
// uses, so the two books differ only in how they find a price level.
//
// modify keeps the order's place in the queue only when it shrinks at the
// same price. Any other change pulls the order and sends it again as a new
// arrival (docs/matching-rules.md).

#include "orderbook/order_pool.hpp"
#include "orderbook/types.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <vector>

namespace ob {

class VectorBook {
public:
  bool add_limit(OrderId id, Side side, Price price, Qty qty, Fills &fills) {
    if (qty <= 0 || pool_.contains(id)) {
      return false;
    }
    qty = match(id, side, price, qty, fills);
    if (qty > 0) {
      auto level = find_or_insert(levels_of(side), side, price);
      pool_.push_back(level->orders, id, side, price, qty);
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
    const Index node = pool_.find(id);
    if (node == OrderPool::kNone) {
      return false;
    }
    remove(node);
    return true;
  }

  bool modify(OrderId id, Price price, Qty qty, Fills &fills) {
    const Index node = pool_.find(id);
    if (node == OrderPool::kNone || qty <= 0) {
      return false;
    }
    OrderPool::Node &order = pool_[node];
    if (price == order.price && qty <= order.qty) {
      order.qty = qty;
      return true;
    }
    const Side side = order.side;
    remove(node);
    return add_limit(id, side, price, qty, fills);
  }

  [[nodiscard]] std::optional<Price> best_bid() const { return best(bids_); }
  [[nodiscard]] std::optional<Price> best_ask() const { return best(asks_); }

  [[nodiscard]] std::size_t order_count() const { return pool_.size(); }

  [[nodiscard]] Snapshot snapshot(std::size_t depth) const {
    Snapshot out;
    view(bids_, depth, out.bids);
    view(asks_, depth, out.asks);
    return out;
  }

private:
  using Index = OrderPool::Index;

  struct Level {
    Price price;
    OrderPool::Queue orders;
  };

  using Levels = std::vector<Level>;

  Levels &levels_of(Side side) { return side == Side::Buy ? bids_ : asks_; }

  static Levels::iterator find(Levels &levels, Side side, Price price) {
    for (auto it = levels.end(); it != levels.begin();) {
      --it;
      if (it->price == price) {
        return it;
      }
      if (better(side, price, it->price)) {
        break;
      }
    }
    return levels.end();
  }

  static Levels::iterator find_or_insert(Levels &levels, Side side,
                                         Price price) {
    auto it = levels.end();
    while (it != levels.begin() && !better(side, price, std::prev(it)->price)) {
      --it;
      if (it->price == price) {
        return it;
      }
    }
    return levels.insert(it, Level{.price = price, .orders = {}});
  }

  Qty match(OrderId id, Side side, Price limit, Qty qty, Fills &fills) {
    Levels &levels = levels_of(opposite(side));
    while (qty > 0 && !levels.empty() &&
           crosses(side, limit, levels.back().price)) {
      Level &level = levels.back();
      const Index head = level.orders.head;
      OrderPool::Node &maker = pool_[head];
      const Qty traded = std::min(qty, maker.qty);
      fills.push_back({.taker = id,
                       .maker = maker.id,
                       .price = maker.price,
                       .qty = traded});
      maker.qty -= traded;
      qty -= traded;
      if (maker.qty == 0) {
        pool_.erase(level.orders, head);
        if (level.orders.empty()) {
          levels.pop_back();
        }
      }
    }
    return qty;
  }

  void remove(Index node) {
    const Side side = pool_[node].side;
    Levels &levels = levels_of(side);
    auto level = find(levels, side, pool_[node].price);
    pool_.erase(level->orders, node);
    if (level->orders.empty()) {
      levels.erase(level);
    }
  }

  static std::optional<Price> best(const Levels &levels) {
    if (levels.empty()) {
      return std::nullopt;
    }
    return levels.back().price;
  }

  void view(const Levels &levels, std::size_t depth,
            std::vector<LevelView> &out) const {
    for (auto it = levels.rbegin(); it != levels.rend() && out.size() < depth;
         ++it) {
      pool_.view(it->orders, it->price, out);
    }
  }

  Levels bids_;
  Levels asks_;
  OrderPool pool_;
};

} // namespace ob
