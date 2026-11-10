#!/bin/sh
# Renders shots.html into out/<lang>/<n>.png (1320×2868) with headless Chrome.
set -eu
cd "$(dirname "$0")"
chrome="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
for l in ko ja en; do
  mkdir -p out/$l
  for n in 1 2 3 4 5 6 7; do
    "$chrome" --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --window-size=1320,2868 \
      --allow-file-access-from-files --virtual-time-budget=6000 --screenshot="$PWD/out/$l/$n.png" "file://$PWD/shots.html?l=$l&s=$n" 2>/dev/null
  done
done
