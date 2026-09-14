#!/bin/sh
# jimbots-compare-3pane.sh — three-pane comparison of inputvideos/jimbots.mp4,
# one source fanned into three branches, hstacked left to right:
#
#   LEFT   = ORIGINAL — the untouched source plate.
#   MIDDLE = VINTAGE — the dotpipe vintage chain (same recipe as
#            archive/frei0r/run-scripts/jimbots-vintage.sh, core values), applied
#            to jimbots.mp4 at native 1920×1080.
#   RIGHT  = GAMMA + SPAcENGRAVE — the approved "just gamma" recipe:
#            eq=gamma=1.5 + dot.spacengrave 5 85 80 68 200 78 360 100 5
#            (run-scripts-dotpipe/jimbots-gamma-spacengrave.sh recipe),
#            Metal backend.
#
#   out = outputvideos/jimbots_3pane-dotpipe.mp4
#         (default 3×720 × 1280 = 2160×1280)
#
# Each branch is fitted into an identical WxH cell (aspect preserved,
# padded, no stretch) so all three panes always match — even if you
# swap in different sources or tweak a branch to a different size.
#
# Architecture — dotpipe runs OUTSIDE ffmpeg (raw pipe), so each branch
# lands in a raw file in a mktemp dir (removed by trap), then one final
# ffmpeg hstacks them:
#
#   LEFT    = decode + cell fit                          → left.raw
#   MIDDLE  = decode → dotpipe vintage (CPU) → cell fit  → mid.raw
#   RIGHT   = decode (gamma lift) → dotpipe spacengrave  → right.raw
#                                            (Metal) → cell fit
#   final   = hstack the three cell raws → encode
#
# Temp size ≈ 3 × (frames × W × H × 3 bytes) — ~6.8 GB for jimbots.mp4.
#
# The vintage chain is CPU core (no GPU needed); the spacengrave pane
# requires Metal — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error, so the RIGHT pane can never
# silently be the CPU pass.
#
# Vintage chain params (one line per effect, matching the pipeline):
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
# Spacengrave params (in the order in the pipeline):
#   --spacengrave 5 \            scanline pitch px (native res; 0 = off)
#     85 \                       stroke width, % of pitch (the bold reference line)
#     80 \                       tone-coupled ink shading (the engraved 3D read)
#     68 \                       baseline line solidity (0-100)
#     200 \                      ink brightness ×100 (2× = the approved gain)
#     78 \                       texture break-up into dashes (the engraving knob)
#     360 \                      ink colour (360 = pure white)
#     100 \                      photo dimmed to pure black under the ink
#     5 \                        subject gate knee (approved on this footage)
#
# Usage:  sh run-scripts-dotpipe/jimbots-compare-3pane.sh [-i input] [-o output]
#         [-w 720] [-h 1280]
set -eu

cd "$(dirname "$0")/.."

IN=inputvideos/jimbots.mp4
OUT=outputvideos/jimbots_3pane-dotpipe.mp4
W=720
H=1280
while getopts "i:o:w:h:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    w) W=$OPTARG ;;
    h) H=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/jimbots-compare-3pane.sh [-i input] [-o output] [-w 720] [-h 1280]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

NW=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
NH=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
ERRLOG="$TMP/dotpipe-err"; : > "$ERRLOG"

# One branch -> an identical WxH cell: aspect preserved, padded, square
# pixels — so the three panes always match regardless of the branch.
CELL="scale=${W}:${H}:force_original_aspect_ratio=decrease,pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2,setsar=1"

# LEFT = original, fitted to the cell
# (-fps_mode passthrough on every decode: no CFR drop/dup, so the
# frame count matches the frei0r pipeline for quirky sources.)
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough -vf "$CELL" \
  -f rawvideo -pix_fmt rgb24 "$TMP/left.raw"

# MIDDLE = vintage chain (dotpipe, CPU core)
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -f rawvideo -pix_fmt rgb24 - \
  | ./dotpipe/dotpipe -w "$NW" -h "$NH" --fps "$R" \
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
  | ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 \
      -s "${NW}x${NH}" -i - -vf "$CELL" \
      -f rawvideo -pix_fmt rgb24 "$TMP/mid.raw"

# RIGHT = gamma lift + spacengrave (dotpipe, Metal)
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough -vf "eq=gamma=1.5" \
  -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$NW" -h "$NH" --fps "$R" \
      --spacengrave 5 \
      85 \
      80 \
      68 \
      200 \
      78 \
      360 \
      100 \
      5 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 \
      -s "${NW}x${NH}" -i - -vf "$CELL" \
      -f rawvideo -pix_fmt rgb24 "$TMP/right.raw"

if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi

# Join the three cell-matched branches
ffmpeg -hide_banner -loglevel error -y \
  -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$R" -i "$TMP/left.raw" \
  -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$R" -i "$TMP/mid.raw" \
  -f rawvideo -pix_fmt rgb24 -s "${W}x${H}" -r "$R" -i "$TMP/right.raw" \
  -filter_complex "[0:v][1:v][2:v]hstack=inputs=3" \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

# yuv444p: the spacengrave pane needs it (4:2:0 mangles the thin
# strokes); it's harmless to the other two. CRF 20 = the house size.
