#!/bin/sh
# model-gamma-spacengrave-vapor.sh — the approved dot.spacengrave look,
# slowed down with a vapor comet trail off the moving ink. Same pipeline
# as model-gamma-spacengrave-metal.sh plus the --vapor mode on dotpipe:
#
#   --vapor 2 95 60 90
#     2   = each frame written twice (half speed: 30 fps source plays at
#           15 fps cadence — duration doubles, motion halves)
#     95  = trail persistence per output frame (0.95: slow fade, so the
#           comet tail stays visible)
#     60  = trail strength (60% of the ghost's remaining ink)
#     90  = x-axis streak: the trail smears along each row (~10px tail,
#           no vertical spread) — a comet, not a fog (0 would be
#           in-place haze). The trail feeds ONLY where ink advanced
#           horizontally, so vertical motion leaves no trail.
#
# The trail is temporal state in dotpipe (a per-pixel buffer that keeps
# the bright ink and decays it each output frame), so it can only be
# added at the stream level: --vapor goes LAST in the chain, after
# --spacengrave. It never darkens the current frame — where the trail
# is brighter than the frame it adds up to faint% of the difference;
# with dim=100 (black behind) only the engraved lines leave a trail.
#
# PIPELINE (left to right):
#
# STAGE 1 — decode + eq=gamma=1.5 → raw RGB24 (ffmpeg, codec only)
# STAGE 2 — dotpipe --spacengrave ... --vapor 2 95 60 90   (Metal backend)
#   9 spacengrave parameters (the approved recipe), then the vapor mode
#   (see above for the four values).
# STAGE 3 — encode (libx264 crf 20, yuv444p). The encoder still runs at
#   the source's frame rate, so the duplicated frames stretch the
#   duration — that IS the slow motion.
#
# Backend: METAL — a "Metal unavailable/failed" fallback warning on
# stderr is treated as a hard error (check below).
#
# Usage:  sh run-scripts-dotpipe/model-gamma-spacengrave-vapor-metal.sh [-i input] [-o output]
#         (defaults: inputvideos/model.mp4 → outputvideos/model-gamma-spacengrave-vapor-dotpipe.mp4)
set -eu

cd "$(dirname "$0")/.."

# -- defaults (override with -i / -o) ------------------------------------
IN=inputvideos/model.mp4
OUT=outputvideos/model-gamma-spacengrave-vapor-dotpipe.mp4

# -- arg parse ------------------------------------------------------------
while getopts "i:o:" opt; do
  case $opt in
    i) IN=$OPTARG ;;
    o) OUT=$OPTARG ;;
    *) echo "usage: sh run-scripts-dotpipe/model-gamma-spacengrave-vapor-metal.sh [-i input] [-o output]" >&2
       exit 2 ;;
  esac
done
mkdir -p "$(dirname "$OUT")"

# -- capture dotpipe's stderr separately: a Metal fallback warning must
#    fail the render, not just print (checked after the pipeline) ---------
ERRLOG=/tmp/dotpipe-err.$$; : > "$ERRLOG"

# -- source geometry (probed; the subject keeps its native resolution) ----
W=$(ffprobe -v error -select_streams v:0 -show_entries stream=width  -of csv=p=0 "$IN")
H=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$IN")
R=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of csv=p=0 "$IN")

# -- the render pipeline (one logical command, three stages) --------------
ffmpeg -hide_banner -loglevel error -i "$IN" -fps_mode passthrough \
  -vf "eq=gamma=1.5" -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w "$W" -h "$H" --fps "$R" \
      --spacengrave 5 \
      85 \
      80 \
      68 \
      200 \
      78 \
      360 \
      100 \
      5 \
      --vapor 2 \
      95 \
      60 \
      90 2>> "$ERRLOG" \
  | ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt rgb24 \
      -s "${W}x${H}" -r "$R" -i - \
      -c:v libx264 -preset medium -crf 20 -pix_fmt yuv444p "$OUT"

# -- hard error on Metal fallback (see header: Backend) --------------------
if grep -q "Metal unavailable/failed" "$ERRLOG"; then
  cat "$ERRLOG" >&2
  exit 3
fi
rm -f "$ERRLOG"
