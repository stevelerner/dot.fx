/* vhs.h — VHS/camcorder effects.
 *
 * Buffer contract: same as crt.h (8-bit RGB, stride in bytes, in place,
 * rejects invalid input, frame for signature uniformity).
 */

#ifndef VHS_H
#define VHS_H

#include <stdint.h>

typedef struct {
    int   radius;         /* >= 0, fringe width in px (v2: asymmetric RGB shift) */
    float amount;         /* 0..1, fringe strength (v2; adapter: 0..100 dial) */
} vhs_chroma_bleed_params_t;

void vhs_chroma_bleed(uint8_t *pixels, int width, int height, int stride,
                      const vhs_chroma_bleed_params_t *params, int frame);

typedef struct {
    float amount;         /* 0..2, peak deviation = 192*amount luma units (adapter: 0..100 dial) */
    int   wavelength;     /* >= 2, pixels per ring cycle */
} vhs_luma_ring_params_t;

void vhs_luma_ring(uint8_t *pixels, int width, int height, int stride,
                   const vhs_luma_ring_params_t *params, int frame);

typedef struct {
    float amount;         /* 0..2, max fringe shift = 8px * amount at full slope/beat (v2; adapter: 0..100 dial) */
    int   period_y;       /* >= 2, rows per crawl beat cycle */
    int   period_t;       /* >= 2, frames per crawl beat cycle */
} vhs_rainbow_params_t;

void vhs_rainbow_phase(uint8_t *pixels, int width, int height, int stride,
                       const vhs_rainbow_params_t *params, int frame);

typedef struct {
    int   amplitude;      /* >= 0, drift amplitude in px, 0 = off */
    int   period;         /* >= 2, frames per drift cycle */
} vhs_tape_wow_params_t;

void vhs_tape_wow(uint8_t *pixels, int width, int height, int stride,
                  const vhs_tape_wow_params_t *params, int frame);

#endif /* VHS_H */
