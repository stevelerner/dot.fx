#!/bin/sh
# model-gamma-dotportal-invert.sh — dot.portal (the approved T look) on an
# INVERTED model.mp4, via dotpipe: ffmpeg decodes + negates, dotpipe
# applies the effect on raw frames, ffmpeg encodes.
#
# Order matters — the video is inverted BEFORE the process; the effect's
# output is never flipped (this is NOT the black-dots-on-white "neg"
# variant, that was the 4-sec probe portal-neg-4s.mp4). The effect maps
# the negative's tones, which changes dot placement/texture.
#
# PIPELINE (left to right):
#
# STAGE 1 — decode + eq=gamma=2.0 + negate → raw RGB24 (ffmpeg, codec
#   only). Order matters: the shadow lift runs on the ORIGINAL tones
#   (it brightens the dark plate so the gate and halftone radius have
#   signal), THEN the video is inverted.
#
# STAGE 2 — dotpipe --dotportal (the approved T look, Metal backend)
#   10 positional parameters, in the order in the pipeline below:
#   --dotportal 18 \           dot pitch px (bold dense body of dots)
#     70 \                     dot radius as % of step (= 6.3 px: bold, bright dots)
#     5 \                      subject-gate knee x100 (approved: 15+ starts dropping the body, 30 = black frame)
#     100 \                    radius tracks tone (halftone max — dot size maps to the appearance)
#     100 \                    gradient displacement as % of step (relief max — dots bend along the surface, breaks the grid)
#     800 \                    brightness x100 (8x — the approved gain)
#     100 \                    aura 0-100 (max halo — the main brightness lever)
#     360 \                    shadow hue (360 = white)
#     360 \                    highlight hue (360 = white — pure-white duotone; cyan 180 read too dim)
#     100 \                    crisp silhouette sharpening (max — dots hug the shape)
#
# STAGE 3 — encode (libx264 crf 20, yuv444p — the fine dots get mangled
#   by 4:2:0 chroma, so this render uses 4:4:4).
#
# Backend: METAL — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error (check below), so the output can
# never silently be the CPU pass.
#
# Usage:  sh run-scripts-dotpipe/model-gamma-dotportal-invert-metal.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-dotportal-invert.mp4)
set -eu

cd "$(dirname "$0")/.."

# -- defaults (override with -i / -o) ------------------------------------
IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-dotportal-invert.mp4

# -- arg parse ------------------------------------------------------------
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-gamma-dotportal-invert-metal.sh [-i input] [-o output]" >&2
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
# STAGE 1 — ffmpeg decode → gamma shadow lift (eq=gamma=2.0) → negate →
#   raw RGB24. -fps_mode passthrough: no CFR drop/dup at decode, so the
#   frame count matches the frei0r (filtergraph) pipeline exactly for
#   quirky sources.
# STAGE 2 — dotpipe --dotportal on the Metal backend (one parameter per
#   line; meanings in the header above).
# STAGE 3 — ffmpeg encode: libx264 crf 20, yuv444p (fine-dot safe).
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -vf "eq=gamma=2.0,negate" -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --dotportal 18 \
      70 \
      5 \
      100 \
      100 \
      800 \
      100 \
      360 \
      360 \
      100 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

# -- hard error on Metal fallback (see header: Backend) --------------------
if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi
rm -f "$ERRLOG"
