#!/bin/sh
# vintage_only.sh — the vintage stack alone: all ten vid.* effects at the
# approved levels, WITHOUT the faint dot.gate overlay (that composite is
# vintagestack.sh). Single linear chain — no split/blend.
# Intensity knobs are turned UP from that baseline (×~1.4; structural
# periods/sizes unchanged) for a stronger vintage read.
#
# Usage:  sh run-scripts/vintage_only.sh [-i input] [-o output]
#         (defaults: inputvideos/test-short.mov → outputvideos/vintage_only.mp4)
#
# Effect params (pipe values → meaning, in chain order; each effect gets
# a plain-English description first):
#   bloom        55|0.10|16    Soft glow around the bright areas.
#                              55 = strength (0-100) · 0.10 = how bright
#                              a pixel must be before it glows (0..1) ·
#                              16 = glow radius, px
#   chromablood  35            RGB fringing on edges (red/blue ghosting).
#                              35 = amount (0-100; 50 ≈ 8 px shift,
#                              100 ≈ 16 px)
#   lumar        35|4          Luma ringing — oscillating light/dark bands
#                              around high-contrast edges.
#                              35 = amount (0-100; 50 = 192 peak,
#                              100 = 384) · 4 = wavelength
#   rainbow      55|48|16      Dot-crawl rainbow phasing (crawling
#                              coloured fringes, old-SMPTE style).
#                              55 = amount (0-100) · 48 = rows per beat
#                              cycle · 16 = frames per beat cycle
#   tapewow      12|90         Slow vertical whole-picture drift (tape
#                              speed wobble).
#                              12 = drift amplitude, px · 90 = frames per
#                              drift cycle
#   scanlines    50|3|0        CRT scanlines (periodic dark rows).
#                              50 = intensity (0-100) · 3 = period, rows ·
#                              0 = offset, rows
#   shadowmask   0|25|3        Aperture-grille shadow bars (the vertical
#                              line structure over dark areas).
#                              0 = type (0 = grille) · 25 = intensity
#                              (0-100) · 3 = pitch
#   vignette     40            Corner darkening. 40 = amount (0-100)
#   barrel       40|100        Barrel distortion (the CRT glass curve).
#                              40 = amount (0-100; 50 = 0.50, 100 = 0.75)
#                              · 100 = zoom (0 = off, 100 = full): zooms
#                              the picture in so the curved edge margin
#                              shows content, not black
#   overscan     32|4          CRT face: rounded corners + bezel crop.
#                              32 = corner radius, px · 4 = bezel crop, px
#   glitch       20|28|12|24|24|16|16|20|3
#                Video-glitch bursts (mostly clean frames, occasional
#                violent tearing):
#                20 = burst density (0-100, how often bursts fire) ·
#                28 = RGB split (0-100) · 12 = static grain (0-100) ·
#                24 = slice tear strength, px · 24 = max block tile, px ·
#                16 = scanline jitter (0-100) · 16 = posterize (0-100) ·
#                20 = v-sync tear, px · 3 = seed (fixed for repeatability)
set -eu

cd "$(dirname "$0")/../.."

IN=inputvideos/test-short.mov
OUT=outputvideos/vintage_only.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts/vintage_only.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -vf "scale=1280:720,frei0r=libretrofx_vid_bloom:55|0.10|16,frei0r=libretrofx_vid_chromablood:35,frei0r=libretrofx_vid_lumar:35|4,frei0r=libretrofx_vid_rainbow:55|48|16,frei0r=libretrofx_vid_tapewow:12|90,frei0r=libretrofx_vid_scanlines:50|3|0,frei0r=libretrofx_vid_shadowmask:0|25|3,frei0r=libretrofx_vid_vignette:40,frei0r=libretrofx_vid_barrel:40|100,frei0r=libretrofx_vid_overscan:32|4,frei0r=libretrofx_vid_glitch:20|28|12|24|24|16|16|20|3" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"

# CRF is the size knob: 18 = near-lossless (largest), 20 = default here,
# 23 = noticeably smaller, still visually clean for this material.
