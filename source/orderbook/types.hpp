#pragma once

// Types shared by every book.
//
// Prices are whole ticks, never floating point: a book compares prices for
// equality on every message, and 0.1 + 0.2 != 0.3.
//
// crosses(side, limit, resting) says whether an order on `side` willing to
// trade at `limit` trades with a resting order at `resting`: a buyer pays up
// to its limit, a seller sells down to its limit. better(side, a, b) says
// whether `a` is the better price for a resting order on `side`: the highest
// bid and the lowest ask are at the front of the book. market_limit(side) is
// the limit a market order trades up to, the most extreme price there is, so
// it crosses anything.
//
// A Fill is one trade. It always happens at the resting (maker) order's
// price.
//
// A Snapshot is a read-only view of the book, best price first on each side,
// with each level's orders in time priority (front trades first). Every book
// produces the same snapshot for the same state, which is how the tests
// check the books against each other.

#include <cstdint>
#include <limits>
#include <vector>

namespace ob {

using Price = std::int64_t;
using Qty = std::int64_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

constexpr Side opposite(Side side) {
  return side == Side::Buy ? Side::Sell : Side::Buy;
}

constexpr bool crosses(Side side, Price limit, Price resting) {
  return side == Side::Buy ? resting <= limit : resting >= limit;
}

constexpr bool better(Side side, Price lhs, Price rhs) {
  return side == Side::Buy ? lhs > rhs : lhs < rhs;
}

constexpr Price market_limit(Side side) {
  return side == Side::Buy ? std::numeric_limits<Price>::max()
                           : std::numeric_limits<Price>::min();
}

struct Fill {
  OrderId taker;
  OrderId maker;
  Price price;
  Qty qty;

  bool operator==(const Fill &) const = default;
};

using Fills = std::vector<Fill>;

struct RestingOrder {
  OrderId id;
  Qty qty;

  bool operator==(const RestingOrder &) const = default;
};

struct LevelView {
  Price price;
  std::vector<RestingOrder> orders;

  bool operator==(const LevelView &) const = default;
};

struct Snapshot {
  std::vector<LevelView> bids;
  std::vector<LevelView> asks;

  bool operator==(const Snapshot &) const = default;
};

} // namespace ob
