#!/bin/sh
# model-dotportal-white.sh — the approved dot.portal WHITE look (the
# subject turned into separated, floating white dots on black) run on
# inputvideos/model.mp4. Renders at NATIVE portrait 720x1280 (no scale —
# the subject is not stretched). Same dot.portal recipe as dotportal.sh,
# minus the downscale (the source is already 720-wide).
#
# Params (pipe order: size|fill|gate|halftone|relief|level|glow|color) —
# this recipe is 24|55|5|70|50|200|60|360 (also the plugin default):
#
#   size = 24   Distance between dot centers, in source px (a dot every
#               12 px = size/2). Bigger = chunkier, fewer dots.
#   fill = 55   Dot radius as a % of the spacing: 55 leaves visible gaps
#               between dots (the "separated" look); 80 = gap-free
#               coverage (the dot.gate look). Bigger = fatter dots.
#   gate = 5    Subject knee ×100 (5 = 0.05): a dot is drawn only where
#               local contrast reaches that knee AND its center is inside
#               the detected subject region — this cuts the field exactly
#               at the silhouette (flat background = black).
#   halftone = 70  How strongly dot size follows the local tone: lit
#               surfaces get fatter dots, shadow smaller — the shading
#               that reads as 3D. 0 = uniform dot size.
#   relief = 50  How far each dot is displaced along the local light
#               gradient, as % of the spacing (50 = half a cell) — the
#               "relief" that sells the shape. 0 = flat field.
#   level = 200  Brightness gain ×100 (100 = 1×; 200 = 2×, the max here):
#               dots hit pure 255; fill + glow carry the perceived
#               brightness.
#   glow = 60   Soft aura drawn around each dot: 0 = off, 60 = default
#               (hologram-ish), 100 = widest bloom.
#   color = 360  Dot colour: 360 = pure white (sentinel); 0..359 = the
#               hue wheel at 60% saturation (0 = red, 120 = green,
#               240 = blue).
#
# Usage:  sh run-scripts/model-dotportal-white.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-dotportal-white.mp4)
#         Tweak: edit the pipe list on the frei0r line at the bottom.
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/model.mp4
OUT=outputvideos/model-dotportal-white.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts/model-dotportal-white.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_dotportal:24|55|5|70|50|200|60|360" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
  "$OUT"

# yuv444p is required here: yuv420p 4:2:0 desaturates the small dots to
# near-gray. CRF is the size knob: 18 = near-lossless, 20 = default,
# 23 = smaller, still clean for this material.
