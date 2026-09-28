# HANDOFF.md — continuing effect tuning in dot.fx (state 2026-09-27)

Supersedes the 2026-09-23 QWEN_PLAN.md / PLAN_RESULTS.md pair (both deleted).
Read `AGENTS.md` and `videoprocessdetails.md` first — scope lock, pause after
each verified step, CPU is the reference for any effect-math change.

## What exists now (approved looks, all on `model.mp4` unless noted)

| Look | Recipe | Stage-1 lift | Script |
|---|---|---|---|
| dot.portal **T** (bold white dot body, approved 09-27) | `--dotportal 18 70 5 100 100 800 100 360 360 100` | `eq=gamma=2.0` | `run-scripts-dotpipe/model-gamma-dotportal-metal.sh` |
| dot.portal T, video inverted first (NOT inverted after) | same | `eq=gamma=2.0,negate` | `run-scripts-dotpipe/model-gamma-dotportal-invert-metal.sh` (not yet eyeball-approved) |
| dot.spacengrave (approved 09-23, unchanged) | `--spacengrave 5 85 80 68 200 78 360 100 5` | `eq=gamma=1.5` | `run-scripts-dotpipe/model-gamma-spacengrave-metal.sh` |
| dot.spacengrave, inverted first (**"amazing"**, approved 09-27) | same | `eq=gamma=1.5,negate` | `run-scripts-dotpipe/model-gamma-spacengrave-invert-metal.sh` |
| spacengrave + vapor comet, half speed (approved 09-27) | + `--vapor 2 95 60 90` | `eq=gamma=1.5` | `run-scripts-dotpipe/model-gamma-spacengrave-vapor-metal.sh` (model + jimbots) |

Run scripts are named `-metal` (GPU, Metal-fallback = hard error) or `-cpu`.
They take `[-i input] [-o output]` — reuse them for other footage instead of
ad-hoc pipelines (the jimbots invert run was exactly
`sh run-scripts-dotpipe/model-gamma-spacengrave-invert-metal.sh -i inputvideos/jimbots.mp4 -o outputvideos/jimbots-gamma-spacengrave-invert.mp4`).

## Knobs that matter (learned by eye, 09-23 → 09-27)

**dot.portal** (10 positional values: size fill gate halftone relief level glow color color2 crisp)
- `fill` is the real brightness lever (dot area). `level` has a hard ceiling
  (~200 adds nothing — hue-preserving clamp); at white duotone, `fill` + `glow`
  do all the brightening.
- `color2` hue: cyan (180) is perceptually **dark** and was the hidden dimmer.
  White (360) = max luminance. Pale hues (green 120, ice ~200) keep tint.
- `gate` (subject knee): 5 = body + faint background dots ("grid with a shape
  walking through it"); 15 thins the body on this footage; **30 = black frame**.
  Keep ≤ 5–8 on model.mp4.
- `relief` (max 100) + `crisp` (max 100) are what broke the grid look — dots
  bend along the surface and shrink on the silhouette. Both 0/off = grid.
- `halftone` 100 = dot size fully tracks tone (the "body of dots walking" read).
- Density: pitch 8 (fine, "too dense") → 12 → 16 → 18 (approved T). Sparseness
  and brightness fight each other — resolve with `fill`/`glow`, not pitch.

**dot.spacengrave** (11 values: size fill halftone line level grain color dim gate texture contour)
- Approved recipe is the 09-23 one; `contour` reads as **TV static** on model
  footage (kept in code, leave off). `texture` is a structural no-op at
  `fill 85` (only matters at lower fill).
- The invert-first variant is the keeper look on 09-27; the plain one still
  ships as the standard.

**--vapor** (stream mode, use last; `slow decay faint streak [dir]`)
- `streak 0` = in-place haze (smoke); `streak > 0` = x-axis tail. `dir` (new
  09-27): 0 = both directions (smoke), 1 = tail right-only, 2 = left-only.
- The trail feeds ONLY where ink advanced **horizontally** (vertical motion
  leaves no trail) — do not regress this (it was the in-place-`prev` bug).
- Approved: `2 95 60 90` (dir 0) on spacengrave. On dot.portal the vapor trail
  read as smoke and the dir variants were **rejected** — portal+vapor is parked,
  don't re-tune without a new direction.
- Order in the loop: composite → write → update, `slow` times per input frame
  (decay ticks per output frame). Changing that order changes perceived tail
  length — user tuned against the current order.

## Tuning workflow (the loop that worked)

1. 4-sec probe: `ffmpeg -t 4` decode (with the stage-1 `eq=gamma=...,[negate]`)
   → `RETROFX_BACKEND=metal ./dotpipe/dotpipe -w 720 -h 1280 --fps 30 <flags>`
   → encode `libx264 crf 20 -pix_fmt yuv444p` (4:2:0 mangles thin dots).
   One letter per variant (`portal-4s-A.mp4`, …) in `outputvideos/`.
2. User watches the mp4s and names the winner (or "beyond X" / "more of Y").
3. Lock the winner into the run script (values + header docs + README row),
   verify with the full-length script run (frame count = source × slow).
4. Prune the losing samples on request. Keep the user's favorite.

## Environment / tooling pitfalls

- **Sandbox blocks Metal**: render/verify with the unsandboxed terminal, or a
  "Metal unavailable/failed" stderr line means you silently got CPU.
- `ffprobe`/`ffmpeg` live in `/opt/homebrew/bin` — put it on PATH.
- `-vsync` is not valid here; use `-fps_mode passthrough` / `vfr`.
- `make dotpipe/dotpipe` worked in the 09-27 session; the 09-23 notes say
  `make` can hang on this volume — fall back to the direct-`cc` recipe in
  `tools/TOOLS.md` (baked into `tools/verify.sh`).
- Any effect-math change: mirror in `core/metal_fx.m`, reverify
  `RETROFX_BACKEND=dual` (`tools/verify.sh <img> <flag> [params]`), and keep
  the `dial 0 = byte-identical` invariant.
- `--vapor` and the parse code live in `dotpipe/dotpipe.c` (CPU-only stage,
  byte-identical across backends by construction). Buffers are RGB24 — row
  stride is `w*3`, loop per channel (the streak pass had this bug once).

## Known issues (reported, deliberately NOT fixed)

- Pre-existing CPU/Metal parity drift of 1–25 px/frame on the approved
  spacengrave recipe (`RETROFX_BACKEND=dual` shows MISMATCH on ~114/120
  frames, reproducible **without** --vapor). Likely dither-boundary rounding.
- README spacengrave table labels the 6th param `glow`; it's `grain`.

## Open threads (not approved — ask before acting)

- Commit the demo pair: `inputvideos/model.mp4` +
  `outputvideos/model-gamma-spacengrave-dotpipe.mp4` are un-ignored and
  staged-ready; `.gitignore` now exceptions them (other media stays ignored).
- README render-scripts table: dotportal, dotportal-invert, spacengrave-invert
  rows missing.
- `jimbots-gamma-spacengrave-invert` was run ad-hoc; no dedicated script.
- `jimbots` has no dotportal script at all.
- `vapor dir` flag: untested beyond the portal rejects; could still be tried
  on spacengrave footage (dir 1/2) if the user wants directional trails.
- 09-27 scratch samples in `outputvideos/` (portal-4s-*, portal-vapor-*,
  portal-invert-*, portal-neg-*, vapor-samples/) — pruned only on request.
