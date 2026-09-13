/* crt.c — CRT/display effects (scanlines, shadow mask).
 *
 * Portable C99, dependency-free. No host headers here.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "crt.h"
#include "fx_hash.h"

#define PI 3.14159265358979323846f

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

/* §6 Barrel curvature (CRT glass).
 *
 * A CRT screen is a convex piece of glass: the centre of the glass is
 * the point closest to the viewer and the edges curve away, so content
 * near the screen edge is farther and gets foreshortened. The picture
 * wraps around the viewer: centre relatively magnified, edges
 * compressed, the image's corners sinking toward the centre.
 *
 * To make a flat render read as that display, remap outward: an output
 * pixel at normalised radius r samples the source at radius r * scale,
 * with
 *
 *     scale = z * (1 + 0.5 * amount * r^2),   r in [0, sqrt(2)] (corner)
 *     z     = 1 / (1 + amount * zoom)         (zoom-in by 1 + amount*zoom)
 *
 * At the corner (r^2 = 2) the sample lands z * (1 + amount) of the way
 * to the corner. zoom = 1 lands it exactly on the corner: the picture
 * is zoomed in by (1 + amount) so the curved margin region shows real
 * content and no clamping occurs. For zoom < 1 samples may still leave
 * the source frame; they clamp to the edge (content fills the frame —
 * no black rim).
 * amount = 0 is byte-identity (early return). Sampling is bilinear
 * from a full-frame snapshot — in place, per the buffer contract (§4)
 * and the spatial-copy rule (§9.4). */
void crt_barrel(uint8_t *pixels, int width, int height, int stride,
                const crt_barrel_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    (void)frame; /* reserved: curvature is time-invariant */

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    if (amount == 0.0f)
        return; /* identity */

    float zoom = params->zoom;
    if (zoom < 0.0f) zoom = 0.0f;
    if (zoom > 1.0f) zoom = 1.0f;
    float z = 1.0f / (1.0f + amount * zoom);

    size_t buf_bytes = (size_t)height * (size_t)stride;
    uint8_t *src = malloc(buf_bytes);
    if (!src)
        return; /* cannot allocate; leave buffer untouched */

    /* Snapshot the source (row by row; honour stride). */
    for (int y = 0; y < height; y++) {
        const uint8_t *sp = pixels + (size_t)y * (size_t)stride;
        uint8_t *dp = src + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            dp[(size_t)x * 3 + 0] = sp[(size_t)x * 3 + 0];
            dp[(size_t)x * 3 + 1] = sp[(size_t)x * 3 + 1];
            dp[(size_t)x * 3 + 2] = sp[(size_t)x * 3 + 2];
        }
    }

    float cx = (float)(width - 1) * 0.5f;
    float cy = (float)(height - 1) * 0.5f;
    float h2x = (float)width * 0.5f;
    float h2y = (float)height * 0.5f;
    float inv_h2x2 = 1.0f / (h2x * h2x);
    float inv_h2y2 = 1.0f / (h2y * h2y);

    for (int y = 0; y < height; y++) {
        uint8_t *drow = pixels + (size_t)y * (size_t)stride;
        float dyo = (float)y - cy;
        float r2y = dyo * dyo * inv_h2y2;

        for (int x = 0; x < width; x++) {
            float dxo = (float)x - cx;
            float scale = z * (1.0f + 0.5f * amount * (r2y + dxo * dxo * inv_h2x2));

            float sx = cx + dxo * scale;
            float sy = cy + dyo * scale;
            if (sx < 0.0f) sx = 0.0f;
            if (sx > (float)(width - 1)) sx = (float)(width - 1);
            if (sy < 0.0f) sy = 0.0f;
            if (sy > (float)(height - 1)) sy = (float)(height - 1);

            int x0 = (int)sx;
            float fx = sx - (float)x0;
            int x1 = x0 + 1;
            if (x1 >= width) { x1 = width - 1; fx = 0.0f; }
            int y0 = (int)sy;
            float fy = sy - (float)y0;
            int y1 = y0 + 1;
            if (y1 >= height) { y1 = height - 1; fy = 0.0f; }

            const uint8_t *a = src + (size_t)y0 * (size_t)stride + (size_t)x0 * 3;
            const uint8_t *b = src + (size_t)y0 * (size_t)stride + (size_t)x1 * 3;
            const uint8_t *c = src + (size_t)y1 * (size_t)stride + (size_t)x0 * 3;
            const uint8_t *d = src + (size_t)y1 * (size_t)stride + (size_t)x1 * 3;

            uint8_t *px = drow + (size_t)x * 3;
            for (int ch = 0; ch < 3; ch++) {
                float top = (1.0f - fx) * (float)a[ch] + fx * (float)b[ch];
                float bot = (1.0f - fx) * (float)c[ch] + fx * (float)d[ch];
                px[ch] = fx_fromf((1.0f - fy) * top + fy * bot);
            }
        }
    }

    free(src);
}
void crt_scanlines(uint8_t *pixels, int width, int height, int stride,
                   const crt_scanlines_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->period < 2)
        return;
    (void)frame; /* reserved: scanlines have no temporal component */

    float intensity = params->intensity;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    int period = params->period;
    int offset = params->offset;

    for (int y = 0; y < height; y++) {
        int phase = (y + offset) % period;
        if (phase < 0)
            phase += period;

        float f = 0.5f * (1.0f + cosf(2.0f * PI * (float)phase / (float)period));
        float gain = 1.0f - intensity * (1.0f - f);

        uint8_t *row = pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            uint8_t *px = row + (size_t)x * 3;
            px[0] = fx_fromf(px[0] * gain);
            px[1] = fx_fromf(px[1] * gain);
            px[2] = fx_fromf(px[2] * gain);
        }
    }
}

