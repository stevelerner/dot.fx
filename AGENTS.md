# AGENTS.md

Retro video FX: C99 effect cores with a Metal GPU backend, exposed as frei0r
plugins. macOS.

## Commands
build harness:  make
build plugins:  make frei0r          # 12 dylibs into build/ + tools/f0r_host
test images:    make inputvideos
clean:          make clean

verify a plugin:
  tools/f0r_host build/libretrofx_<fx>.dylib <apply|identity|roundtrip|determinism> [w h]

Backend selection is by env var: RETROFX_BACKEND=cpu (default) | metal | dual.
`dual` runs both and byte-compares, returning the CPU result — use it to check
GPU parity:
  RETROFX_BACKEND=dual tools/f0r_host build/libretrofx_dotgate.dylib apply 1280 720

Build the affected plugin and run the narrowest relevant f0r_host check before
declaring work done. `-Wall -Wextra -Werror` is on: any warning is a build
failure, including unused variables.

## Rules
- **Do exactly what was asked — no drift, no overthinking.**
  - Scope lock: restate the task in one line before starting; anything not
    in that line is out of scope — mention it in one line, don't do it.
    No extra effects, no demo modes, no sample data.
  - Report, don't fix: drive-by improvements are forbidden. Noticing
    something adjacent (a bug, a stale name, a better wording) goes in the
    reply as one line — no edits unless asked.
  - Output budget: the reply is what changed / what verified / what next,
    one line each. No tables, no essays, no inventing options that weren't
    requested.
  - Drift tell: about to write a long message or propose unrequested work →
    that's the drift; stop and cut it.
- **Pause — non-negotiable cadence. A long unbroken run is a declared
  failure mode.**
  - After each verified step (change built + checked, render approved, question
    answered): STOP, report done / verified / next, wait for a go-ahead. Never
    roll into the next step on your own, even when the plan is written out.
  - When something fails or surprises you (a test failure, a dual mismatch, a
    bug outside the current task's scope): STOP immediately, report what
    happened, and wait. Do not silently start fixing or deep-investigating.
  - Hard cap: at most 3 edit→build→test cycles in a row within one step, then
    pause and report state — even if you already know the next move.
  - Bugs found in already-shipped code: propose the fix, wait for approval
    before touching it.
  - When in doubt whether to continue: pause and ask. Asking costs one line;
    running on costs a session.
- Smallest diff that works. Don't refactor adjacent code.
- **CPU core is the reference.** Metal mirrors it; when they disagree the CPU
  path is right unless stated otherwise. Never change CPU math to make a GPU
  mismatch go away.
- Any change to effect math must be mirrored in both core/*.c and
  core/metal_fx.m, and verified with RETROFX_BACKEND=dual.
- Effects must stay deterministic — same input and frame index, same output.
- C99, stdlib + Metal/Foundation only. No new dependencies without asking.
- Read a neighbouring effect in core/ before adding one.
- If ambiguous, ask one question instead of guessing.
- **Keep the tree tidy.** Don't leave scratch files (probes, patches, one-off
  diags, stale renders) at the repo root — delete them as soon as they've
  served their purpose. At every logical checkpoint (a step verified, a bug
  fixed, a render approved), sweep the root and outputvideos/ for cruft before moving
  on. Never commit, or let .gitignore rules drift, for build artifacts,
  binaries, render outputs, or OS junk (.DS_Store).

## Layout
core/              effect implementations (crt.c, vhs.c, dot.c, glitch.c) + metal_fx.m
frei0r-adapter/    retrofx.c — one dylib per effect via -DRETROFX_EFFECT
harness/, tools/   local test harness + render recipes; full inventory: tools/TOOLS.md
reference/frei0r   upstream frei0r headers
inputvideos/, examples/, build/

plan/dotgate.md is the dot.gate plan (shipped spec + controls roadmap);
plan/vidglitch.md is the vid.glitch spec. HANDOFF.md is current in-flight
state — read it before starting.
tools/TOOLS.md = tool inventory + environment/install requirements — read it
when setting up or debugging the environment.
