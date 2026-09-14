# dot.fx

Retro video effects for **FFmpeg**, shipped as 14 independent frei0r
plugins: 11 `vid.*` CRT/VHS filters (including the stackable
`vid.glitch`), plus the `dot.*` family — `dot.gate` (subject-gated dot
map), `dot.portal` (the subject as separated, floating dots on black)
and `dot.spacengrave` (the subject as an engraved line screen on
black). Four — `dot.gate`, `dot.portal`, `dot.spacengrave`,
`vid.glitch` — also run on the GPU via Metal; the CPU core is the
reference and is the byte-identical fallback when Metal is unavailable.

| Effect | What it does |
|---|---|
| `vid.shadowmask` | RGB subpixel mask: aperture grille, slot mask, or dot triad |
| `vid.scanlines` | Scanline darkening over a cosine row profile |
| `vid.chromablood` | Asymmetric RGB fringe on vertical edges |
| `vid.bloom` | Luma-gated phosphor glow over highlights |
| `vid.barrel` | Curved CRT screen: convex-glass remap, corners recede; optional zoom fills the curved margin |
| `vid.vignette` | Parabolic corner falloff |
| `vid.overscan` | Rounded-bezel cutout of frame border and corners |
| `vid.lumar` | Luma ringing around high-contrast edges |
| `vid.rainbow` | Rainbow phase (dot crawl): crawling asymmetric fringe on edges |
| `vid.tapewow` | Slow vertical whole-picture drift (tape speed wobble) |
| `vid.glitch` | Pro glitch on any frame: burst-gated slice tears, block tiles, scanline jitter, RGB split, posterize, v-sync tear, static — each element individually switchable, composable after any effect (GPU) |
| `dot.gate` | Subject-gated dot map: the frame's subject becomes dots on black, slow per-dot wobble (GPU) |
| `dot.portal` | The subject as separated dots on black: tone-sized (halftone), gradient-displaced (relief), hue or white, optional glow; hard Otsu-gated silhouette (GPU) |
| `dot.spacengrave` | Engraved line screen ("president on a dollar bill"): the photo is removed to black, the subject re-emitted as scanlines that break into dashes over texture (GPU) |

## Requirements

- FFmpeg with frei0r support — the Homebrew **`ffmpeg-full`** formula:
  `brew install ffmpeg-full`.
- macOS with Xcode command-line tools (`cc`, Metal framework) for the
  GPU effects; the CPU core is plain C and builds elsewhere.

## Build

```sh
make frei0r        # → build/libretrofx_<effect>.dylib  (14 plugins: vid.* + dot.*)
```

FFmpeg on macOS loads frei0r modules as `<name>.so`, so create the
matching symlinks once:

```sh
cd build && for f in libretrofx_*.dylib; do ln -sfn "$f" "${f%.dylib}.so"; done
```

## Use with FFmpeg

