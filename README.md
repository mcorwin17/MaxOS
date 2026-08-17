# MaxOS

*September 2025 to April 2026*

A small x86 operating system written from scratch: its own boot sector, a
32-bit kernel with paging and preemptive multitasking, ring-3 processes with
fork/exec/wait and copy-on-write, signals, a FAT16 filesystem it can read,
write and format, and a userspace it boots into — a shell and coreutils
compiled against a homemade libc, loaded off the disk by the kernel's own
filesystem code. Then the depth tracks: four CPUs, a TCP/IP stack that talks
to foreign hosts, a framebuffer, and audio.

```
kernel_main: init is /BIN/SH.BIN
sh: ready
$ cat /HELLO.TXT | wc
1 lines, 5 words, 31 bytes
$ echo one two three > /TMP.TXT
$ cat /TMP.TXT
one two three
```

[docs/roadmap.html](docs/roadmap.html) has the build order and the audit this
grew out of.

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
| E820 memory map | **works** |
| Physical frame allocator | **works**, bitmap, self-tested |
| Paging, `vmm_map`/`unmap` | **works**, identity mapped |
| Kernel heap, `kmalloc`/`kfree` | **works**, canaries + coalescing |
| Demand paging (VMAs) | **works**, zero-fill on fault |
| Threads, preemptive scheduler | **works**, round robin at 100 Hz |
| Spinlocks, blocking sleep | **works** |
| Ring 3, `int 0x80` syscalls | **works**, 8 calls |
| User pointer validation | **works**, kernel pointers bounce |
| Fault isolation | **works**, user crash kills the process, not the kernel |
| Processes: fork, exec, wait, exit | **works** |
| Copy-on-write fork | **works**, verified from userspace |
| Signals, Ctrl-C, `sigreturn` | **works**, handlers resume |
| ATA PIO disk + block cache | **works**, host-verified writes |
| VFS, two backends | **works**, ramfs + FAT16 |
| FAT16 read, subdirs, MBR partitions | **works**, vs qemu's vvfat |
| `open`/`read`/`close` fd syscalls | **works**, from ring 3 |
| Pipes, `dup2`, SIGPIPE | **works** |
| argv, C userspace (crt0 + mini libc) | **works** |
| Programs loaded from the filesystem | **works** |
| `cat file \| wc` between disk-loaded processes | **works** |
| FAT16 write: create, grow, truncate, unlink | **works**, host-verified + survives reboot |
| `mkfs` (kernel formats its own disk) | **works** |
| Coreutils: cat, wc, echo, ls, rm | **works**, loaded from disk |
| Userspace shell: pipelines, `>` redirection | **works** |
| Boot to userspace (`/BIN/SH.BIN` as init) | **works** |
| SMP: ACPI/MADT, LAPIC, AP trampoline | **works**, 4 CPUs |
| Per-CPU scheduling, reschedule IPI | **works** |
| PCI enumeration | **works** |
| NE2000 driver, ARP, IPv4, ICMP | **works**, pings and is pinged |
| TCP: handshake, retransmit, ordered receive | **works**, talks to a foreign stack |
| VBE framebuffer, 1024x768x32 | **works**, pixel-verified |
| Font rendering, graphical console | **works**, 8x16 glyphs |
| AC97 audio, bus-master DMA | **works**, waveform-verified |
| PC speaker, PIT channel 2 | **works**, waveform-verified |

SMP is the depth track. The MADT names the CPUs, INIT-SIPI-SIPI wakes them
through a real-mode trampoline copied to `0x8000`, and each AP walks itself
up to protected mode, paging, and C — then becomes its own idle thread and
starts pulling work off the shared run queue:

```
smp: 4 CPUs in the MADT, lapic at 0xfee00000, BSP is lapic 0
smp: CPU 1 (lapic 1) online
smp: CPU 2 (lapic 2) online
smp: CPU 3 (lapic 3) online
smp:   cpu 0 ran 400 iterations, 62 switches
smp:   cpu 1 ran 800 iterations, 3 switches
smp:   cpu 2 ran 400 iterations, 2 switches
smp:   cpu 3 ran 800 iterations, 3 switches
smp: selftest ok, 6 x 400 = 2400 across 4 CPUs
```

The PIC only ever interrupts the boot CPU, so the PIT tick broadcasts a
reschedule IPI — that's what preemption means on an AP. Kernel threads roam
every CPU; user threads are pinned to CPU 0, because there's one TSS with
one `esp0` and ring-3 transitions have to land on the right kernel stack.
Per-CPU TSSes would lift that.

