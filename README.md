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

| Original | `vid.*` vintage stack | `dot.spacengrave` |
|---|---|---|
| [![model original](docs/screenshots/model-original.jpg)](docs/screenshots/full-model-original.jpg) | [![model vintage](docs/screenshots/model-vintage.jpg)](docs/screenshots/full-model-vintage.jpg) | [![model dot.spacengrave](docs/screenshots/model-spacengrave.jpg)](docs/screenshots/full-model-spacengrave.jpg) |

*Same `model.mp4` frame — original, then the two flagship looks.*

| Effect | What it does |
|---|---|
| `vid.shadowmask` | makes the picture out of fine vertical stripes, slots, or dot triads — like a real TV screen |
| `vid.scanlines` | darkens every other line — the classic scanline look |
| `vid.chromablood` | red/blue color bleed on vertical edges, like VHS chroma smear |
| `vid.bloom` | soft glow around the bright parts of the frame |
| `vid.barrel` | bends the picture outward like convex glass; corners recede |
| `vid.vignette` | darkens the four corners, center stays clean |
| `vid.overscan` | clips the border and corners, like a TV bezel |
| `vid.lumar` | a ringing halo around hard edges |
| `vid.rainbow` | a crawling color fringe walks the edges, like slightly out-of-sync VHS |
| `vid.tapewow` | the whole picture drifts slowly up and down, like flaky tape |
| `vid.glitch` | glitches in bursts: slice tears, block tiles, RGB split, static, v-sync tear — each piece switchable, works after any effect (GPU) |
| `dot.gate` | the subject becomes a field of dots on black that wobble slowly (GPU) |
| `dot.portal` | the subject becomes separated, floating dots on black — sized by tone, bent by the form (GPU) |
| `dot.spacengrave` | the photo drops to black; the subject is re-emitted as an engraved line screen — like the president on a dollar bill (GPU) |

## Requirements

- Homebrew `ffmpeg-full` (codec only — the effects never touch ffmpeg):
  `brew install ffmpeg-full`.
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

Scripts for the example outputs in `outputvideos/` (each runnable from
anywhere; `[-i input] [-o output]` overrides the defaults):

| Thumbnail | Script | Output |
|---|---|---|
| ![spacengrave](docs/screenshots/thumb-spacengrave.jpg) | `run-scripts-dotpipe/model-gamma-spacengrave-metal.sh` | `outputvideos/model-gamma-spacengrave-dotpipe.mp4` |
| ![vintage](docs/screenshots/thumb-vintage.jpg) | `run-scripts-dotpipe/model-vintage-cpu.sh` | `outputvideos/model-vintage-dotpipe.mp4` |

*What each one does:* **spacengrave** erases the photo to pure black and re-emits only the subject as a screen of engraved white lines that break into dashes over texture — the "president on a dollar bill" read. **vintage** keeps the full picture and drives it through the 9-effect CRT/VHS stack — phosphor glow, RGB edge fringing, dot-crawl color phasing, barrel distortion, vignette and slow tape drift — so it reads as a broadcast recording rather than a transform.

`dot.portal` (`model-gamma-dotportal-metal.sh`, T look `18 70 5 100 100 800 100
360 360 100`) renders `outputvideos/model-gamma-dotportal-dotpipe.mp4` — parked
out of this table for now.

Other approved looks (vapor, invert variants, comparisons) have scripts in
`run-scripts-dotpipe/` with the same conventions; `-metal` = runs on the GPU
(a Metal fallback warning fails the render), `-cpu` = CPU-only effects.

### Effect parameters (positional order)

`--glitch` and the `dot.*` flags take 0–100 dials (0 = off, `50` = default
look, `100` = strong; `tapewow` runs on a 24 fps clock). The `vid.*` flags
take raw core values (0–1 strengths, px, frames) — the scripts above are the
source of truth for approved levels.

