# frei0r plugin method (ARCHIVED)

The original shipping path for the 14 effects: one frei0r plugin dylib per
effect, driven through ffmpeg's `frei0r` filter. Archived 2026-09-13 in favor
of **dotpipe** — the raw-pipe host at the repo root. Everything here still
builds and runs; the C cores in `core/` are shared with the active method and
unchanged.

## Layout

| Path | Contents |
|---|---|
| `adapter/retrofx.c` | one dylib per effect via `-DRETROFX_EFFECT`; backend dispatch for the 4 Metal effects |
| `Makefile` | build targets (14 dylibs + `f0r_host`) |
| `build/` | the 14 dylibs (gitignored) |
| `f0r_host.c` / `f0r_host` | minimal frei0r host: dlopens a dylib, exercises the `f0r_*` ABI without ffmpeg |
| `ffmpeg_test.sh` | end-to-end smoke render through each plugin |
| `fx.sh` | named-param ffmpeg wrapper (`dotgate size=28 speed=8 gate=10`) |
| `run-scripts/` | the 6 approved-look render recipes |
| `run-scripts/storage/` | parked older recipes |
| `reference/frei0r` | upstream frei0r SDK clone (headers; gitignored) |

## Requirements

- Homebrew **`ffmpeg-full`** — plain ffmpeg may lack the `frei0r` filter:
  `brew install ffmpeg-full`.
- Xcode command-line tools for the 4 Metal effects.

## Build

`make` hangs on this volume — build single targets:

```sh
make -f archive/frei0r/Makefile archive/frei0r/build/libretrofx_dotgate.dylib
make -f archive/frei0r/Makefile archive/frei0r/f0r_host
```

ffmpeg on macOS loads frei0r modules as `<name>.so`, so symlink once:

```sh
cd archive/frei0r/build && for f in libretrofx_*.dylib; do ln -sfn "$f" "${f%.dylib}.so"; done
```

## Use

`FREI0R_PATH` must be an **absolute** path. Parameters are positional, in the
order of *Effect parameters* below, `|`-separated:

```sh
export FREI0R_PATH="$PWD/archive/frei0r/build"
ffmpeg -hide_banner -loglevel error -y -i input.mov \
  -vf "frei0r=libretrofx_dotgate:28|8|10" \
  out.mp4
```

Stack effects by repeating the filter (applied left to right).

### Named parameters (`fx.sh`)

`fx.sh` expands named params into the positional form and sets
`FREI0R_PATH` / `RETROFX_BACKEND` (default `metal`; `-b cpu|metal|dual`
switches); words before the first stage pass through to ffmpeg.

```sh
archive/frei0r/fx.sh -i input.mov -o out.mp4 \
  dotgate size=28 speed=8 gate=10 \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p
```

### Effect parameters (positional order, then defaults)