## Graphics

A 1024x768x32 linear framebuffer, drawing primitives, and a text console on
top of it. Mode setting goes through the Bochs VBE dispatch interface (ports
`0x1CE`/`0x1CF`) rather than the VESA BIOS — the BIOS route means dropping
back to real mode or carrying a v86 monitor, while these ports work from
protected mode with two `out` instructions. The cost is honest: it's a
qemu/bochs interface, and real hardware wants the BIOS call, which is a
bootloader job for the day this meets real silicon. The framebuffer itself
is the std VGA card's PCI BAR0, found by the same enumeration the NIC uses.

The font isn't hand-typed hex. A build script rasterizes a real monospace
typeface into an 8x16 1bpp table, so the source of truth is a typeface
rather than my memory of one — and a broken glyph is visible immediately
because the demo prints all 95 printable characters.

Verified the way the network was: **the host reads qemu's own render.**
`gfx-test` screendumps to a PPM and checks exact pixel values at known
coordinates — six swatch centres, three points in a gradient, and the
background — plus a lit-pixel count across the glyph band:

```
  swatch 0 red      (  90,130) want c03030 got c03030  OK
  gradient (255,0)  ( 295,220) want ff0040 got ff0040  OK
  gradient (0,127)  (  40,347) want 00fe40 got 00fe40  OK
  ASCII row rendered  1438 lit pixels in the glyph band
GFX VERIFY: PASS
```

The gradient points matter more than the flat swatches: they're what catches
an off-by-one in the scanline pitch, which a solid fill would hide entirely.
The card can hand back a wider virtual width than you asked for, and drawing
against the requested width instead of the real pitch shears the picture
diagonally.

## Sound

An AC97 codec driven by bus-master DMA, and the PC speaker for when a beep is
all you want. AC97 over Sound Blaster 16 on purpose: SB16 is the easier chip,
but it's ISA DMA — a dead-end bus with a 64 KB page limit that teaches nothing
transferable. AC97 is a PCI device found by the same enumeration that finds
the NIC, and it works the way real audio hardware works: you hand the card a
list of buffer descriptors and it walks them itself.

That list is the whole design. 32 descriptors, each naming a physical address
and a sample count; `CIV` is the entry the card is playing, `LVI` is the last
one you've filled, and the card runs until it catches up. Playing a sound is
filling the slot after `LVI` and then moving `LVI`. Nothing is copied on a
timer — the DMA engine reads memory directly while the CPU is elsewhere.

Which is exactly why the first version didn't work. The descriptors point at
*physical* addresses, so the buffers have to be physically contiguous, and I
allocated them the obvious way: call the frame allocator 33 times and check
the addresses came out adjacent. They didn't:

```
ac97: found at pci 0:4, nam 0xc000 nabm 0xc400
ac97: couldn't get contiguous DMA memory
```

The frame allocator hands out the lowest free frame, so consecutive calls
*are* consecutive — right up until something frees a frame in the middle. The
pmm selftest allocates a thousand frames and returns them, vma and vmm do the
same on a smaller scale, and by the time any driver initializes the low end of
the bitmap is full of holes. That's a real allocator gap, not a driver
problem: `pmm_alloc_contiguous(n)` scans for a run of `n` free frames and
claims it atomically. The selftest for it deliberately runs *after* the
allocate-and-free pass, because a fresh bitmap would pass no matter what.

Two smaller traps worth naming. The AC97 volume registers are **attenuation** —
`0x0000` is full volume and `0x8000` is mute, so "no sound at all" and "as loud
as it goes" are one bit apart. And the phase accumulator wanted a 64-bit
divide, which in a freestanding 32-bit build with no libgcc isn't slow, it's a
link error:

```
ld.lld: error: undefined symbol: __udivdi3
```

Splitting it into a whole part and a fractional part keeps both halves inside
32 bits, and the frequency still doesn't drift over the length of a note.

Verified the same way graphics and networking were: **the host measures what
qemu rendered.** qemu's `wav` audiodev writes every sample the DMA engine
consumed to a file, so the pitches below are counted out of the audio itself,
not reported by the kernel. `sound-test` plays an ascending arpeggio and the
recipe counts zero crossings in each note window:

