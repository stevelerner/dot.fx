/* retrofx.c — frei0r adapters for the retrofx core effects.
 *
 * One source file, thirteen plugins: build with
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_SCANLINES   -> vid.scanlines
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_SHADOWMASK  -> vid.shadowmask
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_CHROMABLOOD -> vid.chromablood
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_LUMAR       -> vid.lumar
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_RAINBOW     -> vid.rainbow
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_BARREL      -> vid.barrel
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_BLOOM       -> vid.bloom
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_VIGNETTE    -> vid.vignette
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_OVERSCAN    -> vid.overscan
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_TAPEWOW     -> vid.tapewow
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_VID_GLITCH      -> vid.glitch (generic stackable glitch)
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_DOTGATE         -> dot.gate (subject-gated dot map)
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_DOTPORTAL       -> dot.portal (subject turned into dots)
 *   -DRETROFX_EFFECT=RETROFX_EFFECT_DOT_SPACENGRAVE   -> dot.spacengrave (scanlines that become dots on the subject)
 * (default: vid.scanlines). Each build produces one .dylib/.so exposing the
 * f0r_* ABI symbols; frei0r is one plugin per library.
 *
 * Frame format per the frei0r spec: packed RGBA8888, one uint32_t per
 * pixel. The core takes 8-bit RGB with byte stride, so we copy RGB out of
 * RGBA (packed RGB3, stride = width*3), run the core, copy back, and pass
 * the alpha through unchanged.
 * Time: frei0r time is seconds; the core takes an int frame index — legacy
 * effects use
 * (int)time (first-cut conversion, documented). The two dot effects
 * (dot.gate/dot.portal), vid.glitch and vid.tapewow animate at 24 fps and
 * convert time*24 instead — the glitch needs sub-second block jumps and
 * tapewow's period is in frames, which int seconds can't express.
 */

#include <assert.h>
#include <stdlib.h>

#include "frei0r.h"

/*
 * Effect selector constants — plain #define integers so the #if below
 * can compare them (preprocessor #if only sees macros + literals, not
 * The build picks exactly one via -DRETROFX_EFFECT=0|1|2|3|4.
 */
#define RETROFX_EFFECT_VID_SCANLINES    0
#define RETROFX_EFFECT_VID_SHADOWMASK   1
#define RETROFX_EFFECT_VID_CHROMABLOOD  2
#define RETROFX_EFFECT_VID_LUMAR        3
#define RETROFX_EFFECT_VID_RAINBOW      4
#define RETROFX_EFFECT_VID_BARREL       5
#define RETROFX_EFFECT_VID_BLOOM        6
#define RETROFX_EFFECT_VID_VIGNETTE     7
#define RETROFX_EFFECT_VID_OVERSCAN     8
#define RETROFX_EFFECT_VID_TAPEWOW      9
#define RETROFX_EFFECT_VID_GLITCH       12
#define RETROFX_EFFECT_DOTGATE         10
#define RETROFX_EFFECT_DOTPORTAL       13
#define RETROFX_EFFECT_DOT_SPACENGRAVE 14

#ifndef RETROFX_EFFECT
#define RETROFX_EFFECT RETROFX_EFFECT_VID_SCANLINES
#endif

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
#  include "vhs.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
#  include "vhs.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
#  include "vhs.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
#  include "crt.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
#  include "vhs.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
#  include "glitch.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
#  include "dot.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
#  include "dot.h"
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
#  include "dot.h"
#else
#  error "RETROFX_EFFECT must be a VID_* or DOT* effect"
#endif

