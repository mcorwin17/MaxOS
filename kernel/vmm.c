#include <stdint.h>

#include "vmm.h"
#include "pmm.h"
#include "serial.h"
#include "panic.h"

#define ENTRIES_PER_TABLE  1024
#define TABLE_COVERAGE     (ENTRIES_PER_TABLE * PAGE_SIZE)   /* 4 MB */

#define PD_INDEX(v)   (((v) >> 22) & 0x3FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x3FF)
#define FRAME_OF(e)   ((e) & 0xFFFFF000)

static uint32_t kernel_pd;          /* physical == virtual, identity mapped */
static uint32_t identity_tables;    /* how many PDEs the identity map uses */

/* Page tables and directories are always in identity-mapped RAM with kernel
 * permissions, so a physical address doubles as a pointer no matter whose
 * CR3 is loaded. That one property is what makes editing another process's
 * address space possible at all. */
static inline uint32_t* pd_virt(uint32_t pd) {
    return (uint32_t*)(uintptr_t)pd;
}

static void flush_tlb_entry(uint32_t virt) {
    /* Cheaper than reloading CR3, which throws away every entry. */
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint32_t* page_table_for(uint32_t pd, uint32_t virt, int create) {
    uint32_t* dir = pd_virt(pd);
    uint32_t  pd_index = PD_INDEX(virt);

    if (!(dir[pd_index] & PAGE_PRESENT)) {
        if (!create) return 0;

        uint32_t frame = pmm_alloc_frame();
        if (frame == PMM_NO_FRAME) panic("vmm: out of frames for a page table");

        uint32_t* table = (uint32_t*)(uintptr_t)frame;
        for (int i = 0; i < ENTRIES_PER_TABLE; ++i) table[i] = 0;

        /* USER on the directory entry: the PTE decides the real permission,
         * but both levels must allow user access for ring 3 to get through,
         * and a user PDE with kernel-only PTEs is still kernel-only. */
        dir[pd_index] = frame | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    return (uint32_t*)(uintptr_t)FRAME_OF(dir[pd_index]);
}

void vmm_map_in(uint32_t pd, uint32_t virt, uint32_t phys, uint32_t flags) {
    if ((virt | phys) & (PAGE_SIZE - 1)) {
        panic("vmm_map: unaligned address");
    }

    uint32_t* table = page_table_for(pd, virt, 1);
    table[PT_INDEX(virt)] = FRAME_OF(phys) | (flags & 0xFFF) | PAGE_PRESENT;

    flush_tlb_entry(virt);
}

void vmm_unmap_in(uint32_t pd, uint32_t virt) {
    uint32_t* table = page_table_for(pd, virt, 0);
    if (!table) return;

    table[PT_INDEX(virt)] = 0;
    flush_tlb_entry(virt);
}

uint32_t vmm_get_physical_in(uint32_t pd, uint32_t virt) {
    uint32_t* table = page_table_for(pd, virt, 0);
    if (!table) return 0;

    uint32_t entry = table[PT_INDEX(virt)];
    if (!(entry & PAGE_PRESENT)) return 0;

    return FRAME_OF(entry) | (virt & (PAGE_SIZE - 1));
}

int vmm_is_mapped_in(uint32_t pd, uint32_t virt) {
    uint32_t* table = page_table_for(pd, virt, 0);
    if (!table) return 0;

    return (table[PT_INDEX(virt)] & PAGE_PRESENT) != 0;
}

void vmm_set_flags_in(uint32_t pd, uint32_t virt, uint32_t flags) {
    uint32_t* table = page_table_for(pd, virt, 0);
    if (!table || !(table[PT_INDEX(virt)] & PAGE_PRESENT)) {
        panic("vmm_set_flags: nothing mapped there");
    }

    uint32_t entry = table[PT_INDEX(virt)];
    table[PT_INDEX(virt)] = FRAME_OF(entry) | (flags & 0xFFF) | PAGE_PRESENT;

    flush_tlb_entry(virt);
}

/* Kernel-directory conveniences. */
void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    vmm_map_in(kernel_pd, virt, phys, flags);
}
void vmm_unmap(uint32_t virt)            { vmm_unmap_in(kernel_pd, virt); }
uint32_t vmm_get_physical(uint32_t virt) { return vmm_get_physical_in(kernel_pd, virt); }
int vmm_is_mapped(uint32_t virt)         { return vmm_is_mapped_in(kernel_pd, virt); }

uint32_t vmm_kernel_directory(void)      { return kernel_pd; }

/* Which PDEs belong to the kernel and get shared into every process:
 * the identity map at the bottom and the heap's single 4 MB slot. */
static int is_kernel_pde(uint32_t index) {
    if (index < identity_tables) return 1;
    if (index == PD_INDEX(0xD0000000)) return 1;    /* HEAP_BASE */
    return 0;
}

uint32_t vmm_create_directory(void) {
    uint32_t pd = pmm_alloc_frame();
    if (pd == PMM_NO_FRAME) return 0;

    uint32_t* dir = pd_virt(pd);
    uint32_t* kdir = pd_virt(kernel_pd);

    for (int i = 0; i < ENTRIES_PER_TABLE; ++i) {
        dir[i] = is_kernel_pde((uint32_t)i) ? kdir[i] : 0;
    }

    return pd;
}

void vmm_destroy_directory(uint32_t pd) {
    if (pd == kernel_pd) panic("vmm: refusing to destroy the kernel directory");

    uint32_t* dir = pd_virt(pd);

    for (uint32_t i = 0; i < ENTRIES_PER_TABLE; ++i) {
        if (!(dir[i] & PAGE_PRESENT)) continue;
        if (is_kernel_pde(i)) continue;             /* shared, not ours */

        pmm_free_frame(FRAME_OF(dir[i]));
    }

    pmm_free_frame(pd);
}

void vmm_initialize(void) {
    kernel_pd = pmm_alloc_frame();
    if (kernel_pd == PMM_NO_FRAME) panic("vmm: no frame for the page directory");

    uint32_t* dir = pd_virt(kernel_pd);
    for (int i = 0; i < ENTRIES_PER_TABLE; ++i) dir[i] = 0;

    /* Identity map every byte of physical RAM, kernel-only.
     *
     * Not the long term answer - the kernel wants the higher half - but it
     * buys the property everything above relies on: any frame the allocator
     * hands back is immediately writable at its own address, from any
     * address space. Without that, allocating a page table can return a
     * frame you have no way to write. */
    uint32_t ram_bytes = pmm_total_frames() * PAGE_SIZE;
    identity_tables = (ram_bytes + TABLE_COVERAGE - 1) / TABLE_COVERAGE;

    for (uint32_t t = 0; t < identity_tables; ++t) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == PMM_NO_FRAME) panic("vmm: out of frames identity mapping");

        uint32_t* table = (uint32_t*)(uintptr_t)frame;
        for (uint32_t i = 0; i < ENTRIES_PER_TABLE; ++i) {
            uint32_t phys = t * TABLE_COVERAGE + i * PAGE_SIZE;
            table[i] = phys | PAGE_PRESENT | PAGE_WRITE;
        }

        dir[t] = frame | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Pre-create the heap's page table so the PDE exists before any process
     * directory copies the kernel entries. The heap grows within one 4 MB
     * slot, so this single shared table covers it forever; if it were
     * created later, processes forked earlier would be missing it. */
    (void)page_table_for(kernel_pd, 0xD0000000, 1);

    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_pd));

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;                      /* PG */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    kprintf("vmm: paging on, %u tables identity mapping %u MB\n",
            identity_tables, ram_bytes / (1024 * 1024));
}


