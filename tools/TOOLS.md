# TOOLS.md — tool inventory + environment requirements

Everything in this repo that exists to build, verify, or render: what it is,
how to run it, and what must be installed for the project to keep going.
Read this when starting from scratch or when the environment misbehaves.

## tools/ — C test harnesses and ffmpeg wrappers

### `f0r_host` (`f0r_host.c`)
Minimal frei0r host: dlopens a plugin dylib and exercises the `f0r_*` ABI
directly, no ffmpeg. Modes: `apply | identity | roundtrip | determinism`.

    tools/f0r_host build/libretrofx_dotportal.dylib apply 64 64

- 64×64 RGBA gradient test frame.
- Per-plugin test vectors (off-values + in-domain roundtrip values) live in
  the source, kept in sync with `frei0r-adapter/retrofx.c`.
- Tests by the **reported plugin name** — a mis-named dylib silently tests the
  wrong effect.
- `RETROFX_BACKEND=cpu|metal|dual`; `dual` runs CPU and Metal and
  byte-compares (MISMATCH → stderr). Acceptance bar: `dual` byte-identical.

### `sprcmp` (`sprcmp.c`)
Byte-compares CPU vs GPU **sprite dumps** (gather level) for dotgate —
parity-debug tool, reports which cells/fields differ in hex bits.

    tools/sprcmp build/libretrofx_dotgate.dylib [w h frame]

### `checkmask` (`checkmask.c`)
Measures shadow-mask distribution on a flat white PPM: scans bands of
3×pitch columns, reports the pure-white fraction per band; spread ≤ 2 pts =
pass (exit 0/1).

    tools/checkmask testimgs/flat_white.ppm <pitch>

### `mktestimg.c`
Generates `bars.ppm` / `flat_white.ppm` / `gradient.ppm` into an outdir
(= `make testimgs`; copies already in `testimgs/`).

### `fx.sh`
Named-param ffmpeg wrapper: expands `dotgate size=28 speed=8` into the
positional frei0r form, sets `FREI0R_PATH` / `RETROFX_BACKEND` (default
`metal`), and passes encoder args through.

### `ffmpeg_test.sh`
End-to-end acceptance test: renders a test clip through each
`build/libretrofx_*.dylib` (or an explicit list of effect names) and verifies
ffmpeg exits 0 and output frames differ from input.
Note: it expects `inputvideos/wbl.mov`, which is **not** currently in the tree —
the test clips present are `model.mp4`, `test-short.mov`, `test-short.mp4`.
`FFMPEG` env (default `/opt/homebrew/bin/ffmpeg`) must be a frei0r-enabled
build (Homebrew: `brew install ffmpeg-full`).

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

## run-scripts/ — approved-look render recipes (not test tools)
Each `cd`s to the repo root, renders at 4K through the effect, scales to 720p;
portal renders use `yuv444p`.

- `vintagestack.sh` — the classic stack
- `dotgate_final.sh`, `dotgate_720.sh` — approved dot.gate looks
- `dotportal.sh` — `size|fill|gate|halftone|relief|level|glow|color` = `24|55|5|70|50|200|60|360`
- `dotportal_coarse.sh` — `48|55|5|90|75|200|60|360`
- `model-spacengrave.sh` — the approved dot.spacengrave engraved line
  screen on model.mp4 (720×1280 native, size 5, gate 5, level 200, dim 100)
- `testshort-spacengrave.sh` — same look at 4K (size 15) → 1080p
- `model-dotportal-white.sh`, `model-3panel.sh` — model.mp4 portal render
  + the 3-panel comparison compositor

## Verification assets
- `examples/new/beforematrix.png` + `aftermatrix.jpg` — sample stills
  for visual comparisons.
- `testimgs/{bars,flat_white,gradient}.ppm` — generated test frames
  (see `mktestimg`).

## Environment — what must be installed to keep going
- **macOS + Xcode Command Line Tools** (Apple clang). C99,
  `-Wall -Wextra -Werror`; allowed deps: stdlib + Metal/Foundation only.
- **ffmpeg with the frei0r filter** at `/opt/homebrew/bin/ffmpeg`
  (ffmpeg-full build). Plain Homebrew ffmpeg may lack the frei0r filter.
- **Python venv** at `/Volumes/external/code/.venv` with **numpy** and
  **pillow** — used for numerical image diagnosis (measuring halos, coverage,
  dot brightness — do it at 4K against the 4K subject mask; 720p
  measurements lie). **scipy is NOT installed** — use ring-dilation distance
  instead of distance transforms.
- Note: `make` hangs on this volume — build with direct `cc` invocations
  (recipe below); the Makefile targets are the reference.

## Rebuild
Makefile targets (reference):

- `make frei0r` — all 14 dylibs into `build/` + `tools/f0r_host`
- `make inputvideos` — the mktestimg PPMs
- `make clean`

Direct `cc` fallback (proven; one dylib per effect via `-DRETROFX_EFFECT`):

    cc -std=c99 -O2 -Wall -Wextra -Werror -Icore -Ireference/frei0r/include \
       -DRETROFX_EFFECT=RETROFX_EFFECT_DOTPORTAL -dynamiclib \
       -o build/libretrofx_dotportal.dylib \
       frei0r-adapter/retrofx.c core/crt.c core/vhs.c core/dot.c core/metal_fx.m \
       -lm -framework Metal -framework Foundation
    ln -sf libretrofx_dotportal.dylib build/libretrofx_dotportal.so

(vid.* non-glitch effects: drop `core/dot.c`, `core/metal_fx.m`, and the
Metal frameworks. vid.glitch adds `core/glitch.c`.)

## Verification one-liners

    # narrowest check after building a plugin:
    tools/f0r_host build/libretrofx_<fx>.dylib apply 64 64
    # GPU parity (acceptance bar: byte-identical):
    RETROFX_BACKEND=dual tools/f0r_host build/libretrofx_<fx>.dylib apply 1280 720
