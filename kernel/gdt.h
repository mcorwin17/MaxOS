/* Kernel GDT and TSS.
 *
 * The bootloader built a GDT to get us into protected mode, but it lives
 * inside the boot sector at 0x7C00, which the kernel is entitled to reuse.
 * The CPU re-reads the GDT on every segment register load, so a table in
 * reusable memory is a fault waiting for whoever allocates there first.
 *
 * Six entries now that ring 3 exists: null, kernel code/data, user code/data,
 * and the TSS. The TSS matters for exactly one thing here: esp0/ss0, the
 * stack the CPU switches to when an interrupt arrives from ring 3. */

#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10

/* RPL 3 baked in: these are only ever loaded from ring 3. */
#define USER_CODE_SELECTOR   (0x18 | 3)
#define USER_DATA_SELECTOR   (0x20 | 3)

#define TSS_SELECTOR         0x28

void gdt_initialize(void);

/* Where interrupts from ring 3 land. Has to be the current thread's kernel
 * stack, so the scheduler calls this on every switch. */
void gdt_set_kernel_stack(uint32_t esp0);

#endif
