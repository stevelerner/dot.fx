/* glitch.c — vid.glitch: a pro, burst-driven glitch treatment.
 *
 * The frame is read from a full-frame snapshot, then, while a burst is
 * active, each enabled artifact is composed per pixel, all closed form
 * in (seed, frame):
 *
 *   amount  burst envelope — intermittent: mostly clean, sharp bursts
 *   bands   random-height horizontal slices tear left/right
 *   blocks  random rectangular tiles: duplicate / invert / solid noise
 *   scan    thin scanlines shift or flicker in brightness
 *   rgb     R/B sampled ±k px from the displaced pixel (chroma split)
 *   quant   posterize (color quantization / banding)
 *   vtear   one horizontal strip shifts vertically (vertical-sync tear)
 *   noise   per-pixel static grain
 *
 * Every element is individually tunable; its param = 0 turns it off.
 * `amount` = 0 (or invalid input) is byte-identity; with all element
 * params = 0 the effect is identity even inside a burst (byte-pure).
 * Portable C99, dependency-free.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glitch.h"
#include "fx_hash.h"

#define GL_SEED_BASE 0x746736u /* vid.glitch */

enum {
    G_BURST   = 1,  /* burst envelope (value-noise lattice) */
    G_BANDON  = 2,  /* band active */
    G_BANDDX  = 3,  /* band x offset */
    G_TEARON  = 4,  /* vertical tear active */
    G_TEARY   = 5,  /* tear strip top row */
    G_TEARDY  = 6,  /* tear vertical offset */
    G_RGBK    = 7,  /* rgb split magnitude */
    G_RGBDIR  = 8,  /* rgb split direction */
    G_NOISE   = 9,  /* per-pixel grain */
    G_BANDY   = 10, /* band slice top row */
    G_BANDH   = 11, /* band slice height */
    G_BLOKON  = 12, /* block tile active */
    G_BLOKX   = 13, /* block x0 */
    G_BLOKY   = 14, /* block y0 */
    G_BLOKW   = 15, /* block width */
    G_BLOKH   = 16, /* block height */
    G_BLOKMD  = 17, /* block mode (dup/invert/solid) */
    G_BLOKOX  = 18, /* block duplicate offset x */
    G_BLOKOY  = 19, /* block duplicate offset y */
    G_BLOKSR  = 20, /* block solid colour r */
    G_BLOKSG  = 21, /* block solid colour g */
    G_BLOKSB  = 22, /* block solid colour b */
    G_SCANON  = 23, /* scanline active */
    G_SCANROW = 24, /* scanline row */
    G_SCANMD  = 25, /* scanline mode (shift/flicker) */
    G_SCANDX  = 26, /* scanline x shift */
    G_SCANFL  = 27, /* scanline flicker factor x100 */
    G_FWON    = 28, /* whole-frame signal-loss jump active (amount >= 80) */
    G_FWMD    = 29, /* jump axis (0 = horizontal, 1 = vertical) */
    G_FWDX    = 30  /* jump offset */
};

#define GL_NBANDS   12  /* horizontal tear slices */
#define GL_NBLOCKS  8   /* block tiles */
#define GL_NSCAN    40  /* scanline slots */

typedef struct { int y0, hgt, dx; } gl_band_t;
typedef struct { int x0, y0, w, h, mode, ox, oy, sr, sg, sb; } gl_block_t;
typedef struct { int row, mode, dx, fl; } gl_scan_t;

static int fx_valid(const uint8_t *pixels, int width, int height, int stride)
{
    return pixels != NULL && width > 0 && height > 0 && stride >= width * 3;
}

static int fx_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* round-half-away-from-zero (mirrors the CPU idiom used across the cores). */
static int fx_round(float r)
{
    return (int)(r + (r >= 0.0f ? 0.5f : -0.5f));
}

static uint8_t fx_byte(int v)
{
    return (uint8_t)fx_clampi(v, 0, 255);
}

/* 1-D value noise over a coarse frame grid: smooth interpolation between
 * two lattice hashes. Closed form in (seed, frame, granularity). */
static float gl_vnoise(uint32_t seed, int frame, int grain, uint32_t key)
{
    int i = frame / grain;
    float r = (float)(frame - i * grain) / (float)grain;
    float a = fx_rand01(seed, (uint32_t)i, 0u, key);
    float b = fx_rand01(seed, (uint32_t)i + 1u, 0u, key);
    float s = r * r * (3.0f - 2.0f * r); /* smoothstep */
    return a + (b - a) * s;
}

