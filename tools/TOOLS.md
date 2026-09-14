# TOOLS.md — tool inventory + environment requirements

Everything in this repo that exists to build, verify, or render: what it is,
how to run it, and what must be installed for the project to keep going.
Read this when starting from scratch or when the environment misbehaves.

## tools/ — C test harnesses and ffmpeg wrappers

### `f0r_host` (archived: `archive/frei0r/f0r_host`)
Minimal frei0r host for the archived plugin method: dlopens a plugin dylib
and exercises the `f0r_*` ABI directly, no ffmpeg. Modes:
`apply | identity | roundtrip | determinism`.

    archive/frei0r/f0r_host archive/frei0r/build/libretrofx_dotportal.dylib apply 64 64

- 64×64 RGBA gradient test frame.
- Per-plugin test vectors (off-values + in-domain roundtrip values) live in
  the source, kept in sync with `archive/frei0r/adapter/retrofx.c`.
- Tests by the **reported plugin name** — a mis-named dylib silently tests
  the wrong effect.
- `RETROFX_BACKEND=cpu|metal|dual`; `dual` runs CPU and Metal and
  byte-compares (MISMATCH → stderr). Acceptance bar: `dual` byte-identical.

### `sprcmp` (`sprcmp.c`)
Byte-compares CPU vs GPU **sprite dumps** (gather level) for dotgate —
parity-debug tool, reports which cells/fields differ in hex bits.
Uses an archived plugin dylib:

    tools/sprcmp archive/frei0r/build/libretrofx_dotgate.dylib [w h frame]

### `checkmask` (`checkmask.c`)
Measures shadow-mask distribution on a flat white PPM: scans bands of
3×pitch columns, reports the pure-white fraction per band; spread ≤ 2 pts =
pass (exit 0/1).

    tools/checkmask testimgs/flat_white.ppm <pitch>

### `mktestimg.c`
Generates `bars.ppm` / `flat_white.ppm` / `gradient.ppm` into an outdir
(= `make testimgs`; copies already in `testimgs/`).

### `fx.sh` (archived: `archive/frei0r/fx.sh`)
Named-param ffmpeg wrapper for the frei0r method: expands
`dotgate size=28 speed=8` into the positional frei0r form, sets
`FREI0R_PATH` / `RETROFX_BACKEND` (default `metal`), and passes encoder args
through.

### `ffmpeg_test.sh` (archived: `archive/frei0r/ffmpeg_test.sh`)
End-to-end acceptance test for the frei0r method: renders a test clip
through each `archive/frei0r/build/libretrofx_*.dylib` and verifies ffmpeg
exits 0 and output frames differ from input. Needs an ffmpeg with the frei0r
filter (`brew install ffmpeg-full`).

### `gate_diag.py` (Python)
dot.spacengrave gate diagnosis: replicates `portal_split` (Otsu split +
minority-side subject flag) and the `local_shade` edge probe exactly
(float32, per-pixel), then reports per-region gate pass fractions
(whole / background top-200 / face / body), the split threshold, and the
ink levels a recipe would produce — plus the approved dot.portal frame for
overlap. Use it whenever the subject gate misfires (wrong region, face
excluded, background leaking ink).

### `measure_frame.py` (Python)
Measures any render frame: whole/background/face/body luma stats (mean,
frac >10/>60/>150, 8-band histogram) and the subject/bg luma ratio; pass a
second render (e.g. the approved dot.portal frame of the same instant) for
side-by-side numbers. This is where the acceptance numbers for "the pattern
IS the subject on black" come from (black background, bright sparse ink,
ratio ≥ 3×).

## harness/ — standalone CLI on the cores (no ffmpeg, no Metal)
`main.c` → binary `harness/harness` (gitignored). Drives the effect cores
directly on PPM input with per-effect flags:

    harness/harness --scanlines --mask --barrel --bloom --vignette \
        --overscan --bleed --lumar --rainbow --wow --dotgate --glitch --frame

