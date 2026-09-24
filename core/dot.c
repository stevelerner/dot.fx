/* dot.c — the dot family: subject-gated dot maps (dot.gate, dot.portal).
 *
 * Portable C99, dependency-free. No host headers here.
 *
 * Hard constraint: every dot attribute — position, size, opacity — is a
 * closed-form function of (seed, pixelID, frame).
 * Never integrated state (no `position += velocity`): editors scrub to
 * arbitrary frames, so any effect that depends on render order breaks the
 * moment playback is not strictly sequential (§5). All per-particle
 * randomness goes through the §5 fx_hash.
 * the §5 fx_hash.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dispatch/dispatch.h>

#include "dot.h"
#include "fx_hash.h"
#include "fx_math.h"
#include "fx_scratch.h"

/* Fixed internal seed, like the other effects: the pattern identity is not
 * a user parameter (keeps the host param surface consistent); it only
 * spreads the per-particle hash slots. */
#define HOLOGRAM_SEED 0x5EED1234u
#define GLITCH_SEED   0x617C5EEDu

/* One fx_hash slot per per-particle attribute (slot = the hash's `y`). */
enum {
    SLOT_PHASE = 100, /* loop phase, uniform 0..1 */
    SLOT_BASX  = 101, /* base x, uniform 0..1 */
    SLOT_SWAYP = 102, /* sway phase, 0..1 -> radians */
    SLOT_SWAYA = 103, /* sway amplitude factor, 0..1 */
    SLOT_SIZE  = 104, /* radius factor, 0..1 */
    SLOT_TINT  = 105, /* tint mix, 0..1 */
    /* v2: pillars (106-110), streaks (112-115), sparkle flicker (116). */
    SLOT_PX    = 106, /* pillar x, 0..1 (stable) */
    SLOT_PW    = 107, /* pillar half-width factor, 0..1 (stable) */
    SLOT_PTINT = 108, /* pillar warm tint, 0..1 (stable) */
    SLOT_PULSE = 109, /* pillar pulse phase, 0..1 (stable) */
    SLOT_PF2   = 110, /* pillar per-half-block flicker */
    SLOT_SY    = 112, /* streak y, 0..1 (stable) */
    SLOT_SX    = 113, /* streak x phase, 0..1 (stable) */
    SLOT_SLEN  = 114, /* streak half-length factor, 0..1 (stable) */
    SLOT_SF    = 115, /* streak per-quarter-block flicker */
    SLOT_SPF   = 116, /* sparkle per-half-frame twinkle */
    SLOT_STH   = 117, /* streak thickness, 0..1 (stable) */
};

/* Glitch slots (200+), per band / per particle / per block. */
enum {
    G_BASEY  = 200, /* band base y, 0..1 (stable) */
    G_PHASE  = 201, /* band x phase, 0..1 (stable) */
    G_BANDBH = 202, /* band half-height factor, 0..1 (stable) */
    G_DIR    = 204, /* scroll direction, 0..1 (stable) */
    G_BLENW  = 205, /* band-line half-length factor, 0..1 (stable) */
    G_BLCX   = 206, /* band-line centre x, 0..1 (stable) */
    G_BRIGHT = 212, /* per-block brightness, 0..1 */
    G_UJIT   = 220, /* particle position jitter, 0..1 (stable) */
    G_YJIT   = 221, /* particle y jitter, 0..1 (stable) */
    G_LX     = 222, /* particle half-length x, 0..1 (stable) */
    G_LY     = 223, /* particle half-length y, 0..1 (stable) */
    G_TINT   = 224, /* green tint, 0..1 (stable) */
    G_DON    = 230, /* per-block dot visibility, 0..1 */
    G_DPOS   = 231, /* dot x, 0..1 (stable) */
    G_DPOSY  = 232, /* dot y, 0..1 (stable) */
    G_DRAD   = 233, /* dot radius factor, 0..1 (stable) */
};

static int fx_valid(const uint8_t *pixels, int width, int height, int stride)
{
    return pixels != NULL && width > 0 && height > 0 &&
           stride >= width * 3;
}

/* Shared by the pixel->particle effects below: additive soft-ellipse
 * rasterizer. Centre (cx, cy), semi-axes lx, ly (px); falloff
 * a = 1 - (dx/lx)² - (dy/ly)², clamped to >= 0; optionally shaded across
 * the disc (a *= 1 + shx·ex + shy·ey, clamped >= 0) so one edge is the
 * lit side; colour (cr, cg, cb) * a * env added with 255-clamp.
 * Order-independent (additive); +0.5 rounding (§9.5). */
