# dot.fx

Retro video effects for **FFmpeg**: 14 independent effect cores — 11 `vid.*`
CRT/VHS filters (including the stackable `vid.glitch`) plus the `dot.*`
family — `dot.gate` (subject-gated dot map), `dot.portal` (the subject as
separated, floating dots on black), `dot.spacengrave` (the subject as an
engraved line screen on black). Four — `dot.gate`, `dot.portal`,
`dot.spacengrave`, `vid.glitch` — also run on the GPU via Metal; the CPU
core is the reference and the byte-identical fallback when Metal is
unavailable.

Shipped via **dotpipe**, a raw-pipe effect host: ffmpeg decodes and encodes,
raw frames pipe through the effects binary — works with **any** ffmpeg
build.

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

- Any Homebrew ffmpeg (codec only — the effects never touch ffmpeg):
  `brew install ffmpeg`.
- macOS with Xcode command-line tools (`cc`, Metal framework) for the GPU
  effects; the CPU cores are plain C and build elsewhere.

## Build

```sh
make        # → dotpipe/dotpipe
```

## Use

ffmpeg decodes to raw RGB24, dotpipe applies the effects in the order given,
ffmpeg encodes:

```sh
ffmpeg -hide_banner -loglevel error -i input.mov -f rawvideo -pix_fmt rgb24 - \
  | RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 1920 -h 1080 \
      --spacengrave 5 85 80 68 200 78 360 100 5 \
  | ffmpeg -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 -s 1920x1080 -i - out.mp4
```

Stack effects by listing more flags, applied left to right:

```sh
./dotpipe/dotpipe -w 1920 -h 1080 --scanlines 0.65 3 0 --dotgate 28 8 10
```

No flags = byte-identical passthrough; `dual` cross-checks Metal vs CPU
per frame and exits loud on mismatch.

### Render scripts

Approved looks, each runnable from anywhere (`[-i input] [-o output]`
override the defaults; 3pane also `[-w 720] [-h 1280]`):

| Script | What it renders |
|---|---|
| `run-scripts-dotpipe/model-vintage.sh` / `jimbots-vintage.sh` | the full 9-effect `vid.*` CRT/VHS stack at locked levels |
| `run-scripts-dotpipe/model-gamma-spacengrave.sh` / `jimbots-gamma-spacengrave.sh` | the approved `dot.spacengrave` recipe — gamma shadow lift, native resolution, effect at `5 85 80 68 200 78 360 100 5` (720×1280 is under the 2048-row Metal bound, so this one genuinely runs on the GPU) |
| `run-scripts-dotpipe/model-compare-3pane.sh` / `jimbots-compare-3pane.sh` | three-pane hstacked comparison of the same source |

### Effect parameters (positional order)

`--glitch` and the `dot.*` flags take 0–100 dials (0 = off, `50` = default
look, `100` = strong; `tapewow` runs on a 24 fps clock). The `vid.*` flags
take raw core values (0–1 strengths, px, frames) — the scripts above are the
source of truth for approved levels.

| Effect | Flag | Parameters (positional order) |
|---|---|---|
| `vid.shadowmask` | `--mask` | `grille\|slot\|triad`, <br>intensity 0–1, <br>pitch px |
| `vid.scanlines` | `--scanlines` | intensity 0–1, <br>period rows ≥ 2, <br>offset rows |
| `vid.chromablood` | `--bleed` | radius px, <br>amount (0 = off) |
| `vid.bloom` | `--bloom` | amount (0 = off), <br>threshold 0–1, <br>radius px |
| `vid.barrel` | `--barrel` | amount 0–1 (0 = off), <br>zoom 0–1 (0 = off) |
| `vid.vignette` | `--vignette` | amount 0–1 (0 = off) |
| `vid.overscan` | `--overscan` | radius px, <br>margin px |
| `vid.lumar` | `--lumar` | amount 0–1 (0 = off), <br>wavelength px ≥ 2 |
| `vid.rainbow` | `--rainbow` | amount 0–1 (0 = off), <br>period_y rows ≥ 2, <br>period_t frames ≥ 2 |
| `vid.tapewow` | `--wow` | amplitude px (0 = off), <br>period frames ≥ 2 |
| `vid.glitch` | `--glitch` | `amount` burst frequency 0–100 (0 = off), <br>`rgb` split strength 0–100 (0 = off), <br>`noise` 0–100 (0 = off), <br>`bands` tear strength 0–100 (0 = off), <br>`blocks` tile strength 0–100 (0 = off), <br>`scan` 0–100 (0 = off), <br>`quant` 0–100 (0 = off), <br>`vtear` tear strength 0–100 (0 = off), <br>`seed` 0–999 |
| `dot.gate` | `--dotgate` | `size` dot pitch px (0 = off), <br>`speed` wobble px, <br>`gate` subject-knee ×100 (the fill knob; dark/smooth material needs 1) |
| `dot.portal` | `--dotportal` | `size` dot pitch px (0 = off), <br>`fill` dot radius as % of step, <br>`gate` subject-knee ×100, <br>`halftone` radius tracks tone 0–100, <br>`relief` gradient displacement as % of step, <br>`level` brightness ×100 (100 = 1×), <br>`glow` aura 0–100, <br>`color` hue 0–360 (360 = white) |
| `dot.spacengrave` | `--spacengrave` | `size` scanline pitch px (0 = off), <br>`fill` stroke width as % of pitch, <br>`halftone` ink follows tone 0–100, <br>`line` baseline coverage 0–100, <br>`level` ink brightness ×100 (100 = 1×), <br>`glow` texture break-up 0–100, <br>`color` hue 0–360 (360 = white), <br>`dim` source dimming 0–100 (100 = black behind), <br>`gate` subject-knee ×100 (0 = full frame) |

**How the effects actually work** — subject gating, what makes
`dot.portal` read as particles, the engraving model, glitch bursts, and
stack order: **`videoprocessdetails.md`**.

## Compute backend (`dot.gate` / `dot.portal` / `dot.spacengrave` / `vid.glitch`)

Select with the `RETROFX_BACKEND` environment variable (the other `vid.*`
effects are CPU-only for now):

| Value | Behavior |
|---|---|
| `cpu` (default) | CPU reference implementation |
| `metal` | Metal GPU (Apple silicon) |
| `dual` | runs both, byte-compares every frame; a MISMATCH means a parity regression (either output is still usable) |

```sh
RETROFX_BACKEND=metal sh run-scripts-dotpipe/model-gamma-spacengrave.sh
```

Measured CPU-vs-Metal performance on the `inputvideos/` videos:
**`PERFORMANCE.md`**.

## Verification

The acceptance bar for the GPU effects is `dual` byte-identity:

```sh
RETROFX_BACKEND=dual ./dotpipe/dotpipe -w 1280 -h 720 --dotgate 28 8 10 < in.raw > out.raw
```

(`dual` reports per-frame cross-check counts on stderr and exits non-zero on
mismatch.)

`tools/` (harness, parity tooling, render checks) is documented in
**`tools/TOOLS.md`**.

The original frei0r plugin method is preserved in **`archive/frei0r/`**
(README, PERFORMANCE, scripts) — it still builds and runs, but dotpipe is
the way.

## Troubleshooting

- **`retrofx: Metal unavailable/failed`** — the process runs in a sandbox;
  `MTLCreateSystemDefaultDevice()` returns nil there. Run unsandboxed.
- **`dual` backend reports MISMATCH** — a CPU/GPU parity regression; the
  `cpu` and `metal` outputs are each still valid on their own.
