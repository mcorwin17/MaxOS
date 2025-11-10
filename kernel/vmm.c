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

static uint32_t* page_directory;

static void flush_tlb_entry(uint32_t virt) {
    /* Cheaper than reloading CR3, which throws away every entry. */
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint32_t* page_table_for(uint32_t virt, int create) {
    uint32_t pd_index = PD_INDEX(virt);

    if (!(page_directory[pd_index] & PAGE_PRESENT)) {
        if (!create) return 0;

        uint32_t frame = pmm_alloc_frame();
        if (frame == PMM_NO_FRAME) panic("vmm: out of frames for a page table");

        uint32_t* table = (uint32_t*)(uintptr_t)frame;
        for (int i = 0; i < ENTRIES_PER_TABLE; ++i) table[i] = 0;

        page_directory[pd_index] = frame | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Works because all of physical memory is identity mapped, so a table's
     * physical address is also a usable virtual one. */
    return (uint32_t*)(uintptr_t)FRAME_OF(page_directory[pd_index]);
}

void vmm_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    if ((virt | phys) & (PAGE_SIZE - 1)) {
        panic("vmm_map: unaligned address");
    }

    uint32_t* table = page_table_for(virt, 1);
    table[PT_INDEX(virt)] = FRAME_OF(phys) | (flags & 0xFFF) | PAGE_PRESENT;

    flush_tlb_entry(virt);
}

void vmm_unmap(uint32_t virt) {
    uint32_t* table = page_table_for(virt, 0);
    if (!table) return;

    table[PT_INDEX(virt)] = 0;
    flush_tlb_entry(virt);
}

uint32_t vmm_get_physical(uint32_t virt) {
    uint32_t* table = page_table_for(virt, 0);
    if (!table) return 0;

    uint32_t entry = table[PT_INDEX(virt)];
    if (!(entry & PAGE_PRESENT)) return 0;

    return FRAME_OF(entry) | (virt & (PAGE_SIZE - 1));
}

int vmm_is_mapped(uint32_t virt) {
    uint32_t* table = page_table_for(virt, 0);
    if (!table) return 0;

    return (table[PT_INDEX(virt)] & PAGE_PRESENT) != 0;
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

void vmm_initialize(void) {
    uint32_t pd_frame = pmm_alloc_frame();
    if (pd_frame == PMM_NO_FRAME) panic("vmm: no frame for the page directory");

    page_directory = (uint32_t*)(uintptr_t)pd_frame;
    for (int i = 0; i < ENTRIES_PER_TABLE; ++i) page_directory[i] = 0;

    /* Identity map every byte of physical RAM.
     *
     * Not the long term answer - the kernel wants to live in the higher half
     * and userspace wants the bottom - but it buys one important property
     * right now: any frame the allocator hands back is immediately writable
     * at its own address. Without that, allocating a page table after paging
     * is on can return a frame that isn't mapped, and there's no way to write
     * the table you just allocated. */
    uint32_t ram_bytes = pmm_total_frames() * PAGE_SIZE;
    uint32_t tables = (ram_bytes + TABLE_COVERAGE - 1) / TABLE_COVERAGE;

    for (uint32_t t = 0; t < tables; ++t) {
        uint32_t frame = pmm_alloc_frame();
        if (frame == PMM_NO_FRAME) panic("vmm: out of frames identity mapping");

        uint32_t* table = (uint32_t*)(uintptr_t)frame;
        for (uint32_t i = 0; i < ENTRIES_PER_TABLE; ++i) {
            uint32_t phys = t * TABLE_COVERAGE + i * PAGE_SIZE;
            table[i] = phys | PAGE_PRESENT | PAGE_WRITE;
        }

        page_directory[t] = frame | PAGE_PRESENT | PAGE_WRITE;
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_frame));

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;                      /* PG */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    kprintf("vmm: paging on, %u tables identity mapping %u MB\n",
            tables, ram_bytes / (1024 * 1024));
}
