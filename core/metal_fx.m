/* metal_fx.m — Metal (GPU) implementations of the two dot effects.
 *
 * Bit-identical to the CPU core (core/dot.c) by construction:
 *   1. The MSL source below mirrors the CPU expression-for-expression
 *      (same float constants, same operation order — IEEE-754 strict,
 *      no fast-math on either side). Transcendentals go through
 *      core/fx_math.h on the CPU and the mirrored fx_sinf below.
 *   2. Emission is order-independent: each sprite contributes integer
 *      channel amounts (uint)(c*a + 0.5), atomic_add-ed into per-channel
 *      planes. Integer addition is commutative + associative, so no
 *      parallel ordering leaks into the bytes, and the final clamp is
 *      min(255, base + Σ) — the same as the CPU's sequential clamp.
 * §5 (closed form, scrub-safe) is unchanged: no integrated state.
 *
 * Objective-C is used only for the Metal plumbing; the exported API is
 * plain C (metal_fx.h) so the C adapter can call it.
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metal_fx.h"
#include "fx_hash.h" /* host-side vid.glitch state: same fx_rand01 as core/glitch.c */

static const char *kMSL =
"// MSL mirror of core/dot.c — keep in lockstep (constants & order).\n"
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"#include <metal_atomic>\n"
"\n"
"constant float FX_PI = 3.14159265f;\n"
"\n"
"uint fx_hash(uint seed, uint frame, uint x, uint y) {\n"
"    uint h = seed;\n"
"    h ^= x * 0x9E3779B9u;  h = (h << 13) | (h >> 19);  h *= 0x85EBCA6Bu;\n"
"    h ^= y * 0xC2B2AE35u;  h = (h << 17) | (h >> 15);  h *= 0x27D4EB2Fu;\n"
"    h ^= frame * 0x165667B1u;\n"
"    h ^= h >> 15;  h *= 0x2545F491u;  h ^= h >> 13;\n"
"    return h;\n"
"}\n"
"float fx_rand01(uint seed, uint frame, uint x, uint y) {\n"
"    return float(fx_hash(seed, frame, x, y) >> 8) * (1.0f / 16777216.0f);\n"
"}\n"
"float fx_sinf(float x) {\n"
"    float k = rint(x * 0.1591549431f);\n"
"    float r = x - k * 6.2831853072f;\n"
"    float r2 = r * r;\n"
"    return r * (1.0f + r2 * (-0.1666666716f + r2 * (0.0083333339f + r2 * (-0.0001984127f + r2 * 0.0000027557f))));\n"
"}\n"
"/* This GPU's '/' is not always correctly rounded (reciprocal-multiply),\n"
" * and MSL may contract an fma-based refinement differently per call\n"
" * site. Integer-exact correctly-rounded division instead: a 24-bit\n"
" * mantissa quotient with ties-to-even, using only exact integer ops,\n"
" * so the MSL compiler has no rounding freedom. Verified bit-exact vs\n"
" * CPU fdiv over a 200k-input sweep (verify_div2.c). Assumes normal\n"
" * finite x, y (true at all call sites; the '/' fallbacks are\n"
" * unreachable guards). */\n"
"float fx_div(float x, float y) {\n"
"    uint32_t ux = __builtin_bit_cast(uint32_t, x);\n"
"    uint32_t uy = __builtin_bit_cast(uint32_t, y);\n"
"    uint32_t neg = (ux ^ uy) >> 31;\n"
"    uint32_t ax = ux & 0x7FFFFFFFu, ay = uy & 0x7FFFFFFFu;\n"
"    if (ax == 0u) return neg ? -0.0f : 0.0f;\n"
"    uint32_t ex = (ax >> 23) & 0xFFu, ey = (ay >> 23) & 0xFFu;\n"
"    if (ex == 0u || ey == 0u) return x / y;\n"
"    uint32_t mx = (ax & 0x7FFFFFu) | 0x800000u;\n"
"    uint32_t my = (ay & 0x7FFFFFu) | 0x800000u;\n"
"    uint32_t k = (mx < my) ? 24u : 23u;\n"
"    uint64_t A = (uint64_t)mx << k;\n"
"    uint64_t Q = A / my;\n"
"    uint64_t R = A % my;\n"
"    if (2u * R > my || (2u * R == my && (Q & 1u))) Q++;\n"
"    int64_t E = (int64_t)ex - (int64_t)ey - (int64_t)(k - 23u) + 127;\n"
"    if (Q == ((uint64_t)1u << 24)) { Q = (uint64_t)1u << 23; E++; }\n"
"    if (E < 1 || E > 255) return x / y;\n"
"    uint32_t b = (uint32_t)(neg << 31) | ((uint32_t)E << 23) | (uint32_t)(Q & 0x7FFFFFu);\n"
"    return __builtin_bit_cast(float, b);\n"
"}\n"
"/* This GPU's sqrt() is not always correctly rounded (probe5: 1 ulp off for\n"
" * some inputs; the CPU's fsqrt is). One Newton pass with an exact fma\n"
" * residual is bit-exact vs CPU (probe5: 0/100k sweep + the failing value).\n"
" * Use for every sqrt in this mirror. */\n"
"float crsqrt(float x) {\n"
"    if (x <= 0.0f) return 0.0f;\n"
"    float r = sqrt(x);\n"
"    return r + fx_div(fma(-r, r, x), 2.0f * r);\n"
"}\n"
"/* The CPU (cc -O2, arm64) contracts a*b + c*d + e*f dots and gx*gx + gy*gy\n"
" * into explicit FMA chains (verified by disassembly of _local_shade).\n"
" * MSL fusion is context-dependent, so the mirror writes the SAME fma\n"
" * chains explicitly — fma() is a primitive op the MSL compiler keeps. */\n"
"float luma_at(device const uint8_t *px, int stride, int w, int h, int x, int y) {\n"
"    if (x < 0) x = 0;\n"
"    if (x >= w) x = w - 1;\n"
"    if (y < 0) y = 0;\n"
"    if (y >= h) y = h - 1;\n"
"    int i = y * stride + x * 3;\n"
"    return fx_div(fma(0.114f, float(px[i + 2]), fma(0.299f, float(px[i]), 0.587f * float(px[i + 1]))), 255.0f);\n"
"}\n"
"float local_shade(device const uint8_t *px, int stride, int w, int h,\n"
"                  int sx, int sy, int step, float l0,\n"
"                  thread float &shx, thread float &shy, thread float &edge) {\n"
"    float le = luma_at(px, stride, w, h, sx + step, sy);\n"
"    float lw = luma_at(px, stride, w, h, sx - step, sy);\n"
"    float ls = luma_at(px, stride, w, h, sx, sy + step);\n"
"    float ln = luma_at(px, stride, w, h, sx, sy - step);\n"
"    float gx = le - lw;\n"
"    float gy = ls - ln;\n"
"    float gmag = crsqrt(fma(gx, gx, gy * gy));\n"
"    /* subject gate: wide probe too (mirror CPU local_shade) */\n"
"    float le2 = luma_at(px, stride, w, h, sx + 3 * step, sy);\n"
"    float lw2 = luma_at(px, stride, w, h, sx - 3 * step, sy);\n"
"    float ls2 = luma_at(px, stride, w, h, sx, sy + 3 * step);\n"
"    float ln2 = luma_at(px, stride, w, h, sx, sy - 3 * step);\n"
"    float gx2 = le2 - lw2;\n"
"    float gy2 = ls2 - ln2;\n"
"    float gmag2 = crsqrt(fma(gx2, gx2, gy2 * gy2));\n"
"    /* Pin the product as an opaque atom: MSL fuses (sum*0.2f)+0.004f into\n"
"     * fma(sum,0.2f,0.004f) (empirically: shade 1-2 ulp vs the CPU dylib's\n"
"     * separate round(sum*0.2f)+add(0.004f)). fma(t,c,0.0f) is bit-identical\n"
"     * to t*c and blocks the fusion (s5 barrier idiom). */\n"
"    float lm = fma((l0 + le + lw + ls + ln), 0.2f, 0.0f);\n"
"    float shade = fx_div(l0, lm + 0.004f);\n"
"    if (shade < 0.55f) shade = 0.55f;\n"
"    if (shade > 1.45f) shade = 1.45f;\n"
"    float k = min(gmag, 1.0f) * 0.5f;\n"
"    if (gmag > 0.001f) { shx = fx_div(gx, gmag) * k; shy = fx_div(gy, gmag) * k; }\n"
"    else { shx = 0.0f; shy = 0.0f; }\n"
"    edge = (gmag > gmag2) ? gmag : gmag2;\n"
"    return shade;\n"
"}\n"
"struct Sprite { float x, y, r; float cr, cg, cb; float env; float shx, shy; float valid; };\n"
"struct HoloP { int size; int speed; int gate; int split; int bright; int w; int h; int stride; int frame; };\n"
"struct PortP { int size; int fill; int gate; int halftone; int relief; int level; int color; int glow; int split; int bright; int w; int h; int stride; };\n"
"struct GridP { int w; int h; int n; };\n"
"struct CompP { int npix; };\n"
"\n"
"kernel void clear_acc(device uint *accR [[buffer(0)]],\n"
"                      device uint *accG [[buffer(1)]],\n"
"                      device uint *accB [[buffer(2)]],\n"
"                      constant GridP &p [[buffer(3)]],\n"
"                      uint tid [[thread_position_in_grid]])\n"
"{\n"
"    if (tid >= (uint)p.n) return;\n"
"    accR[tid] = 0u; accG[tid] = 0u; accB[tid] = 0u;\n"
"}\n"
"\n"
"kernel void gather_gate(device const uint8_t *px [[buffer(0)]],\n"
"                       device Sprite *spr [[buffer(1)]],\n"
"                       constant HoloP &p [[buffer(2)]],\n"
"                       uint tid [[thread_position_in_grid]])\n"
"{\n"
"    int step = max(p.size / 2, 2);\n"
"    int cols = (p.w + step - 1) / step;\n"
"    int rows = (p.h + step - 1) / step;\n"
"    if (tid >= (uint)(cols * rows)) return;\n"
"    int sy = step / 2 + (int)(tid % (uint)rows) * step;\n"
"    int sx = step / 2 + (int)(tid / (uint)rows) * step;\n"
"    if (sx >= p.w || sy >= p.h) {\n"
"        /* The CPU gather loop (sy < height) drops grid points outside the\n"
"         * frame; the tid grid's ceil(h/step) can include them when the last\n"
"         * row straddles the edge. Mark invalid (raster skips valid < 0.5)\n"
"         * instead of reading l0 out of bounds. Mirror the CPU, not the\n"
"         * sprdump tid loop. */\n"
"        Sprite z; z.x = 0.0f; z.y = 0.0f; z.r = 0.0f;\n"
"        z.cr = 0.0f; z.cg = 0.0f; z.cb = 0.0f;\n"
"        z.env = 0.0f; z.shx = 0.0f; z.shy = 0.0f; z.valid = 0.0f;\n"
"        spr[tid] = z; return;\n"
"    }\n"
"    int i3 = sy * p.stride + sx * 3;\n"
"    /* form B: matches the CPU codegen of the l0 site here (cc -O2: fmul(0.587*G)\n"
"     * then fmadd(0.299,R) then fmadd(0.114,B) — see _dotgate/_gate_cell\n"
"     * disassembly). luma_at uses the same form B. */\n"
"    float luma = fx_div(fma(0.114f, float(px[i3 + 2]), fma(0.299f, float(px[i3]), 0.587f * float(px[i3 + 1]))), 255.0f);\n"
"    uint id = (uint)sy * 8192u + (uint)sx;\n"
"    float shx, shy, edge;\n"
"    float shade = local_shade(px, p.stride, p.w, p.h, sx, sy, step, luma, shx, shy, edge);\n"
"    uint ts = (uint)p.frame / 4u;\n"
"    float env = shade; /* twinkle flattened to 1.0, envelope floor 1.0 */\n"
"    float wob = fma(0.2f, (float)p.speed, 1.0f);\n"
"    float dy = fma(fx_sinf(fma(2.0f * FX_PI, fx_rand01(0x5EED1234u, 0u, id, 100u),\n"
"                        fx_div(2.0f * FX_PI * (float)p.frame, 60.0f))), wob, 0.0f);\n"
"    /* MSL fuses a+b*c across named temporaries (s5, tid=11 y) — pin the\n"
"     * products below with fma(t,c,0.0f); the CPU dylib kept them unfused. */\n"
"    float dx = fma(fx_rand01(0x5EED1234u, ts, id, 102u) - 0.5f, wob, 0.0f);\n"
"    Sprite s;\n"
"    s.x = (float)sx + dx;\n"
"    s.y = (float)sy + dy;\n"
"    s.r = (float)step * 0.8f; /* covers cell corners (no black gaps) */\n"
"    /* gate_ramp codegen — binary gate (full at knee, absent below) AND\n"
"     * subject region (Otsu minority side, tested at the FINAL center,\n"
"     * same as gather_portal; OOB = not subject). */\n"
"    float knee = fma((float)p.gate, 0.01f, 0.0f);\n"
"    int fcx = (int)s.x;\n"
"    int fcy = (int)s.y;\n"
"    float T = fma((float)p.split, 1.0f / 256.0f, 0.0f);\n"
"    bool subject = (fcx >= 0 && fcy >= 0 && fcx < p.w && fcy < p.h);\n"
"    if (subject) {\n"
"        int ci3 = fcy * p.stride + fcx * 3;\n"
"        float lc = fx_div(fma(0.114f, float(px[ci3 + 2]), fma(0.299f, float(px[ci3]), 0.587f * float(px[ci3 + 1]))), 255.0f);\n"
"        subject = p.bright ? (lc > T) : (lc < T);\n"
"    }\n"
"    float pf = (edge >= knee && subject) ? 1.0f : 0.0f;\n"
"    float g  = fma(fma(382.5f, fma(0.65f, luma, 0.35f), 0.0f), pf, 0.0f);\n"
"    float pr = fma(g, fma(0.26f, luma, 0.50f), 0.0f);\n"
"    float pg = g;\n"
"    float pb = fma(g, fma(0.30f, luma, 0.50f), 0.0f);\n"
"    s.cr = min(255.0f, pr * shade);\n"
"    s.cg = min(255.0f, pg * shade);\n"
"    s.cb = min(255.0f, pb * shade);\n"
"    s.env = env;\n"
"    s.shx = shx;\n"
"    s.shy = shy;\n"
"    s.valid = 1.0f;\n"
"    spr[tid] = s;\n"
"}\n"
"\n"
"kernel void gather_portal(device const uint8_t *px [[buffer(0)]],\n"
"                          device Sprite *spr [[buffer(1)]],\n"
"                          constant PortP &p [[buffer(2)]],\n"
"                          uint tid [[thread_position_in_grid]])\n"
"{\n"
"    int step = max(p.size / 2, 2);\n"
"    int cols = (p.w + step - 1) / step;\n"
"    int rows = (p.h + step - 1) / step;\n"
"    uint n = (uint)(cols * rows);\n"
"    /* 2 layers: tid < n = the dot, tid >= n = its glow aura (larger,\n"
"     * dimmer disc of the same cell). Integer accumulation in raster is\n"
"     * order-independent, so layer order does not affect the sum. */\n"
"    if (tid >= 2u * n) return;\n"
"    uint cell = (tid >= n) ? (tid - n) : tid;\n"
"    int sy = step / 2 + (int)(cell % (uint)rows) * step;\n"
"    int sx = step / 2 + (int)(cell / (uint)rows) * step;\n"
"    if (sx >= p.w || sy >= p.h) {\n"
"        /* Same edge guard as gather_gate: the CPU loop (sy < height)\n"
"         * drops out-of-frame grid points; mark invalid. */\n"
"        Sprite z; z.x = 0.0f; z.y = 0.0f; z.r = 0.0f;\n"
"        z.cr = 0.0f; z.cg = 0.0f; z.cb = 0.0f;\n"
"        z.env = 0.0f; z.shx = 0.0f; z.shy = 0.0f; z.valid = 0.0f;\n"
"        spr[tid] = z; return;\n"
"    }\n"
"    int i3 = sy * p.stride + sx * 3;\n"
"    /* form B: matches the CPU codegen of the l0 site (see gather_gate). */\n"
"    float luma = fx_div(fma(0.114f, float(px[i3 + 2]), fma(0.299f, float(px[i3]), 0.587f * float(px[i3 + 1]))), 255.0f);\n"
"    float shx, shy, edge;\n"
"    float shade = local_shade(px, p.stride, p.w, p.h, sx, sy, step, luma, shx, shy, edge);\n"
"    /* halftone: radius factor from the cell's own tone. */\n"
"    float hs = fma((float)p.halftone, 0.01f, 0.0f);\n"
"    float rf = fma(hs, luma, 1.0f - hs);\n"
"    if (rf < 0.05f) rf = 0.05f;\n"
"    if (rf > 1.0f) rf = 1.0f;\n"
"    /* separation: radius as a fraction of the grid step. */\n"
"    float fill01 = fma((float)p.fill, 0.01f, 0.0f);\n"
"    float rbase = fma(fill01, (float)step, 0.0f);\n"
"    float r = fma(rbase, rf, 0.0f);\n"
"    if (r < 0.01f) r = 0.01f;\n"
"    /* relief: displace along the local luma gradient, in px. */\n"
"    float rel01 = fma((float)p.relief, 0.01f, 0.0f);\n"
"    float relf = fma(rel01, (float)step, 0.0f);\n"
"    float knee = fma((float)p.gate, 0.01f, 0.0f);\n"
"    float lv = fma((float)p.level, 0.01f, 0.0f);\n"
"    Sprite s;\n"
"    s.x = fma(shx, relf, (float)sx);\n"
"    s.y = fma(shy, relf, (float)sy);\n"
"    s.r = r;\n"
"    /* portal_ramp codegen — binary gate (full at knee, absent below) AND\n"
"     * subject region (Otsu minority side, tested at the FINAL center:\n"
"     * relief can push it past the silhouette; OOB = not subject).\n"
"     * Hue weights from p.color at fixed sat 0.60 (360 = white sentinel). */\n"
"    int fcx = (int)s.x;\n"
"    int fcy = (int)s.y;\n"
"    float T = fma((float)p.split, 1.0f / 256.0f, 0.0f);\n"
"    bool subject = (fcx >= 0 && fcy >= 0 && fcx < p.w && fcy < p.h);\n"
"    if (subject) {\n"
"        int ci3 = fcy * p.stride + fcx * 3;\n"
"        float lc = fx_div(fma(0.114f, float(px[ci3 + 2]), fma(0.299f, float(px[ci3]), 0.587f * float(px[ci3 + 1]))), 255.0f);\n"
"        subject = p.bright ? (lc > T) : (lc < T);\n"
"    }\n"
"    float pf = (edge >= knee && subject) ? 1.0f : 0.0f;\n"
"    float g  = fma(fma(382.5f, fma(0.65f, luma, 0.35f), 0.0f), pf, 0.0f);\n"
"    float hr, hg, hb;\n"
"    if (p.color >= 360) { hr = 1.0f; hg = 1.0f; hb = 1.0f; }\n"
"    else {\n"
"    float hue = (float)p.color;\n"
"    if (hue < 0.0f) hue = 0.0f;\n"
"    float h6 = fma(hue, 1.0f / 60.0f, 0.0f);\n"
"    int hi = (int)h6;\n"
"    if (hi > 5) hi = 5;\n"
"    float fr = fma(h6, 1.0f, -(float)hi);\n"
"    float hr, hg, hb;\n"
"    if (hi == 0)      { hr = 1.0f; hg = fr;   hb = 0.0f; }\n"
"    else if (hi == 1) { hr = 1.0f; hg = 1.0f; hb = fr; }\n"
"    else if (hi == 2) { hr = 0.0f; hg = 1.0f; hb = fr; }\n"
"    else if (hi == 3) { hr = 0.0f; hg = fr;   hb = 1.0f; }\n"
"    else if (hi == 4) { hr = fr;   hg = 0.0f; hb = 1.0f; }\n"
"    else              { hr = 1.0f; hg = 0.0f; hb = fr; }\n"
"    }\n"
"    float wr = fma(hr, 0.60f, 1.0f - 0.60f);\n"
"    float wg = fma(hg, 0.60f, 1.0f - 0.60f);\n"
"    float wb = fma(hb, 0.60f, 1.0f - 0.60f);\n"
"    float pr = fma(g, wr, 0.0f);\n"
"    float pg = fma(g, wg, 0.0f);\n"
"    float pb = fma(g, wb, 0.0f);\n"
"    float cr = fma(pr * shade, lv, 0.0f);\n"
"    float cg = fma(pg * shade, lv, 0.0f);\n"
"    float cb = fma(pb * shade, lv, 0.0f);\n"
"    /* Hue-preserving clamp (mirror of the CPU): scale so the brightest\n"
"     * channel reaches 255 — a per-channel min() washes the hue to white. */\n"
"    float m = max(cr, max(cg, cb));\n"
"    if (m > 255.0f) { float sc = fx_div(255.0f, m); cr *= sc; cg *= sc; cb *= sc; }\n"
"    s.cr = cr;\n"
"    s.cg = cg;\n"
"    s.cb = cb;\n"
"    s.env = shade;\n"
"    s.shx = shx;\n"
"    s.shy = shy;\n"
"    s.valid = 1.0f;\n"
"    if (tid >= n) {\n"
"        /* glow layer: scale the same cell's dot into its aura. */\n"
"        float gl01 = fma((float)p.glow, 0.01f, 0.0f);\n"
"        float rs = fma(gl01, 1.0f, 1.0f);\n"
"        float cs = fma(0.25f, gl01, 0.0f);\n"
"        s.r = fma(s.r, rs, 0.0f);\n"
"        s.cr = fma(s.cr, cs, 0.0f);\n"
"        s.cg = fma(s.cg, cs, 0.0f);\n"
"        s.cb = fma(s.cb, cs, 0.0f);\n"
"    }\n"
"    spr[tid] = s;\n"
"}\n"
"\n"
"kernel void raster(device const Sprite *spr [[buffer(0)]],\n"
"                   device atomic_uint *accR [[buffer(1)]],\n"
"                   device atomic_uint *accG [[buffer(2)]],\n"
"                   device atomic_uint *accB [[buffer(3)]],\n"
"                   constant GridP &p [[buffer(4)]],\n"
"                   uint tid [[thread_position_in_grid]])\n"
"{\n"
"    if (tid >= (uint)p.n) return;\n"
"    Sprite s = spr[tid];\n"
"    if (s.valid < 0.5f) return;\n"
"    int xa = (int)(s.x - s.r) - 1;\n"
"    int xb = (int)(s.x + s.r) + 1;\n"
"    int ya = (int)(s.y - s.r) - 1;\n"
"    int yb = (int)(s.y + s.r) + 1;\n"
"    if (xa < 0) xa = 0;\n"
"    if (ya < 0) ya = 0;\n"
"    if (xb >= p.w) xb = p.w - 1;\n"
"    if (yb >= p.h) yb = p.h - 1;\n"
"    if (xa > xb || ya > yb) return;\n"
"    for (int sy = ya; sy <= yb; sy++) {\n"
"        float ey = fx_div((float)sy + 0.5f - s.y, s.r);\n"
"        float ey2 = ey * ey;\n"
"        for (int sx = xa; sx <= xb; sx++) {\n"
"            float ex = fx_div((float)sx + 0.5f - s.x, s.r);\n"
"            /* Mirror the CPU codegen (otool _dotgate): d2 = fma(ex,ex,ey2);\n"
"            * a = env*((1-d2)*(1+shx*ex+shy*ey)), inner adds as fma; stamp\n"
"            * = fma(c,a,0.5) (cc -O2 contracts c*a+0.5f; one rounding). fma()\n"
"            * is a MSL primitive the compiler keeps; plain mul/add drifts 1 ulp. */\n"
"            float d2 = fma(ex, ex, ey2);\n"
"            if (d2 >= 1.0f) continue;\n"
"            /* pin the two muls as fma(..,0.0f): this MSL re-associates pure\n"
"             * mul chains (a 1-ulp off on `a`; ex/ey/d2/t/u were bit-exact). */\n"
"            float a = fma((1.0f - d2), fma(s.shy, ey, fma(s.shx, ex, 1.0f)), 0.0f);\n"
"            a = fma(s.env, a, 0.0f);\n"
"            if (a < 0.0f) a = 0.0f;\n"
"            uint i = (uint)sy * (uint)p.w + (uint)sx;\n"
"            float ar = fma(s.cr, a, 0.5f); atomic_fetch_add_explicit(accR + i, (uint)ar, memory_order_relaxed);\n"
"            float ag = fma(s.cg, a, 0.5f); atomic_fetch_add_explicit(accG + i, (uint)ag, memory_order_relaxed);\n"
"            float ab = fma(s.cb, a, 0.5f); atomic_fetch_add_explicit(accB + i, (uint)ab, memory_order_relaxed);\n"
"        }\n"
"    }\n"
"}\n"
"\n"
"kernel void compose(device uint8_t *px [[buffer(0)]],\n"
"                    device uint *accR [[buffer(1)]],\n"
"                    device uint *accG [[buffer(2)]],\n"
"                    device uint *accB [[buffer(3)]],\n"
"                    constant CompP &p [[buffer(4)]],\n"
"                    uint tid [[thread_position_in_grid]])\n"
"{\n"
"    if (tid >= (uint)p.npix) return;\n"
"    /* base removed: out = dot accumulation, 255-clamped */\n"
"    int i3 = (int)tid * 3;\n"
"    px[i3]     = (uint8_t)min(255u, accR[tid]);\n"
"    px[i3 + 1] = (uint8_t)min(255u, accG[tid]);\n"
"    px[i3 + 2] = (uint8_t)min(255u, accB[tid]);\n"
"}\n"
"/* ---- vid.glitch: mirror of core/glitch.c's per-pixel composition. ---- */\n"
"/* The per-frame derived state (burst gate, band/block/scan geometry,\n"
" * tear strip, split magnitude) is computed host-side in\n"
" * metal_vid_glitch with the same expressions and fx_rand01 — this kernel\n"
" * is the per-pixel pass only, integer except the noise term. */\n"
"struct VGBand { int y0, hgt, dx; };\n"
"struct VGBlock { int x0, y0, w, h, mode, ox, oy, sr, sg, sb; };\n"
"struct VGScan { int row, mode, dx, fl; };\n"
"struct VGState {\n"
"    int w, h, stride;\n"
"    uint seed, t;\n"
"    float noise_m;\n"
"    int k, dir, levels;\n"
"    VGBand band[12];\n"
"    VGBlock block[8];\n"
"    VGScan scan[40];\n"
"    int tear_y0, tear_hgt, tear_dy;\n"
"    int fwdx, fwdy;\n"
"};\n"
"int vgposter(int v, int levels) {\n"
"    int idx = (v * levels + 127) / 255; /* 0..levels */\n"
"    return (idx * 255) / levels;\n"
"}\n"
"kernel void vidglitch(device const uint8_t *src [[buffer(0)]],\n"
"                      device uint8_t *dst [[buffer(1)]],\n"
"                      constant VGState &p [[buffer(2)]],\n"
"                      uint tid [[thread_position_in_grid]])\n"
"{\n"
"    if (tid >= (uint)((uint)p.w * (uint)p.h)) return;\n"
"    int x = (int)(tid % (uint)p.w);\n"
"    int y = (int)(tid / (uint)p.w);\n"
"\n"
"    int bxoff = 0;\n"
"    for (int b = 0; b < 12; b++)\n"
"        if (p.band[b].hgt > 0 && y >= p.band[b].y0 && y < p.band[b].y0 + p.band[b].hgt) { bxoff = p.band[b].dx; break; }\n"
"    int sdx = 0, sfl = 0;\n"
"    for (int i = 0; i < 40; i++)\n"
"        if (p.scan[i].row == y) { if (p.scan[i].mode == 0) sdx = p.scan[i].dx; else sfl = p.scan[i].fl; break; }\n"
"    int dy = (p.tear_hgt > 0 && y >= p.tear_y0 && y < p.tear_y0 + p.tear_hgt) ? p.tear_dy : 0;\n"
"\n"
"    int x0 = x + p.fwdx; if (x0 < 0) x0 = 0; if (x0 >= p.w) x0 = p.w - 1;\n"
"    int y0 = y + p.fwdy; if (y0 < 0) y0 = 0; if (y0 >= p.h) y0 = p.h - 1;\n"
"    int sy = y0 - dy; if (sy < 0) sy = 0; if (sy >= p.h) sy = p.h - 1;\n"
"    int sx = x0 + bxoff + sdx; if (sx < 0) sx = 0; if (sx >= p.w) sx = p.w - 1;\n"
"\n"
"    int mode = -1, ox = 0, oy = 0, sr = 0, sg = 0, sb = 0;\n"
"    for (int i = 0; i < 8; i++) {\n"
"        if (p.block[i].w > 0 && x >= p.block[i].x0 && x < p.block[i].x0 + p.block[i].w &&\n"
"            y >= p.block[i].y0 && y < p.block[i].y0 + p.block[i].h) {\n"
"            mode = p.block[i].mode; ox = p.block[i].ox; oy = p.block[i].oy;\n"
"            sr = p.block[i].sr; sg = p.block[i].sg; sb = p.block[i].sb;\n"
"            break;\n"
"        }\n"
"    }\n"
"    if (mode == 0) {\n"
"        sx = x0 + ox; if (sx < 0) sx = 0; if (sx >= p.w) sx = p.w - 1;\n"
"        sy = y0 + oy; if (sy < 0) sy = 0; if (sy >= p.h) sy = p.h - 1;\n"
"    }\n"
"    int rx = sx + p.dir * p.k; if (rx < 0) rx = 0; if (rx >= p.w) rx = p.w - 1;\n"
"    int bx = sx - p.dir * p.k; if (bx < 0) bx = 0; if (bx >= p.w) bx = p.w - 1;\n"
"\n"
"    int base = sy * p.stride;\n"
"    int cr = (int)src[base + rx * 3 + 0];\n"
"    int cg = (int)src[base + sx * 3 + 1];\n"
"    int cb = (int)src[base + bx * 3 + 2];\n"
"    if (sfl > 0) { cr = (cr * sfl) / 100; cg = (cg * sfl) / 100; cb = (cb * sfl) / 100; }\n"
"    if (mode == 1) { cr = 255 - cr; cg = 255 - cg; cb = 255 - cb; }\n"
"    if (mode == 2) { cr = sr; cg = sg; cb = sb; }\n"
"    if (p.levels > 1) { cr = vgposter(cr, p.levels); cg = vgposter(cg, p.levels); cb = vgposter(cb, p.levels); }\n"
"    int n = 0;\n"
"    if (p.noise_m > 0.0f) {\n"
"        uint pid = (uint)y * (uint)p.w + (uint)x;\n"
"        /* G_NOISE = 9. The CPU (cc -O2) does three separate roundings —\n"
"         * (r-0.5) then *2.0 then *noise_m — plus the fx_round idiom\n"
"         * (biased add + truncating cast, never a builtin round).\n"
"         * fma(x,y,0.0f) is the exact mul and blocks MSL fusing the chain\n"
"         * into one rounding (s5 barrier idiom). */\n"
"        float q = fx_rand01(p.seed, p.t, pid, 9u) - 0.5f;\n"
"        float q2 = fma(q, 2.0f, 0.0f);\n"
"        float q3 = fma(q2, p.noise_m, 0.0f);\n"
"        n = (int)(q3 + (q3 >= 0.0f ? 0.5f : -0.5f));\n"
"    }\n"
"    int dr = cr + n; if (dr < 0) dr = 0; if (dr > 255) dr = 255;\n"
"    int dg = cg + n; if (dg < 0) dg = 0; if (dg > 255) dg = 255;\n"
"    int db = cb + n; if (db < 0) db = 0; if (db > 255) db = 255;\n"
"    int di = y * p.stride + x * 3;\n"
"    dst[di]     = (uint8_t)dr;\n"
"    dst[di + 1] = (uint8_t)dg;\n"
"    dst[di + 2] = (uint8_t)db;\n"
"}\n"
"/* ---- dot.spacengrave: mirror of core/dot.c's dot_spacengrave(). ---- */\n"
"/* Per-row scalars (phase, d, tri) are derived host-side (same expressions,\n"
" * same compiler), and per-pixel reads/writes are independent, so one\n"
" * per-pixel kernel over (w, h) is the whole effect: dimmed pass-through,\n"
" * or dimmed base + additive ink. Reads the src snapshot, writes dst —\n"
" * never read-modify-write one buffer (self-feedback pitfall, dot.c §5.1). */\n"
"struct RowTri { int y; float tri; };\n"
"struct SEState {\n"
"    int w, h, stride;\n"
"    int pitch, probe;\n"
"    float halfw, inv_pitch;\n"
"    float wr, wg, wb, lv, bias, grain01, half01;\n"
"    float keep, T, knee;\n"
"    int bright, gate_on;\n"
"    RowTri rows[2048];\n"
"};\n"
"kernel void spacengrave(device const uint8_t *src [[buffer(0)]],\n"
"                        device uint8_t *dst [[buffer(1)]],\n"
"                        constant SEState &p [[buffer(2)]],\n"
"                        uint tid [[thread_position_in_grid]])\n"
"{\n"
"    if (tid >= (uint)((uint)p.w * (uint)p.h)) return;\n"
"    int x = (int)(tid % (uint)p.w);\n"
"    int y = (int)(tid / (uint)p.w);\n"
"    int i3 = y * p.stride + x * 3;\n"
"    /* dimmed base — the CPU's global dim pass, one rounding per byte.\n"
"     * keep = 1.0 is exact, so this reduces to the untouched source */\n"
"    float br = (float)src[i3 + 0] * p.keep + 0.5f;\n"
"    float bg = (float)src[i3 + 1] * p.keep + 0.5f;\n"
"    float bb = (float)src[i3 + 2] * p.keep + 0.5f;\n"
"    float tri = p.rows[y].tri;\n"
"    int ar = 0, ag = 0, ab = 0;\n"
"    if (tri > 0.0f) {\n"
"        float l0 = luma_at(src, p.stride, p.w, p.h, x, y);\n"
"        float shx, shy, edge;\n"
"        local_shade(src, p.stride, p.w, p.h, x, y, p.probe, l0, shx, shy, edge);\n"
"        float e = edge * 4.0f;\n"
"        if (e > 1.0f) e = 1.0f; /* 0..1 local contrast */\n"
"        int pass = 1;\n"
"        if (p.gate_on != 0) {\n"
"            /* subject gate: knee + subject side of the Otsu split */\n"
"            if (e < p.knee) pass = 0;\n"
"            else pass = p.bright != 0 ? (l0 > p.T) : (l0 < p.T);\n"
"        }\n"
"        if (pass != 0) {\n"
"            float cov = p.bias\n"
"                      - p.grain01 * (e - 0.5f)\n"
"                      + p.half01 * (l0 - 0.5f);\n"
"            if (cov < 0.0f) cov = 0.0f;\n"
"            if (cov > 1.0f) cov = 1.0f;\n"
"            /* ordered-look dither, deterministic in (x, y) */\n"
"            float t = fx_rand01(0u, 0u, (uint)x, (uint)y);\n"
"            if (cov >= t) {\n"
"                float ink = 255.0f * tri * p.lv * (0.15f + 0.85f * l0);\n"
"                ar = (int)(ink * p.wr + (ink * p.wr >= 0.0f ? 0.5f : -0.5f));\n"
"                ag = (int)(ink * p.wg + (ink * p.wg >= 0.0f ? 0.5f : -0.5f));\n"
"                ab = (int)(ink * p.wb + (ink * p.wb >= 0.0f ? 0.5f : -0.5f));\n"
"                if (ar < 0) ar = 0;\n"
"                if (ag < 0) ag = 0;\n"
"                if (ab < 0) ab = 0;\n"
"            }\n"
"        }\n"
"    }\n"
"    if (ar <= 0 && ag <= 0 && ab <= 0) { /* dashed off / off-stroke */\n"
"        dst[i3]     = (uint8_t)br;\n"
"        dst[i3 + 1] = (uint8_t)bg;\n"
"        dst[i3 + 2] = (uint8_t)bb;\n"
"        return;\n"
"    }\n"
"    /* frame already globally dimmed (keep) — add the ink, clip at 255 */\n"
"    int v;\n"
"    v = (int)br + ar; dst[i3]     = (uint8_t)(v > 255 ? 255 : v);\n"
"    v = (int)bg + ag; dst[i3 + 1] = (uint8_t)(v > 255 ? 255 : v);\n"
"    v = (int)bb + ab; dst[i3 + 2] = (uint8_t)(v > 255 ? 255 : v);\n"
"}\n";

