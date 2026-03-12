# MaxOS
#
# Wants an i686-elf cross toolchain. If you don't have one, clang works fine -
# it cross compiles out of the box, you just have to name the target.

TARGET  := i686-elf
ASM     := nasm
QEMU    := qemu-system-i386

# Prefer a real cross gcc, otherwise clang with an explicit target.
ifneq ($(shell command -v $(TARGET)-gcc 2>/dev/null),)
  CC        := $(TARGET)-gcc
  CCTARGET  :=
  LD        := $(TARGET)-ld
else ifneq ($(shell command -v clang 2>/dev/null),)
  CC        := clang
  CCTARGET  := --target=$(TARGET)
  LD        := ld.lld
else
  $(error No cross compiler. Install $(TARGET)-gcc, or clang + lld)
endif

# Never let a host-targeted build through. That's how an arm64 Mach-O ended up
# in the disk image being jumped to as x86.
ifeq ($(CC),clang)
  ifeq ($(CCTARGET),)
    $(error clang without --target would build for the host)
  endif
endif

# -ffunction-sections so _start goes in .text._start and link.ld can put it
# first; the bootloader jumps to 0x10000 with no idea what's there.
# -fno-omit-frame-pointer so panic() can walk the ebp chain for a backtrace.
#
# The -mno-* flags are not optional. At -O2 the compiler will happily vectorise
# a string loop into movsd/movaps, and SSE raises #UD until CR4.OSFXSR is set.
# That fault arrives before the IDT exists, so it triple faults and the machine
# silently resets. Cost me an afternoon. Turn SSE back on only alongside
# OSFXSR/OSXMMEXCPT and saving FPU state on context switch.
CFLAGS  := $(CCTARGET) -m32 -ffreestanding -nostdlib -fno-pic \
           -ffunction-sections -fno-omit-frame-pointer \
           -mno-mmx -mno-sse -mno-sse2 -mno-80387 \
           -fno-stack-protector -Wall -Wextra -O2 -Ibuild $(CFLAGS_EXTRA)

SECTORS := 192         # keep in sync with KERNEL_SECTOR_COUNT in boot.asm

CSRCS   := kernel serial panic idt pic pit console kbd shell pmm gdt vmm \
           heap vma thread process syscall signal ata bcache vfs ramfs \
           fat16 pipe smp pci ne2000 net
ASRCS   := isr switch usermode

# The AP trampoline is assembled flat (it starts in real mode at 0x8000) and
# wrapped as a byte array the kernel copies there at runtime.
build/trampblob.o: kernel/trampoline.asm | build
	$(ASM) -f bin $< -o build/trampoline.bin
	@{ echo 'BITS 32'; echo 'section .rodata'; \
	   echo 'global tramp_blob_start'; echo 'global tramp_blob_end'; \
	   echo 'tramp_blob_start:'; \
	   od -An -v -t x1 build/trampoline.bin | \
	     sed -e 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g' -e 's/^/  db /' -e 's/,$$//'; \
	   echo 'tramp_blob_end:'; } > build/trampblob.asm
	$(ASM) -f elf32 build/trampblob.asm -o $@

# kernel.o first so _start lands at the front even before link.ld sorts it
OBJS    := $(patsubst %,build/%.o,$(CSRCS)) $(patsubst %,build/%.o,$(ASRCS)) \
           build/trampblob.o

UPROGS  := $(wildcard user/*.asm)

.PHONY: all clean qemu boot-test kernel-test test help
.DEFAULT_GOAL := all

all: floppy.img

bin build:
	mkdir -p $@

# boot.asm %includes the rest, so rebuild on any of them
bin/boot.bin: bootloader/boot.asm $(wildcard bootloader/*.asm) | bin
	$(ASM) -f bin $< -o $@

# User programs get assembled flat and baked into a generated header the
# kernel embeds, because there's no filesystem to load them from yet.
build/user_blobs.h: $(UPROGS) | build
	@{ echo '/* generated from user/ by the build - do not edit */'; \
	   echo 'struct user_blob { const char* name; const unsigned char* bytes; unsigned int len; };'; \
	   for f in $(UPROGS); do \
	     n=$$(basename $$f .asm); \
	     $(ASM) -f bin $$f -o build/$$n.ubin || exit 1; \
	     echo "static const unsigned char blob_$$n[] = {"; \
	     od -An -v -t x1 build/$$n.ubin | \
	       sed -e 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'; \
	     echo "};"; \
	   done; \
	   echo 'static const struct user_blob user_blobs[] = {'; \
	   for f in $(UPROGS); do \
	     n=$$(basename $$f .asm); \
	     echo "  { \"$$n\", blob_$$n, sizeof(blob_$$n) },"; \
	   done; \
	   echo '};'; \
	   echo 'static const unsigned int user_blob_count = sizeof(user_blobs) / sizeof(user_blobs[0]);'; \
	} > $@

