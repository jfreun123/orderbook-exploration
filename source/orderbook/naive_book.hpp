#pragma once

// The slow, obvious book, shaped like the LeetCode solution: each side is one
// vector of orders kept in priority order, worst first and best last, so the
// next order to trade is at the back.
//
// Unlike the LeetCode solution it keeps arrival order within a price and can
// find an order by id, so it supports cancel and modify. Every find is a
// linear scan of both sides, and every insert shifts the orders behind it.
//
// A new order rests behind every order at its own price or better, which in
// a worst-to-best vector means just in front of them: rest() inserts at the
// first order the new one is not better than.
//
// modify keeps the order's place in the queue only when it shrinks at the
// same price. Any other change pulls the order and sends it again as a new
// arrival (docs/matching-rules.md).

#include "orderbook/types.hpp"

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
      rest({.id = id, .side = side, .price = price, .qty = qty});
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
    for (Orders *orders : {&bids_, &asks_}) {
      auto it = find(*orders, id);
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

  using Orders = std::vector<Order>;

  Orders &side_of(Side side) { return side == Side::Buy ? bids_ : asks_; }

  static Orders::iterator find(Orders &orders, OrderId id) {
    return std::find_if(orders.begin(), orders.end(),
                        [id](const Order &order) { return order.id == id; });
  }

  Order *find(OrderId id) {
    for (Orders *orders : {&bids_, &asks_}) {
      auto it = find(*orders, id);
      if (it != orders->end()) {
        return &*it;
      }
    }
    return nullptr;
  }

  Qty match(OrderId id, Side side, Price limit, Qty qty, Fills &fills) {
    Orders &resting = side_of(opposite(side));
    while (qty > 0 && !resting.empty() &&
           crosses(side, limit, resting.back().price)) {
      Order &maker = resting.back();
      const Qty traded = std::min(qty, maker.qty);
      fills.push_back({.taker = id,
                       .maker = maker.id,
                       .price = maker.price,
                       .qty = traded});
      maker.qty -= traded;
      qty -= traded;
      if (maker.qty == 0) {
        resting.pop_back();
      }
    }
    return qty;
  }

  void rest(const Order &order) {
    Orders &orders = side_of(order.side);
    auto pos = std::partition_point(
        orders.begin(), orders.end(), [&](const Order &other) {
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
