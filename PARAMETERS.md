# Effect parameters — details

`--glitch` and the `dot.*` flags take 0–100 dials (0 = off, `50` = default
look, `100` = strong; `tapewow` runs on a 24 fps clock). The `vid.*` flags
take raw core values (0–1 strengths, px, frames) — the run scripts in
`run-scripts-dotpipe/` are the source of truth for approved levels.

Parameter order is positional, left to right, as listed below.

## `vid.shadowmask` — `--mask <type> <intensity> <pitch>`

- `type` — picks the subpixel mask pattern: `grille` (vertical aperture-grille stripes), `slot` (mask + slot) or `triad` (per-subpixel dots)
- `intensity` — how hard the stripes/dark areas crush the image
- `pitch` — stripe period in px

To set `type` = grille, `intensity` = 0.40, and `pitch` = 2, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --mask grille 0.40 2 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.scanlines` — `--scanlines <intensity> <period> <offset>`

- `intensity` — depth of the dark lines
- `period` — rows per line+gap
- `offset` — shifts the pattern vertically

To set `intensity` = 1.0, `period` = 2, and `offset` = 0, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --scanlines 1.0 2 0 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.chromablood` — `--bleed <radius> <amount>`

- `amount` — strength (0 = off)

To set `radius` = 14 and `amount` = 1.0, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --bleed 14 1.0 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.bloom` — `--bloom <amount> <threshold> <radius>`

- `amount` — glow strength (0 = off)
- `threshold` — how bright a pixel must be to bloom (0–1)
- `radius` — glow spread in px

To set `amount` = 2.5, `threshold` = 0.05, and `radius` = 28, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --bloom 2.5 0.05 28 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.barrel` — `--barrel <amount> <zoom>`

- `amount` — curvature strength (0 = off)
- `zoom` — magnifies slightly so the image still fills the curved margin (0 = off)

To set `amount` = 0.8 and `zoom` = 1.0, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --barrel 0.8 1.0 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.vignette` — `--vignette <amount>`

- `amount` — corner crush (0 = off)

To set `amount` = 1.0, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --vignette 1.0 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.overscan` — `--overscan <radius> <margin>`

- `radius` — corner roundness, px
- `margin` — how far the crop cuts in, px

To set `radius` = 140 and `margin` = 44, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --overscan 140 44 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.lumar` — `--lumar <amount> <wavelength>`

- `amount` — halo strength (0 = off)
- `wavelength` — spacing of the ring oscillation, px

To set `amount` = 1.5 and `wavelength` = 4, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --lumar 1.5 4 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.rainbow` — `--rainbow <amount> <period_y> <period_t>`

- `amount` — strength (0 = off)
- `period_y` — rows per phasing beat
- `period_t` — frames per beat