typedef struct {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipe_clear;
    id<MTLComputePipelineState> pipe_gate;
    id<MTLComputePipelineState> pipe_portal;
    id<MTLComputePipelineState> pipe_raster;
    id<MTLComputePipelineState> pipe_compose;
    id<MTLComputePipelineState> pipe_vid;
    id<MTLComputePipelineState> pipe_se;
    /* Buffer cache, keyed by (w, h, stride, nspr) — reused across frames. */
    int cw, ch, cstride, cnspr;
    id<MTLBuffer> in;
    id<MTLBuffer> spr;
    id<MTLBuffer> accR, accG, accB;
    /* vid.glitch buffers, own cache: src snapshot + dst only (no sprites). */
    int vw, vh, vstride;
    id<MTLBuffer> vid_in, vid_out;
} metal_ctx_t;

static metal_ctx_t ctx = { 0 };
static int ctx_ready = 0;

/* Guards ctx (device, buffer cache, render) against concurrent callers —
 * FCPX may render in parallel. Created in metal_fx_init() (the only
 * pre-render entry point), never released (process lifetime). A render
 * path that sees nil has never been initialised — returns -1, exactly
 * like the device-nil check did. Recursive, so a re-entrant init cannot
 * deadlock. */
static NSRecursiveLock *ctx_lock = nil;

