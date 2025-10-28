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
| C kernel, VGA text output | **works**, reaches `kernel_main` |
| Serial (COM1) output, `kprintf` | **works** |
| IDT + 32 exception handlers | **works**, faults report and halt |
| `panic()` / `KASSERT` + backtrace | **works** |
| PIC remapped, IRQ dispatch | **works** |
| PIT timer, `sleep_ms`, uptime | **works**, 100 Hz |
| PS/2 keyboard | **works**, IRQ1 |
| Serial console input | **works**, IRQ4 |
| Shell | **works**, 8 commands |
| Memory management | todo |
| Filesystem | todo |

`make test` is what backs the "works" rows. Two checks, both over serial so
nothing depends on a human squinting at a QEMU window:

- **`make boot-test`** boots a stub kernel ([tests/boot-test.asm](tests/boot-test.asm))
  and exits through `isa-debug-exit`. Covers the boot path on its own: boot
  sector entry, disk read, GDT, protected mode switch, handoff to `0x1000`.
- **`make kernel-test`** boots the real thing, checks it gets through
  `kernel_main`, and checks the timer counted 50 ticks across a 500 ms sleep:

```
idt: exceptions + IRQs installed, PIC remapped to 0x20
pit: 100Hz on IRQ0
interrupts enabled
timer: 50 ticks over a 500ms sleep, uptime 1010ms
```
- **`make fault-test`** deliberately faults on vectors 0, 6 and 13 and checks
  each one is *reported* rather than triple-faulting. 0 and 6 use the dummy
  error code path, 13 gets a real one from the CPU, so it exercises both stub
  shapes. A general protection fault looks like:

```
=== exception 13: general protection fault ===

*** PANIC ***
general protection fault
vector 13  error 0x00000050
eip 0x000011cd  cs 0x0008  eflags 0x00010016
eax 0x00000050  ebx 0x00001000  ecx 0x00000000  edx 0x000003f8
esi 0x000b8000  edi 0x00000000  ebp 0x0008fff4  ds  0x0010
backtrace:
  [0] 0x00001008
halted.
```

- **`make shell-test`** types at the shell over COM1 and checks it answers.

Serial is deliberately the primary channel, in both directions. VGA text is
nice to look at but can't be read from outside the VM, and a keyboard can't be
typed at by a script — so the keyboard IRQ and serial receive both feed the
same input buffer, and the shell doesn't care which one a keystroke came from.
That's what makes it testable:

```
> help
  help    list commands
  clear   clear the screen
  uptime  milliseconds since boot
  ticks   raw timer tick count
  echo    print the rest of the line
  mem     memory stats (nothing to report yet)
  reboot  reset via the keyboard controller
  panic   deliberately panic, to see the handler
> echo hello from the shell
hello from the shell
> nosuchcommand
unknown command: nosuchcommand
  try 'help'
```

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
