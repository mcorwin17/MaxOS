/* Physical frame allocator. Bitmap, one bit per 4 KB frame. */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE      4096
#define PMM_NO_FRAME   0        /* 0 is never handed out, so it doubles as the
                                 * failure value - the first page is reserved
                                 * anyway */

void pmm_initialize(void);

uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t addr);

uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_usable_bytes(void);

/* Prints the E820 map as the BIOS gave it to us. */
void pmm_dump_map(void);

/* Allocates a batch, checks nothing repeats, frees it, and checks the free
 * count lands exactly back where it started. A drift of one frame is a real
 * leak and worth catching now rather than in month twelve. */
void pmm_selftest(void);

#endif