static void add_ellipse(uint8_t *pixels, int width, int height, int stride,
                        float cx, float cy, float lx, float ly,
                        float cr, float cg, float cb, float env,
                        float shx, float shy)
{
    int xa = (int)(cx - lx) - 1;
    int xb = (int)(cx + lx) + 1;
    int ya = (int)(cy - ly) - 1;
    int yb = (int)(cy + ly) + 1;
    if (xa < 0) xa = 0;
    if (ya < 0) ya = 0;
    if (xb >= width)  xb = width - 1;
    if (yb >= height) yb = height - 1;
    if (xa > xb || ya > yb)
        return;

    for (int sy = ya; sy <= yb; sy++) {
        uint8_t *row = pixels + (size_t)sy * (size_t)stride;
        float ey = ((float)sy + 0.5f - cy) / ly;
        float ey2 = ey * ey;
        for (int sx = xa; sx <= xb; sx++) {
            float ex = ((float)sx + 0.5f - cx) / lx;
            float d2 = ex * ex + ey2;
            if (d2 >= 1.0f)
                continue;
            float a = (1.0f - d2) * (1.0f + shx * ex + shy * ey) * env;
            if (a < 0.0f)
                a = 0.0f;
            int add0 = (int)(cr * a + 0.5f);
            int add1 = (int)(cg * a + 0.5f);
            int add2 = (int)(cb * a + 0.5f);
            if (add0 == 0 && add1 == 0 && add2 == 0)
                continue;
            uint8_t *q = row + (size_t)sx * 3;
            int v;
            v = q[0] + add0; q[0] = (uint8_t)(v > 255 ? 255 : v);
            v = q[1] + add1; q[1] = (uint8_t)(v > 255 ? 255 : v);
            v = q[2] + add2; q[2] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}

/* Dot-map paradigm: these effects PROCESS the footage — the base frame is
 * removed (black), and the footage is read as a regular grid of dots whose
 * brightness is gated by local texture/edges: the subject (a textured,
 * edged shape) becomes the dots, flat backgrounds drop to black — a "dot
 * map of the subject" that moves with it. Dot colour is a G-led ramp
 * modulated by the source tone (dot_ramp). The grid is a fixed function of
 * `size` (step = size/2); every cell is kept.
 *
 * Depth pass: local_shade() probes the luma field around each grid point
 * and transfers the local shade into the dot: env and colour are weighted
 * by the sample's luma RELATIVE to its neighbourhood (lit side > shadow
 * side), and the disc itself is shaded — its bright edge faces the brighter
 * neighbour — so the dot field carries the shape's terminator.
 *
 * Order (matters): 1) GATHER grid samples while the pixels are still the
 * originals (reading after the dim pass would kill the local_shade probes
 * and the ramp's luma), 2) remove the base, 3) emit. Fixed iteration order
 * ⇒ deterministic (§5).
 *
 * Common per-pixel rules: grid step = max(2, size/2); disc r = 0.8*step
 * (covers cell corners — no black gaps); dot colour = dot_ramp() * shade,
 * 255-clamped; envelope = shade (flicker flattened, floor lifted). All
 * motion closed form in (seed, pixelID, frame). */

typedef struct {
    float x, y, r;
    float cr, cg, cb;
    float env;
    float shx, shy; /* local shading direction * strength (per unit radius) */
} sprite_t;

/* In-place base dim: out = in * keep. keep >= 0.999 early-outs (untouched);
 * the dot effects pass keep = 0 (base removed — black behind the dots). */
static void fx_dim(uint8_t *pixels, int width, int height, int stride, float keep)
{
    if (keep >= 0.999f)
        return;
    for (int y = 0; y < height; y++) {
        uint8_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            uint8_t *q = row + (size_t)x * 3;
            q[0] = (uint8_t)(q[0] * keep + 0.5f);
            q[1] = (uint8_t)(q[1] * keep + 0.5f);
            q[2] = (uint8_t)(q[2] * keep + 0.5f);
        }
    }
}

/* Emit the gathered sprites additively (after the dim pass). */
static void fx_emit(const sprite_t *spr, int n,
                    uint8_t *pixels, int width, int height, int stride)
{
    for (int i = 0; i < n; i++) {
        const sprite_t s = spr[i];
        add_ellipse(pixels, width, height, stride, s.x, s.y, s.r, s.r,
                    s.cr, s.cg, s.cb, s.env, s.shx, s.shy);
    }
}

/* Per-frame luma plane (width*height floats, row-major): each pixel's
 * luma computed exactly once with the same formula and operation order
 * luma_at() used, so local_shade()'s probes (8 per cell, and per-pixel
 * in dot_spacengrave) load a cached value instead of recomputing.
 * Values are bit-identical to the per-call computation. */
static float *dot_luma_plane(const uint8_t *pixels, int width, int height,
                             int stride)
{
    float *plane = (float *)fx_scratch(2, (size_t)width * (size_t)height * sizeof *plane);
    if (!plane)
        return NULL;
    for (int y = 0; y < height; y++) {
        float *dp = plane + (size_t)y * (size_t)width;
        for (int x = 0; x < width; x++) {
            const uint8_t *q = pixels + (size_t)y * (size_t)stride + (size_t)x * 3;
            dp[x] = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
        }
    }
    return plane;
}

/* Luma of a pixel with clamped coordinates (neighbour probing at the
 * edges), read from the per-frame luma plane. */
static float luma_at(const float *plane, int width, int height,
                     int x, int y)
{
    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    if (y >= height) y = height - 1;
    return plane[(size_t)y * (size_t)width + (size_t)x];
}

/* Local-shade probe ("depth" for the particle field, v5): from the luma
 * field around one grid sample, estimate
 *   shade — the sample's luma relative to its neighbourhood (0.55..1.45;
 *            >1 lit side of the shape, <1 shadow side) → weights env and
 *            colour so the particle field keeps the shape's shading;
 *   shx/shy — the local gradient direction (toward the brighter neighbour)
 *            scaled by its magnitude (≤ 0.5 falloff across the disc) → the
 *            sprite is shaded so its bright edge faces the lit side;
 *   edge    — the local contrast (gradient magnitude) at the near (±step)
 *            and wide (±3*step) probe scales, max of the two → the matrix
 *            mode's subject gate (texture/edges localize the dots on the
 *            subject; flat backgrounds stay black).
 * Local reads only; closed form; deterministic. */
static void local_shade(const float *plane, int width, int height,
                        int sx, int sy, int step, float l0,
                        float *out_shade, float *out_shx, float *out_shy,
                        float *out_edge)
{
    float le = luma_at(plane, width, height, sx + step, sy);
    float lw = luma_at(plane, width, height, sx - step, sy);
    float ls = luma_at(plane, width, height, sx, sy + step);
    float ln = luma_at(plane, width, height, sx, sy - step);

    float gx = le - lw;
    float gy = ls - ln;
    float gmag = sqrtf(fmaf(gx, gx, gy * gy));

    /* matrix subject gate: a wide probe too — a subject is textured at
     * several scales (its features within face distance of every point of
     * it), while a flat background is smooth at every scale. */
    float le2 = luma_at(plane, width, height, sx + 3 * step, sy);
    float lw2 = luma_at(plane, width, height, sx - 3 * step, sy);
    float ls2 = luma_at(plane, width, height, sx, sy + 3 * step);
    float ln2 = luma_at(plane, width, height, sx, sy - 3 * step);

    float gx2 = le2 - lw2;
    float gy2 = ls2 - ln2;
    float gmag2 = sqrtf(fmaf(gx2, gx2, gy2 * gy2));

    float lm = (l0 + le + lw + ls + ln) * 0.2f;

    float shade = l0 / (lm + 0.004f);
    if (shade < 0.55f) shade = 0.55f;
    if (shade > 1.45f) shade = 1.45f;

    float k = (gmag < 1.0f ? gmag : 1.0f) * 0.5f;
    float shx = 0.0f, shy = 0.0f;
    if (gmag > 0.001f) {
        shx = (gx / gmag) * k;
        shy = (gy / gmag) * k;
    }
    *out_shade = shade;
    *out_shx = shx;
    *out_shy = shy;
    *out_edge = (gmag > gmag2 ? gmag : gmag2);
}

/* gate_ramp — the new dot.gate subject gate: BINARY — on iff local
 * contrast reaches `knee` AND the cell is on the subject side of the
 * Otsu split (subject tested at the dot's FINAL center in gate_cell),
 * absent otherwise — the partial ramp left a faint contour (the
 * "halo") and bled past the silhouette. The colour ramp is dot_ramp's
 * (green-led), pf replacing its soft knee. */
static void gate_ramp(float shade, float luma, float edge, float knee,
                      int subject, float *cr, float *cg, float *cb)
{
    float pf = (edge >= knee && subject) ? 1.0f : 0.0f;
    float g = 382.5f * (0.35f + 0.65f * luma) * pf;
    float pr = g * (0.50f + 0.26f * luma);
    float pg = g;
    float pb = g * (0.50f + 0.30f * luma);
    *cr = pr * shade;
    *cg = pg * shade;
    *cb = pb * shade;
}

/* dot.gate: the footage read as a dot grid — the base is removed, and the
 * frame is re-emitted as a subject-gated dot map (dot_ramp over the black
 * base): the subject becomes the dots and moves with it. No rise, no slice
 * jumps — only a slow per-dot wobble (per-pixel twinkle flattened to a
 * static map; motion comes from the wobble only).
 *
 *  base:    removed (black behind the dots)
 *  grid:    step = max(2, size/2);  r = step*0.8 (covers cell corners,
 *           no black gaps)
 *  depth:   local_shade(): shade = clamp(l0/local-mean, 0.55..1.45)
 *           → env = shade, colour *= shade; disc shaded toward the
 *           brighter neighbour (shape's 3D read); edge = local contrast
 *           (near ±step / wide ±3*step gradient, max) → subject gate
 *  pixel:   id = sy*8192 + sx:
 *           dy = sin(2πf/60 + 2πR(0, id, SLOT_PHASE)) * (1 + 0.2*speed)
 *           dx = (R(f/4, id, SLOT_SWAYP) - 0.5) * (1 + 0.2*speed)
 *  colour:  gate_ramp() * shade, 255-clamped — binary gate: full at
 *           `gate` knee inside the Otsu subject region (minority side),
 *           absent otherwise
 *  speed:   wobble amplitude scale in px (0 = static map)
 *
 * size < 1, speed < 0 or gate < 1 is byte-identity.
 */

/* Per-cell dotgate sprite. Shared by dotgate() and the debug sprdump —
 * the MSL mirror is gather_gate in metal_fx.m (lockstep). */
static int gate_cell(const float *plane, int width, int height,
                     int sx, int sy, int step, float luma,
                     const dotgate_params_t *params,
                     int split, int bright,
                     int frame,
                     sprite_t *s)
{
    float f = (float)frame;
    float r = (float)step * 0.8f; /* covers cell corners (no black gaps) */
    float wob = 1.0f + 0.2f * (float)params->speed;
    uint32_t ts = (uint32_t)(frame / 4); /* sway clock */

    uint32_t id = (uint32_t)sy * 8192u + (uint32_t)sx;

    float shade, shx, shy, edge;
    local_shade(plane, width, height, sx, sy, step, luma,
                &shade, &shx, &shy, &edge);

    float env = shade; /* per-cell twinkle flattened; shade clamped 0.55..1.45 */
    float dy = fx_sinf(2.0f * FX_PI * f / 60.0f +
                       2.0f * FX_PI * fx_rand01(HOLOGRAM_SEED, 0, id, SLOT_PHASE)) * wob;
    float dx = (fx_rand01(HOLOGRAM_SEED, ts, id, SLOT_SWAYP) - 0.5f) * wob;

    s->x = (float)sx + dx;
    s->y = (float)sy + dy;
    s->r = r;
    s->shx = shx;
    s->shy = shy;
    /* Subject region at the FINAL center: the wobble can push a dot's
     * center off its sample, and a sample-center test left a faint
     * contour. T = split * 2^-8 is exact; an OOB final center is not
     * subject (identical guard in the MSL mirror — no read there). */
    float knee = fmaf((float)params->gate, 0.01f, 0.0f);
    float T = fmaf((float)split, 1.0f / 256.0f, 0.0f);
    int fcx = (int)s->x;
    int fcy = (int)s->y;
    int subject = 0;
    if (fcx >= 0 && fcy >= 0 && fcx < width && fcy < height) {
        float lc = luma_at(plane, width, height, fcx, fcy);
        subject = bright ? (lc > T) : (lc < T);
    }
    gate_ramp(shade, luma, edge, knee, subject, &s->cr, &s->cg, &s->cb);
    if (s->cr > 255.0f) s->cr = 255.0f;
    if (s->cg > 255.0f) s->cg = 255.0f;
    if (s->cb > 255.0f) s->cb = 255.0f;
    s->env = env;
    return 1;
}

void dotgate(uint8_t *pixels, int width, int height, int stride,
             const dotgate_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->size < 1 || params->speed < 0 || params->gate < 1)
        return;

    if (frame < 0) frame = 0;

    /* Subject region (host-side, shared by both backends). */
    int bright = 0;
    int split = portal_split(pixels, width, height, stride, &bright);

    int step = params->size / 2;
    if (step < 2) step = 2;

    const float *plane = dot_luma_plane(pixels, width, height, stride);
    if (!plane)
        return; /* cannot allocate; leave buffer untouched */

    int cols = (width + step - 1) / step;
    int rows = (height + step - 1) / step;
    sprite_t *spr = (sprite_t *)fx_scratch(0, (size_t)cols * (size_t)rows * sizeof(sprite_t));
    if (!spr)
        return; /* nothing written yet — frame untouched */
    /* 1) gather from the ORIGINAL pixels. Each cell's sprite is a pure
     * function of its own (sx, sy) plus read-only inputs (gate_cell
     * always returns 1), so rows are independent and parallelize over
     * dispatch_apply; each cell writes a precomputed slot (row * cols_a +
     * col) — the exact row-major order of the old running counter, so
     * fx_emit() sees the identical array. (Per-cell work is sub-microsecond,
     * so parallelize over rows, not cells — per-cell items were slower.) */
    int cols_a = (step / 2 < width) ? (width - 1 - step / 2) / step + 1 : 0;
    int rows_a = (step / 2 < height) ? (height - 1 - step / 2) / step + 1 : 0;
    int n = rows_a * cols_a;
    if (rows_a > 1) {
        dispatch_apply(rows_a, DISPATCH_APPLY_AUTO, ^(size_t k) {
            int sy = step / 2 + (int)k * step;
            const uint8_t *row = pixels + (size_t)sy * (size_t)stride;
            for (int j = 0; j < cols_a; j++) {
                int sx = step / 2 + j * step;
                const uint8_t *q = row + (size_t)sx * 3;
                float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
                sprite_t s;
                gate_cell(plane, width, height, sx, sy, step, luma,
                          params, split, bright, frame, &s);
                spr[k * cols_a + j] = s;
            }
        });
    } else {
        for (int k = 0; k < rows_a; k++) {
            int sy = step / 2 + k * step;
            const uint8_t *row = pixels + (size_t)sy * (size_t)stride;
            for (int j = 0; j < cols_a; j++) {
                int sx = step / 2 + j * step;
                const uint8_t *q = row + (size_t)sx * 3;
                float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
                sprite_t s;
                gate_cell(plane, width, height, sx, sy, step, luma,
                          params, split, bright, frame, &s);
                spr[k * cols_a + j] = s;
            }
        }
    }

    /* 2) remove the base (black behind the dots), 3) emit the field. */
    fx_dim(pixels, width, height, stride, 0.0f);
    fx_emit(spr, n, pixels, width, height, stride);
}