/* Burst gate: in a glitch burst when the envelope is below the duty cycle
 * (duty = amount/100). amount = 0 never bursts. */
static int gl_burst(uint32_t seed, int frame, int amount)
{
    if (amount <= 0)
        return 0;
    float duty = (float)amount / 100.0f;
    float env = gl_vnoise(seed, frame, 5, G_BURST);
    return env < duty ? 1 : 0;
}

/* Posterize to `levels` output levels (255 maps to 255, 0 to 0). */
static int gl_posterize(int v, int levels)
{
    int idx = (v * levels + 127) / 255; /* 0..levels */
    return (idx * 255) / levels;
}

void vid_glitch(uint8_t *pixels, int width, int height, int stride,
                const vid_glitch_params_t *params, int frame)
{
    if (!fx_valid(pixels, width, height, stride) || params == NULL)
        return;

    int amount = fx_clampi(params->amount, 0, 100);
    if (amount == 0)
        return; /* identity: master off */

    int rgb   = fx_clampi(params->rgb, 0, 100);
    int noise = fx_clampi(params->noise, 0, 100);
    int bnd   = fx_clampi(params->bands, 0, 100);
    int blk   = fx_clampi(params->blocks, 0, 100);
    int scn   = fx_clampi(params->scan, 0, 100);
    int quant = fx_clampi(params->quant, 0, 100);
    int vt    = fx_clampi(params->vtear, 0, 100);
    int seedv = fx_clampi(params->seed, 0, 999);
    uint32_t seed = GL_SEED_BASE + (uint32_t)seedv;

    if (frame < 0)
        frame = 0;
    uint32_t t = (uint32_t)frame;

    /* (1) burst envelope — the whole effect is intermittent. */
    if (!gl_burst(seed, frame, amount))
        return; /* clean: outside a burst */

    /* rgb split, per frame. v4: 50 = v2-max (±32 px), 100 = 2×. */
    int k = 0, dir = 1;
    if (rgb > 0) {
        int kmax = (rgb * 64) / 100; /* 50 -> 32, 100 -> 64 */
        if (kmax < 1) kmax = 1;
        k = (int)(fx_rand01(seed, t, 0u, G_RGBK) * (float)(kmax + 1)); /* 0..kmax */
        dir = (fx_rand01(seed, t, 0u, G_RGBDIR) > 0.5f) ? 1 : -1;
    }

    /* horizontal slice tears: random-height slices, max |dx| = bnd px. */
    gl_band_t band[GL_NBANDS];
    for (int b = 0; b < GL_NBANDS; b++) {
        band[b].y0 = 0; band[b].hgt = 0; band[b].dx = 0;
        if (bnd == 0 || fx_rand01(seed, t, (uint32_t)b, G_BANDON) >= 0.55f)
            continue;
        int hmax = height < 96 ? height : 96;
        if (hmax < 4) hmax = 4;
        band[b].hgt = 4 + (int)(fx_rand01(seed, t, (uint32_t)b, G_BANDH)
                                * (float)(hmax - 3)); /* 4..hmax */
        int span = height - band[b].hgt;
        if (span < 0) span = 0;
        band[b].y0 = (int)(fx_rand01(seed, t, (uint32_t)b, G_BANDY)
                           * (float)(span + 1));
        int bmax = bnd * 2; /* 50 -> ±100 px (v2-max), 100 -> ±200 */
        if (bmax < 2) bmax = 2;
        band[b].dx = fx_round((fx_rand01(seed, t, (uint32_t)b, G_BANDDX) - 0.5f)
                              * 2.0f * (float)bmax);
    }

    /* block/tile corruption: tiles up to blk px, mode dup/invert/solid. */
    gl_block_t block[GL_NBLOCKS];
    int nblock = 0;
    for (int i = 0; i < GL_NBLOCKS; i++) {
        block[i].w = 0;
        if (blk == 0 || fx_rand01(seed, t, (uint32_t)i, G_BLOKON) >= 0.70f)
            continue;
        int bmax = blk * 2; /* 50 -> 100 px (v2-max), 100 -> 200 */
        if (bmax < 2) bmax = 2;
        int smin = bmax / 2;
        if (smin < 2) smin = 2;
        if (smin > bmax) smin = bmax;
        int w = smin + (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKW)
                             * (float)(bmax - smin + 1));
        int h = smin + (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKH)
                             * (float)(bmax - smin + 1));
        if (w > width) w = width;
        if (h > height) h = height;
        block[i].x0 = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKX)
                            * (float)(width - w + 1));
        block[i].y0 = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKY)
                            * (float)(height - h + 1));
        block[i].w = w;
        block[i].h = h;
        block[i].mode = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKMD) * 3.0f);
        int omax = (blk * 64) / 100; /* 50 -> ±16 (v2-max), 100 -> ±32 */
        if (omax < 2) omax = 2;
        block[i].ox = fx_round((fx_rand01(seed, t, (uint32_t)i, G_BLOKOX) - 0.5f) * (float)omax); /* ±omax/2 */
        block[i].oy = fx_round((fx_rand01(seed, t, (uint32_t)i, G_BLOKOY) - 0.5f) * (float)omax); /* ±omax/2 */
        block[i].sr = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKSR) * 255.0f);
        block[i].sg = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKSG) * 255.0f);
        block[i].sb = (int)(fx_rand01(seed, t, (uint32_t)i, G_BLOKSB) * 255.0f);
        nblock = 1;
    }

    /* scanline jitter: thin rows that shift or flicker in brightness. */
    gl_scan_t scan[GL_NSCAN];
    int nscan = scn > 0 ? 1 + (scn * 39) / 100 : 0;
    if (nscan > GL_NSCAN) nscan = GL_NSCAN;
    for (int i = 0; i < GL_NSCAN; i++) {
        scan[i].row = -1;
        if (i >= nscan || fx_rand01(seed, t, (uint32_t)i, G_SCANON) >= 0.80f)
            continue;
        scan[i].row = (int)(fx_rand01(seed, t, (uint32_t)i, G_SCANROW)
                            * (float)height);
        scan[i].mode = (fx_rand01(seed, t, (uint32_t)i, G_SCANMD) < 0.60f) ? 0 : 1;
        int smax = (scn * 48) / 100; /* 50 -> ±12 (v2-max), 100 -> ±24 */
        if (smax < 2) smax = 2;
        scan[i].dx = fx_round((fx_rand01(seed, t, (uint32_t)i, G_SCANDX) - 0.5f) * (float)smax); /* ±smax/2 */
        scan[i].fl = 100 + fx_round((fx_rand01(seed, t, (uint32_t)i, G_SCANFL) - 0.5f)
                                    * 2.0f * (float)scn); /* 50 -> x0.5..x1.5, 100 -> x0..x2 */
    }

    /* color quantization: stronger quant = fewer output levels. */
    int levels = 0;
    if (quant > 0) {
        if (quant <= 50)
            levels = 31 - (quant - 1) * 24 / 49; /* 1 -> 31, 50 -> 7 (v2-max) */
        else
            levels = 3 + (100 - quant) * 4 / 50; /* 100 -> 3 */
        if (levels < 3) levels = 3;
        if (levels > 31) levels = 31;
    }

    /* vertical sync tear: one strip, per frame. */
    int tys = 0, tsh = 0, tdy = 0;
    if (vt > 0 && fx_rand01(seed, t, 0u, G_TEARON) < (float)vt * 0.007f) {
        tsh = (height * vt) / 300; /* 50 -> h/6 (v2-max), 100 -> h/3 */
        if (tsh < 2) tsh = 2;
        if (tsh > height) tsh = height;
        int span = height - tsh;
        if (span < 0) span = 0;
        tys = (int)(fx_rand01(seed, t, 0u, G_TEARY) * (float)(span + 1));
        tdy = fx_round((fx_rand01(seed, t, 0u, G_TEARDY) - 0.5f)
                       * (float)(vt * 4)); /* 50 -> ±100 (v2-max), 100 -> ±200 */
    }

    /* Whole-frame "signal loss" catastrophe: driven by the strongest
     * element's severity (sev = max element param), so 50 = the v2-max
     * look exactly and only 100 crosses into it. sev 100 -> ±21% of
     * frame dim on ~42% of burst frames. */
    int sev = rgb > noise ? rgb : noise;
    if (bnd > sev) sev = bnd;
    if (blk > sev) sev = blk;
    if (scn > sev) sev = scn;
    if (quant > sev) sev = quant;
    if (vt > sev) sev = vt;
    int fwdx = 0, fwdy = 0;
    if (sev >= 80 &&
        fx_rand01(seed, t, 0u, G_FWON) < (float)(sev - 79) * 0.02f) {
        int hz = fx_rand01(seed, t, 0u, G_FWMD) < 0.5f;
        int span = (sev - 79) * (hz ? width : height) / 100; /* 100 -> 21% */
        int off = fx_round((fx_rand01(seed, t, 0u, G_FWDX) - 0.5f)
                           * 2.0f * (float)span);
        if (hz) fwdx = off;
        else    fwdy = off;
    }

    float noise_m = (float)(noise * 2); /* 50 -> ±100 (v2-max), 100 -> ±200 */

    /* Full-frame snapshot: displaced/split reads never see written output. */
    size_t nb = (size_t)height * (size_t)stride;
    uint8_t *src = malloc(nb);
    if (!src)
        return; /* cannot allocate; leave buffer untouched */
    memcpy(src, pixels, nb);

    for (int y = 0; y < height; y++) {
        int bxoff = 0;
        if (bnd > 0)
            for (int b = 0; b < GL_NBANDS; b++)
                if (band[b].hgt > 0 && y >= band[b].y0 && y < band[b].y0 + band[b].hgt) {
                    bxoff = band[b].dx;
                    break;
                }

        int sdx = 0, sfl = 0;
        if (nscan > 0)
            for (int i = 0; i < GL_NSCAN; i++)
                if (scan[i].row == y) {
                    if (scan[i].mode == 0) sdx = scan[i].dx;
                    else                  sfl = scan[i].fl;
                    break;
                }

        int dy = (tsh > 0 && y >= tys && y < tys + tsh) ? tdy : 0;

        uint8_t *drow = pixels + (size_t)y * (size_t)stride;

        for (int x = 0; x < width; x++) {
            /* whole-frame catastrophe shift first, then local elements. */
            int x0 = fx_clampi(x + fwdx, 0, width - 1);
            int y0 = fx_clampi(y + fwdy, 0, height - 1);
            int sx = fx_clampi(x0 + bxoff + sdx, 0, width - 1);

            /* block/tile: first hit wins. */
            int mode = -1, ox = 0, oy = 0, sr = 0, sg = 0, sb = 0;
            if (nblock)
                for (int i = 0; i < GL_NBLOCKS; i++) {
                    const gl_block_t *B = &block[i];
                    if (B->w > 0 && x >= B->x0 && x < B->x0 + B->w &&
                        y >= B->y0 && y < B->y0 + B->h) {
                        mode = B->mode; ox = B->ox; oy = B->oy;
                        sr = B->sr; sg = B->sg; sb = B->sb;
                        break;
                    }
                }

            if (mode == 0) { /* duplicate: tile shows content from a shifted point */
                sx = fx_clampi(x0 + ox, 0, width - 1);
            }

            /* Per-pixel source row: a mode-0 shift must not leak to later
             * pixels in the row (row-scoped sy did exactly that). */
            int sy = (mode == 0) ? fx_clampi(y0 + oy, 0, height - 1)
                                 : fx_clampi(y0 - dy, 0, height - 1);
            const uint8_t *srow = src + (size_t)sy * (size_t)stride;

            int rx = fx_clampi(sx + dir * k, 0, width - 1);
            int bx = fx_clampi(sx - dir * k, 0, width - 1);

            int cr = srow[(size_t)rx * 3 + 0];
            int cg = srow[(size_t)sx * 3 + 1];
            int cb = srow[(size_t)bx * 3 + 2];

            if (sfl > 0) { cr = (cr * sfl) / 100; cg = (cg * sfl) / 100; cb = (cb * sfl) / 100; }
            if (mode == 1) { cr = 255 - cr; cg = 255 - cg; cb = 255 - cb; }
            if (mode == 2) { cr = sr; cg = sg; cb = sb; }
            if (levels > 1) {
                cr = gl_posterize(cr, levels);
                cg = gl_posterize(cg, levels);
                cb = gl_posterize(cb, levels);
            }

            int n = 0;
            if (noise_m > 0.0f) {
                uint32_t pid = (uint32_t)y * (uint32_t)width + (uint32_t)x;
                n = fx_round((fx_rand01(seed, t, pid, G_NOISE) - 0.5f)
                             * 2.0f * noise_m);
            }

            uint8_t *d = drow + (size_t)x * 3;
            d[0] = fx_byte(cr + n);
            d[1] = fx_byte(cg + n);
            d[2] = fx_byte(cb + n);
        }
    }

    free(src);
}