```
  tone 1: want ~440 Hz, measured 441 Hz, peak 19659
  tone 2: want ~554 Hz, measured 550 Hz, peak 19659
  tone 3: want ~659 Hz, measured 658 Hz, peak 19659
  tone 4: want ~880 Hz, measured 883 Hz, peak 19659
  tone 5: want ~880 Hz, measured 883 Hz, peak 19659
sound-test: PASS
```

Four *distinct* pitches in a known order, so a driver that plays *something*
still fails. The peak is a free second check nobody had to design: the demo
asks for 60% volume, and 60% of a full-scale 32767 is 19660.

The speaker gets the same treatment — `pcspk-audiodev` routes PIT channel 2
into the same wav backend, so the square wave is measurable too. `beep-test`
plays a descending set so it can't pass on a stale capture from the run above.
The expected frequencies are computed from the divisor rather than the round
number, because the PIT divides 1193182 by an integer and 1000 Hz isn't one of
the results:

```
  tone 1: asked 1000 Hz  divisor 1193 -> 1000 Hz  measured  990 Hz  OK
  tone 2: asked  750 Hz  divisor 1590 ->  750 Hz  measured  750 Hz  OK
  tone 3: asked  500 Hz  divisor 2386 ->  500 Hz  measured  500 Hz  OK
```

The speaker shares the PIT command register with the scheduler tick. Only the
channel bits in the command byte keep them apart, and getting those wrong
reprograms the system timer instead of making a noise.

Scope: no mixing, no volume control per stream, no audio device file. Refills
are polled — `ac97_play` sleeps until a descriptor frees up rather than taking
the buffer-completion interrupt. That's fine for playing a tone and wrong for
anything that has to keep a stream fed under load; the interrupt is wired in
the control register and unused.

## Networking

PCI enumeration finds the card, an NE2000 driver moves frames, and the stack
does ARP, IPv4 and ICMP — enough to ping and be pinged. NE2000 over virtio-net
on purpose: virtio is the better device and the right answer for throughput,
but it's descriptor rings and feature negotiation before a single byte moves.
The NE2000 is port I/O and a 16 KB on-card buffer, and the whole driver fits
in one readable file, which is what a first NIC should be.

```
> ping
net: echo reply from 10.0.2.2 seq 1
net: echo reply from 10.0.2.2 seq 2
net: echo reply from 10.0.2.2 seq 3
net: echo reply from 10.0.2.2 seq 4
  sent 4, got 4 replies
```

Two ways of checking it, because the guest agreeing with itself proves
nothing:

**The capture.** qemu writes a pcap of the wire — its record, not the
kernel's. A script parses it and **recomputes every checksum independently**,
so a stack whose sender and receiver share one misunderstanding can't pass:

```
[ 2]  74B IPv4 10.0.2.15 -> 10.0.2.2 hdrsum=OK ICMP echo-req seq=1 sum=OK
[ 3]  74B IPv4 10.0.2.2 -> 10.0.2.15 hdrsum=OK ICMP echo-rep seq=1 sum=OK
checksums verified independently: IP 8 ok / 0 bad, ICMP 8 ok / 0 bad
```

**Two kernels on one wire.** The gateway test only ever exercises the
outbound half — we ask, something else answers. So `net2-test` boots two
MaxOS guests onto a shared socket link with no host stack between them and
has one ping the other. The responder has to serve an ARP request and an
echo request with nothing to copy from:

```
guest A:  net: echo reply from 10.9.0.2 seq 2
guest B:  net: echo request from 10.9.0.1, replied
```

Seq 1 is missing on purpose — the first packet is dropped while ARP
resolves, and the next ping is the retry. That's the design, not a flake.

Scope at this layer: static address, no routing table, no fragmentation.

## TCP

The connection state machine, retransmission, and enough of the rest to
fetch a page:

```
> http 8080
  connecting to 10.0.2.2:8080
  connected
HTTP/1.0 200 OK
Content-Length: 2040

MaxOS TCP works. MaxOS TCP works. MaxOS TCP works. ...
  2081 bytes received
```

The point of a connection is that both ends believe the same thing about
what has been delivered. Sequence numbers, the ack that moves `snd_una`,
and the retransmit timer all exist to keep that true while the wire loses,
duplicates and reorders things underneath.