/* Debug (dual hunt): per-cell gather in tid order, 10 floats per cell
 * (x, y, r, cr, cg, cb, env, shx, shy, valid). Same layout as the MSL
 * Sprite struct in metal_fx.m. For byte comparison
 * against metal_dotgate_sprdump(). */
void dotgate_sprdump(const uint8_t *pixels, int width, int height, int stride,
                     const dotgate_params_t *params,
                     int frame, float *out)
{
    if (pixels == NULL || params == NULL || out == NULL)
        return;
    if (params->size < 1 || params->speed < 0 || params->gate < 1)
        return; /* out untouched */
    if (frame < 0) frame = 0;

    /* Subject region — same host-side split as dotgate(). */
    int bright = 0;
    int split = portal_split(pixels, width, height, stride, &bright);

    int step = params->size / 2;
    if (step < 2) step = 2;
    int cols = (width + step - 1) / step;
    int rows = (height + step - 1) / step;
    const float *plane = dot_luma_plane(pixels, width, height, stride);
    if (!plane)
        return; /* out untouched */
    for (int tid = 0; tid < cols * rows; tid++) {
        float *o = out + (size_t)tid * 10;
        int sy = step / 2 + (tid % rows) * step;
        int sx = step / 2 + (tid / rows) * step;
        const uint8_t *q = pixels + (size_t)sy * (size_t)stride + (size_t)sx * 3;
        float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
        sprite_t s;
        if (gate_cell(plane, width, height, sx, sy, step, luma,
                      params, split, bright, frame, &s)) {
            o[0] = s.x; o[1] = s.y; o[2] = s.r;
            o[3] = s.cr; o[4] = s.cg; o[5] = s.cb;
            o[6] = s.env; o[7] = s.shx; o[8] = s.shy; o[9] = 1.0f;
        } else {
            for (int i = 0; i < 10; i++)
                o[i] = 0.0f;
        }
    }
}

