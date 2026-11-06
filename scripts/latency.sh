#!/usr/bin/env bash
# Measures every order-by-order book's per-message latency, on a fresh heap
# and on a scattered one, first with the book hot in cache (a tight loop),
# then with 4 MB of other memory read between messages, the way the rest of
# a trading system would push the book out of cache. Charts go to results/:
# one per book comparing the two heaps, one comparing the books hot, and one
# comparing them cold.
#
#   scripts/docker.sh --cpuset-cpus=2 -- scripts/latency.sh
#
# Expects the `bench` preset to be built. Extra arguments go to every
# `latency` run, for example --orders 100000.
set -euo pipefail

build=${OB_BUILD_DIR:-build-bench}
out=results
mkdir -p "$out"

run() {
  local name=$1 book=$2
  shift 2
  "$build/benchmark/latency" "$book" "$@" \
    >"$out/$name.csv" 2>"$out/$name.txt"
  grep -v '^op ' "$out/$name.txt"
}

for book in map vector array; do
  run "latency-$book-fresh" "$book" "$@"
  run "latency-$book-scattered" "$book" --scatter "$@"
  run "latency-cold-$book-fresh" "$book" --messages 300000 --evict 4096 "$@"
  run "latency-cold-$book-scattered" "$book" --messages 300000 --evict 4096 --scatter "$@"
  scripts/plot_latency.py "$out/latency-$book.png" \
    "$book book: fresh heap vs scattered heap" \
    "$out/latency-$book-fresh.csv" "$out/latency-$book-scattered.csv"
done

scripts/plot_latency.py "$out/latency-books.png" \
  "map vs vector vs array, fresh heap" \
  "$out/latency-map-fresh.csv" "$out/latency-vector-fresh.csv" \
  "$out/latency-array-fresh.csv"

MAX_NS=1000 scripts/plot_latency.py "$out/latency-cold.png" \
  "map vs vector vs array, book pushed out of cache, scattered heap" \
  "$out/latency-cold-map-scattered.csv" "$out/latency-cold-vector-scattered.csv" \
  "$out/latency-cold-array-scattered.csv"
