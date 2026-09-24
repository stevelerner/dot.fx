/* fx_scratch.h — persistent, grow-only scratch buffers.
 *
 * Effect cores use per-frame scratch (row buffers, luma rows, full-frame
 * snapshots). Each slot is allocated once, grown only when a bigger frame
 * arrives, and never freed — kills the per-frame malloc/free churn.
 * Slots are shared across cores and effects: every user fully writes the
 * region before reading it, and effect calls are sequential, so sharing is
 * safe.
 */

#ifndef FX_SCRATCH_H
#define FX_SCRATCH_H

#include <stdint.h>
#include <stdlib.h>

static inline uint8_t *fx_scratch(int slot, size_t need)
{
    static uint8_t *buf[3] = { NULL, NULL, NULL };
    static size_t   cap[3] = { 0, 0, 0 };
    if (slot < 0 || slot > 2 || need == 0)
        return NULL;
    if (need > cap[slot]) {
        uint8_t *b = realloc(buf[slot], need);
        if (b == NULL)
            return NULL;
        buf[slot] = b;
        cap[slot] = need;
    }
    return buf[slot];
}

#endif /* FX_SCRATCH_H */
