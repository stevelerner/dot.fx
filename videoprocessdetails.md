# dot.fx — how the effects actually work

The README is the quick reference (what each effect does, the dials, the
commands). This file is the deep end: how the `dot.*` family finds the
subject, what makes `dot.portal` read as particles, how
`dot.spacengrave` engraves, how `vid.glitch` bursts, and why the
`vid.*` stack is ordered the way it is. Read it when you're tuning a
look and want to know *why* a dial does what it does.

## The `dot.*` family — subject gating

All three `dot.*` effects are **subject-gated**: output exists only
where *two* tests pass, per pixel, per frame:

1. **Local contrast reaches the `gate` knee.** The core probes luma
   gradients around the pixel (near ±step, wide ±3·step). Flat walls,
   sky, smooth skin interiors fail the test — nothing is emitted there.
   The `gate` dial is the knee, ×100: lower = more of the frame
   qualifies (`1` ≈ full subject field, `10` ≈ edge-only outline).
   Dark or smooth material needs `gate = 1`.
2. **The pixel is on the subject side of a per-frame Otsu luma split.**
   Each frame is split into a bright field and a dark minority (or
   vice-versa — the minority side is taken as the subject, so inverted
   footage works too), and only the subject side of that split can
   carry dots/lines.

Together the two tests make the dot field end *exactly* at the
silhouette — no outline, no spill — and drop flat backgrounds to black
(`dim`/removal). `size = 0` leaves the frame byte-identical for every
`dot.*` effect.

### `dot.gate` — the dot map

The subject becomes a field of dots on black. Dots are a cool green
ramp modulated by the source tone (lit areas read brighter), with a
slow per-dot wobble (`speed`; `0` = static). This is the "dot-map"
look: a texture field that tracks the subject, gateable faint for
overlays (the vintage-stack uses it at low blend factor over the
glass).

### `dot.portal` — the particle look

Same gate, different geometry. Three things make the dots read as
*separated particles* instead of a dot map:

- **Separated** — `fill` sets dot radius as a % of the pitch step.
  Gap-free coverage is `80`; the portal look sits well below it
  (default `55`), so every dot has black sky around it.
- **Tone-sized (halftone)** — lit surfaces fatten their dots, shadow
  shrinks them (`halftone`). The dot field therefore carries shading.
- **Displaced along the luma gradient (relief)** — each dot is pushed
  a little down its local gradient (`relief`, % of step), so the
  field bulges on the lit cheek and caves into shadow. *This* is the
  3D read.

Colour is white by default (`color` `360`) or any hue at fixed
saturation; `glow` adds a soft aura; `level` is the ink brightness
gain. The silhouette stays hard because the gate is binary **and**
region-bounded: a dot must pass the contrast knee *and* its center —
**after** relief displacement — must land on the subject side of the
Otsu split.

Two later additions build on the same shade/edge values `local_shade()`
already computes for the gate and relief:

- **Duotone (`color2`)** — instead of one flat hue, each dot blends from
  `color` in the shadow toward `color2` in the highlight, weighted by the
  same per-cell `shade` that already drives relief. `color2` defaults to
  `color` (flat hue, byte-identical to the pre-duotone look). Tuning note:
  cyan reads perceptually *dark* as a highlight hue and was a hidden
  dimmer in early passes — white or a pale hue reads bright instead.
- **Silhouette-sharpening (`crisp`)** — blends `halftone`'s pure-tone sizing
  with local gradient magnitude (the same `edge` the subject gate reads),
  so high-contrast cells shrink further than tone alone would size them.
  Combined with `relief`, this is what breaks the look out of a regular
  grid: dots bend along the surface *and* shrink at the outline instead of
  reading as a lattice with a shape behind it. `0` = off, reproduces the
  pre-`crisp` sizing exactly.

### `dot.spacengrave` — the engraving

The inverse construction: instead of dots, the subject is re-emitted
as **scanlines** that behave like engraved ink:

- `dim` removes the source photo to black behind the ink (approved
  recipe: `100` = pure black).
- **Solid lines over lit flat areas** — `line` is the baseline coverage,
  `fill` the stroke width as % of pitch, `halftone` lets ink darkness
  follow tone (engraved shading), `level` the ink brightness.
- **Break-up over texture** — where the source has texture the lines
  fracture into irregular dashes; `glow` (core name: `grain`) is the
  break-up amount. *Break-up + line shading are what carry the 3D* —
  the same role halftone/relief play in portal: the pattern IS the
  subject.

Two later additions:

- **A second dash layer (`texture`)** — an independent higher-frequency
  dash pass at half the pitch and half the stroke width, composited on top
  of the primary stroke before the 255 clamp. Pure texture break-up, no
  change to the primary stroke's geometry or dither. `0` = off, byte-identical
  to the single-layer engraving; on the approved recipe (`fill 85`) it's a
  structural no-op — it only reads at lower `fill`.
- **Contour-following axis (`contour`)** — tilts the stroke's phase axis
  toward the local iso-luma contour instead of staying locked to vertical,
  so the line direction can follow the subject's form rather than always
  running top-to-bottom — the difference between "engraving" and "CRT
  scanlines" in principle. `0` = off (pure vertical, byte-identical to the
  original construction). Tuning note: on the approved test footage it
  reads as fine flicker/static rather than a contour, so it's kept in code
  but left at `0` in the shipped recipe — footage with lower-frequency
  gradients may read it differently.

The approved recipe also runs a pipeline *around* the effect (see
`run-scripts-dotpipe/model-gamma-spacengrave-metal.sh` for the per-stage docs):