typedef struct {
    unsigned int width;
    unsigned int height;

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    double intensity;     /* 0..100, 0 = off, 50 = default look */
    double period;        /* >= 2 */
    double offset;        /* rows */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    int   type;           /* 0 grille, 1 slot, 2 triad */
    double intensity;     /* 0..100, 0 = off, 50 = default look */
    double pitch;         /* >= 1 */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    double amount;        /* 0..100, 0 = off, 50 = 8 px shift, 100 = 16 px */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    double amount;        /* 0..100, 0 = off, 50 = 192 luma peak, 100 = 384 */
    double wavelength;    /* >= 2 */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    double amount;        /* 0..100, 0 = off, 50 = 8 px shift, 100 = 16 px */
    double period_y;      /* >= 2, rows per beat cycle */
    double period_t;      /* >= 2, frames per beat cycle */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    double amount;        /* 0..100, 0 = off, 50 = 0.50, 100 = 0.75 */
    double zoom;          /* 0..100, 0 = off, 100 = zoom by 1+amount (margin vanishes) */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    double amount;        /* 0..100, 0 = off, 50 = 1.0, 100 = 2.0 */
    float threshold;      /* 0..1, luma cutoff */
    double radius;        /* >= 0 */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    double amount;        /* 0..100, 0 = off, 50 = 1.0 (black corners, saturated above) */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    double radius;        /* >= 0, corner radius in pixels */
    double margin;        /* >= 0, bezel crop in pixels */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    double amplitude;     /* >= 0, drift amplitude in px, 0 = off, 50 = default look */
    double period;        /* >= 2, frames per drift cycle */
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    double amount;        /* 0..100, burst density (time-based trigger); 0 = off */
    double rgb;           /* 0..100 strength, RGB channel split; 0 = off */
    double noise;         /* 0..100, static/grain intensity; 0 = off */
    double bands;         /* 0..100, horizontal slice displacement px; 0 = off */
    double blocks;        /* 0..100, max block-tile size px; 0 = off */
    double scan;          /* 0..100, scanline jitter; 0 = off */
    double quant;         /* 0..100, posterize strength; 0 = off */
    double vtear;        /* 0..100, vertical-sync tear px; 0 = off */
    double seed;         /* 0..999, reproducible variation */
    int dual_frames, dual_bad_frames;
    long long dual_diffpix, dual_maxdiff;
    int metal_warned;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    double size;          /* >= 1, dot pitch in px (grid step = size/2) */
    double speed;         /* >= 0, wobble amplitude in px */
    double gate;          /* 1..100, subject-gate knee x100 (binary, inside the Otsu subject region) */
    int dual_frames, dual_bad_frames;
    long long dual_diffpix, dual_maxdiff;
    int metal_warned;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    double size;          /* >= 1, dot pitch in px (grid step = size/2) */
    double fill;          /* 0..100, dot radius as % of step */
    double gate;          /* 1..100, subject-gate knee x100 */
    double halftone;      /* 0..100, how strongly radius tracks tone */
    double relief;        /* 0..200, gradient displacement as % of step */
    double level;         /* 0..200, brightness gain x100 (100 = 1x) */
    double color;         /* 0..360, dot hue in degrees (125 = green default) */
    double glow;          /* 0..100, aura around each dot (0 = off, 40 = default) */
    int dual_frames, dual_bad_frames;
    long long dual_diffpix, dual_maxdiff;
    int metal_warned;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    double size;          /* 0 = off (identity), else >= 2, scanline pitch in px */
    double fill;          /* 0..100, stroke width as % of the pitch */
    double halftone;      /* 0..100, how strongly coverage tracks tone (engraved shading) */
    double line;          /* 0..100, baseline coverage — line solidity everywhere */
    double level;         /* 0..200, ink brightness x100 (100 = 1x) */
    double glow;          /* grain: 0..100, texture break-up into dashes (core param: grain) */
    double color;         /* 0..360, hue in degrees (360 = white, no tint) */
    double dim;           /* 0..100, source dimming (0 = photo visible, 100 = black behind) */
    double gate;          /* 0 = full-frame ink; 1..100 = subject-gated knee (portal_split) */
    int dual_frames, dual_bad_frames;
    long long dual_diffpix, dual_maxdiff;
    int metal_warned;
#endif
} retrofx_instance_t;

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
/* Only bloom keeps a 0..1 float dial (the luma threshold). */
static void clamp01f(float *v)
{
    if (*v < 0.0f) *v = 0.0f;
    if (*v > 1.0f) *v = 1.0f;
}
#endif

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES || RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK || \
    RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD || RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR || \
    RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW || RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL || \
    RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM || RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
/* Strength dial clamped to 0..100 (0 = off, 50 = default look). */
static void clamp100(double *v)
{
    if (*v < 0.0) *v = 0.0;
    if (*v > 100.0) *v = 100.0;
}
#endif

#if RETROFX_EFFECT != RETROFX_EFFECT_VID_BARREL && RETROFX_EFFECT != RETROFX_EFFECT_VID_VIGNETTE && \
    RETROFX_EFFECT != RETROFX_EFFECT_VID_CHROMABLOOD
/* Barrel, vignette and chromablood have no int params and do not call this. */
static int as_int(double v, int lo, int hi)
{
    int i = (int)v;
    if (i < lo) i = lo;
    if (i > hi) i = hi;
    return i;
}
#endif

#if RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE || RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH || \
    RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL || RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
/* Runtime backend selection (env RETROFX_BACKEND, read once and cached):
 *   cpu   (default) — C core; portable reference implementation
 *   metal — Metal (GPU) implementation (core/metal_fx.m), byte-identical target
 *   dual  — run BOTH, byte-compare, return the CPU result (cross-check)
 * Metal unavailability or failure degrades to the CPU core with one warning. */
#  include <stdio.h>
#  include <string.h>
#  include "metal_fx.h"

typedef enum { RETROFX_BE_CPU = 0, RETROFX_BE_METAL = 1, RETROFX_BE_DUAL = 2 }
    retrofx_backend_t;

static retrofx_backend_t retrofx_backend(void)
{
    static int cached = -2;
    if (cached == -2) {
        const char *e = getenv("RETROFX_BACKEND");
        if (e != NULL && strcmp(e, "metal") == 0)
            cached = RETROFX_BE_METAL;
        else if (e != NULL && strcmp(e, "dual") == 0)
            cached = RETROFX_BE_DUAL;
        else
            cached = RETROFX_BE_CPU;
    }
    return (retrofx_backend_t)cached;
}

