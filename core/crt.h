/* crt.h — CRT/display effects.
 *
 * Buffer contract (every function obeys this):
 *  - 8-bit RGB, 3 bytes per pixel, no alpha
 *  - stride is the BYTE distance between row starts, >= width*3
 *  - operates in place
 *  - rejects invalid input without touching the buffer
 *  - frame is the timeline index, for signature uniformity
 */

#ifndef CRT_H
#define CRT_H

#include <stdint.h>

typedef enum {
    CRT_MASK_GRILLE = 0,  /* aperture grille: vertical stripes   */
    CRT_MASK_SLOT   = 1,  /* slot mask: staggered stripe rows     */
    CRT_MASK_TRIAD  = 2,  /* dot triad: hexagonal dot lattice     */
} crt_mask_type_t;

typedef struct {
    float intensity;      /* 0..1 (adapter: 0..100 dial, 0 = off) */
    int   period;         /* >= 2, default 3 */
    int   offset;         /* default 0 */
} crt_scanlines_params_t;

typedef struct {
    crt_mask_type_t type;
    float intensity;      /* 0..1 (adapter: 0..100 dial, 0 = off) */
    int   pitch;          /* >= 1, default 3 */
} crt_shadow_mask_params_t;

typedef struct {
    float amount;         /* 0..1, 0 = off (adapter: 0..100 dial) */
    float zoom;           /* 0..1, 0 = off; 1 = zoom by (1+amount), margin vanishes */
} crt_barrel_params_t;

typedef struct {
    float amount;         /* 0..2, 0 = off (adapter: 0..100 dial) */
    float threshold;      /* 0..1, luma cutoff above which pixels bloom */
    int   radius;         /* >= 0, blur radius in pixels, 0 = off */
} crt_bloom_params_t;

typedef struct {
    float amount;         /* 0..1, 0 = off (adapter: 0..100 dial) */
} crt_vignette_params_t;

typedef struct {
    int   radius;         /* >= 0, corner radius in pixels, 0 = sharp corner */
    int   margin;         /* >= 0, bezel crop in pixels, 0 = none */
} crt_overscan_params_t;

void crt_scanlines(uint8_t *pixels, int width, int height, int stride,
                   const crt_scanlines_params_t *params, int frame);

void crt_shadow_mask(uint8_t *pixels, int width, int height, int stride,
                     const crt_shadow_mask_params_t *params, int frame);

void crt_barrel(uint8_t *pixels, int width, int height, int stride,
                const crt_barrel_params_t *params, int frame);

void crt_bloom(uint8_t *pixels, int width, int height, int stride,
               const crt_bloom_params_t *params, int frame);

void crt_vignette(uint8_t *pixels, int width, int height, int stride,
                  const crt_vignette_params_t *params, int frame);

void crt_overscan(uint8_t *pixels, int width, int height, int stride,
                  const crt_overscan_params_t *params, int frame);

#endif /* CRT_H */