static void release_ctx_buffers(void)
{
    [ctx.in release];     ctx.in = nil;
    [ctx.spr release];    ctx.spr = nil;
    [ctx.accR release];   ctx.accR = nil;
    [ctx.accG release];   ctx.accG = nil;
    [ctx.accB release];   ctx.accB = nil;
    ctx.cw = ctx.ch = ctx.cstride = ctx.cnspr = 0;
    [ctx.vid_in release]; ctx.vid_in = nil;
    [ctx.vid_out release]; ctx.vid_out = nil;
    ctx.vw = ctx.vh = ctx.vstride = 0;
}

static void ensure_buffers(int w, int h, int stride, int nspr)
{
    if (ctx.cw == w && ctx.ch == h && ctx.cstride == stride && ctx.cnspr == nspr)
        return;
    release_ctx_buffers();
    size_t nb = (size_t)w * (size_t)h * 3;
    size_t na = (size_t)w * (size_t)h * 4;
    ctx.in = [ctx.device newBufferWithLength:nb options:MTLResourceStorageModeShared];
    ctx.spr = [ctx.device newBufferWithLength:(size_t)nspr * 44 options:MTLResourceStorageModeShared];
    ctx.accR = [ctx.device newBufferWithLength:na options:MTLResourceStorageModeShared];
    ctx.accG = [ctx.device newBufferWithLength:na options:MTLResourceStorageModeShared];
    ctx.accB = [ctx.device newBufferWithLength:na options:MTLResourceStorageModeShared];
    if (!ctx.in || !ctx.spr || !ctx.accR || !ctx.accG || !ctx.accB) {
        release_ctx_buffers();
        return;
    }
    ctx.cw = w; ctx.ch = h; ctx.cstride = stride; ctx.cnspr = nspr;
}