To set `amount` = 1.5, `period_y` = 48, and `period_t` = 8, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --rainbow 1.5 48 8 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.tapewow` — `--wow <amplitude> <period>`

- `amplitude` — drift in px (0 = off)
- `period` — frames per full wobble

To set `amplitude` = 26 and `period` = 60, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  ./dotpipe/dotpipe -w 720 -h 1280 --wow 26 60 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `vid.glitch` — `--glitch <amount> <rgb> <noise> <bands> <blocks> <scan> <quant> <vtear> <seed>`

- `amount` — burst frequency 0–100 (0 = off, gates everything below)
- `rgb` — RGB split strength on torn edges
- `noise` — static injected into bursts
- `bands` — slice-tear strength (horizontal slices shifted)
- `blocks` — tile displacement strength
- `scan` — scanline jitter
- `quant` — posterize to banded colors
- `vtear` — vertical tear strength
- `seed` — randomization seed 0–999 (change it for a different pattern)

To set `amount` = 70, `rgb` = 60, `noise` = 40, `bands` = 50, `blocks` = 40, `scan` = 40, `quant` = 50, `vtear` = 20, and `seed` = 42, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --glitch 70 60 40 50 40 40 50 20 42 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `dot.gate` — `--dotgate <size> <speed> <gate>`

- `size` — dot pitch px, the grid density (0 = off)
- `speed` — per-dot wobble in px — dots slowly breathe in and out of position
- `gate` — subject knee ×100 — the fill knob: how prominent an area must be to earn dots (dark/smooth material needs 1)

To set `size` = 28, `speed` = 8, and `gate` = 10, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --dotgate 28 8 10 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `dot.portal` — `--dotportal <size> <fill> <gate> <halftone> <relief> <level> <glow> <color> <color2> <crisp>`

- `size` — dot pitch px (0 = off)
- `fill` — dot radius as % of step — the main brightness/weight lever
- `gate` — subject knee ×100 (higher = only the strongest-form areas keep dots)
- `halftone` — dot size tracks the local tone 0–100 — makes the dot body follow the shading
- `relief` — dots displace along the surface gradient as % of step — bends the field along the form and breaks the grid
- `level` — brightness ×100 (100 = 1×; saturates around 200 — brighten with `fill`/`glow` instead)
- `glow` — aura 0–100 — halo that lights the gaps between dots
- `color` — shadow hue 0–360 (360 = white)
- `color2` — highlight hue 0–360 (360 = white) — shade-blended: shadow → `color`, lit → `color2` (default = `color`, flat; note cyan reads dim — white or pale hues read bright)
- `crisp` — silhouette sharpening 0–100 — high-contrast dots shrink so the dot body hugs the outline (0 = off)

To set `size` = 18, `fill` = 70, `gate` = 5, `halftone` = 100, `relief` = 100, `level` = 800, `glow` = 100, `color` = 360, `color2` = 360, and `crisp` = 100, execute the following:

```sh
ffmpeg -i in.mov -f rawvideo -pix_fmt rgb24 - |
  RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --dotportal 18 70 5 100 100 800 100 360 360 100 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## `dot.spacengrave` — `--spacengrave <size> <fill> <halftone> <line> <level> <grain> <color> <dim> <gate> <texture> <contour>`

- `size` — scanline pitch px (0 = off)
- `fill` — stroke width as % of pitch
- `halftone` — ink follows the tone 0–100 — the engraved 3-D shading read
- `line` — baseline line coverage 0–100 — how solid the base line is
- `level` — ink brightness ×100 (100 = 1×)
- `grain` — texture break-up 0–100 — breaks the lines into dashes over texture (the engraving knob)
- `color` — ink hue 0–360 (360 = white)
- `dim` — source dimming 0–100 (100 = the photo goes fully black behind the ink)
- `gate` — subject knee ×100 (0 = full frame)
- `texture` — second dash layer at half pitch/width, baseline coverage 0–100 (0 = off)
- `contour` — tilt the stroke axis toward the local iso-luma contour 0–100 (0 = off = vertical)

To set `size` = 5, `fill` = 85, `halftone` = 80, `line` = 68, `level` = 200, `grain` = 78, `color` = 360, `dim` = 100, and `gate` = 5, execute the following:

```sh
ffmpeg -i in.mov -vf eq=gamma=1.5 -f rawvideo -pix_fmt rgb24 - |
  RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --spacengrave 5 85 80 68 200 78 360 100 5 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```

## vapor mode — `--vapor <slow> <decay> <faint> <streak> <dir>`

Stream-level mode, goes last in the chain.

- `slow` — each frame written N times (≥1, half speed at 2)
- `decay` — trail persistence per output frame 0–100
- `faint` — trail strength 0–100
- `streak` — x-axis smear 0–100 (0 = in-place haze, >0 = comet tail along each row — the trail feeds only where ink advanced horizontally, so vertical motion leaves no trail)
- `dir` — tail direction 0–2 (0 = both directions, 1 = right-only, 2 = left-only)

To set `slow` = 2, `decay` = 95, `faint` = 60, and `streak` = 90, execute the following:

```sh
ffmpeg -i in.mov -vf eq=gamma=1.5 -f rawvideo -pix_fmt rgb24 - |
  RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --spacengrave 5 85 80 68 200 78 360 100 5 --vapor 2 95 60 90 |
  ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -i - out.mp4
```
