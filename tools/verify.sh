#!/bin/sh
# tools/verify.sh — one-command build + GPU-parity check for dotpipe.
#
# Usage: tools/verify.sh <input.ppm|input.mp4> <dotpipe effect flag> [params...]
#   still:  tools/verify.sh testimgs/bars.ppm --dotgate 28 8 10
#   clip:   tools/verify.sh inputvideos/model.mp4 --spacengrave 5 85 80 68 200 78 360 100 5
#
# What it does, in order:
#   1. builds dotpipe/dotpipe with the direct-cc recipe from tools/TOOLS.md
#      (NOT make — make hangs on this volume), failing loudly with the log
#   2. probes width/height (ffprobe) and decodes to raw RGB24 — a single
#      frame for image inputs, a short 1-second clip for video inputs
#   3. runs RETROFX_BACKEND=dual ./dotpipe/dotpipe -w W -h H <flag> [params...]
#   4. prints the raw stderr, then one verdict line:
#        PASS = dotpipe ran clean, full frame count through, no MISMATCH
#        WARN = "Metal unavailable/failed" (CPU-only environment; the dual
#               byte-compare degraded to CPU — not a parity failure)
#        FAIL = build error, non-zero exit, a MISMATCH line, or a short
#               frame count (truncated pipe)
#
# Exit status: 0 = PASS or WARN, 1 = FAIL, 2 = usage error.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
FFMPEG=${FFMPEG:-/opt/homebrew/bin/ffmpeg}
FFPROBE=${FFPROBE:-/opt/homebrew/bin/ffprobe}

if [ "$#" -lt 2 ]; then
    echo "usage: tools/verify.sh <input.ppm|input.mp4> <dotpipe effect flag> [params...]" >&2
    exit 2
fi

IN=$1; shift
FLAGS="$*"

case "$IN" in
    *.ppm|*.pgm|*.png|*.jpg|*.jpeg|*.bmp|*.tga|*.tif|*.tiff) MODE=still ;;
    *) MODE=clip ;;
esac

WORK=$(mktemp -d /tmp/verifysh.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
RAW="$WORK/in.raw"
OUT="$WORK/out.raw"
STDERRF="$WORK/stderr"
: > "$STDERRF"

cd "$ROOT" || exit 1

# --- 1. build (direct cc, per tools/TOOLS.md) ---
if ! cc -std=c99 -O2 -Wall -Wextra -Werror -Icore -o dotpipe/dotpipe \
        dotpipe/dotpipe.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/metal_fx.m \
        -lm -framework Metal -framework Foundation 2>"$WORK/build.log"; then
    echo "FAIL: build"
    cat "$WORK/build.log"
    exit 1
fi

# --- 2. dimensions + decode to raw RGB24 ---
DIM=$("$FFPROBE" -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=p=0 "$IN" 2>&1)
W=${DIM%%,*}; H=${DIM##*,}
case "$W$H" in
    *[!0-9]*|'') echo "FAIL: cannot determine width/height of $IN ($DIM)"; exit 1 ;;
esac

if [ "$MODE" = still ]; then
    "$FFMPEG" -v error -y -i "$IN" -f rawvideo -pix_fmt rgb24 "$RAW" 2>"$STDERRF" || {
        echo "FAIL: decode"; cat "$STDERRF"; exit 1; }
else
    "$FFMPEG" -v error -y -ss 1 -t 1 -i "$IN" -f rawvideo -pix_fmt rgb24 "$RAW" 2>"$STDERRF" || {
        echo "FAIL: decode"; cat "$STDERRF"; exit 1; }
fi

INBYTES=$(wc -c < "$RAW" | tr -d '[:space:]')
FRAME=$((W * H * 3))
if [ "$INBYTES" -lt "$FRAME" ] || [ $((INBYTES % FRAME)) -ne 0 ]; then
    echo "FAIL: decoded raw is not a whole number of ${W}x${H} frames ($INBYTES bytes)"; exit 1
fi
NFRAMES=$((INBYTES / FRAME))

# --- 3. dual-backend run ---
RETROFX_BACKEND=dual ./dotpipe/dotpipe -w "$W" -h "$H" $FLAGS \
    < "$RAW" > "$OUT" 2>"$STDERRF"
RC=$?
OUTBYTES=$(wc -c < "$OUT" | tr -d '[:space:]') 2>/dev/null || OUTBYTES=0

# --- 4. verdict ---
VERD=0
if [ "$RC" -ne 0 ]; then
    VERD=1; echo "FAIL: dotpipe exited $RC ($MODE, ${W}x${H}, $NFRAMES frame(s), $FLAGS)"
elif grep -q 'MISMATCH' "$STDERRF"; then
    VERD=1; echo "FAIL: dual MISMATCH ($MODE, ${W}x${H}, $NFRAMES frame(s), $FLAGS)"
elif [ "$OUTBYTES" -ne "$INBYTES" ]; then
    VERD=1; echo "FAIL: frame count dropped ($INBYTES -> $OUTBYTES bytes)"
elif grep -q 'Metal unavailable/failed' "$STDERRF"; then
    echo "WARN: Metal unavailable in this environment; dual degraded to CPU ($MODE, ${W}x${H}, $NFRAMES frame(s))"
else
    echo "PASS: dual byte-identical ($MODE, ${W}x${H}, $NFRAMES frame(s), $FLAGS)"
fi
if [ -s "$STDERRF" ]; then
    cat "$STDERRF"
fi
exit "$VERD"
