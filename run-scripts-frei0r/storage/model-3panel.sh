#!/bin/sh
# model-3panel.sh — side-by-side comparison: videos stacked horizontally
# (hstack), each fitted into an identical cell so nothing gets stretched
# (the 720x1280 lesson). Three or four panels.
#
# Default lineup (left to right, the full family compare):
#   1  = inputvideos/model.mp4              (original)
#   2  = outputvideos/model-spacengrave.mp4       (dot.spacengrave)
#   3  = outputvideos/model-vintage.mp4           (vintage stack)
#   4  = outputvideos/model-dotportal-white.mp4   (dot.portal white)
#   out = outputvideos/model_3panel.mp4
#
# For the old three-panel form (original | vintage | dot.portal), pass
# -d "" to drop the 4th slot. Any inputs work: -a/-b/-c/-d.
#
# Usage:  sh run-scripts/model-3panel.sh [-a p1] [-b p2] [-c p3] [-d p4]
#                             [-o output] [-w 720] [-h 1280]
#
# Each input is scaled with aspect preserved, then centered on a WxH
# black cell — so portrait, landscape or any resolution all fit.
# Final output is W*3 x H (default 2160 x 1280) or W*4 x H (2880 x 1280).
set -eu

cd "$(dirname "$0")/../.."

A=inputvideos/model.mp4
B=outputvideos/model-spacengrave.mp4
C=outputvideos/model-vintage.mp4
D=outputvideos/model-dotportal-white.mp4   # 4th panel; -d "" drops it (3 panels)
OUT=outputvideos/model_3panel.mp4
W=720
H=1280
while getopts "a:b:c:d:o:w:h:" opt; do
  case $opt in
    a) A=$OPTARG ;;
    b) B=$OPTARG ;;
    c) C=$OPTARG ;;
    d) D=$OPTARG ;;
    o) OUT=$OPTARG ;;
    w) W=$OPTARG ;;
    h) H=$OPTARG ;;
    *) echo "usage: sh run-scripts/model-3panel.sh [-a p1] [-b p2] [-c p3] [-d p4] [-o out] [-w 720] [-h 1280]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

# One input -> a WxH cell: scale keeping aspect, pad the leftover.
cell() {
  echo "scale=${W}:${H}:force_original_aspect_ratio=decrease,pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2,setsar=1"
}

if [ -n "$D" ]; then
  ffmpeg -hide_banner -loglevel error -y \
    -i "$A" -i "$B" -i "$C" -i "$D" \
    -filter_complex "[0:v]$(cell)[p0];[1:v]$(cell)[p1];[2:v]$(cell)[p2];[3:v]$(cell)[p3];[p0][p1][p2][p3]hstack=inputs=4" \
    -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
    "$OUT"
else
  ffmpeg -hide_banner -loglevel error -y \
    -i "$A" -i "$B" -i "$C" \
    -filter_complex "[0:v]$(cell)[p0];[1:v]$(cell)[p1];[2:v]$(cell)[p2];[p0][p1][p2]hstack=inputs=3" \
    -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
    "$OUT"
fi
