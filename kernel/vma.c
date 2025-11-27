#include <stdint.h>

#include "vma.h"
#include "vmm.h"
#include "pmm.h"
#include "heap.h"
#include "serial.h"
#include "panic.h"

struct vma {
    uint32_t    start;
    uint32_t    end;        /* exclusive */
    uint32_t    flags;
    struct vma* next;
};

static struct vma* regions;
static uint32_t    resident_pages;

void vma_initialize(void) {
    regions = 0;
    resident_pages = 0;
}

static struct vma* find(uint32_t addr) {
    for (struct vma* v = regions; v; v = v->next) {
        if (addr >= v->start && addr < v->end) return v;
    }
    return 0;
}

int vma_reserve(uint32_t start, uint32_t size, uint32_t flags) {
    if ((start | size) & (PAGE_SIZE - 1)) {
        panic("vma_reserve: unaligned region");
    }

    uint32_t end = start + size;

    /* Overlapping regions would make the fault handler's answer depend on
     * list order, which is a bug waiting to be blamed on something else. */
    for (struct vma* v = regions; v; v = v->next) {
        if (start < v->end && end > v->start) return 0;
    }

    struct vma* v = (struct vma*)kmalloc(sizeof(struct vma));
    if (!v) return 0;

    v->start = start;
    v->end   = end;
    v->flags = flags;
    v->next  = regions;
    regions  = v;

    return 1;
}

void vma_release(uint32_t start) {
    struct vma** link = &regions;

    while (*link && (*link)->start != start) link = &(*link)->next;
    if (!*link) return;

    struct vma* v = *link;

    for (uint32_t addr = v->start; addr < v->end; addr += PAGE_SIZE) {
        uint32_t phys = vmm_get_physical(addr);
        if (!phys) continue;

        vmm_unmap(addr);
        pmm_free_frame(phys & ~(PAGE_SIZE - 1));
        resident_pages--;
    }

    *link = v->next;
    kfree(v);
}

/* Runs in interrupt context, from the page fault handler. Allocating is fine
 * here today because nothing takes a lock yet; once there's a scheduler this
 * needs revisiting, because a fault handler that blocks on an allocator some
 * thread already holds is a deadlock. */
int vma_handle_fault(uint32_t addr) {
    struct vma* v = find(addr);
    if (!v) return 0;

    uint32_t page = addr & ~(PAGE_SIZE - 1);

    /* Already mapped means this is a permission problem, not a missing page,
     * and we have nothing to say about it yet. */
    if (vmm_is_mapped(page)) return 0;

    uint32_t frame = pmm_alloc_frame();
    if (frame == PMM_NO_FRAME) return 0;

    uint32_t flags = 0;
    if (v->flags & VMA_WRITE) flags |= PAGE_WRITE;
    if (v->flags & VMA_USER)  flags |= PAGE_USER;

    vmm_map(page, frame, flags);

    /* Anonymous memory has to read back as zero, or a process gets whatever
     * the last owner of that frame left behind. */
    volatile uint32_t* p = (volatile uint32_t*)page;
    for (uint32_t i = 0; i < PAGE_SIZE / 4; ++i) p[i] = 0;

    resident_pages++;
    return 1;
}

uint32_t vma_resident_pages(void) { return resident_pages; }

uint32_t vma_region_count(void) {
    uint32_t n = 0;
    for (struct vma* v = regions; v; v = v->next) ++n;
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

    /* The four data frames come back. The page tables built to hold their
     * mappings don't, and that's deliberate: the four pages are spread across
     * four different 4 MB regions, so each needed its own table. Freeing an
     * empty page table means scanning all 1024 entries on every unmap, and
     * they get reused the moment anything maps into the same 4 MB again.
     * Accounted for here rather than quietly tolerated - if this grows past
     * one table per region touched, something really is leaking. */
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
