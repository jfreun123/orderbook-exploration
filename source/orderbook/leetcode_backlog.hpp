#pragma once

// LeetCode 1801, "Number of Orders in the Backlog":
// https://leetcode.com/problems/number-of-orders-in-the-backlog/
//
// Each order is {price, amount, orderType}, with orderType 0 for a buy and 1
// for a sell. A buy trades with the cheapest sell priced at or below it; a
// sell trades with the most expensive buy priced at or above it. Whatever is
// left over rests in the backlog. Return the total amount left in the backlog
// after every order, modulo 1e9 + 7.
//
// The class below is the submission as written for LeetCode, kept verbatim
// with its own formatting and comments, which is why clang-format is turned
// off around it.

#include <algorithm>
#include <functional>
#include <map>
#include <vector>

namespace leetcode {

using std::vector;

// clang-format off
class Solution {
    using Price = int;
    using Quantity = long long;
    using Bids = std::map<Price, Quantity, std::greater<>>;  // best = highest
    using Asks = std::map<Price, Quantity, std::less<>>;     // best = lowest

    enum Side { Buy = 0, Sell = 1 };
    static constexpr Quantity kMod = 1'000'000'007;

    static bool crossed(const Bids& bids, const Asks& asks) {
        return !bids.empty() && !asks.empty() &&
               bids.begin()->first >= asks.begin()->first;
    }

    // Matches top of book until uncrossed. Returns quantity filled per side.
    static Quantity uncross(Bids& bids, Asks& asks) {
        Quantity filled = 0;
        while (crossed(bids, asks)) {
            const auto bestBid = bids.begin();
            const auto bestAsk = asks.begin();
            const Quantity fill = std::min(bestBid->second, bestAsk->second);

            bestBid->second -= fill;
            bestAsk->second -= fill;
            filled += fill;

            if (bestBid->second == 0) bids.erase(bestBid);
            if (bestAsk->second == 0) asks.erase(bestAsk);
        }
        return filled;
    }

public:
    int getNumberOfBacklogOrders(vector<vector<int>>& orders) {
        Bids bids;
        Asks asks;
        Quantity backlog = 0;

        for (const auto& order : orders) {
            const Price price = order[0];
            const Quantity quantity = order[1];

            if (order[2] == Buy) bids[price] += quantity;
            else                 asks[price] += quantity;

            // Book was uncrossed before this order, so uncrossing now is
            // equivalent to matching the incoming order against the far side.
            backlog += quantity - 2 * uncross(bids, asks);
        }
        return static_cast<int>(backlog % kMod);
    }
};
// clang-format on

} // namespace leetcode
