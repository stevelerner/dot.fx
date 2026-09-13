#!/usr/bin/env bash
# ffmpeg_test.sh — end-to-end acceptance test for frei0r plugins using wbl.mov
#
# usage: tools/ffmpeg_test.sh [effect ...]
#   effect = plugin base name, e.g. "scanlines", "shadowmask", "chromablood"
#   (no args → test every build/libretrofx_*.dylib found)
#
# For each effect:
#   1. symlink the dylib to a .so (this ffmpeg build appends ".so" to the
#      module name — Linux convention)
#   2. render wbl.mov through the plugin with the frei0r-enabled ffmpeg
#   3. verify: ffmpeg exits 0, output exists, output frames actually differ
#      from input (compare md5 of one decoded frame from each)
#
# env: FFMPEG — path to a frei0r-enabled ffmpeg
#      (default: /opt/homebrew/bin/ffmpeg, the ffmpeg-full build)

set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FFMPEG="${FFMPEG:-/opt/homebrew/bin/ffmpeg}"
VIDEO="$REPO/inputvideos/wbl.mov"

fail() { echo "FAIL: $*" >&2; exit 1; }

[ -x "$FFMPEG" ] || fail "ffmpeg not found at $FFMPEG (set FFMPEG=...)"
"$FFMPEG" -filters 2>/dev/null | grep -qw 'frei0r' \
  || fail "$FFMPEG has no frei0r filter — use a frei0r-enabled build (Homebrew ffmpeg-full)"
[ -f "$VIDEO" ] || fail "test video not found: $VIDEO"

if [ "$#" -gt 0 ]; then
  EFFECTS=("$@")
else
  EFFECTS=()
  for dylib in "$REPO"/build/libretrofx_*.dylib; do
    [ -e "$dylib" ] || continue
    EFFECTS+=("$(basename "$dylib" .dylib)")
  done
fi
[ "${#EFFECTS[@]}" -gt 0 ] \
  || fail "no build/libretrofx_*.dylib found — build one first (plan.md §8 Task 5)"

frame_md5() {
  # md5 of one decoded video frame (t≈1s) from a container; empty on failure
  "$FFMPEG" -hide_banner -loglevel error -ss 1 -i "$1" -frames:v 1 -f rawvideo - 2>/dev/null \
    | md5
}

MD5_IN="$(frame_md5 "$VIDEO")"
[ -n "$MD5_IN" ] || fail "could not decode reference frame from $VIDEO"

overall=0
for base in "${EFFECTS[@]}"; do
  name="libretrofx_${base}"
  dylib="$REPO/build/${name}.dylib"
  if [ ! -f "$dylib" ]; then
    echo "SKIP: $base (no $name.dylib — build it first)"
    continue
  fi

  echo "== $base"
  ln -sf "${name}.dylib" "$REPO/build/${name}.so"

  out="$REPO/outputvideos/wbl_${base}.mov"
  if ! FREI0R_PATH="$REPO/build" "$FFMPEG" -hide_banner -loglevel error -y \
      -i "$VIDEO" -vf "frei0r=${name}" "$out" 2>&1; then
    echo "FAIL: $base — ffmpeg render failed"
    overall=1
    continue
  fi
  [ -s "$out" ] || { echo "FAIL: $base — output missing or empty ($out)"; overall=1; continue; }

  md5_out="$(frame_md5 "$out")"
  if [ -z "$md5_out" ]; then
    echo "FAIL: $base — could not decode output frame"
    overall=1
  elif [ "$MD5_IN" = "$md5_out" ]; then
    echo "FAIL: $base — output identical to input (effect not applied?)"
    overall=1
  else
    echo "PASS: $base — exit 0, $(du -h "$out" | cut -f1), output frames differ from input"
  fi
done

if [ "$overall" -ne 0 ]; then
  echo "RESULT: FAILURES — see above"
  exit 1
fi
echo "RESULT: ALL PASS"
