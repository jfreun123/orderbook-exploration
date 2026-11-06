#pragma once

// A book for one instrument whose prices fall in a known range of ticks.
//
// - Each side is an array with one slot per tick, so finding a price level is
//   an index: no search at all.
// - Orders live in an OrderPool: pooled nodes, an intrusive list per price,
//   and an id index (see order_pool.hpp).
// - The best bid and best ask are cached. When a best level empties, the book
//   walks the array to the next non-empty tick; in a busy book that is
//   usually a tick or two away.
//
// The price range is fixed when the book is built: an order priced outside
// [0, max_price) is rejected.

// orderbook
#include "orderbook/order_pool.hpp"
#include "orderbook/types.hpp"

// std
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
    // Whatever cannot be filled is dropped, never rested. The most a buyer
    // could have to pay is the top of the range, and a seller the bottom.
    match(id, side, side == Side::Buy ? max_price_ - 1 : 0, qty, fills);
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
    // Shrinking in place keeps the order's place in the queue. Anything else
    // loses it: the order is pulled and sent again as a new arrival.
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

  // Trades against the other side while its best price crosses `limit`, and
  // returns what is left.
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
        remove(head); // moves `best` on if the level empties
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

  // Takes an order out of the book. If that empties the best level, walks
  // outward to the next one.
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
  Price best_bid_ = -1; // -1: no bids
  Price best_ask_;      // max_price_: no asks
  OrderPool pool_;
};

} // namespace ob
