#!/bin/sh
# Build and run the loopback check (driver must be installed).
set -eu
cd "$(dirname "$0")"
mkdir -p ../build
clang++ -std=c++17 -O2 loopback_check.cpp -framework CoreAudio -framework CoreFoundation -o ../build/loopback_check
exec ../build/loopback_check
