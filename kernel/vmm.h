/* Virtual memory. Two level paging: page directory -> page table -> frame.
 *
 * There's one kernel directory, and processes each get their own. Kernel
 * mappings (the identity map and the heap) are shared into every process
 * directory by copying the PDEs, so they point at the same page tables and
 * a kernel mapping made through one is visible in all. */

#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PAGE_PRESENT   0x001
#define PAGE_WRITE     0x002
#define PAGE_USER      0x004

/* Builds the kernel address space and turns paging on. */
void vmm_initialize(void);

uint32_t vmm_kernel_directory(void);

/* Mark the 4 MB slot containing virt as kernel-owned, so every process
 * directory copies it. Must be called before any process exists - it
 * updates the template, not the directories already cloned from it. */
void vmm_share_pde(uint32_t virt);

/* New directory with the kernel PDEs copied in. Returns the physical (and,
 * thanks to the identity map, also virtual) address, or 0. */
uint32_t vmm_create_directory(void);

/* Frees the directory and any page tables that aren't shared with the
 * kernel. Frames referenced by leftover PTEs are NOT freed - release the
 * VMAs first. Never call on the directory currently in CR3. */
void vmm_destroy_directory(uint32_t pd);

/* Explicit-directory operations. The _in forms work on any directory; the
 * bare forms are the kernel directory. */
void     vmm_map_in(uint32_t pd, uint32_t virt, uint32_t phys, uint32_t flags);
void     vmm_unmap_in(uint32_t pd, uint32_t virt);
uint32_t vmm_get_physical_in(uint32_t pd, uint32_t virt);
int      vmm_is_mapped_in(uint32_t pd, uint32_t virt);

/* Rewrites the flag bits of an existing mapping (for copy-on-write's
 * write-bit games). Panics if nothing is mapped there. */
void     vmm_set_flags_in(uint32_t pd, uint32_t virt, uint32_t flags);

void     vmm_map(uint32_t virt, uint32_t phys, uint32_t flags);
void     vmm_unmap(uint32_t virt);
uint32_t vmm_get_physical(uint32_t virt);
int      vmm_is_mapped(uint32_t virt);

void vmm_selftest(void);

#endif