| Plugin | Parameters |
|---|---|
| `libretrofx_vid_shadowmask` | `type` (0 grille, 1 slot, 2 triad) = `0`, <br>`intensity` 0–100 (0 = off) = `50`, <br>`pitch` px = `3` |
| `libretrofx_vid_scanlines` | `intensity` 0–100 (0 = off) = `50`, <br>`period` rows ≥ 2 = `3`, <br>`offset` rows = `0` |
| `libretrofx_vid_chromablood` | `amount` 0–100 (0 = off) = `50` |
| `libretrofx_vid_bloom` | `amount` 0–100 (0 = off) = `50`, <br>`threshold` 0–1 = `0.10`, <br>`radius` px = `16` |
| `libretrofx_vid_barrel` | `amount` 0–100 (0 = off) = `50`, <br>`zoom` 0–100 (0 = off) = `0` |
| `libretrofx_vid_vignette` | `amount` 0–100 (0 = off) = `50` |
| `libretrofx_vid_overscan` | `radius` px = `48`, <br>`margin` px = `8` |
| `libretrofx_vid_lumar` | `amount` 0–100 (0 = off) = `50`, <br>`wavelength` px ≥ 2 = `4` |
| `libretrofx_vid_rainbow` | `amount` 0–100 (0 = off) = `50`, <br>`period_y` rows ≥ 2 = `48`, <br>`period_t` frames ≥ 2 = `16` |
| `libretrofx_vid_tapewow` | `amplitude` px (0 = off) = `50`, <br>`period` frames ≥ 2 = `90` |
| `libretrofx_vid_glitch` | `amount` burst frequency 0–100 (0 = off) = `30`, <br>`rgb` split strength 0–100 (0 = off) = `4`, <br>`noise` 0–100 (0 = off) = `8`, <br>`bands` tear strength 0–100 (0 = off) = `45`, <br>`blocks` tile strength 0–100 (0 = off) = `0`, <br>`scan` 0–100 (0 = off) = `0`, <br>`quant` 0–100 (0 = off) = `0`, <br>`vtear` tear strength 0–100 (0 = off) = `28`, <br>`seed` 0–999 = `3` |
| `libretrofx_dotgate` | `size` dot pitch px (0 = off) = `24`, <br>`speed` wobble px = `6`, <br>`gate` subject-knee ×100 (the fill knob; dark/smooth material needs 1) = `10` |
| `libretrofx_dotportal` | `size` dot pitch px (0 = off) = `24`, <br>`fill` dot radius as % of step = `55`, <br>`gate` subject-knee ×100 = `5`, <br>`halftone` radius tracks tone 0–100 = `70`, <br>`relief` gradient displacement as % of step = `50`, <br>`level` brightness ×100 (100 = 1×) = `200`, <br>`glow` aura 0–100 = `60`, <br>`color` hue 0–360 (360 = white) = `360` |
| `libretrofx_dot_spacengrave` | `size` scanline pitch px (0 = off) = `5`, <br>`fill` stroke width as % of pitch = `45`, <br>`halftone` ink follows tone 0–100 = `60`, <br>`line` baseline coverage 0–100 = `75`, <br>`level` ink brightness ×100 (100 = 1×) = `100`, <br>`glow` texture break-up 0–100 = `65`, <br>`color` hue 0–360 (360 = white) = `360`, <br>`dim` source dimming 0–100 (100 = black behind) = `100`, <br>`gate` subject-knee ×100 (0 = full frame) = `30` |

The `dot.*` and `vid.glitch` dials take the same values in dotpipe
(`--dotgate`, `--dotportal`, `--spacengrave`, `--glitch`).

### Render scripts

| Script | What it renders |
|---|---|
| `run-scripts/model-vintage.sh` / `jimbots-vintage.sh` | the full 9-effect `vid.*` CRT/VHS stack at locked levels |
| `run-scripts/model-gamma-spacengrave-metal.sh` | the approved `dot.spacengrave` recipe (gamma shadow lift, 720×1280 native) |
| `run-scripts/model-compare-3pane.sh` / `jimbots-compare-3pane.sh` | three-pane hstacked comparison |

Each `cd`s to the repo root; `[-i input] [-o output]` override defaults.

## Compute backend (`dot.gate` / `dot.portal` / `dot.spacengrave` / `vid.glitch`)

`RETROFX_BACKEND=cpu` (default) | `metal` | `dual` (runs both, byte-compares
every frame — a MISMATCH means a parity regression).

## Verification

```sh
archive/frei0r/f0r_host archive/frei0r/build/libretrofx_dotgate.dylib apply 64 64
RETROFX_BACKEND=dual archive/frei0r/f0r_host archive/frei0r/build/libretrofx_dotgate.dylib apply 1280 720
```

## Performance

**`PERFORMANCE.md`** next to this file — per-effect CPU-vs-Metal pipeline
numbers (the cores are shared, so they apply to dotpipe too) and the frei0r
end-to-end script timings.
