#!/bin/sh
# fx — dot.fx effects with named parameters.
#
# ffmpeg's frei0r wrapper only takes POSITIONAL params
# (frei0r=libretrofx_vid_glitch:100|50|...), so fx expands the readable
# named form into that, and sets FREI0R_PATH / RETROFX_BACKEND for you:
#
#   tools/fx.sh -i input.mov -o outputvideos/max.mov \
#     dotgate size=28 speed=8 gate=10 \
#     vidglitch amount=100 rgb=100 noise=100 bands=100 blocks=100 \
#               scan=100 quant=100 vtear=100 seed=3
#
#   → ffmpeg ... -vf "frei0r=libretrofx_dotgate:28|8|10,frei0r=libretrofx_vid_glitch:100|100|100|100|100|100|100|100|3"
#
# - bare numbers work too (positional, in param order):  dotgate 28 8 10
# - `|` and spaces around `=` are accepted:  vidglitch amount = 100 | rgb = 39
# - omitted params take the adapter defaults (README "Effect parameters" table)
# - words before the first stage pass through to ffmpeg
#   (e.g. -c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p)
#
# options:
#   -i file    input (required)
#   -o file    output (required)
#   -b name    backend cpu | metal | dual  (default: $RETROFX_BACKEND or metal)
#   -h         this help
#
# effects: dotgate vidglitch shadowmask scanlines chromablood
#          bloom barrel vignette overscan lumar rainbow tapewow

set -u

die() { printf 'fx: %s\n' "$*" >&2; exit 2; }

usage() {
  sed -n '2,28p' "$0" | sed 's/^# \{0,1\}//'
}

FXROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${FREI0R_PATH:-}" ]; then
  FREI0R_PATH=$FXROOT/build
  [ -d "$FREI0R_PATH" ] || die "no $FREI0R_PATH — build first (README, Build)"
fi
BACKEND=${RETROFX_BACKEND:-metal}

EFFECTS="dotgate vidglitch shadowmask scanlines chromablood bloom barrel vignette overscan lumar rainbow tapewow"

is_effect() {
  case " $EFFECTS " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

plugin_of() {
  case $1 in
    vidglitch) printf 'libretrofx_vid_glitch' ;;
    *)         printf 'libretrofx_%s' "$1" ;;
  esac
}

params_of() {
  case $1 in
    dotgate)     printf 'size speed gate' ;;
    vidglitch)   printf 'amount rgb noise bands blocks scan quant vtear seed' ;;
    shadowmask)  printf 'type intensity pitch' ;;
    scanlines)   printf 'intensity period offset' ;;
    chromablood) printf 'amount' ;;
    bloom)       printf 'amount threshold radius' ;;
    barrel)      printf 'amount' ;;
    vignette)    printf 'amount' ;;
    overscan)    printf 'radius margin' ;;
    lumar)       printf 'amount wavelength' ;;
    rainbow)     printf 'amount period_y period_t' ;;
    tapewow)     printf 'amplitude period' ;;
  esac
}

defaults_of() {
  case $1 in
    dotgate)     printf '24 6 10' ;;
    vidglitch)   printf '30 4 8 45 0 0 0 28 3' ;;
    shadowmask)  printf '0 50 3' ;;
    scanlines)   printf '50 3 0' ;;
    chromablood) printf '50' ;;
    bloom)       printf '50 0.10 16' ;;
    barrel)      printf '50' ;;
    vignette)    printf '50' ;;
    overscan)    printf '48 8' ;;
    lumar)       printf '50 4' ;;
    rainbow)     printf '50 48 16' ;;
    tapewow)     printf '50 90' ;;
  esac
}

nth() { # $1 = space-separated list, $2 = 1-based n
  n=$2
  for w in $1; do
    if [ "$n" = 1 ]; then printf '%s' "$w"; return 0; fi
    n=$((n - 1))
  done
  return 1
}

# expand_stage EFFECT [spec...] → positional "v1|v2|..."
expand_stage() {
  eff=$1; shift
  names=$(params_of "$eff") || die "unknown effect '$eff' (try: $EFFECTS)"
  defs=$(defaults_of "$eff")

  pairs=""; pos=""
  for s in "$@"; do
    case $s in
      *=*) pairs="$pairs $s" ;;
      *)   pos="$pos $s" ;;
    esac
  done
  seen=""
  for p in $pairs; do
    nm=${p%%=*}
    case " $names " in *" $nm "*) : ;; *) die "$eff: unknown parameter '$nm' (expected: $names)" ;; esac
    case " $seen " in *" $nm "*) die "$eff: parameter '$nm' given twice" ;; *) seen="$seen $nm" ;; esac
  done

  out=""; n=0
  for name in $names; do
    n=$((n + 1))
    val=""
    for p in $pairs; do
      case $p in "$name="*) val=${p#*=}; break ;; esac
    done
    [ -n "$val" ] || val=$(nth "$pos" "$n") || val=$(nth "$defs" "$n")
    case $val in
      ''|*[!0-9.-]*) die "$eff: '$name' expects a number (got '$val')" ;;
    esac
    if [ -n "$out" ]; then out="$out|$val"; else out="$val"; fi
  done
  printf '%s' "$out"
}

IN=""; OUT=""
while [ $# -gt 0 ]; do
  case $1 in
    -h) usage; exit 0 ;;
    -i) [ $# -ge 2 ] || die "-i needs a file"; IN=$2; shift 2 ;;
    -o) [ $# -ge 2 ] || die "-o needs a file"; OUT=$2; shift 2 ;;
    -b) [ $# -ge 2 ] || die "-b needs a backend"
        case $2 in cpu|metal|dual) : ;; *) die "backend must be cpu|metal|dual (got '$2')" ;; esac
        BACKEND=$2; shift 2 ;;
    *) break ;; # everything else (flags, values, stages) is pass-through
  esac
done
[ -n "$IN" ]  || die "missing -i input"
[ -n "$OUT" ] || die "missing -o output"

# accept `|` as separator and spaces around `=`: "amount = 100 | rgb = 39"
WORDS=$(printf '%s ' "$@" | sed -e 's/|/ /g' -e 's/ *= */=/g')

VF=""; EXTRA=""; cur_eff=""; cur_specs=""

flush() {
  [ -n "$cur_eff" ] || return 0
  vf=$(expand_stage "$cur_eff" $cur_specs) || return 1
  stage="frei0r=$(plugin_of "$cur_eff"):$vf"
  if [ -n "$VF" ]; then VF="$VF,$stage"; else VF="$stage"; fi
  cur_eff=""; cur_specs=""
}

for w in $WORDS; do
  if is_effect "$w"; then
    flush || exit 2
    cur_eff=$w
  elif [ -n "$cur_eff" ]; then
    cur_specs="$cur_specs $w"
  else
    EXTRA="$EXTRA $w"
  fi
done
flush || exit 2

[ -n "$VF" ] || die "no effect stage given (effects: $EFFECTS)"

printf 'fx: backend=%s\nfx: -vf "%s"\n' "$BACKEND" "$VF" >&2

# $EXTRA expands word-by-word on purpose (pass-through flags)
# shellcheck disable=SC2086
FREI0R_PATH=$FREI0R_PATH RETROFX_BACKEND=$BACKEND \
  ffmpeg -hide_banner -loglevel error -y -i "$IN" -vf "$VF" $EXTRA "$OUT"
