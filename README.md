# MaxOS

A small x86 OS I'm writing from scratch to learn how this stuff actually works.
512-byte BIOS boot sector, loads a freestanding C kernel off a floppy image,
switches to 32-bit protected mode and jumps to it.

It's early. No interrupts, no keyboard, no memory management, no filesystem.
[docs/roadmap.html](docs/roadmap.html) has the order I'm doing things in.

## Status

| | |
|---|---|
| Boot sector (512B, BIOS) | written, **not booted yet** |
| Disk load via `int 13h`, with retry | written, not booted yet |
| GDT + protected mode switch | written, not booted yet |
| C kernel, VGA text output | written, not booted yet |
| Interrupts | todo |
| Keyboard | todo |
| Memory management | todo |
| Filesystem | todo |

"Not booted yet" is doing real work in that table. I don't have the cross
toolchain installed on this machine right now, so none of the current code has
been assembled or run. I'm not claiming any of it works until I've watched it
work — the last version of this repo claimed a lot more than it did.

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