/* dot.portal: the subject turned into a population of discrete dots.
 *
 * Shares dotgate's grid, subject gate and colour model; differs in the
 * three things that make dots read as particles rather than as a screen:
 *
 *  separation: r = step * (fill/100) * rf.  fill = 80 reproduces
 *              dotgate's gap-free r = 0.8*step; the default 35 leaves
 *              real black between neighbours — THE defining difference.
 *  halftone:   rf = hs*luma + (1 - hs), clamped [0.05, 1] — radius
 *              tracks the cell's tone, so lit surfaces fatten and shadow
 *              shrinks toward nothing (the 3D read).
 *  relief:     x = sx + shx*relf, y = sy + shy*relf, relf =
 *              (relief/100)*step — dots pushed along the local luma
 *              gradient (toward the brighter neighbour), so the lattice
 *              deforms into bands following the form.
 *
 * Gate is a BINARY subject gate here (dot_ramp's is fixed at 0.10 and
 * shared with dotgate — untouchable), so portal_ramp() below is the
 * portal-specific ramp. Dots are full where local contrast reaches the
 * knee AND the cell lies in the subject region (Otsu minority side),
 * absent otherwise — partial dots left a visible faint outline (the
 * "contour"), and the contrast gate alone bleeds 1-2 cells past the
 * silhouette (the "halo").
 *
 * base:   removed (black behind the dots) — same as dotgate
 * grid:   step = max(2, size/2) — same as dotgate
 * depth:  local_shade() — same as dotgate; its shx/shy now drive the
 *         displacement as well as the disc shading
 * colour: portal_ramp() * shade; hue = `color` (default 360 = white;
 *         0..359 = hue at fixed 0.60 saturation — the old 0.37 pastel
 *         muted the primaries (red→salmon, green→mint, blue→
 *         periwinkle); 0.60 gives bright primaries and leaves white
 *         (360) untouched, since all its weights are 1; hue-
 *         preserving clamp at level overflow
 * glow:   optional aura layer — each dot also draws a larger, dimmer
 *         disc behind it (radius x(1+g/100), colour x g/400), g = `glow`
 *
 * Time-invariant: no wobble, no per-frame hash. `frame` is unused.
 * size < 1 is byte-identity.
 */

/* Radius floor: add_ellipse divides by lx/ly, so keep it positive. */
#define DOT_PORTAL_R_MIN 0.01f

int portal_split(const uint8_t *pixels, int width, int height, int stride,
                 int *bright)
{
    unsigned long hist[256];
    int i;
    for (i = 0; i < 256; i++)
        hist[i] = 0;
    for (int y = 0; y < height; y++) {
        const uint8_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            const uint8_t *q = row + (size_t)x * 3;
            float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
            int b = (int)(luma * 256.0f);
            if (b > 255) b = 255;
            hist[b]++;
        }
    }
    unsigned long N = 0, tm = 0;
    for (i = 0; i < 256; i++) {
        N += hist[i];
        tm += hist[i] * (unsigned long)i;
    }
    /* Otsu: maximise between-class variance; integer prefix sums, the
     * float compare is a single code path (both backends share it). */
    unsigned long w0 = 0, m0 = 0, best = 0;
    int best_i = 128;
    for (i = 0; i < 255; i++) {
        w0 += hist[i];
        m0 += hist[i] * (unsigned long)i;
        unsigned long w1 = N - w0;
        if (w0 == 0 || w1 == 0)
            continue;
        float mu0 = (float)m0 / (float)w0;
        float mu1 = (float)(tm - m0) / (float)w1;
        float d = mu0 - mu1;
        float v = (float)w0 * (float)w1 * d * d;
        if (v > best) {
            best = v;
            best_i = i;
        }
    }
    if (bright != NULL) {
        unsigned long dark = 0;
        for (i = 0; i < best_i; i++)
            dark += hist[i];
        *bright = (dark * 2 > N) ? 1 : 0; /* minority side = subject */
    }
    return best_i;
}

/* Hue (0..360; 360 = white) to unit wheel colours (max channel 1).
 * Closed form; same branch structure as the MSL mirror in gather_portal.
 * Shared with core/metal_fx.m (declared in dot.h) — single definition. */
