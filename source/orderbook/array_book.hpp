#pragma once

// A book for one instrument whose prices fall in a known range of ticks,
// [0, max_price). An order priced outside it is rejected.
//
// Each side is an array with one slot per tick, so finding a price level is
// an index: no search at all. Orders live in an OrderPool: pooled nodes, an
// intrusive list per price, and an id index.
//
// The best bid and best ask are cached, as -1 and max_price while that side
// is empty. When a best level empties, remove() walks the array to the next
// non-empty tick, which in a busy book is usually a tick or two away.
// match() stops at the edge of the array through in_range(), so a market
// order's extreme limit never reads past it.
//
// modify keeps the order's place in the queue only when it shrinks at the
// same price. Any other change pulls the order and sends it again as a new
// arrival (docs/matching-rules.md).

#include "orderbook/order_pool.hpp"
#include "orderbook/types.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace ob {

class ArrayBook {
public:
  explicit ArrayBook(Price max_price)
      : max_price_(max_price), bids_(slot(max_price)), asks_(slot(max_price)),
        best_ask_(max_price) {}

  bool add_limit(OrderId id, Side side, Price price, Qty qty, Fills &fills) {
    if (qty <= 0 || !in_range(price) || pool_.contains(id)) {
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
    const Index node = pool_.find(id);
    if (node == OrderPool::kNone) {
      return false;
    }
    remove(node);
    return true;
  }

  bool modify(OrderId id, Price price, Qty qty, Fills &fills) {
    const Index node = pool_.find(id);
    if (node == OrderPool::kNone || qty <= 0 || !in_range(price)) {
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

  [[nodiscard]] std::optional<Price> best_bid() const {
    return best_bid_ >= 0 ? std::optional(best_bid_) : std::nullopt;
  }

  [[nodiscard]] std::optional<Price> best_ask() const {
    return best_ask_ < max_price_ ? std::optional(best_ask_) : std::nullopt;
  }

  [[nodiscard]] std::size_t order_count() const { return pool_.size(); }

  [[nodiscard]] Snapshot snapshot(std::size_t depth) const {
    Snapshot out;
    for (Price price = best_bid_; price >= 0 && out.bids.size() < depth;
         --price) {
      if (!bids_[slot(price)].empty()) {
        pool_.view(bids_[slot(price)], price, out.bids);
      }
    }
    for (Price price = best_ask_; price < max_price_ && out.asks.size() < depth;
         ++price) {
      if (!asks_[slot(price)].empty()) {
        pool_.view(asks_[slot(price)], price, out.asks);
      }
    }
    return out;
  }

private:
  using Index = OrderPool::Index;
  using Levels = std::vector<OrderPool::Queue>;

  static std::size_t slot(Price price) {
    return static_cast<std::size_t>(price);
  }

  [[nodiscard]] bool in_range(Price price) const {
    return price >= 0 && price < max_price_;
  }

  Levels &levels_of(Side side) { return side == Side::Buy ? bids_ : asks_; }

  Qty match(OrderId id, Side side, Price limit, Qty qty, Fills &fills) {
    const Levels &levels = levels_of(opposite(side));
    const Price &best = side == Side::Buy ? best_ask_ : best_bid_;
    while (qty > 0 && in_range(best) && crosses(side, limit, best)) {
      const Index head = levels[slot(best)].head;
      OrderPool::Node &maker = pool_[head];
      const Qty traded = std::min(qty, maker.qty);
      fills.push_back({.taker = id,
                       .maker = maker.id,
                       .price = maker.price,
                       .qty = traded});
      maker.qty -= traded;
      qty -= traded;
      if (maker.qty == 0) {
        remove(head);
      }
    }
    return qty;
  }

  void rest(OrderId id, Side side, Price price, Qty qty) {
    pool_.push_back(levels_of(side)[slot(price)], id, side, price, qty);
    if (side == Side::Buy) {
      best_bid_ = std::max(best_bid_, price);
    } else {
      best_ask_ = std::min(best_ask_, price);
    }
  }

  void remove(Index node) {
    const Side side = pool_[node].side;
    const Price price = pool_[node].price;
    OrderPool::Queue &level = levels_of(side)[slot(price)];
    pool_.erase(level, node);
    if (!level.empty()) {
      return;
    }
    if (side == Side::Buy && price == best_bid_) {
      while (best_bid_ >= 0 && bids_[slot(best_bid_)].empty()) {
        --best_bid_;
      }
    } else if (side == Side::Sell && price == best_ask_) {
      while (best_ask_ < max_price_ && asks_[slot(best_ask_)].empty()) {
        ++best_ask_;
      }
    }
  }

  Price max_price_;
  Levels bids_;
  Levels asks_;
  Price best_bid_ = -1;
  Price best_ask_;
  OrderPool pool_;
};

} // namespace ob