| Thumbnail | Effect | Flag | Parameters (positional order) |
|---|---|---|---|
| [![shadowmask](docs/screenshots/thumb-shadowmask.jpg)](docs/screenshots/full-shadowmask.jpg) | `vid.shadowmask` | `--mask` | picks the subpixel mask pattern: `grille` (vertical aperture-grille stripes), `slot` (mask + slot) or `triad` (per-subpixel dots) — <br>`intensity` how hard the stripes/dark areas crush the image, <br>`pitch` stripe period in px |
| [![scanlines](docs/screenshots/thumb-scanlines.jpg)](docs/screenshots/full-scanlines.jpg) | `vid.scanlines` | `--scanlines` | darkens every other scanline on a smooth cosine row profile, like a CRT mask in front of the picture — <br>`intensity` depth of the dark lines, <br>`period` rows per line+gap, <br>`offset` shifts the pattern vertically |
| [![chromablood](docs/screenshots/thumb-chromablood.jpg)](docs/screenshots/full-chromablood.jpg) | `vid.chromablood` | `--bleed` | fringes vertical color edges with asymmetric red/blue bleed, the classic VHS chroma smear — <br>`radius` how far the color bleeds in px, <br>`amount` strength (0 = off) |
| [![bloom](docs/screenshots/thumb-bloom.jpg)](docs/screenshots/full-bloom.jpg) | `vid.bloom` | `--bloom` | adds a soft phosphor glow around the bright parts of the frame — <br>`amount` glow strength (0 = off), <br>`threshold` how bright a pixel must be to bloom (0–1), <br>`radius` glow spread in px |
| [![barrel](docs/screenshots/thumb-barrel.jpg)](docs/screenshots/full-barrel.jpg) | `vid.barrel` | `--barrel` | curves the picture outward like a convex CRT glass — edges recede and stretch — <br>`amount` curvature strength (0 = off), <br>`zoom` magnifies slightly so the image still fills the curved margin (0 = off) |
| [![vignette](docs/screenshots/thumb-vignette.jpg)](docs/screenshots/full-vignette.jpg) | `vid.vignette` | `--vignette` | a parabolic falloff that darkens the four corners while leaving the center clean — <br>`amount` corner crush (0 = off) |
| [![overscan](docs/screenshots/thumb-overscan.jpg)](docs/screenshots/full-overscan.jpg) | `vid.overscan` | `--overscan` | simulates the TV displaying a hair more than the frame: the border and corners are clipped by a rounded "bezel" cut — <br>`radius` corner roundness px, <br>`margin` how far the crop cuts in px |
| [![lumar](docs/screenshots/thumb-lumar.jpg)](docs/screenshots/full-lumar.jpg) | `vid.lumar` | `--lumar` | rings high-contrast edges with a luma halo, like an old decoder ringing on hard edges — <br>`amount` halo strength (0 = off), <br>`wavelength` spacing of the ring oscillation px |
| [![rainbow](docs/screenshots/thumb-rainbow.jpg)](docs/screenshots/full-rainbow.jpg) | `vid.rainbow` | `--rainbow` | dot-crawl color phasing: a crawling asymmetric fringe walks the color edges frame by frame, like a slightly out-of-sync VHS — <br>`amount` strength (0 = off), <br>`period_y` rows per phasing beat, <br>`period_t` frames per beat |
| [![tapewow](docs/screenshots/thumb-tapewow.jpg)](docs/screenshots/full-tapewow.jpg) | `vid.tapewow` | `--wow` | slow vertical whole-picture drift, the picture breathing up and down like flaky tape speed — <br>`amplitude` drift in px (0 = off), <br>`period` frames per full wobble |
| [![glitch](docs/screenshots/thumb-glitch.jpg)](docs/screenshots/full-glitch.jpg) | `vid.glitch` | `--glitch` | pro glitch bursts: a random burst clock turns effect groups on for a few frames at a time, so the frame glitches in episodes rather than constantly — <br>`amount` burst frequency 0–100 (0 = off, gates everything below), <br>`rgb` RGB split strength on torn edges, <br>`noise` static injected into bursts, <br>`bands` slice-tear strength (horizontal slices shifted), <br>`blocks` tile displacement strength, <br>`scan` scanline jitter, <br>`quant` posterize to banded colors, <br>`vtear` vertical tear strength, <br>`seed` randomization seed 0–999 (change it for a different pattern) |
| [![dotgate](docs/screenshots/thumb-dotgate.jpg)](docs/screenshots/full-dotgate.jpg) | `dot.gate` | `--dotgate` | maps the frame's subject into a dot field on black — <br>`size` dot pitch px, the grid density (0 = off), <br>`speed` per-dot wobble in px — dots slowly breathe in and out of position, <br>`gate` subject knee ×100 — the fill knob: how prominent an area must be to earn dots (dark/smooth material needs 1) |
| [![dotportal](docs/screenshots/thumb-dotportal.jpg)](docs/screenshots/full-dotportal.jpg) | `dot.portal` | `--dotportal` | the subject becomes a body of separated, floating dots on black — <br>`size` dot pitch px (0 = off), <br>`fill` dot radius as % of step — the main brightness/weight lever, <br>`gate` subject knee ×100 (higher = only the strongest-form areas keep dots), <br>`halftone` dot size tracks the local tone 0–100 — makes the dot body follow the shading, <br>`relief` dots displace along the surface gradient as % of step — bends the field along the form and breaks the grid, <br>`level` brightness ×100 (100 = 1×; saturates around 200 — brighten with `fill`/`glow` instead), <br>`glow` aura 0–100 — halo that lights the gaps between dots, <br>`color` shadow hue 0–360 (360 = white), <br>`color2` highlight hue 0–360 (360 = white) — shade-blended: shadow → `color`, lit → `color2` (default = `color`, flat; note cyan reads dim — white or pale hues read bright), <br>`crisp` silhouette sharpening 0–100 — high-contrast dots shrink so the dot body hugs the outline (0 = off) |
| [![spacengrave](docs/screenshots/thumb-spacengrave.jpg)](docs/screenshots/full-spacengrave.jpg) | `dot.spacengrave` | `--spacengrave` | erases the photo to black and re-emits the subject as an engraved line screen — <br>`size` scanline pitch px (0 = off), <br>`fill` stroke width as % of pitch, <br>`halftone` ink follows the tone 0–100 — the engraved 3-D shading read, <br>`line` baseline line coverage 0–100 — how solid the base line is, <br>`level` ink brightness ×100 (100 = 1×), <br>`grain` texture break-up 0–100 — breaks the lines into dashes over texture (the engraving knob), <br>`color` ink hue 0–360 (360 = white), <br>`dim` source dimming 0–100 (100 = the photo goes fully black behind the ink), <br>`gate` subject knee ×100 (0 = full frame), <br>`texture` second dash layer at half pitch/width, baseline coverage 0–100 (0 = off), <br>`contour` tilt the stroke axis toward the local iso-luma contour 0–100 (0 = off = vertical) |
| [![vapor](docs/screenshots/thumb-vapor.jpg)](docs/screenshots/full-vapor.jpg) | vapor mode (stream-level, goes last) | `--vapor` | `slow` each frame written N times (≥1, half speed at 2), <br>`decay` trail persistence per output frame 0–100, <br>`faint` trail strength 0–100, <br>`streak` x-axis smear 0–100 (0 = in-place haze, >0 = comet tail along each row — the trail feeds only where ink advanced horizontally, so vertical motion leaves no trail), <br>`dir` tail direction 0–2 (0 = both directions, 1 = right-only, 2 = left-only) |

**How the effects actually work** — subject gating, what makes
`dot.portal` read as particles, the engraving model, glitch bursts, and
stack order: **`videoprocessdetails.md`**.

**Tuning these effects further** — approved recipes, knob lessons, workflow,
and known issues: **`HANDOFF.md`**.

## Compute backend (`dot.gate` / `dot.portal` / `dot.spacengrave` / `vid.glitch`)

Select with the `RETROFX_BACKEND` environment variable (the other `vid.*`
effects are CPU-only for now):

| Value | Behavior |
|---|---|
| `cpu` (default) | CPU reference implementation |
| `metal` | Metal GPU (Apple silicon) |
| `dual` | runs both, byte-compares every frame; a MISMATCH means a parity regression (either output is still usable) |

```sh
RETROFX_BACKEND=metal sh run-scripts-dotpipe/model-gamma-spacengrave-metal.sh
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