/* dual mode: byte-compare CPU (reference) vs Metal; report to stderr. */
static void retrofx_dual_diff(retrofx_instance_t *inst, int frame,
                              const uint8_t *cpu, const uint8_t *gpu, size_t nb)
{
    long long diffpix = 0, maxdiff = 0;
    for (size_t i = 0; i < nb; i++) {
        int d = (int)cpu[i] - (int)gpu[i];
        if (d < 0) d = -d;
        if (d != 0) {
            diffpix++;
            if (d > maxdiff) maxdiff = d;
        }
    }
    inst->dual_frames++;
    if (diffpix != 0) {
        inst->dual_bad_frames++;
        inst->dual_diffpix += diffpix;
        if (maxdiff > inst->dual_maxdiff) inst->dual_maxdiff = maxdiff;
        fprintf(stderr, "retrofx dual: frame %d MISMATCH: %lld px differ (max Δ %lld)\n",
                frame, diffpix, maxdiff);
    }
}
#endif

/* Backend dispatch (RETROFX_BACKEND) for the Metal-enabled effects —
 * one shared path; the per-effect wrappers in this #if chain bind the
 * core + Metal function pair. CPU stays the reference; Metal is
 * byte-identical by construction (metal_fx.m); dual runs both and
 * byte-compares, returning the CPU result. The casts are safe: all
 * four pairs share the (uint8_t*, int, int, int, const <params>*, int)
 * signature shape. */
typedef void (*retrofx_cpu_fn)(uint8_t *, int, int, int, const void *, int);
typedef int  (*retrofx_metal_fn)(uint8_t *, int, int, int, const void *, int);

static void retrofx_dispatch(retrofx_instance_t *inst, uint8_t *rgb,
                             int w, int h, int stride,
                             const void *p, int frame,
                             retrofx_cpu_fn cpu, retrofx_metal_fn metal)
{
    retrofx_backend_t be = retrofx_backend();
    if (be == RETROFX_BE_CPU) {
        cpu(rgb, w, h, stride, p, frame);
        return;
    }
    /* DUAL: both passes must see the unmodified input — take the reference
     * copy BEFORE Metal runs. */
    size_t nb = (size_t)w * (size_t)h * 3;
    uint8_t *cref = (be == RETROFX_BE_DUAL) ? malloc(nb) : NULL;
    if (be == RETROFX_BE_DUAL && cref == NULL) {
        /* No scratch for the reference copy: run Metal, fall back to the
         * CPU core — with the same one-time warning as every other
         * fallback path. */
        int mrc = (metal_fx_init() == 0)
            ? metal(rgb, w, h, stride, p, frame) : -1;
        if (mrc != 0) {
            if (!inst->metal_warned) {
                fprintf(stderr, "retrofx: Metal unavailable/failed; using CPU core\n");
                inst->metal_warned = 1;
            }
            cpu(rgb, w, h, stride, p, frame);
        }
        return;
    }
    if (cref != NULL) {
        memcpy(cref, rgb, nb);
        cpu(cref, w, h, stride, p, frame); /* reference */
    }
    int mrc = (metal_fx_init() == 0)
        ? metal(rgb, w, h, stride, p, frame) : -1;
    if (cref != NULL) {
        if (mrc != 0) {
            if (!inst->metal_warned) {
                fprintf(stderr, "retrofx: Metal unavailable/failed; dual degraded to CPU\n");
                inst->metal_warned = 1;
            }
        } else {
            retrofx_dual_diff(inst, frame, cref, rgb, nb);
        }
        memcpy(rgb, cref, nb); /* return the CPU reference */
        free(cref);
        return;
    }
    /* METAL mode */
    if (mrc != 0) {
        if (!inst->metal_warned) {
            fprintf(stderr, "retrofx: Metal unavailable/failed; using CPU core\n");
            inst->metal_warned = 1;
        }
        cpu(rgb, w, h, stride, p, frame);
    }
}

#if RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
static void retrofx_run_dotgate(retrofx_instance_t *inst, uint8_t *rgb,
                                int w, int h, int stride,
                                const dotgate_params_t *p, int frame)
{
    retrofx_dispatch(inst, rgb, w, h, stride, p, frame,
                     (retrofx_cpu_fn)dotgate, (retrofx_metal_fn)metal_dotgate);
}
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
static void retrofx_run_spacengrave(retrofx_instance_t *inst, uint8_t *rgb,
                                    int w, int h, int stride,
                                    const dot_spacengrave_params_t *p, int frame)
{
    retrofx_dispatch(inst, rgb, w, h, stride, p, frame,
                     (retrofx_cpu_fn)dot_spacengrave,
                     (retrofx_metal_fn)metal_spacengrave);
}
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
static void retrofx_run_vidglitch(retrofx_instance_t *inst, uint8_t *rgb,
                                  int w, int h, int stride,
                                  const vid_glitch_params_t *p, int frame)
{
    retrofx_dispatch(inst, rgb, w, h, stride, p, frame,
                     (retrofx_cpu_fn)vid_glitch,
                     (retrofx_metal_fn)metal_vid_glitch);
}
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
static void retrofx_run_dotportal(retrofx_instance_t *inst, uint8_t *rgb,
                                  int w, int h, int stride,
                                  const dotportal_params_t *p, int frame)
{
    retrofx_dispatch(inst, rgb, w, h, stride, p, frame,
                     (retrofx_cpu_fn)dotportal,
                     (retrofx_metal_fn)metal_dotportal);
}
#endif

int f0r_init(void)
{
    return 1;
}

void f0r_deinit(void)
{
}