What's deliberately missing: a send window (one segment is outstanding at a
time — stop-and-wait, correct and slow), reassembly of out-of-order
segments (they're dropped and re-acked, so the peer resends from the hole),
congestion control, and window scaling. Each of those is a subject rather
than a missing line, and naming them beats implying they're there.

**Verified against a stack that isn't mine.** The client test connects out
through qemu's user networking, which means the TCP peer is qemu's own
stack, with a real host HTTP server behind it. If our handshake, sequence
numbers or checksums are wrong, that connection simply never forms — no
amount of agreeing with ourselves helps. The host server independently
confirms it received a well-formed request, and the byte count the guest
prints has to match what the server actually wrote:

```
request from the guest (50 bytes):
    GET / HTTP/1.0
    Host: maxos
    Connection: close
server sent 2081 bytes back
  byte count matches the server: 2081   OK
```

**And read off the wire.** `tests/tcpcheck.awk` parses qemu's capture and
recomputes every TCP checksum over the pseudo-header, which is the check
that catches a stack whose sender and receiver share one misunderstanding:

```
[ 1] 10.0.2.15:49152 -> 10.0.2.2:8080  SYN      seq=1989715617 len=0    sum=OK
[ 2] 10.0.2.2:8080  -> 10.0.2.15:49152 SYN|ACK  seq=64001      len=0    sum=OK
[ 3] 10.0.2.15:49152 -> 10.0.2.2:8080  ACK      seq=1989715618 len=0    sum=OK
[ 4] 10.0.2.15:49152 -> 10.0.2.2:8080  ACK|PSH  seq=1989715618 len=50   sum=OK
[ 6] 10.0.2.2:8080  -> 10.0.2.15:49152 ACK      seq=64002      len=1440 sum=OK
[ 7] 10.0.2.2:8080  -> 10.0.2.15:49152 ACK|PSH  seq=65442      len=641  sum=OK
[10] 10.0.2.2:8080  -> 10.0.2.15:49152 ACK|FIN  seq=66083      len=0    sum=OK
[12] 10.0.2.15:49152 -> 10.0.2.2:8080  ACK|FIN  seq=1989715668 len=0    sum=OK

tcp segments: 13
checksums recomputed here: 13 ok / 0 bad
payload bytes: guest->host 50, host->guest 2081
```

Connecting out only exercises the active open, so `serve` does the other
half — `tcp-test` puts two MaxOS kernels on one socket link and has one
accept the other:

```
guest B:  listening on 7000
          accepted
          got: GET / HTTP/1.0
guest A:  connected
          maxos echo: GET / HTTP/1.0
```

Passive open is genuinely different code: LISTEN → SYN_RCVD → ESTABLISHED
is driven by someone else's SYN, and the close arrives as CLOSE_WAIT →
LAST_ACK rather than FIN_WAIT. A client-only stack has never run any of it.

**What SMP actually broke** was never the SMP code — it was three bugs that
had been latent all along and only had a window on one CPU:

- **`.bss` was never zeroed.** A flat binary has no loader to do it. Every
  static C promises starts at zero had been starting as whatever the BIOS
  left in RAM; it went unnoticed because that RAM happened to be zero, until
  an array landed past the end of the loaded image and came up holding
  `0xf000fea5`.
- **A lost wakeup in `wait()`.** It scanned for zombies under the process
  lock, released it, *then* marked itself WAITING. On four CPUs the child
  exits inside that window, sets the parent READY, and the parent clobbers
  it and sleeps forever.
- **`fork_ret` never released the run-queue lock.** The SMP scheduler hands
  the lock across a context switch for the incoming thread to drop — and a
  forked child's first run enters through the interrupt-unwind path, not
  through `schedule()`.

The one the whole roadmap pointed at:

```
> run /BIN/PIPELINE.BIN
1 lines, 5 words, 31 bytes
pipeline: done
  pid 3 exited with 0
```

That's `cat /HELLO.TXT | wc` — two C programs compiled against a ~100-line
libc, sitting as files on the FAT disk, loaded by the kernel's own
filesystem code, connected by `pipe`/`fork`/`dup2`/`exec`, with EOF arriving
because the last writer closed. The counts are right, the waits complete,
and steady state is frame-neutral.

The libc is written, not ported — `crt0.asm`, syscall wrappers, `print`,
`printn`, `strlen`. A few hundred lines understood completely beat a couple
hundred thousand configured.

The filesystem test is the fun one. qemu's vvfat driver synthesizes a real
FAT filesystem from a host directory, so the kernel's FAT16 reader is
verified against an implementation that isn't in this repo: the host writes
files, the guest lists them (names *and* sizes), cats them, byte-sums a
10 KB file spanning multiple clusters, walks a subdirectory, and then a
ring-3 program reads one through `open`/`read`/`close`:

```
> ls /
  BIG.BIN       10000
  HELLO.TXT     31
  SUB           <dir>
> cksum /BIG.BIN
  10000 bytes, sum 1275000      <- matches the host's number exactly
> run readfile
readfile got: hello from the host filesystem
```

The VFS is path-based routing with longest-prefix mounts and a per-fs
vtable; ramfs exists to keep it honest — one backend is just that
filesystem with extra steps. Inodes and a dentry cache earn their
complexity when there are hard links, rename, and cache pressure; none of
those exist here yet.

Ctrl-C is the start of a line discipline: while a foreground process runs,
`0x03` from either input source becomes SIGINT instead of a buffered byte.
Default action is Unix-style death (`killed by signal 2`); a handler
installed with `signal(sig, handler, restorer)` gets the interrupted context
parked, runs, and returns through the restorer's `sigreturn`, which puts the
world back:

```
> run catcher
catcher: ready, interrupt me
catcher: caught SIGINT in the handler
catcher: resumed after sigreturn
  pid 2 exited with 5
```

User faults ride the same rails now — an exception in ring 3 becomes
SIGSEGV, so `crash` dies "killed by signal 11" (exit 139), matching the
convention everything else uses.

The disk is ATA PIO with a write-back block cache in front. `dtest` writes a
pattern through the cache, flushes, and re-reads from the device; the make
target then checks the bytes landed in the image *file* after qemu exits —
the cache can't fake that. (The roadmap said virtio, but that advice serves
the 64-bit cloud path; on a legacy BIOS design ATA PIO is the native choice.)

