/* harness — standalone CLI to drive the core directly.
 *
 * Usage:
 *   harness <in.ppm> <out.ppm> [options]
 *
 * Options:
 *   --scanlines <intensity> <period> <offset>
 *   --mask grille|slot|triad <intensity> <pitch>
 *   --barrel <amount>
 *   --bloom <amount> [threshold] [radius]
 *   --vignette <amount>
 *   --overscan <radius> [margin]
 *   --bleed <radius> [amount]
 *   --lumar <amount> <wavelength>
 *   --rainbow <amount> <period_y> <period_t>
 *   --wow <amplitude> [period]
 *   --dotgate <size> [speed]
 *   --glitch <amount> [rgb [noise [bands [blocks [scan [quant [vtear [seed]]]]]]]]
 *   --frame <n>
 *
 * Reads a binary PPM (P6) image, applies the selected effects in order,
 * writes a binary PPM (P6) image.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt.h"
#include "dot.h"
#include "glitch.h"
#include "vhs.h"

static void die(const char *msg)
{
    fprintf(stderr, "harness: %s\n", msg);
    exit(1);
}

typedef struct {
    int   w, h;
    int   stride;
    uint8_t *px;
} image_t;

static image_t read_ppm(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        die("cannot open input file");

    /* Parse P6 header: "P6\n<w> <h>\n255\n".
     * Tolerates whitespace after each token. */
    char magic[3];
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '6') {
        fclose(f);
        die("not a P6 PPM file");
    }
    /* Consume the newline after "P6". */
    int c;
    do {
        c = fgetc(f);
    } while (c != EOF && isspace(c) && c != '\n');
    /* c is now '\n' or EOF; either way we proceed. */

    int w = 0, h = 0;
    if (fscanf(f, "%d %d", &w, &h) != 2) {
        fclose(f);
        die("bad dimensions");
    }
    /* Consume whitespace up to and including the maxval line. */
    int maxval = 0;
    do {
        c = fgetc(f);
    } while (c != EOF && c != '\n');
    if (fscanf(f, "%d", &maxval) != 1 || maxval != 255) {
        fclose(f);
        die("expected maxval 255");
    }
    /* Consume the final newline. */
    do {
        c = fgetc(f);
    } while (c != EOF && c != '\n');

    if (w <= 0 || h <= 0) {
        fclose(f);
        die("invalid dimensions");
    }

    int stride = w * 3;
    size_t nbytes = (size_t)h * (size_t)stride;
    uint8_t *px = malloc(nbytes);
    if (!px) {
        fclose(f);
        die("out of memory");
    }
    if (fread(px, 1, nbytes, f) != nbytes) {
        free(px);
        fclose(f);
        die("short read");
    }
    fclose(f);

    image_t img = { w, h, stride, px };
    return img;
}