/* Structs must match the MSL definitions exactly. */
typedef struct { int size, speed, gate, split, bright, w, h, stride, frame; } HoloP;
typedef struct { int size, fill, gate, halftone, relief, level, color, glow, split, bright, w, h, stride; } PortP;
typedef struct { int w, h, n; } GridP;
typedef struct { int npix; } CompP;

static int metal_render_locked(int w, int h, int stride,
                               id<MTLComputePipelineState> gather,
                               const void *params, size_t paramlen,
                               int nspr,
                               const uint8_t *src, uint8_t *dst)
{
    if (ctx.device == nil)
        return -1;
    ensure_buffers(w, h, stride, nspr);
    if (ctx.in == nil)
        return -1;

    size_t nb = (size_t)w * (size_t)h * 3;
    int npix = w * h;
    int tg = 256;
    GridP gp;
    CompP cp;

    @autoreleasepool {
        memcpy([ctx.in contents], src, nb);
        /* Zero every sprite slot: gather leaves skipped samples unwritten
         * and shared storage is not guaranteed zero-initialized — stale
         * slots with valid >= 0.5 would rasterize phantom sprites. */
        memset([ctx.spr contents], 0, (size_t)nspr * 44);
        id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        if (cb == nil || enc == nil)
            return -1;
        MTLSize grid = MTLSizeMake(tg, 1, 1);

        gp.w = w; gp.h = h; gp.n = npix;
        cp.npix = npix;

        /* 1) clear the accumulator planes */
        [enc setComputePipelineState:ctx.pipe_clear];
        [enc setBuffer:ctx.accR offset:0 atIndex:0];
        [enc setBuffer:ctx.accG offset:0 atIndex:1];
        [enc setBuffer:ctx.accB offset:0 atIndex:2];
        [enc setBytes:&gp length:sizeof(gp) atIndex:3];
        [enc dispatchThreadgroups:MTLSizeMake((npix + tg - 1) / tg, 1, 1)
            threadsPerThreadgroup:grid];

        /* 2) gather sprites from the ORIGINAL pixels */
        [enc setComputePipelineState:gather];
        [enc setBuffer:ctx.in offset:0 atIndex:0];
        [enc setBuffer:ctx.spr offset:0 atIndex:1];
        [enc setBytes:params length:paramlen atIndex:2];
        [enc dispatchThreadgroups:MTLSizeMake((nspr + tg - 1) / tg, 1, 1)
            threadsPerThreadgroup:grid];

        /* 3) rasterize (order-independent integer accumulation) */
        gp.n = nspr;
        [enc setComputePipelineState:ctx.pipe_raster];
        [enc setBuffer:ctx.spr offset:0 atIndex:0];
        [enc setBuffer:ctx.accR offset:0 atIndex:1];
        [enc setBuffer:ctx.accG offset:0 atIndex:2];
        [enc setBuffer:ctx.accB offset:0 atIndex:3];
        [enc setBytes:&gp length:sizeof(gp) atIndex:4];
        [enc dispatchThreadgroups:MTLSizeMake((nspr + tg - 1) / tg, 1, 1)
            threadsPerThreadgroup:grid];

        /* 4) compose: dim(base) + accumulation, 255-clamped, in place */
        [enc setComputePipelineState:ctx.pipe_compose];
        [enc setBuffer:ctx.in offset:0 atIndex:0];
        [enc setBuffer:ctx.accR offset:0 atIndex:1];
        [enc setBuffer:ctx.accG offset:0 atIndex:2];
        [enc setBuffer:ctx.accB offset:0 atIndex:3];
        [enc setBytes:&cp length:sizeof(cp) atIndex:4];
        [enc dispatchThreadgroups:MTLSizeMake((npix + tg - 1) / tg, 1, 1)
            threadsPerThreadgroup:grid];

        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.error != nil) {
            fprintf(stderr, "retrofx metal: %s\n",
                    [[cb.error localizedDescription] UTF8String]);
            return -1;
        }
        memcpy(dst, [ctx.in contents], nb);
    }
    return 0;
}

