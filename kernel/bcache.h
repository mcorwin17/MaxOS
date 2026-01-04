/* Block cache in front of the disk.
 *
 * Without this every read hits the device; with it, repeat reads are a
 * memory access and writes coalesce until a flush. Goes in before the
 * filesystem on purpose - retrofitting a cache through code that assumed
 * synchronous I/O is the painful order. */

#ifndef BCACHE_H
#define BCACHE_H

#include <stdint.h>

struct buf {
    uint32_t lba;
    int      valid;
    int      dirty;
    uint32_t stamp;         /* for LRU eviction */
    uint8_t  data[512];
};

/* Cached read: hit is a lookup, miss reads the device and may evict (and
 * write back) the oldest buffer. Returns 0 on failure. */
struct buf* bread(uint32_t lba);

/* Mark modified. Nothing touches the device until flush or eviction. */
void bwrite(struct buf* b);

/* Write every dirty buffer back. Non-zero if any write failed. */
int  bflush(void);

uint32_t bcache_hits(void);
uint32_t bcache_misses(void);

void bcache_initialize(void);

void bcache_selftest(void);

#endif
