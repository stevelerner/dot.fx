/* fx_hash.h — deterministic per-pixel randomness.
 *
 * Any randomness in the core must be a pure function of (seed, frame, x, y).
 * Editors scrub and seek, and renders get split across machines — a frame
 * must be byte-identical no matter what was rendered before it.
 */

#ifndef FX_HASH_H
#define FX_HASH_H

#include <stdint.h>

/* 32-bit integer hash. Returns uniform bits for any input tuple.
 * Marked `inline` so that including translation units that do not call it
 * do not trigger -Wunused-function under -Werror. */
static inline uint32_t fx_hash(uint32_t seed, uint32_t frame, uint32_t x, uint32_t y)
{
    uint32_t h = seed;
    h ^= x * 0x9E3779B9u;  h = (h << 13) | (h >> 19);  h *= 0x85EBCA6Bu;
    h ^= y * 0xC2B2AE35u;  h = (h << 17) | (h >> 15);  h *= 0x27D4EB2Fu;
    h ^= frame * 0x165667B1u;
    h ^= h >> 15;  h *= 0x2545F491u;  h ^= h >> 13;
    return h;
}

/* Uniform float in [0,1). */
static inline float fx_rand01(uint32_t seed, uint32_t frame, uint32_t x, uint32_t y)
{
    return (float)(fx_hash(seed, frame, x, y) >> 8) * (1.0f / 16777216.0f);
}

#endif /* FX_HASH_H */
