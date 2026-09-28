# QWEN_PLAN.md — planned work for the local qwen coding agent

Planning happens in the cloud; all coding on this repo is done by the local
qwen agent (Zed → Ollama on navigator, `qwen3.8:27b-mtp-q4_K_M`). This file
is the queue of standalone task prompts for it — paste one at a time, verify,
then move to the next. Don't batch multiple prompts into one qwen turn.

**Give qwen once, before any prompt below:** "Read `AGENTS.md` and
`videoprocessdetails.md` first and follow their rules exactly — scope lock,
pause after each verified step, CPU is the reference, any effect-math change
must be mirrored in `core/metal_fx.m` and reverified with
`RETROFX_BACKEND=dual`. Use `tools/verify.sh <test-image> <flag> [params...]`
for that instead of composing the build/ffmpeg/dotpipe pipeline by hand, and
paste its raw output in your report — see `tools/TOOLS.md`'s Playbooks
section for the add-a-dial and dual-verify checklists these prompts assume."

## Part 0 — write tools/verify.sh (do this first, once)

`AGENTS.md` and `tools/TOOLS.md` already document this script's exact
contract, written in advance so you have a spec to build to — implement it
now.

> Task: create `tools/verify.sh`, matching the usage already documented in
> `AGENTS.md` and the "Verification one-liners" section of `tools/TOOLS.md`:
> `tools/verify.sh <test-image.ppm> <dotpipe effect flag> [params...]`. It
> should, in order: (1) build `dotpipe/dotpipe` using the direct-`cc` recipe
> in `tools/TOOLS.md` (not `make` — that file notes `make` hangs on this
> volume), failing loudly with the build log on error; (2) get the test
> image's width/height (e.g. via `ffprobe`) and convert it to a raw RGB24
> frame (e.g. via `ffmpeg -f rawvideo -pix_fmt rgb24`); (3) run
> `RETROFX_BACKEND=dual ./dotpipe/dotpipe -w <w> -h <h> <flag> [params...]`
> on that frame; (4) print a single unambiguous `PASS:`/`FAIL:` line plus the
> raw stderr — treat a `MISMATCH` line or non-zero exit as `FAIL`, and an
> "unavailable/failed" (no-Metal) line as a `WARN` since that's a CPU-only
> environment, not a parity failure. Verify it yourself by running it against
> `testimgs/bars.ppm --dotgate 28 8 10` and confirming a clean PASS, then
> against a deliberately-broken build (e.g. a stray syntax error) to confirm
> it reports FAIL with the build log rather than crashing. Report the exact
> commands you ran and their output.

## Part 1 — CPU performance

Context: `PERFORMANCE.md` shows `dot.gate` and `dot.spacengrave` costing
~21-23ms/frame on CPU at 1080p, and the 9-effect `vid.*` CPU stack is the
slowest thing in the repo (113s for 816 frames, `jimbots-vintage.sh`). These
four target those numbers, safest first.

### 1.1 — Kill per-frame malloc/free (safest, broadest win)

> Task: every effect core in `core/crt.c`, `core/vhs.c`, `core/glitch.c`, and
> `core/dot.c` calls `malloc()`/`free()` for a scratch buffer (source
> snapshot, row buffer, luma row) on *every single frame* it processes.
> Replace this with a persistent, grow-only scratch buffer that's allocated
> once and reused across frames (e.g. a `static` buffer per call site, or a
> small shared helper that tracks its current capacity and only reallocs
> when a bigger frame arrives). Do this one core file at a time. Do NOT
> change any pixel math — only the allocation lifetime. After each file,
> rebuild and run `RETROFX_BACKEND=dual ./dotpipe/dotpipe -w 1280 -h 720
> --dotgate 28 8 10 < in.raw > out.raw` (swap in the relevant effect flag for
> non-dot effects) to confirm output is unchanged, then run the matching
> `run-scripts-dotpipe/*.sh` end to end and confirm timing improved. Stop
> after each file and report before moving to the next.

### 1.2 — Precompute the luma plane in core/dot.c

> Task: in `core/dot.c`, `local_shade()` calls `luma_at()` 8 times per grid
> cell (4 near-probe + 4 wide-probe neighbors), and `luma_at()` recomputes
> `0.299*r + 0.587*g + 0.114*b` from raw RGB every single call — including
> from `dot_spacengrave`'s per-pixel loop, where adjacent pixels' probes
> overlap heavily. Add a single per-frame luma plane (float array,
> width*height, computed once at the start of `dotgate()`, `dotportal()`,
> and `dot_spacengrave()` using the *exact same formula and operation
> order* already in `luma_at()`), and have `luma_at()`/`local_shade()` read
> from it instead of recomputing. This must be bit-identical output — same
> formula, just cached, not restructured or reordered. Verify with
> `RETROFX_BACKEND=dual` on all three effects (`--dotgate`, `--dotportal`,
> `--spacengrave`) before and after, byte-for-byte. Report per-effect CPU
> timing before/after using the `PERFORMANCE.md` reproduce commands.

### 1.3 — Parallelize the per-cell gather loop (bigger change, needs care)

