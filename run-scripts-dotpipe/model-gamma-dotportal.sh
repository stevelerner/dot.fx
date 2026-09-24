#!/bin/sh
# model-gamma-dotportal.sh — dot.portal (white-cyan duotone, fine dots) on model.mp4
# via dotpipe (the raw-pipe effect host): ffmpeg decodes, dotpipe applies
# the effect on raw frames, ffmpeg encodes. The approved fine-dot look
# (reviewed 2026-09-23): small separated dots, white->cyan duotone,
# subtle aura — recipe below.
#
# PIPELINE (left to right):
#
# STAGE 1 — decode + eq=gamma=1.5 → raw RGB24 (ffmpeg, codec only;
#   the shadow lift runs BEFORE the effect, same position as in
#   model-gamma-spacengrave.sh. The lift brightens the dark plate so
#   the subject gate and the halftone radius have signal to work on.)
#
# STAGE 2 — dotpipe --dotportal (the effect, Metal backend)
#   9 positional parameters, in the order in the pipeline below:
#   --dotportal 8 \            dot pitch px (step = 4 px — the fine grid)
#     50 \                     dot radius as % of step (= 2.0 px: right at the separation limit — brighter, dots still distinct)
#     5 \                      subject-gate knee x100 (approved on this footage)
#     70 \                     radius tracks tone (halftone)
#     50 \                     gradient displacement as % of step (relief)
#     200 \                    brightness x100 (2x = the approved gain)
#     50 \                     aura 0-100 (bright halo — the main brightness lever; 360 here would fuse the dots)
#     360 \                    shadow hue (360 = white)
#     180 \                    highlight hue (180 = cyan) — white->cyan shade-blended duotone
#
# STAGE 3 — encode (libx264 crf 20, yuv444p — the fine dots get mangled
#   by 4:2:0 chroma, so this render uses 4:4:4).
#
# Backend: METAL — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error (check below), so the output can
# never silently be the CPU pass.
#
# Usage:  sh run-scripts-dotpipe/model-gamma-dotportal.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-dotportal-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

# -- defaults (override with -i / -o) ------------------------------------
IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-dotportal-dotpipe.mp4

# -- arg parse ------------------------------------------------------------
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-gamma-dotportal.sh [-i input] [-o output]" >&2
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
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height  -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# -- the render pipeline (one logical command, three stages) --------------
# STAGE 1 — ffmpeg decode → gamma shadow lift (eq=gamma=1.5) → raw RGB24.
#   -fps_mode passthrough: no CFR drop/dup at decode, so the frame count
#   matches the frei0r (filtergraph) pipeline exactly for quirky sources.
# STAGE 2 — dotpipe --dotportal on the Metal backend (one parameter per
#   line; meanings in the header above).
# STAGE 3 — ffmpeg encode: libx264 crf 20, yuv444p (fine-dot safe).
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -vf "eq=gamma=1.5" -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --dotportal 8 \
      50 \
      5 \
      70 \
      50 \
      200 \
      50 \
      360 \
      180 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

# -- hard error on Metal fallback (see header: Backend) --------------------
if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi
rm -f "$ERRLOG"
