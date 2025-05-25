# --------------------------------------------------
# Makefile for MaxOS
# Description: Builds the bootloader, kernel, and floppy image.
# --------------------------------------------------

# Default target: build full floppy image
all: floppy.img

# Assemble bootloader to binary
bin/boot.bin: bootloader/boot.asm
	nasm -f bin bootloader/boot.asm -o bin/boot.bin

# Compile kernel C code to object file
build/kernel.o: kernel/kernel.c
	gcc -ffreestanding -c kernel/kernel.c -o build/kernel.o

# Link kernel object to flat binary
bin/kernel.bin: build/kernel.o kernel/linker.ld
	ld -o bin/kernel.bin -Ttext 0x1000 build/kernel.o --oformat binary

# Create floppy image with bootloader and kernel
floppy.img: bin/boot.bin bin/kernel.bin
	dd if=/dev/zero of=floppy.img bs=512 count=2880
	dd if=bin/boot.bin of=floppy.img conv=notrunc bs=512 count=1
	dd if=bin/kernel.bin of=floppy.img conv=notrunc bs=512 seek=1

# Run floppy image in QEMU
qemu: floppy.img
	qemu-system-i386 -fda floppy.img -boot a

# Remove build artifacts
clean:
	rm -rf bin/*
	rm -rf build/*
	rm -f floppy.img