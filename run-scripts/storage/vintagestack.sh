#!/bin/sh
# vintagestack.sh — vintage stack + faint dot.gate overlay
# (README: "Vintage stack + faint dot.gate overlay").
#
# Usage:  sh run-scripts/vintagestack.sh [-i input] [-o output]
#         (defaults: inputvideos/test-short.mov → outputvideos/vintage_dots.mp4)
#
# The dot branch mirrors the vintage chain (same prefix, same glass
# stages) with dotgate inserted after tapewow: the dot map is computed
# on the same pixels and gets the same geometry (barrel zoom, tapewow
# drift), so it registers onto the head. A flat dot layer floats
# offset — barrel zoom pushes the head's features outward while the
# dots stay flat, reading as a ghost "skull" inside the head.
#
# Effect params (pipe values → meaning, in chain order; each effect gets
# a plain-English description first):
#   bloom        40|0.10|16    Soft glow around the bright areas.
#                              40 = strength (0-100) · 0.10 = how bright
#                              a pixel must be before it glows (0..1) ·
#                              16 = glow radius, px
#   chromablood  25            RGB fringing on edges (red/blue ghosting).
#                              25 = amount (0-100; 50 ≈ 8 px shift,
#                              100 ≈ 16 px)
#   lumar        25|4          Luma ringing — oscillating light/dark bands
#                              around high-contrast edges.
#                              25 = amount (0-100; 50 = 192 peak,
#                              100 = 384) · 4 = wavelength
#   rainbow      40|48|16      Dot-crawl rainbow phasing (crawling
#                              coloured fringes, old-SMPTE style).
#                              40 = amount (0-100) · 48 = rows per beat
#                              cycle · 16 = frames per beat cycle
#   tapewow      8|90          Slow vertical whole-picture drift (tape
#                              speed wobble).
#                              8 = drift amplitude, px · 90 = frames per
#                              drift cycle
#   dotgate      8|4|10        (dot branch only) the subject as a dot map:
#                              8 = dot pitch, px · 4 = wobble, px ·
#                              10 = subject knee ×100 (10 = 0.10)
#   scanlines    35|3|0        CRT scanlines (periodic dark rows).
#                              35 = intensity (0-100) · 3 = period, rows ·
#                              0 = offset, rows
#   shadowmask   0|15|3        Aperture-grille shadow bars (the vertical
#                              line structure over dark areas).
#                              0 = type (0 = grille) · 15 = intensity
#                              (0-100) · 3 = pitch
#   vignette     25            Corner darkening. 25 = amount (0-100)
#   barrel       25|100        Barrel distortion (the CRT glass curve).
#                              25 = amount (0-100; 50 = 0.50, 100 = 0.75)
#                              · 100 = zoom (0 = off, 100 = full): zooms
#                              the picture in so the curved edge margin
#                              shows content, not black
#   overscan     24|4          CRT face: rounded corners + bezel crop.
#                              24 = corner radius, px · 4 = bezel crop, px
#   glitch       15|20|8|24|24|12|12|15|3
#                Video-glitch bursts (mostly clean frames, occasional
#                violent tearing):
#                15 = burst density (0-100, how often bursts fire) ·
#                20 = RGB split (0-100) · 8 = static grain (0-100) ·
#                24 = slice tear strength, px · 24 = max block tile, px ·
#                12 = scanline jitter (0-100) · 12 = posterize (0-100) ·
#                15 = v-sync tear, px · 3 = seed (fixed for repeatability)
#   dot layer    colorchannelmixer rr/gg/bb = 0.20 → faint 20% overlay,
#                screen-blended over the vintage chain
set -eu

cd "$(dirname "$0")/.."

IN=inputvideos/test-short.mov
OUT=outputvideos/vintage_dots.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts/vintagestack.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

FREI0R_PATH=$PWD/build ffmpeg -hide_banner -loglevel error -y -i "$IN" \
  -filter_complex "
    [0:v]scale=1280:720,split=2[b1][b2];
    [b1]frei0r=libretrofx_vid_bloom:40|0.10|16,
        frei0r=libretrofx_vid_chromablood:25,
        frei0r=libretrofx_vid_lumar:25|4,
        frei0r=libretrofx_vid_rainbow:40|48|16,
        frei0r=libretrofx_vid_tapewow:8|90,
        frei0r=libretrofx_vid_scanlines:35|3|0,
        frei0r=libretrofx_vid_shadowmask:0|15|3,
        frei0r=libretrofx_vid_vignette:25,
        frei0r=libretrofx_vid_barrel:25|100,
        frei0r=libretrofx_vid_overscan:24|4,
        frei0r=libretrofx_vid_glitch:15|20|8|24|24|12|12|15|3,
        format=rgba[vt];
    [b2]frei0r=libretrofx_vid_bloom:40|0.10|16,
        frei0r=libretrofx_vid_chromablood:25,
        frei0r=libretrofx_vid_lumar:25|4,
        frei0r=libretrofx_vid_rainbow:40|48|16,
        frei0r=libretrofx_vid_tapewow:8|90,
        frei0r=libretrofx_dotgate:8|4|10,
        frei0r=libretrofx_vid_scanlines:35|3|0,
        frei0r=libretrofx_vid_shadowmask:0|15|3,
        frei0r=libretrofx_vid_vignette:25,
        frei0r=libretrofx_vid_barrel:25|100,
        frei0r=libretrofx_vid_overscan:24|4,
        frei0r=libretrofx_vid_glitch:15|20|8|24|24|12|12|15|3,
        colorchannelmixer=rr=0.20:gg=0.20:bb=0.20,
        format=rgba[dots];
    [vt][dots]blend=all_mode=screen,format=rgba[out]" \
  -map "[out]" -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p \
  "$OUT"

# CRF is the size knob: 18 = near-lossless (largest), 20 = default here,
# 23 = noticeably smaller, still visually clean for this material.