User programs are hand-written assembly in [user/](user/), assembled flat and
embedded in the kernel image at build time — there's no filesystem to load
them from yet. `run <name>` in the shell spawns one:

```
> run forktest
forktest: before fork, marker=A
child: marker=B
parent: marker=A
parent: child exited 9 as expected
  pid 4 exited with 0
  frame delta 0
```

That's fork and copy-on-write observed from the inside: the marker page is
resident before the fork and shared read-only after it; the child's write
triggers a COW copy, so the parent keeps seeing A. `frame delta 0` means a
full spawn/fork/COW/exit/reap cycle returned every frame it took.

A user crash is the process's problem, not the kernel's:

```
> run crash
crash: touching kernel memory from ring 3
=== user fault: page fault ===
  protection violation, on a read, from user mode
  at 0x00100000, eip 0x08048016
process 2 (crash) killed: exception 14 in user mode
> echo alive
alive
```

Syscalls are `int 0x80` through a DPL-3 trap gate. Not `syscall`/`sysret` —
that's the right answer on x86-64, where it's *the* mechanism, but on 32-bit
AMD's `syscall` barely exists and `sysenter` is a fast path to add later, not
a starting point.

Reserving address space is separate from backing it. A 16 MB region costs one
small allocation; frames only appear when a page is touched, via the page
fault handler consulting the region list. Page tables built to hold those
mappings are kept rather than freed on unmap — freeing one means scanning all
1024 entries on every unmap, and they get reused as soon as anything maps into
the same 4 MB. The selftest accounts for them explicitly rather than tolerating
the drift.

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
- **`make fault-test`** deliberately faults on vectors 0, 6, 13 and 14 and
  checks each one is *reported* rather than triple-faulting. 0 and 6 use the
  dummy error code path, 13 and 14 get a real one from the CPU, so it
  exercises both stub shapes. A page fault looks like:

```
=== exception 14: page fault ===
  page not present, on a write, from kernel mode

*** PANIC ***
page fault
vector 14  error 0x00000002
eip 0x00001bc0  cs 0x0008  eflags 0x00010206
cr2 0x40000000  (faulting address)
backtrace:
  [0] 0x000011c6
  [1] 0x00001008
halted.
```

  A general protection fault looks like:

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
- **`make user-test`** runs ring 3 end to end: user output arrives, a kernel
  pointer handed to `write()` bounces, a wild user read kills the process
  while the kernel keeps answering, and compute-bound user code gets
  preempted by the timer.
- **`make fork-test`** checks fork, COW (the child's write must not show
  through to the parent), `wait` carrying the exit code, `exec`, and a frame
  delta of zero across the whole cycle.
- **`make sig-test`** sends a real `^C` byte over serial: default action
  kills `loop`, `catcher`'s handler catches and resumes via `sigreturn`, and
  a user fault arrives as signal 11.
