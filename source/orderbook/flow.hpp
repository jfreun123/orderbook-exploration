#pragma once

// Synthetic order flow: a seeded stream of adds, cancels, modifies and market
// orders around a drifting mid price. It is not real market data. It is a
// repeatable stream with the same mix of messages a real feed has, so every
// book can be fed the same messages and timed or checked against the others.

// orderbook
#include "orderbook/types.hpp"

// std
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace ob {

enum class EventType : std::uint8_t { Limit, Market, Cancel, Modify };

struct Event {
  EventType type;
  OrderId id;
  Side side;
  Price price; // unused for Market and Cancel
  Qty qty;     // unused for Cancel
};

// Sends one event to a book. Returns what the book call returned.
template <class Book> bool apply(Book &book, const Event &event, Fills &fills) {
  switch (event.type) {
  case EventType::Limit:
    return book.add_limit(event.id, event.side, event.price, event.qty, fills);
  case EventType::Market:
    return book.add_market(event.id, event.side, event.qty, fills);
  case EventType::Cancel:
    return book.cancel(event.id);
  case EventType::Modify:
    return book.modify(event.id, event.price, event.qty, fills);
  }
  return false;
}

struct FlowConfig {
  std::uint64_t seed = 1;
  Price start_mid = 10'000; // ticks
  Price max_price = 20'000; // every price stays in [0, max_price)
  Price spread = 20;        // limit orders land within this many ticks of mid
  Qty max_qty = 10;
  // The stream adds orders faster than it cancels them until about this many
  // have been sent and not yet cancelled, then holds there.
  std::size_t target_orders = 1'000;
  // Out of 100 messages once the book is full. Whatever is left is cancels.
  int limit_pct = 45;
  int market_pct = 5;
  int modify_pct = 10;
};

class Flow {
public:
  explicit Flow(const FlowConfig &config)
      : config_(config), rng_(config.seed), mid_(config.start_mid) {}

  Event next() {
    // A slow random walk for the mid, one tick at a time.
    if (below(8) == 0) {
      mid_ += below(2) == 0 ? 1 : -1;
      mid_ = std::clamp(mid_, config_.spread,
                        config_.max_price - 1 - config_.spread);
    }

    const int roll = static_cast<int>(below(100));
    const bool filling = live_.size() < config_.target_orders;
    if (live_.empty() || filling || roll < config_.limit_pct) {
      return limit();
    }
    if (roll < config_.limit_pct + config_.market_pct) {
      return market();
    }
    if (roll < config_.limit_pct + config_.market_pct + config_.modify_pct) {
      return modify();
    }
    return cancel();
  }

  std::vector<Event> take(std::size_t count) {
    std::vector<Event> events;
    events.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      events.push_back(next());
    }
    return events;
  }

private:
  // A number in [0, bound). Done by hand, because the standard distributions
  // give different numbers on libstdc++ and libc++, and the stream has to be
  // the same everywhere.
  std::uint64_t below(std::uint64_t bound) { return rng_() % bound; }

  Side side() { return below(2) == 0 ? Side::Buy : Side::Sell; }

  Qty qty() {
    return 1 +
           static_cast<Qty>(below(static_cast<std::uint64_t>(config_.max_qty)));
  }

  // Mostly behind the mid on its own side, so it rests; sometimes a tick or
  // two through it, so it trades.
  Price limit_price(Side s) {
    const auto away =
        static_cast<Price>(below(static_cast<std::uint64_t>(config_.spread))) -
        2;
    return s == Side::Buy ? mid_ - away : mid_ + away;
  }

  Event limit() {
    const OrderId id = next_id_++;
    const Side s = side();
    live_.push_back({.id = id, .side = s});
    return {.type = EventType::Limit,
            .id = id,
            .side = s,
            .price = limit_price(s),
            .qty = qty()};
  }

  Event market() {
    return {.type = EventType::Market,
            .id = next_id_++,
            .side = side(),
            .price = 0,
            .qty = qty()};
  }

  // Modify and cancel pick an order this stream sent and has not cancelled.
  // It may have filled since, in which case the book answers false.
  Event modify() {
    const Live order = live_[below(live_.size())];
    return {.type = EventType::Modify,
            .id = order.id,
            .side = order.side,
            .price = limit_price(order.side),
            .qty = qty()};
  }

  Event cancel() {
    const std::size_t at = below(live_.size());
    const OrderId id = live_[at].id;
    live_[at] = live_.back();
    live_.pop_back();
    return {.type = EventType::Cancel,
            .id = id,
            .side = Side::Buy,
            .price = 0,
            .qty = 0};
  }

  struct Live {
    OrderId id;
    Side side;
  };

  FlowConfig config_;
  std::mt19937_64 rng_;
  Price mid_;
  OrderId next_id_ = 1;
  std::vector<Live> live_;
};

} // namespace ob

