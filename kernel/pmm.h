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

/* A run of physically adjacent frames, for devices that DMA a flat range.
 * Separate from the normal path because "allocate n times and hope they're
 * neighbours" stops being true the moment anything has freed a frame in the
 * middle - which the selftests do before any driver starts. */
uint32_t pmm_alloc_contiguous(uint32_t count);

/* Sharing for copy-on-write. A frame starts at count 1 when allocated;
 * pmm_ref bumps it, pmm_free_frame decrements and only really frees at
 * zero. So "free" always means "drop my claim", shared or not. */
void     pmm_ref(uint32_t addr);
uint32_t pmm_refcount(uint32_t addr);

uint32_t pmm_total_frames(void);
uint32_t pmm_free_frames(void);
uint32_t pmm_usable_bytes(void);

/* Highest address worth identity mapping: the end of the last E820 region
 * of ANY type, not just usable. Firmware parks ACPI tables in reserved
 * regions immediately above RAM, and a map that stops at usable RAM can't
 * read them. */
uint32_t pmm_map_limit(void);

/* Prints the E820 map as the BIOS gave it to us. */
void pmm_dump_map(void);

/* Allocates a batch, checks nothing repeats, frees it, and checks the free
 * count lands exactly back where it started. A drift of one frame is a real
 * leak and worth catching now rather than in month twelve. */
void pmm_selftest(void);

#endif
