#pragma once

// The slow, obvious book, shaped like the LeetCode solution: each side is one
// container of orders kept in priority order, and the best order is always at
// the back. Unlike the LeetCode heaps it keeps arrival order within a price,
// and it can find an order by id, so it supports cancel and modify. Both of
// those are linear scans, and every insert shifts the orders behind it.

// orderbook
#include "orderbook/types.hpp"

// std
#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace ob {

class NaiveBook {
public:
  bool add_limit(OrderId id, Side side, Price price, Qty qty, Fills &fills) {
    if (qty <= 0 || find(id) != nullptr) {
      return false;
    }
    qty = match(id, side, price, qty, fills);
    if (qty > 0) {
      rest(Order{.id = id, .side = side, .price = price, .qty = qty});
    }
    return true;
  }

  bool add_market(OrderId id, Side side, Qty qty, Fills &fills) {
    if (qty <= 0) {
      return false;
    }
    // Whatever cannot be filled is dropped, never rested.
    match_any_price(id, side, qty, fills);
    return true;
  }

  bool cancel(OrderId id) {
    for (auto *orders : {&bids_, &asks_}) {
      auto it = std::ranges::find(*orders, id, &Order::id);
      if (it != orders->end()) {
        orders->erase(it);
        return true;
      }
    }
    return false;
  }

  bool modify(OrderId id, Price price, Qty qty, Fills &fills) {
    Order *order = find(id);
    if (order == nullptr || qty <= 0) {
      return false;
    }
    // Shrinking an order in place keeps its place in the queue. Anything else
    // loses it: the order is pulled and sent again as a new arrival.
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

  [[nodiscard]] std::size_t order_count() const {
    return bids_.size() + asks_.size();
  }

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

  // Worst first, best last, so the next order to trade is a pop_back away.
  using Orders = std::vector<Order>;

  Orders &side_of(Side side) { return side == Side::Buy ? bids_ : asks_; }

  Order *find(OrderId id) {
    for (auto *orders : {&bids_, &asks_}) {
      auto it = std::ranges::find(*orders, id, &Order::id);
      if (it != orders->end()) {
        return &*it;
      }
    }
    return nullptr;
  }

  // Trades `qty` against the other side while its best price crosses `limit`,
  // and returns what is left.
  Qty match(OrderId id, Side side, Price limit, Qty qty, Fills &fills) {
    Orders &resting = side_of(opposite(side));
    while (qty > 0 && !resting.empty() &&
           crosses(side, limit, resting.back().price)) {
      qty = trade(id, resting, qty, fills);
    }
    return qty;
  }

  void match_any_price(OrderId id, Side side, Qty qty, Fills &fills) {
    Orders &resting = side_of(opposite(side));
    while (qty > 0 && !resting.empty()) {
      qty = trade(id, resting, qty, fills);
    }
  }

  static Qty trade(OrderId id, Orders &resting, Qty qty, Fills &fills) {
    Order &maker = resting.back();
    const Qty traded = std::min(qty, maker.qty);
    fills.push_back(
        {.taker = id, .maker = maker.id, .price = maker.price, .qty = traded});
    maker.qty -= traded;
    if (maker.qty == 0) {
      resting.pop_back();
    }
    return qty - traded;
  }

  // A new order goes behind every order at its own price or better, which is
  // in front of them in this worst-to-best vector.
  void rest(const Order &order) {
    Orders &orders = side_of(order.side);
    auto pos = std::ranges::partition_point(orders, [&](const Order &other) {
      return better(order.side, order.price, other.price);
    });
    orders.insert(pos, order);
  }

  static std::optional<Price> best(const Orders &orders) {
    if (orders.empty()) {
      return std::nullopt;
    }
    return orders.back().price;
  }

  static std::vector<LevelView> levels(const Orders &orders,
                                       std::size_t depth) {
    std::vector<LevelView> out;
    for (auto it = orders.rbegin(); it != orders.rend(); ++it) {
      if (out.empty() || out.back().price != it->price) {
        if (out.size() == depth) {
          break;
        }
        out.push_back({.price = it->price, .orders = {}});
      }
      out.back().orders.push_back({.id = it->id, .qty = it->qty});
    }
    return out;
  }

  Orders bids_;
  Orders asks_;
};

} // namespace ob