/* Lock wrapper — holds ctx_lock for the whole render + copy-back. */
static int metal_render(int w, int h, int stride,
                        id<MTLComputePipelineState> gather,
                        const void *params, size_t paramlen,
                        int nspr,
                        const uint8_t *src, uint8_t *dst)
{
    if (ctx_lock == nil)
        return -1; /* metal_fx_init() has never been called */
    [ctx_lock lock];
    int rc = metal_render_locked(w, h, stride, gather, params, paramlen,
                                 nspr, src, dst);
    [ctx_lock unlock];
    return rc;
}

/* ---- vid.glitch: one-shot src→dst kernel, no sprite pipeline ---------- */

static void ensure_vid_buffers(int w, int h, int stride)
{
    if (ctx.vw == w && ctx.vh == h && ctx.vstride == stride)
        return;
    if (ctx.vid_in != nil) {
        [ctx.vid_in release];  ctx.vid_in = nil;
        [ctx.vid_out release]; ctx.vid_out = nil;
    }
    ctx.vw = ctx.vh = ctx.vstride = 0;
    size_t nb = (size_t)w * (size_t)h * 3;
    ctx.vid_in  = [ctx.device newBufferWithLength:nb options:MTLResourceStorageModeShared];
    ctx.vid_out = [ctx.device newBufferWithLength:nb options:MTLResourceStorageModeShared];
    if (ctx.vid_in == nil || ctx.vid_out == nil) {
        if (ctx.vid_in != nil)  [ctx.vid_in release];  ctx.vid_in = nil;
        if (ctx.vid_out != nil) [ctx.vid_out release]; ctx.vid_out = nil;
        return;
    }
    ctx.vw = w; ctx.vh = h; ctx.vstride = stride;
}

