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
# first; the bootloader jumps to 0x1000 with no idea what's there.
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
           -fno-stack-protector -Wall -Wextra -O2 $(CFLAGS_EXTRA)

SECTORS := 32          # keep in sync with KERNEL_SECTOR_COUNT in boot.asm

# kernel.o first so _start lands at the front even before link.ld sorts it
COBJS   := build/kernel.o build/serial.o build/panic.o build/idt.o
OBJS    := $(COBJS) build/isr.o

.PHONY: all clean qemu boot-test kernel-test test help
.DEFAULT_GOAL := all

all: floppy.img

bin build:
	mkdir -p $@

# boot.asm %includes the rest, so rebuild on any of them
bin/boot.bin: bootloader/boot.asm $(wildcard bootloader/*.asm) | bin
	$(ASM) -f bin $< -o $@

build/%.o: kernel/%.c $(wildcard kernel/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/isr.o: kernel/isr.asm | build
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
		&& echo "kernel-test: PASS" \
		|| { echo "kernel-test: FAIL"; cat bin/kernel-test.log 2>/dev/null; exit 1; }

# Deliberately fault and check the handler says so instead of resetting.
# Covers both stub shapes: 0 and 6 push a dummy error code, 13 gets a real one
# from the CPU. If those two paths disagree the frame is misaligned and every
# register in the dump is wrong.
fault-test:
	@for v in 0 6 13; do \
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

test: boot-test kernel-test fault-test

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
