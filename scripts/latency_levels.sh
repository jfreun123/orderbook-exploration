#!/usr/bin/env bash
# The market-by-price comparison from David Gross's talk: std::map vs the
# reverse vector (linear and binary search), on a fresh and a scattered
# heap, first with the book hot in cache (a tight loop), then with 4 MB of
# other memory read between updates, the way the rest of a trading system
# would push the book out of cache. Charts and CSVs go to results/.
#
#   scripts/docker.sh --cpuset-cpus=2 -- scripts/latency_levels.sh
#
# Expects the `bench` preset to be built.
set -euo pipefail

build=${OB_BUILD_DIR:-build-bench}
out=results
mkdir -p "$out"

run() {
  local name=$1 book=$2
  shift 2
  "$build/benchmark/latency" "$book" --levels 1000 "$@" \
    >"$out/$name.csv" 2>"$out/$name.txt"
  grep -v '^op ' "$out/$name.txt"
}

for book in map-levels vector-levels vector-levels-binary; do
  run "levels-hot-$book-fresh" "$book"
  run "levels-hot-$book-scattered" "$book" --scatter
  run "levels-cold-$book-fresh" "$book" --messages 300000 --evict 4096
  run "levels-cold-$book-scattered" "$book" --messages 300000 --evict 4096 --scatter
done

MAX_NS=200 scripts/plot_latency.py "$out/levels-hot.png" \
  "Price levels, book hot in cache: std::map vs reverse vector" \
  "$out/levels-hot-map-levels-fresh.csv" \
  "$out/levels-hot-vector-levels-fresh.csv"

MAX_NS=1000 scripts/plot_latency.py "$out/levels-cold-map.png" \
  "std::map price levels, book pushed out of cache: fresh vs scattered heap" \
  "$out/levels-cold-map-levels-fresh.csv" \
  "$out/levels-cold-map-levels-scattered.csv"

MAX_NS=1000 scripts/plot_latency.py "$out/levels-cold.png" \
  "Price levels, book pushed out of cache (scattered heap): std::map vs reverse vector" \
  "$out/levels-cold-map-levels-scattered.csv" \
  "$out/levels-cold-vector-levels-scattered.csv" \
  "$out/levels-cold-vector-levels-binary-scattered.csv"
