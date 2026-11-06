#!/usr/bin/env bash
# Run a command inside the pinned toolchain image, with the repo mounted at
# /work.
#
#   scripts/docker.sh cmake --preset dev
#   scripts/docker.sh --cpuset-cpus=2 -- ./build-bench/benchmark/benchmark
#
# Arguments before a `--` go to `docker run`, so every setting a measurement
# depends on is spelled out on the command line that ran it.
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
image=orderbook-dev

docker build --quiet --tag "$image" - <"$root/Dockerfile" >/dev/null

run_flags=()
for arg in "$@"; do
  if [[ $arg == -- ]]; then
    while [[ $1 != -- ]]; do
      run_flags+=("$1")
      shift
    done
    shift
    break
  fi
done

if [[ -t 0 && -t 1 ]]; then
  run_flags+=(--interactive --tty)
fi

# Run as the calling user so build output in the mounted repo is not owned by
# root. That user has no home directory in the image, and git wants one.
exec docker run --rm --init \
  --user "$(id -u):$(id -g)" --env HOME=/tmp \
  --volume "$root:/work" --workdir /work \
  ${run_flags[@]+"${run_flags[@]}"} \
  "$image" "$@"
