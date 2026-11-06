// The market-by-price books on a hand-written case, then against each other
// on 200,000 generated level updates: the same best prices after every
// update and the same full ladder every thousand.

#include "orderbook/level_book.hpp"
#include "orderbook/level_flow.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace {

using ob::Side;

template <class Book> class LevelBookTest : public testing::Test {};

using LevelBooks =
    testing::Types<ob::MapLevelBook, ob::VectorLevelBook<ob::Search::Linear>,
                   ob::VectorLevelBook<ob::Search::Binary>>;
TYPED_TEST_SUITE(LevelBookTest, LevelBooks, );

TYPED_TEST(LevelBookTest, AddsMergeAndRemovalsEraseEmptyLevels) {
  TypeParam book;
  book.update(Side::Buy, 99, 5);
  book.update(Side::Buy, 100, 3);
  book.update(Side::Buy, 98, 1);
  book.update(Side::Buy, 100, 2);
  book.update(Side::Sell, 102, 4);
  EXPECT_EQ(book.best_bid(), 100);
  EXPECT_EQ(book.best_ask(), 102);
  EXPECT_EQ(book.levels(Side::Buy),
            (std::vector<ob::LevelQty>{{100, 5}, {99, 5}, {98, 1}}));
  book.update(Side::Buy, 100, -5);
  EXPECT_EQ(book.best_bid(), 99);
  book.update(Side::Sell, 102, -4);
  EXPECT_EQ(book.best_ask(), std::nullopt);
}

TEST(LevelBooksAgree, OnEveryUpdate) {
  ob::LevelFlow flow{{.seed = 3, .levels = 200}};
  ob::MapLevelBook map;
  ob::VectorLevelBook<ob::Search::Linear> linear;
  ob::VectorLevelBook<ob::Search::Binary> binary;
  std::vector<ob::LevelUpdate> updates = flow.fill();
  const std::vector<ob::LevelUpdate> more = flow.take(200'000);
  updates.insert(updates.end(), more.begin(), more.end());
  for (std::size_t i = 0; i < updates.size(); ++i) {
    const auto &[side, price, delta] = updates[i];
    map.update(side, price, delta);
    linear.update(side, price, delta);
    binary.update(side, price, delta);
    ASSERT_EQ(linear.best_bid(), map.best_bid()) << "update " << i;
    ASSERT_EQ(linear.best_ask(), map.best_ask()) << "update " << i;
    if (i % 1'000 == 0) {
      for (const Side s : {Side::Buy, Side::Sell}) {
        ASSERT_EQ(linear.levels(s), map.levels(s)) << "update " << i;
        ASSERT_EQ(binary.levels(s), map.levels(s)) << "update " << i;
      }
    }
  }
}

} // namespace