build/process.o: build/user_blobs.h

# C userspace: crt0 + a small libc, linked flat at the user base. These are
# NOT embedded in the kernel - the tests put them on the FAT disk and the
# kernel loads them through its own filesystem.
UCFLAGS := $(CCTARGET) -m32 -ffreestanding -nostdlib -fno-pic \
           -ffunction-sections -mno-mmx -mno-sse -mno-sse2 -mno-80387 \
           -fno-stack-protector -Wall -Wextra -O2 -Iuser/lib

UPROGS_C := cat wc pipeline writefile echo ls rm sh

userprogs: $(patsubst %,build/%.ubin,$(shell echo $(UPROGS_C) | tr a-z A-Z))

build/ucrt0.o: user/lib/crt0.asm | build
	$(ASM) -f elf32 $< -o $@

build/ulib.o: user/lib/ulib.c user/lib/ulib.h | build
	$(CC) $(UCFLAGS) -c $< -o $@

define UPROG_RULE
build/$(shell echo $(1) | tr a-z A-Z).ubin: user/prog/$(1).c build/ucrt0.o build/ulib.o user/user.ld
	$$(CC) $$(UCFLAGS) -c user/prog/$(1).c -o build/u_$(1).o
	$$(LD) -m elf_i386 -T user/user.ld -o $$@ build/ucrt0.o build/u_$(1).o build/ulib.o
endef
$(foreach p,$(UPROGS_C),$(eval $(call UPROG_RULE,$(p))))

all: userprogs

