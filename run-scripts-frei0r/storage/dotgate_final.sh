#!/bin/sh
# dotgate_final.sh — the approved "pure dot map" look: the subject of the
# frame becomes a field of dots on black, everything else black. Nothing
# else is applied. Render: 4K in, 4K out (outputvideos/dotgate_final.mp4).
#
# Why no pre-scaling: the dot grid is computed at the SOURCE resolution.
# Scaling the video down first makes the dots 3× chunkier on a softened
# image — blurred/hazy. (720p output = dotgate_720.sh, which scales
# AFTER the effect.)
#
# Params (pipe order: size|speed|gate) — this recipe is 28|8|10:
#
#   size = 28   Distance between dot centers, in source px (a dot every
#               14 px = size/2, so ~275 × 155 dots across a 4K frame).
#               Bigger = chunkier, fewer dots; smaller = finer, denser.
#               Resolution-dependent: 28 is right for 4K; at 720p it is
#               3× too coarse (use 6 — see dotgate_small.sh).
#   speed = 8   How far each dot wanders from its grid position, in px —
#               a slow wobble. 0 = perfectly still. At 720p class, drift
#               reads as distortion, so keep it small.
#   gate = 10   Subject coverage, as knee ×100 (10 = 0.10). A dot is
#               drawn only where local contrast (edges/detail) reaches
#               that knee AND the cell is inside the detected subject
#               region; everything else is black. Lower = more coverage:
#               1 ≈ the whole subject filled in (what dark, smooth
#               material needs); higher = fewer dots (10 ≈ a neon edge
#               outline).
#
# Usage:  sh run-scripts/dotgate_final.sh [-i input] [-o output]
#         (defaults: inputvideos/test-short.mov → outputvideos/dotgate_final.mp4)
#         Tweak: edit the 28|8|10 on the frei0r line at the bottom.
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/dotgate_final.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts/dotgate_final.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_dotgate:28|8|10" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"