- **`make disk-test`** writes through the block cache, flushes, verifies on
  the device from inside the guest — then verifies the bytes in the disk
  image file from the host after qemu exits.
- **`make fat-test`** reads a vvfat-synthesized filesystem: names, sizes,
  contents, a multi-cluster byte sum, a subdirectory, the ramfs backend, and
  a ring-3 `open`/`read`/`close` of a host-written file.
- **`make pipe-test`** runs the pipeline twice (the second run must be
  frame-neutral), plus the Ctrl-C-on-blocked-read regression: a killed
  reader must not eat the next keystroke.
- **`make lock-test`** runs four threads incrementing a shared counter, once
  with the spinlock and once deliberately without, and requires the two to
  disagree:

```
thread: selftest ok, 4 threads x 200 increments = 800
thread: NO LOCK, counter is 578, expected 800, lost 222 increments
```

  The unlocked run losing nothing is a *failure*, because it means the race
  window is too narrow to prove the lock does anything. The first version of
  this test passed both ways — all four workers finished inside a single 10 ms
  tick and were never preempted at all. `ps` showing `ticks=0` against every
  worker is what gave it away.

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

### Known issues

The kernel load used to ask for 32 sectors in a single `int 13h` call, which
runs past the 18-sector track boundary — SeaBIOS papers over that, real
BIOSes often won't. It's a per-sector loop with its own LBA→CHS conversion
and a reset-and-retry now, so it crosses tracks and heads properly. Still
untested on real hardware, which is the only test that counts for a boot
sector.

User threads are pinned to CPU 0. There's one TSS with one `esp0`, so every
ring-3 transition has to land on the same kernel stack; per-CPU TSSes would
lift it. Kernel threads already roam freely.

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
  kernel.c       entry, .bss zeroing, init order, kernel_main
  idt.c isr.asm  exceptions and IRQs
  pmm.c vmm.c    frames, paging, and the contiguous run allocator
  heap.c vma.c   kmalloc, and the regions demand paging faults against
  thread.c       scheduler, spinlocks, switch.asm
  process.c      fork, exec, wait, copy-on-write, usermode.asm
  syscall.c      int 0x80, and the pointer validation behind it
  signal.c       delivery on the way back to ring 3, sigreturn
  ata.c bcache.c disk, write-back cache
  vfs.c fat16.c ramfs.c   path routing and two backends
  pipe.c         pipe/dup2, SIGPIPE
  smp.c          MADT, LAPIC, trampoline.asm, per-CPU run queues
  pci.c ne2000.c network card
  net.c tcp.c    ARP/IPv4/ICMP, and TCP on top of it
  fb.c           VBE framebuffer and the graphical console
  ac97.c speaker.c   bus-master audio, and the PC speaker
  link.ld        flat binary at 0x10000

tests/
  boot-test.asm  a stub kernel that reports over serial
  tcpcheck.awk   reads a pcap and recomputes every TCP checksum

docs/
  roadmap.html   what to build next and in what order
```

## Memory map

| | |
|---|---|
| E820 map from the BIOS | `0x500` – `0x804` |
| Bootloader | `0x7C00` – `0x7DFF` |
| AP trampoline (copied there at runtime) | `0x8000` |
| Stack (real mode) | `0x9000`, grows down |
| Kernel | `0x10000`, up to 288 sectors (144 KB) |
| Stack (protected mode) | `0x90000`, grows down |
| VGA text buffer | `0xB8000` |
| PMM bitmap, then the frame refcounts | `0x100000` |

The kernel moved from `0x1000` to `0x10000` early on: at `0x1000` it had
24 KB before it reached the boot sector at `0x7C00`, which it is still
executing out of during the load.

The bitmap sits at 1 MB rather than straight after the kernel, which is the
tidier-looking option and a trap: `kernel_end` is around `0x7000`, so a few KB
of bitmap runs directly over the boot sector at `0x7C00` — and the GDT lives
in there. The CPU re-reads the GDT on every segment register load, so the
first interrupt after `sti` faults on a descriptor table made of bitmap.

The kernel installs its own GDT at startup for the same reason: the
bootloader's is in memory the kernel is entitled to reuse.

## Reading

[OSDev Wiki](https://wiki.osdev.org/), and Intel SDM Vol. 3 for the protected
mode chapter. More in [CREDITS.md](CREDITS.md).

## License

MIT, see [LICENSE](LICENSE).
