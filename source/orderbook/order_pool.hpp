#pragma once

// Storage for resting orders, shared by VectorBook and ArrayBook.
//
// Orders live in one vector that is reused through a free list, so adding an
// order does not allocate once the pool has grown, and orders sit next to
// each other in memory. The orders at one price form a doubly linked list
// threaded through the pool by index (an intrusive list), oldest at the head
// so it trades first, which lets an order be unlinked in O(1) without
// searching its queue. A hash map from order id to pool index finds an order
// for cancel and modify; find() returns kNone when the id is not resting.
//
// push_back() adds an order at the back of a queue, erase() takes one out of
// its queue and frees it, and view() appends a queue's orders to a snapshot
// level.

#include "orderbook/types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ob {

class OrderPool {
public:
  using Index = std::uint32_t;
  static constexpr Index kNone = std::numeric_limits<Index>::max();

  struct Node {
    OrderId id;
    Price price;
    Qty qty;
    Index prev;
    Index next;
    Side side;
  };

  struct Queue {
    Index head = kNone;
    Index tail = kNone;

    [[nodiscard]] bool empty() const { return head == kNone; }
  };

  [[nodiscard]] bool contains(OrderId id) const { return index_.contains(id); }

  [[nodiscard]] std::size_t size() const { return index_.size(); }

  [[nodiscard]] Index find(OrderId id) const {
    auto found = index_.find(id);
    return found == index_.end() ? kNone : found->second;
  }

  Node &operator[](Index node) { return nodes_[node]; }

  Index push_back(Queue &queue, OrderId id, Side side, Price price, Qty qty) {
    const Index node = allocate();
    nodes_[node] = {.id = id,
                    .price = price,
                    .qty = qty,
                    .prev = queue.tail,
                    .next = kNone,
                    .side = side};
    if (queue.tail == kNone) {
      queue.head = node;
    } else {
      nodes_[queue.tail].next = node;
    }
    queue.tail = node;
    index_.emplace(id, node);
    return node;
  }

  void erase(Queue &queue, Index node) {
    const Node &order = nodes_[node];
    if (order.prev == kNone) {
      queue.head = order.next;
    } else {
      nodes_[order.prev].next = order.next;
    }
    if (order.next == kNone) {
      queue.tail = order.prev;
    } else {
      nodes_[order.next].prev = order.prev;
    }
    index_.erase(order.id);
    free_.push_back(node);
  }

  void view(const Queue &queue, Price price,
            std::vector<LevelView> &out) const {
    LevelView &level =
        out.emplace_back(LevelView{.price = price, .orders = {}});
    for (Index node = queue.head; node != kNone; node = nodes_[node].next) {
      level.orders.push_back({.id = nodes_[node].id, .qty = nodes_[node].qty});
    }
  }

private:
  Index allocate() {
    if (!free_.empty()) {
      const Index node = free_.back();
      free_.pop_back();
      return node;
    }
    nodes_.emplace_back();
    return static_cast<Index>(nodes_.size() - 1);
  }

  std::vector<Node> nodes_;
  std::vector<Index> free_;
  std::unordered_map<OrderId, Index> index_;
};

} // namespace ob
