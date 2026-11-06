#pragma once

// std
#include <cstdint>
#include <vector>

namespace ob {

// Prices are whole ticks, never floating point: a book compares prices for
// equality on every message, and 0.1 + 0.2 != 0.3.
using Price = std::int64_t;
using Qty = std::int64_t;
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

constexpr Side opposite(Side side) {
  return side == Side::Buy ? Side::Sell : Side::Buy;
}

// Whether a `taker` order on `side` at `limit` will trade with a resting order
// at `resting`: a buyer pays up to its limit, a seller sells down to its limit.
constexpr bool crosses(Side side, Price limit, Price resting) {
  return side == Side::Buy ? resting <= limit : resting >= limit;
}

// Whether `lhs` is a better price than `rhs` for a resting order on `side`:
// the highest bid and the lowest ask are at the front of the book.
constexpr bool better(Side side, Price lhs, Price rhs) {
  return side == Side::Buy ? lhs > rhs : lhs < rhs;
}

// One trade. It always happens at the resting (maker) order's price.
struct Fill {
  OrderId taker;
  OrderId maker;
  Price price;
  Qty qty;

  bool operator==(const Fill &) const = default;
};

using Fills = std::vector<Fill>;

// A read-only view of the book, best price first on each side, used by tests
// and by the replay tool. Every book produces the same snapshot for the same
// state, so the books can be checked against each other.
struct RestingOrder {
  OrderId id;
  Qty qty;

  bool operator==(const RestingOrder &) const = default;
};

struct LevelView {
  Price price;
  std::vector<RestingOrder> orders; // time priority: front trades first

  bool operator==(const LevelView &) const = default;
};

struct Snapshot {
  std::vector<LevelView> bids;
  std::vector<LevelView> asks;

  bool operator==(const Snapshot &) const = default;
};

} // namespace ob
