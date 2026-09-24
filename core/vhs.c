/* vhs.c — VHS/camcorder effects (chroma bleed).
 *
 * Portable C99, dependency-free. No host headers here.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "vhs.h"
#include "fx_hash.h"
#include "fx_scratch.h"

static int fx_valid(const uint8_t *pixels, int width, int height, int stride)
{
    return pixels != NULL && width > 0 && height > 0 &&
           stride >= width * 3;
}

static uint8_t fx_fromf(float v)
{
    if (v < 0.0f)   v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)(v + 0.5f);
}

/* Shared: bilinear horizontal sample of one channel from a row snapshot,
 * clamped at row ends. Used by the v2 fringe effects below. */
static float vhs_row_sample(const uint8_t *row, int width, float xf, int ch)
{
    if (xf < 0.0f) xf = 0.0f;
    if (xf > (float)(width - 1)) xf = (float)(width - 1);
    int x0 = (int)xf;
    float fx = xf - (float)x0;
    int x1 = x0 + 1;
    if (x1 >= width) { x1 = width - 1; fx = 0.0f; }
    return (1.0f - fx) * (float)row[(size_t)x0 * 3 + ch]
         + fx * (float)row[(size_t)x1 * 3 + ch];
}

/* §6.7 Chromablood v2 (Gate A rework) — asymmetric colour fringe.
 *
 * v1 box-blurred I and Q (a desaturating blur) — on low-saturation footage
 * it read as unchanged (user verdict 2026-08-26, plan Task 6). v2 shifts
 * the R and B channels in opposite horizontal directions around an anchor
 * green:
 *
 *     shift = radius * amount                (px, >= 0)
 *     out_R[x] = in_R[x - shift]             (bilinear, clamped at row ends)
 *     out_G[x] = in_G[x]                     (anchor: untouched)
 *     out_B[x] = in_B[x + shift]
 *
 * The visible signal is now an RGB delta on every vertical edge, sized by
 * the local gradient and independent of saturation — the classic VHS
 * "chroma lags luma" fringe. Row-local O(1)/pixel; reads a per-row
 * snapshot (§9.4); rounding via fx_fromf (§9.5). radius = 0 or
 * amount = 0 are byte-identity. No temporal component — (void)frame. */
void vhs_chroma_bleed(uint8_t *pixels, int width, int height, int stride,
                      const vhs_chroma_bleed_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->radius < 0)
        return;
    (void)frame; /* reserved: the fringe has no temporal component */

    int radius = params->radius;
    if (radius == 0)
        return; /* identity: no fringe */

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    if (amount == 0.0f)
        return; /* identity */

    float shift = (float)radius * amount;

    uint8_t *rowbuf = fx_scratch(0, (size_t)width * 3);
    if (!rowbuf)
        return; /* cannot allocate; leave buffer untouched */

    for (int y = 0; y < height; y++) {
        uint8_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            rowbuf[(size_t)x * 3 + 0] = row[(size_t)x * 3 + 0];
            rowbuf[(size_t)x * 3 + 1] = row[(size_t)x * 3 + 1];
            rowbuf[(size_t)x * 3 + 2] = row[(size_t)x * 3 + 2];
        }

        for (int x = 0; x < width; x++) {
            /* R lags, B leads: flat regions (x ± shift have the same
             * value) stay byte-identical; edges take the fringe. */
            uint8_t *d = row + (size_t)x * 3;
            d[0] = fx_fromf(vhs_row_sample(rowbuf, width, (float)x - shift, 0));
            d[2] = fx_fromf(vhs_row_sample(rowbuf, width, (float)x + shift, 2));
            /* d[1] (green anchor) untouched. */
        }
    }
}

/* §6.1 Luma ring: a signed edge-slope amplitude modulates a fixed spatial
 * carrier along the scanline — bright/dark bands around high-contrast
 * edges. In place, row-local: the row's luma is read before it is written
 * back, so no full-frame copy is needed. */