void f0r_get_plugin_info(f0r_plugin_info_t *info)
{
#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    info->name = "retrofx_vid_scanlines";
    info->explanation = "CRT scanline darkening over a cosine row profile.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    info->name = "retrofx_vid_shadowmask";
    info->explanation = "CRT shadow mask: aperture grille, slot mask, or dot triad.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    info->name = "retrofx_vid_chromablood";
    info->explanation = "VHS chromablood: asymmetric RGB fringe on vertical edges (v2).";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    info->name = "retrofx_vid_lumar";
    info->explanation = "VHS luma ringing around high-contrast edges.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    info->name = "retrofx_vid_rainbow";
    info->explanation = "VHS rainbow phase (dot crawl): crawling asymmetric fringe on edges (v2).";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    info->name = "retrofx_vid_barrel";
    info->explanation = "CRT barrel curvature: convex-screen parabolic remap, bilinear sampling.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    info->name = "retrofx_vid_bloom";
    info->explanation = "CRT bloom: soft luma-gated phosphor glow added over highlights.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    info->name = "retrofx_vid_vignette";
    info->explanation = "CRT vignette: parabolic brightness falloff toward the corners.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    info->name = "retrofx_vid_overscan";
    info->explanation = "CRT overscan: rounded-bezel cutout of the frame border and corners.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    info->name = "retrofx_vid_tapewow";
    info->explanation = "VHS tape wow: slow vertical drift of the whole picture (tape speed wobble).";
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    info->name = "retrofx_vid_glitch";
    info->explanation = "Pro burst glitch: intermittent tears, block tiles, scanlines, RGB split, posterize, static — corrupts any frame.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    info->name = "retrofx_dotgate";
    info->explanation = "Subject-gated dot map: the frame's subject becomes dots on black; slow per-dot wobble.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    info->name = "retrofx_dotportal";
    info->explanation = "Turns the subject into a field of discrete dots: separated, sized by tone, displaced along the luma gradient.";
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    info->name = "retrofx_dot_spacengrave";
    info->explanation = "Engraved scanline overlay over the source: solid lines over flat areas that break into irregular dashes over texture — a dollar-bill etching.";
#endif
    info->author = "retrofx";
    info->plugin_type = F0R_PLUGIN_TYPE_FILTER;
    info->color_model = F0R_COLOR_MODEL_RGBA8888;
    info->frei0r_version = FREI0R_MAJOR_VERSION;
    info->major_version = 1;
    info->minor_version = 0;
#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    info->num_params = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    info->num_params = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    info->num_params = 1;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    info->num_params = 2;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    info->num_params = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    info->num_params = 2;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    info->num_params = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    info->num_params = 1;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    info->num_params = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    info->num_params = 9;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    info->num_params = 8;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    info->num_params = 9;
#else
    info->num_params = 2;
#endif
}

