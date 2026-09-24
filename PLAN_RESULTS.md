# PLAN_RESULTS.md — QWEN_PLAN.md execution results (2026-09-23)

Plan executed end to end. Every code item shipped **dual-parity verified**
(`tools/verify.sh`, CPU = reference) with **A/B byte-identity** at default
settings. One feature (2.4 `contour`) is correct but **not usable on the
model footage** after review — kept in code, default off.

## Status per plan item

| Item | Status | Verified |
|---|---|---|
| 0 — `tools/verify.sh` | done | builds, decodes, dual-runs, PASS/WARN/FAIL verdict |
| 1.1 shared scratch | done | dual PASS |
| 1.2 luma plane | done | dual PASS (bit-identical caching) |
| 1.3 dispatch_apply chunks | done — **kept despite 2–6 % slower** (user call) | dual PASS |
| 1.4 | skipped (per plan) | — |
| 2.1 `color2` duotone (dot.portal) | done | dual PASS × 3 images; A/B byte-identical at `color2 = color` |
| 2.2 `texture` 2nd layer (dot.spacengrave) | done | dual PASS; A/B byte-identical at `texture 0` |
| 2.3 `crisp` (dot.portal) | done | dual PASS flat+100; A/B byte-identical vs saved reference |
| 2.4 `contour` (dot.spacengrave) | done in code, **look rejected on this footage** (see below) | dual PASS × 3 images @ 0/100; tall 1280×2400 PASS via Metal @ contour > 0; A/B byte-identical at 0 |
| pre-existing bug fix | `gather_portal` MSL variable-shadowing (uninitialized outer weights read as white) — one line removed | sprite-dump field diff 0 (was 889+); full battery PASS |

## Final state (the looks)

- **dot.portal keeper** — `run-scripts-dotpipe/model-gamma-dotportal.sh`:
  recipe `8 50 5 70 50 200 50 360 180` (fine dots at the separation limit,
  bright halo, **white→cyan duotone**, gamma-1.5 input lift, Metal,
  fallback = hard error) → `outputvideos/model-gamma-dotportal-dotpipe.mp4`
- **dot.spacengrave keeper** — step-1 look (vertical engraving):
  `model-gamma-spacengrave.sh`, recipe `5 85 80 68 200 78 360 100 5`,
  `contour` off. This is the approved one; contour renders were reviewed
  (40 and 100, dashed and solid) and all read as **TV static** on the model
  footage — tilt itself, not just the dashing, is the problem.

## What we learned

### Parity discipline (what made the four new dials safe)
1. **CPU is the reference; MSL mirrors expression-for-expression.** Dual
   byte-compare (`RETROFX_BACKEND=dual`) catches any drift. Never weaken a
   check to pass it.
2. **`dial 0 = byte-identical` is the safety invariant.** Every new dial is
   guarded by `dial > 0` and uses `fma(0, x, y) == y` exactness so defaults
   stay bit-exact. A/B-verify against a reference saved **before** the
   rebuild.
3. **GPU floats are not free.** The GPU's `sqrt`/`/` are not always
   correctly rounded → use the mirror's `crsqrt`/`fx_div`. MSL fuses
   mul+add into fma context-dependently → pin with explicit `fma(..., 0.0f)`.
   This MSL toolchain has **no `fmodf`/`fabsf`** — use `fmod`/`fabs`
   (both exact ops, parity-safe).
4. **Duplicated MSL code is a drift hazard.** The `gather_portal` white-output
   bug came from re-declared locals in a nested branch silently diverting
   assignments. Keep mirrored kernels single-path where possible.

### Feature design vs look
5. **Dual parity proves correctness, not aesthetics.** 2.4 `contour` passed
   every parity gate and still failed the eyeball test — per-pixel axis tilt
   on a textured subject reads as multi-directional speckle ("TV static"),
   with or without the dash break-up. Look features need per-footage review
   before they're "done".
6. **Dot.portal brightness has a hard ceiling at `level`.** The
   hue-preserving clamp scales each dot so its brightest channel hits 255, so
   `level > 200` adds nothing. The real "brighter" levers are **`fill`**
   (dot area) and **`glow`** (halo — lights the gaps).
7. **Positional args are shift-traps.** Adding an 11th dial means a 10-value
   command silently puts the 10th value into `texture`, not `contour` — and
   with the approved recipe that's a no-op, so the "contour 100" test looked
   like a dead feature for one debugging cycle. Pass the full chain or
   nothing.

### Environment / tooling
8. `make` hangs on this volume — always direct `cc` (the recipe in
   `tools/TOOLS.md`, baked into `verify.sh`).
9. The in-editor edit tool **double-unescapes** string values: a literal `\n`
   (backslash-n) in a C string literal became a real newline and broke the
   MSL source. For surgery inside string-literal tables, use a Python
   heredoc with exact byte strings and `count == 1` assertions.
10. The 2048-row Metal bound existed only for the per-row table; `contour > 0`
    computes per-pixel, so **tall frames (>2048) now run on Metal** — verified
    dual at 1280×2400.

## Known quirks (reported, not fixed — pre-existing)

- README's spacengrave table labels the 6th param `glow`; it's `grain`.
- With the approved recipe (`fill 85`), the `texture` dial is a structural
  no-op: half-pitch is 2, so per-row phase is 0 or 0.5 and the stroke
  (centered 0.25, half-width 0.2125) never lands on a row. `texture` only
  does something at lower `fill`.
