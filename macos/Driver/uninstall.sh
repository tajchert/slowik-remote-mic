#!/bin/sh
set -eu
sudo rm -rf /Library/Audio/Plug-Ins/HAL/SlowikMic.driver
sudo killall -9 coreaudiod || true
echo "Uninstalled."
