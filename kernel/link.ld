/* Flat binary at 0x1000 - has to match KERNEL_OFFSET in boot.asm, since the
 * bootloader just jumps there. No ELF headers, nothing parses them. */

ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x1000;

    /* _start first: whatever lands at 0x1000 is the entry point */
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

    .bss : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    kernel_end = .;     /* for the physical allocator later */
}
