#include <stdint.h>

#include "bcache.h"
#include "ata.h"
#include "serial.h"
#include "panic.h"
#include "spinlock.h"

#define NBUF 16

static struct buf bufs[NBUF];
static uint32_t   clock_stamp;
static uint32_t   hits, misses;
static struct spinlock cache_lock;

static struct buf* lookup(uint32_t lba) {
    for (int i = 0; i < NBUF; ++i) {
        if (bufs[i].valid && bufs[i].lba == lba) return &bufs[i];
    }
    return 0;
}

static struct buf* pick_victim(void) {
    struct buf* oldest = &bufs[0];

    for (int i = 0; i < NBUF; ++i) {
        if (!bufs[i].valid) return &bufs[i];        /* free wins outright */
        if (bufs[i].stamp < oldest->stamp) oldest = &bufs[i];
    }

    return oldest;
}

struct buf* bread(uint32_t lba) {
    uint32_t irq = spin_lock_irq(&cache_lock);

    struct buf* b = lookup(lba);
    if (b) {
        hits++;
        b->stamp = ++clock_stamp;
        spin_unlock_irq(&cache_lock, irq);
        return b;
    }

    misses++;
    b = pick_victim();

    /* Evicting someone else's dirty data writes it back first - dropping
     * it would turn an eviction into silent corruption. */
    if (b->valid && b->dirty) {
        if (ata_write_sector(b->lba, b->data) != 0) {
            spin_unlock_irq(&cache_lock, irq);
            return 0;
        }
        b->dirty = 0;
    }

    if (ata_read_sector(lba, b->data) != 0) {
        b->valid = 0;
        spin_unlock_irq(&cache_lock, irq);
        return 0;
    }

    b->lba   = lba;
    b->valid = 1;
    b->dirty = 0;
    b->stamp = ++clock_stamp;

    spin_unlock_irq(&cache_lock, irq);
    return b;
}

void bwrite(struct buf* b) {
    if (!b || !b->valid) panic("bwrite: not a cached buffer");
    b->dirty = 1;
    b->stamp = ++clock_stamp;
}

int bflush(void) {
    uint32_t irq = spin_lock_irq(&cache_lock);

    int rc = 0;
    for (int i = 0; i < NBUF; ++i) {
        if (!bufs[i].valid || !bufs[i].dirty) continue;

        if (ata_write_sector(bufs[i].lba, bufs[i].data) != 0) {
            rc = -1;
            continue;
        }
        bufs[i].dirty = 0;
    }

    spin_unlock_irq(&cache_lock, irq);
    return rc;
}

uint32_t bcache_hits(void)   { return hits; }
uint32_t bcache_misses(void) { return misses; }

void bcache_initialize(void) {
    spin_init(&cache_lock, "bcache");
    for (int i = 0; i < NBUF; ++i) bufs[i].valid = 0;
    clock_stamp = 0;
    hits = misses = 0;
}

/* Write a recognizable pattern through the cache, read it back three ways -
 * cache hit, fresh device read, and (from the host, after qemu exits) the
 * bytes of the disk image file itself. */
#define TEST_LBA 100

void bcache_selftest(void) {
    if (!ata_present()) {
        kprintf("bcache: no disk, selftest skipped\n");
        return;
    }

    struct buf* b = bread(TEST_LBA);
    if (!b) panic("bcache selftest: read failed");

    static const char magic[8] = { 'M','A','X','O','S','D','S','K' };
    for (int i = 0; i < 8; ++i) b->data[i] = (uint8_t)magic[i];
    for (int i = 8; i < 512; ++i) b->data[i] = (uint8_t)((i * 7 + 13) & 0xFF);
    bwrite(b);

    /* Same sector again: must be a hit, and must show the new bytes. */
    uint32_t h = hits;
    struct buf* again = bread(TEST_LBA);
    if (again != b)  panic("bcache selftest: same lba, different buffer");
    if (hits != h + 1) panic("bcache selftest: reread wasn't a hit");

    if (bflush() != 0) panic("bcache selftest: flush failed");

    /* Around the cache entirely: the device itself has to have it now. */
    uint8_t direct[512];
    if (ata_read_sector(TEST_LBA, direct) != 0) {
        panic("bcache selftest: direct read failed");
    }
    for (int i = 0; i < 8; ++i) {
        if (direct[i] != (uint8_t)magic[i]) {
            panic("bcache selftest: flush never reached the device");
        }
    }
    for (int i = 8; i < 512; ++i) {
        if (direct[i] != (uint8_t)((i * 7 + 13) & 0xFF)) {
            panic("bcache selftest: device data wrong past the magic");
        }
    }

    kprintf("bcache: selftest ok, sector %u written through and verified "
            "on the device\n", TEST_LBA);
}
