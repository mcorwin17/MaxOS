/* Virtual memory. Two level paging: page directory -> page table -> frame,
 * each table 1024 entries, each page table covering 4 MB. */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PAGE_PRESENT   0x001
#define PAGE_WRITE     0x002
#define PAGE_USER      0x004

/* Builds the kernel address space and turns paging on. */
void vmm_initialize(void);

/* Both addresses must be page aligned. Allocates a page table if the region
 * doesn't have one yet. */
void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_unmap(uint32_t virt);

/* Returns 0 if nothing is mapped there. */
uint32_t vmm_get_physical(uint32_t virt);

int vmm_is_mapped(uint32_t virt);

/* Maps a fresh frame somewhere nothing is using, writes through it, checks it
 * reads back, unmaps and checks the frame count comes home. */
void vmm_selftest(void);

#endif
