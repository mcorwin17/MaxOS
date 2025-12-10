#include <stdint.h>
#include <stddef.h>

#include "vma.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"
#include "panic.h"

static struct addrspace  kernel_as;
static struct addrspace* active;

/* Page fault error code bits. */
#define PF_PRESENT 0x1
#define PF_WRITE   0x2

void vma_initialize(void) {
    vma_as_init(&kernel_as, vmm_kernel_directory());
    active = &kernel_as;
}

struct addrspace* vma_kernel_space(void) { return &kernel_as; }
struct addrspace* vma_active(void)       { return active; }
void vma_set_active(struct addrspace* as) { active = as ? as : &kernel_as; }

void vma_as_init(struct addrspace* as, uint32_t pd) {
    as->pd       = pd;
    as->regions  = 0;
    as->resident = 0;
}

static struct vma* find(struct addrspace* as, uint32_t addr) {
    for (struct vma* v = as->regions; v; v = v->next) {
        if (addr >= v->start && addr < v->end) return v;
    }
    return 0;
}

int vma_reserve_in(struct addrspace* as, uint32_t start, uint32_t size,
                   uint32_t flags) {
    if ((start | size) & (PAGE_SIZE - 1)) {
        panic("vma_reserve: unaligned region");
    }

    uint32_t end = start + size;

    /* Overlapping regions would make the fault handler's answer depend on
     * list order, which is a bug waiting to be blamed on something else. */
    for (struct vma* v = as->regions; v; v = v->next) {
        if (start < v->end && end > v->start) return 0;
    }

    struct vma* v = (struct vma*)kmalloc(sizeof(struct vma));
    if (!v) return 0;

    v->start = start;
    v->end   = end;
    v->flags = flags;
    v->next  = as->regions;
    as->regions = v;

    return 1;
}

static void release_region(struct addrspace* as, struct vma* v) {
    for (uint32_t addr = v->start; addr < v->end; addr += PAGE_SIZE) {
        uint32_t phys = vmm_get_physical_in(as->pd, addr);
        if (!phys) continue;

        vmm_unmap_in(as->pd, addr);
        pmm_free_frame(phys & ~(PAGE_SIZE - 1));
        as->resident--;
    }
}

void vma_release_in(struct addrspace* as, uint32_t start) {
    struct vma** link = &as->regions;

    while (*link && (*link)->start != start) link = &(*link)->next;
    if (!*link) return;

    struct vma* v = *link;
    release_region(as, v);

    *link = v->next;
    kfree(v);
}

void vma_release_all(struct addrspace* as) {
    while (as->regions) {
        struct vma* v = as->regions;
        release_region(as, v);
        as->regions = v->next;
        kfree(v);
    }
}

int vma_clone(struct addrspace* dst, struct addrspace* src) {
    for (struct vma* v = src->regions; v; v = v->next) {
        if (!vma_reserve_in(dst, v->start, v->end - v->start, v->flags)) {
            return 0;
        }

        for (uint32_t addr = v->start; addr < v->end; addr += PAGE_SIZE) {
            uint32_t phys = vmm_get_physical_in(src->pd, addr);
            if (!phys) continue;            /* never touched, stays lazy */

            uint32_t frame = phys & ~(PAGE_SIZE - 1);

            /* Share the frame, strip the write bit from BOTH sides. The
             * next write from either becomes a fault, and the fault makes
             * the private copy. Order matters: refcount up before the
             * parent loses write, so a racing fault sees a shared frame. */
            pmm_ref(frame);

            uint32_t ro = (v->flags & VMA_USER) ? PAGE_USER : 0;
            vmm_map_in(dst->pd, addr, frame, ro);
            vmm_set_flags_in(src->pd, addr, ro);

            dst->resident++;
        }
    }

    return 1;
}

int vma_user_range_ok(struct addrspace* as, uint32_t addr, uint32_t len) {
    if (len == 0) return 1;

    uint32_t end = addr + len;
    if (end < addr) return 0;               /* wrapped */

    /* Every byte must land in a user region. Walk region by region rather
     * than page by page so a range spanning two adjacent regions passes. */
    uint32_t at = addr;
    while (at < end) {
        struct vma* v = find(as, at);
        if (!v || !(v->flags & VMA_USER)) return 0;

        at = v->end;
    }

    return 1;
}

