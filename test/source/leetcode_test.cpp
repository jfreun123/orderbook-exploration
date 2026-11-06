// The LeetCode solution on the problem's two examples, on a book that never
// crosses, and on an order that sweeps several levels.

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
  EXPECT_EQ(backlog({{11, 2, 0}, {10, 3, 0}, {8, 4, 0}, {9, 10, 1}}), 9);
}

} // namespace
