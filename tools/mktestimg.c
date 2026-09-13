/* mktestimg — generate binary PPM (P6) test images into inputvideos/.
 *
 *   bars.ppm        SMPTE-style colour bars, 640x480
 *   flat_white.ppm  solid 255 white, 256x256
 *   gradient.ppm    horizontal RGB ramp, 640x480
 *
 * Usage: mktestimg <outdir>
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static void die(const char *msg)
{
    fprintf(stderr, "mktestimg: %s\n", msg);
    exit(1);
}

static FILE *open_out(const char *dir, const char *name, int w, int h,
                      char *header, size_t header_cap)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);

    FILE *f = fopen(path, "wb");
    if (!f)
        die("cannot open output file for writing");

    int n = snprintf(header, header_cap, "P6\n%d %d\n255\n", w, h);
    if (n < 0 || (size_t)n >= header_cap) {
        fclose(f);
        die("header too long");
    }
    fwrite(header, 1, (size_t)n, f);
    return f;
}

static void close_out(FILE *f)
{
    if (fclose(f) != 0)
        die("write failed");
}

/* One full 8-entry colour table; rows 0..5 use the top 360 px,
 * row 6 is a 60 px band, row 7 is the bottom 60 px. */
static void write_bars(const char *dir)
{
    enum { W = 640, H = 480, ROWS = 8 };
    static const uint8_t cols[8][3] = {
        { 255, 255, 255 }, { 255, 255,  0 }, {   0, 255, 255 },
        {   0, 255,   0 }, { 255,   0, 255 }, { 255,   0,   0 },
        {   0,   0, 255 }, {  50,  50,  50 },
    };

    char header[64];
    FILE *f = open_out(dir, "bars.ppm", W, H, header, sizeof header);

    for (int y = 0; y < H; y++) {
        int row = y < H - 120 ? (y * ROWS) / (H - 120)
                              : (y < H - 60 ? ROWS - 2 : ROWS - 1);
        const uint8_t *px = cols[row];
        for (int x = 0; x < W; x++)
            fwrite(px, 1, 3, f);
    }
    close_out(f);
}

static void write_flat_white(const char *dir)
{
    enum { W = 256, H = 256 };
    char header[64];
    FILE *f = open_out(dir, "flat_white.ppm", W, H, header, sizeof header);

    uint8_t px[3] = { 255, 255, 255 };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            fwrite(px, 1, 3, f);
    close_out(f);
}

static void write_gradient(const char *dir)
{
    enum { W = 640, H = 480 };
    char header[64];
    FILE *f = open_out(dir, "gradient.ppm", W, H, header, sizeof header);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t px[3] = {
                (uint8_t)(255.0 * x / (W - 1)),
                (uint8_t)(255.0 * (W - 1 - x) / (W - 1)),
                (uint8_t)(255.0 * ((x * 7) % (W - 1)) / (W - 1)),
            };
            fwrite(px, 1, 3, f);
        }
    }
    close_out(f);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
        return 1;
    }
    const char *dir = argv[1];

    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
        die("mkdir failed");

    write_bars(dir);
    write_flat_white(dir);
    write_gradient(dir);
    return 0;
}
