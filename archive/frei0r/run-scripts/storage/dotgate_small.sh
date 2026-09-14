#!/bin/sh
# dotgate_small.sh — the dot.gate dot map sized for small (720p-class)
# inputs: the approved 4K recipe (28|8) reads zoomed/chunky at 720×1280,
# so the dot pitch is scaled by 720/3840 (28→6) and the wobble is turned
# off (any drift at this scale reads as distortion on the thin subject).
# No scale step — the output keeps the input's own geometry (portrait in
# = portrait out).
#
# Params (pipe order: size|speed|gate) — this recipe is 6|0|1:
#
#   size = 6    Distance between dot centers, in source px (a dot every
#               3 px = size/2 — a fine, dense map). Bigger = chunkier,
#               fewer dots; smaller = finer. Pick per resolution: 28 at
#               4K, ~6 at 720p.
#   speed = 0   How far each dot wanders from its grid position, in px.
#               0 = perfectly still (off here on purpose — see above).
#   gate = 1    Subject coverage, as knee ×100 (1 = 0.01). A dot is
#               drawn only where local contrast reaches that knee AND the
#               cell is inside the detected subject region; everything
#               else is black. 1 ≈ the whole subject filled in; 10 ≈ a
#               neon edge outline. Dark, smooth 720p material needs the
#               low knee — its interior contrast never reaches 0.10.
#
# Usage:  sh run-scripts/dotgate_small.sh [-i input] [-o output] [-p size|speed|gate]
#         (defaults: inputvideos/test-short.mov → outputvideos/dotgate_small.mp4, 6|0|1)
#         (-p values contain | — quote them: -p "6|0|1")
#         Example: sh run-scripts/dotgate_small.sh -i inputvideos/model.mp4
#                  -o outputvideos/my_dots.mp4 -p "6|0|1"
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/dotgate_small.mp4
PARAMS="6|0|1"
while getopts "i:o:p:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    p) PARAMS=$OPTARG ;;
    *) echo "usage: sh run-scripts/dotgate_small.sh [-i input] [-o output] [-p size|speed|gate]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_dotgate:$PARAMS" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"
