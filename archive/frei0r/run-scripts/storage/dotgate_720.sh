#!/bin/sh
# dotgate_720.sh — the approved dot.gate dot-map look, output at 720p.
# ORDER MATTERS: the effect runs on the 4K source (the 28 px dots stay
# fine and crisp), and only THEN is the result scaled down to 1280×720.
# Scaling first makes the dots 3× chunkier on a softened image —
# blurred/hazy.
#
# Params (pipe order: size|speed|gate) — this recipe is 28|8|10:
#
#   size = 28   Distance between dot centers, in source (4K) px (a dot
#               every 14 px = size/2). Bigger = chunkier, fewer dots;
#               smaller = finer, denser. (At native 720p input use 6 —
#               see dotgate_small.sh.)
#   speed = 8   How far each dot wanders from its grid position, in px —
#               a slow wobble. 0 = perfectly still.
#   gate = 10   Subject coverage, as knee ×100 (10 = 0.10). A dot is
#               drawn only where local contrast (edges/detail) reaches
#               that knee AND the cell is inside the detected subject
#               region; everything else is black. Lower = more coverage
#               (1 ≈ the whole subject filled in); higher = fewer dots
#               (10 ≈ a neon edge outline).
#
# Usage:  sh run-scripts/dotgate_720.sh [-i input] [-o output]
#         (defaults: inputvideos/test-short.mov → outputvideos/dotgate_720.mp4)
#         Tweak: edit the 28|8|10 on the frei0r line at the bottom.
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/dotgate_720.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts/dotgate_720.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_dotgate:28|8|10,scale=1280:720" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"
