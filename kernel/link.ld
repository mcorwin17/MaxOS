/* Flat binary at 0x10000 - has to match KERNEL_OFFSET in boot.asm, since the
 * bootloader just jumps there. No ELF headers, nothing parses them. */

ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x10000;

    /* _start first: whatever lands at 0x10000 is the entry point */
    .text : {
        *(.text._start)
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    /* _start zeroes this range itself. A flat binary has no loader to do it,
     * and .bss mostly isn't in the file - so without the loop below, every
     * static C promises starts at zero actually starts as whatever the BIOS
     * left in that RAM. */
    bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    bss_end = .;

    kernel_end = .;     /* for the physical allocator later */
}
