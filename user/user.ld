/* User programs: flat binaries at USER_CODE_BASE. crt0.o is first on the
 * link line, so its .text is the entry byte. */

ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS
{
    . = 0x08048000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : { *(.rodata) *(.rodata.*) }
    .data   : { *(.data) *(.data.*) }
    .bss    : { *(COMMON) *(.bss) *(.bss.*) }
}
