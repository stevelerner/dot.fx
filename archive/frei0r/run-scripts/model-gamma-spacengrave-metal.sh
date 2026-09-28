#!/bin/sh
# model-gamma-spacengrave-metal.sh — dot.spacengrave on model.mp4,
# JUST THE GAMMA LIFT, no 2× supersample: the A/B "without L9" half of
# the pair. Companion to model-spacengrave-metal.sh (WITH the supersample).
#
# PIPELINE (left to right) — each stage documented right before it:
#
# STAGE 1 — eq=gamma=1.5   (shadow lift — the "original gamma")
#   Purpose: the footage is dark (mean luma ~36/255) and too dark for
#   the Otsu subject gate (gate = 5) and tone shading to find the
#   subject. Gamma > 1 lifts the darks proportionally (the darker, the
#   more) and leaves the highlights (skin) nearly untouched — unlike a
#   flat brightness offset.
#   Parameters (full form eq=brightness:B:contrast:C:gamma=G; we set gamma only):
#     gamma = 1.5     1 = no lift; higher = a stronger darks-only lift.
#                     1.5 was approved on this footage.
#
# STAGE 2 — frei0r=libretrofx_dot_spacengrave   (the effect)
#   9 positional parameters in pipe order:
#     size | fill | halftone | line | level | grain | color | dim | gate
#   This recipe: 5|85|80|68|200|78|360|100|5 — the ORIGINAL approved
#   params (grain 78) at NATIVE resolution (size 5 per 720-wide).
#
#     size = 5      Scanline pitch in source px (the vertical period);
#                 0 = off. 5 = the approved reference look at 720-wide.
#     fill = 85     Stroke width as % of pitch (85 = the bold reference
#                 line, 3px ink / 2px gap at pitch 5).
#     halftone = 80  Ink follows local tone (engraved shading).
#     line = 68     Baseline coverage — line solidity everywhere.
#     level = 200   Ink brightness ×100 (200 = 2×, the approved gain).
#     grain = 78    Texture break-up into dashes (78 = raised from 60
#                 for definition; the knob that engraves the face).
#     color = 360   Ink colour (360 = pure white).
#     dim = 100     Photo removed to pure black under the ink.
#     gate = 5      Subject gate knee (approved on this footage).
#
# (Full param docs: run-scripts-dotpipe/model-gamma-spacengrave-metal.sh.)
#
# Backend: METAL — and GENUINELY GPU: native 720×1280 is under the
# 2048-row Metal bound (unlike the supersampled pair, which falls back
# to CPU by design; output parity holds either way).
#
# Usage:  sh archive/frei0r/run-scripts/model-gamma-spacengrave-metal.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-spacengrave-metal.mp4)
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-spacengrave-metal.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh archive/frei0r/run-scripts/model-gamma-spacengrave-metal.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/archive/frei0r/build RETROFX_BACKEND=metal \
  ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "eq=gamma=1.5,frei0r=libretrofx_dot_spacengrave:5|85|80|68|200|78|360|100|5" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p \
  "$OUT"

# A CPU-fallback warning on stderr ("retrofx: Metal unavailable/failed")
# would mean the frame silently went to the CPU core — for THIS recipe
# at native resolution a clean run prints nothing.
