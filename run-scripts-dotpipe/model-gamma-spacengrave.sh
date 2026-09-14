#!/bin/sh
# model-gamma-spacengrave.sh — dot.spacengrave on model.mp4 via
# dotpipe (the raw-pipe effect host): ffmpeg decodes, dotpipe applies
# the effect on raw frames, ffmpeg encodes. Same approved look as
# model-gamma-spacengrave-metal.sh (gamma lift + native recipe) — no
# frei0r, no plugin path; the effect engine is the dotpipe binary.
#
# PIPELINE (left to right):
#
# STAGE 1 — decode + eq=gamma=1.5 → raw RGB24 (ffmpeg, codec only;
#   the shadow lift runs BEFORE the effect, same position as in the
#   frei0r recipe: eq=gamma=1.5,frei0r=...)
#
# STAGE 2 — dotpipe --spacengrave   (the effect, Metal backend)
#   9 positional parameters in order:
#     size | fill | halftone | line | level | grain | color | dim | gate
#   This recipe: 5 85 80 68 200 78 360 100 5 — the ORIGINAL approved
#   params (same values, space-separated here instead of |).
#   (Per-parameter docs: run-scripts-frei0r/model-gamma-spacengrave-metal.sh.)
#
# STAGE 3 — encode (libx264 crf 20, yuv444p — same as the frei0r recipe)
#
# Backend: METAL — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error (check below), so the output can
# never silently be the CPU pass.
#
# Usage:  sh run-scripts-dotpipe/model-gamma-spacengrave.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-spacengrave-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-spacengrave-dotpipe.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-gamma-spacengrave.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

ERRLOG=/tmp/dotpipe-err.$$; : > "$ERRLOG"

W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# decode (gamma lift folded in) -> dotpipe (Metal) -> encode
# -fps_mode passthrough: no CFR drop/dup at decode, so the frame count
# matches the frei0r (filtergraph) pipeline exactly for quirky sources.
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -vf "eq=gamma=1.5" -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --spacengrave 5 85 80 68 200 78 360 100 5 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi
rm -f "$ERRLOG"
