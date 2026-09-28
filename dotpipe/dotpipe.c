/* dotpipe — raw-video pipe effect host.
 *
 * ffmpeg as codec only, dotpipe as the effect engine (no frei0r, no
 * custom ffmpeg build — any ffmpeg works, even a minimal one):
 *
 *   ffmpeg -i in.mp4 -f rawvideo -pix_fmt rgb24 - \
 *     | dotpipe -w 720 -h 1280 --dotgate 28 8 10 \
 *     | ffmpeg -f rawvideo -pix_fmt rgb24 -s 720x1280 -r 24 -i - out.mp4
 *
 * Reads raw RGB24 frames (w*h*3 bytes each) from stdin, applies the
 * selected effects IN COMMAND-LINE ORDER (like an ffmpeg filter chain),
 * writes to stdout. No effects = byte-identical passthrough.
 *
 * Clock: like the frei0r adapter, legacy effects tick on whole seconds
 * and the 24 fps group (tapewow, dotgate, vid.glitch) ticks at 24 Hz,
 * so for a --fps N source (N may be fractional, e.g. 60000/1001) the
 * core frame index is (int)(frame*24/N) for the 24 fps group and
 * (int)(frame/N) otherwise — the adapter's own semantics.
 *
 * Usage:
 *   dotpipe -w <width> -h <height> [--fps 24] [options]
 *
 * Options (values are the raw core parameters):
 *   --scanlines <intensity 0-1> <period> <offset>
 *   --mask grille|slot|triad <intensity 0-1> <pitch>
 *   --barrel <amount 0-1> [zoom 0-1]
 *   --bloom <amount> [threshold 0-1] [radius]
 *   --vignette <amount 0-1>
 *   --overscan <radius> [margin]
 *   --bleed <radius> [amount]
 *   --lumar <amount 0-1> <wavelength>
 *   --rainbow <amount 0-1> <period_y> <period_t>
 *   --wow <amplitude> [period]
 *   --dotgate <size> [speed [gate]]
 *   --dotportal <size> [fill [gate [halftone [relief [level [glow [color [color2 [crisp]]]]]]]]]]]
 *   --spacengrave <size> [fill [halftone [line [level [grain [color [dim [gate [texture [contour]]]]]]]]]]]]
 *   --glitch <amount> [rgb [noise [bands [blocks [scan [quant [vtear [seed]]]]]]]]
 *   --vapor <slow 1+> [decay 0-100] [faint 0-100] [streak 0-100] [dir 0-2]
 *     temporal mode — use last: repeats every output frame `slow` times
 *     (slow motion) and adds a decaying trail fed ONLY where the ink
 *     advances horizontally (a pixel that just lit up with ink already
 *     in its row) — vertical motion leaves no trail, and the trail
 *     never darkens the current frame.
 *     streak = 0 (default): the trail fades in place around the moving
 *       edge (haze/fog).
 *     streak > 0: the trail is additionally smeared along the X axis
 *       with a per-pixel decay of streak% (a horizontal tail — clear
 *       line, no vertical spread; 90 = long tail, 70 = short tail).
 *     dir (default 0): 0 = tail in both directions (symmetric comet,
 *       reads as smoke), 1 = tail extends only to the RIGHT of the
 *       feed point (a one-way left-to-right streak), 2 = only LEFT.
 *
 * 0-100 adapter dials convert to core values as in frei0r-adapter/
 * retrofx.c (e.g. scanlines 65 -> 0.65, shadowmask 4 -> 0.048,
 * chromablood 25 -> bleed radius 4 px, barrel 35 -> 0.41).
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt.h"
#include "dot.h"
#include "glitch.h"
#include "metal_fx.h"
#include "vhs.h"

static void die(const char *msg)
{
    fprintf(stderr, "dotpipe: %s\n", msg);
    exit(1);
}

/* --fps accepts plain numbers (24, 29.97) or fractions (30000/1001,
 * as ffprobe's r_frame_rate emits). */
static double parse_fps(const char *s)
{
    const char *slash = strchr(s, '/');
    if (slash != NULL) {
        double n = atof(s), d = atof(slash + 1);
        if (d > 0.0)
            return n / d;
    }
    return atof(s);
}

