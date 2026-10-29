#!/bin/sh
# Install the driver into the system HAL folder and restart coreaudiod.
set -eu
cd "$(dirname "$0")"
[ -d build/SlowikMic.driver ] || { echo "run ./build.sh first" >&2; exit 1; }
sudo rm -rf /Library/Audio/Plug-Ins/HAL/SlowikMic.driver
sudo cp -R build/SlowikMic.driver /Library/Audio/Plug-Ins/HAL/
sudo killall -9 coreaudiod || true
echo "Installed. coreaudiod respawns in ~2-3 s; then 'Slowik Mic' appears in Sound settings."