- **2× supersample (lanczos) → effect → down** — dash geometry computed
  on a finer grid reads smoother after the downsample.
- **Gamma shadow lift (`eq=gamma=1.5`)** — the test material is dark
  (mean luma ~36/255) and too dark for the Otsu gate and tone shading
  to find the subject; gamma > 1 lifts the darks proportionally and
  leaves highlights (skin) nearly untouched. A flat brightness offset
  was tried and rejected — it lifts skin too.
- The supersampled intermediate (2560 rows on the 720×1280 source)
  exceeds the 2048-row Metal row-table bound, so the effect renders on
  the CPU core — by design, byte-identical, and the
  "retrofx: Metal unavailable/failed" warning in that render is
  expected, not an error.
- **Encode `yuv444p`** for the dot effects — 4:2:0 chroma subsampling
  desaturates the small dots toward gray.

## `vapor` mode — a stream-level trail, not a per-frame effect

Unlike the `dot.*`/`vid.*` cores, `vapor` holds an accumulator *across*
frames and composites it before writing each one, so it must run last in
the chain — it works on whatever the effects before it produced, and frame
N is informed by frame N-1's trail.

- **Haze vs. comet tail** — `streak = 0` leaves the trail in place (an
  in-place haze/smoke read); `streak > 0` smears it along the row (a comet
  tail). The tail only accumulates where the source ink moved
  *horizontally* between frames — vertical-only motion leaves no trail (an
  earlier build fed the accumulator in place regardless of motion axis;
  this is the fix, don't regress it).
- **`dir`** picks which half of the horizontal smear survives: both
  directions (default, reads as smoke), right-only, or left-only.
- **Loop order is load-bearing** — each output frame the effect composites
  the existing trail, writes the fresh ink, then decays the accumulator;
  `slow` repeats an input frame N times before advancing, so decay ticks
  per *output* frame, not per input frame. Reordering composite/write/decay
  changes the perceived tail length even at the same `decay` value.
- Tried and parked: `vapor` after `dot.portal` reads as smoke regardless of
  `dir` — the portal dot field doesn't have the directional motion `vapor`
  needs. The directional tail was tuned and approved on `dot.spacengrave`
  output specifically.

CPU-only by construction: `vapor` and its parsing live in
`dotpipe/dotpipe.c`, not `core/`, so it has no Metal mirror and is
byte-identical across backends by construction — nothing to dual-verify.

## `vid.glitch` — bursts, not constant distortion

A burst envelope gates the effect: **sharp bursts separated by clean
frames**, not a permanent distortion. `amount` sets burst frequency
(`0` = off, byte-identical); `seed` sets the variation.

During a burst, the enabled elements compose, each with its own
strength dial (`0` = off, individually switchable):

| Dial | Element |
|---|---|
| `bands` | horizontal slice tears (offset rows) |
| `blocks` | block/tile corruption — duplicate, invert, or solid-noise tiles |
| `scan` | scanline jitter: row shift + brightness flicker |
| `rgb` | RGB channel split (chromatic aberration) |
| `quant` | colour posterize (banding) |
| `vtear` | vertical-sync tear |
| `noise` | static grain |

Strength semantics: `50` = balanced full look, `100` = 2×, and any
element at ≥ `80` earns occasional whole-frame signal-loss jumps. It's
meant to stack *after* other effects — e.g. over `dot.gate` for a
corrupted dot-map. With all element dials at `0` even the bursts are
byte-pure (displacement only).

## The `vid.*` stack — order matters

The vintage stack (`model-vintage.sh` / `jimbots-vintage.sh`) applies
the nine `vid.*` glass effects at locked levels. Two things that will
bite you if you reorder or re-blend:

- **`tapewow` sits *before* the glass stages** (scanlines / shadowmask
  / vignette / barrel / overscan) — the raster wobbles *inside* the
  fixed glass. Put it after and the wobble drags the curved frame up
  and down with it.
- **When blending a second branch (e.g. a faint `dot.gate` overlay),
  `format=rgba` on both blend inputs *and* the blend output is
  required.** Otherwise this ffmpeg negotiates `blend` into YUV420P
  (the encoder's format propagates backwards); a screen blend in YUV
  screens the chroma planes, so "black" chroma (128,128) screens up to
  ≈(192,192) — a full-frame magenta cast (measured: black+black
  rendered as (132,0,152)). And **`split=2`** is required to fan the
  input into the two branches — feeding the same label into both makes
  this build re-negotiate one branch to the source's native size, which
  `blend` rejects.

(That overlay variant is the parked `vintagestack.sh` in
`archive/frei0r/run-scripts/storage/`; the active dotpipe scripts run the bare nine-effect
stack.)

## Backends and parity (summary)

`dot.*` + `vid.glitch` have Metal implementations; `RETROFX_BACKEND`
selects `cpu` / `metal` / `dual`. The CPU core is the reference — the
Metal path must be byte-identical (the `dual` cross-check is the
acceptance gate; dotpipe's `dual` backend drives it). Numbers: `PERFORMANCE.md`.
The 2048-row bound on `metal_spacengrave` is the one known, deliberate
CPU fallback. `vapor` has no Metal path at all — see above.

Known open exception: the approved `dot.spacengrave` recipe currently shows
a small (1–25 px/frame) CPU/Metal drift under `dual` on most frames of the
test footage, reproducible without `--vapor` — likely a dither-boundary
rounding case, not yet root-caused. Both backends are independently valid
output; this is a parity gap to close, not a correctness bug in either one.