void portal_hue(float hue, float *r1, float *g1, float *b1)
{
    if (hue < 0.0f) hue = 0.0f;
    if (hue >= 360.0f) { *r1 = 1.0f; *g1 = 1.0f; *b1 = 1.0f; return; }
    float h6 = fmaf(hue, 1.0f / 60.0f, 0.0f);
    int i = (int)h6;
    if (i > 5) i = 5;
    float fr = fmaf(h6, 1.0f, -(float)i);
    if (i == 0)      { *r1 = 1.0f; *g1 = fr;        *b1 = 0.0f; }
    else if (i == 1) { *r1 = 1.0f; *g1 = 1.0f;      *b1 = fr; }
    else if (i == 2) { *r1 = 0.0f; *g1 = 1.0f;      *b1 = fr; }
    else if (i == 3) { *r1 = 0.0f; *g1 = fr;        *b1 = 1.0f; }
    else if (i == 4) { *r1 = fr;   *g1 = 0.0f;      *b1 = 1.0f; }
    else             { *r1 = 1.0f; *g1 = 0.0f;      *b1 = fr; }
}

/* Portal ramp: binary subject gate (full at knee, absent below — the
 * subject test is done at the dot's FINAL center, see portal_cell) x
 * tone ramp x hue weights, then x shade. knee > 0, hue 0..360.
 * Saturation fixed at 0.60 (the 0.37 pastel muted primaries — user
 * wanted bright red/green/blue; white is unaffected: all its weights
 * are 1, so wr = wg = wb = 1 at any saturation). */
static void portal_ramp(float shade, float luma, float edge, float knee,
                        int hue, int hue2, int subject,
                        float *cr, float *cg, float *cb)
{
    float pf = (edge >= knee && subject) ? 1.0f : 0.0f;
    float g = 382.5f * (0.35f + 0.65f * luma) * pf;
    float r1, g1, b1;
    portal_hue((float)hue, &r1, &g1, &b1);
    const float sat = 0.60f;
    float wr = fmaf(r1, sat, 1.0f - sat);
    float wg = fmaf(g1, sat, 1.0f - sat);
    float wb = fmaf(b1, sat, 1.0f - sat);
    if (hue2 != hue) {
        /* Duotone: pull the hue weights toward the highlight hue by the
         * dot's shade (local_shade clamps it to 0.55..1.45), so t = 0 at
         * the shadow end (full `hue`), 1 at the lit end (full `hue2`).
         * hue2 == hue takes the untouched path above — bit-identical to
         * the flat-hue ramp. fma-pinned, MSL mirror in gather_portal. */
        float r2, g2, b2;
        portal_hue((float)hue2, &r2, &g2, &b2);
        float wr2 = fmaf(r2, sat, 1.0f - sat);
        float wg2 = fmaf(g2, sat, 1.0f - sat);
        float wb2 = fmaf(b2, sat, 1.0f - sat);
        const float inv = 1.0f / 0.9f;
        float t = fmaf(shade, inv, -0.55f * inv);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        wr = fmaf(t, wr2 - wr, wr);
        wg = fmaf(t, wg2 - wg, wg);
        wb = fmaf(t, wb2 - wb, wb);
    }
    float pr = fmaf(g, wr, 0.0f);
    float pg = fmaf(g, wg, 0.0f);
    float pb = fmaf(g, wb, 0.0f);
    *cr = pr * shade;
    *cg = pg * shade;
    *cb = pb * shade;
}

/* Per-cell dotportal sprite. Shared by dotportal() and the debug
 * sprdump — the MSL mirror is gather_portal in metal_fx.m (lockstep). */
static int portal_cell(const float *plane, int width, int height,
                       int sx, int sy, int step, float luma,
                       const dotportal_params_t *params,
                       int split, int bright,
                       sprite_t *s)
{
    float shade, shx, shy, edge;
    local_shade(plane, width, height, sx, sy, step, luma,
                &shade, &shx, &shy, &edge);

    /* halftone: radius factor from the cell's own tone. */
    float hs = fmaf((float)params->halftone, 0.01f, 0.0f);
    float rf = fmaf(hs, luma, 1.0f - hs);
    if (rf < 0.05f) rf = 0.05f;
    if (rf > 1.0f) rf = 1.0f;

    /* crisp (2.3): shrink dots at high local contrast — silhouette cells
     * (high edge) draw smaller, soft interiors (low edge) stay full.
     * k = crisp/100 * e01 (the gate's 0..1 contrast idiom); rf *= (1-k)
     * as one fma. crisp = 0 leaves rf bit-exactly as the halftone ramp
     * produced it (the whole block is dead). MSL mirror in gather_portal. */
    if (params->crisp > 0) {
        float crisp01 = fmaf((float)params->crisp, 0.01f, 0.0f);
        float e01 = edge * 4.0f;
        if (e01 > 1.0f) e01 = 1.0f;
        float k = fmaf(crisp01, e01, 0.0f);
        rf = fmaf(k, -rf, rf);
    }

    /* separation: radius as a fraction of the grid step. */
    float fill01 = fmaf((float)params->fill, 0.01f, 0.0f);
    float rbase = fmaf(fill01, (float)step, 0.0f);
    float r = fmaf(rbase, rf, 0.0f);
    if (r < DOT_PORTAL_R_MIN) r = DOT_PORTAL_R_MIN;

    /* relief: displace along the local luma gradient, in px. */
    float rel01 = fmaf((float)params->relief, 0.01f, 0.0f);
    float relf = fmaf(rel01, (float)step, 0.0f);

    float knee = fmaf((float)params->gate, 0.01f, 0.0f);

    s->x = fmaf(shx, relf, (float)sx);
    s->y = fmaf(shy, relf, (float)sy);
    s->r = r;
    s->shx = shx;
    s->shy = shy;
    /* Subject region at the FINAL center: relief can push a dot's center
     * past the silhouette, and a sample-center test left a thin halo.
     * T = split * 2^-8 is exact; an OOB final center is not subject
     * (identical guard in the MSL mirror — no read there). */
    float T = fmaf((float)split, 1.0f / 256.0f, 0.0f);
    int fcx = (int)s->x;
    int fcy = (int)s->y;
    int subject = 0;
    if (fcx >= 0 && fcy >= 0 && fcx < width && fcy < height) {
        float lc = luma_at(plane, width, height, fcx, fcy);
        subject = bright ? (lc > T) : (lc < T);
    }
    portal_ramp(shade, luma, edge, knee, params->color, params->color2,
                subject, &s->cr, &s->cg, &s->cb);
    /* level: brightness gain (100 = 1x) — pinned multiply, MSL mirror
     * writes fma(pr*shade, lv, 0) so neither compiler re-associates. */
    float lv = fmaf((float)params->level, 0.01f, 0.0f);
    s->cr = fmaf(s->cr, lv, 0.0f);
    s->cg = fmaf(s->cg, lv, 0.0f);
    s->cb = fmaf(s->cb, lv, 0.0f);
    /* Hue-preserving clamp: scale the whole dot so its brightest channel
     * reaches 255. A plain per-channel clamp would wash the hue to white
     * at higher level, making `color` invisible. */
    float m = s->cr;
    if (s->cg > m) m = s->cg;
    if (s->cb > m) m = s->cb;
    if (m > 255.0f) {
        float sc = 255.0f / m;
        s->cr *= sc;
        s->cg *= sc;
        s->cb *= sc;
    }
    s->env = shade; /* flicker flattened; shade clamped 0.55..1.45 — as dotgate */
    return 1;
}