## dotpipe/ — raw-pipe effect host (ffmpeg as codec only) — THE active method
`dotpipe/dotpipe` (gitignored; built by `make` / the Makefile rule).
Reads raw RGB24 frames (`w*h*3` bytes) from stdin, applies the selected
effects in CLI order, writes to stdout, frame index auto-increments
(time effects animate). Same cores as harness/ — plus `--dotportal` and
`--spacengrave`, which the harness lacks — and the four Metal effects
dispatch on `RETROFX_BACKEND=cpu|metal|dual` (dual runs both, byte-compares,
returns the CPU result).

    # single raw frame
    ./dotpipe/dotpipe -w 720 -h 1280 --dotgate 28 8 10 < in.raw > out.raw

    # full clip (any ffmpeg, frei0r not required)
    ffmpeg -i in.mp4 -f rawvideo -pix_fmt rgb24 - \
      | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 1920 -h 1080 --spacengrave 5 85 80 68 200 78 360 100 5 \
      | ffmpeg -f rawvideo -pix_fmt rgb24 -s 1920x1080 -i - out.mp4

No effect flags = byte-identical passthrough.

## run-scripts-dotpipe/ — approved-look render recipes (not test tools)
Each script `cd`s to the repo root and renders at the source's native
geometry; spacengrave/3pane outputs use `yuv444p`, vintage `yuv420p`.
All take `[-i input] [-o output]` (3pane also `[-w 720] [-h 1280]`).

- `model-vintage.sh` / `jimbots-vintage.sh` — the approved 9-effect `vid.*`
  CRT/VHS stack at locked levels (CPU cores, no GPU needed)
- `model-gamma-spacengrave.sh` / `jimbots-gamma-spacengrave.sh` — the
  approved dot.spacengrave engraved line screen (gamma shadow lift, native
  resolution; Metal backend — a CPU-fallback warning is a hard error)
- `model-compare-3pane.sh` / `jimbots-compare-3pane.sh` — three-pane
  hstacked comparison (original | vintage | gamma + spacengrave; each
  branch lands in a raw file, final `hstack` encode)

The archived frei0r versions of the same recipes live in
`archive/frei0r/run-scripts/` (plus parked older ones in
`archive/frei0r/run-scripts/storage/`).

## Verification assets
- `testimgs/{bars,flat_white,gradient}.ppm` — generated test frames
  (see `mktestimg`).

## Environment — what must be installed to keep going
- **macOS + Xcode Command Line Tools** (Apple clang). C99,
  `-Wall -Wextra -Werror`; allowed deps: stdlib + Metal/Foundation only.
- **Any ffmpeg** at `/opt/homebrew/bin/ffmpeg` (codec only for dotpipe;
  the frei0r filter — `brew install ffmpeg-full` — is needed only for the
  archived method in `archive/frei0r/`).
- **Python venv** at `/Volumes/external/code/.venv` with **numpy** and
  **pillow** — used for numerical image diagnosis (measuring halos, coverage,
  dot brightness — do it at 4K against the 4K subject mask; 720p
  measurements lie). **scipy is NOT installed** — use ring-dilation distance
  instead of distance transforms.
- Note: `make` hangs on this volume — build single targets / direct `cc`
  (recipes below); the Makefile targets are the reference.

## Rebuild

Makefile targets (reference):

- `make` — `dotpipe/dotpipe` (+ harness, testimgs)
- `make inputvideos` — the mktestimg PPMs
- `make clean`
- archived frei0r plugins: `make -f archive/frei0r/Makefile <single-target>`

Direct `cc` (proven):

    cc -std=c99 -O2 -Wall -Wextra -Werror -Icore -o dotpipe/dotpipe \
       dotpipe/dotpipe.c core/crt.c core/vhs.c core/glitch.c core/dot.c core/metal_fx.m \
       -lm -framework Metal -framework Foundation

Archived dylibs (one per effect via `-DRETROFX_EFFECT`): see
`archive/frei0r/Makefile`.

## Verification one-liners

    # narrowest dotpipe check (single raw frame through a GPU effect):
    ./dotpipe/dotpipe -w 720 -h 1280 --dotgate 28 8 10 < in.raw > out.raw
    # GPU parity (acceptance bar: byte-identical):
    RETROFX_BACKEND=dual ./dotpipe/dotpipe -w 1280 -h 720 --dotgate 28 8 10 < in.raw > out.raw
    # archived frei0r plugin check:
    RETROFX_BACKEND=dual archive/frei0r/f0r_host archive/frei0r/build/libretrofx_dotgate.dylib apply 1280 720
