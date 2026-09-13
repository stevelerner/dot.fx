/* f0r_host.c — minimal frei0r host driver for local testing.
 *
 * The Homebrew ffmpeg on this machine was built without the frei0r filter,
 * so this stands in for it: dlopen a plugin dylib, exercise the f0r_* ABI
 * exactly as a host would (init, plugin info, param info, construct,
 * set/get params, update, destruct), and verify the effect actually works:
 *
 *   tools/f0r_host <plugin.dylib> <mode> [w h]
 *
 *   mode=apply      run with defaults, expect output != input (at some
 *                   instant: time-animated effects are identity at t=0)
 *   mode=identity   run with effect off, expect output == input
 *   mode=roundtrip  set params, read them back, expect exact values
 *   mode=determinism re-render the same frame (with another frame in
 *                    between), expect byte-identical output — the §5
 *                    property, valid for time-animated effects too
 *
 * Test frame: 64x64 RGBA, deterministic gradient. Exits 0 on pass.
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frei0r.h"

typedef int (*init_fn)(void);
typedef void (*deinit_fn)(void);
typedef void (*get_plugin_info_fn)(f0r_plugin_info_t *);
typedef void (*get_param_info_fn)(f0r_param_info_t *, int);
typedef f0r_instance_t (*construct_fn)(unsigned int, unsigned int);
typedef void (*destruct_fn)(f0r_instance_t);
typedef void (*set_param_fn)(f0r_instance_t, f0r_param_t, int);
typedef void (*get_param_fn)(f0r_instance_t, f0r_param_t, int);
typedef void (*update_fn)(f0r_instance_t, double, const uint32_t *, uint32_t *);

static int g_fail = 0;

/* Per-plugin test vectors, kept in sync with frei0r-adapter/retrofx.c.
 * off: parameter values that must leave the frame byte-identical.
 * rt:  in-domain values that must read back exactly (float epsilon).
 * Params are positional per plugin — a generic vector is meaningless
 * across different param layouts (e.g. intensity is param 1 of 3 for
 * shadowmask but param 0 of 3 for scanlines). */
struct plugin_test { const char *name; double off[9]; double rt[9]; };
static const struct plugin_test plugin_tests[] = {
    { "retrofx_vid_scanlines",   { 0.0, 3.0, 0.0 },   { 37.0, 5.0, 2.0 } },
    { "retrofx_vid_shadowmask",  { 0.0, 0.0, 3.0 },   { 1.0, 35.0, 5.0 } },
    { "retrofx_vid_chromablood", { 0.0 },             { 31.0 } },
    { "retrofx_vid_lumar",       { 0.0, 4.0 },        { 21.0, 5.0 } },
    { "retrofx_vid_rainbow",     { 0.0, 48.0, 16.0 }, { 21.0, 40.0, 12.0 } },
    { "retrofx_vid_barrel",      { 0.0 },             { 70.0 } },
    { "retrofx_vid_bloom",       { 0.0, 0.5, 8.0 },   { 21.0, 0.33, 5.0 } },
    { "retrofx_vid_vignette",    { 0.0 },             { 21.0 } },
    { "retrofx_vid_overscan",    { 0.0, 0.0 },        { 33.0, 5.0 } },
    { "retrofx_vid_tapewow",     { 0.0, 90.0 },       { 3.5, 45.0 } },
    { "retrofx_vid_glitch",      { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 },
                                      { 50.0, 6.0, 12.0, 40.0, 32.0, 30.0, 60.0, 24.0, 7.0 } },
    { "retrofx_dotgate",   { 0.0, 0.0, 10.0 },     { 24.0, 6.0, 5.0 } },
    { "retrofx_dotportal", { 0.0, 0.0, 10.0, 0.0, 0.0, 100.0, 0.0, 125.0 }, { 24.0, 35.0, 5.0, 70.0, 50.0, 100.0, 40.0, 360.0 } },
    { "retrofx_dot_spacengrave", { 0.0, 0.0, 0.0, 0.0, 100.0, 0.0, 0.0, 0.0, 0.0 }, { 5.0, 85.0, 80.0, 68.0, 200.0, 60.0, 360.0, 100.0, 5.0 } },
};