namespace ob {

// A level update for a market-by-price book (level_book.hpp).
struct LevelUpdate {
  Side side;
  Price price;
  Qty delta;
};

struct LevelFlowConfig {
  std::uint64_t seed = 1;
  Price mid = 100'000;
  Price levels = 1'000; // ticks of book on each side of the mid
  // Out of 100 updates, how many land within `near_ticks` of the top of the
  // book. The rest land anywhere in the book.
  int near_pct = 90;
  Price near_ticks = 8;
  // Out of 100 removals, how many take a level's whole quantity, erasing it.
  int erase_pct = 40;
  Qty max_qty = 100;
};

// A seeded stream of level updates. It keeps its own copy of the quantity at
// every level, so a removal never takes more than is there. The mid is
// fixed, so the two sides never cross; levels still appear and disappear
// at the top as their quantity comes and goes.
class LevelFlow {
public:
  explicit LevelFlow(const LevelFlowConfig &config)
      : config_(config), rng_(config.seed),
        bids_(static_cast<std::size_t>(config.levels)),
        asks_(static_cast<std::size_t>(config.levels)) {}

  // One add at every level of both sides: the book, fully built.
  std::vector<LevelUpdate> fill() {
    std::vector<LevelUpdate> out;
    for (Price offset = 0; offset < config_.levels; ++offset) {
      out.push_back(add(Side::Buy, offset));
      out.push_back(add(Side::Sell, offset));
    }
    return out;
  }

  LevelUpdate next() {
    const Side side = below(2) == 0 ? Side::Buy : Side::Sell;
    const bool near = static_cast<int>(below(100)) < config_.near_pct;
    const Price span =
        near ? std::min(config_.near_ticks, config_.levels) : config_.levels;
    const auto offset =
        static_cast<Price>(below(static_cast<std::uint64_t>(span)));
    const Qty have = qty_at(side, offset);
    if (have == 0 || below(2) == 0) {
      return add(side, offset);
    }
    Qty take = have;
    if (static_cast<int>(below(100)) >= config_.erase_pct) {
      take = std::min(
          have, 1 + static_cast<Qty>(below(static_cast<std::uint64_t>(have))));
    }
    qty_at(side, offset) -= take;
    return {.side = side, .price = price(side, offset), .delta = -take};
  }

  std::vector<LevelUpdate> take(std::size_t count) {
    std::vector<LevelUpdate> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      out.push_back(next());
    }
    return out;
  }

private:
  std::uint64_t below(std::uint64_t bound) { return rng_() % bound; }

  Qty &qty_at(Side side, Price offset) {
    auto &qty = side == Side::Buy ? bids_ : asks_;
    return qty[static_cast<std::size_t>(offset)];
  }

  // Offset 0 is the tick next to the mid, the top of the book.
  [[nodiscard]] Price price(Side side, Price offset) const {
    return side == Side::Buy ? config_.mid - 1 - offset
                             : config_.mid + 1 + offset;
  }

  LevelUpdate add(Side side, Price offset) {
    const Qty qty = 1 + static_cast<Qty>(
                            below(static_cast<std::uint64_t>(config_.max_qty)));
    qty_at(side, offset) += qty;
    return {.side = side, .price = price(side, offset), .delta = qty};
  }

  LevelFlowConfig config_;
  std::mt19937_64 rng_;
  std::vector<Qty> bids_;
  std::vector<Qty> asks_;
};

} // namespace ob
