/* Kernel GDT.
 *
 * The bootloader built one to get us into protected mode, but it lives inside
 * the boot sector at 0x7C00, which the kernel is perfectly entitled to reuse
 * for something else. The CPU re-reads the GDT on every segment register load,
 * so a table sitting in reusable memory is a fault waiting for whoever
 * allocates there first. Build our own and stop depending on it.
 *
 * Same selectors as the bootloader's, so nothing else has to change:
 * code 0x08, data 0x10. */

#ifndef GDT_H
#define GDT_H

#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10

void gdt_initialize(void);

#endif
