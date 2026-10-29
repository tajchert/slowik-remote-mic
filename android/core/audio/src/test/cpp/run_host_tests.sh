#!/bin/sh
# Host-side protocol tests (no Android deps). Usage: ./run_host_tests.sh
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/slowik_protocol_test"
${CXX:-c++} -std=c++17 -Wall -Wextra -o "$OUT" protocol_test.cpp
"$OUT"