void dotportal(uint8_t *pixels, int width, int height, int stride,
               const dotportal_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->size < 1 || params->fill < 0 || params->gate < 1 ||
        params->halftone < 0 || params->relief < 0 || params->level < 0 ||
        params->color < 0 || params->glow < 0)
        return;
    (void)frame; /* reserved: the dot field is time-invariant */

    /* Subject region (host-side, shared by both backends). */
    int bright = 0;
    int split = portal_split(pixels, width, height, stride, &bright);

    int step = params->size / 2;
    if (step < 2) step = 2;

    const float *plane = dot_luma_plane(pixels, width, height, stride);
    if (!plane)
        return; /* cannot allocate; leave buffer untouched */

    int cols = (width + step - 1) / step;
    int rows = (height + step - 1) / step;
    sprite_t *spr = (sprite_t *)fx_scratch(0, (size_t)cols * (size_t)rows * sizeof(sprite_t));
    if (!spr)
        return; /* nothing written yet — frame untouched */
    /* 1) gather from the ORIGINAL pixels — same row-parallel structure
     * as dotgate(): independent rows over dispatch_apply, each cell
     * writing its precomputed (row * cols_a + col) slot in the exact
     * row-major order of the old running counter. */
    int cols_a = (step / 2 < width) ? (width - 1 - step / 2) / step + 1 : 0;
    int rows_a = (step / 2 < height) ? (height - 1 - step / 2) / step + 1 : 0;
    int n = rows_a * cols_a;
    if (rows_a > 1) {
        dispatch_apply(rows_a, DISPATCH_APPLY_AUTO, ^(size_t k) {
            int sy = step / 2 + (int)k * step;
            const uint8_t *row = pixels + (size_t)sy * (size_t)stride;
            for (int j = 0; j < cols_a; j++) {
                int sx = step / 2 + j * step;
                const uint8_t *q = row + (size_t)sx * 3;
                float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
                sprite_t s;
                portal_cell(plane, width, height, sx, sy, step, luma,
                            params, split, bright, &s);
                spr[k * cols_a + j] = s;
            }
        });
    } else {
        for (int k = 0; k < rows_a; k++) {
            int sy = step / 2 + k * step;
            const uint8_t *row = pixels + (size_t)sy * (size_t)stride;
            for (int j = 0; j < cols_a; j++) {
                int sx = step / 2 + j * step;
                const uint8_t *q = row + (size_t)sx * 3;
                float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
                sprite_t s;
                portal_cell(plane, width, height, sx, sy, step, luma,
                            params, split, bright, &s);
                spr[k * cols_a + j] = s;
            }
        }
    }

    /* 2) remove the base (black behind the dots), 3) emit the field.
     * Glow layer first (aura behind the dots): a larger, dimmer disc per
     * sprite. Metal mirrors this as a second sprite layer in gather_portal
     * (integer accumulation is order-independent, so layer order is safe). */
    fx_dim(pixels, width, height, stride, 0.0f);
    float gl01 = fmaf((float)params->glow, 0.01f, 0.0f);
    if (gl01 > 0.0f) {
        float rs = fmaf(gl01, 1.0f, 1.0f); /* 1 + g/100 */
        float cs = fmaf(0.25f, gl01, 0.0f); /* g/400 */
        for (int i = 0; i < n; i++) {
            const sprite_t s = spr[i];
            float rg = fmaf(s.r, rs, 0.0f);
            add_ellipse(pixels, width, height, stride, s.x, s.y, rg, rg,
                        fmaf(s.cr, cs, 0.0f), fmaf(s.cg, cs, 0.0f),
                        fmaf(s.cb, cs, 0.0f), s.env, s.shx, s.shy);
        }
    }
    fx_emit(spr, n, pixels, width, height, stride);
}


/* dot.spacengrave — engraved scanline OVERLAY (plan/spacengrave.md).
 * The source picture stays fully visible; a line-screen of additive
 * near-white ink is laid OVER it, and the source beneath is dimmed by
 * `dim` (0 = photo fully visible, 100 = pure black behind, as in
 * dot.gate / dot.portal).
 *
 * Vertical period only (pitch = size px): within each period the stroke
 * (fill/100 of the pitch) is centred at 1/4 of it, so a thin stroke
 * leaves the larger gap below it (the reference's asymmetric row
 * profile). Per pixel on the stroke:
 *   cov = line/100  (baseline solidity)
 *       - grain/100 * (edge - 0.5)   (texture breaks the line: the face
 *                                     fragments, flat sky stays solid)
 *       + halftone/100 * (l0 - 0.5)  (tone shading: darker breaks up more)
 * and cov is dithered per (x, y) pixel with fx_rand01 (time-invariant,
 * no frame term), so partial coverage renders as IRREGULAR dashes, not
 * a uniformly dim line. The per-y dither also stops the dashes from
 * correlating into vertical bars. That dither is what makes the
 * "dollar bill".
 *
 * `gate` (0 = off, 1..100 = knee x100) restricts the ink to the
 * subject, with the SAME portal_split region + contrast-knee test that
 * dot.gate / dot.portal use — so on a dimmed/black background the
 * engraved subject stands on its own: dots-for-lines, the pattern IS
 * the picture.
 *
 * Ink = additive, hue-tinted (portal_hue at sat 0.60, as in v1),
 * level-scaled, 255-clipped — and its brightness tracks local tone
 * (0.15..1.0 with luma), so on the dimmed/black background the lines
 * themselves carry the shading: lit areas glow, shadow areas dim,
 * the way dot.portal's dots ARE the picture. Edge/contrast come from local_shade() —
 * the same probe dot.gate/dot.portal use for their subject gate.
 *
 * Pitfall (plan §5.1): luma_at()/local_shade() probe neighbours up to
 * 3*probe px away, and the effect writes in place — once row y is inked,
 * row y+1's probe would read ink, not source, and the engraving would
 * feed back on itself. So all reads go to a private copy of the source;
 * writes go to pixels.
 *
 * size = 0: identity pass-through (a genuine untouched frame). */
