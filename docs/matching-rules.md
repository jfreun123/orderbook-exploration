# Matching rules

Every book in `source/orderbook/` follows these rules, and
`test/source/book_test.cpp` checks each one against every book.
`test/source/agreement_test.cpp` then feeds all the books the same 200,000
generated messages per seed and checks that they answer every message the
same way.

## The book

- Two sides: **bids** (resting buy orders) and **asks** (resting sell orders).
- Prices are whole ticks (`std::int64_t`), never floating point.
- **Price-time priority.** On each side, a better price trades first: the
  highest bid, the lowest ask. Between orders at the same price, the one that
  arrived first trades first.
- An order that is resting keeps its id until it fills or is cancelled. Ids
  are unique among resting orders.

## `add_limit(id, side, price, qty)`

1. Rejected (`false`) if `qty <= 0` or `id` is already resting. `ArrayBook`
   also rejects a price outside the range it was built for.
2. While the order has quantity left and the best opposite price **crosses**
   its limit (an ask at or below a buy's limit, a bid at or above a sell's
   limit), it trades with the oldest order at that best price.
3. Every trade happens at the **resting** order's price, for the smaller of
   the two quantities. A resting order that reaches zero leaves the book.
4. Whatever is left rests at the order's limit price, behind every order
   already there.

## `add_market(id, side, qty)`

Trades like a limit order with no price limit. Whatever cannot be filled is
dropped: a market order never rests. Rejected only if `qty <= 0`.

## `cancel(id)`

Removes a resting order, including what is left of a partly filled one.
Returns `false` if the id is not resting: unknown, already cancelled, or
already filled.

## `modify(id, price, qty)`

- Returns `false` if the id is not resting or `qty <= 0`.
- **Same price, same or smaller quantity:** the quantity changes in place and
  the order **keeps** its place in the queue.
- **Anything else** (new price, or more quantity): the order loses its place.
  It is pulled and sent again as a new limit order with the same id, so it
  goes to the back of its new queue, and it can trade if the new price
  crosses.

Keeping priority on a size-down and losing it otherwise is the common
exchange rule: shrinking an order gives nothing away, but adding size or
moving the price would let an order jump the queue.

## What these books leave out

These are the rules the code implements, not a full exchange: no
self-trade prevention, no hidden or iceberg orders, no time-in-force (IOC,
FOK, GTD), no auctions, no tick-size or lot-size checks.
