/* checkmask — measure the shadow-mask distribution on a flat white field.
 *
 * For a stripe-based mask (grille/slot) the pattern has period 3*pitch
 * columns. For the triad the tile is 3*pitch wide. In every case we slice
 * the image into N equal column bands (N = width / (3*pitch), clamped to
 * 1..8) and report the pure-white fraction in each band. If the toroidal
 * wrap in the triad is correct, the distribution across bands should be
 * roughly uniform — i.e. the spread (max-min) should be small. A missing
 * wrap concentrates unmodulated pixels along tile boundaries and shows up
 * as a large spread.
 *
 * Usage: checkmask <file.ppm> <pitch>
 * Exit 0 if spread <= 2 points, else 1.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void die(const char *msg)
{
    fprintf(stderr, "checkmask: %s\n", msg);
    exit(1);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <file.ppm> <pitch>\n", argv[0]);
        return 1;
    }
    int pitch = atoi(argv[2]);
    if (pitch <= 0) die("pitch must be > 0");

    FILE *f = fopen(argv[1], "rb");
    if (!f) die("cannot open input");

    char magic[3];
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '6')
        die("not P6");
    int c;
    do { c = fgetc(f); } while (c != EOF && c != '\n');
    int w = 0, h = 0;
    if (fscanf(f, "%d %d", &w, &h) != 2) die("bad dims");
    do { c = fgetc(f); } while (c != EOF && c != '\n');
    int maxval = 0;
    if (fscanf(f, "%d", &maxval) != 1) die("bad maxval");
    do { c = fgetc(f); } while (c != EOF && c != '\n');

    size_t nbytes = (size_t)w * (size_t)h * 3;
    uint8_t *px = malloc(nbytes);
    if (!px) die("oom");
    if (fread(px, 1, nbytes, f) != nbytes) die("short read");
    fclose(f);

    int tile_w = 3 * pitch;
    if (tile_w < 1) tile_w = 1;
    int n = w / tile_w;
    if (n < 1) n = 1;
    if (n > 8) n = 8;

    long totals[8] = {0};
    long whites[8] = {0};

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = px + ((size_t)y * w + x) * 3;
            int band = (x * n) / w;
            if (band > 7) band = 7;
            totals[band]++;
            if (p[0] > 250 && p[1] > 250 && p[2] > 250)
                whites[band]++;
        }
    }

    double mx = -1.0, mn = 1e30;
    for (int b = 0; b < n; b++) {
        double pct = 100.0 * (double)whites[b] / (double)totals[b];
        printf("band %d: %ld/%ld = %.3f%% pure white\n",
               b, whites[b], totals[b], pct);
        if (pct > mx) mx = pct;
        if (pct < mn) mn = pct;
    }
    double spread = mx - mn;
    printf("bands: %d  spread: %.3f points %s\n", n, spread,
           (spread <= 2.0) ? "(PASS <= 2)" : "(FAIL > 2)");
    free(px);
    return (spread <= 2.0) ? 0 : 1;
}