void dot_spacengrave(uint8_t *pixels, int width, int height, int stride,
                     const dot_spacengrave_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->fill < 0 || params->halftone < 0 || params->line < 0 ||
        params->level < 0 || params->grain < 0 || params->color < 0 ||
        params->dim < 0 || params->gate < 0 || params->texture < 0 ||
        params->contour < 0)
        return; /* invalid params: leave the frame untouched */
    if (params->size < 2)
        return; /* 0 = off (identity); 1 = sub-pixel pitch, no sensible ink */
    (void)frame; /* reserved: the field is time-invariant */

    const size_t nb = (size_t)width * (size_t)height * 3;
    uint8_t *src = fx_scratch(1, nb);
    if (!src)
        return; /* nothing written yet — frame untouched */
    memcpy(src, pixels, nb);

    const float *plane = dot_luma_plane(pixels, width, height, stride);
    if (!plane)
        return; /* cannot allocate; leave buffer untouched */

    int pitch = params->size;
    int probe = pitch / 2;
    if (probe < 1) probe = 1;
    float half = (float)params->fill * 0.005f; /* stroke half-width, /period */
    float inv_pitch = 1.0f / (float)pitch;

    /* Subject gate (same portal_split as dot.gate / dot.portal): the
     * split is computed on the SOURCE copy before any ink is written. */
    int gate_on = params->gate >= 1;
    int bright = 0;
    float T = 0.0f;
    float knee = 0.0f;
    if (gate_on) {
        int split = portal_split(src, width, height, stride, &bright);
        T = (float)split / 256.0f;
        knee = (float)params->gate * 0.01f;
    }

    float r1, g1, b1;
    portal_hue((float)params->color, &r1, &g1, &b1);
    const float sat = 0.60f; /* same fixed saturation as portal_ramp */
    float wr = r1 * sat + (1.0f - sat);
    float wg = g1 * sat + (1.0f - sat);
    float wb = b1 * sat + (1.0f - sat);
    float lv = (float)params->level * 0.01f;    /* 0..2 */
    float bias = (float)params->line * 0.01f;   /* 0..1 */
    float grain01 = (float)params->grain * 0.01f; /* 0..1 */
    float half01 = (float)params->halftone * 0.01f; /* 0..1 */
    float keep = 1.0f - (float)params->dim * 0.01f; /* 1 = photo, 0 = black */

    /* Second dash layer (2.2): half pitch, half stroke width, same 1/4
     * centring and grain/halftone response; baseline coverage = the
     * `texture` dial. texture = 0 → tri2 stays 0 and every second-layer
     * block below is dead code — output byte-identical to the
     * single-layer effect. */
    float tex01 = (float)params->texture * 0.01f;
    int pitch2 = pitch / 2;
    if (pitch2 < 2) pitch2 = 2;
    float half2 = half * 0.5f;
    float inv_pitch2 = 1.0f / (float)pitch2;

    /* Contour tilt (2.4): blend the stroke axis from the row toward the
     * local iso-luma contour. contour = 0 → c01 = 0 → the per-pixel
     * blend below is dead (fma(0, x, y) == y exactly) and the loop
     * keeps the per-row envelope — byte-identical to the pre-2.4 code. */
    float c01 = (float)params->contour * 0.01f; /* 0..1 */

    /* dim: GLOBAL source removal — the dot.gate / dot.portal idiom
     * ("the base is removed (black behind the dots)"): the WHOLE frame
     * is dimmed, not just the inked pixels. Dimming only inside the ink
     * write would leave the photo fully visible everywhere the line is
     * dashed off or the stroke is absent — the "scanlines over the
     * photo" overlay look. keep = 1.0 is exact, so dim = 0 skips the
     * pass and stays bit-identical to the photo-visible render. */
    if (keep < 1.0f) {
        for (size_t i = 0; i < nb; i++)
            pixels[i] = (uint8_t)((float)pixels[i] * keep + 0.5f);
    }

    for (int y = 0; y < height; y++) {
        /* Stroke envelope. contour 0 (default): the phase axis is the
         * row — computed once per row, and a row off both strokes is
         * skipped entirely (the pre-2.4 fast path, byte-identical).
         * contour > 0 (2.4): the axis tilts toward the local iso-luma
         * contour, so the envelope is per-pixel (from shx/shy below). */
        float row_tri = 0.0f, row_tri2 = 0.0f;
        if (c01 == 0.0f) {
            float phase = (float)(y % pitch) * inv_pitch;
            float d = fabsf(phase - 0.25f); /* stroke centred at 1/4 of period */
            row_tri = (d >= half) ? 0.0f : (1.0f - d / half);
            if (tex01 > 0.0f) {
                float phase2 = (float)(y % pitch2) * inv_pitch2;
                float d2 = fabsf(phase2 - 0.25f);
                row_tri2 = (d2 >= half2) ? 0.0f : (1.0f - d2 / half2);
            }
            if (row_tri <= 0.0f && row_tri2 <= 0.0f)
                continue; /* off both strokes: untouched source */
        }
        uint8_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            float l0 = luma_at(plane, width, height, x, y);
            float shade, shx, shy, edge;
            local_shade(plane, width, height, x, y, probe, l0,
                        &shade, &shx, &shy, &edge);
            (void)shade;
            float tri, tri2;
            if (c01 == 0.0f) {
                tri = row_tri;
                tri2 = row_tri2;
            } else {
                /* Contour (2.4): the phase coordinate u blends the row
                 * (y) toward the gradient-perpendicular coordinate
                 * (shx*fy - shy*x, the iso-luma contour direction).
                 * Flat (n == 0) keeps the axis vertical. fmod matches
                 * (y % pitch) exactly at u == y. */
                float fy = (float)y;
                float u = fy;
                float n = sqrtf(fmaf(shx, shx, fmaf(shy, shy, 0.0f)));
                if (n > 0.0f) {
                    float u_c = fmaf(shx / n, fy,
                                     fmaf(-(shy / n), (float)x, 0.0f));
                    u = fmaf(c01, u_c - fy, fy);
                }
                float phase = fmodf(u, (float)pitch);
                if (phase < 0.0f) phase += (float)pitch;
                phase *= inv_pitch;
                float d = fabsf(phase - 0.25f);
                tri = (d >= half) ? 0.0f : (1.0f - d / half);
                tri2 = 0.0f;
                if (tex01 > 0.0f) {
                    float phase2 = fmodf(u, (float)pitch2);
                    if (phase2 < 0.0f) phase2 += (float)pitch2;
                    phase2 *= inv_pitch2;
                    float d2 = fabsf(phase2 - 0.25f);
                    tri2 = (d2 >= half2) ? 0.0f : (1.0f - d2 / half2);
                }
                if (tri <= 0.0f && tri2 <= 0.0f)
                    continue; /* off both strokes: untouched source */
            }
            float e = edge * 4.0f;
            if (e > 1.0f) e = 1.0f; /* 0..1 local contrast */

            /* Subject gate: both layers ink only where local contrast
             * reaches the knee AND the pixel is on the subject side of
             * the split — the exact portal_ramp pf test. Background
             * pixels pass through (dimmed, no ink). */
            int pass = 1;
            if (gate_on)
                pass = (e >= knee) && (bright ? (l0 > T) : (l0 < T));
            if (!pass)
                continue;

            float cov = bias
                      - grain01 * (e - 0.5f)
                      + half01 * (l0 - 0.5f);
            if (cov < 0.0f) cov = 0.0f;
            if (cov > 1.0f) cov = 1.0f;

            /* Ordered-look dither per pixel: partial coverage becomes an
             * irregular on/off dash pattern, deterministic in (x, y) —
             * no frame term, so the effect stays time-invariant. The
             * y term keeps each row's dashes independent (an x-only
             * dither correlates them into vertical bars). */
            float t = fx_rand01(0u, 0u, (uint32_t)x, (uint32_t)y);
            int ar = 0, ag = 0, ab = 0;
            if (cov >= t) { /* dashed on at this x */
                float ink = 255.0f * tri * lv * (0.15f + 0.85f * l0);
                ar = (int)(ink * wr + (ink * wr >= 0.0f ? 0.5f : -0.5f));
                ag = (int)(ink * wg + (ink * wg >= 0.0f ? 0.5f : -0.5f));
                ab = (int)(ink * wb + (ink * wb >= 0.0f ? 0.5f : -0.5f));
                if (ar < 0) ar = 0;
                if (ag < 0) ag = 0;
                if (ab < 0) ab = 0;
            }
            /* Second layer: its own dither seed, its own baseline coverage
             * (`texture`%), same grain/halftone response and ink model —
             * additive under the same 255-clip (integer sums, exact). */
            int br2 = 0, bg2 = 0, bb2 = 0;
            if (tex01 > 0.0f && tri2 > 0.0f) {
                float cov2 = tex01
                           - grain01 * (e - 0.5f)
                           + half01 * (l0 - 0.5f);
                if (cov2 < 0.0f) cov2 = 0.0f;
                if (cov2 > 1.0f) cov2 = 1.0f;
                float t2 = fx_rand01(1u, 0u, (uint32_t)x, (uint32_t)y);
                if (cov2 >= t2) {
                    float ink2 = 255.0f * tri2 * lv * (0.15f + 0.85f * l0);
                    br2 = (int)(ink2 * wr + (ink2 * wr >= 0.0f ? 0.5f : -0.5f));
                    bg2 = (int)(ink2 * wg + (ink2 * wg >= 0.0f ? 0.5f : -0.5f));
                    bb2 = (int)(ink2 * wb + (ink2 * wb >= 0.0f ? 0.5f : -0.5f));
                    if (br2 < 0) br2 = 0;
                    if (bg2 < 0) bg2 = 0;
                    if (bb2 < 0) bb2 = 0;
                }
            }
            uint8_t *q = row + (size_t)x * 3;
            int v;
            /* the frame is already globally dimmed (keep) — add the ink
             * (primary + second layer) and clip at 255. */
            v = (int)q[0] + ar + br2; q[0] = (uint8_t)(v > 255 ? 255 : v);
            v = (int)q[1] + ag + bg2; q[1] = (uint8_t)(v > 255 ? 255 : v);
            v = (int)q[2] + ab + bb2; q[2] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
}
/* Debug (dual hunt): per-cell gather in tid order, 10 floats per cell
 * (x, y, r, cr, cg, cb, env, shx, shy, valid). Same layout as the MSL
 * Sprite struct in metal_fx.m. For byte comparison against
 * metal_dotportal_sprdump(). */