/* §5.2 Shadow mask */
void crt_shadow_mask(uint8_t *pixels, int width, int height, int stride,
                     const crt_shadow_mask_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->pitch < 1)
        return;
    (void)frame; /* reserved: shadow mask has no temporal component */

    float intensity = params->intensity;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    if (intensity == 0.0f)
        return;

    int pitch = params->pitch;

    switch (params->type) {
    case CRT_MASK_GRILLE:
        /* Vertical stripes: owning channel is (x/pitch) mod 3. */
        for (int y = 0; y < height; y++) {
            uint8_t *row = pixels + (size_t)y * (size_t)stride;
            for (int x = 0; x < width; x++) {
                int own = (x / pitch) % 3;
                uint8_t *px = row + (size_t)x * 3;
                float w, g, b;
                if (own == 0)      { w = 1.0f; g = 1.0f - intensity; b = 1.0f - intensity; }
                else if (own == 1) { w = 1.0f - intensity; g = 1.0f; b = 1.0f - intensity; }
                else               { w = 1.0f - intensity; g = 1.0f - intensity; b = 1.0f; }
                px[0] = fx_fromf(px[0] * w);
                px[1] = fx_fromf(px[1] * g);
                px[2] = fx_fromf(px[2] * b);
            }
        }
        break;

    case CRT_MASK_SLOT:
        /* As grille, but stripe columns offset by half a cell every
         * other row-block: col = ((x/pitch) + ((y/(pitch*2)) mod 2)) mod 3. */
        for (int y = 0; y < height; y++) {
            int rowblock = (y / (pitch * 2)) % 2;
            uint8_t *row = pixels + (size_t)y * (size_t)stride;
            for (int x = 0; x < width; x++) {
                int own = ((x / pitch) + rowblock) % 3;
                uint8_t *px = row + (size_t)x * 3;
                float w, g, b;
                if (own == 0)      { w = 1.0f; g = 1.0f - intensity; b = 1.0f - intensity; }
                else if (own == 1) { w = 1.0f - intensity; g = 1.0f; b = 1.0f - intensity; }
                else               { w = 1.0f - intensity; g = 1.0f - intensity; b = 1.0f; }
                px[0] = fx_fromf(px[0] * w);
                px[1] = fx_fromf(px[1] * g);
                px[2] = fx_fromf(px[2] * b);
            }
        }
        break;

    case CRT_MASK_TRIAD:
        /* Hexagonal dot lattice. Tile is 3*pitch wide by pitch*sqrt(3) tall,
         * containing six dot centres. Distance search wraps toroidally —
         * a pixel near a tile edge is closest to a dot in the *next* tile. */
        {
            float tile_w_f  = (float)(3 * pitch);
            float tile_h_f  = (float)pitch * 1.7320508075688772f; /* sqrt(3) */
            float tile_w2_f = tile_w_f * 0.5f;
            float tile_h2_f = tile_h_f * 0.5f;
            float radius    = (float)pitch * 0.7f;
            float radius2   = radius * radius;

            /* Dot centres in pitch-units; owning channel per centre.
             * row 0 (y=0):       R at 0.0,  G at 1.0,  B at 2.0
             * row 1 (y=tile/2):  B at 0.5,  R at 1.5,  G at 2.5 */
            const float dx[6] = { 0.0f, 1.0f, 2.0f, 0.5f, 1.5f, 2.5f };
            const float dy[6] = { 0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f };
            const int   own[6] = { 0, 1, 2, 2, 0, 1 };

            for (int y = 0; y < height; y++) {
                float ty_f = (float)y;
                if (tile_h_f > 0.0f) {
                    float q = ty_f / tile_h_f;
                    ty_f -= (float)((int)q) * tile_h_f;
                }
                if (ty_f < 0.0f) ty_f += tile_h_f;

                uint8_t *row = pixels + (size_t)y * (size_t)stride;
                for (int x = 0; x < width; x++) {
                    float tx_f = (float)x;
                    if (tile_w_f > 0.0f) {
                        float q = tx_f / tile_w_f;
                        tx_f -= (float)((int)q) * tile_w_f;
                    }
                    if (tx_f < 0.0f) tx_f += tile_w_f;

                    float best = 1e30f;
                    int best_own = 0;
                    for (int i = 0; i < 6; i++) {
                        float ddx = tx_f - dx[i] * (float)pitch;
                        if (ddx > tile_w2_f)      ddx -= tile_w_f;
                        else if (ddx < -tile_w2_f) ddx += tile_w_f;
                        float ddy = ty_f - dy[i] * (float)pitch;
                        if (ddy > tile_h2_f)      ddy -= tile_h_f;
                        else if (ddy < -tile_h2_f) ddy += tile_h_f;
                        float d2 = ddx * ddx + ddy * ddy;
                        if (d2 < best) { best = d2; best_own = own[i]; }
                    }

                    float weight = 0.0f;
                    if (best < radius2) {
                        float t = best / radius2;
                        weight = 1.0f - t;
                    }

                    uint8_t *px = row + (size_t)x * 3;
                    float scale = 1.0f - intensity * weight;
                    float w, g, b;
                    if (best_own == 0)      { w = 1.0f; g = scale; b = scale; }
                    else if (best_own == 1) { w = scale; g = 1.0f; b = scale; }
                    else                    { w = scale; g = scale; b = 1.0f; }
                    px[0] = fx_fromf(px[0] * w);
                    px[1] = fx_fromf(px[1] * g);
                    px[2] = fx_fromf(px[2] * b);
                }
            }
        }
        break;
    }
}

