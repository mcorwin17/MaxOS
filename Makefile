# MaxOS
#
# Needs an i686-elf cross toolchain. See build.sh for the prefix I use.

TARGET  := i686-elf
CC      := $(TARGET)-gcc
LD      := $(TARGET)-ld
ASM     := nasm

# Don't let a host compiler anywhere near this. Host objects link fine and
# aren't x86 machine code, which is a miserable way to lose an evening.
ifeq ($(findstring -elf-,$(CC)),)
$(error CC=$(CC) is not a cross compiler, expected $(TARGET)-gcc)
endif

# -ffunction-sections so _start lands in .text._start and link.ld can put it
# first; the bootloader jumps to 0x1000 blind.
CFLAGS  := -m32 -ffreestanding -nostdlib -fno-pic -ffunction-sections \
           -fno-stack-protector -Wall -Wextra -O2

SECTORS := 32          # keep in sync with KERNEL_SECTOR_COUNT in boot.asm

.PHONY: all clean qemu help
.DEFAULT_GOAL := all

all: floppy.img

bin build:
	mkdir -p $@

# boot.asm %includes the rest, so rebuild on any of them
bin/boot.bin: bootloader/boot.asm $(wildcard bootloader/*.asm) | bin
	$(ASM) -f bin $< -o $@

build/kernel.o: kernel/kernel.c | build
	$(CC) $(CFLAGS) -c $< -o $@

bin/kernel.bin: build/kernel.o kernel/link.ld | bin
	$(LD) -m elf_i386 -T kernel/link.ld -o $@ $<

floppy.img: bin/boot.bin bin/kernel.bin
	@test $$(stat -c%s bin/boot.bin) -eq 512 || \
		{ echo "boot.bin is not 512 bytes"; exit 1; }
	@test $$(stat -c%s bin/kernel.bin) -le $$((512 * $(SECTORS))) || \
		{ echo "kernel > $(SECTORS) sectors, bump KERNEL_SECTOR_COUNT"; exit 1; }
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=bin/boot.bin   of=$@ conv=notrunc bs=512 count=1 status=none
	dd if=bin/kernel.bin of=$@ conv=notrunc bs=512 seek=1 status=none

qemu: floppy.img
	qemu-system-i386 -fda floppy.img -boot a

clean:
	rm -rf bin build floppy.img

help:
	@echo "make        build floppy.img"
	@echo "make qemu   boot it"
	@echo "make clean"
