#!/bin/sh
# Render the README graphics (HTML/SVG sources here) to PNG with headless Chrome at 2x.
# Usage: docs/images/src/render.sh [name ...]   (default: all)
set -eu
cd "$(dirname "$0")"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
size() { sed -n 's/.*(\([0-9]*\)x\([0-9]*\)).*/\1,\2/p' "$1" | head -1; }
for f in ${@:-*.html}; do
  f="${f%.html}.html"
  wh=$(size "$f")
  "$CHROME" --headless --disable-gpu --hide-scrollbars --force-device-scale-factor=2 \
    --window-size="$wh" --screenshot="../${f%.html}.png" "file://$PWD/$f" 2>/dev/null
  echo "rendered ${f%.html}.png ($wh @2x)"
done
