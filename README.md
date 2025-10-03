# MaxOS

A small x86 OS I'm writing from scratch to learn how this stuff actually works.
512-byte BIOS boot sector, loads a freestanding C kernel off a floppy image,
switches to 32-bit protected mode and jumps to it.

It's early. No interrupts, no keyboard, no memory management, no filesystem.
[docs/roadmap.html](docs/roadmap.html) has the order I'm doing things in.

## Status

| | |
|---|---|
| Boot sector (512B, BIOS) | **works**, booted in QEMU |
| Disk load via `int 13h`, with retry | **works** |
| GDT + protected mode switch | **works** |
| Handoff to the kernel at `0x1000` | **works** |
| C kernel, VGA text output | compiles, not booted yet |
| Interrupts | todo |
| Keyboard | todo |
| Memory management | todo |
| Filesystem | todo |

`make boot-test` is what backs the "works" rows. It boots a stub kernel
([tests/boot-test.asm](tests/boot-test.asm)) that reports over COM1 and then
quits through `isa-debug-exit`, so the whole path — boot sector entry, disk
read, GDT, protected mode switch, handoff to `0x1000` — gets checked without a
C toolchain and without anyone watching a screen. A pass looks like:

```
kernel reached 0x1000, boot path works
```

The C kernel row still says "not booted" because that's a separate thing from
the boot path working. Nothing moves to "works" until I've watched it run.

### Known issue

The kernel load asks for 32 sectors in one `int 13h` call starting at CHS
0/0/2, which runs past the 18-sector track boundary. SeaBIOS papers over it,
real BIOSes often won't. Wants a per-track loop or LBA (`AH=42h`) before this
ever goes near a USB stick.

## Building

Needs `nasm`, an `i686-elf` cross toolchain, and `qemu-system-i386`.

```bash
make        # build floppy.img
make qemu   # boot it
make clean
```

The Makefile refuses to build with a host compiler. Host objects link perfectly
happily and are not x86 machine code.

## Layout

```
bootloader/
  boot.asm       entry, stack, disk load, mode switch
                 (entry code is first in the file on purpose — nasm -f bin
                  emits in order and the BIOS jumps straight to 0x7C00)
  pstring.asm    print_string, 16-bit, BIOS teletype
  ppstring.asm   print_string_pm, 32-bit, straight to VGA memory
  gdt.asm        flat 4GB code/data descriptors
  switchpm.asm   CR0.PE, far jump, into the kernel

kernel/
  kernel.c       VGA text output, cursor, scrolling
  link.ld        flat binary at 0x1000

docs/
  roadmap.html   what to build next and in what order
```

## Memory map

| | |
|---|---|
| Bootloader | `0x7C00` – `0x7DFF` |
| Kernel | `0x1000`, up to 32 sectors |
| Stack (real mode) | `0x9000`, grows down |
| Stack (protected mode) | `0x90000`, grows down |
| VGA text buffer | `0xB8000` |

## Reading

[OSDev Wiki](https://wiki.osdev.org/), and Intel SDM Vol. 3 for the protected
mode chapter. More in [CREDITS.md](CREDITS.md).

## License

MIT, see [LICENSE](LICENSE).