> Task: in `core/dot.c`, `dotgate()`/`dotportal()`'s gather loop computes one
> independent `sprite_t` per grid cell and appends it to `spr[n++]` — the
> sequential index makes it look order-dependent but each cell's math only
> depends on its own `(sx, sy)`, not on other cells. Restructure so each
> cell writes to a precomputed slot index (`row * cols + col`) instead of a
> running counter, then parallelize the gather loop with `dispatch_apply`
> (libdispatch/GCD — already implicitly available since Foundation is
> linked). Do NOT touch `metal_fx.m` or any effect math. This changes
> execution order, not values, so output must stay bit-identical — verify
> with `RETROFX_BACKEND=dual`, and additionally run the same render twice
> and byte-diff the two outputs against each other to catch any threading
> nondeterminism the dual check might miss. Report before/after wall-clock
> on both `inputvideos/` files.

### 1.4 — Optional/stretch: division→reciprocal in add_ellipse (higher risk, do only if 1.1-1.3 land clean)

> Task: `add_ellipse()` in `core/dot.c` divides by `lx`/`ly` for every pixel
> in its inner loop; hoisting `1/lx`/`1/ly` once and multiplying is a real
> win but changes floating-point rounding, which will break CPU/Metal
> byte-parity unless mirrored. Per `AGENTS.md`: make the identical change in
> both `core/dot.c` and the matching kernel in `core/metal_fx.m`, then
> re-verify with `RETROFX_BACKEND=dual` across the parity battery (64×64 /
> 720×1280 / 2560×1440 / 3840×2160, per `tools/TOOLS.md`). If dual reports
> any mismatch, revert — don't chase it into the CPU math. Ask before
> starting this one; skip it if 1.1-1.3 already meet the goal.

## Part 2 — Look improvements: dot.portal and dot.spacengrave

Ordered safest → biggest lift. Each is a standalone task; verify before
moving to the next.

### 2.1 — dot.portal duotone hue (shading-only, safest)

> Task: `portal_ramp()` in `core/dot.c` tints every dot with a single flat
> hue from the `color` dial. Add a second hue dial (e.g. `color2`, same
> 0-360 encoding as `color`, 360 = white) and blend between the two by the
> dot's own `shade` value (already computed in `local_shade` — <1 = shadow
> side, >1 = lit side) instead of one flat hue: shadow-leaning dots pull
> toward `color`, highlight-leaning dots pull toward `color2`. Add the CLI
> arg to `dotpipe/dotpipe.c`'s `--dotportal` flag (positional, after
> `color`) and the README's parameter table row. Mirror the identical blend
> in `gather_portal` in `core/metal_fx.m`. Verify with
> `RETROFX_BACKEND=dual` across the parity battery in `tools/TOOLS.md`, then
> render `run-scripts-dotpipe/model-gamma-spacengrave-metal.sh`-style output at a
> few `color`/`color2` combos and eyeball it. Default `color2` to the same
> value as `color` so existing renders stay byte-identical unless the new
> dial is touched.

### 2.2 — dot.spacengrave second dash layer (additive, no geometry change)

> Task: `dot_spacengrave()` in `core/dot.c` draws one dash layer at pitch
> `size`. Add a second, independent dash layer at a higher frequency (e.g.
> a fixed multiple of the primary pitch, or a new `size2` dial) with its own
> lower baseline coverage, composited additively on top of the existing ink
> before the 255-clamp — texture break-up only, no change to the primary
> stroke geometry or its dither. Gate it behind a new dial (e.g. `texture`
> 0-100, 0 = off = current behavior exactly) so `size < 2` and `texture 0`
> stay byte-identical to today. This effect has no Metal path for the
> CPU-only fallback case (spacengrave already runs on CPU past the
> 2048-row bound) but IS one of the four Metal effects normally — mirror
> the layer in `core/metal_fx.m` too. Verify with dual, then render and
> compare against the current approved recipe.

### 2.3 — dot.portal gradient-linked size (geometry, medium lift)

> Task: in `portal_cell()` (`core/dot.c`), dot radius factor `rf` currently
> comes only from `halftone * luma`. Add a gradient-magnitude term:
> `local_shade()` already returns `edge` (gradient magnitude, near/wide
> probe max) — blend it into `rf` via a new dial (e.g. `crisp` 0-100: at 0,
> `rf` is unchanged from today; at 100, high-edge cells shrink further,
> sharpening the silhouette against soft, low-edge interiors). Work out the
> exact blend formula yourself, but keep it a closed-form function of
> already-available `edge`/`luma`/`halftone` — no new probes. Mirror in
> `gather_portal` in `core/metal_fx.m`. Verify with dual; default
> `crisp 0` must reproduce today's output exactly.

### 2.4 — dot.spacengrave contour-following lines (biggest lift, do last)

> Task: `dot_spacengrave()` currently draws fixed vertical scanlines
> (phase = `y % pitch`) regardless of subject shape — that reads as "CRT
> scanlines," not "engraving." Add a `contour` dial (0-100) that blends the
> stroke direction toward the local gradient-perpendicular direction
> (`shx`/`shy`, already computed by `local_shade`): at `contour 0`, behavior
> is byte-identical to today (pure vertical); at `contour 100`, the
> effective phase axis for a given pixel rotates toward following the
> subject's local contour instead of the fixed y-axis. This changes the
> per-pixel geometry construction, not just shading — design the exact math
> yourself (you have `videoprocessdetails.md`'s description of the current
> model as a reference for what must keep working: asymmetric stroke
> profile, per-pixel dither, texture break-up), but do NOT change what
> happens at `contour 0`. This is the riskiest of the four — expect it to
> need its own mirrored `core/metal_fx.m` kernel change and a full
> dual-verify pass across the parity battery before you call it done. If
> the geometry rework turns out bigger than expected, stop and report back
> rather than pushing through.
