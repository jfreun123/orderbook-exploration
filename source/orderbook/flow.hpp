#pragma once

// Synthetic order flow: a seeded stream of limit orders, market orders,
// cancels and modifies around a drifting mid price. It is not real market
// data. It is a repeatable stream with the same mix of messages a real feed
// has, so every book can be fed the same messages and timed or checked
// against the others. apply() sends one event to a book and returns what
// the book answered.
//
// The stream adds orders faster than it cancels them until about
// target_orders have been sent and not yet cancelled, then holds there with
// the mix in FlowConfig: limit_pct, market_pct and modify_pct out of 100,
// the rest cancels. The mid takes a slow random walk, one tick at a time.
// Limit prices land within spread ticks of the mid, mostly on their own
// side so they rest, sometimes a tick or two through it so they trade.
// Modify and cancel pick an order this stream sent and has not cancelled; it
// may have filled since, in which case the book answers false, as happens
// on a real feed too. An Event's price is unused for Market and Cancel, and
// its qty for Cancel.
//
// Random numbers come from rng() % bound rather than the standard
// distributions, which give different numbers on libstdc++ and libc++; the
// stream has to be the same everywhere.

#include "orderbook/types.hpp"

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
  Price price;
  Qty qty;
};

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
  Price start_mid = 10'000;
  Price max_price = 20'000;
  Price spread = 20;
  Qty max_qty = 10;
  std::size_t target_orders = 1'000;
  int limit_pct = 45;
  int market_pct = 5;
  int modify_pct = 10;
};

class Flow {
public:
  explicit Flow(const FlowConfig &config)
      : config_(config), rng_(config.seed), mid_(config.start_mid) {}

  Event next() {
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
  struct Live {
    OrderId id;
    Side side;
  };

  std::uint64_t below(std::uint64_t bound) { return rng_() % bound; }

  Side side() { return below(2) == 0 ? Side::Buy : Side::Sell; }

  Qty qty() {
    return 1 +
           static_cast<Qty>(below(static_cast<std::uint64_t>(config_.max_qty)));
  }

  Price limit_price(Side side) {
    const auto away =
        static_cast<Price>(below(static_cast<std::uint64_t>(config_.spread))) -
        2;
    return side == Side::Buy ? mid_ - away : mid_ + away;
  }

  Event limit() {
    const OrderId id = next_id_++;
    const Side side = this->side();
    live_.push_back({.id = id, .side = side});
    return {.type = EventType::Limit,
            .id = id,
            .side = side,
            .price = limit_price(side),
            .qty = qty()};
  }

  Event market() {
    return {.type = EventType::Market,
            .id = next_id_++,
            .side = side(),
            .price = 0,
            .qty = qty()};
  }

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

  FlowConfig config_;
  std::mt19937_64 rng_;
  Price mid_;
  OrderId next_id_ = 1;
  std::vector<Live> live_;
};

} // namespace ob