`FREI0R_PATH` must be an **absolute** path to `build/`. Parameters are
positional, in the order of *Effect parameters* below, `|`-separated
(this ffmpeg's frei0r wrapper takes `|`, not `,`):

```sh
export FREI0R_PATH="/path/to/dot.fx/build"

ffmpeg -hide_banner -loglevel error -y -i input.mov \
  -vf "frei0r=libretrofx_dotgate:28|8|10" \
  outputvideos/dotgate_final.mp4
```

Stack multiple effects by repeating the filter (applied left to right):

```sh
-vf "frei0r=libretrofx_vid_scanlines:65|3|0,frei0r=libretrofx_dotgate:28|8|10"
```

### Render scripts

Approved looks, each runnable from anywhere (`[-i input] [-o output]`
override the defaults):

| Script | What it renders |
|---|---|
| `run-scripts-frei0r/model-vintage.sh` / `jimbots-vintage.sh` | the full 9-effect `vid.*` CRT/VHS stack at locked levels (portrait subject / 1080p) |
| `run-scripts-dotpipe/model-vintage.sh` / `jimbots-vintage.sh` | that same stack via `dotpipe` (the raw-pipe host below) — no frei0r needed |
| `run-scripts-frei0r/model-gamma-spacengrave-metal.sh` | the approved `dot.spacengrave` recipe — gamma shadow lift, native resolution, effect at `5\|85\|80\|68\|200\|78\|360\|100\|5` (720×1280 is under the 2048-row Metal bound, so this one genuinely runs on the GPU) |
| `run-scripts-dotpipe/model-gamma-spacengrave.sh` / `jimbots-gamma-spacengrave.sh` | that same approved look via `dotpipe` instead of frei0r |
| `run-scripts-frei0r/model-compare-3pane.sh` / `jimbots-compare-3pane.sh` | three-pane hstacked comparison of the same source |
| `run-scripts-dotpipe/model-compare-3pane.sh` / `jimbots-compare-3pane.sh` | that same three-pane comparison via `dotpipe` |

`run-scripts-frei0r/storage/` holds the parked approved recipes
(`dotgate_final`, `dotportal`, `vintagestack`, 4K/testshort variants) —
they work, but their `-metal` companions and outputs are from the
previous input set.

### Named parameters (`tools/fx.sh`)

`fx.sh` expands named params into the positional form and sets
`FREI0R_PATH` / `RETROFX_BACKEND` (default `metal`; `-b cpu|metal|dual`
switches):

```sh
tools/fx.sh -i input.mov -o outputvideos/dotgate_final.mp4 \
  dotgate size=28 speed=8 gate=10 \
  -c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p
```

Bare numbers work positionally (`dotgate 28 8 10`); omitted params take
the defaults below; words before the first stage pass through to ffmpeg.

### dotpipe (raw-pipe host — no frei0r needed)

`dotpipe/dotpipe` is a standalone effect engine: ffmpeg decodes/encodes,
raw RGB24 frames pipe through the binary. Same cores — and the same
Metal path (`RETROFX_BACKEND=cpu|metal|dual`) — so it works with **any**
ffmpeg build, even one without the frei0r filter:

```sh
ffmpeg -hide_banner -loglevel error -i input.mov -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 1920 -h 1080 \
      --spacengrave 5 85 80 68 200 78 360 100 5 \
  | ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 -s 1920x1080 -i - out.mp4
```

No flags = byte-identical passthrough; `dual` cross-checks Metal vs CPU
per frame and exits loud on mismatch. `run-scripts-dotpipe/model-gamma-spacengrave.sh`
is the approved look end-to-end (a Metal CPU-fallback warning is a hard
error there).

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

`0` on a strength dial = off (byte-identical passthrough), `50` =
default look, `100` = strong; pixel/geometry dials take px and frames
(`tapewow.period` runs on a 24 fps clock, so `90` = a 3.75 s drift
cycle).

**How the effects actually work** — subject gating, what makes
`dot.portal` read as particles, the engraving model, glitch bursts,
and stack order: **`videoprocessdetails.md`**.

## Compute backend (`dot.gate` / `dot.portal` / `dot.spacengrave` / `vid.glitch`)

Select with the `RETROFX_BACKEND` environment variable (the other `vid.*`
effects are CPU-only for now — see `plan/vid-update.md` Phase 2):

| Value | Behavior |
|---|---|
| `cpu` (default) | CPU reference implementation |
| `metal` | Metal GPU (Apple silicon) |
| `dual` | runs both, byte-compares every frame; a MISMATCH means a parity regression (either output is still usable) |

```sh
RETROFX_BACKEND=metal sh run-scripts-frei0r/model-gamma-spacengrave-metal.sh
```

Measured CPU-vs-Metal performance on the `inputvideos/` videos:
**`PERFORMANCE.md`**.

## Verification

The acceptance bar for the GPU effects is `dual` byte-identity:

```sh
RETROFX_BACKEND=dual tools/f0r_host build/libretrofx_dotgate.dylib apply 64 64
```

`tools/` (harness, parity tooling, render checks) is documented in
**`tools/TOOLS.md`**.

## Troubleshooting

- **`Could not find module libretrofx_<fx>`** — `FREI0R_PATH` is not
  absolute, or the `build/*.so` symlinks are missing (see Build).
- **`retrofx: Metal unavailable/failed`** — the process runs in a
  sandbox; `MTLCreateSystemDefaultDevice()` returns nil there. Run
  unsandboxed.
- **`dual` backend reports MISMATCH** — a CPU/GPU parity regression; the
  `cpu` and `metal` outputs are each still valid on their own.