build/%.o: kernel/%.c $(wildcard kernel/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: kernel/%.asm | build
	$(ASM) -f elf32 $< -o $@

bin/kernel.bin: $(OBJS) kernel/link.ld | bin
	$(LD) -m elf_i386 -T kernel/link.ld -o $@ $(OBJS)

# $1 = kernel binary, $2 = output image
define make_image
	@test $$(stat -c%s bin/boot.bin) -eq 512 || \
		{ echo "boot.bin is not 512 bytes"; exit 1; }
	@test $$(stat -c%s $(1)) -le $$((512 * $(SECTORS))) || \
		{ echo "$(1) > $(SECTORS) sectors, bump KERNEL_SECTOR_COUNT"; exit 1; }
	dd if=/dev/zero of=$(2) bs=512 count=2880 status=none
	dd if=bin/boot.bin of=$(2) conv=notrunc bs=512 count=1 status=none
	dd if=$(1) of=$(2) conv=notrunc bs=512 seek=1 status=none
endef

floppy.img: bin/boot.bin bin/kernel.bin
	$(call make_image,bin/kernel.bin,floppy.img)

qemu: floppy.img
	$(QEMU) -fda floppy.img -boot a

# Boots a tiny asm kernel that reports over serial, so the whole boot path is
# checkable without a C toolchain and without a human looking at a screen.
bin/boot-test.bin: tests/boot-test.asm | bin
	$(ASM) -f bin $< -o $@

boot-test: bin/boot.bin bin/boot-test.bin
	$(call make_image,bin/boot-test.bin,bin/boot-test.img)
	@rm -f bin/boot-test.log
	@-$(QEMU) -fda bin/boot-test.img -boot a \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		-serial file:bin/boot-test.log \
		-display none -no-reboot
	@grep -q "boot path works" bin/boot-test.log \
		&& echo "boot-test: PASS" \
		|| { echo "boot-test: FAIL"; cat bin/boot-test.log; exit 1; }

# Boots the real kernel and checks it got through kernel_main. It halts rather
# than exiting, so this needs a timeout - that's the point, a hang is a failure.
kernel-test: floppy.img
	@rm -f bin/kernel-test.log
	@-timeout 20 $(QEMU) -fda floppy.img -boot a \
		-display none -no-reboot \
		-serial file:bin/kernel-test.log
	@grep -q "init done" bin/kernel-test.log \
		|| { echo "kernel-test: FAIL (never finished init)"; \
		     cat bin/kernel-test.log 2>/dev/null; exit 1; }
	@# 100Hz over a 500ms sleep is 50 ticks. Allow one either side rather than
	@# demanding exactness from a busy host.
	@grep -qE "timer: (49|50|51) ticks" bin/kernel-test.log \
		|| { echo "kernel-test: FAIL (timer not ticking at the right rate)"; \
		     grep timer: bin/kernel-test.log 2>/dev/null; exit 1; }
	@echo "kernel-test: PASS"

# Deliberately fault and check the handler says so instead of resetting.
# Covers both stub shapes: 0 and 6 push a dummy error code, 13 and 14 get a
# real one from the CPU. If those two paths disagree the frame is misaligned
# and every register in the dump is wrong.
# 14 only became possible once paging was on - before that a wild pointer just
# wrote to whatever physical memory was at that address.
fault-test:
	@for v in 0 6 13 14; do \
		echo "vector $$v:"; \
		$(MAKE) --no-print-directory clean >/dev/null 2>&1; \
		$(MAKE) --no-print-directory CFLAGS_EXTRA=-DTEST_FAULT=$$v floppy.img >/dev/null || exit 1; \
		rm -f bin/fault.log; \
		timeout 20 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
			-serial file:bin/fault.log >/dev/null 2>&1 || true; \
		if grep -q "exception $$v" bin/fault.log; then \
			echo "  PASS"; \
		else \
			echo "  FAIL"; cat bin/fault.log; exit 1; \
		fi; \
	done
	@$(MAKE) --no-print-directory clean >/dev/null 2>&1

# Drives the shell over COM1, since the whole point of feeding serial into the
# same input buffer as the keyboard is that a script can type at it.
shell-test: floppy.img
	@rm -f bin/shell-test.log
	@-{ sleep 5; printf 'help\nuptime\necho shell works\n'; sleep 2; } | \
		timeout 25 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
			-serial stdio -monitor none > bin/shell-test.log 2>&1 || true
	@grep -q "shell works" bin/shell-test.log \
		&& echo "shell-test: PASS" \
		|| { echo "shell-test: FAIL"; cat bin/shell-test.log 2>/dev/null; exit 1; }

# Runs the shared counter test twice: once locked, which has to come out
# exact, and once deliberately unlocked, which has to not. A lock that passes
# both ways isn't holding anything - and the first version of this passed both
# ways, because the workers finished inside one 10ms tick and were never
# preempted at all.
lock-test:
	@$(MAKE) --no-print-directory clean >/dev/null 2>&1
	@$(MAKE) --no-print-directory floppy.img >/dev/null || exit 1
	@rm -f bin/lock.log
	@-timeout 40 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
		-serial file:bin/lock.log >/dev/null 2>&1 || true
	@grep -q "thread: selftest ok" bin/lock.log \
		|| { echo "lock-test: FAIL, locked run lost increments"; \
		     grep "thread:" bin/lock.log 2>/dev/null; exit 1; }
	@$(MAKE) --no-print-directory clean >/dev/null 2>&1
	@$(MAKE) --no-print-directory CFLAGS_EXTRA=-DTEST_NO_LOCK=1 floppy.img >/dev/null || exit 1
	@rm -f bin/nolock.log
	@-timeout 40 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
		-serial file:bin/nolock.log >/dev/null 2>&1 || true
	@grep -q "NO LOCK" bin/nolock.log \
		|| { echo "lock-test: FAIL, unlocked run never reported"; exit 1; }
	@if grep -q "lost 0 increments" bin/nolock.log; then \
		echo "lock-test: FAIL, unlocked run lost nothing - the race window is"; \
		echo "  too narrow to prove the lock does anything"; exit 1; \
	fi
	@grep "NO LOCK" bin/nolock.log
	@echo "lock-test: PASS"
	@$(MAKE) --no-print-directory clean >/dev/null 2>&1

# Ring 3 end to end: a user program runs, its write() syscall lands, a
# kernel pointer handed to write() bounces, a wild user read gets the
# process killed without taking the kernel down, and pure-userspace compute
# gets preempted by the timer.
user-test: floppy.img
	@rm -f bin/user-test.log
	@-{ sleep 5; printf 'run hello\n'; sleep 2; printf 'run crash\n'; sleep 2; \
	    printf 'echo alive\n'; sleep 1; printf 'run spin\n'; sleep 5; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
			-serial stdio -monitor none > bin/user-test.log 2>&1 || true
	@grep -q "hello from ring 3" bin/user-test.log || { echo "user-test: FAIL (no ring 3 output)"; exit 1; }
	@grep -q "bad write rejected" bin/user-test.log || { echo "user-test: FAIL (kernel pointer accepted)"; exit 1; }
	@grep -q "exited with 42" bin/user-test.log     || { echo "user-test: FAIL (exit code lost)"; exit 1; }
	@grep -q "killed by exception" bin/user-test.log || { echo "user-test: FAIL (user fault not contained)"; exit 1; }
	@grep -q "^alive" bin/user-test.log             || { echo "user-test: FAIL (kernel died with the process)"; exit 1; }
	@grep -q "still scheduling" bin/user-test.log   || { echo "user-test: FAIL (ring 3 never preempted)"; exit 1; }
	@echo "user-test: PASS"

# fork + copy-on-write + wait + exec. The marker byte is shared COW between
# parent and child; the child's write must not show through. Frame delta 0
# proves nothing leaks across a full spawn/fork/exit/reap cycle.
fork-test: floppy.img
	@rm -f bin/fork-test.log
	@-{ sleep 5; printf 'run forktest\n'; sleep 3; printf 'run exectest\n'; sleep 3; \
	    printf 'run forktest\n'; sleep 3; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
			-serial stdio -monitor none > bin/fork-test.log 2>&1 || true
	@grep -q "child: marker=B" bin/fork-test.log  || { echo "fork-test: FAIL (child never ran)"; exit 1; }
	@grep -q "parent: marker=A" bin/fork-test.log || { echo "fork-test: FAIL (COW leaked the child's write)"; exit 1; }
	@grep -q "child exited 9 as expected" bin/fork-test.log || { echo "fork-test: FAIL (wait lost the exit code)"; exit 1; }
	@grep -q "exited with 42" bin/fork-test.log   || { echo "fork-test: FAIL (exec didn't become hello)"; exit 1; }
	@grep -q "frame delta 0" bin/fork-test.log    || { echo "fork-test: FAIL (frames leaked)"; exit 1; }
	@echo "fork-test: PASS"

# Ctrl-C both ways: default action kills loop (128+2), a handler in catcher
# gets the full deliver/handle/sigreturn/resume round trip, and a user fault
# reads as signal 11 through the same machinery.
sig-test: floppy.img
	@rm -f bin/sig-test.log
	@-{ sleep 5; printf 'run loop\n'; sleep 2; printf '\003'; sleep 2; \
	    printf 'run catcher\n'; sleep 2; printf '\003'; sleep 2; \
	    printf 'run crash\n'; sleep 2; printf 'echo alive\n'; sleep 1; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a -display none -no-reboot \
			-serial stdio -monitor none > bin/sig-test.log 2>&1 || true
	@grep -q "killed by signal 2" bin/sig-test.log  || { echo "sig-test: FAIL (Ctrl-C default action)"; exit 1; }
	@grep -q "caught SIGINT in the handler" bin/sig-test.log || { echo "sig-test: FAIL (handler never ran)"; exit 1; }
	@grep -q "resumed after sigreturn" bin/sig-test.log || { echo "sig-test: FAIL (sigreturn didn't resume)"; exit 1; }
	@grep -q "killed by signal 11" bin/sig-test.log || { echo "sig-test: FAIL (fault didn't become SIGSEGV)"; exit 1; }
	@grep -q "^alive" bin/sig-test.log              || { echo "sig-test: FAIL (kernel went down)"; exit 1; }
	@echo "sig-test: PASS"

# Whole storage path: identify, write through the cache, flush, verify from
# the device inside the guest - then verify the bytes landed in the image
# file from out here, after qemu is gone. The cache can't fake that.
disk-test: floppy.img
	@dd if=/dev/zero of=bin/d.img bs=1M count=4 status=none
	@rm -f bin/disk-test.log
	@-{ sleep 5; printf 'disk\n'; sleep 1; printf 'dtest\n'; sleep 2; } | \
		timeout 30 $(QEMU) -fda floppy.img -boot a \
			-drive file=bin/d.img,format=raw,if=ide \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/disk-test.log 2>&1 || true
	@grep -q "selftest ok, sector 100" bin/disk-test.log \
		|| { echo "disk-test: FAIL (guest-side verify)"; exit 1; }
	@dd if=bin/d.img bs=512 skip=100 count=1 status=none | head -c 8 | \
		grep -q "MAXOSDSK" \
		|| { echo "disk-test: FAIL (write never reached the image file)"; exit 1; }
	@echo "disk-test: PASS"

# The FAT reader against a filesystem this repo didn't make: qemu's vvfat
# synthesizes real FAT from a host directory. Names, sizes, exact contents,
# a byte-sum over a multi-cluster file, a subdirectory, the ramfs backend
# through the same vtable, and a ring-3 read through the fd syscalls.
fat-test: floppy.img
	@rm -rf bin/fatdir bin/fat-test.log
	@mkdir -p bin/fatdir/SUB
	@printf 'hello from the host filesystem\n' > bin/fatdir/HELLO.TXT
	@awk 'BEGIN{for(i=0;i<10000;i++)printf "%c",((i*31+7)%255)+1}' \
		> bin/fatdir/BIG.BIN
	@printf 'note in a subdirectory\n' > bin/fatdir/SUB/NOTE.TXT
	@-{ sleep 5; printf 'ls /\n'; sleep 1; printf 'cat /HELLO.TXT\n'; sleep 1; \
	    printf 'cat /SUB/NOTE.TXT\n'; sleep 1; printf 'cksum /BIG.BIN\n'; sleep 2; \
	    printf 'cat /ram/hello.txt\n'; sleep 1; printf 'run readfile\n'; sleep 2; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a \
			-drive file=fat:bin/fatdir,if=ide,snapshot=on \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/fat-test.log 2>&1 || true
	@grep -q "HELLO.TXT" bin/fat-test.log || { echo "fat-test: FAIL (ls)"; exit 1; }
	@grep -q "hello from the host filesystem" bin/fat-test.log \
		|| { echo "fat-test: FAIL (file content)"; exit 1; }
	@grep -q "note in a subdirectory" bin/fat-test.log \
		|| { echo "fat-test: FAIL (subdirectory)"; exit 1; }
	@SUM=$$(awk 'BEGIN{s=0;for(i=0;i<10000;i++)s+=((i*31+7)%255)+1;print s}'); \
		grep -q "10000 bytes, sum $$SUM" bin/fat-test.log \
		|| { echo "fat-test: FAIL (multi-cluster read)"; exit 1; }
	@grep -q "ramfs says hi" bin/fat-test.log \
		|| { echo "fat-test: FAIL (second backend)"; exit 1; }
	@grep -q "readfile got: hello" bin/fat-test.log \
		|| { echo "fat-test: FAIL (fd syscalls from ring 3)"; exit 1; }
	@echo "fat-test: PASS"

# The roadmap's headline: cat file | wc between two processes loaded from
# the filesystem, through a pipe. Run twice - the first run sets the heap
# high-water mark, the second must be frame-neutral. Also checks a console-
# blocked read dies cleanly on Ctrl-C without eating the next keystroke.
pipe-test: floppy.img
	@rm -rf bin/fatdir bin/pipe-test.log
	@mkdir -p bin/fatdir/BIN
	@printf 'hello from the host filesystem\n' > bin/fatdir/HELLO.TXT
	@cp build/CAT.ubin bin/fatdir/BIN/CAT.BIN
	@cp build/WC.ubin bin/fatdir/BIN/WC.BIN
	@cp build/PIPELINE.ubin bin/fatdir/BIN/PIPELINE.BIN
	@-{ sleep 5; printf 'run /BIN/CAT.BIN /HELLO.TXT\n'; sleep 2; \
	    printf 'run /BIN/WC.BIN\n'; sleep 2; printf '\003'; sleep 1; \
	    printf 'echo prompt intact\n'; sleep 1; \
	    printf 'run /BIN/PIPELINE.BIN\n'; sleep 4; \
	    printf 'run /BIN/PIPELINE.BIN\n'; sleep 4; } | \
		timeout 60 $(QEMU) -fda floppy.img -boot a \
			-drive file=fat:bin/fatdir,if=ide,snapshot=on \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/pipe-test.log 2>&1 || true
	@grep -q "hello from the host filesystem" bin/pipe-test.log \
		|| { echo "pipe-test: FAIL (cat from disk)"; exit 1; }
	@grep -q "killed by signal 2" bin/pipe-test.log \
		|| { echo "pipe-test: FAIL (console read not interruptible)"; exit 1; }
	@grep -q "prompt intact" bin/pipe-test.log \
		|| { echo "pipe-test: FAIL (Ctrl-C ate a keystroke)"; exit 1; }
	@grep -q "1 lines, 5 words, 31 bytes" bin/pipe-test.log \
		|| { echo "pipe-test: FAIL (the pipeline)"; exit 1; }
	@grep -q "pipeline: done" bin/pipe-test.log \
		|| { echo "pipe-test: FAIL (waits never finished)"; exit 1; }
	@tail -6 bin/pipe-test.log | grep -q "frame delta 0" \
		|| { echo "pipe-test: FAIL (steady state leaks frames)"; exit 1; }
	@echo "pipe-test: PASS"

# FAT writes, proven three ways: the guest writes and re-reads a file whose
# chain spans clusters; the recipe then parses the raw image itself (od, no
# kernel code involved) to find the dirent and check the first bytes; and a
# second boot without mkfs still sees the same checksum - it survived.
write-test: floppy.img
	@dd if=/dev/zero of=bin/d.img bs=1M count=4 status=none
	@rm -f bin/write-test.log bin/write-test2.log
	@-{ sleep 5; printf 'mkfs\n'; sleep 2; printf 'run writefile\n'; sleep 3; \
	    printf 'cksum /OUT.TXT\n'; sleep 2; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a \
			-drive file=bin/d.img,format=raw,if=ide \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/write-test.log 2>&1 || true
	@grep -q "wrote and verified 5034 bytes" bin/write-test.log \
		|| { echo "write-test: FAIL (guest write/verify)"; exit 1; }
	@SUM=$$(grep -o "5034 bytes, sum [0-9]*" bin/write-test.log | head -1); \
	 test -n "$$SUM" || { echo "write-test: FAIL (no cksum)"; exit 1; }; \
	 echo "guest says: $$SUM"
	@# host side: the dirent must be in the root directory of the raw image
	@dd if=bin/d.img bs=512 skip=17 count=1 status=none | head -c 11 | \
		grep -q "OUT     TXT" \
		|| { echo "write-test: FAIL (dirent not in the image)"; exit 1; }
	@dd if=bin/d.img bs=512 skip=49 count=1 status=none | head -c 20 | \
		grep -q "written from ring 3" \
		|| { echo "write-test: FAIL (data not in the image)"; exit 1; }
	@# boot again WITHOUT mkfs: the file has to still be there
	@-{ sleep 5; printf 'cksum /OUT.TXT\n'; sleep 2; } | \
		timeout 30 $(QEMU) -fda floppy.img -boot a \
			-drive file=bin/d.img,format=raw,if=ide \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/write-test2.log 2>&1 || true
	@S1=$$(grep -o "5034 bytes, sum [0-9]*" bin/write-test.log | head -1); \
	 S2=$$(grep -o "5034 bytes, sum [0-9]*" bin/write-test2.log | head -1); \
	 test -n "$$S2" -a "$$S1" = "$$S2" \
		|| { echo "write-test: FAIL (didn't survive the reboot)"; exit 1; }
	@echo "write-test: PASS"

# Boot to a userspace prompt and live there: the kernel finds /BIN/SH.BIN
# and makes it init. Pipelines, redirection into a created file, rm, and
# both Ctrl-C behaviors - ignored at the prompt, fatal to a blocked child.
sh-test: floppy.img
	@rm -rf bin/fatdir bin/sh-test.log
	@mkdir -p bin/fatdir/BIN
	@printf 'hello from the host filesystem\n' > bin/fatdir/HELLO.TXT
	@for b in SH CAT WC LS ECHO RM PIPELINE; do \
		cp build/$$b.ubin bin/fatdir/BIN/$$b.BIN || exit 1; done
	@-{ sleep 6; printf 'ls /BIN\n'; sleep 1; \
	    printf 'cat /HELLO.TXT | wc\n'; sleep 3; \
	    printf 'echo one two three > /TMP.TXT\n'; sleep 1; \
	    printf 'cat /TMP.TXT\n'; sleep 1; \
	    printf 'rm /TMP.TXT\n'; sleep 1; printf 'cat /TMP.TXT\n'; sleep 1; \
	    printf '\003'; sleep 1; printf 'echo survived\n'; sleep 1; \
	    printf 'wc\n'; sleep 1; printf '\003'; sleep 1; \
	    printf 'echo still here\n'; sleep 1; } | \
		timeout 60 $(QEMU) -fda floppy.img -boot a \
			-drive file=fat:bin/fatdir,if=ide,snapshot=on \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/sh-test.log 2>&1 || true
	@grep -q "init is /BIN/SH.BIN" bin/sh-test.log || { echo "sh-test: FAIL (never became init)"; exit 1; }
	@grep -q "sh: ready" bin/sh-test.log            || { echo "sh-test: FAIL (sh never started)"; exit 1; }
	@grep -q "SH.BIN" bin/sh-test.log               || { echo "sh-test: FAIL (user ls)"; exit 1; }
	@grep -q "1 lines, 5 words, 31 bytes" bin/sh-test.log || { echo "sh-test: FAIL (pipeline from the prompt)"; exit 1; }
	@grep -q "^one two three" bin/sh-test.log       || { echo "sh-test: FAIL (redirection)"; exit 1; }
	@grep -q "can.t open /TMP.TXT" bin/sh-test.log  || { echo "sh-test: FAIL (rm)"; exit 1; }
	@grep -q "survived" bin/sh-test.log             || { echo "sh-test: FAIL (sh died to Ctrl-C)"; exit 1; }
	@grep -q "killed by signal 2" bin/sh-test.log   || { echo "sh-test: FAIL (child not killable)"; exit 1; }
	@grep -q "still here" bin/sh-test.log           || { echo "sh-test: FAIL (sh gone after child kill)"; exit 1; }
	@echo "sh-test: PASS"

# Four CPUs: every AP comes online, work lands on more than one of them, a
# locked counter survives genuine parallelism, and the whole userspace stack
# still runs. The last part is the real test - the uniprocessor bugs SMP
# exposed were all in wait/fork paths, not in the SMP code.
smp-test: floppy.img
	@rm -rf bin/fatdir bin/smp-test.log
	@mkdir -p bin/fatdir/BIN
	@printf 'hello from the host filesystem\n' > bin/fatdir/HELLO.TXT
	@for b in SH CAT WC LS ECHO RM; do cp build/$$b.ubin bin/fatdir/BIN/$$b.BIN || exit 1; done
	@-{ sleep 7; printf 'ls /BIN\n'; sleep 2; \
	    printf 'cat /HELLO.TXT | wc\n'; sleep 3; \
	    printf 'echo smp holds up > /T.TXT\n'; sleep 2; printf 'cat /T.TXT\n'; sleep 2; } | \
		timeout 60 $(QEMU) -fda floppy.img -boot a -smp 4 \
			-drive file=fat:bin/fatdir,if=ide,snapshot=on \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/smp-test.log 2>&1 || true
	@grep -q "4 CPUs in the MADT" bin/smp-test.log || { echo "smp-test: FAIL (MADT)"; exit 1; }
	@test $$(grep -c "online" bin/smp-test.log) -eq 3 \
		|| { echo "smp-test: FAIL (not all APs started)"; exit 1; }
	@grep -q "across 4 CPUs" bin/smp-test.log || { echo "smp-test: FAIL (work didn't spread)"; exit 1; }
	@grep -q "selftest ok, 6 x 400 = 2400" bin/smp-test.log \
		|| { echo "smp-test: FAIL (lost increments under parallelism)"; exit 1; }
	@grep -q "sh: ready" bin/smp-test.log || { echo "smp-test: FAIL (no userspace)"; exit 1; }
	@grep -q "1 lines, 5 words, 31 bytes" bin/smp-test.log \
		|| { echo "smp-test: FAIL (pipeline hung on SMP)"; exit 1; }
	@grep -q "^smp holds up" bin/smp-test.log \
		|| { echo "smp-test: FAIL (fs write on SMP)"; exit 1; }
	@echo "smp-test: PASS"

# Ping qemu's gateway and check the replies come back. The capture is the
# real evidence: qemu writes it, not us, so the frames and checksums in it
# were validated by something that isn't this kernel.
net-test: floppy.img
	@rm -f bin/net-test.log bin/net.pcap
	@-{ sleep 6; printf 'net\n'; sleep 1; printf 'ping\n'; sleep 5; printf 'net\n'; sleep 1; } | \
		timeout 40 $(QEMU) -fda floppy.img -boot a \
			-netdev user,id=n0 -device ne2k_pci,netdev=n0 \
			-object filter-dump,id=d0,netdev=n0,file=bin/net.pcap \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/net-test.log 2>&1 || true
	@grep -q "ne2000: found at pci" bin/net-test.log || { echo "net-test: FAIL (no card)"; exit 1; }
	@grep -q "link    up" bin/net-test.log           || { echo "net-test: FAIL (link down)"; exit 1; }
	@grep -q "sent 4, got 4 replies" bin/net-test.log || { echo "net-test: FAIL (no echo replies)"; exit 1; }
	@test -s bin/net.pcap || { echo "net-test: FAIL (nothing on the wire)"; exit 1; }
	@echo "net-test: PASS"

# Two kernels on one virtual wire, no host stack between them. This is the
# only test that exercises the INBOUND paths - guest B has to answer an ARP
# request and an echo request with nothing to copy from.
net2-test: floppy.img
	@rm -f bin/net2-a.log bin/net2-b.log bin/net2.pcap
	@( { sleep 8; printf 'ip 10.9.0.2\n'; sleep 12; printf 'net\n'; sleep 2; } | \
	   timeout 40 $(QEMU) -fda floppy.img -boot a \
		-netdev socket,id=n0,listen=127.0.0.1:14550 \
		-device ne2k_pci,netdev=n0,mac=52:54:00:12:34:57 \
		-object filter-dump,id=d0,netdev=n0,file=bin/net2.pcap \
		-display none -no-reboot -serial stdio -monitor none \
		> bin/net2-b.log 2>&1 || true ) &
	@sleep 3
	@-{ sleep 6; printf 'ip 10.9.0.1\n'; sleep 3; printf 'ping 10.9.0.2\n'; sleep 6; } | \
		timeout 30 $(QEMU) -fda floppy.img -boot a \
			-netdev socket,id=n0,connect=127.0.0.1:14550 \
			-device ne2k_pci,netdev=n0,mac=52:54:00:12:34:56 \
			-display none -no-reboot -serial stdio -monitor none \
			> bin/net2-a.log 2>&1 || true
	@sleep 12
	@grep -q "echo reply from 10.9.0.2" bin/net2-a.log \
		|| { echo "net2-test: FAIL (no reply from the other guest)"; exit 1; }
	@grep -q "echo request from 10.9.0.1, replied" bin/net2-b.log \
		|| { echo "net2-test: FAIL (responder never answered)"; exit 1; }
	@echo "net2-test: PASS"

test: boot-test kernel-test fault-test shell-test user-test fork-test \
      sig-test disk-test fat-test pipe-test write-test sh-test smp-test \
      net-test net2-test

clean:
	rm -rf bin build floppy.img

help:
	@echo "make             build floppy.img"
	@echo "make qemu        boot it"
	@echo "make boot-test   boot a stub kernel, check the boot path"
	@echo "make kernel-test boot the real kernel, check it reaches kernel_main"
	@echo "make test        both of the above"
	@echo "make clean"
	@echo ""
	@echo "using CC=$(CC) $(CCTARGET), LD=$(LD)"