int vma_handle_fault(uint32_t addr, uint32_t error_code) {
    struct addrspace* as = active;

    struct vma* v = find(as, addr);
    if (!v) return 0;

    uint32_t page = addr & ~(PAGE_SIZE - 1);

    /* Write to a present page: only legitimate if this is copy-on-write,
     * meaning the region says writable but the mapping says not. */
    if (error_code & PF_PRESENT) {
        if (!(error_code & PF_WRITE)) return 0;
        if (!(v->flags & VMA_WRITE))  return 0;

        uint32_t phys  = vmm_get_physical_in(as->pd, page);
        if (!phys) return 0;
        uint32_t frame = phys & ~(PAGE_SIZE - 1);

        uint32_t flags = PAGE_WRITE;
        if (v->flags & VMA_USER) flags |= PAGE_USER;

        if (pmm_refcount(frame) > 1) {
            /* Shared: make a private copy. Both frames are identity mapped,
             * so the copy is a plain loop regardless of whose CR3 this is. */
            uint32_t copy = pmm_alloc_frame();
            if (copy == PMM_NO_FRAME) return 0;

            const uint32_t* from = (const uint32_t*)(uintptr_t)frame;
            uint32_t*       to   = (uint32_t*)(uintptr_t)copy;
            for (uint32_t i = 0; i < PAGE_SIZE / 4; ++i) to[i] = from[i];

            vmm_map_in(as->pd, page, copy, flags);
            pmm_free_frame(frame);          /* drop our claim on the shared one */
        } else {
            /* Last owner standing: just give the write bit back. */
            vmm_set_flags_in(as->pd, page, flags);
        }

        return 1;
    }

    /* Not present: demand paging. */
    uint32_t frame = pmm_alloc_frame();
    if (frame == PMM_NO_FRAME) return 0;

    uint32_t flags = 0;
    if (v->flags & VMA_WRITE) flags |= PAGE_WRITE;
    if (v->flags & VMA_USER)  flags |= PAGE_USER;

    vmm_map_in(as->pd, page, frame, flags);

    /* Zero through the identity mapping: it's always mapped, and anonymous
     * memory must not hand out whatever the frame's last owner left. */
    uint32_t* p = (uint32_t*)(uintptr_t)frame;
    for (uint32_t i = 0; i < PAGE_SIZE / 4; ++i) p[i] = 0;

    as->resident++;
    return 1;
}

/* Kernel-space wrappers. */
int vma_reserve(uint32_t start, uint32_t size, uint32_t flags) {
    return vma_reserve_in(&kernel_as, start, size, flags);
}
void vma_release(uint32_t start)  { vma_release_in(&kernel_as, start); }
uint32_t vma_resident_pages(void) { return kernel_as.resident; }

uint32_t vma_region_count(void) {
    uint32_t n = 0;
    for (struct vma* v = kernel_as.regions; v; v = v->next) ++n;
    return n;
}


/* 1.25 GB: past the identity map, past the heap, nothing else is there. */
#define TEST_BASE  0x50000000
#define TEST_SIZE  (16 * 1024 * 1024)

void vma_selftest(void) {
    uint32_t frames_before = pmm_free_frames();

    if (!vma_reserve(TEST_BASE, TEST_SIZE, VMA_WRITE)) {
        panic("vma selftest: reserve failed");
    }

    /* The whole point: 16 MB of address space should not have cost 16 MB of
     * frames. Allow a couple for the heap growing to hold the region struct. */
    uint32_t after_reserve = pmm_free_frames();
    if (frames_before - after_reserve > 2) {
        kprintf("vma: reserving 16 MB consumed %u frames\n",
                frames_before - after_reserve);
        panic("vma selftest: reserve allocated eagerly");
    }

    /* Touch four pages spread across the region. */
    const uint32_t offsets[4] = { 0, 4 * 1024 * 1024, 9 * 1024 * 1024,
                                  TEST_SIZE - PAGE_SIZE };

    for (int i = 0; i < 4; ++i) {
        volatile uint32_t* p = (volatile uint32_t*)(TEST_BASE + offsets[i]);

        if (*p != 0) panic("vma selftest: fresh page wasn't zeroed");

        *p = 0xABCD0000u + (uint32_t)i;
    }

    for (int i = 0; i < 4; ++i) {
        volatile uint32_t* p = (volatile uint32_t*)(TEST_BASE + offsets[i]);
        if (*p != 0xABCD0000u + (uint32_t)i) {
            panic("vma selftest: page didn't hold what was written");
        }
    }

    if (vma_resident_pages() != 4) {
        panic("vma selftest: wrong number of pages became resident");
    }

    vma_release(TEST_BASE);

    if (vma_resident_pages() != 0) panic("vma selftest: pages still resident");

    /* The data frames come back; the page tables built to hold them are
     * kept on purpose - freeing an empty table means scanning all 1024
     * entries on every unmap, and it's reused the moment anything maps into
     * the same 4 MB. Accounted for, not tolerated: more than one per region
     * touched means something really leaked. */
    uint32_t frames_after = pmm_free_frames();
    uint32_t retained = after_reserve - frames_after;

    if (retained > 5) {
        kprintf("vma: %u frames before release, %u after, %u retained\n",
                after_reserve, frames_after, retained);
        panic("vma selftest: frames leaked beyond the page tables");
    }

    kprintf("vma: selftest ok, 16 MB reserved for %u frames, 4 faulted in, "
            "%u page tables retained\n",
            frames_before - after_reserve, retained);
}