void vhs_luma_ring(uint8_t *pixels, int width, int height, int stride,
                   const vhs_luma_ring_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->wavelength < 2)
        return;
    (void)frame; /* reserved: luma ring has no temporal component */

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 2.0f) amount = 2.0f; /* adapter dial: 100 = 2.0 */
    if (amount == 0.0f)
        return; /* identity: no ringing, output == input */

    float pi = 3.14159265358979323846f;
    float step = 2.0f * pi / (float)params->wavelength;

    float *Y = (float *)fx_scratch(0, (size_t)width * sizeof *Y);
    if (!Y)
        return; /* cannot allocate; leave buffer untouched */

    for (int y = 0; y < height; y++) {
        uint8_t *row = pixels + (size_t)y * (size_t)stride;

        /* Row luma, read before any in-place writes. */
        for (int x = 0; x < width; x++) {
            const uint8_t *p = row + (size_t)x * 3;
            Y[x] = 0.299f * (float)p[0] +
                   0.587f * (float)p[1] +
                   0.114f * (float)p[2];
        }

        for (int x = 0; x < width; x++) {
            /* Normalised horizontal slope, one-sided at row ends. */
            int xm = x > 0 ? x - 1 : x;
            int xp = x < width - 1 ? x + 1 : x;
            float s = (Y[xp] - Y[xm]) / 255.0f;
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;

            /* Signed slope flips the carrier phase across the edge:
             * bright band on one side, dark on the other. */
            float dY = amount * 192.0f * s * cosf(step * (float)x);

            /* Common offset on all channels: shifts luma by dY,
             * chroma preserved. */
            uint8_t *p = row + (size_t)x * 3;
            p[0] = fx_fromf((float)p[0] + dY);
            p[1] = fx_fromf((float)p[1] + dY);
            p[2] = fx_fromf((float)p[2] + dY);
        }
    }
}

/* §6.8 Rainbow v2 (Gate A rework) — crawling fringe.
 *
 * v1 rotated each pixel's I/Q (a hue twist) — on low-saturation footage a
 * twist about a near-grey point is a no-op, so it read as unchanged
 * (user verdict 2026-08-26, plan Task 6). v2 keeps v1's edge slope and
 * crawl beat, but realises the phase error as an asymmetric RGB fringe
 * whose signed offset crawls over rows and frames:
 *
 *     s     = clamp( (Y[x+1] - Y[x-1]) / 255, -1, 1 )   (same slope, §6.1)
 *     cosph = cos( 2*pi*(y/period_y + frame/period_t) ) (same crawl beat)
 *     d     = 8 * amount * s * cosph                    (signed px shift)
 *     out_R[x] = in_R[x - d]
 *     out_G[x] = in_G[x]                                (anchor)
 *     out_B[x] = in_B[x + d]
 *
 * As the beat crawls, the fringe's side and sign flip — moving colour
 * fringing on the edges, the dot-crawl look. All three channels move,
 * so it now bites on saturated and unsaturated edges alike (the visible
 * signal is an RGB delta, not a chroma rotation). Row-local; reads a
 * per-row snapshot (§9.4). amount = 0 is byte-identity; flat rows
 * (s = 0) are byte-identical. Deterministic per §5: a pure function of
 * (params, frame, x, y). */
