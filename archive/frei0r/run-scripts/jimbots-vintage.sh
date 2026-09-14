#!/bin/sh
# jimbots-vintage.sh — full vintage stack (no dot.gate), NO glitch.
# The nine vid.* CRT/VHS effects at their locked levels, on a portrait
# subject at NATIVE geometry (no scale — the subject is not stretched).
# Tuned 2026-09-03: glitch removed, shadowmask 15→4, bloom 40→60,
# barrel 25→35, scanlines 35→65.
#
# Usage:  sh archive/frei0r/run-scripts/jimbots-vintage.sh [-i input] [-o output]
#         (defaults: inputvideos/jimbots.mp4 → outputvideos/jimbots-vintage.mp4)
#
# Effect params (pipe values → meaning, in chain order; each effect gets
# a plain-English description first):
#   bloom        60|0.10|16   Soft glow around the bright areas.
#                             60 = strength (0-100, raised from 40) ·
#                             0.10 = how bright a pixel must be before it
#                             glows (0..1) · 16 = glow radius, px
#   chromablood  25           RGB fringing on edges (red/blue ghosting).
#                             25 = amount (0-100)
#   lumar        25|4         Luma ringing — oscillating light/dark bands
#                             around high-contrast edges.
#                             25 = amount (0-100) · 4 = wavelength
#   rainbow      40|48|16     Dot-crawl rainbow phasing (crawling coloured
#                             fringes, old-SMPTE style).
#                             40 = amount (0-100) · 48 = rows per beat
#                             cycle · 16 = frames per beat cycle
#   tapewow      8|90         Slow vertical whole-picture drift (tape
#                             speed wobble).
#                             8 = drift amplitude, px · 90 = frames per
#                             drift cycle
#   scanlines    65|3|0       CRT scanlines (periodic dark rows).
#                             65 = intensity (0-100) · 3 = period, rows ·
#                             0 = offset, rows
#   shadowmask   0|4|3        Aperture-grille shadow bars (the vertical
#                             line structure over dark areas).
#                             0 = type (0 = grille) · 4 = intensity (0-100)
#                             · 3 = pitch
#   vignette     35           Corner darkening. 35 = amount (0-100)
#   barrel       35|100       Barrel distortion (the CRT glass curve).
#                             35 = amount (0-100; 50 = 0.50, 100 = 0.75)
#                             · 100 = zoom (0 = off, 100 = full): zooms
#                             the picture in so the curved edge margin
#                             shows content, not black
#   overscan     24|4         CRT face: rounded corners + bezel crop.
#                             24 = corner radius, px · 4 = bezel crop, px
#   (glitch removed 2026-09-03 — it was the faint 6|8|5|8|0|5|5|6|3
#    recipe; the user asked for none at all in the compare lineup.)
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/jimbots.mp4
OUT=outputvideos/jimbots-vintage.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh archive/frei0r/run-scripts/jimbots-vintage.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/archive/frei0r/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "frei0r=libretrofx_vid_bloom:60|0.10|16,
       frei0r=libretrofx_vid_chromablood:25,
       frei0r=libretrofx_vid_lumar:25|4,
       frei0r=libretrofx_vid_rainbow:40|48|16,
       frei0r=libretrofx_vid_tapewow:8|90,
       frei0r=libretrofx_vid_scanlines:65|3|0,
       frei0r=libretrofx_vid_shadowmask:0|4|3,
       frei0r=libretrofx_vid_vignette:35,
       frei0r=libretrofx_vid_barrel:35|100,
       frei0r=libretrofx_vid_overscan:24|4" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"

# Geometry note: no `scale=` — the subject keeps its native portrait
# resolution (e.g. 720x1280 for inputvideos/jimbots.mp4) rather than being
# stretched to a landscape frame.
