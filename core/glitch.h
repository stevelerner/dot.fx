/* glitch.h — vid.glitch: a pro, burst-driven glitch treatment.
 *
 * Buffer contract: same as vhs.h (8-bit RGB, stride in bytes, in place,
 * rejects invalid input, frame for signature uniformity).
 *
 * Artifacts — each individually tunable, param = 0 turns it off, all
 * closed form in (seed, frame) — deterministic:
 *   amount  burst envelope — intermittent: mostly clean, sharp bursts
 *   bands   random-height horizontal slices tear left/right
 *   blocks  random rectangular tiles: duplicate / invert / solid noise
 *   scan    thin scanlines shift or flicker in brightness
 *   rgb     R/B sampled in opposite directions (chromatic split)
 *   quant   posterize (color quantization / banding)
 *   vtear   one horizontal strip shifts vertically (vertical-sync tear)
 *   noise   fine static grain
 *
 * `amount` = 0 (or invalid params) is byte-identity; with all element
 * params = 0 the effect is identity even inside a burst (byte-pure).
 * Spec: plan/vidglitch.md.
 */

#ifndef GLITCH_H
#define GLITCH_H

#include <stdint.h>

typedef struct {
    int amount;   /* 0..100, burst density (time-based trigger); 0 = off/identity;
                   * strongest element >= 80 also enables whole-frame signal-loss jumps */
    int rgb;      /* 0..100 strength, RGB channel split (50 = ±32 px v2-max, 100 = ±64); 0 = off */
    int noise;    /* 0..100, static/grain intensity (amplitude 2*noise; 50 = ±100); 0 = off */
    int bands;    /* 0..100 strength, horizontal slice tears (max ±2*bnd px; 50 = ±100); 0 = off */
    int blocks;   /* 0..100 strength, block-tile size (max 2*blk px; 50 = 100 px); 0 = off */
    int scan;     /* 0..100 strength, scanline jitter (rows per burst = 1+scan*39/100; 50 = v2-max); 0 = off */
    int quant;    /* 0..100, posterize strength (levels 31..3; 50 = 7 = v2-max); 0 = off */
    int vtear;    /* 0..100 strength, vertical-sync tear (strip vt*h/300, ±2*vt px; 50 = v2-max); 0 = off */
    int seed;     /* 0..999, reproducible variation */
} vid_glitch_params_t;

void vid_glitch(uint8_t *pixels, int width, int height, int stride,
                const vid_glitch_params_t *params, int frame);

#endif /* GLITCH_H */
