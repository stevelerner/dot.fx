#!/bin/sh
# model-vintage.sh — full vintage stack (no dot.gate), NO glitch, via
# dotpipe (the raw-pipe effect host): ffmpeg decodes to raw RGB24,
# dotpipe applies the ten-effect chain IN ORDER, ffmpeg encodes.
# Same recipe as archive/frei0r/run-scripts/model-vintage.sh, in
# dotpipe-native core values (dial value in parentheses):
#
#   bloom       1.20 0.10 16   Soft glow on highlights (60|0.10|16).
#                              1.20 = strength · 0.10 = threshold 0..1 ·
#                              16 = radius px
#   bleed        4 1.0         RGB fringing on edges (chromablood 25).
#                              4 = radius px · 1.0 = amount
#   lumar       0.50 4         Luma ringing around edges (25|4).
#                              0.50 = amount · 4 = wavelength px
#   rainbow     0.80 48 16     Dot-crawl rainbow phasing (40|48|16).
#                              0.80 = amount · 48 = rows per beat ·
#                              16 = frames per beat
#   wow         8 90           Slow vertical drift (tapewow 8|90).
#                              8 = amplitude px · 90 = frames per cycle
#   scanlines   0.65 3 0       CRT dark rows (65|3|0).
#                              0.65 = intensity · 3 = period rows ·
#                              0 = offset rows
#   mask        grille 0.048 3  Aperture-grille bars (shadowmask 0|4|3).
#                              grille = type · 0.048 = intensity ·
#                              3 = pitch
#   vignette    0.70           Corner darkening (35).
#   barrel      0.41 1.0       CRT glass curve (35|100).
#                              0.41 = amount · 1.0 = zoom (margin vanishes)
#   overscan    24 4           Rounded corners + bezel crop (24|4).
#                              24 = radius px · 4 = crop px
#
# (Per-parameter meaning: archive/frei0r/run-scripts/model-vintage.sh header.)
#
# All ten effects are CPU core (the Metal implementations cover
# glitch/dotgate/portal/spacengrave) — no GPU needed.
#
# Usage:  sh run-scripts-dotpipe/model-vintage.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-vintage-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

IN=inputvideos/model.mp4
OUT=outputvideos/model-vintage-dotpipe.mp4
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-vintage.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# decode -> dotpipe (the ten-effect vintage chain, in order) -> encode
# -fps_mode passthrough: no CFR drop/dup at decode, so the frame count
# matches the frei0r (filtergraph) pipeline exactly even for sources
# with quirky timestamps (jimbots.mp4 otherwise gains 5 frames).
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -f rawvideo -pix_fmt rgb24 - \
  | ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --bloom 1.20 0.10 16 --bleed 4 1.0 --lumar 0.50 4 \
      --rainbow 0.80 48 16 --wow 8 90 --scanlines 0.65 3 0 \
      --mask grille 0.048 3 --vignette 0.70 --barrel 0.41 1.0 \
      --overscan 24 4 \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p "$OUT"

# Geometry note: no `scale=` — the subject keeps its native portrait
# resolution (720x1280 for inputvideos/model.mp4) rather than being
# stretched to a landscape frame.