static int metal_warned = 0;   /* one-time Metal fallback warning */

/* True when s can be the start of a positional number. */
static int next_is_num(const char *s)
{
    return s != 0 && s[0] != 0 &&
           (isdigit((unsigned char)s[0]) || s[0] == '+');
}

/* Read `need` (or more, up to `max_extra`) integer values that follow the
 * current flag at argv[ip]. Values may be stopped by the next "--flag" or
 * "-w/-h" token, which we leave in place for the main loop. Returns the
 * number of values actually read. On error prints to stderr. */
static int opt_ints(int argc, char **argv, int *ip, int need, int max_extra,
                    int *vals)
{
    int i = *ip + 1, k = 0;
    while (k < need) {
        if (i >= argc) return -1;
        vals[k] = atoi(argv[i]);
        i++; k++;
    }
    while (k < need + max_extra && i < argc && next_is_num(argv[i])) {
        vals[k] = atoi(argv[i]);
        i++; k++;
    }
    *ip = i;          /* first unconsumed token (next flag, or argc) */
    return k;
}

/* Backend dispatch for the Metal-enabled effects — mirrors the archived
 * adapter (archive/frei0r/adapter/retrofx.c) exactly: RETROFX_BACKEND env,
 * cpu (default) / metal / dual. CPU is the reference; Metal is byte-identical
 * by construction; dual runs both, byte-compares, returns the CPU result.
 * Metal unavailability degrades to CPU with one warning. The casts are
 * safe: all four pairs share the (uint8_t*, int, int, int, const <p>*,
 * int) signature shape. */
typedef void (*dotpipe_cpu_fn)(uint8_t *, int, int, int, const void *, int);
typedef int  (*dotpipe_metal_fn)(uint8_t *, int, int, int, const void *, int);

