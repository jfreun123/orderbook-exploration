// Feeds the same generated message stream to every order-by-order book and
// checks that they answer every message the same way and end every step in
// the same state: the same fills, best prices and order count after every
// message, and the same top-20 snapshot every thousand. NaiveBook is the
// reference. The last check makes sure the stream exercised matching, not
// just built a book.

#include "orderbook/array_book.hpp"
#include "orderbook/flow.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

namespace {

class Agreement : public testing::TestWithParam<std::uint64_t> {};

TEST_P(Agreement, AllBooksAgreeOnEveryMessage) {
  const ob::FlowConfig config{.seed = GetParam(), .target_orders = 2'000};
  ob::Flow flow{config};
  ob::NaiveBook naive;
  ob::MapBook map;
  ob::VectorBook vector;
  ob::ArrayBook array{config.max_price};
  ob::Fills naive_fills;
  ob::Fills map_fills;
  ob::Fills vector_fills;
  ob::Fills array_fills;

  constexpr std::size_t kMessages = 200'000;
  constexpr std::size_t kSnapshotEvery = 1'000;
  std::size_t fills_seen = 0;
  for (std::size_t i = 0; i < kMessages; ++i) {
    const ob::Event event = flow.next();
    naive_fills.clear();
    map_fills.clear();
    vector_fills.clear();
    array_fills.clear();
    const bool naive_ok = ob::apply(naive, event, naive_fills);
    ASSERT_EQ(ob::apply(map, event, map_fills), naive_ok) << "message " << i;
    ASSERT_EQ(ob::apply(vector, event, vector_fills), naive_ok)
        << "message " << i;
    ASSERT_EQ(ob::apply(array, event, array_fills), naive_ok)
        << "message " << i;
    ASSERT_EQ(map_fills, naive_fills) << "message " << i;
    ASSERT_EQ(vector_fills, naive_fills) << "message " << i;
    ASSERT_EQ(array_fills, naive_fills) << "message " << i;
    ASSERT_EQ(map.best_bid(), naive.best_bid()) << "message " << i;
    ASSERT_EQ(vector.best_bid(), naive.best_bid()) << "message " << i;
    ASSERT_EQ(array.best_bid(), naive.best_bid()) << "message " << i;
    ASSERT_EQ(map.best_ask(), naive.best_ask()) << "message " << i;
    ASSERT_EQ(vector.best_ask(), naive.best_ask()) << "message " << i;
    ASSERT_EQ(array.best_ask(), naive.best_ask()) << "message " << i;
    ASSERT_EQ(map.order_count(), naive.order_count()) << "message " << i;
    ASSERT_EQ(vector.order_count(), naive.order_count()) << "message " << i;
    ASSERT_EQ(array.order_count(), naive.order_count()) << "message " << i;
    if (i % kSnapshotEvery == 0) {
      const ob::Snapshot expected = naive.snapshot(20);
      ASSERT_EQ(map.snapshot(20), expected) << "message " << i;
      ASSERT_EQ(vector.snapshot(20), expected) << "message " << i;
      ASSERT_EQ(array.snapshot(20), expected) << "message " << i;
    }
    fills_seen += naive_fills.size();
  }
  EXPECT_GT(fills_seen, kMessages / 20);
}

INSTANTIATE_TEST_SUITE_P(Seeds, Agreement, testing::Values(1, 2, 3));

} // namespace
