/* sprcmp.c — byte-compare the CPU vs GPU gather (sprite dump). Debug.
 *
 *   tools/sprcmp build/libretrofx_dotgate.dylib [w h frame]
 *
 * Builds the same f0r_host gradient, runs dotgate_sprdump (CPU) and
 * metal_dotgate_sprdump (GPU) with the plugin defaults, and reports which
 * sprite cells/fields differ (hex bits). Zero exit = byte-identical.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct gate_p { int size, speed; };

#define NF 10 /* floats per sprite cell */

static void *req(void *lib, const char *name)
{
    void *s = dlsym(lib, name);
    if (!s) { fprintf(stderr, "missing symbol %s\n", name); exit(1); }
    return s;
}

static unsigned bits(float f)
{
    unsigned u;
    memcpy(&u, &f, 4);
    return u;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <dotgate>.dylib [w h frame]\n", argv[0]);
        return 2;
    }
    int w = argc > 2 ? atoi(argv[2]) : 1280;
    int h = argc > 3 ? atoi(argv[3]) : 720;
    int frame = argc > 4 ? atoi(argv[4]) : 0;

    void *lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    struct gate_p gp; memset(&gp, 0, sizeof gp);
    /* plugin defaults (f0r_construct) */
    gp.size = 24; gp.speed = 6;

    int step = gp.size / 2;
    if (step < 2) step = 2;
    int cols = (w + step - 1) / step;
    int rows = (h + step - 1) / step;
    size_t nspr = (size_t)cols * rows;

    size_t npix = (size_t)w * (size_t)h;
    uint8_t *img = malloc(npix * 3);
    float *a = malloc(nspr * NF * 4), *b = malloc(nspr * NF * 4);
    if (!img || !a || !b) { fprintf(stderr, "alloc\n"); return 1; }
    float (*luma_at_dbg)(const uint8_t *, int, int, int, int) = NULL;
    {
        void *s = dlsym(lib, "particle_luma_at_dbg");
        if (s) luma_at_dbg = (float (*)(const uint8_t *, int, int, int, int))s;
    }

    /* f0r_host gradient (the dual failing input) */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t r = (uint32_t)(x * 255 / (w - 1));
            uint32_t g = (uint32_t)(y * 255 / (h - 1));
            uint32_t bl = (uint32_t)((x + y) * 128 / (w + h - 2));
            if (x >= w / 2 - 1 && x <= w / 2 + 1)
                r = g = bl = 0;
            img[(size_t)(y * w + x) * 3 + 0] = (uint8_t)r;
            img[(size_t)(y * w + x) * 3 + 1] = (uint8_t)g;
            img[(size_t)(y * w + x) * 3 + 2] = (uint8_t)bl;
        }

    int rc;
    {
        void (*cpu)(const uint8_t *, int, int, int, const void *, int, float *)
            = req(lib, "dotgate_sprdump");
        int (*gpu)(const uint8_t *, int, int, int, const void *, int, float *)
            = req(lib, "metal_dotgate_sprdump");
        int (*init)(void) = req(lib, "metal_fx_init");
        if (init() != 0) { fprintf(stderr, "metal_fx_init failed\n"); return 1; }
        cpu(img, w, h, w * 3, &gp, frame, a);
        rc = gpu(img, w, h, w * 3, &gp, frame, b);
    }
    if (rc != 0) { fprintf(stderr, "gpu sprdump failed\n"); return 1; }

    if (getenv("SPRCMP_CPUHEX")) {
        for (size_t tid = 0; tid < nspr; tid++) {
            if (a[tid * NF + NF - 1] > 0.5f) {
                printf("cpu tid=%zu ", tid);
                for (int i = 0; i < NF; i++)
                    printf("%08X ", bits(a[tid * NF + i]));
                printf("\n");
            }
        }
        return 0;
    }

    if (getenv("SPRCMP_LUMA")) {
        if (!luma_at_dbg) {
            fprintf(stderr, "dylib missing particle_luma_at_dbg — rebuild\n");
            return 2;
        }
        for (size_t tid = 0; tid < nspr; tid++) {
            if (a[tid * NF + NF - 1] > 0.5f) {
                int sy = (step / 2) + (int)(tid % (size_t)rows) * step;
                int sx = (step / 2) + (int)(tid / (size_t)rows) * step;
                printf("tid=%zu (%d,%d) l0=%08X le=%08X lw=%08X ls=%08X ln=%08X\n",
                       tid, sx, sy,
                       bits(luma_at_dbg(img, w, w * 3, sx, sy)),
                       bits(luma_at_dbg(img, w, w * 3, sx + step, sy)),
                       bits(luma_at_dbg(img, w, w * 3, sx - step, sy)),
                       bits(luma_at_dbg(img, w, w * 3, sx, sy + step)),
                       bits(luma_at_dbg(img, w, w * 3, sx, sy - step)));
            }
        }
        return 0;
    }

    int kept = 0, dcells = 0, shown = 0;
    for (size_t tid = 0; tid < nspr; tid++) {
        const float *ca = a + tid * NF;
        const float *cb = b + tid * NF;
        int any = 0;
        for (int i = 0; i < NF; i++)
            if (bits(ca[i]) != bits(cb[i])) { any = 1; break; }
        if (ca[NF - 1] > 0.5f) kept++;
        if (!any)
            continue;
        dcells++;
        if (shown < 5) {
            int sy = (step / 2) + (int)(tid % (size_t)rows) * step;
            int sx = (step / 2) + (int)(tid / (size_t)rows) * step;
            printf("cell tid=%zu (sx=%d sy=%d):\n", tid, sx, sy);
            static const char *fn[NF] = {
                "x", "y", "r", "cr", "cg", "cb", "env", "shx", "shy", "valid"
            };
            for (int i = 0; i < NF; i++) {
                if (bits(ca[i]) != bits(cb[i]))
                    printf("  %-6s CPU=%08X GPU=%08X\n",
                           fn[i], bits(ca[i]), bits(cb[i]));
            }
            shown++;
        }
    }
    printf("%s: %zu cells, %d kept, %d differing cells\n",
           "dotgate", nspr, kept, dcells);
    return dcells == 0 ? 0 : 1;
}