static void run_paired(uint8_t *rgb, int w, int h, int stride,
                       const void *p, int frame,
                       dotpipe_cpu_fn cpu, dotpipe_metal_fn metal)
{
    static int cached = -2;   /* 0 = cpu, 1 = metal, 2 = dual */
    if (cached == -2) {
        const char *e = getenv("RETROFX_BACKEND");
        if (e != NULL && strcmp(e, "metal") == 0)
            cached = 1;
        else if (e != NULL && strcmp(e, "dual") == 0)
            cached = 2;
        else
            cached = 0;
    }
    if (cached == 0) {
        cpu(rgb, w, h, stride, p, frame);
        return;
    }
    size_t nb = (size_t)w * (size_t)h * 3;
    uint8_t *cref = (cached == 2) ? malloc(nb) : NULL;
    if (cached == 2 && cref == NULL) {
        int mrc = (metal_fx_init() == 0)
            ? metal(rgb, w, h, stride, p, frame) : -1;
        if (mrc != 0) {
            if (!metal_warned) {
                fprintf(stderr, "dotpipe: Metal unavailable/failed; using CPU core\n");
                metal_warned = 1;
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
            if (!metal_warned) {
                fprintf(stderr, "dotpipe: Metal unavailable/failed; dual degraded to CPU\n");
                metal_warned = 1;
            }
        } else {
            long long diffpix = 0;
            for (size_t k = 0; k < (size_t)nb / 3; k++) {
                if (cref[k*3] != rgb[k*3] || cref[k*3+1] != rgb[k*3+1] ||
                    cref[k*3+2] != rgb[k*3+2])
                    diffpix++;
            }
            if (diffpix) {
                fprintf(stderr, "dotpipe dual: frame %d MISMATCH: %lld px differ\n",
                        frame, diffpix);
            }
        }
        memcpy(rgb, cref, nb); /* return the CPU reference */
        free(cref);
        return;
    }
    /* METAL mode */
    if (mrc != 0) {
        if (!metal_warned) {
            fprintf(stderr, "dotpipe: Metal unavailable/failed; using CPU core\n");
            metal_warned = 1;
        }
        cpu(rgb, w, h, stride, p, frame);
    }
}

/* --vapor: per-output-frame trail compositing. Integer-only and
 * deterministic; out can never fall below px (the trail only adds) and
 * can never overflow: d = trail - px <= 255 - px, so
 * px + d*faint/100 <= px + 255 - px = 255 for any faint <= 100. */
static void vapor_composite(const uint8_t *px, const uint8_t *trail,
                            uint8_t *out, size_t n, int faint)
{
    for (size_t i = 0; i < n; i++) {
        int d = (int)trail[i] - (int)px[i];
        out[i] = (uint8_t)(px[i] + (d > 0 ? d * faint / 100 : 0));
    }
}

/* Decay the trail, feeding it ONLY where the ink advanced HORIZONTALLY:
 * a pixel that just lit up (px > prev) AND whose left or right same-row
 * neighbor was lit in the PREVIOUS frame — i.e. the edge moved along x.
 * Vertical motion (ink arriving from above/below, same-row neighbors dark
 * last frame) feeds nothing, so vertical movement leaves no trail;
 * horizontal movement leaves one. Stationary ink never feeds, so its
 * trail decays away. `prev` is updated in place to the current frame;
 * the neighbor test must therefore use last-frame values captured BEFORE
 * they are overwritten (left neighbor via carried state, right neighbor
 * still intact). */
static void vapor_update(uint8_t *trail, uint8_t *prev, const uint8_t *px,
                         int w, int h, int decay)
{
    for (int y = 0; y < h; y++) {
        size_t base = (size_t)y * (size_t)w * 3;
        int left_prev_any = 0; /* was pixel x-1 lit in the previous frame */
        for (int x = 0; x < w; x++) {
            size_t i = base + (size_t)x * 3;
            int old_any = prev[i] > 0 || prev[i + 1] > 0 || prev[i + 2] > 0;
            int right_prev_any =
                x < w - 1 && (prev[i + 3] > 0 || prev[i + 4] > 0 || prev[i + 5] > 0);
            int lateral = left_prev_any || right_prev_any;
            for (int c = 0; c < 3; c++) {
                size_t j = i + (size_t)c;
                int t = (int)trail[j] * decay / 100;
                int d = (int)px[j] - (int)prev[j];
                if (d > 0 && lateral && d > t)
                    t = d;
                prev[j] = (uint8_t)px[j];
                trail[j] = (uint8_t)t;
            }
            left_prev_any = old_any;
        }
    }
}

/* Streak pass: smear the trail along each ROW (x axis only, per color
 * channel) with a per-pixel decay of streak%. dir selects the tail
 * direction: 0 = both (two passes, max — a symmetric comet, reads as
 * smoke), 1 = right-only (single left-to-right pass — a one-way tail
 * to the right of the feed point), 2 = left-only. The buffer is RGB24
 * (3 bytes per pixel), so the row stride is w*3 bytes and each channel
 * is smeared on its own. Visible reach ~ 10/(100-streak) px (90% → ~10
 * px tail, 95% → ~20). Integer-only, deterministic, no overflow (values
 * only ever decrease). */
static void vapor_streak_pass(const uint8_t *trail, uint8_t *out, int w, int h,
                              int streak, int dir)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *row = trail + (size_t)y * (size_t)w * 3;
        uint8_t *g = out + (size_t)y * (size_t)w * 3;
        for (int c = 0; c < 3; c++) {
            if (dir == 0 || dir == 1) {
                /* left-to-right: value propagates to the RIGHT */
                for (int x = 0; x < w; x++) {
                    int v = (x > 0) ? (int)g[(x - 1) * 3 + c] * streak / 100 : 0;
                    g[x * 3 + c] =
                        (uint8_t)(v > row[x * 3 + c] ? v : row[x * 3 + c]);
                }
            }
            if (dir == 0 || dir == 2) {
                /* right-to-left: value propagates to the LEFT. For
                 * dir==0 both passes run and the max is kept, so each
                 * direction is taken independently. */
                uint8_t *t = (dir == 0) ? (g + (size_t)w * 3) : g;
                for (int x = 0; x < w; x++)
                    t[x * 3 + c] = row[x * 3 + c];
                for (int x = w - 1; x >= 0; x--) {
                    int v = (x < w - 1) ? (int)t[(x + 1) * 3 + c] * streak / 100 : 0;
                    if (v > t[x * 3 + c])
                        t[x * 3 + c] = (uint8_t)v;
                }
                if (dir == 0) {
                    for (int x = 0; x < w; x++)
                        if (t[x * 3 + c] > g[x * 3 + c])
                            g[x * 3 + c] = t[x * 3 + c];
                }
            }
        }
    }
}

