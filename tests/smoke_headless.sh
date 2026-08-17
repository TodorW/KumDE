#!/bin/bash
# Headless smoke test: launches kumde on wlroots' headless backend (no
# GPU/display needed -- exactly what a CI runner has) with the pixman
# software renderer, takes a real screenshot via kumshot, and asserts it
# isn't suspiciously small (i.e. actually rendered something) before
# cleanly shutting the compositor down.
#
# This is the automated form of the "run it and look at a screenshot"
# method that caught kumde's worst historical bug: never sending the
# first configure event, so nothing ever rendered, with zero crash and
# zero compiler warning. A PNG of a screen that never rendered anything
# compresses to a few hundred bytes (solid color); a real desktop with
# a terminal renders to double-digit KB -- the size gap is large and
# reliable enough to use as a pass/fail signal without needing an image
# library on the test runner.
set -euo pipefail

KUMDE_BIN="${1:?usage: smoke_headless.sh /path/to/kumde /path/to/kumshot}"
KUMSHOT_BIN="${2:?usage: smoke_headless.sh /path/to/kumde /path/to/kumshot}"
MIN_BYTES=2000

WORKDIR=$(mktemp -d)
KUMDE_PID=""
cleanup() {
    [ -n "$KUMDE_PID" ] && kill "$KUMDE_PID" 2>/dev/null || true
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

export XDG_RUNTIME_DIR="$WORKDIR"
export WLR_BACKENDS=headless
export WLR_RENDERER=pixman
unset WAYLAND_DISPLAY DISPLAY

"$KUMDE_BIN" > "$WORKDIR/kumde.log" 2>&1 &
KUMDE_PID=$!

# kumde picks its own socket name (wl_display_add_socket_auto), not
# whatever WAYLAND_DISPLAY happened to be set to beforehand -- read the
# name it actually chose back out of its own startup log line.
wl_socket=""
for _ in $(seq 1 50); do
    wl_socket=$(sed -n 's/.*WAYLAND_DISPLAY=\(.*\)/\1/p' "$WORKDIR/kumde.log" | head -1)
    [ -n "$wl_socket" ] && [ -S "$WORKDIR/$wl_socket" ] && break
    wl_socket=""
    sleep 0.1
done

if [ -z "$wl_socket" ]; then
    echo "FAIL: kumde never created its Wayland socket"
    cat "$WORKDIR/kumde.log"
    exit 1
fi
export WAYLAND_DISPLAY="$wl_socket"

if ! kill -0 "$KUMDE_PID" 2>/dev/null; then
    echo "FAIL: kumde exited before becoming ready"
    cat "$WORKDIR/kumde.log"
    exit 1
fi

sleep 0.3

if ! "$KUMSHOT_BIN" -o "$WORKDIR/shot.png" > "$WORKDIR/kumshot.log" 2>&1; then
    echo "FAIL: kumshot could not capture the headless output"
    cat "$WORKDIR/kumshot.log"
    cat "$WORKDIR/kumde.log"
    exit 1
fi

size=$(wc -c < "$WORKDIR/shot.png")
if [ "$size" -lt "$MIN_BYTES" ]; then
    echo "FAIL: screenshot is only $size bytes (< $MIN_BYTES) -- looks like nothing rendered"
    cat "$WORKDIR/kumde.log"
    exit 1
fi

echo "PASS: headless smoke test (screenshot: ${size} bytes)"
