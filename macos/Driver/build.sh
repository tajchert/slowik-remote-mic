#!/bin/sh
# Configure + build SlowikMic.driver (Release) and ad-hoc sign it.
set -eu
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >/dev/null
cmake --build build --config Release -j --target SlowikMic loopback_ring_test
codesign --force --sign - --deep build/SlowikMic.driver
codesign -v build/SlowikMic.driver && echo "OK: build/SlowikMic.driver"