/* §6.4 Bloom (phosphor glow).
 *
 * A CRT's phosphor emits light, so bright pixels spill into their
 * neighbours. Model: gate the colour by a soft luma weight, low-pass it
 * with a separable box blur, and add the spread back over the original:
 *
 *     w(px)  = clamp( (Y - T) / (255 - T), 0, 1 )     (soft luma gate)
 *     T      = 255 * threshold
 *     bloom  = boxblur( w * RGB )                     (H then V, clamped edges)
 *     out    = clamp0_255( RGB + amount * bloom )
 *
 * Only luma above the threshold contributes, so dark regions stay
 * byte-identical and the gate is continuous (no hard edge). The blur
 * reads a full-frame snapshot (§9.4); rounding via fx_fromf (§9.5).
 * amount = 0, radius = 0, or threshold = 1 are byte-identity.
 * No stochastic component — (void)frame. */
void crt_bloom(uint8_t *pixels, int width, int height, int stride,
               const crt_bloom_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->radius < 0)
        return;
    (void)frame; /* reserved: bloom has no temporal component */

    int radius = params->radius;
    if (radius == 0)
        return; /* identity */

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 2.0f) amount = 2.0f; /* adapter dial: 100 = 2.0 */
    if (amount == 0.0f)
        return; /* identity */

    float threshold = params->threshold;
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;
    float cutoff = 255.0f * threshold;
    if (cutoff >= 255.0f)
        return; /* nothing can exceed the gate => identity */

    float inv_span = 1.0f / (255.0f - cutoff);

    size_t buf_bytes = (size_t)height * (size_t)stride;
    uint8_t *src   = malloc(buf_bytes); /* source snapshot */
    uint8_t *gated = malloc(buf_bytes); /* w * RGB per pixel */
    uint8_t *horiz = malloc(buf_bytes); /* horizontal blur result */
    if (!src || !gated || !horiz) {
        free(src); free(gated); free(horiz);
        return; /* cannot allocate; leave buffer untouched */
    }

    /* 1) Snapshot + soft luma gate. */
    for (int y = 0; y < height; y++) {
        const uint8_t *sp = pixels + (size_t)y * (size_t)stride;
        uint8_t *dp = src + (size_t)y * (size_t)stride;
        uint8_t *gp = gated + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            const uint8_t *p = sp + (size_t)x * 3;
            dp[(size_t)x * 3 + 0] = p[0];
            dp[(size_t)x * 3 + 1] = p[1];
            dp[(size_t)x * 3 + 2] = p[2];
            float Y = 0.299f * (float)p[0] + 0.587f * (float)p[1]
                    + 0.114f * (float)p[2];
            float w = (Y - cutoff) * inv_span;
            if (w < 0.0f) w = 0.0f;
            else if (w > 1.0f) w = 1.0f;
            gp[(size_t)x * 3 + 0] = (uint8_t)(w * (float)p[0] + 0.5f);
            gp[(size_t)x * 3 + 1] = (uint8_t)(w * (float)p[1] + 0.5f);
            gp[(size_t)x * 3 + 2] = (uint8_t)(w * (float)p[2] + 0.5f);
        }
    }

    /* 2) Horizontal box blur (uniform taps, clamped at row ends). */
    for (int y = 0; y < height; y++) {
        const uint8_t *sp = gated + (size_t)y * (size_t)stride;
        uint8_t *dp = horiz + (size_t)y * (size_t)stride;
        for (int x = 0; x < width; x++) {
            int lo = x - radius; if (lo < 0) lo = 0;
            int hi = x + radius; if (hi >= width) hi = width - 1;
            int count = hi - lo + 1;
            for (int ch = 0; ch < 3; ch++) {
                float sum = 0.0f;
                for (int i = lo; i <= hi; i++)
                    sum += (float)sp[(size_t)i * 3 + ch];
                dp[(size_t)x * 3 + ch] = (uint8_t)(sum / (float)count + 0.5f);
            }
        }
    }

    /* 3) Vertical blur + additive blend over the snapshot. */
    for (int y = 0; y < height; y++) {
        const uint8_t *sp = src + (size_t)y * (size_t)stride;
        uint8_t *drow = pixels + (size_t)y * (size_t)stride;
        int lo = y - radius; if (lo < 0) lo = 0;
        int hi = y + radius; if (hi >= height) hi = height - 1;
        int count = hi - lo + 1;
        for (int x = 0; x < width; x++) {
            uint8_t *px = drow + (size_t)x * 3;
            for (int ch = 0; ch < 3; ch++) {
                float sum = 0.0f;
                for (int i = lo; i <= hi; i++)
                    sum += (float)(horiz + (size_t)i * (size_t)stride)
                                               [(size_t)x * 3 + ch];
                float bloom = sum / (float)count;
                px[ch] = fx_fromf((float)sp[(size_t)x * 3 + ch] + amount * bloom);
            }
        }
    }

    free(src); free(gated); free(horiz);
}

