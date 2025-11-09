#include <stdint.h>

#include "gdt.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry   gdt[3];
static struct gdt_pointer gdtp;

static void gdt_set_entry(int n, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    gdt[n].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[n].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[n].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[n].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[n].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));

    gdt[n].access      = access;
}

void gdt_initialize(void) {
    gdt_set_entry(0, 0, 0, 0, 0);                    /* null */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC0);        /* code, ring 0 */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);        /* data, ring 0 */

    gdtp.limit = (uint16_t)(sizeof(gdt) - 1);        /* limit is size-1 */
    gdtp.base  = (uint32_t)(uintptr_t)&gdt;

    /* Loading the table isn't enough - CS keeps whatever descriptor it
     * cached until a far jump reloads it, so the ljmp is what actually
     * moves us onto the new table. */
    __asm__ volatile(
        "lgdt (%0)\n\t"
        "mov %1, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp %2, $1f\n\t"
        "1:\n\t"
        :
        : "r"(&gdtp), "i"(KERNEL_DATA_SELECTOR), "i"(KERNEL_CODE_SELECTOR)
        : "eax", "memory");
}
