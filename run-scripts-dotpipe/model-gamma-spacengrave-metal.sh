#!/bin/sh
# model-gamma-spacengrave-metal.sh — dot.spacengrave on model.mp4 via
# dotpipe (the raw-pipe effect host): ffmpeg decodes, dotpipe applies
# the effect on raw frames, ffmpeg encodes. Same approved look as
# archive/frei0r/run-scripts/model-gamma-spacengrave-metal.sh (gamma lift +
# native recipe) — no frei0r, no plugin path; the effect engine is the
# dotpipe binary.
#
# PIPELINE (left to right):
#
# STAGE 1 — decode + eq=gamma=1.5 → raw RGB24 (ffmpeg, codec only;
#   the shadow lift runs BEFORE the effect, same position as in the
#   frei0r recipe: eq=gamma=1.5,frei0r=...). The lift brightens the
#   dark plate so the engraving gate has signal to work on.
#
# STAGE 2 — dotpipe --spacengrave   (the effect, Metal backend)
#   9 positional parameters, in the order in the pipeline below:
#   --spacengrave 5 \            scanline pitch px (native res; 0 = off)
#     85 \                       stroke width, % of pitch (the bold reference line)
#     80 \                       tone-coupled ink shading (the engraved 3D read)
#     68 \                       baseline line solidity (0–100)
#     200 \                      ink brightness ×100 (2× = the approved gain)
#     78 \                       texture break-up into dashes (the engraving knob)
#     360 \                      ink colour (360 = pure white)
#     100 \                      photo dimmed to pure black under the ink
#     5 \                        subject gate knee (approved on this footage)
#
# STAGE 3 — encode (libx264 crf 20, yuv444p — the thin strokes get
#   mangled by 4:2:0 chroma, so this render uses 4:4:4).
#
# Backend: METAL — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error (check below), so the output can
# never silently be the CPU pass.
#
# Usage:  sh run-scripts-dotpipe/model-gamma-spacengrave-metal.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-spacengrave-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

# -- defaults (override with -i / -o) ------------------------------------
IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-spacengrave-dotpipe.mp4

# -- arg parse ------------------------------------------------------------
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-gamma-spacengrave-metal.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

# -- capture dotpipe's stderr separately: a Metal fallback warning must
#    fail the render, not just print (checked after the pipeline) ---------
ERRLOG=/tmp/dotpipe-err.$$; : > "$ERRLOG"

# -- source geometry (probed; the subject keeps its native resolution —
#    no scale anywhere in the pipeline) ------------------------------------
W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# -- the render pipeline (one logical command, three stages) --------------
# STAGE 1 — ffmpeg decode → gamma shadow lift (eq=gamma=1.5) → raw RGB24.
#   -fps_mode passthrough: no CFR drop/dup at decode, so the frame count
#   matches the frei0r (filtergraph) pipeline exactly for quirky sources.
# STAGE 2 — dotpipe --spacengrave on the Metal backend (one parameter per
#   line; meanings in the header above).
# STAGE 3 — ffmpeg encode: libx264 crf 20, yuv444p (thin-stroke safe).
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -vf "eq=gamma=1.5" -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --spacengrave 5 \
      85 \
      80 \
      68 \
      200 \
      78 \
      360 \
      100 \
      5 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

# -- hard error on Metal fallback (see header: Backend) --------------------
if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi
rm -f "$ERRLOG"