/* §6.5 Vignette.
 *
 * The CRT glass is convex and beveled at the rim, so perceived brightness
 * falls off toward the screen corners. Model: a parabolic radial gain
 * centred on the frame (same radius normalisation as barrel):
 *
 *     r^2  = (dxo/(w/2))^2 + (dyo/(h/2))^2     (0 at centre, 2 at corner)
 *     gain = 1 - 0.5 * amount * r^2            (clamped >= 0)
 *     out  = clamp0_255( in * gain )
 *
 * gain is 1 at the centre and (1 - amount) at the corner, so amount = 1
 * folds the corner to black and amount = 0 is byte-identity (early
 * return). A common per-pixel multiplier preserves hue. Row-local O(1),
 * no snapshot needed (pure point op). No stochastic component —
 * (void)frame. */
void crt_vignette(uint8_t *pixels, int width, int height, int stride,
                  const crt_vignette_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    (void)frame; /* reserved: vignette has no temporal component */

    float amount = params->amount;
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    if (amount == 0.0f)
        return; /* identity */

    float cx = (float)(width - 1) * 0.5f;
    float cy = (float)(height - 1) * 0.5f;
    float inv_h2x2 = 4.0f / ((float)width * (float)width);
    float inv_h2y2 = 4.0f / ((float)height * (float)height);

    for (int y = 0; y < height; y++) {
        uint8_t *drow = pixels + (size_t)y * (size_t)stride;
        float dyo = (float)y - cy;
        float r2y = dyo * dyo * inv_h2y2;

        for (int x = 0; x < width; x++) {
            float dxo = (float)x - cx;
            float gain = 1.0f - 0.5f * amount * (r2y + dxo * dxo * inv_h2x2);
            if (gain < 0.0f) gain = 0.0f;

            uint8_t *px = drow + (size_t)x * 3;
            px[0] = fx_fromf((float)px[0] * gain);
            px[1] = fx_fromf((float)px[1] * gain);
            px[2] = fx_fromf((float)px[2] * gain);
        }
    }
}