static int metal_render_vid_locked(id<MTLComputePipelineState> pipe,
                                   int w, int h, int stride,
                                   uint8_t *rgb,
                                   const void *state, size_t statelen)
{
    if (ctx.device == nil)
        return -1;
    ensure_vid_buffers(w, h, stride);
    if (ctx.vid_in == nil)
        return -1;

    size_t nb = (size_t)w * (size_t)h * 3;
    int npix = w * h;
    int tg = 256;

    @autoreleasepool {
        memcpy([ctx.vid_in contents], rgb, nb);
        id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        if (cb == nil || enc == nil)
            return -1;
        [enc setComputePipelineState:pipe];
        [enc setBuffer:ctx.vid_in offset:0 atIndex:0];
        [enc setBuffer:ctx.vid_out offset:0 atIndex:1];
        [enc setBytes:state length:statelen atIndex:2];
        [enc dispatchThreadgroups:MTLSizeMake((npix + tg - 1) / tg, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.error != nil) {
            fprintf(stderr, "retrofx metal: %s\n",
                    [[cb.error localizedDescription] UTF8String]);
            return -1;
        }
        memcpy(rgb, [ctx.vid_out contents], nb);
    }
    return 0;
}

/* Lock wrapper — holds ctx_lock for the whole render + copy-back. */
static int metal_render_vid(id<MTLComputePipelineState> pipe, int w, int h, int stride,
                            uint8_t *rgb,
                            const void *state, size_t statelen)
{
    if (ctx_lock == nil)
        return -1; /* metal_fx_init() has never been called */
    [ctx_lock lock];
    int rc = metal_render_vid_locked(pipe, w, h, stride, rgb, state, statelen);
    [ctx_lock unlock];
    return rc;
}

static int metal_fx_init_locked(void);

int metal_fx_init(void)
{
    if (ctx_lock == nil)
        ctx_lock = [[NSRecursiveLock alloc] init]; /* once; not released */
    [ctx_lock lock];
    int rc = metal_fx_init_locked();
    [ctx_lock unlock];
    return rc;
}

static int metal_fx_init_locked(void)
{
    if (ctx_ready)
        return ctx.device != nil ? 0 : -1;
    ctx_ready = 1;

    @autoreleasepool {
        ctx.device = MTLCreateSystemDefaultDevice();
        if (ctx.device == nil)
            return -1;
        NSError *e = nil;
        MTLCompileOptions *copts = [MTLCompileOptions new];
        copts.languageVersion = MTLLanguageVersion3_1;
        id<MTLLibrary> lib =
            [ctx.device newLibraryWithSource:@(kMSL) options:copts error:&e];
        [copts release];
        if (lib == nil) {
            fprintf(stderr, "retrofx metal: MSL compile failed: %s\n",
                    e != nil ? [[e localizedDescription] UTF8String] : "?");
            [ctx.device release];
            ctx.device = nil;
            return -1;
        }
        int fail = 0;
#define GETPIPE(name, var) \
    do { \
        id<MTLFunction> f = [lib newFunctionWithName:@(#name)]; \
        if (f != nil) \
            ctx.var = [ctx.device newComputePipelineStateWithFunction:f error:&e]; \
        if (ctx.var == nil) { \
            fprintf(stderr, "retrofx metal: pipeline %s failed\n", #name); \
            fail = 1; \
        } \
    } while (0)
        GETPIPE(clear_acc, pipe_clear);
        GETPIPE(gather_gate, pipe_gate);
        GETPIPE(gather_portal, pipe_portal);
        GETPIPE(raster, pipe_raster);
        GETPIPE(compose, pipe_compose);
        GETPIPE(vidglitch, pipe_vid);
        GETPIPE(spacengrave, pipe_se);
#undef GETPIPE
        if (fail) {
            [lib release];
            [ctx.device release];
            ctx.device = nil;
            return -1;
        }
        ctx.queue = [ctx.device newCommandQueue];
        [lib release];
    }
    return 0;
}

void metal_fx_shutdown(void)
{
    if (ctx_lock == nil)
        return; /* never initialised */
    [ctx_lock lock];
    release_ctx_buffers();
    [ctx.pipe_clear release];    ctx.pipe_clear = nil;
    [ctx.pipe_gate release];     ctx.pipe_gate = nil;
    [ctx.pipe_portal release];   ctx.pipe_portal = nil;
    [ctx.pipe_raster release];   ctx.pipe_raster = nil;
    [ctx.pipe_compose release];  ctx.pipe_compose = nil;
    [ctx.pipe_vid release];      ctx.pipe_vid = nil;
    [ctx.pipe_se release];       ctx.pipe_se = nil;
    [ctx.queue release];         ctx.queue = nil;
    [ctx.device release];        ctx.device = nil;
    ctx_ready = 0;
    [ctx_lock unlock];
}

/* ---- public C API ------------------------------------------------------ */

int metal_dotgate(uint8_t *rgb, int width, int height, int stride,
                  const dotgate_params_t *p, int frame)
{
    if (rgb == NULL || width <= 0 || height <= 0 || stride < width * 3 ||
        p == NULL)
        return -1;
    /* identity guards — mirror dotgate() exactly */
    if (p->size < 1 || p->speed < 0 || p->gate < 1)
        return 0;
    if (frame < 0)
        frame = 0;

    int step = p->size / 2;
    if (step < 2) step = 2;
    int nspr = ((width + step - 1) / step) * ((height + step - 1) / step);

    HoloP mp;
    mp.size = p->size;
    mp.speed = p->speed;
    mp.gate = p->gate;
    /* Subject region — same host-side split as dotgate(). */
    int subj_bright = 0;
    mp.split = portal_split(rgb, width, height, stride, &subj_bright);
    mp.bright = subj_bright;
    mp.w = width;
    mp.h = height;
    mp.stride = stride;
    mp.frame = frame;
    return metal_render(width, height, stride, ctx.pipe_gate,
                        &mp, sizeof(mp), nspr, rgb, rgb);
}

int metal_dotportal(uint8_t *rgb, int width, int height, int stride,
                    const dotportal_params_t *p, int frame)
{
    if (rgb == NULL || width <= 0 || height <= 0 || stride < width * 3 ||
        p == NULL)
        return -1;
    /* identity guards — mirror dotportal() exactly */
    if (p->size < 1 || p->fill < 0 || p->gate < 1 ||
        p->halftone < 0 || p->relief < 0 || p->level < 0 ||
        p->color < 0 || p->glow < 0)
        return 0;
    (void)frame; /* reserved: the dot field is time-invariant */

    int step = p->size / 2;
    if (step < 2) step = 2;
    int nspr = ((width + step - 1) / step) * ((height + step - 1) / step);

    PortP mp;
    mp.size = p->size;
    mp.fill = p->fill;
    mp.gate = p->gate;
    mp.halftone = p->halftone;
    mp.relief = p->relief;
    mp.level = p->level;
    mp.color = p->color;
    mp.glow = p->glow;
    /* Subject region — same host-side split as dotportal(). */
    int subj_bright = 0;
    mp.split = portal_split(rgb, width, height, stride, &subj_bright);
    mp.bright = subj_bright;
    mp.w = width;
    mp.h = height;
    mp.stride = stride;
    return metal_render(width, height, stride, ctx.pipe_portal,
                        &mp, sizeof(mp), nspr * 2, rgb, rgb);
}

/* ---- vid.glitch -------------------------------------------------------- */

/* round-half-away-from-zero — mirrors glitch.c's fx_round. */
static int vg_round(float r)
{
    return (int)(r + (r >= 0.0f ? 0.5f : -0.5f));
}

static int vg_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* 1-D value noise — mirrors glitch.c's gl_vnoise (grain fixed at 5,
 * its only call site: the burst gate). */
static float vg_vnoise(uint32_t seed, int frame, uint32_t key)
{
    int i = frame / 5;
    float r = (float)(frame - i * 5) / 5.0f;
    float a = fx_rand01(seed, (uint32_t)i, 0u, key);
    float b = fx_rand01(seed, (uint32_t)i + 1u, 0u, key);
    float s = r * r * (3.0f - 2.0f * r); /* smoothstep */
    return a + (b - a) * s;
}

/* Host mirror of the MSL VGState — same fields, same order (all 4-byte,
 * so the layouts match). */
typedef struct { int y0, hgt, dx; } vg_band_t;
typedef struct { int x0, y0, w, h, mode, ox, oy, sr, sg, sb; } vg_block_t;
typedef struct { int row, mode, dx, fl; } vg_scan_t;
typedef struct {
    int w, h, stride;
    uint32_t seed, t;
    float noise_m;
    int k, dir, levels;
    vg_band_t band[12];
    vg_block_t block[8];
    vg_scan_t scan[40];
    int tear_y0, tear_hgt, tear_dy;
    int fwdx, fwdy;
} VGState;

int metal_vid_glitch(uint8_t *rgb, int width, int height, int stride,
                     const vid_glitch_params_t *p, int frame)
{
    if (rgb == NULL || width <= 0 || height <= 0 || stride < width * 3 ||
        p == NULL)
        return -1;

    /* Per-frame derived state: mirrors vid_glitch() (core/glitch.c)
     * expression-for-expression with the same fx_rand01 (fx_hash.h), same
     * compiler — host side, so the GPU has no float surface beyond the
     * kernel's noise term. Key ints are glitch.c's G_* enum (1..27). */
    int amount = vg_clampi(p->amount, 0, 100);
    if (amount == 0)
        return 0; /* identity: master off */

    int rgbv   = vg_clampi(p->rgb, 0, 100);
    int noise  = vg_clampi(p->noise, 0, 100);
    int bnd    = vg_clampi(p->bands, 0, 100);
    int blk    = vg_clampi(p->blocks, 0, 100);
    int scn    = vg_clampi(p->scan, 0, 100);
    int quant  = vg_clampi(p->quant, 0, 100);
    int vt     = vg_clampi(p->vtear, 0, 100);
    int seedv  = vg_clampi(p->seed, 0, 999);
    uint32_t seed = 0x746736u + (uint32_t)seedv; /* GL_SEED_BASE */

    if (frame < 0)
        frame = 0;
    uint32_t t = (uint32_t)frame;

    /* (1) burst envelope — the whole effect is intermittent. */
    float duty = (float)amount / 100.0f;
    if (!(vg_vnoise(seed, frame, 1u) < duty)) /* G_BURST = 1 */
        return 0; /* clean: outside a burst */

    VGState s;
    memset(&s, 0, sizeof s);
    s.w = width; s.h = height; s.stride = stride;
    s.seed = seed; s.t = t;
    s.noise_m = (float)(noise * 2); /* 50 -> ±100 (v2-max), 100 -> ±200 */

    /* rgb split, per frame. v4: 50 = v2-max (±32 px), 100 = 2×. */
    int k = 0, dir = 1;
    if (rgbv > 0) {
        int kmax = (rgbv * 64) / 100; /* 50 -> 32, 100 -> 64 */
        if (kmax < 1) kmax = 1;
        k = (int)(fx_rand01(seed, t, 0u, 7u) * (float)(kmax + 1)); /* G_RGBK */
        dir = (fx_rand01(seed, t, 0u, 8u) > 0.5f) ? 1 : -1;        /* G_RGBDIR */
    }
    s.k = k;
    s.dir = dir;

    /* horizontal slice tears: random-height slices, max |dx| = bnd px. */
    for (int b = 0; b < 12; b++) {
        s.band[b].y0 = 0; s.band[b].hgt = 0; s.band[b].dx = 0;
        if (bnd == 0 || fx_rand01(seed, t, (uint32_t)b, 2u) >= 0.55f) /* G_BANDON */
            continue;
        int hmax = height < 96 ? height : 96;
        if (hmax < 4) hmax = 4;
        s.band[b].hgt = 4 + (int)(fx_rand01(seed, t, (uint32_t)b, 11u)
                                  * (float)(hmax - 3)); /* G_BANDH, 4..hmax */
        int span = height - s.band[b].hgt;
        if (span < 0) span = 0;
        s.band[b].y0 = (int)(fx_rand01(seed, t, (uint32_t)b, 10u) /* G_BANDY */
                             * (float)(span + 1));
        int bmax = bnd * 2; /* 50 -> ±100 px (v2-max), 100 -> ±200 */
        if (bmax < 2) bmax = 2;
        s.band[b].dx = vg_round((fx_rand01(seed, t, (uint32_t)b, 3u) - 0.5f) /* G_BANDDX */
                                * 2.0f * (float)bmax);
    }

    /* block/tile corruption: tiles up to blk px, mode dup/invert/solid. */
    for (int i = 0; i < 8; i++) {
        s.block[i].w = 0;
        if (blk == 0 || fx_rand01(seed, t, (uint32_t)i, 12u) >= 0.70f) /* G_BLOKON */
            continue;
        int bmax = blk * 2; /* 50 -> 100 px (v2-max), 100 -> 200 */
        if (bmax < 2) bmax = 2;
        int smin = bmax / 2;
        if (smin < 2) smin = 2;
        if (smin > bmax) smin = bmax;
        int w = smin + (int)(fx_rand01(seed, t, (uint32_t)i, 15u) /* G_BLOKW */
                             * (float)(bmax - smin + 1));
        int h = smin + (int)(fx_rand01(seed, t, (uint32_t)i, 16u) /* G_BLOKH */
                             * (float)(bmax - smin + 1));
        if (w > width) w = width;
        if (h > height) h = height;
        s.block[i].x0 = (int)(fx_rand01(seed, t, (uint32_t)i, 13u) /* G_BLOKX */
                              * (float)(width - w + 1));
        s.block[i].y0 = (int)(fx_rand01(seed, t, (uint32_t)i, 14u) /* G_BLOKY */
                              * (float)(height - h + 1));
        s.block[i].w = w;
        s.block[i].h = h;
        s.block[i].mode = (int)(fx_rand01(seed, t, (uint32_t)i, 17u) * 3.0f); /* G_BLOKMD */
        int omax = (blk * 64) / 100; /* 50 -> ±16 (v2-max), 100 -> ±32 */
        if (omax < 2) omax = 2;
        s.block[i].ox = vg_round((fx_rand01(seed, t, (uint32_t)i, 18u) - 0.5f) * (float)omax); /* G_BLOKOX ±omax/2 */
        s.block[i].oy = vg_round((fx_rand01(seed, t, (uint32_t)i, 19u) - 0.5f) * (float)omax); /* G_BLOKOY ±omax/2 */
        s.block[i].sr = (int)(fx_rand01(seed, t, (uint32_t)i, 20u) * 255.0f); /* G_BLOKSR */
        s.block[i].sg = (int)(fx_rand01(seed, t, (uint32_t)i, 21u) * 255.0f); /* G_BLOKSG */
        s.block[i].sb = (int)(fx_rand01(seed, t, (uint32_t)i, 22u) * 255.0f); /* G_BLOKSB */
    }

    /* scanline jitter: thin rows that shift or flicker in brightness. */
    int nscan = scn > 0 ? 1 + (scn * 39) / 100 : 0;
    if (nscan > 40) nscan = 40;
    for (int i = 0; i < 40; i++) {
        s.scan[i].row = -1;
        if (i >= nscan || fx_rand01(seed, t, (uint32_t)i, 23u) >= 0.80f) /* G_SCANON */
            continue;
        s.scan[i].row = (int)(fx_rand01(seed, t, (uint32_t)i, 24u) /* G_SCANROW */
                              * (float)height);
        s.scan[i].mode = (fx_rand01(seed, t, (uint32_t)i, 25u) < 0.60f) ? 0 : 1; /* G_SCANMD */
        int smax = (scn * 48) / 100; /* 50 -> ±12 (v2-max), 100 -> ±24 */
        if (smax < 2) smax = 2;
        s.scan[i].dx = vg_round((fx_rand01(seed, t, (uint32_t)i, 26u) - 0.5f) * (float)smax); /* G_SCANDX ±smax/2 */
        s.scan[i].fl = 100 + vg_round((fx_rand01(seed, t, (uint32_t)i, 27u) - 0.5f) /* G_SCANFL */
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
    s.levels = levels;

    /* vertical sync tear: one strip, per frame. */
    if (vt > 0 && fx_rand01(seed, t, 0u, 4u) < (float)vt * 0.007f) { /* G_TEARON */
        int tsh = (height * vt) / 300; /* 50 -> h/6 (v2-max), 100 -> h/3 */
        if (tsh < 2) tsh = 2;
        if (tsh > height) tsh = height;
        int span = height - tsh;
        if (span < 0) span = 0;
        s.tear_y0 = (int)(fx_rand01(seed, t, 0u, 5u) * (float)(span + 1)); /* G_TEARY */
        s.tear_hgt = tsh;
        s.tear_dy = vg_round((fx_rand01(seed, t, 0u, 6u) - 0.5f) /* G_TEARDY */
                             * (float)(vt * 4)); /* 50 -> ±100 (v2-max), 100 -> ±200 */
    }

    /* Whole-frame "signal loss" catastrophe — mirrors glitch.c: driven by
     * the strongest element's severity, so 50 = the v2-max look exactly. */
    int sev = rgbv > noise ? rgbv : noise;
    if (bnd > sev) sev = bnd;
    if (blk > sev) sev = blk;
    if (scn > sev) sev = scn;
    if (quant > sev) sev = quant;
    if (vt > sev) sev = vt;
    if (sev >= 80 &&
        fx_rand01(seed, t, 0u, 28u) < (float)(sev - 79) * 0.02f) { /* G_FWON */
        int hz = fx_rand01(seed, t, 0u, 29u) < 0.5f;                /* G_FWMD */
        int span = (sev - 79) * (hz ? width : height) / 100; /* 100 -> 21% */
        int off = vg_round((fx_rand01(seed, t, 0u, 30u) - 0.5f) /* G_FWDX */
                           * 2.0f * (float)span);
        if (hz) s.fwdx = off;
        else    s.fwdy = off;
    }

    return metal_render_vid(ctx.pipe_vid, width, height, stride, rgb, &s, sizeof s);
}

/* ---- dot.spacengrave ---------------------------------------------------- */

/* Structs must match the MSL definitions exactly (SEState/RowTri). */
typedef struct { int y; float tri; } se_row_t;
typedef struct {
    int w, h, stride;
    int pitch, probe;
    float halfw, inv_pitch;
    float wr, wg, wb, lv, bias, grain01, half01;
    float keep, T, knee;
    int bright, gate_on;
    se_row_t rows[2048];
} SEState;

int metal_spacengrave(uint8_t *rgb, int width, int height, int stride,
                      const dot_spacengrave_params_t *p, int frame)
{
    if (rgb == NULL || width <= 0 || height <= 0 || stride < width * 3 ||
        p == NULL)
        return -1;
    /* guards — mirror dot_spacengrave() in core/dot.c exactly (a -1 here
     * degrades to the CPU core, so the guards must agree). */
    if (p->fill < 0 || p->halftone < 0 || p->line < 0 ||
        p->level < 0 || p->grain < 0 || p->color < 0 ||
        p->dim < 0 || p->gate < 0)
        return 0; /* invalid params: leave the frame untouched */
    if (p->size < 2)
        return 0; /* 0 = off (identity); 1 = sub-pixel pitch, no sensible ink */
    if (height > 2048)
        return -1; /* the MSL per-row table is bounded (rows[2048]) */
    (void)frame; /* reserved: the field is time-invariant */

    int pitch = p->size;
    int probe = pitch / 2;
    if (probe < 1) probe = 1;
    float half = (float)p->fill * 0.005f; /* stroke half-width, /period */
    float inv_pitch = 1.0f / (float)pitch;

    int gate_on = p->gate >= 1;
    int bright = 0;
    float T = 0.0f;
    float knee = 0.0f;
    if (gate_on) {
        int split = portal_split(rgb, width, height, stride, &bright);
        T = (float)split / 256.0f;
        knee = (float)p->gate * 0.01f;
    }

    float r1, g1, b1;
    portal_hue((float)p->color, &r1, &g1, &b1); /* core/dot.c, via dot.h */
    const float sat = 0.60f; /* same fixed saturation as portal_ramp */
    float wr = r1 * sat + (1.0f - sat);
    float wg = g1 * sat + (1.0f - sat);
    float wb = b1 * sat + (1.0f - sat);
    float lv = (float)p->level * 0.01f;    /* 0..2 */
    float bias = (float)p->line * 0.01f;   /* 0..1 */
    float grain01 = (float)p->grain * 0.01f; /* 0..1 */
    float half01 = (float)p->halftone * 0.01f; /* 0..1 */
    float keep = 1.0f - (float)p->dim * 0.01f; /* 1 = photo, 0 = black */

    SEState s;
    memset(&s, 0, sizeof s);
    s.w = width; s.h = height; s.stride = stride;
    s.pitch = pitch;
    s.probe = probe;
    s.halfw = half;
    s.inv_pitch = inv_pitch;
    s.wr = wr; s.wg = wg; s.wb = wb;
    s.lv = lv; s.bias = bias; s.grain01 = grain01; s.half01 = half01;
    s.keep = keep; s.T = T; s.knee = knee;
    s.bright = bright;
    s.gate_on = gate_on;

    /* per-row stroke envelope — mirrors dot_spacengrave() expression-for-
     * expression (same compiler, so the roundings agree). */
    for (int y = 0; y < height; y++) {
        float phase = (float)(y % pitch) * inv_pitch;
        float d = fabsf(phase - 0.25f);
        s.rows[y].y = y;
        s.rows[y].tri = (d >= half) ? 0.0f : (1.0f - d / half);
    }

    return metal_render_vid(ctx.pipe_se, width, height, stride, rgb, &s, sizeof s);
}


/* ---- debug (dual hunt) ------------------------------------------------- */

/* Gather only, then copy the sprite slots back (10 floats/cell, tid
 * order — the MSL Sprite layout). */
static int metal_sprdump_locked(int w, int h, int stride,
                                id<MTLComputePipelineState> gather,
                                const void *params, size_t paramlen,
                                int nspr,
                                const uint8_t *src, float *out)
{
    if (ctx.device == nil)
        return -1;
    ensure_buffers(w, h, stride, nspr);
    if (ctx.in == nil)
        return -1;

    size_t nb = (size_t)w * (size_t)h * 3;

    @autoreleasepool {
        memcpy([ctx.in contents], src, nb);
        memset([ctx.spr contents], 0, (size_t)nspr * 44);
        id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        if (cb == nil || enc == nil)
            return -1;
        MTLSize grid = MTLSizeMake(256, 1, 1);
        [enc setComputePipelineState:gather];
        [enc setBuffer:ctx.in offset:0 atIndex:0];
        [enc setBuffer:ctx.spr offset:0 atIndex:1];
        [enc setBytes:params length:paramlen atIndex:2];
        [enc dispatchThreadgroups:MTLSizeMake((nspr + 255) / 256, 1, 1)
            threadsPerThreadgroup:grid];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.error != nil) {
            fprintf(stderr, "retrofx metal: %s\n",
                    [[cb.error localizedDescription] UTF8String]);
            return -1;
        }
        memcpy(out, [ctx.spr contents], (size_t)nspr * 40);
    }
    return 0;
}

/* Lock wrapper — holds ctx_lock for the whole gather + copy-back. */
static int metal_sprdump(int w, int h, int stride,
                         id<MTLComputePipelineState> gather,
                         const void *params, size_t paramlen,
                         int nspr,
                         const uint8_t *src, float *out)
{
    if (ctx_lock == nil)
        return -1; /* metal_fx_init() has never been called */
    [ctx_lock lock];
    int rc = metal_sprdump_locked(w, h, stride, gather, params, paramlen,
                                  nspr, src, out);
    [ctx_lock unlock];
    return rc;
}

int metal_dotgate_sprdump(const uint8_t *rgb, int width, int height, int stride,
                          const dotgate_params_t *p, int frame,
                          float *out)
{
    if (rgb == NULL || out == NULL || width <= 0 || height <= 0 ||
        stride < width * 3 || p == NULL)
        return -1;
    /* guards — mirror metal_dotgate(); -1 here means identity/skip */
    if (p->size < 1 || p->speed < 0 || p->gate < 1)
        return -1;
    if (frame < 0)
        frame = 0;

    int step = p->size / 2;
    if (step < 2) step = 2;
    int nspr = ((width + step - 1) / step) * ((height + step - 1) / step);

    HoloP mp;
    mp.size = p->size;
    mp.speed = p->speed;
    mp.gate = p->gate;
    /* Subject region — same host-side split as metal_dotgate(). */
    int subj_bright = 0;
    mp.split = portal_split(rgb, width, height, stride, &subj_bright);
    mp.bright = subj_bright;
    mp.w = width;
    mp.h = height;
    mp.stride = stride;
    mp.frame = frame;
    return metal_sprdump(width, height, stride, ctx.pipe_gate,
                         &mp, sizeof(mp), nspr, rgb, out);
}

int metal_dotportal_sprdump(const uint8_t *rgb, int width, int height, int stride,
                            const dotportal_params_t *p, int frame,
                            float *out)
{
    if (rgb == NULL || out == NULL || width <= 0 || height <= 0 ||
        stride < width * 3 || p == NULL)
        return -1;
    /* guards — mirror metal_dotportal(); -1 here means identity/skip */
    if (p->size < 1 || p->fill < 0 || p->gate < 1 ||
        p->halftone < 0 || p->relief < 0 || p->level < 0 ||
        p->color < 0 || p->glow < 0)
        return -1;
    (void)frame; /* reserved: the dot field is time-invariant */

    int step = p->size / 2;
    if (step < 2) step = 2;
    int nspr = ((width + step - 1) / step) * ((height + step - 1) / step);

    PortP mp;
    mp.size = p->size;
    mp.fill = p->fill;
    mp.gate = p->gate;
    mp.halftone = p->halftone;
    mp.relief = p->relief;
    mp.level = p->level;
    mp.color = p->color;
    mp.glow = p->glow;
    /* Subject region — same host-side split as dotportal(). */
    int subj_bright = 0;
    mp.split = portal_split(rgb, width, height, stride, &subj_bright);
    mp.bright = subj_bright;
    mp.w = width;
    mp.h = height;
    mp.stride = stride;
    return metal_sprdump(width, height, stride, ctx.pipe_portal,
                         &mp, sizeof(mp), nspr, rgb, out);
}