void vhs_rainbow_phase(uint8_t *pixels, int width, int height, int stride,
                       const vhs_rainbow_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->period_y < 2 || params->period_t < 2)
        return;

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 2.0f) amount = 2.0f; /* adapter dial: 100 = 2.0 */
    if (amount == 0.0f)
        return; /* identity: no crawling fringe */

    float pi      = 3.14159265358979323846f;
    float f       = (float)(frame >= 0 ? frame : -frame);
    float row_ph  = 2.0f * pi / (float)params->period_y;
    float time_ph = 2.0f * pi / (float)params->period_t;
    const float max_shift = 8.0f; /* px at full slope, full beat, amount 1 */

    float *Y = (float *)fx_scratch(0, (size_t)width * sizeof *Y);
    uint8_t *rowbuf = fx_scratch(1, (size_t)width * 3);
    if (!Y || !rowbuf)
        return; /* cannot allocate; leave buffer untouched */

    for (int y = 0; y < height; y++) {
        uint8_t *row = pixels + (size_t)y * (size_t)stride;

        /* Row snapshot + luma, read before any in-place writes. */
        for (int x = 0; x < width; x++) {
            rowbuf[(size_t)x * 3 + 0] = row[(size_t)x * 3 + 0];
            rowbuf[(size_t)x * 3 + 1] = row[(size_t)x * 3 + 1];
            rowbuf[(size_t)x * 3 + 2] = row[(size_t)x * 3 + 2];
            Y[x] = 0.299f * (float)rowbuf[(size_t)x * 3 + 0]
                 + 0.587f * (float)rowbuf[(size_t)x * 3 + 1]
                 + 0.114f * (float)rowbuf[(size_t)x * 3 + 2];
        }

        /* Crawl beat: drifts row by row, frame by frame. */
        float cosph = cosf(row_ph * (float)y + time_ph * f);

        for (int x = 0; x < width; x++) {
            /* Normalised horizontal slope, one-sided at row ends — the
             * same edge amplitude the luma ring uses. */
            int xm = x > 0 ? x - 1 : x;
            int xp = x < width - 1 ? x + 1 : x;
            float s = (Y[xp] - Y[xm]) / 255.0f;
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;

            /* Signed shift: the fringe side flips across the edge and
             * crawls with the beat. */
            float d = max_shift * amount * s * cosph;

            uint8_t *out = row + (size_t)x * 3;
            out[0] = fx_fromf(vhs_row_sample(rowbuf, width, (float)x - d, 0));
            out[2] = fx_fromf(vhs_row_sample(rowbuf, width, (float)x + d, 2));
            /* out[1] (green anchor) untouched; s = 0 ⇒ d = 0 ⇒ no-op. */
        }
    }
}

/* §6.9 Tape wow (flutter): capstan wobble modulates tape speed, so the
 * whole picture drifts slowly up and down. Closed form in the frame
 * index (§5 — never integrated state):
 *
 *     dy  = amplitude * sin( 2*pi*frame/period )    (signed px)
 *     out[y] = bilinear( in[wrap(y - dy)] )         (torus wrap, vertical)
 *
 * Full-frame snapshot before writing (§9.4); rounding via fx_fromf
 * (§9.5). amplitude = 0 is byte-identity. Consecutive frames differ by
 * design (the drift); re-rendering the same frame is byte-identical. */
void vhs_tape_wow(uint8_t *pixels, int width, int height, int stride,
                  const vhs_tape_wow_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->amplitude < 0 || params->period < 2)
        return;

    int amplitude = params->amplitude;
    if (amplitude == 0)
        return; /* identity: no drift */

    float pi = 3.14159265358979323846f;
    float fr = (float)(frame >= 0 ? frame : -frame);
    float dy = (float)amplitude *
               sinf(2.0f * pi * fr / (float)params->period);

    size_t buf_bytes = (size_t)height * (size_t)stride;
    uint8_t *src = fx_scratch(0, buf_bytes);
    if (!src)
        return; /* cannot allocate; leave buffer untouched */

    for (int y = 0; y < height; y++) {
        const uint8_t *sp = pixels + (size_t)y * (size_t)stride;
        uint8_t *dp = src + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            dp[(size_t)x * 3 + 0] = sp[(size_t)x * 3 + 0];
            dp[(size_t)x * 3 + 1] = sp[(size_t)x * 3 + 1];
            dp[(size_t)x * 3 + 2] = sp[(size_t)x * 3 + 2];
        }
    }

    for (int y = 0; y < height; y++) {
        uint8_t *drow = pixels + (size_t)y * (size_t)stride;

        /* Source row with torus wrap (the tape loops). */
        float fy = (float)y - dy;
        int y0 = (int)fy;
        float fyf = fy - (float)y0;
        if (fyf < 0.0f) { y0--; fyf = 1.0f + fyf; }
        y0 %= height;
        if (y0 < 0) y0 += height;
        int y1 = (y0 + 1) % height;

        const uint8_t *r0 = src + (size_t)y0 * (size_t)stride;
        const uint8_t *r1 = src + (size_t)y1 * (size_t)stride;

        for (int x = 0; x < width; x++) {
            uint8_t *px = drow + (size_t)x * 3;
            for (int ch = 0; ch < 3; ch++) {
                float v = (1.0f - fyf) * (float)r0[(size_t)x * 3 + ch]
                        + fyf * (float)r1[(size_t)x * 3 + ch];
                px[ch] = fx_fromf(v);
            }
        }
    }
}
