#!/bin/sh
# dotgate_small_720.sh — the dot.gate dot map for small (720p-class)
# inputs, output forced to 1280×720: dot pitch scaled from the 4K recipe
# (28→6, × 720/3840), wobble off (drift reads as distortion at this
# scale). NOTE: the final scale to 1280×720 STRETCHES a portrait input —
# for true geometry use dotgate_small.sh (no scale step).
#
# Params (pipe order: size|speed|gate) — this recipe is 6|0|1:
#
#   size = 6    Distance between dot centers, in source px (a dot every
#               3 px = size/2 — a fine, dense map). Bigger = chunkier,
#               fewer dots; smaller = finer.
#   speed = 0   How far each dot wanders from its grid position, in px.
#               0 = perfectly still (off here on purpose).
#   gate = 1    Subject coverage, as knee ×100 (1 = 0.01). A dot is
#               drawn only where local contrast reaches that knee AND the
#               cell is inside the detected subject region; everything
#               else is black. 1 ≈ the whole subject filled in; 10 ≈ a
#               neon edge outline. Dark, smooth 720p material needs the
#               low knee — its interior contrast never reaches 0.10.
#
# Usage:  sh run-scripts/dotgate_small_720.sh [-i input] [-o output] [-p size|speed|gate]
#         (defaults: inputvideos/test-short.mov → outputvideos/dotgate_small_720.mp4, 6|0|1)
#         (-p values contain | — quote them: -p "6|0|1")
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/dotgate_small_720.mp4
PARAMS="6|0|1"
while getopts "i:o:p:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    p) PARAMS=$OPTARG ;;
    *) echo "usage: sh run-scripts/dotgate_small_720.sh [-i input] [-o output] [-p size|speed|gate]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_dotgate:$PARAMS,scale=1280:720" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"
