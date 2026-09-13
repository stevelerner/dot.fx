#!/bin/sh
# dotportal_coarse_bright.sh — the approved COARSE dot.portal look
# (halftone 90, relief 75) on a BRIGHTENED input: an eq lift runs BEFORE
# the effect (night material is too dark for the Otsu subject gate and
# tone sizing to find the subject). Same scale order as
# dotportal_coarse.sh (effect first, then scale).
#
# The lift (override with -l):
#   brightness=0.30:contrast=1.2   ffmpeg eq preset applied to the input
#   before the effect: +0.30 on the 0..1 brightness scale (≈ +76/255)
#   and 1.2× contrast.
#
# Params (pipe order: size|fill|gate|halftone|relief|level|glow|color) —
# this recipe is 48|55|5|90|75|200|60|360:
#
#   size = 48   Distance between dot centers, in source (4K) px (a dot
#               every 24 px = size/2 — twice as coarse as the base look).
#   fill = 55   Dot radius as a % of the spacing: 55 leaves visible gaps
#               between dots; 80 = gap-free coverage. Bigger = fatter.
#   gate = 5    Subject knee ×100 (5 = 0.05): dots only where local
#               contrast reaches that knee AND the center is inside the
#               detected subject region (silhouette cut, black else).
#   halftone = 90  Dot size follows the local tone strongly: lit surfaces
#               get much fatter dots, shadow smaller — the strong 3D.
#               0 = uniform dot size.
#   relief = 75  Dots displaced 75% of a cell along the local light
#               gradient — strong shape relief. 0 = flat field.
#   level = 200  Brightness gain ×100 (100 = 1×; 200 = max): dots hit
#               pure 255 at 4K; fill + glow carry perceived brightness.
#   glow = 60   Soft aura around each dot: 0 = off, 60 = default, 100 =
#               widest bloom.
#   color = 360  Dot colour: 360 = pure white; 0..359 = hue wheel at 60%
#               saturation (0 = red, 120 = green, 240 = blue).
#
# Usage:  sh run-scripts/dotportal_coarse_bright.sh [-i input] [-o output] [-p params] [-l lift]
#         (-p values contain | — quote them: -p "48|55|5|90|75|200|60|360")
#         (-l example: -l "brightness=0.40:contrast=1.3")
#         (defaults: inputvideos/test-short.mov → outputvideos/dotportal_coarse_bright.mp4)
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/dotportal_coarse_bright.mp4
PARAMS="48|55|5|90|75|200|60|360"
LIFT=brightness=0.30:contrast=1.2
while getopts "i:o:p:l:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    p) PARAMS=$OPTARG ;;
    l) LIFT=$OPTARG ;;
    *) echo "usage: sh run-scripts/dotportal_coarse_bright.sh [-i input] [-o output] [-p params] [-l lift]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "eq=$LIFT,frei0r=libretrofx_dotportal:$PARAMS,scale=1280:720" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
  "$OUT"

# yuv444p is required here: yuv420p 4:2:0 desaturates the small dots to
# near-gray. CRF is the size knob: 18 = near-lossless, 20 = default,
# 23 = smaller, still clean for this material.
