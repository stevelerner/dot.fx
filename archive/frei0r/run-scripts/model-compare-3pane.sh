#!/bin/sh
# model-compare-3pane.sh — three-pane comparison of inputvideos/model.mp4,
# one source fanned into three branches, hstacked left to right:
#
#   LEFT   = ORIGINAL — the untouched source plate.
#   MIDDLE = VINTAGE — archive/frei0r/run-scripts/storage/model-vintage.sh recipe
#            (the nine vid.* CRT/VHS effects, NO dot.gate layer, NO
#            glitch — tuned 2026-09-03: bloom 60, scanlines 65,
#            shadowmask 4, barrel 35), applied to model.mp4 at native
#            720×1280 so it matches the other two panes.
#            Params → meaning:
#              bloom       60|0.10|16   Soft glow on highlights (strength,
#                                       threshold 0..1, radius px).
#              chromablood 25           RGB fringing on edges (0-100).
#              lumar       25|4         Luma ringing around edges (amount,
#                                       wavelength px).
#              rainbow     40|48|16     Dot-crawl rainbow phasing (amount,
#                                       rows per beat, frames per beat).
#              tapewow     8|90         Slow vertical drift (amplitude px,
#                                       frames per cycle).
#              scanlines   65|3|0       CRT dark rows (intensity — raised
#                                       from 35, period rows, offset).
#              shadowmask  0|4|3        Aperture-grille bars (type,
#                                       intensity — lowered from 15, pitch).
#              vignette    35           Corner darkening (0-100).
#              barrel      35|100       CRT glass curve (amount — raised
#                                       from 25, zoom).
#              overscan    24|4         Rounded corners + bezel crop
#                                       (radius, crop px).
#              (glitch removed on 2026-09-03.)
#   RIGHT  = GAMMA SPAcENGRAVE — the approved "just gamma" recipe:
#            eq=gamma=1.5 + dot.spacengrave 5|85|80|68|200|78|360|100|5
#            (archive/frei0r/run-scripts/model-gamma-spacengrave-metal.sh recipe).
#
#   out = outputvideos/model_3pane.mp4   (default 3×720 × 1280 = 2160×1280)
#
# Each branch is fitted into an identical WxH cell (aspect preserved,
# padded, no stretch) so all three panes always match — even if you
# swap in different sources or tweak a branch to a different size.
#
# Usage:  sh archive/frei0r/run-scripts/model-compare-3pane.sh [-i input] [-o output]
#         [-w 720] [-h 1280]
#
# ── MIDDLE branch: the vintage (model-vintage.sh recipe) ────────────────
#   (param docs above; chain order below)
# ── RIGHT branch: gamma + dot.spacengrave ─────────────────────────────
#   eq=gamma=1.5                Darks-only shadow lift (the approved
#                               "original gamma"): lifts the dark plate
#                               proportionally, leaves skin ~untouched.
#   frei0r spacengrave params (size|fill|halftone|line|level|grain|
#   color|dim|gate):
#     5    Scanline pitch px (native res; 0 = off).
#     85   Stroke width % of pitch (the bold reference line).
#     80   Tone-coupled ink shading (the engraved 3D read).
#     68   Baseline line solidity (0-100).
#     200  Ink brightness ×100 (2 = 2×, the approved gain).
#     78   Texture break-up into dashes (the engraving knob).
#     360  Ink colour (360 = pure white).
#     100  Photo dimmed to pure black under the ink.
#     5    Subject gate knee (approved on this footage).
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/model.mp4
OUT=outputvideos/model_3pane.mp4
W=720
H=1280
while getopts "i:o:w:h:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    w) W=$OPTARG ;;
    h) H=$OPTARG ;;
    *) echo "usage: sh archive/frei0r/run-scripts/model-compare-3pane.sh [-i input] [-o output] [-w 720] [-h 1280]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

# One branch -> an identical WxH cell: aspect preserved, padded, square
# pixels — so the three panes always match regardless of the branch.
cell() {
  echo "scale=${W}:${H}:force_original_aspect_ratio=decrease,pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2,setsar=1"
}

# The vintage chain (model-vintage.sh recipe, no dot layer, no glitch).
VINTAGE="frei0r=libretrofx_vid_bloom:60|0.10|16,frei0r=libretrofx_vid_chromablood:25,frei0r=libretrofx_vid_lumar:25|4,frei0r=libretrofx_vid_rainbow:40|48|16,frei0r=libretrofx_vid_tapewow:8|90,frei0r=libretrofx_vid_scanlines:65|3|0,frei0r=libretrofx_vid_shadowmask:0|4|3,frei0r=libretrofx_vid_vignette:35,frei0r=libretrofx_vid_barrel:35|100,frei0r=libretrofx_vid_overscan:24|4"

# LEFT   = original | MIDDLE = vintage (model-vintage recipe) | RIGHT = gamma + spacengrave
FREI0R_PATH=$PWD/archive/frei0r/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -filter_complex "
    [0:v]split=3[src0][src1][src2];
    [src0]$(cell)[p0];
    [src1]${VINTAGE},$(cell)[p1];
    [src2]eq=gamma=1.5,frei0r=libretrofx_dot_spacengrave:5|85|80|68|200|78|360|100|5,$(cell)[p2];
    [p0][p1][p2]hstack=inputs=3" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
  "$OUT"

# yuv444p: the spacengrave pane needs it (4:2:0 mangles the thin
# strokes); it's harmless to the other two. CRF 20 = the house size.
