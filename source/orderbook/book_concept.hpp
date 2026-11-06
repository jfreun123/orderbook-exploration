#pragma once

// orderbook
#include "orderbook/types.hpp"

// std
#include <concepts>
#include <cstddef>
#include <optional>

namespace ob {

// What every book in this repo can do. The rules behind each call are written
// down in docs/matching-rules.md, and test/source/book_test.cpp checks every
// book against them.
template <class Book>
concept OrderBook =
    requires(Book book, const Book cbook, OrderId id, Side side, Price price,
             Qty qty, Fills &fills, std::size_t depth) {
      { book.add_limit(id, side, price, qty, fills) } -> std::same_as<bool>;
      { book.add_market(id, side, qty, fills) } -> std::same_as<bool>;
      { book.cancel(id) } -> std::same_as<bool>;
      { book.modify(id, price, qty, fills) } -> std::same_as<bool>;
      { cbook.best_bid() } -> std::same_as<std::optional<Price>>;
      { cbook.best_ask() } -> std::same_as<std::optional<Price>>;
      { cbook.order_count() } -> std::same_as<std::size_t>;
      { cbook.snapshot(depth) } -> std::same_as<Snapshot>;
    };

} // namespace ob
