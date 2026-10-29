#!/bin/sh
# End-to-end check: slowik-rx (real time, host clock) <- tools/slowik_sim.py over UDP on localhost.
# usage: macos/Receiver/scripts/integration.sh [seconds] [extra slowik_sim.py send args]
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SECS=${1:-30}; shift || true
OUT=${TMPDIR:-/tmp}/slowik-rx-integration
PORT=${SLOWIK_PORT:-47811}  # not 47800: a real phone on the LAN may be streaming there
mkdir -p "$OUT"
(cd "$HERE" && swift build -c release --product slowik-rx >/dev/null)
BIN=$(cd "$HERE" && swift build -c release --show-bin-path)/slowik-rx
"$BIN" --port $PORT --wav "$OUT/rx.wav" --seconds $((SECS + 4)) --live --no-bonjour > "$OUT/rx.log" 2>&1 &
sleep 1
python3 "$ROOT/tools/slowik_sim.py" send --host 127.0.0.1 --port $PORT --seconds "$SECS" --loss 0.02 --jitter-ms 8 --ppm 150 "$@" > "$OUT/sim.log"
wait
grep -E "t= *(5|10|20|$SECS)s|summary" "$OUT/rx.log"
python3 "$HERE/scripts/wavcheck.py" "$OUT/rx.wav"
