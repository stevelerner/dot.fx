/* dot.h — the dot family: subject-gated dot-map effects (dot.gate,
 * dot.portal).
 *
 * A different paradigm from crt.h/vhs.h: the frame becomes a regular grid
 * of dots whose brightness is gated by local texture/edges, so the
 * subject (a textured, edged shape) becomes the dots and flat backgrounds
 * drop to black — a "dot map of the subject" that moves with it — plus
 * closed-form per-dot motion. The base frame is removed (true black
 * behind the dots).
 *
 * Buffer contract: same as crt.h (8-bit RGB, stride in bytes, in place,
 * rejects invalid input, frame for signature uniformity).
 */

#ifndef DOT_H
#define DOT_H

#include <stdint.h>

/* dot.gate — the static dot map: subject-gated dot field with a slow
 * per-dot wobble. */
typedef struct {
    int size;   /* >= 1, dot pitch in px (grid step = size/2, disc r = step*0.8) */
    int speed;  /* >= 0, wobble amplitude in px (0 = static) */
    int gate;   /* 1..100, subject-gate knee x100 (binary threshold on local contrast, inside the subject region) */
} dotgate_params_t;

void dotgate(uint8_t *pixels, int width, int height, int stride,
             const dotgate_params_t *params, int frame);

/* Debug (dual hunt): per-cell gather in tid order, 10 floats per cell
 * (x, y, r, cr, cg, cb, env, shx, shy, valid). Same layout as the MSL
 * Sprite struct in metal_fx.m. For byte comparison against
 * metal_dotgate_sprdump(). */
void dotgate_sprdump(const uint8_t *pixels, int width, int height,
                     int stride, const dotgate_params_t *params,
                     int frame, float *out);

/* dot.portal — the subject turned INTO dots (contrast dot.gate, which
 * renders the subject BEHIND a gap-free dot screen). Dots are separated
 * (fill < 50), sized by local tone, and displaced along the local luma
 * gradient, so the subject reads as a population of discrete particles
 * describing a 3D form. Time-invariant: the field moves because the
 * subject does. */
typedef struct {
    int size;      /* >= 1, dot pitch in px (grid step = size/2) */
    int fill;      /* 0..100, dot radius as % of step (80 = dot.gate's radius) */
    int gate;      /* 1..100, subject-gate knee x100 (binary threshold on local contrast, inside the subject region) */
    int halftone;  /* 0..100, how strongly radius tracks tone */
    int relief;    /* 0..200, gradient displacement as % of step */
    int level;     /* 0..200, brightness gain x100 (100 = 1x, 200 = 2x) */
    int color;     /* 0..360, dot hue in degrees (125 = green; 360 = white, no tint) */
    int glow;      /* 0..100, aura around each dot (radius x(1+g/100), colour x g/400); 0 = off */
} dotportal_params_t;

void dotportal(uint8_t *pixels, int width, int height, int stride,
               const dotportal_params_t *params, int frame);

/* Subject-region split: Otsu threshold of the frame's 256-bin luma
 * histogram. Returns the split bin index i (threshold = i/256); sets
 * *bright = 1 when the subject is the BRIGHT side (the minority side is
 * the subject: dark-on-bright or bright-on-dark). Host-side, closed
 * form; shared by both backends (Metal calls the same C code before
 * dispatch), so no MSL mirror is needed. */
int portal_split(const uint8_t *pixels, int width, int height, int stride,
                 int *bright);

/* Hue (0..360; 360 = white) to unit wheel colours (max channel 1), as
 * used by portal_ramp() — declared here and shared with core/metal_fx.m
 * (single definition in core/dot.c; do not duplicate). */
void portal_hue(float hue, float *r1, float *g1, float *b1);

/* Debug (dual hunt): per-cell gather in tid order, 10 floats per cell
 * (x, y, r, cr, cg, cb, env, shx, shy, valid). Same layout as the MSL
 * Sprite struct in metal_fx.m. For byte comparison against
 * metal_dotportal_sprdump(). */
void dotportal_sprdump(const uint8_t *pixels, int width, int height,
                       int stride, const dotportal_params_t *params,
                       int frame, float *out);

/* dot.spacengrave — engraved scanline overlay (plan/spacengrave.md):
 * a line-screen of additive near-white ink over a dimmed source (dim:
 * 0 = photo fully visible, 100 = pure black behind, as in dot.gate /
 * dot.portal). Vertical period only (pitch = size px); per-pixel
 * coverage = `line` bias - `grain` * local contrast (texture breaks the
 * line into irregular dashes) + `halftone` tone shading (darker = more
 * break-up), dithered per (x, y) pixel (fx_hash), so partial coverage
 * renders as dashes, not a dimmer line (the per-y dither keeps the
 * dashes from correlating into vertical bars). Ink brightness tracks
 * local tone, so the lines themselves carry the shading. `gate`
 * restricts the ink to the subject region (portal_split + the same
 * contrast knee dot.gate / dot.portal use): 0 = full-frame ink, 1..100
 * = subject only — the engraved subject on black, dots-for-lines.
 * Off-stroke pixels pass the (dimmed) source through. Time-invariant;
 * size = 0 = identity. */
typedef struct {
    int size;      /* 0 = off (identity), else >= 2, scanline pitch in px */
    int fill;      /* 0..100, stroke width as % of the pitch */
    int halftone;  /* 0..100, how strongly coverage tracks tone (engraved shading) */
    int line;      /* 0..100, baseline coverage — how solid the lines are everywhere */
    int level;     /* 0..200, ink brightness x100 (100 = 1x, 200 = 2x) */
    int grain;     /* 0..100, how strongly local texture breaks the line into dashes */
    int color;     /* 0..360, hue in degrees (360 = white, no tint) */
    int dim;       /* 0..100, source dimming (0 = photo visible, 100 = black behind) */
    int gate;      /* 0 = full-frame ink; 1..100 = subject-gated knee x100 (dot.portal's gate) */
} dot_spacengrave_params_t;

void dot_spacengrave(uint8_t *pixels, int width, int height, int stride,
                     const dot_spacengrave_params_t *params, int frame);

#endif /* DOT_H */