static void write_ppm(const char *path, const image_t *img)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        die("cannot open output file");
    fprintf(f, "P6\n%d %d\n255\n", img->w, img->h);
    for (int y = 0; y < img->h; y++)
        fwrite(img->px + (size_t)y * (size_t)img->stride, 1,
               (size_t)img->w * 3, f);
    if (fclose(f) != 0)
        die("write failed");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <in.ppm> <out.ppm> [options]\n"
            "  --scanlines <intensity> <period> <offset>\n"
            "  --mask grille|slot|triad <intensity> <pitch>\n"
            "  --barrel <amount>\n"
            "  --bloom <amount> [threshold] [radius]\n"
            "  --vignette <amount>\n"
            "  --overscan <radius> [margin]\n"
            "  --bleed <radius> [amount]\n"
            "  --lumar <amount> <wavelength>\n"
            "  --rainbow <amount> <period_y> <period_t>\n"
            "  --wow <amplitude> [period]\n"
            "  --dotgate <size> [speed]\n"
            "  --glitch <amount> [rgb [noise [bands [blocks [scan [quant [vtear [seed]]]]]]]]\n"
            "  --frame <n>",
            argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    const char *out_path = argv[2];

    float sl_intensity = 0.5f, sl_period = 3.0f, sl_offset = 0.0f;
    int   have_scanlines = 0;
    crt_mask_type_t mask_type = CRT_MASK_GRILLE;
    float mask_intensity = 0.5f, mask_pitch = 3.0f;
    int   have_mask = 0;
    int   bleed_radius = 16;
    float bleed_amount = 1.0f;
    int   have_bleed = 0;
    float lumar_amount = 1.0f, lumar_wavelength = 4.0f;
    int   have_lumar = 0;
    float barrel_amount = 0.15f;
    int   have_barrel = 0;
    float bloom_amount = 1.0f, bloom_threshold = 0.10f;
    int   bloom_radius = 16;
    int   have_bloom = 0;
    float vignette_amount = 1.0f;
    int   have_vignette = 0;
    int   overscan_radius = 48;
    int   overscan_margin = 8;
    int   have_overscan = 0;
    float rainbow_amount = 1.0f, rainbow_py = 48.0f, rainbow_pt = 16.0f;
    int   have_rainbow = 0;
    int   wow_amplitude = 4;
    int   wow_period = 90;
    int   have_wow = 0;
    int   gate_size = 24, gate_speed = 6;
    int   have_gate = 0;
    int   vg_amount = 30, vg_rgb = 4, vg_noise = 8, vg_bands = 45;
    int   vg_blocks = 0, vg_scan = 0, vg_quant = 0, vg_vtear = 28, vg_seed = 3;
    int   have_vglitch = 0;
    int   frame = 0;

    int i = 3;
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "--scanlines") == 0) {
            if (i + 3 >= argc + 1 && (argc - i) < 4) die("--scanlines needs 3 args");
            sl_intensity = (float)atof(argv[i+1]);
            sl_period    = (float)atoi(argv[i+2]);
            sl_offset    = (float)atoi(argv[i+3]);
            have_scanlines = 1;
            i += 4;
        } else if (strcmp(a, "--mask") == 0) {
            if ((argc - i) < 4) die("--mask needs 3 args");
            if (strcmp(argv[i+1], "grille") == 0)      mask_type = CRT_MASK_GRILLE;
            else if (strcmp(argv[i+1], "slot") == 0)   mask_type = CRT_MASK_SLOT;
            else if (strcmp(argv[i+1], "triad") == 0)  mask_type = CRT_MASK_TRIAD;
            else die("unknown mask type");
            mask_intensity = (float)atof(argv[i+2]);
            mask_pitch     = (float)atoi(argv[i+3]);
            have_mask = 1;
            i += 4;
        } else if (strcmp(a, "--bleed") == 0) {
            if ((argc - i) < 2) die("--bleed needs 1 arg");
            bleed_radius = atoi(argv[i+1]);
            if (i + 2 < argc)
                bleed_amount = (float)atof(argv[i+2]);
            have_bleed = 1;
            i += (i + 2 < argc) ? 3 : 2;
        } else if (strcmp(a, "--lumar") == 0) {
            if ((argc - i) < 3) die("--lumar needs 2 args");
            lumar_amount = (float)atof(argv[i+1]);
            lumar_wavelength = (float)atoi(argv[i+2]);
            have_lumar = 1;
            i += 3;
        } else if (strcmp(a, "--barrel") == 0) {
            if ((argc - i) < 2) die("--barrel needs 1 arg");
            barrel_amount = (float)atof(argv[i+1]);
            have_barrel = 1;
            i += 2;
        } else if (strcmp(a, "--bloom") == 0) {
            if ((argc - i) < 2) die("--bloom needs 1 arg");
            bloom_amount = (float)atof(argv[i+1]);
            int n = 1;
            if (i + 2 < argc) { bloom_threshold = (float)atof(argv[i+2]); n = 2; }
            if (i + 3 < argc) { bloom_radius = atoi(argv[i+3]); n = 3; }
            have_bloom = 1;
            i += 1 + n;
        } else if (strcmp(a, "--vignette") == 0) {
            if ((argc - i) < 2) die("--vignette needs 1 arg");
            vignette_amount = (float)atof(argv[i+1]);
            have_vignette = 1;
            i += 2;
        } else if (strcmp(a, "--overscan") == 0) {
            if ((argc - i) < 2) die("--overscan needs 1 arg");
            overscan_radius = atoi(argv[i+1]);
            if (i + 2 < argc)
                overscan_margin = atoi(argv[i+2]);
            have_overscan = 1;
            i += (i + 2 < argc) ? 3 : 2;
        } else if (strcmp(a, "--rainbow") == 0) {
            if ((argc - i) < 4) die("--rainbow needs 3 args");
            rainbow_amount = (float)atof(argv[i+1]);
            rainbow_py     = (float)atoi(argv[i+2]);
            rainbow_pt     = (float)atoi(argv[i+3]);
            have_rainbow = 1;
            i += 4;
        } else if (strcmp(a, "--wow") == 0) {
            if ((argc - i) < 2) die("--wow needs 1 arg");
            wow_amplitude = atoi(argv[i+1]);
            if (i + 2 < argc)
                wow_period = atoi(argv[i+2]);
            have_wow = 1;
            i += (i + 2 < argc) ? 3 : 2;
        } else if (strcmp(a, "--dotgate") == 0) {
            if ((argc - i) < 2) die("--dotgate needs 1 arg");
            gate_size = atoi(argv[i+1]);
            if (i + 2 < argc)
                gate_speed = atoi(argv[i+2]);
            have_gate = 1;
            i += (i + 2 < argc) ? 3 : 2;
        } else if (strcmp(a, "--glitch") == 0) {
            if ((argc - i) < 2) die("--glitch needs 1 arg");
            vg_amount = atoi(argv[i+1]);
            int n = 1;
            if (i + 2 < argc) { vg_rgb = atoi(argv[i+2]); n = 2; }
            if (i + 3 < argc) { vg_noise = atoi(argv[i+3]); n = 3; }
            if (i + 4 < argc) { vg_bands = atoi(argv[i+4]); n = 4; }
            if (i + 5 < argc) { vg_blocks = atoi(argv[i+5]); n = 5; }
            if (i + 6 < argc) { vg_scan = atoi(argv[i+6]); n = 6; }
            if (i + 7 < argc) { vg_quant = atoi(argv[i+7]); n = 7; }
            if (i + 8 < argc) { vg_vtear = atoi(argv[i+8]); n = 8; }
            if (i + 9 < argc) { vg_seed = atoi(argv[i+9]); n = 9; }
            have_vglitch = 1;
            i += 1 + n;
        } else if (strcmp(a, "--frame") == 0) {
            if ((argc - i) < 2) die("--frame needs 1 arg");
            frame = atoi(argv[i+1]);
            i += 2;
        } else {
            die("unknown option");
        }
    }

    image_t img = read_ppm(in_path);

    if (have_scanlines) {
        crt_scanlines_params_t p = {
            .intensity = sl_intensity,
            .period    = (int)sl_period,
            .offset    = (int)sl_offset,
        };
        crt_scanlines(img.px, img.w, img.h, img.stride, &p, frame);
    }
    if (have_mask) {
        crt_shadow_mask_params_t p = {
            .type      = mask_type,
            .intensity = mask_intensity,
            .pitch     = (int)mask_pitch,
        };
        crt_shadow_mask(img.px, img.w, img.h, img.stride, &p, frame);
    }
    if (have_bleed) {
        vhs_chroma_bleed_params_t p = {
            .radius = bleed_radius,
            .amount = bleed_amount,
        };
        vhs_chroma_bleed(img.px, img.w, img.h, img.stride, &p, frame);
    }
    if (have_lumar) {
        vhs_luma_ring_params_t p = {
            .amount     = lumar_amount,
            .wavelength = (int)lumar_wavelength,
        };
        vhs_luma_ring(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_barrel) {
        crt_barrel_params_t p = {
            .amount = barrel_amount,
        };
        crt_barrel(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_bloom) {
        crt_bloom_params_t p = {
            .amount    = bloom_amount,
            .threshold = bloom_threshold,
            .radius    = bloom_radius,
        };
        crt_bloom(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_vignette) {
        crt_vignette_params_t p = {
            .amount = vignette_amount,
        };
        crt_vignette(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_overscan) {
        crt_overscan_params_t p = {
            .radius = overscan_radius,
            .margin = overscan_margin,
        };
        crt_overscan(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_rainbow) {
        vhs_rainbow_params_t p = {
            .amount   = rainbow_amount,
            .period_y = (int)rainbow_py,
            .period_t = (int)rainbow_pt,
        };
        vhs_rainbow_phase(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_wow) {
        vhs_tape_wow_params_t p = {
            .amplitude = wow_amplitude,
            .period    = wow_period,
        };
        vhs_tape_wow(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_gate) {
        dotgate_params_t p = {
            .size  = gate_size,
            .speed = gate_speed,
        };
        dotgate(img.px, img.w, img.h, img.stride, &p, frame);
    }

    if (have_vglitch) {
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
        vid_glitch(img.px, img.w, img.h, img.stride, &p, frame);
    }

    write_ppm(out_path, &img);
    free(img.px);
    return 0;
}
