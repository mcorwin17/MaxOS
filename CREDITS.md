# Credits

What I actually used while writing this.

- **Intel 64 and IA-32 Software Developer's Manual, Vol. 3** — GDT descriptor
  format, `CR0.PE`, and why you need a far jump to enter protected mode.
- **BIOS interface** — `int 10h` teletype (AH=0x0E) and video mode set,
  `int 13h` sector read (AH=0x02) and controller reset (AH=0x00).
- **VGA spec** — text buffer at `0xB8000`, 80x25 cells of two bytes each, and
  the colour attribute layout.
- **[OSDev Wiki](https://wiki.osdev.org/)** — general structure of the boot
  sequence and the protected mode switch.
- **NASM manual**, the `bin` output format section — section ordering, which
  decides what actually ends up at offset 0.
