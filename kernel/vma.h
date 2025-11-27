/* Virtual memory areas.
 *
 * A VMA says "this range of addresses is legitimate, and here's what should
 * happen when someone touches it". The page fault handler consults the list
 * before deciding a fault is fatal: inside a region it allocates and maps a
 * frame on the spot, outside one it's a genuine fault.
 *
 * This is the piece fork and copy-on-write get built on, which is why it goes
 * in before threads rather than after. */

#ifndef VMA_H
#define VMA_H

#include <stdint.h>

#define VMA_WRITE  0x1
#define VMA_USER   0x2

void vma_initialize(void);

/* Reserves address space without backing any of it. Costs one small
 * allocation, not one frame per page. */
int  vma_reserve(uint32_t start, uint32_t size, uint32_t flags);

/* Unmaps and frees whatever ended up resident, then drops the region. */
void vma_release(uint32_t start);

/* Returns non-zero if the fault was inside a region and has been serviced. */
int  vma_handle_fault(uint32_t addr);

uint32_t vma_resident_pages(void);
uint32_t vma_region_count(void);

void vma_selftest(void);

#endif
