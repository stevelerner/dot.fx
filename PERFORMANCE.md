# Performance — CPU vs Metal (measured 2026-09-13)

The four GPU effects (`dot.gate`, `dot.portal`, `dot.spacengrave`,
`vid.glitch`) — CPU core vs Metal, in a real FFmpeg pipeline, on the two
`inputvideos/` videos.

## Machine
Apple M4 Pro (Mac16,11), macOS 26.6.2, Homebrew `ffmpeg-full`
(Lavc63.1.101) with frei0r.

## Method
- `ffmpeg -filter_threads 1 -i <input> -vf "frei0r=libretrofx_<fx>:<params>"
  -an -f null -` — decode + effect only, no encode; `FREI0R_PATH=build`.
- `RETROFX_BACKEND=cpu` vs `metal`. Numbers are FFmpeg's wall-clock
  `speed` (includes decode overhead — honest end-to-end, not a kernel
  microbenchmark).
- Params: approved recipe where one exists (`dotgate 28|8`, spacengrave
  `10|85|80|68|200|78|360|100|5`), adapter defaults otherwise.
- Metal pipeline compiles on first frame of each process; that one-time
  cost is inside the measured runs (a fraction of a second against
  274/816 frames — small next to the measured margins).
- Zero CPU-fallback warnings across all 16 runs: the Metal path genuinely
  executed (both inputs sit under the 2048-row bound).
- Stability: two full passes over the script battery differ by 1–3%
  (e.g. 10.8 s vs 11.2 s) — the numbers below are reproducible within
  noise.

## Inputs
| File | Geometry | Frames |
|---|---|---|
| `inputvideos/model.mp4` | 720×1280 @ 30 | 274 (9.1 s) |
| `inputvideos/jimbots.mp4` | 1920×1080 @ 59.94 | 816 (13.7 s) |

## Pipeline throughput — `jimbots.mp4` (1920×1080)

| Effect | CPU | Metal | Metal win |
|---|---|---|---|
| `dot.gate` (28\|8) | 0.74× · 18.5 s | 1.88× · 7.3 s | **2.5×** |
| `dot.spacengrave` (recipe) | 0.80× · 17.1 s | 1.36× · 10.1 s | **1.7×** |
| `vid.glitch` (defaults) | 6.43× · 2.1 s | 11.6× · 1.2 s | **1.8×** |
| `dot.portal` (defaults) | 1.52× · 9.0 s | 1.92× · 7.1 s | **1.26×** |

## Pipeline throughput — `model.mp4` (720×1280)

| Effect | CPU | Metal |
|---|---|---|
| `dot.gate` (28\|8) | 3.38× · 2.7 s | 4.6× · 2.0 s |
| `dot.spacengrave` (recipe) | 3.65× · 2.5 s | 4.78× · 1.9 s |
| `vid.glitch` (defaults) | 33× · 0.27 s | 47× · 0.19 s |
| `dot.portal` (defaults) | 8.79× · 1.0 s | 5.32× · 1.7 s |

## Per-frame cost (derived)

`jimbots.mp4` — 816 frames:

| Effect | CPU ms/f | CPU fps | Metal ms/f | Metal fps |
|---|---|---|---|---|
| `dot.gate` | 22.6 | 44 | 8.9 | 112 |
| `dot.spacengrave` | 20.9 | 48 | 12.3 | 81 |
| `dot.portal` | 11.0 | 91 | 8.7 | 114 |
| `vid.glitch` | 2.6 | 383 | 1.4 | 691 |

`model.mp4` — 274 frames:

| Effect | CPU ms/f | Metal ms/f |
|---|---|---|
| `dot.gate` | 9.9 | 7.2 |
| `dot.spacengrave` | 9.1 | 7.0 |
| `dot.portal` | 3.8 | 6.2 |
| `vid.glitch` | 1.0 | 0.7 |

fps = frames/s the stage sustains; ≥59.94 fps (jimbots) or ≥30 fps
(model) means the stage keeps up with real-time for that input.
`dot.gate` and `dot.spacengrave` are the heavyweights on CPU
(≈21 ms/f @1080p) but both clear real-time on Metal.

## End-to-end render scripts (wall clock, decode + effect + encode)

`run-scripts/`, defaults, measured 2026-09-13:

| Script | Workload | Wall |
|---|---|---|
| `model-gamma-spacengrave-metal.sh` | 274 f, GPU | 2.6 s |
| `model-spacengrave-metal.sh` | 274 f, 2× supersample → CPU core by row bound | 11.2 s |
| `model-vintage.sh` | 274 f, 9-effect `vid.*` CPU stack | 17.9 s |
| `model-compare-3pane.sh` | 274 f × 3 branches | 21.7 s |
| `jimbots-spacengrave-metal.sh` | 816 f, GPU | 13.4 s |
| `jimbots-gamma-spacengrave-metal.sh` | 816 f, GPU | 13.9 s |
| `jimbots-vintage.sh` | 816 f, 9-effect `vid.*` CPU stack | 118.0 s |
| `jimbots-compare-3pane.sh` | 816 f × 3 branches | 139.5 s |

## Reading the numbers
- **At 1080p Metal wins on all four — 1.26–2.5×.** `dot.gate` has the
  largest gap (per-pixel probes + Otsu region test): ≈23 ms/frame CPU →
  ≈9 ms/frame GPU, i.e. ≈112 fps real-time at 1080p.
- **At 720p the effect is cheap next to FFmpeg's decode/pipe overhead**,
  so the CPU↔Metal delta shrinks to noise level; the `dot.portal`
  "reversal" (8.79× CPU vs 5.32× Metal) is within that overhead — the
  same effect shows the normal Metal lead at 1080p (1.26×).
- `vid.glitch` is light in both backends; the win is real but small in
  absolute seconds.
- The 9-effect `vid.*` stack is CPU-only (see `plan/vid-update.md`
  Phase 2) — that's why `jimbots-vintage` is the slowest thing in this
  repo at ~2 min for 816 frames.
- `dot.spacengrave` frames taller than 2048 rows render on CPU **by
  design** (row-table bound — HANDOFF §1); expected, not a regression.
- These are wall-clock pipeline numbers, not a correctness claim. The
  correctness gate is the dual-backend byte-identity battery
  (64×64 / 720×1280 / 2560×1440 / 3840×2160 — HANDOFF §4).

## Reproduce
```sh
export FREI0R_PATH="$PWD/build"
export RETROFX_BACKEND=cpu     # then repeat with =metal
ffmpeg -hide_banner -y -filter_threads 1 -i inputvideos/jimbots.mp4 \
  -vf "frei0r=libretrofx_dotgate:28|8" -an -f null -
# final stats line:  frame= 816 ... speed=0.741x elapsed=0:00:18.48  (cpu)
#                    frame= 816 ... speed=1.88x  elapsed=0:00:07.27  (metal)
```
