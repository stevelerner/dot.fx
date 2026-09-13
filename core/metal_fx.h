/* metal_fx.h — Metal (GPU) implementations of the two dot effects.
 *
 * C API only (the implementation, core/metal_fx.m, is Objective-C but the
 * surface is plain C so the C adapter and harnesses can drive it).
 *
 * Contract: byte-identical to the CPU core (core/dot.c) — see the
 * file header of metal_fx.m for how that is achieved (mirrored MSL,
 * order-independent integer accumulation). Return values: 0 = success
 * (including the identity no-op), -1 = Metal unavailable/failed — the
 * caller should fall back to the CPU core.
 */

#ifndef METAL_FX_H
#define METAL_FX_H

#include <stdint.h>

#include "dot.h"
#include "glitch.h"

int  metal_fx_init(void);
void metal_fx_shutdown(void);

int  metal_dotgate(uint8_t *rgb, int width, int height, int stride,
                   const dotgate_params_t *params, int frame);
int  metal_dotportal(uint8_t *rgb, int width, int height, int stride,
                     const dotportal_params_t *params, int frame);
int  metal_vid_glitch(uint8_t *rgb, int width, int height, int stride,
                      const vid_glitch_params_t *params, int frame);
int  metal_spacengrave(uint8_t *rgb, int width, int height, int stride,
                       const dot_spacengrave_params_t *params, int frame);

/* Debug (dual hunt): run gather only and copy the sprite slots back
 * (10 floats per cell, tid order — same layout as dot*_sprdump).
 * 0 = success, -1 = failed OR the input is identity (guards) — the
 * caller must not compare in the -1 case. */
int  metal_dotgate_sprdump(const uint8_t *rgb, int width, int height, int stride,
                           const dotgate_params_t *params, int frame,
                           float *out);
int  metal_dotportal_sprdump(const uint8_t *rgb, int width, int height, int stride,
                             const dotportal_params_t *params, int frame,
                             float *out);

#endif /* METAL_FX_H */
