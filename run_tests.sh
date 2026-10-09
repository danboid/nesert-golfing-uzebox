#!/bin/sh
# Host regression: builds the real game code against a simulated mode 6 screen and plays it with a
# trial-and-error bot (fork a copy of the game, try a swing headless, keep the best one).
set -e
cd "$(dirname "$0")"
python3 tools/gen_data.py
mkdir -p test_out
for cfg in "0:" "3:"; do
  last=${cfg%%:*}
  rm -f test_host
  make -f Makefile.host LAST=$last >/dev/null
  echo "=== LAST_HOLE_TEST=$last"
  ./test_host --holes=${HOLES:-120} --frames=600000 --out=test_out | grep -E "reached|strokes|ramtiles|overflow"
done