void dotportal_sprdump(const uint8_t *pixels, int width, int height, int stride,
                       const dotportal_params_t *params,
                       int frame, float *out)
{
    if (pixels == NULL || params == NULL || out == NULL)
        return;
    if (params->size < 1 || params->fill < 0 || params->gate < 1 ||
        params->halftone < 0 || params->relief < 0 || params->level < 0 ||
        params->color < 0 || params->glow < 0)
        return; /* out untouched */
    (void)frame;

    /* Subject region — same host-side split as dotportal(). */
    int bright = 0;
    int split = portal_split(pixels, width, height, stride, &bright);

    int step = params->size / 2;
    if (step < 2) step = 2;

    int cols = (width + step - 1) / step;
    int rows = (height + step - 1) / step;
    const float *plane = dot_luma_plane(pixels, width, height, stride);
    if (!plane)
        return; /* out untouched */
    for (int tid = 0; tid < cols * rows; tid++) {
        float *o = out + (size_t)tid * 10;
        int sy = step / 2 + (tid % rows) * step;
        int sx = step / 2 + (tid / rows) * step;
        const uint8_t *q = pixels + (size_t)sy * (size_t)stride + (size_t)sx * 3;
        float luma = (0.299f * q[0] + 0.587f * q[1] + 0.114f * q[2]) / 255.0f;
        sprite_t s;
        if (portal_cell(plane, width, height, sx, sy, step, luma,
                        params, split, bright, &s)) {
            o[0] = s.x; o[1] = s.y; o[2] = s.r;
            o[3] = s.cr; o[4] = s.cg; o[5] = s.cb;
            o[6] = s.env; o[7] = s.shx; o[8] = s.shy; o[9] = 1.0f;
        } else {
            for (int i = 0; i < 10; i++)
                o[i] = 0.0f;
        }
    }
}

/* Debug (dual hunt): expose the luma-plane value for one pixel for
 * tools/sprcmp -D SPRCMP_LUMA (13b localization). Square test image, so
 * height is taken as width. Same formula as the per-frame luma plane. */
float particle_luma_at_dbg(const uint8_t *pixels, int width, int stride,
                           int x, int y)
{
    const float *plane = dot_luma_plane(pixels, width, width, stride);
    if (!plane)
        return 0.0f;
    return luma_at(plane, width, width, x, y);
}