/* §6.6 Overscan (rounded-bezel cutout).
 *
 * The visible tube area is smaller than the raster: the bezel clips a
 * margin of the frame and the screen corners are rounded. Model: a
 * rounded-rectangle mask centred on the frame, inset by `margin`, with
 * corner radius `radius`; outside it, pixels fade to black over a fixed
 * 2 px feather (signed-distance form):
 *
 *     hx = (w-1)/2 - margin,  hy = (h-1)/2 - margin     (half-extents)
 *     ax = max( |x - cx| - (hx - radius), 0 )
 *     ay = max( |y - cy| - (hy - radius), 0 )
 *     d  = sqrt( ax*ax + ay*ay ) - radius   (< 0 inside, > 0 outside)
 *     out = in * clamp( 1 - d/2, 0, 1 )
 *
 * radius = margin = 0 ⇒ the mask covers the whole frame ⇒ gain 1
 * everywhere ⇒ byte-identity (early return). A common per-pixel
 * multiplier preserves hue where the feather transitions. Row-local,
 * no snapshot. No stochastic component — (void)frame. */
void crt_overscan(uint8_t *pixels, int width, int height, int stride,
                  const crt_overscan_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;
    if (params->radius < 0 || params->margin < 0)
        return;
    (void)frame; /* reserved: overscan has no temporal component */

    int radius = params->radius;
    int margin = params->margin;
    if (radius == 0 && margin == 0)
        return; /* identity: mask covers the whole frame */

    float cx = (float)(width - 1) * 0.5f;
    float cy = (float)(height - 1) * 0.5f;
    float hx = (float)(width - 1) * 0.5f - (float)margin;
    float hy = (float)(height - 1) * 0.5f - (float)margin;
    const float feather = 2.0f;

    for (int y = 0; y < height; y++) {
        uint8_t *drow = pixels + (size_t)y * (size_t)stride;
        float ay = fabsf((float)y - cy) - (hy - (float)radius);
        if (ay < 0.0f) ay = 0.0f;
        float ay2 = ay * ay;

        for (int x = 0; x < width; x++) {
            float ax = fabsf((float)x - cx) - (hx - (float)radius);
            if (ax < 0.0f) ax = 0.0f;
            float d = sqrtf(ax * ax + ay2) - (float)radius;
            float gain = 1.0f - d / feather;
            if (gain >= 1.0f)
                continue; /* inside the mask: unchanged */
            if (gain <= 0.0f) gain = 0.0f;

            uint8_t *px = drow + (size_t)x * 3;
            px[0] = fx_fromf((float)px[0] * gain);
            px[1] = fx_fromf((float)px[1] * gain);
            px[2] = fx_fromf((float)px[2] * gain);
        }
    }
}
