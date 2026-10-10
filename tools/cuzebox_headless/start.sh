#!/bin/bash
# Start cuzebox-ex (https://github.com/weber21w/cuzebox-ex) headless with its API on 127.0.0.1:24680.
#   CUZEBOX_EX_DIR = the cuzebox-ex checkout, built with `make -B TSYS=linux` (default ~/src/cuzebox-ex)
# Runs its own Xvfb on :99, uses a copy of cuzebox-ex's config.cfg with ApiServer=1 in ./run/, then presses
# F3 (game picture only) and F4 (frame limiter off) in the emulator window. Needs Xvfb and xdotool.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
CZ="${CUZEBOX_EX_DIR:-$HOME/src/cuzebox-ex}"
mkdir -p "$here/run"; cd "$here/run"
[ -f config.cfg ] || sed 's/^ApiServer=0/ApiServer=1/' "$CZ/config.cfg" > config.cfg
pkill -x cuzebox 2>/dev/null || true; pkill -f "Xvfb :99" 2>/dev/null || true; sleep 0.5
setsid Xvfb :99 -screen 0 1280x1024x24 > /dev/null 2>&1 < /dev/null &
sleep 1
DISPLAY=:99 SDL_AUDIODRIVER=dummy setsid "$CZ/cuzebox" > cuzebox.log 2>&1 < /dev/null &
for i in $(seq 1 50); do python3 -c "import socket; socket.create_connection(('127.0.0.1',24680),1)" 2>/dev/null && break; sleep 0.2; done
W=$(DISPLAY=:99 xdotool search --name CUzeBox | head -1)
[ -n "$W" ] && DISPLAY=:99 xdotool windowfocus --sync "$W" key F3 key F4
echo "cuzebox-ex is up (API 127.0.0.1:24680)"
