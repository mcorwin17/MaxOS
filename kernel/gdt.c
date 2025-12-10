#include <stdint.h>
#include <stddef.h>

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

/* Full hardware layout. Only ss0/esp0 and the iomap base get used; the rest
 * exists because the CPU says a TSS is this shape. */
struct tss {
    uint32_t prev, esp0;
    uint16_t ss0, pad0;
    uint32_t esp1;
    uint16_t ss1, pad1;
    uint32_t esp2;
    uint16_t ss2, pad2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, pad3, cs, pad4, ss, pad5, ds, pad6, fs, pad7, gs, pad8;
    uint16_t ldt, pad9;
    uint16_t trap, iomap_base;
} __attribute__((packed));

static struct gdt_entry   gdt[6];
static struct gdt_pointer gdtp;
static struct tss         tss;

static void gdt_set_entry(int n, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    gdt[n].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[n].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[n].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[n].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[n].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));

    gdt[n].access      = access;
}

void gdt_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

void gdt_initialize(void) {
    gdt_set_entry(0, 0, 0, 0, 0);                    /* null */
    gdt_set_entry(1, 0, 0xFFFFF, 0x9A, 0xC0);        /* kernel code */
    gdt_set_entry(2, 0, 0xFFFFF, 0x92, 0xC0);        /* kernel data */
    gdt_set_entry(3, 0, 0xFFFFF, 0xFA, 0xC0);        /* user code, DPL 3 */
    gdt_set_entry(4, 0, 0xFFFFF, 0xF2, 0xC0);        /* user data, DPL 3 */

    /* TSS: byte granularity, base is the struct's address. iomap_base past
     * the limit means no I/O bitmap, so every port access from ring 3 takes
     * a #GP - which is what should happen. */
    for (size_t i = 0; i < sizeof(tss); ++i) ((uint8_t*)&tss)[i] = 0;
    tss.ss0        = KERNEL_DATA_SELECTOR;
    tss.esp0       = 0;                              /* set per switch */
    tss.iomap_base = sizeof(tss);

    gdt_set_entry(5, (uint32_t)(uintptr_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    gdtp.limit = (uint16_t)(sizeof(gdt) - 1);        /* limit is size-1 */
    gdtp.base  = (uint32_t)(uintptr_t)&gdt;

    /* Loading the table isn't enough - CS keeps its cached descriptor until
     * a far jump reloads it. */
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
        "mov %3, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "r"(&gdtp), "i"(KERNEL_DATA_SELECTOR), "i"(KERNEL_CODE_SELECTOR),
          "i"(TSS_SELECTOR)
        : "eax", "memory");
}
