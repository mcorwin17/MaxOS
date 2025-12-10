/* Virtual memory areas, per address space.
 *
 * A VMA says "this range is legitimate, and here's what should happen when
 * someone touches it". The page fault handler consults the active address
 * space's list before deciding a fault is fatal: a missing page inside a
 * region gets allocated and zeroed on the spot, a write to a shared
 * copy-on-write page gets its private copy, anything else is a real fault.
 *
 * The kernel has a static address space; processes carry their own. */

#ifndef VMA_H
#define VMA_H

#include <stdint.h>

#define VMA_WRITE  0x1
#define VMA_USER   0x2

struct vma {
    uint32_t    start;
    uint32_t    end;        /* exclusive */
    uint32_t    flags;
    struct vma* next;
};

struct addrspace {
    uint32_t    pd;         /* physical address of the page directory */
    struct vma* regions;
    uint32_t    resident;   /* pages currently backed by a frame */
};

void vma_initialize(void);

struct addrspace* vma_kernel_space(void);

/* Which address space page faults resolve against. The scheduler keeps this
 * in step with CR3. */
void vma_set_active(struct addrspace* as);
struct addrspace* vma_active(void);

void vma_as_init(struct addrspace* as, uint32_t pd);

int  vma_reserve_in(struct addrspace* as, uint32_t start, uint32_t size,
                    uint32_t flags);
void vma_release_in(struct addrspace* as, uint32_t start);
void vma_release_all(struct addrspace* as);

/* Copy every region of src into dst, sharing resident pages copy-on-write:
 * the frame is refcounted and both mappings lose the write bit. Returns 0
 * on allocation failure. */
int  vma_clone(struct addrspace* dst, struct addrspace* src);

/* True if [addr, addr+len) lies entirely inside user regions of as. What
 * syscalls use to vet pointers before touching them. */
int  vma_user_range_ok(struct addrspace* as, uint32_t addr, uint32_t len);

/* Returns non-zero if the fault was serviced. */
int  vma_handle_fault(uint32_t addr, uint32_t error_code);

/* Kernel-space wrappers, kept for the boot-time selftest. */
int  vma_reserve(uint32_t start, uint32_t size, uint32_t flags);
void vma_release(uint32_t start);
uint32_t vma_resident_pages(void);
uint32_t vma_region_count(void);

void vma_selftest(void);

#endif
