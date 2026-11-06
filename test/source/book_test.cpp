// Every rule in docs/matching-rules.md, checked against every book.

#include "orderbook/array_book.hpp"
#include "orderbook/book_concept.hpp"
#include "orderbook/map_book.hpp"
#include "orderbook/naive_book.hpp"
#include "orderbook/vector_book.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace {

using ob::Fills;
using ob::Side;

static_assert(ob::OrderBook<ob::NaiveBook>);
static_assert(ob::OrderBook<ob::MapBook>);
static_assert(ob::OrderBook<ob::VectorBook>);
static_assert(ob::OrderBook<ob::ArrayBook>);

template <class Book> Book make_book() { return Book{}; }
template <> ob::ArrayBook make_book<ob::ArrayBook>() {
  return ob::ArrayBook{1'000};
}

template <class Book> class BookTest : public testing::Test {
protected:
  Book book = make_book<Book>(); // NOLINT(*-non-private-member-*)
  Fills fills;                   // NOLINT(*-non-private-member-*)

  bool buy(ob::OrderId id, ob::Price price, ob::Qty qty) {
    return book.add_limit(id, Side::Buy, price, qty, fills);
  }
  bool sell(ob::OrderId id, ob::Price price, ob::Qty qty) {
    return book.add_limit(id, Side::Sell, price, qty, fills);
  }
};

using Books =
    testing::Types<ob::NaiveBook, ob::MapBook, ob::VectorBook, ob::ArrayBook>;
TYPED_TEST_SUITE(BookTest, Books, );

TYPED_TEST(BookTest, EmptyBookHasNoBestPrices) {
  EXPECT_EQ(this->book.best_bid(), std::nullopt);
  EXPECT_EQ(this->book.best_ask(), std::nullopt);
  EXPECT_EQ(this->book.order_count(), 0U);
}

TYPED_TEST(BookTest, LimitOrdersThatDoNotCrossRest) {
  EXPECT_TRUE(this->buy(1, 99, 5));
  EXPECT_TRUE(this->buy(2, 100, 5));
  EXPECT_TRUE(this->sell(3, 102, 5));
  EXPECT_TRUE(this->sell(4, 101, 5));
  EXPECT_TRUE(this->fills.empty());
  EXPECT_EQ(this->book.best_bid(), 100);
  EXPECT_EQ(this->book.best_ask(), 101);
  EXPECT_EQ(this->book.order_count(), 4U);
}

TYPED_TEST(BookTest, SamePriceTradesOldestFirst) {
  this->buy(1, 100, 5);
  this->buy(2, 100, 5);
  this->sell(3, 100, 7);
  EXPECT_EQ(this->fills, (Fills{{3, 1, 100, 5}, {3, 2, 100, 2}}));
  // Order 2 keeps the front of the queue with what is left of it.
  EXPECT_EQ(this->book.snapshot(1).bids[0].orders,
            (std::vector<ob::RestingOrder>{{2, 3}}));
}

TYPED_TEST(BookTest, BetterPriceTradesFirstEvenIfLater) {
  this->sell(1, 101, 5);
  this->sell(2, 100, 5);
  this->buy(3, 101, 5);
  EXPECT_EQ(this->fills, (Fills{{3, 2, 100, 5}}));
}

TYPED_TEST(BookTest, TradesHappenAtTheRestingPrice) {
  this->sell(1, 100, 5);
  this->buy(2, 105, 5);
  EXPECT_EQ(this->fills, (Fills{{2, 1, 100, 5}}));
}

TYPED_TEST(BookTest, SweepsLevelsThenRestsTheRemainder) {
  this->sell(1, 100, 2);
  this->sell(2, 101, 2);
  this->sell(3, 103, 2);
  this->buy(4, 102, 10);
  EXPECT_EQ(this->fills, (Fills{{4, 1, 100, 2}, {4, 2, 101, 2}}));
  EXPECT_EQ(this->book.best_bid(), 102);
  EXPECT_EQ(this->book.best_ask(), 103);
  EXPECT_EQ(this->book.snapshot(1).bids[0].orders,
            (std::vector<ob::RestingOrder>{{4, 6}}));
}

TYPED_TEST(BookTest, MarketOrderTakesAnyPriceAndNeverRests) {
  this->sell(1, 100, 2);
  this->sell(2, 900, 2);
  EXPECT_TRUE(this->book.add_market(3, Side::Buy, 10, this->fills));
  EXPECT_EQ(this->fills, (Fills{{3, 1, 100, 2}, {3, 2, 900, 2}}));
  EXPECT_EQ(this->book.order_count(), 0U);
  EXPECT_EQ(this->book.best_bid(), std::nullopt);
}

TYPED_TEST(BookTest, MarketOrderIntoEmptyBookDoesNothing) {
  EXPECT_TRUE(this->book.add_market(1, Side::Sell, 10, this->fills));
  EXPECT_TRUE(this->fills.empty());
  EXPECT_EQ(this->book.order_count(), 0U);
}

TYPED_TEST(BookTest, CancelRemovesTheOrder) {
  this->buy(1, 100, 5);
  this->buy(2, 99, 5);
  EXPECT_TRUE(this->book.cancel(1));
  EXPECT_EQ(this->book.best_bid(), 99);
  EXPECT_FALSE(this->book.cancel(1));
  EXPECT_FALSE(this->book.cancel(42));
}

TYPED_TEST(BookTest, CancelAfterPartialFillRemovesWhatIsLeft) {
  this->buy(1, 100, 5);
  this->sell(2, 100, 3);
  EXPECT_TRUE(this->book.cancel(1));
  EXPECT_EQ(this->book.order_count(), 0U);
}

TYPED_TEST(BookTest, CancelOfAFilledOrderFails) {
  this->buy(1, 100, 5);
  this->sell(2, 100, 5);
  EXPECT_FALSE(this->book.cancel(1));
}

TYPED_TEST(BookTest, ShrinkingKeepsQueuePriority) {
  this->buy(1, 100, 5);
  this->buy(2, 100, 5);
  EXPECT_TRUE(this->book.modify(1, 100, 3, this->fills));
  this->sell(3, 100, 3);
  EXPECT_EQ(this->fills, (Fills{{3, 1, 100, 3}}));
}

TYPED_TEST(BookTest, GrowingLosesQueuePriority) {
  this->buy(1, 100, 5);
  this->buy(2, 100, 5);
  EXPECT_TRUE(this->book.modify(1, 100, 6, this->fills));
  this->sell(3, 100, 5);
  EXPECT_EQ(this->fills, (Fills{{3, 2, 100, 5}}));
}

TYPED_TEST(BookTest, RepricingCanTrade) {
  this->buy(1, 98, 5);
  this->sell(2, 100, 3);
  EXPECT_TRUE(this->book.modify(1, 100, 5, this->fills));
  EXPECT_EQ(this->fills, (Fills{{1, 2, 100, 3}}));
  EXPECT_EQ(this->book.best_bid(), 100);
  EXPECT_EQ(this->book.best_ask(), std::nullopt);
}

TYPED_TEST(BookTest, ModifyOfUnknownOrderFails) {
  EXPECT_FALSE(this->book.modify(7, 100, 5, this->fills));
}

TYPED_TEST(BookTest, RejectsBadOrders) {
  EXPECT_FALSE(this->buy(1, 100, 0));
  EXPECT_FALSE(this->buy(1, 100, -1));
  EXPECT_TRUE(this->buy(1, 100, 5));
  EXPECT_FALSE(this->sell(1, 105, 5)) << "duplicate id";
  EXPECT_FALSE(this->book.add_market(2, Side::Sell, 0, this->fills));
  EXPECT_FALSE(this->book.modify(1, 100, 0, this->fills));
  EXPECT_EQ(this->book.order_count(), 1U);
}

TYPED_TEST(BookTest, SnapshotIsBestFirstAndLimitedToDepth) {
  this->buy(1, 98, 1);
  this->buy(2, 100, 2);
  this->buy(3, 100, 3);
  this->buy(4, 99, 4);
  this->sell(5, 103, 5);
  const ob::Snapshot snap = this->book.snapshot(2);
  EXPECT_EQ(snap.bids, (std::vector<ob::LevelView>{
                           {100, {{2, 2}, {3, 3}}},
                           {99, {{4, 4}}},
                       }));
  EXPECT_EQ(snap.asks, (std::vector<ob::LevelView>{{103, {{5, 5}}}}));
}

TEST(ArrayBookRange, RejectsPricesOutsideTheRange) {
  ob::ArrayBook book{1'000};
  Fills fills;
  EXPECT_FALSE(book.add_limit(1, Side::Buy, -1, 5, fills));
  EXPECT_FALSE(book.add_limit(1, Side::Buy, 1'000, 5, fills));
  EXPECT_TRUE(book.add_limit(1, Side::Buy, 999, 5, fills));
  EXPECT_FALSE(book.modify(1, 1'000, 5, fills));
}

} // namespace