int main(int argc, char **argv)
{
    int w = 0, h = 0;
    double fps = 24.0;

    enum { FX_SCANLINES, FX_MASK, FX_BLEED, FX_LUMAR, FX_BARREL, FX_BLOOM,
           FX_VIGNETTE, FX_OVERSCAN, FX_RAINBOW, FX_WOW, FX_DOTGATE,
           FX_DOTPORTAL, FX_SPACENGRAVE, FX_GLITCH, FX_MAX };
    int order[FX_MAX];   /* effects in the order given on the command line */
    int nsteps = 0;

    /* Effect parameters — defaults are the adapter defaults in core values
     * (dial meaning: README.md *Effect parameters*); each --flag below
     * overwrites the fields it names. */
    float sl_intensity = 0.5f, sl_period = 3.0f, sl_offset = 0.0f;  /* scanlines: strength, rows per period, row offset */
    crt_mask_type_t mask_type = CRT_MASK_GRILLE;                    /* mask: grille|slot|triad */
    float mask_intensity = 0.5f, mask_pitch = 3.0f;                 /* mask: strength, pitch px */
    int   bleed_radius = 16;                                        /* chromablood: fringe radius px */
    float bleed_amount = 1.0f;                                      /* chromablood: strength */
    float lumar_amount = 1.0f, lumar_wavelength = 4.0f;             /* luma ring: strength, wavelength px */
    float barrel_amount = 0.15f, barrel_zoom = 0.0f;                /* barrel: curve strength, zoom (margin fill) */
    float bloom_amount = 1.0f, bloom_threshold = 0.10f;             /* bloom: strength, luma gate 0..1 */
    int   bloom_radius = 16;                                        /* bloom: glow radius px */
    float vignette_amount = 1.0f;                                   /* vignette: corner falloff strength */
    int   overscan_radius = 48, overscan_margin = 8;                /* overscan: corner radius px, bezel crop px */
    float rainbow_amount = 1.0f, rainbow_py = 48.0f, rainbow_pt = 16.0f; /* dot crawl: strength, rows/beat, frames/beat */
    int   wow_amplitude = 4, wow_period = 90;                       /* tapewow: drift px, frames/cycle (24 Hz clock) */
    int   gate_size = 24, gate_speed = 6, gate_gate = 10;           /* dot.gate: dot pitch px, wobble px, subject knee ×100 */
    int   p_size = 24, p_fill = 55, p_gate = 5, p_halftone = 70;    /* dot.portal: pitch px, radius % of step, gate ×100, tone tracking */
    int   p_relief = 50, p_level = 200, p_glow = 60, p_color = 360; /* dot.portal: relief % of step, brightness ×100, aura, hue (360 = white) */
    int   p_color2 = 360;                                           /* dot.portal: highlight hue (360 = white); default tracks `color` = flat */
    int   p_crisp = 0;                                              /* dot.portal: silhouette sharpening % (0 = off) */
    int   s_size = 5, s_fill = 45, s_halftone = 60, s_line = 75;    /* spacengrave: pitch px, stroke % of pitch, ink shading, baseline solidity */
    int   s_level = 100, s_grain = 65, s_color = 360, s_dim = 100, s_gate = 30; /* brightness ×100, dash break-up, hue, photo dimming, gate ×100 */
    int   s_texture = 0;                                  /* spacengrave: second dash layer baseline coverage % (0 = off) */
    int   s_contour = 0;                                  /* spacengrave: stroke axis tilt toward local contours % (0 = off = vertical) */
    int   vg_amount = 30, vg_rgb = 4, vg_noise = 8, vg_bands = 45;  /* glitch: burst frequency, RGB split, static, slice tears */
    int   vg_blocks = 0, vg_scan = 0, vg_quant = 0, vg_vtear = 28, vg_seed = 3; /* glitch: tiles, scan jitter, posterize, v-sync tear, seed */
    int   vapor_slow = 0, vapor_decay = 85, vapor_faint = 20, vapor_streak = 0;  /* vapor: repeat 1+ (0 = off), trail persistence 0-100, ghost strength 0-100, x-axis streak per-px decay 0-100 (0 = haze) */
    int   vapor_dir = 0;               /* vapor: tail direction 0 = both, 1 = right-only, 2 = left-only */

    int i = 1;
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "-w") == 0) {
            if (i + 1 >= argc) die("-w needs a value");
            w = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(a, "-h") == 0) {
            if (i + 1 >= argc) die("-h needs a value");
            h = atoi(argv[i + 1]);
            i += 2;
        } else if (strcmp(a, "--fps") == 0) {
            if (i + 1 >= argc) die("--fps needs a value");
            fps = parse_fps(argv[i + 1]);
            if (fps < 1.0) die("--fps must be >= 1");
            i += 2;
        } else if (strcmp(a, "--scanlines") == 0) {
            if ((argc - i) < 4) die("--scanlines needs 3 args");
            sl_intensity = (float)atof(argv[i+1]);
            sl_period    = (float)atoi(argv[i+2]);
            sl_offset    = (float)atoi(argv[i+3]);
            order[nsteps++] = FX_SCANLINES;
            i += 4;
        } else if (strcmp(a, "--mask") == 0) {
            if ((argc - i) < 4) die("--mask needs 3 args");
            if (strcmp(argv[i+1], "grille") == 0)      mask_type = CRT_MASK_GRILLE;
            else if (strcmp(argv[i+1], "slot") == 0)   mask_type = CRT_MASK_SLOT;
            else if (strcmp(argv[i+1], "triad") == 0)  mask_type = CRT_MASK_TRIAD;
            else die("unknown mask type");
            mask_intensity = (float)atof(argv[i+2]);
            mask_pitch     = (float)atoi(argv[i+3]);
            order[nsteps++] = FX_MASK;
            i += 4;
        } else if (strcmp(a, "--bleed") == 0) {
            int v[2] = {0, 0};
            int n = opt_ints(argc, argv, &i, 1, 1, v);
            if (n < 0) die("--bleed needs 1 arg");
            bleed_radius = v[0];
            if (n > 1) bleed_amount = (float)v[1];
            order[nsteps++] = FX_BLEED;
        } else if (strcmp(a, "--lumar") == 0) {
            if ((argc - i) < 3) die("--lumar needs 2 args");
            lumar_amount = (float)atof(argv[i+1]);
            lumar_wavelength = (float)atoi(argv[i+2]);
            order[nsteps++] = FX_LUMAR;
            i += 3;
        } else if (strcmp(a, "--barrel") == 0) {
            if ((argc - i) < 2) die("--barrel needs 1 arg");
            barrel_amount = (float)atof(argv[i+1]);
            if (i + 2 < argc) barrel_zoom = (float)atof(argv[i+2]);
            order[nsteps++] = FX_BARREL;
            i += (i + 2 < argc) ? 3 : 2;
        } else if (strcmp(a, "--bloom") == 0) {
            if ((argc - i) < 2) die("--bloom needs 1 arg");
            bloom_amount = (float)atof(argv[i+1]);
            if (i + 2 < argc) {
                bloom_threshold = (float)atof(argv[i+2]);
                if (i + 3 < argc) bloom_radius = atoi(argv[i+3]);
                i += 4;
            } else {
                i += 2;
            }
            order[nsteps++] = FX_BLOOM;
        } else if (strcmp(a, "--vignette") == 0) {
            if ((argc - i) < 2) die("--vignette needs 1 arg");
            vignette_amount = (float)atof(argv[i+1]);
            order[nsteps++] = FX_VIGNETTE;
            i += 2;
        } else if (strcmp(a, "--overscan") == 0) {
            int v[2] = {0, 0};
            int n = opt_ints(argc, argv, &i, 1, 1, v);
            if (n < 0) die("--overscan needs 1 arg");
            overscan_radius = v[0];
            if (n > 1) overscan_margin = v[1];
            order[nsteps++] = FX_OVERSCAN;
        } else if (strcmp(a, "--rainbow") == 0) {
            if ((argc - i) < 4) die("--rainbow needs 3 args");
            rainbow_amount = (float)atof(argv[i+1]);
            rainbow_py     = (float)atoi(argv[i+2]);
            rainbow_pt     = (float)atoi(argv[i+3]);
            order[nsteps++] = FX_RAINBOW;
            i += 4;
        } else if (strcmp(a, "--wow") == 0) {
            int v[2] = {0, 0};
            int n = opt_ints(argc, argv, &i, 1, 1, v);
            if (n < 0) die("--wow needs 1 arg");
            wow_amplitude = v[0];
            if (n > 1) wow_period = v[1];
            order[nsteps++] = FX_WOW;
        } else if (strcmp(a, "--dotgate") == 0) {
            int v[3] = {0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 2, v);
            if (n < 0) die("--dotgate needs 1 arg");
            gate_size = v[0];
            if (n > 1) gate_speed = v[1];
            if (n > 2) gate_gate = v[2];
            order[nsteps++] = FX_DOTGATE;
        } else if (strcmp(a, "--dotportal") == 0) {
            int v[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 9, v);
            if (n < 0) die("--dotportal needs 1 arg");
            p_size     = v[0];
            if (n > 1) p_fill     = v[1];
            if (n > 2) p_gate     = v[2];
            if (n > 3) p_halftone = v[3];
            if (n > 4) p_relief   = v[4];
            if (n > 5) p_level    = v[5];
            if (n > 6) p_glow     = v[6];
            if (n > 7) p_color    = v[7];
            if (n > 8) p_color2   = v[8];
            if (n > 9) p_crisp    = v[9];
            else       p_color2  = p_color; /* ungiven: same hue = flat, byte-identical */
            order[nsteps++] = FX_DOTPORTAL;
        } else if (strcmp(a, "--spacengrave") == 0) {
            int v[11] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 10, v);
            if (n < 0) die("--spacengrave needs 1 arg");
            s_size     = v[0];
            if (n > 1) s_fill     = v[1];
            if (n > 2) s_halftone = v[2];
            if (n > 3) s_line     = v[3];
            if (n > 4) s_level    = v[4];
            if (n > 5) s_grain    = v[5];
            if (n > 6) s_color    = v[6];
            if (n > 7) s_dim      = v[7];
            if (n > 8) s_gate     = v[8];
            if (n > 9) s_texture  = v[9];
            if (n > 10) s_contour = v[10];
            order[nsteps++] = FX_SPACENGRAVE;
        } else if (strcmp(a, "--glitch") == 0) {
            int v[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 8, v);
            if (n < 0) die("--glitch needs 1 arg");
            vg_amount = v[0];
            if (n > 1) vg_rgb    = v[1];
            if (n > 2) vg_noise  = v[2];
            if (n > 3) vg_bands  = v[3];
            if (n > 4) vg_blocks = v[4];
            if (n > 5) vg_scan   = v[5];
            if (n > 6) vg_quant  = v[6];
            if (n > 7) vg_vtear  = v[7];
            if (n > 8) vg_seed   = v[8];
            order[nsteps++] = FX_GLITCH;
        } else if (strcmp(a, "--vapor") == 0) {
            int v[5] = {0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 4, v);
            if (n < 0) die("--vapor needs 1 arg");
            vapor_slow = v[0];
            if (vapor_slow < 1) die("--vapor slow must be >= 1");
            if (n > 1) vapor_decay = v[1];
            if (n > 2) vapor_faint = v[2];
            if (n > 3) vapor_streak = v[3];
            if (n > 4) vapor_dir = v[4];
            if (vapor_dir < 0 || vapor_dir > 2) die("--vapor dir must be 0, 1 or 2");
        } else {
            die("unknown option");
        }
    }

    if (w <= 0 || h <= 0) {
        fprintf(stderr,
            "usage: %s -w <width> -h <height> [--fps 24] [options]\n"
            "  effects (applied in the order given): --scanlines --mask\n"
            "  --bleed --lumar --barrel --bloom --vignette --overscan\n"
            "  --rainbow --wow --dotgate --dotportal --spacengrave --glitch\n"
            "  --vapor (slow motion + fading trail; use last)\n"
            "  reads raw RGB24 frames from stdin, writes to stdout",
            argv[0]);
        return 1;
    }

    /* One frame of RGB24 — w*h*3 bytes in, w*h*3 bytes out, per iteration. */
    size_t nbytes = (size_t)w * (size_t)h * 3;
    uint8_t *px = malloc(nbytes);
    if (!px)
        die("out of memory");

    /* --vapor state: persistent trail buffer (zero = no trail yet), the
     * previous output frame (for the new-edge feed), and a compositing
     * scratch. Allocated only when the mode is on, so without the flag
     * the frame loop is untouched (byte-identical passthrough). */
    uint8_t *vapor_trail = NULL, *vapor_prev = NULL, *vapor_out = NULL;
    uint8_t *vapor_smear = NULL;   /* streak scratch, only when streak > 0 */
    if (vapor_slow > 0) {
        vapor_trail = calloc(1, nbytes);
        vapor_prev  = calloc(1, nbytes);
        vapor_out   = malloc(nbytes);
        if (vapor_streak > 0)
            vapor_smear = malloc(nbytes);
        if (vapor_trail == NULL || vapor_prev == NULL || vapor_out == NULL ||
            (vapor_streak > 0 && vapor_smear == NULL))
            die("out of memory");
    }

    /* Raw pipes from ffmpeg are throughput-sensitive: 1 MiB stdio buffers
     * keep the process from stalling the pipe on every syscall. */
    static char ibuf[1 << 20], obuf[1 << 20];
    setvbuf(stdin, ibuf, _IOFBF, sizeof ibuf);
    setvbuf(stdout, obuf, _IOFBF, sizeof obuf);

    /* Frame loop: read one raw frame, run it through the effect chain in
     * CLI order (switch below), write it out. Frame index drives the time
     * effects; a short read at EOF ends the stream. */
    for (int frame = 0; ; frame++) {
        if (fread(px, 1, nbytes, stdin) != nbytes) {
            if (feof(stdin))
                break;               /* clean EOF */
            die("short frame on stdin");
        }

        for (int s = 0; s < nsteps; s++) {
            int fx = order[s];
            /* Adapter clock classes: the 24 fps group ticks at 24 Hz,
             * legacy effects on whole seconds (frei0r-adapter/retrofx.c).
             * Double truncation mirrors the adapter's (int)time and
             * (int)(time*24) exactly for any fps. */
            double tsec = (double)frame / fps;
            int tf = (fx == FX_WOW || fx == FX_DOTGATE || fx == FX_GLITCH)
                     ? (int)(tsec * 24.0)
                     : (int)tsec;

            switch (fx) {
            case FX_SCANLINES: {
                crt_scanlines_params_t p = {
                    .intensity = sl_intensity,
                    .period    = (int)sl_period,
                    .offset    = (int)sl_offset,
                };
                crt_scanlines(px, w, h, w * 3, &p, tf);
            } break;
            case FX_MASK: {
                crt_shadow_mask_params_t p = {
                    .type      = mask_type,
                    .intensity = mask_intensity,
                    .pitch     = (int)mask_pitch,
                };
                crt_shadow_mask(px, w, h, w * 3, &p, tf);
            } break;
            case FX_BLEED: {
                vhs_chroma_bleed_params_t p = {
                    .radius = bleed_radius,
                    .amount = bleed_amount,
                };
                vhs_chroma_bleed(px, w, h, w * 3, &p, tf);
            } break;
            case FX_LUMAR: {
                vhs_luma_ring_params_t p = {
                    .amount     = lumar_amount,
                    .wavelength = (int)lumar_wavelength,
                };
                vhs_luma_ring(px, w, h, w * 3, &p, tf);
            } break;
            case FX_BARREL: {
                crt_barrel_params_t p = {
                    .amount = barrel_amount,
                    .zoom   = barrel_zoom,
                };
                crt_barrel(px, w, h, w * 3, &p, tf);
            } break;
            case FX_BLOOM: {
                crt_bloom_params_t p = {
                    .amount    = bloom_amount,
                    .threshold = bloom_threshold,
                    .radius    = bloom_radius,
                };
                crt_bloom(px, w, h, w * 3, &p, tf);
            } break;
            case FX_VIGNETTE: {
                crt_vignette_params_t p = {
                    .amount = vignette_amount,
                };
                crt_vignette(px, w, h, w * 3, &p, tf);
            } break;
            case FX_OVERSCAN: {
                crt_overscan_params_t p = {
                    .radius = overscan_radius,
                    .margin = overscan_margin,
                };
                crt_overscan(px, w, h, w * 3, &p, tf);
            } break;
            case FX_RAINBOW: {
                vhs_rainbow_params_t p = {
                    .amount   = rainbow_amount,
                    .period_y = (int)rainbow_py,
                    .period_t = (int)rainbow_pt,
                };
                vhs_rainbow_phase(px, w, h, w * 3, &p, tf);
            } break;
            case FX_WOW: {
                vhs_tape_wow_params_t p = {
                    .amplitude = wow_amplitude,
                    .period    = wow_period,
                };
                vhs_tape_wow(px, w, h, w * 3, &p, tf);
            } break;
            case FX_DOTGATE: {
                dotgate_params_t p = {
                    .size  = gate_size,
                    .speed = gate_speed,
                    .gate  = gate_gate,
                };
                run_paired(px, w, h, w * 3, &p, tf,
                           (dotpipe_cpu_fn)dotgate, (dotpipe_metal_fn)metal_dotgate);
            } break;
            case FX_DOTPORTAL: {
                dotportal_params_t p = {
                    .size     = p_size,
                    .fill     = p_fill,
                    .gate     = p_gate,
                    .halftone = p_halftone,
                    .relief   = p_relief,
                    .level    = p_level,
                    .color    = p_color,
                    .color2   = p_color2,
                    .crisp    = p_crisp,
                    .glow     = p_glow,
                };
                run_paired(px, w, h, w * 3, &p, tf,
                           (dotpipe_cpu_fn)dotportal, (dotpipe_metal_fn)metal_dotportal);
            } break;
            case FX_SPACENGRAVE: {
                dot_spacengrave_params_t p = {
                    .size     = s_size,
                    .fill     = s_fill,
                    .halftone = s_halftone,
                    .line     = s_line,
                    .level    = s_level,
                    .grain    = s_grain,
                    .color    = s_color,
                    .dim      = s_dim,
                    .gate     = s_gate,
                    .texture  = s_texture,
                    .contour  = s_contour,
                };
                run_paired(px, w, h, w * 3, &p, tf,
                           (dotpipe_cpu_fn)dot_spacengrave,
                           (dotpipe_metal_fn)metal_spacengrave);
            } break;
            case FX_GLITCH: {
                vid_glitch_params_t p = {
                    .amount = vg_amount,
                    .rgb    = vg_rgb,
                    .noise  = vg_noise,
                    .bands  = vg_bands,
                    .blocks = vg_blocks,
                    .scan   = vg_scan,
                    .quant  = vg_quant,
                    .vtear  = vg_vtear,
                    .seed   = vg_seed,
                };
                run_paired(px, w, h, w * 3, &p, tf,
                           (dotpipe_cpu_fn)vid_glitch, (dotpipe_metal_fn)metal_vid_glitch);
            } break;
            default:
                die("internal: bad effect id");
            }
        }

        if (vapor_slow > 0) {
            /* --vapor (use last): write the frame `slow` times (slow
             * motion), each one composited with the decaying trail;
             * the trail ticks per OUTPUT frame, so the ghost fades
             * smoothly across the duplicated frames. With streak > 0
             * the trail is first smeared along the x axis (comet) —
             * re-smeared each copy so it fades too. */
            for (int k = 0; k < vapor_slow; k++) {
                const uint8_t *ghost = vapor_trail;
                if (vapor_streak > 0) {
                    vapor_streak_pass(vapor_trail, vapor_smear, w, h,
                                     vapor_streak, vapor_dir);
                    ghost = vapor_smear;
                }
                vapor_composite(px, ghost, vapor_out, nbytes,
                                vapor_faint);
                if (fwrite(vapor_out, 1, nbytes, stdout) != nbytes)
                    die("short write on stdout");
                vapor_update(vapor_trail, vapor_prev, px, w, h, vapor_decay);
            }
        } else {
            if (fwrite(px, 1, nbytes, stdout) != nbytes)
                die("short write on stdout");
        }
    }

    fflush(stdout);
    metal_fx_shutdown();
    free(px);
    free(vapor_trail);
    free(vapor_prev);
    free(vapor_out);
    free(vapor_smear);
    return 0;
}