/* 1 GB: well past the RAM we identity mapped, so nothing else is there. */
#define TEST_VIRT   0x40000000

void vmm_selftest(void) {
    uint32_t baseline = pmm_free_frames();

    if (vmm_is_mapped(TEST_VIRT)) {
        panic("vmm selftest: test address was already mapped");
    }

    uint32_t frame = pmm_alloc_frame();
    if (frame == PMM_NO_FRAME) panic("vmm selftest: no frame");

    vmm_map(TEST_VIRT, frame, PAGE_WRITE);

    if (vmm_get_physical(TEST_VIRT) != frame) {
        panic("vmm selftest: translation doesn't match what we mapped");
    }

    volatile uint32_t* p = (volatile uint32_t*)TEST_VIRT;
    p[0] = 0xC0FFEE;
    p[1] = 0xDEADBEEF;

    /* Read it back through the identity mapping instead of the new one - if
     * those disagree the translation is wrong even though the write appeared
     * to work. */
    volatile uint32_t* direct = (volatile uint32_t*)(uintptr_t)frame;
    if (direct[0] != 0xC0FFEE || direct[1] != 0xDEADBEEF) {
        panic("vmm selftest: write went somewhere else");
    }

    vmm_unmap(TEST_VIRT);
    if (vmm_is_mapped(TEST_VIRT)) {
        panic("vmm selftest: still mapped after unmap");
    }

    pmm_free_frame(frame);

    /* Page tables allocated along the way are kept on purpose, so allow for
     * one. Anything beyond that is a leak. */
    uint32_t now = pmm_free_frames();
    if (now != baseline && now != baseline - 1) {
        panic("vmm selftest: frames leaked");
    }

    kprintf("vmm: selftest ok, mapped and unmapped at 0x%08x\n", TEST_VIRT);
}
