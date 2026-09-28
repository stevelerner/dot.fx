#!/bin/sh
# model-vintage-cpu.sh — full vintage stack (no dot.gate), NO glitch, via
# dotpipe (the raw-pipe effect host): ffmpeg decodes to raw RGB24,
# dotpipe applies the ten-effect chain IN ORDER, ffmpeg encodes.
# Same recipe as archive/frei0r/run-scripts/model-vintage.sh, in
# dotpipe-native core values (dial value in parentheses):
#
#   --bloom 1.20 0.10 16 \        soft glow on highlights; strength, threshold 0..1, radius px (dial 60|0.10|16)
#   --bleed 4 1.0 \               RGB fringing on edges; radius px, amount (dial chromablood 25)
#   --lumar 0.50 4 \              luma ringing around edges; amount, wavelength px (dial 25|4)
#   --rainbow 0.80 48 16 \        dot-crawl rainbow phasing; amount, rows per beat, frames per beat (dial 40|48|16)
#   --wow 8 90 \                  slow vertical drift; amplitude px, frames per cycle (dial tapewow 8|90)
#   --scanlines 0.65 3 0 \        CRT dark rows; intensity, period rows, offset rows (dial 65|3|0)
#   --mask grille 0.048 3 \       aperture-grille bars; type, intensity, pitch px (dial shadowmask 0|4|3)
#   --vignette 0.70 \             corner darkening (dial 35)
#   --barrel 0.41 1.0 \           CRT glass curve; amount, zoom (margin vanishes) (dial 35|100)
#   --overscan 24 4 \             rounded corners + bezel crop; radius px, crop px (dial 24|4)
#
# (Per-parameter meaning: archive/frei0r/run-scripts/model-vintage.sh header.)
#
# All ten effects are CPU core (the Metal implementations cover
# glitch/dotgate/portal/spacengrave) — no GPU needed.
#
# Usage:  sh run-scripts-dotpipe/model-vintage-cpu.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-vintage-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

# -- defaults (override with -i / -o) ------------------------------------
IN=inputvideos/model.mp4
OUT=outputvideos/model-vintage-dotpipe.mp4

# -- arg parse ------------------------------------------------------------
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-vintage-cpu.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

# -- source geometry (probed once; the subject keeps its native resolution
#    — there is no scale anywhere in the pipeline) -------------------------
W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# -- the render pipeline (one logical command, three stages) --------------
# STAGE 1 — ffmpeg decode → raw RGB24 on stdout.
#   -fps_mode passthrough: no CFR drop/dup at decode, so the frame count
#   matches the frei0r (filtergraph) pipeline exactly even for sources
#   with quirky timestamps (jimbots.mp4 otherwise gains 5 frames).
# STAGE 2 — dotpipe: the ten-effect vintage chain, applied in the order
#   below (one flag per line; param meanings in the header above).
# STAGE 3 — ffmpeg encode: libx264 crf 20, yuv420p (house settings).
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -f rawvideo -pix_fmt rgb24 - \
  | ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --bloom 1.20 0.10 16 \
      --bleed 4 1.0 \
      --lumar 0.50 4 \
      --rainbow 0.80 48 16 \
      --wow 8 90 \
      --scanlines 0.65 3 0 \
      --mask grille 0.048 3 \
      --vignette 0.70 \
      --barrel 0.41 1.0 \
      --overscan 24 4 \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p "$OUT"

# Geometry note: no `scale=` — the subject keeps its native portrait
# resolution (720x1280 for inputvideos/model.mp4) rather than being
# stretched to a landscape frame.