static const struct plugin_test *find_test(const char *name)
{
    size_t n = sizeof plugin_tests / sizeof plugin_tests[0];
    for (size_t i = 0; i < n; i++)
        if (strcmp(plugin_tests[i].name, name) == 0)
            return &plugin_tests[i];
    return NULL;
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); g_fail = 1; } \
    else         { printf("ok: %s\n", msg); } \
} while (0)

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <plugin.dylib> <apply|identity|roundtrip|determinism> [w h]\n", argv[0]);
        return 2;
    }
    const char *path = argv[1];
    const char *mode = argv[2];
    unsigned int w = argc > 3 ? (unsigned int)atoi(argv[3]) : 64;
    unsigned int h = argc > 4 ? (unsigned int)atoi(argv[4]) : 64;
    if (w < 8 || h < 8) { w = 64; h = 64; }

    void *lib = dlopen(path, RTLD_NOW);
    if (!lib) {
        fprintf(stderr, "FAIL: dlopen: %s\n", dlerror());
        return 1;
    }
    init_fn f_init = (init_fn)dlsym(lib, "f0r_init");
    deinit_fn f_deinit = (deinit_fn)dlsym(lib, "f0r_deinit");
    get_plugin_info_fn f_gpi = (get_plugin_info_fn)dlsym(lib, "f0r_get_plugin_info");
    get_param_info_fn f_gpar = (get_param_info_fn)dlsym(lib, "f0r_get_param_info");
    construct_fn f_construct = (construct_fn)dlsym(lib, "f0r_construct");
    destruct_fn f_destruct = (destruct_fn)dlsym(lib, "f0r_destruct");
    set_param_fn f_set = (set_param_fn)dlsym(lib, "f0r_set_param_value");
    get_param_fn f_get = (get_param_fn)dlsym(lib, "f0r_get_param_value");
    update_fn f_update = (update_fn)dlsym(lib, "f0r_update");
    if (!f_init || !f_gpi || !f_gpar || !f_construct ||
        !f_destruct || !f_set || !f_get || !f_update) {
        fprintf(stderr, "FAIL: missing f0r_ symbol(s)\n");
        return 1;
    }
    CHECK(f_init() == 1, "f0r_init returns 1");

    f0r_plugin_info_t pi;
    memset(&pi, 0, sizeof(pi));
    f_gpi(&pi);
    printf("plugin: %s (%d params)\n", pi.name ? pi.name : "(null)", pi.num_params);
    CHECK(pi.name != NULL, "plugin has a name");
    CHECK(pi.color_model == F0R_COLOR_MODEL_RGBA8888, "color model is RGBA8888");

    char namebuf[64];
    for (int i = 0; i < pi.num_params; i++) {
        f0r_param_info_t pinfo;
        memset(&pinfo, 0, sizeof(pinfo));
        f_gpar(&pinfo, i);
        snprintf(namebuf, sizeof namebuf, "param %d '%s'", i, pinfo.name ? pinfo.name : "(null)");
        CHECK(pinfo.name != NULL && pinfo.type == F0R_PARAM_DOUBLE, namebuf);
    }

    f0r_instance_t inst = f_construct(w, h);
    CHECK(inst != NULL, "construct");
    if (!inst) return 1;

    /* Deterministic test frame: RGBA gradient. */
    size_t npix = (size_t)w * h;
    uint32_t *inframe = malloc(npix * 4);
    uint32_t *out1 = malloc(npix * 4);
    uint32_t *out2 = malloc(npix * 4);
    if (!inframe || !out1 || !out2) { fprintf(stderr, "FAIL: alloc\n"); return 1; }
    /* Deterministic test frame: smooth gradient PLUS a hard dark stripe
     * at x ~ w/2. Edge-localized effects (luma ring) need a real contrast
     * transition to bite, while the gradient keeps non-edge effects
     * (scanlines, mask, bleed) engaged. */
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            uint32_t r = (uint32_t)(x * 255 / (w - 1));
            uint32_t g = (uint32_t)(y * 255 / (h - 1));
            uint32_t b = (uint32_t)((x + y) * 128 / (w + h - 2));
            if (x >= w / 2 - 1 && x <= w / 2 + 1)
                r = g = b = 0;
            inframe[y * w + x] = (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16); /* memory-order RGBA (R low byte, LE) */
        }

    if (strcmp(mode, "apply") == 0) {
        /* Time-animated effects (tapewow) are byte-identity at t=0 by
         * design (sin phase zero), so probe a few instants and accept a
         * change at any of them. Static effects change at all of them. */
        const double ts[4] = { 0.0, 3.0, 7.0, 12.0 };
        int changed = 0;
        for (int i = 0; i < 4; i++) {
            f_update(inst, ts[i], inframe, out1);
            if (memcmp(inframe, out1, npix * 4) != 0) { changed = 1; break; }
        }
        CHECK(changed, "defaults change the frame (at some instant)");
    } else if (strcmp(mode, "identity") == 0) {
        /* Effect-off values: one vector per plugin (see plugin_tests). */
        const struct plugin_test *t = find_test(pi.name);
        if (!t) {
            printf("skip: no identity vector for plugin '%s' "
                   "(add one to f0r_host.c plugin_tests)\n", pi.name);
        } else {
            for (int i = 0; i < pi.num_params; i++) {
                double v = t->off[i];
                f_set(inst, (f0r_param_t)&v, i);
            }
            f_update(inst, 0.0, inframe, out1);
            CHECK(memcmp(inframe, out1, npix * 4) == 0,
                  "off params leave frame untouched");
        }
    } else if (strcmp(mode, "roundtrip") == 0) {
        const struct plugin_test *t = find_test(pi.name);
        if (!t) {
            printf("skip: no roundtrip vector for plugin '%s' "
                   "(add one to f0r_host.c plugin_tests)\n", pi.name);
        } else {
            for (int i = 0; i < pi.num_params; i++) {
                double v = t->rt[i];
                f_set(inst, (f0r_param_t)&v, i);
            }
            for (int i = 0; i < pi.num_params; i++) {
                double got = -1.0;
                f_get(inst, (f0r_param_t)&got, i);
                printf("param %d: set %g, got %g\n", i, t->rt[i], got);
                /* plugin stores intensities/amounts as float; allow float precision */
                double diff = got - t->rt[i]; if (diff < 0) diff = -diff;
                if (diff > 1e-6) g_fail = 1;
                else printf("ok: param %d roundtrip\n", i);
            }
        }
    } else if (strcmp(mode, "determinism") == 0) {
        /* §5: a frame must be byte-identical no matter what was rendered
         * before it. Render a different frame first, then the frame of
         * interest twice: the two renders must be identical. (Comparing
         * consecutive frames instead would fail time-animated effects by
         * design.) In-domain (roundtrip) params are set first so dual
         * cross-checks exercise the full param surface (e.g. dotgate at
         * its default size/speed).
         */
        const struct plugin_test *t = find_test(pi.name);
        if (t)
            for (int i = 0; i < pi.num_params; i++) {
                double v = t->rt[i];
                f_set(inst, (f0r_param_t)&v, i);
            }
        f_update(inst, 2.0, inframe, out1);
        f_update(inst, 7.0, inframe, out2);
        f_update(inst, 7.0, inframe, out1);
        CHECK(memcmp(out1, out2, npix * 4) == 0,
              "re-rendered frame byte-identical (§5)");
    } else {
        fprintf(stderr, "FAIL: unknown mode '%s'\n", mode);
        return 2;
    }

    f_destruct(inst);
    free(inframe);
    free(out1);
    free(out2);
    f_deinit();
    dlclose(lib);
    return g_fail ? 1 : 0;
}
