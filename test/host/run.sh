#!/bin/sh
# Build and run the host-side decoder tests (no board needed).
set -e
cd "$(dirname "$0")"
out="${TMPDIR:-/tmp}/einktodo-rle1_test"
c++ -std=c++11 -O2 -Wall -Wextra -o "$out" rle1_test.cpp
"$out" "$@"
