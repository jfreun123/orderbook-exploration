#!/usr/bin/env bash
# Measures every book's per-message latency, on a fresh heap and on a
# scattered one, and draws the charts into results/.
#
#   scripts/docker.sh --cpuset-cpus=2 -- scripts/latency.sh
#
# Expects the `bench` preset to be built. Extra arguments go to every
# `latency` run (for example --orders 100000).
set -euo pipefail

build=${OB_BUILD_DIR:-build-bench}
out=results
mkdir -p "$out"

for book in map vector array; do
  for variant in fresh scattered; do
    flags=()
    [[ $variant == scattered ]] && flags+=(--scatter)
    "$build/benchmark/latency" "$book" ${flags[@]+"${flags[@]}"} "$@" \
      >"$out/latency-$book-$variant.csv" 2>"$out/latency-$book-$variant.txt"
    cat "$out/latency-$book-$variant.txt"
  done
  scripts/plot_latency.py "$out/latency-$book.png" \
    "$book book: fresh heap vs scattered heap" \
    "$out/latency-$book-fresh.csv" "$out/latency-$book-scattered.csv"
done

scripts/plot_latency.py "$out/latency-books.png" \
  "map vs vector vs array, fresh heap" \
  "$out/latency-map-fresh.csv" "$out/latency-vector-fresh.csv" \
  "$out/latency-array-fresh.csv"