void f0r_get_param_info(f0r_param_info_t *pinfo, int index)
{
    pinfo->type = F0R_PARAM_DOUBLE;
#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    switch (index) {
    case 0:
        pinfo->name = "intensity";
        pinfo->explanation = "Scanline depth 0..100 (0 = off, 50 = default, 100 = full).";
        break;
    case 1:
        pinfo->name = "period";
        pinfo->explanation = "Rows per scanline cycle, >= 2.";
        break;
    case 2:
        pinfo->name = "offset";
        pinfo->explanation = "Row offset of the profile.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    switch (index) {
    case 0:
        pinfo->name = "type";
        pinfo->explanation = "0 aperture grille, 1 slot mask, 2 dot triad.";
        break;
    case 1:
        pinfo->name = "intensity";
        pinfo->explanation = "Mask depth 0..100 (0 = off, 50 = default, 100 = full mask).";
        break;
    case 2:
        pinfo->name = "pitch";
        pinfo->explanation = "Dot/stripe pitch in pixels, >= 1.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Fringe strength 0..100 (0 = off, 50 = 8 px shift, 100 = 16 px).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Ring strength 0..100 (0 = off, 50 = 192 luma peak, 100 = 384).";
        break;
    case 1:
        pinfo->name = "wavelength";
        pinfo->explanation = "Ring wavelength in pixels, >= 2.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Crawl strength 0..100 (0 = off, 50 = 8 px max shift, 100 = 16 px).";
        break;
    case 1:
        pinfo->name = "period_y";
        pinfo->explanation = "Rows per crawl beat cycle, >= 2.";
        break;
    case 2:
        pinfo->name = "period_t";
        pinfo->explanation = "Frames per crawl beat cycle, >= 2.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Curvature strength 0..100 (0 = off, 50 = 0.50, 100 = 0.75).";
        break;
    case 1:
        pinfo->name = "zoom";
        pinfo->explanation = "Zoom-in 0..100 (0 = off; 100 = zoom by 1+amount so the curved margin shows content — no edge clamping).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Bloom strength 0..100 (0 = off, 50 = default, 100 = double).";
        break;
    case 1:
        pinfo->name = "threshold";
        pinfo->explanation = "Luma cutoff 0..1 above which pixels bloom.";
        break;
    case 2:
        pinfo->name = "radius";
        pinfo->explanation = "Glow spread in pixels, >= 0 (0 = off).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Corner falloff 0..100 (0 = off, 50 = black corners; the model saturates at 50).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    switch (index) {
    case 0:
        pinfo->name = "radius";
        pinfo->explanation = "Rounded-corner radius in pixels, >= 0 (0 = sharp corner).";
        break;
    case 1:
        pinfo->name = "margin";
        pinfo->explanation = "Bezel crop in pixels, >= 0 (0 = none).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    switch (index) {
    case 0:
        pinfo->name = "amplitude";
        pinfo->explanation = "Drift amplitude in pixels, >= 0 (0 = off, 50 = default, 100 = strong).";
        break;
    case 1:
        pinfo->name = "period";
        pinfo->explanation = "Frames per drift cycle, >= 2.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    switch (index) {
    case 0:
        pinfo->name = "amount";
        pinfo->explanation = "Burst density (time-based trigger) 0..100; 0 = off/identity; strongest element >= 80: whole-frame signal-loss jumps.";
        break;
    case 1:
        pinfo->name = "rgb";
        pinfo->explanation = "RGB channel split strength 0..100 (50 = ±32 px, 100 = ±64 px); 0 = off.";
        break;
    case 2:
        pinfo->name = "noise";
        pinfo->explanation = "Static/grain intensity 0..100 (50 = ±100, 100 = ±200); 0 = off.";
        break;
    case 3:
        pinfo->name = "bands";
        pinfo->explanation = "Horizontal slice tear strength 0..100 (50 = ±100 px, 100 = ±200 px); 0 = off.";
        break;
    case 4:
        pinfo->name = "blocks";
        pinfo->explanation = "Block-tile corruption strength 0..100 (50 = tiles up to 100 px, 100 = 200 px); 0 = off.";
        break;
    case 5:
        pinfo->name = "scan";
        pinfo->explanation = "Scanline jitter strength 0..100: rows per burst = 1+scan*39/100 (0 = off).";
        break;
    case 6:
        pinfo->name = "quant";
        pinfo->explanation = "Color quantization (posterize) strength 0..100 (50 = 7 levels, 100 = 3 levels); 0 = off.";
        break;
    case 7:
        pinfo->name = "vtear";
        pinfo->explanation = "Vertical-sync tear strength 0..100 (50 = h/6 strip ±100 px, 100 = h/3 strip ±200 px); 0 = off.";
        break;
    case 8:
        pinfo->name = "seed";
        pinfo->explanation = "Reproducible variation, 0..999.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    switch (index) {
    case 0:
        pinfo->name = "size";
        pinfo->explanation = "Dot grid pitch in px (step = size/2, disc r = step*0.8); 0 = off, else >= 2.";
        break;
    case 1:
        pinfo->name = "speed";
        pinfo->explanation = "Wobble amplitude in px, >= 0 (0 = static).";
        break;
    case 2:
        pinfo->name = "gate";
        pinfo->explanation = "Subject-gate knee x100 (binary, inside the Otsu subject region); 1..100, default 10.";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    switch (index) {
    case 0:
        pinfo->name = "size";
        pinfo->explanation = "Dot pitch in px (grid step = size/2); 0 = off, else >= 1.";
        break;
    case 1:
        pinfo->name = "fill";
        pinfo->explanation = "Dot radius as % of step, 0..100 (35 = separated dots, 80 = dot.gate's gap-free radius).";
        break;
    case 2:
        pinfo->name = "gate";
        pinfo->explanation = "Subject-gate knee x100, 1..100 (hard threshold: dot on where local contrast >= knee AND its (possibly relief-displaced) center lies in the subject region — the Otsu minority luma side — off below; lower = more of the subject becomes dots).";
        break;
    case 3:
        pinfo->name = "halftone";
        pinfo->explanation = "How strongly dot radius tracks tone, 0..100 (0 = uniform size).";
        break;
    case 4:
        pinfo->name = "relief";
        pinfo->explanation = "Gradient displacement as % of step, 0..200 (0 = flat lattice).";
        break;
    case 5:
        pinfo->name = "level";
        pinfo->explanation = "Brightness gain x100, 0..200 (100 = 1x, 200 = 2x).";
        break;
    case 6:
        pinfo->name = "glow";
        pinfo->explanation = "Aura around each dot, 0..100 (0 = off, 60 = default); larger + dimmer disc behind each dot.";
        break;
    case 7:
        pinfo->name = "color";
        pinfo->explanation = "Dot hue in degrees 0..360 (0 = red, 125 = green; 360 = white, no tint).";
        break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    switch (index) {
    case 0:
        pinfo->name = "size";
        pinfo->explanation = "Scanline pitch in px (the vertical period); 0 = off (identity), else >= 2.";
        break;
    case 1:
        pinfo->name = "fill";
        pinfo->explanation = "Stroke width as % of the pitch, 0..100 (45 = default).";
        break;
    case 2:
        pinfo->name = "halftone";
        pinfo->explanation = "How strongly coverage tracks tone, 0..100 (0 = no tone shading) — the engraved shading: darker areas break up more.";
        break;
    case 3:
        pinfo->name = "line";
        pinfo->explanation = "Baseline coverage, 0..100 — how solid the lines are everywhere (75 = default).";
        break;
    case 4:
        pinfo->name = "level";
        pinfo->explanation = "Ink brightness x100, 0..200 (100 = 1x, 200 = 2x).";
        break;
    case 5:
        pinfo->name = "grain";
        pinfo->explanation = "How strongly local texture breaks the line into dashes, 0..100 (65 = default) — the face fragments, flat sky stays solid.";
        break;
    case 6:
        pinfo->name = "color";
        pinfo->explanation = "Hue in degrees 0..360 (0 = red, 125 = green; 360 = white, no tint).";
        break;
    case 7:
        pinfo->name = "dim";
        pinfo->explanation = "Source dimming, 0..100 (0 = photo fully visible, 100 = pure black behind the ink, as in dot.gate / dot.portal).";
        break;
    case 8:
        pinfo->name = "gate";
        pinfo->explanation = "Subject gate: 0 = full-frame ink; 1..100 = contrast-knee x100, ink only on the subject (the same portal_split region dot.gate / dot.portal use).";
        break;
    }
#endif
}

f0r_instance_t f0r_construct(unsigned int width, unsigned int height)
{
    retrofx_instance_t *inst = calloc(1, sizeof(*inst));
    if (!inst)
        return NULL;
    inst->width = width;
    inst->height = height;

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    inst->intensity = 50;
    inst->period = 3;
    inst->offset = 0;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    inst->type = CRT_MASK_GRILLE;
    inst->intensity = 50;
    inst->pitch = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    inst->amount = 50;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    inst->amount = 50;
    inst->wavelength = 4;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    inst->amount = 50;
    inst->period_y = 48;
    inst->period_t = 16;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    inst->amount = 50;
    inst->zoom = 0;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    inst->amount = 50;
    inst->threshold = 0.10f;
    inst->radius = 16;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    inst->amount = 50;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    inst->radius = 48;
    inst->margin = 8;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    inst->amplitude = 50;
    inst->period = 90;
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    inst->amount = 30;
    inst->rgb = 4;
    inst->noise = 8;
    inst->bands = 45;
    inst->blocks = 0;
    inst->scan = 0;
    inst->quant = 0;
    inst->vtear = 28;
    inst->seed = 3;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    inst->size = 24;
    inst->speed = 6;
    inst->gate = 10;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    inst->size = 24;
    inst->fill = 55;
    inst->gate = 5;
    inst->halftone = 70;
    inst->relief = 50;
    inst->level = 200;
    inst->color = 360;
    inst->glow = 60;
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    inst->size = 5;
    inst->fill = 45;
    inst->halftone = 60;
    inst->line = 75;
    inst->level = 100;
    inst->glow = 65;
    inst->color = 360;
    inst->dim = 100;
    inst->gate = 30;
#endif
    return (f0r_instance_t)inst;
}

void f0r_destruct(f0r_instance_t instance)
{
#if RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE || RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL || \
    RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH || RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    retrofx_instance_t *inst = (retrofx_instance_t *)instance;
    if (inst->dual_frames > 0) {
        fprintf(stderr,
                "retrofx dual: %d frames cross-checked, %d mismatched, "
                "%lld differing px, max Δ %lld — %s\n",
                inst->dual_frames, inst->dual_bad_frames,
                inst->dual_diffpix, inst->dual_maxdiff,
                inst->dual_bad_frames == 0 ? "byte-identical" : "MISMATCH");
    }
#endif
    free(instance);
}

void f0r_set_param_value(f0r_instance_t instance, f0r_param_t param, int index)
{
    retrofx_instance_t *inst = (retrofx_instance_t *)instance;
    double v = *(double *)param;

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    switch (index) {
    case 0: inst->intensity = v; clamp100(&inst->intensity); break;
    case 1: inst->period = v; break;
    case 2: inst->offset = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    switch (index) {
    case 0: inst->type = as_int(v, 0, 2); break;
    case 1: inst->intensity = v; clamp100(&inst->intensity); break;
    case 2: inst->pitch = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    case 1: inst->wavelength = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    case 1: inst->period_y = v; break;
    case 2: inst->period_t = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    case 1: inst->zoom = v; clamp100(&inst->zoom); break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    case 1: inst->threshold = (float)v; clamp01f(&inst->threshold); break;
    case 2: inst->radius = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    switch (index) {
    case 0: inst->amount = v; clamp100(&inst->amount); break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    switch (index) {
    case 0: inst->radius = v; break;
    case 1: inst->margin = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    switch (index) {
    case 0: inst->amplitude = v; break;
    case 1: inst->period = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    switch (index) {
    case 0: inst->amount = v; break;
    case 1: inst->rgb = v; break;
    case 2: inst->noise = v; break;
    case 3: inst->bands = v; break;
    case 4: inst->blocks = v; break;
    case 5: inst->scan = v; break;
    case 6: inst->quant = v; break;
    case 7: inst->vtear = v; break;
    case 8: inst->seed = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    switch (index) {
    case 0: inst->size = v; break;
    case 1: inst->speed = v; break;
    case 2: inst->gate = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    switch (index) {
    case 0: inst->size = v; break;
    case 1: inst->fill = v; break;
    case 2: inst->gate = v; break;
    case 3: inst->halftone = v; break;
    case 4: inst->relief = v; break;
    case 5: inst->level = v; break;
    case 6: inst->glow = v; break;
    case 7: inst->color = v; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    switch (index) {
    case 0: inst->size = v; break;
    case 1: inst->fill = v; break;
    case 2: inst->halftone = v; break;
    case 3: inst->line = v; break;
    case 4: inst->level = v; break;
    case 5: inst->glow = v; break;
    case 6: inst->color = v; break;
    case 7: inst->dim = v; break;
    case 8: inst->gate = v; break;
    }
#endif
}

void f0r_get_param_value(f0r_instance_t instance, f0r_param_t param, int index)
{
    retrofx_instance_t *inst = (retrofx_instance_t *)instance;
    double v = 0.0;

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    switch (index) {
    case 0: v = inst->intensity; break;
    case 1: v = inst->period; break;
    case 2: v = inst->offset; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    switch (index) {
    case 0: v = (double)inst->type; break;
    case 1: v = inst->intensity; break;
    case 2: v = inst->pitch; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    switch (index) {
    case 0: v = inst->amount; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    switch (index) {
    case 0: v = inst->amount; break;
    case 1: v = inst->wavelength; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    switch (index) {
    case 0: v = inst->amount; break;
    case 1: v = inst->period_y; break;
    case 2: v = inst->period_t; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    switch (index) {
    case 0: v = inst->amount; break;
    case 1: v = inst->zoom; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    switch (index) {
    case 0: v = inst->amount; break;
    case 1: v = inst->threshold; break;
    case 2: v = inst->radius; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    switch (index) {
    case 0: v = inst->amount; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    switch (index) {
    case 0: v = inst->radius; break;
    case 1: v = inst->margin; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    switch (index) {
    case 0: v = inst->amplitude; break;
    case 1: v = inst->period; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    switch (index) {
    case 0: v = inst->amount; break;
    case 1: v = inst->rgb; break;
    case 2: v = inst->noise; break;
    case 3: v = inst->bands; break;
    case 4: v = inst->blocks; break;
    case 5: v = inst->scan; break;
    case 6: v = inst->quant; break;
    case 7: v = inst->vtear; break;
    case 8: v = inst->seed; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    switch (index) {
    case 0: v = inst->size; break;
    case 1: v = inst->speed; break;
    case 2: v = inst->gate; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    switch (index) {
    case 0: v = inst->size; break;
    case 1: v = inst->fill; break;
    case 2: v = inst->gate; break;
    case 3: v = inst->halftone; break;
    case 4: v = inst->relief; break;
    case 5: v = inst->level; break;
    case 6: v = inst->glow; break;
    case 7: v = inst->color; break;
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    switch (index) {
    case 0: v = inst->size; break;
    case 1: v = inst->fill; break;
    case 2: v = inst->halftone; break;
    case 3: v = inst->line; break;
    case 4: v = inst->level; break;
    case 5: v = inst->glow; break;
    case 6: v = inst->color; break;
    case 7: v = inst->dim; break;
    case 8: v = inst->gate; break;
    }
#endif
    *(double *)param = v;
}

void f0r_update(f0r_instance_t instance, double time,
                const uint32_t *inframe, uint32_t *outframe)
{
    assert(instance && inframe && outframe);
    retrofx_instance_t *inst = (retrofx_instance_t *)instance;
    int w = (int)inst->width;
    int h = (int)inst->height;
    size_t npix = (size_t)w * (size_t)h;
    int frame = (int)time; /* first-cut time->frame conversion */
#if RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE || RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH || \
    RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    /* Dot effects, vid.glitch and vid.tapewow animate at 24 fps: their
     * clocks (twinkle, glitch jumps, tape-drift period in frames) need
     * sub-second resolution, which int seconds can't express.
     * Legacy effects keep the int-seconds base above. */
    frame = (int)(time * 24.0);
#endif

    if (npix == 0)
        return;

    /* RGBA -> RGB (stride w*4 bytes).
     * Host convention (frei0r C API; how ffmpeg and Blender hand the frame
     * over): the uint32 array is memory-order RGBA — bytes R,G,B,A — so on
     little-endian R is the LOW byte. Copy byte-wise instead of shifting
     * (the old shifts assumed R in bits 16..23 and rotated R<->B for every
     * host that uses the standard layout — visible as cyan reading amber). */
    uint8_t *rgb = malloc((size_t)w * (size_t)h * 3);
    if (!rgb)
        return; /* leave outframe untouched */

    const uint8_t *inb = (const uint8_t *)inframe;
    for (size_t i = 0; i < npix; i++) {
        rgb[i * 3 + 0] = inb[i * 4 + 0];
        rgb[i * 3 + 1] = inb[i * 4 + 1];
        rgb[i * 3 + 2] = inb[i * 4 + 2];
    }

#if RETROFX_EFFECT == RETROFX_EFFECT_VID_SCANLINES
    {
        crt_scanlines_params_t p;
        p.intensity = (float)(inst->intensity * 0.5 / 50.0);
        p.period = as_int(inst->period, 2, 4096);
        p.offset = (int)inst->offset;
        crt_scanlines(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_SHADOWMASK
    {
        crt_shadow_mask_params_t p;
        p.type = (crt_mask_type_t)inst->type;
        p.intensity = (float)(inst->intensity * 0.6 / 50.0);
        p.pitch = as_int(inst->pitch, 1, 4096);
        crt_shadow_mask(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_CHROMABLOOD
    {
        vhs_chroma_bleed_params_t p;
        p.radius = (int)(inst->amount * 0.16 + 0.5); /* dial -> px (50 = 8, 100 = 16) */
        p.amount = 1.0f;
        vhs_chroma_bleed(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_LUMAR
    {
        vhs_luma_ring_params_t p;
        p.amount = (float)(inst->amount * 0.02);
        p.wavelength = as_int(inst->wavelength, 2, 4096);
        vhs_luma_ring(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_RAINBOW
    {
        vhs_rainbow_params_t p;
        p.amount = (float)(inst->amount * 0.02);
        p.period_y = as_int(inst->period_y, 2, 4096);
        p.period_t = as_int(inst->period_t, 2, 4096);
        vhs_rainbow_phase(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BARREL
    {
        crt_barrel_params_t p;
        /* Dial levels 25 = 0.35, 50 = 0.50, 100 = 0.75 were picked against
         * the OLD remap (the "carnival mirror" direction) and were kept as
         * approved once crt_barrel was flipped to the real CRT curvature
         * (edges recede, corners sink toward the centre). Out-of-frame
         * samples clamp to the edge, so content fills the frame — no
         * black rim (user request). zoom (default 0 = off) zooms the
         * picture in by 1 + amount, so the curved margin region shows
         * content instead of the clamped edge (100 = margin vanishes). */
        if (inst->amount <= 25.0)
            p.amount = (float)(inst->amount * 0.014);
        else if (inst->amount <= 50.0)
            p.amount = (float)(0.35 + 0.006 * (inst->amount - 25.0));
        else
            p.amount = (float)(0.5 + 0.005 * (inst->amount - 50.0));
        p.zoom = (float)(inst->zoom * 0.01);
        crt_barrel(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_BLOOM
    {
        crt_bloom_params_t p;
        p.amount = (float)(inst->amount * 0.02);
        p.threshold = inst->threshold;
        p.radius = as_int(inst->radius, 0, 4096);
        crt_bloom(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_VIGNETTE
    {
        crt_vignette_params_t p;
        p.amount = (float)(inst->amount * 0.02);
        crt_vignette(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_OVERSCAN
    {
        crt_overscan_params_t p;
        p.radius = as_int(inst->radius, 0, 4096);
        p.margin = as_int(inst->margin, 0, 4096);
        crt_overscan(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_TAPEWOW
    {
        vhs_tape_wow_params_t p;
        p.amplitude = as_int(inst->amplitude, 0, 4096);
        p.period = as_int(inst->period, 2, 4096);
        vhs_tape_wow(rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_VID_GLITCH
    {
        vid_glitch_params_t p;
        p.amount = as_int(inst->amount, 0, 100);  /* 0 = off (core guard) */
        p.rgb    = as_int(inst->rgb, 0, 100);
        p.noise  = as_int(inst->noise, 0, 100);
        p.bands  = as_int(inst->bands, 0, 100);
        p.blocks = as_int(inst->blocks, 0, 100);
        p.scan   = as_int(inst->scan, 0, 100);
        p.quant  = as_int(inst->quant, 0, 100);
        p.vtear  = as_int(inst->vtear, 0, 100);
        p.seed   = as_int(inst->seed, 0, 999);
        retrofx_run_vidglitch(inst, rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTGATE
    {
        dotgate_params_t p;
        p.size = as_int(inst->size, 0, 512);   /* 0 = off (core guard) */
        p.speed = as_int(inst->speed, 0, 4096);
        p.gate = as_int(inst->gate, 1, 100);
        retrofx_run_dotgate(inst, rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOTPORTAL
    {
        dotportal_params_t p;
        p.size = as_int(inst->size, 0, 512);        /* 0 = off (core guard) */
        p.fill = as_int(inst->fill, 0, 100);
        p.gate = as_int(inst->gate, 1, 100);
        p.halftone = as_int(inst->halftone, 0, 100);
        p.relief = as_int(inst->relief, 0, 200);
        p.level = as_int(inst->level, 0, 200);
        p.glow = as_int(inst->glow, 0, 100);
        p.color = as_int(inst->color, 0, 360);
        retrofx_run_dotportal(inst, rgb, w, h, w * 3, &p, frame);
    }
#elif RETROFX_EFFECT == RETROFX_EFFECT_DOT_SPACENGRAVE
    {
        dot_spacengrave_params_t p;
        p.size = as_int(inst->size, 0, 512);   /* 0 = off (identity) */
        p.fill = as_int(inst->fill, 0, 100);
        p.halftone = as_int(inst->halftone, 0, 100);
        p.line = as_int(inst->line, 0, 100);
        p.level = as_int(inst->level, 0, 200);
        p.grain = as_int(inst->glow, 0, 100);
        p.color = as_int(inst->color, 0, 360);
        p.dim = as_int(inst->dim, 0, 100);
        p.gate = as_int(inst->gate, 0, 100);
        retrofx_run_spacengrave(inst, rgb, w, h, w * 3, &p, frame);
    }
#endif

    /* RGB -> RGBA, alpha passed through */
    uint8_t *outb = (uint8_t *)outframe;
    for (size_t i = 0; i < npix; i++) {
        outb[i * 4 + 0] = rgb[i * 3 + 0];
        outb[i * 4 + 1] = rgb[i * 3 + 1];
        outb[i * 4 + 2] = rgb[i * 3 + 2];
        outb[i * 4 + 3] = inb[i * 4 + 3]; /* alpha passthrough */
    }

    free(rgb);
}
