#include "orderbook/leetcode_backlog.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

int backlog(std::vector<std::vector<int>> orders) {
  return leetcode::Solution{}.getNumberOfBacklogOrders(orders);
}

TEST(LeetcodeBacklog, ExampleOne) {
  EXPECT_EQ(backlog({{10, 5, 0}, {15, 2, 1}, {25, 1, 1}, {30, 4, 0}}), 6);
}

TEST(LeetcodeBacklog, ExampleTwoWrapsModulo) {
  EXPECT_EQ(
      backlog(
          {{7, 1'000'000'000, 1}, {15, 3, 0}, {5, 999'999'995, 0}, {5, 1, 1}}),
      999'999'984);
}

TEST(LeetcodeBacklog, NothingCrosses) {
  EXPECT_EQ(backlog({{10, 3, 0}, {11, 4, 1}}), 7);
}

TEST(LeetcodeBacklog, SweepsSeveralLevels) {
  // The sell for 10 at 9 takes 2 at 11 and 3 at 10, then rests 5 at 9.
  EXPECT_EQ(backlog({{11, 2, 0}, {10, 3, 0}, {8, 4, 0}, {9, 10, 1}}), 9);
}

} // namespace
