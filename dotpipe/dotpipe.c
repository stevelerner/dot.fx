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
 *   --dotportal <size> [fill [gate [halftone [relief [level [glow [color]]]]]]]
 *   --spacengrave <size> [fill [halftone [line [level [grain [color [dim [gate]]]]]]]]
 *   --glitch <amount> [rgb [noise [bands [blocks [scan [quant [vtear [seed]]]]]]]]
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
    int   s_size = 5, s_fill = 45, s_halftone = 60, s_line = 75;    /* spacengrave: pitch px, stroke % of pitch, ink shading, baseline solidity */
    int   s_level = 100, s_grain = 65, s_color = 360, s_dim = 100, s_gate = 30; /* brightness ×100, dash break-up, hue, photo dimming, gate ×100 */
    int   vg_amount = 30, vg_rgb = 4, vg_noise = 8, vg_bands = 45;  /* glitch: burst frequency, RGB split, static, slice tears */
    int   vg_blocks = 0, vg_scan = 0, vg_quant = 0, vg_vtear = 28, vg_seed = 3; /* glitch: tiles, scan jitter, posterize, v-sync tear, seed */

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
            int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 7, v);
            if (n < 0) die("--dotportal needs 1 arg");
            p_size     = v[0];
            if (n > 1) p_fill     = v[1];
            if (n > 2) p_gate     = v[2];
            if (n > 3) p_halftone = v[3];
            if (n > 4) p_relief   = v[4];
            if (n > 5) p_level    = v[5];
            if (n > 6) p_glow     = v[6];
            if (n > 7) p_color    = v[7];
            order[nsteps++] = FX_DOTPORTAL;
        } else if (strcmp(a, "--spacengrave") == 0) {
            int v[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
            int n = opt_ints(argc, argv, &i, 1, 8, v);
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
            "  reads raw RGB24 frames from stdin, writes to stdout",
            argv[0]);
        return 1;
    }

    /* One frame of RGB24 — w*h*3 bytes in, w*h*3 bytes out, per iteration. */
    size_t nbytes = (size_t)w * (size_t)h * 3;
    uint8_t *px = malloc(nbytes);
    if (!px)
        die("out of memory");

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

        if (fwrite(px, 1, nbytes, stdout) != nbytes)
            die("short write on stdout");
    }

    fflush(stdout);
    metal_fx_shutdown();
    free(px);
    return 0;
}
